// Receiver: event decoding, ACK, dedup, state -> LEDs, LCD text.
#include <string.h>

#include "../src/event_parser.h"
#include "../src/lcd_view.h"
#include "../src/rx_state.h"
#include "test_framework.h"

using namespace rx;

static ParseResult parse(const char* s, Event& e) { return parseEvent(s, strlen(s), e); }

// Example from docs/03_lora_protocol.md (real encoder output).
static const char* kAlert =
    "{\"v\":1,\"type\":\"ALERT\",\"device\":\"KITCHEN_01\",\"dst\":\"GATEWAY_01\",\"event_id\":458764,"
    "\"category\":\"AIR_QUALITY\",\"severity\":\"HIGH\",\"prev\":\"MEDIUM\",\"sensor\":\"MQ135\",\"raw_adc\":2850,"
    "\"air_index\":85,\"temperature\":32.5,\"humidity\":68,\"timestamp\":123456}";

TEST(rx_parses_doc_alert) {
  Event e;
  CHECK(parse(kAlert, e) == ParseResult::Ok);
  CHECK(!e.recovery);
  CHECK_STR(e.device, "KITCHEN_01");
  CHECK_STR(e.dst, "GATEWAY_01");
  CHECK_EQ(e.eventId, 458764u);
  CHECK(e.category == Cat::Air);
  CHECK(e.severity == Sev::High);
  CHECK(e.hasAir && e.airIndex == 85);
  CHECK(e.hasTemp); CHECK_NEAR(e.temperature, 32.5, 1e-4);
  CHECK(e.hasHum); CHECK_NEAR(e.humidity, 68, 1e-4);
}

TEST(rx_parses_recovery_and_system_flags) {
  Event e;
  CHECK(parse("{\"type\":\"RECOVERY\",\"device\":\"KITCHEN_01\",\"dst\":\"GATEWAY_01\",\"event_id\":7,"
              "\"category\":\"HUMIDITY\",\"severity\":\"NORMAL\",\"timestamp\":5}", e) == ParseResult::Ok);
  CHECK(e.recovery && e.severity == Sev::Normal && e.category == Cat::Hum);
  CHECK(!e.hasTemp && !e.hasAir);
  CHECK(parse("{\"type\":\"ALERT\",\"device\":\"KITCHEN_01\",\"event_id\":8,\"category\":\"SYSTEM\","
              "\"severity\":\"LOW\",\"flags\":16,\"note\":\"REMINDER\"}", e) == ParseResult::Ok);
  CHECK(e.category == Cat::System && e.hasFlags && e.flags == 16 && e.reminder);
}

TEST(rx_rejects_bad_frames) {
  Event e;
  CHECK(parse("", e) == ParseResult::NotJson);
  CHECK(parse("hello", e) == ParseResult::NotJson);
  CHECK(parse("{\"type\":\"ACK\",\"device\":\"KITCHEN_01\",\"ack_for\":3}", e) == ParseResult::NotEvent);
  CHECK(parse("{\"type\":\"ALERT\",\"device\":\"KITCHEN_01\",\"category\":\"SYSTEM\",\"severity\":\"LOW\"}", e) ==
        ParseResult::MissingField);  // no event_id
  CHECK(parse("{\"type\":\"ALERT\",\"device\":\"KITCHEN_01\",\"event_id\":1,\"category\":\"FIRE\",\"severity\":\"LOW\"}", e) ==
        ParseResult::BadValue);
  // A key name inside a string value must not be taken as the key.
  CHECK(parse("{\"type\":\"ALERT\",\"device\":\"event_id\",\"event_id\":9,\"category\":\"SYSTEM\",\"severity\":\"LOW\"}", e) ==
        ParseResult::Ok);
  CHECK_EQ(e.eventId, 9u);
}

TEST(rx_builds_ack_the_node_accepts) {
  char b[96];
  const size_t n = buildAck(b, sizeof b, "GATEWAY_01", "KITCHEN_01", 458764);
  CHECK(n > 0);
  CHECK_STR(b, "{\"type\":\"ACK\",\"device\":\"GATEWAY_01\",\"dst\":\"KITCHEN_01\",\"ack_for\":458764}");
  CHECK_EQ(buildAck(b, 10, "GATEWAY_01", "KITCHEN_01", 1), 0u);
}

TEST(rx_dedup_retransmission) {
  SeenIds s;
  CHECK(!s.checkAndInsert(5));
  CHECK(s.checkAndInsert(5));
  for (uint32_t i = 100; i < 120; ++i) s.checkAndInsert(i);  // ring of 16 overflows
  CHECK(!s.checkAndInsert(5));
}

TEST(rx_state_drives_leds) {
  ReceiverState st;
  CHECK(st.led() == Led::WaitingBlink);
  Event e;
  CHECK(parse(kAlert, e) == ParseResult::Ok);
  st.apply(e, 1000);
  CHECK(st.led() == Led::Red);
  CHECK(st.worst() == Cat::Air);
  Event h;
  parse("{\"type\":\"ALERT\",\"device\":\"KITCHEN_01\",\"event_id\":2,\"category\":\"HUMIDITY\",\"severity\":\"LOW\"}", h);
  st.apply(h, 2000);
  CHECK(st.led() == Led::Red);   // HIGH still dominates
  Event r;
  parse("{\"type\":\"RECOVERY\",\"device\":\"KITCHEN_01\",\"event_id\":3,\"category\":\"AIR_QUALITY\",\"severity\":\"NORMAL\"}", r);
  st.apply(r, 3000);
  CHECK(st.led() == Led::Yellow);  // humidity LOW left
  CHECK(st.worst() == Cat::Hum);
  Event r2;
  parse("{\"type\":\"RECOVERY\",\"device\":\"KITCHEN_01\",\"event_id\":4,\"category\":\"HUMIDITY\",\"severity\":\"NORMAL\"}", r2);
  st.apply(r2, 4000);
  CHECK(st.led() == Led::Green);
}

TEST(rx_lcd_lines_fit_16_columns) {
  ReceiverState st;
  char l1[17], l2[17];
  formatLine1(st, 0, 0, l1);
  formatLine2(st, false, 0, 0, 0, l2);
  CHECK_STR(l1, "AirGuard RX     ");
  CHECK_STR(l2, "Aguardando LoRa ");
  Event e;
  parse(kAlert, e);
  st.apply(e, 0);
  formatLine1(st, 0, 0, l1);
  formatLine2(st, false, -45, 0, 0, l2);
  CHECK_STR(l1, "HIGH: GAS       ");
  CHECK_STR(l2, "32.5C 68% 4.25V ");   // air_index 85 / 20 = 4.25 V
  formatLine2(st, true, -45, 0, 0, l2);
  CHECK_STR(l2, "RSSI -45dBm #1  ");
  CHECK_EQ(strlen(l1), 16u);
}

TEST(rx_heartbeat_resyncs_levels_and_values) {
  Heartbeat h;
  const char* f = "{\"v\":1,\"type\":\"HB\",\"device\":\"KITCHEN_01\",\"dst\":\"GATEWAY_01\",\"seq\":12,"
                  "\"levels\":\"NNLH\",\"air_index\":5,\"temperature\":25.4,\"humidity\":61,\"timestamp\":60}";
  CHECK(parseHeartbeat(f, strlen(f), h) == ParseResult::Ok);
  CHECK_EQ(h.seq, 12u);
  CHECK(h.levels[2] == Sev::Low && h.levels[3] == Sev::High);
  Event e;
  CHECK(parseEvent(f, strlen(f), e) == ParseResult::NotEvent);  // HB is not an event
  ReceiverState st;
  st.apply(h, 1000);
  CHECK(st.led(1000, 15000) == Led::Red);   // SYSTEM HIGH from the levels
  CHECK_NEAR(st.temperature(), 25.4, 1e-3);
  const char* bad = "{\"type\":\"HB\",\"device\":\"KITCHEN_01\",\"seq\":1,\"levels\":\"NNX\"}";
  CHECK(parseHeartbeat(bad, strlen(bad), h) == ParseResult::BadValue);
}

TEST(rx_lost_signal_after_timeout) {
  ReceiverState st;
  Heartbeat h;
  const char* f = "{\"type\":\"HB\",\"device\":\"KITCHEN_01\",\"seq\":1,\"levels\":\"NNNN\"}";
  CHECK(parseHeartbeat(f, strlen(f), h) == ParseResult::Ok);
  st.apply(h, 1000);
  CHECK(st.led(15000, 15000) == Led::Green);
  CHECK(st.led(16001, 15000) == Led::LostBlink);
  char l1[17], l2[17];
  formatLine1(st, 26000, 15000, l1);
  formatLine2(st, false, 0, 26000, 15000, l2);
  CHECK_STR(l1, "SEM SINAL       ");
  CHECK_STR(l2, "ultimo ha 25s   ");
}

// ---- telemetry lines (AirGuard Web) ------------------------------------------
#include "../src/rx_telemetry.h"

TEST(rx_telemetry_line_format) {
  rx::TlmLine t("ack", 42);
  t.num("id", 7);
  t.boolean("ok", true);
  CHECK(t.finish() > 0);
  CHECK_STR(t.c_str(), "@T {\"src\":\"gw\",\"k\":\"ack\",\"ms\":42,\"id\":7,\"ok\":true}\r\n");
}

TEST(rx_telemetry_frame_escaped_and_bounded) {
  const char f[] = "{\"type\":\"HB\"}\nJUNK";
  rx::TlmLine t("rx", 1);
  t.strn("frame", f, 14);  // includes the '\n'
  CHECK(t.finish() > 0);
  CHECK(strstr(t.c_str(), "\"frame\":\"{\\\"type\\\":\\\"HB\\\"}\\u000a\"}"));
  CHECK(!strstr(t.c_str(), "JUNK"));
}

TEST(rx_telemetry_overflow_gives_empty_line) {
  rx::TlmLine t("rx", 1);
  char big[800];
  memset(big, 'a', sizeof big);
  t.strn("frame", big, sizeof big);
  CHECK_EQ(t.finish(), 0);
  CHECK_STR(t.c_str(), "");
}

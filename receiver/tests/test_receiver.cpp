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
  const size_t n = buildAck(b, sizeof b, "GATEWAY_01", "KITCHEN_01", 458764, -31, 9);
  CHECK(n > 0);
  CHECK_STR(b, "{\"type\":\"ACK\",\"device\":\"GATEWAY_01\",\"dst\":\"KITCHEN_01\",\"ack_for\":458764,"
               "\"rssi\":-31,\"snr\":9}");
  CHECK_EQ(buildAck(b, 10, "GATEWAY_01", "KITCHEN_01", 1, 0, 0), 0u);
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

TEST(rx_green_led_blinks_fast_while_a_message_is_on_the_lcd) {
  // no message: exactly as before (steady in NORMAL, 1 Hz before the first frame, off otherwise)
  CHECK(greenLed(Led::Green, false, 0, 500, 125));
  CHECK(greenLed(Led::Green, false, 777, 500, 125));
  CHECK(greenLed(Led::WaitingBlink, false, 0, 500, 125));
  CHECK(!greenLed(Led::WaitingBlink, false, 500, 500, 125));
  CHECK(greenLed(Led::WaitingBlink, false, 1000, 500, 125));
  CHECK(!greenLed(Led::Yellow, false, 0, 500, 125));
  CHECK(!greenLed(Led::Red, false, 0, 500, 125));
  CHECK(!greenLed(Led::LostBlink, false, 0, 500, 125));
  // message on the LCD: 4 Hz, whatever the green LED was doing
  const Led normal[] = {Led::Green, Led::WaitingBlink, Led::LostBlink};
  for (Led l : normal) {
    CHECK(greenLed(l, true, 0, 500, 125));
    CHECK(!greenLed(l, true, 125, 500, 125));
    CHECK(greenLed(l, true, 250, 500, 125));
    CHECK(!greenLed(l, true, 499, 500, 125));
  }
  // 5 s of message = 20 flashes
  int flashes = 0;
  bool prev = false;
  for (uint32_t t = 0; t < 5000; ++t) {
    const bool on = greenLed(Led::Green, true, t, 500, 125);
    if (on && !prev) ++flashes;
    prev = on;
  }
  CHECK_EQ(flashes, 20);
  // an alert owns the LEDs: green stays off even if a message were still marked as shown
  CHECK(!greenLed(Led::Yellow, true, 0, 500, 125));
  CHECK(!greenLed(Led::Red, true, 0, 500, 125));
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

// ---- receiver telemetry for the node (STAT) ------------------------------------
TEST(rx_hb_tracker_gaps_and_reboot) {
  HbTracker t;
  CHECK(!t.any());
  t.onHeartbeat(10);
  CHECK_EQ(t.map(), 1u);
  t.onHeartbeat(11);
  t.onHeartbeat(11);           // same frame twice: ignored
  t.onHeartbeat(14);           // 12 and 13 lost
  CHECK_EQ(t.lastSeq(), 14u);
  CHECK_EQ(t.map(), 0b11001u); // 14, (13), (12), 11, 10
  CHECK_EQ(t.received(), 3u);
  CHECK_EQ(t.missed(), 2u);
  t.onHeartbeat(100);          // long gap: only the newest is known
  CHECK_EQ(t.map(), 1u);
  t.onHeartbeat(1);            // node rebooted
  CHECK_EQ(t.lastSeq(), 1u);
  CHECK_EQ(t.map(), 1u);
  CHECK_EQ(t.missed(), 2u + 85u);
}

TEST(rx_stat_frame_fits_the_radio) {
  GwStat st;
  st.seq = 4000000000u; st.map = 0xFFFFFFFFu; st.hbs = 4000000000u; st.miss = 4000000000u;
  st.events = 4000000000u; st.dups = 4000000000u; st.bad = 4000000000u; st.upS = 4000000000u;
  st.rssi = -120; st.snr = -20; st.lcd = st.lora = st.lost = true;
  char b[240];
  const size_t n = buildStat(b, sizeof b, "GATEWAY_01", "KITCHEN_01", st);
  CHECK(n > 0 && n < 240);
  CHECK(strstr(b, "\"type\":\"STAT\""));
  CHECK(strstr(b, "\"rssi\":-120,\"snr\":-20"));
}

// ---- WhatsApp message relayed by the node (TEXT frame) -----------------------
static ParseResult parseTxt(const char* s, TextMsg& t) { return parseText(s, strlen(s), t); }

TEST(rx_parses_text_frame) {
  TextMsg t;
  CHECK(parseTxt("{\"type\":\"TEXT\",\"device\":\"KITCHEN_01\",\"dst\":\"GATEWAY_01\",\"message_id\":7,"
                 "\"text\":\"Maria: Chego em 10 min\"}", t) == ParseResult::Ok);
  CHECK_STR(t.device, "KITCHEN_01");
  CHECK_STR(t.dst, "GATEWAY_01");
  CHECK_EQ(t.id, 7u);
  CHECK_STR(t.text, "Maria: Chego em 10 min");
  // escapes written by the node's JSON writer; a key name inside the text is not a key
  CHECK(parseTxt("{\"type\":\"TEXT\",\"device\":\"KITCHEN_01\",\"message_id\":4294967295,"
                 "\"text\":\"a \\\"b\\\" \\\\ c\\u0041 ,\\\"text\\\":\\\"x\"}", t) == ParseResult::Ok);
  CHECK_EQ(t.id, 4294967295u);
  CHECK_STR(t.dst, "");
  CHECK_STR(t.text, "a \"b\" \\ cA ,\"text\":\"x");
}

TEST(rx_text_frame_rejections) {
  TextMsg t;
  Event e;
  Heartbeat h;
  const char* txt = "{\"type\":\"TEXT\",\"device\":\"KITCHEN_01\",\"message_id\":1,\"text\":\"oi\"}";
  CHECK(parseTxt(txt, t) == ParseResult::Ok);
  // the other parsers pass a TEXT on, and parseText passes the other frames on
  CHECK(parseEvent(txt, strlen(txt), e) == ParseResult::NotEvent);
  CHECK(parseHeartbeat(txt, strlen(txt), h) == ParseResult::NotEvent);
  CHECK(parseTxt(kAlert, t) == ParseResult::NotEvent);
  CHECK(parseTxt("oi", t) == ParseResult::NotJson);
  CHECK(parseTxt("{\"type\":\"TEXT\",\"device\":\"KITCHEN_01\",\"message_id\":1}", t) == ParseResult::MissingField);
  CHECK(parseTxt("{\"type\":\"TEXT\",\"message_id\":1,\"text\":\"oi\"}", t) == ParseResult::MissingField);
  CHECK(parseTxt("{\"type\":\"TEXT\",\"device\":\"KITCHEN_01\",\"text\":\"oi\"}", t) == ParseResult::MissingField);
  CHECK(parseTxt("{\"type\":\"TEXT\",\"device\":\"KITCHEN_01\",\"message_id\":1,\"text\":\"\"}", t) == ParseResult::BadValue);
  CHECK(parseTxt("{\"type\":\"TEXT\",\"device\":\"KITCHEN_01\",\"message_id\":1,\"text\":\"a\\q\"}", t) ==
        ParseResult::MissingField);  // invalid escape
  CHECK_STR(t.text, "");
}

TEST(rx_lcd_message_one_page_stays_5_seconds) {
  LcdMessage m;
  char l1[17], l2[17];
  CHECK(!m.active(0));
  CHECK(m.show("Maria: Chego em 10 min", 1000, 5000, 2));
  CHECK_EQ(m.pages(), 1);
  CHECK(!m.cut());
  m.lines(1000, l1, l2);
  CHECK_STR(l1, "Maria: Chego em ");
  CHECK_STR(l2, "10 min          ");
  CHECK(m.active(1000) && m.active(5999));
  m.lines(5999, l1, l2);
  CHECK_STR(l1, "Maria: Chego em ");            // same page for the whole time
  CHECK(!m.active(6000));                        // at most 5 s
}

TEST(rx_lcd_message_two_pages_share_the_time) {
  LcdMessage m;
  char l1[17], l2[17];
  // 16 + 16 + 16 + 5 characters once wrapped
  CHECK(m.show("AAAAAAAAAAAAAAAA BBBBBBBBBBBBBBBB CCCCCCCCCCCCCCCC DDDDD", 0, 5000, 2));
  CHECK_EQ(m.pages(), 2);
  CHECK(!m.cut());
  m.lines(0, l1, l2);
  CHECK_STR(l1, "AAAAAAAAAAAAAAAA");
  CHECK_STR(l2, "BBBBBBBBBBBBBBBB");
  m.lines(2499, l1, l2);
  CHECK_STR(l1, "AAAAAAAAAAAAAAAA");
  m.lines(2500, l1, l2);
  CHECK_STR(l1, "CCCCCCCCCCCCCCCC");
  CHECK_STR(l2, "DDDDD           ");
  CHECK(m.active(4999));
  CHECK(!m.active(5000));
}

TEST(rx_lcd_message_longer_text_is_cut_with_dots) {
  LcdMessage m;
  char l1[17], l2[17];
  CHECK(m.show("um dois tres quatro cinco seis sete oito nove dez onze doze treze catorze quinze "
               "dezesseis dezessete dezoito dezenove vinte", 0, 5000, 2));
  CHECK_EQ(m.pages(), 2);
  CHECK(m.cut());
  m.lines(0, l1, l2);
  CHECK_STR(l1, "um dois tres    ");
  CHECK_STR(l2, "quatro cinco    ");
  m.lines(4000, l1, l2);
  CHECK_STR(l1, "seis sete oito  ");
  CHECK_STR(l2, "nove dez onze...");
  // a word longer than a line is split, not lost
  CHECK(m.show("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0, 5000, 2));
  m.lines(0, l1, l2);
  CHECK_STR(l1, "ABCDEFGHIJKLMNOP");
  CHECK_STR(l2, "QRSTUVWXYZ      ");
  CHECK(!m.cut());
}

TEST(rx_lcd_message_appears_twice_with_the_normal_screen_in_between) {
  LcdMessage m;
  char l1[17], l2[17];
  // one page: 5 s, normal screen for 2 s, 5 s again, then over
  CHECK(m.show("Maria: Chego em 10 min", 1000, 5000, 2, 2, 2000));
  CHECK(m.active(1000) && m.active(5999));
  CHECK_EQ(m.appearance(1000), 1);
  CHECK(!m.active(6000) && !m.active(7999));       // gap: the node's data is back
  CHECK(m.active(8000) && m.active(12999));
  CHECK_EQ(m.appearance(8000), 2);
  m.lines(8000, l1, l2);
  CHECK_STR(l1, "Maria: Chego em ");
  CHECK_STR(l2, "10 min          ");
  CHECK(!m.active(13000));                          // only twice
  CHECK(!m.active(15000) && !m.active(60000));
  // two pages: each appearance starts again at the first page
  CHECK(m.show("AAAAAAAAAAAAAAAA BBBBBBBBBBBBBBBB CCCCCCCCCCCCCCCC DDDDD", 0, 5000, 2, 2, 2000));
  m.lines(0, l1, l2);
  CHECK_STR(l1, "AAAAAAAAAAAAAAAA");
  m.lines(2500, l1, l2);
  CHECK_STR(l1, "CCCCCCCCCCCCCCCC");
  m.lines(7000, l1, l2);
  CHECK_STR(l1, "AAAAAAAAAAAAAAAA");
  m.lines(9499, l1, l2);
  CHECK_STR(l1, "AAAAAAAAAAAAAAAA");
  m.lines(9500, l1, l2);
  CHECK_STR(l1, "CCCCCCCCCCCCCCCC");
  CHECK(m.active(11999) && !m.active(12000));
  // an alert in the gap cancels the second appearance too
  CHECK(m.show("oi", 0, 5000, 2, 2, 2000));
  m.cancel();
  CHECK(!m.active(7500));
  // a new message replaces the one waiting for its second appearance
  CHECK(m.show("primeira", 0, 5000, 2, 2, 2000));
  CHECK(m.show("segunda", 6000, 5000, 2, 2, 2000));
  CHECK(m.active(6000));
  m.lines(6000, l1, l2);
  CHECK_STR(l1, "segunda         ");
  CHECK(!m.active(11000) && m.active(13000) && !m.active(18000));
  // millis() wrap-around across the two appearances
  CHECK(m.show("oi", 0xFFFFF000u, 5000, 2, 2, 2000));
  CHECK(m.active(0x00000100u));                     // 4.35 s in
  CHECK(!m.active(0x00000800u));                    // 6.1 s in: gap
  CHECK(m.active(0x00001800u));                     // 10.2 s in: second time
  CHECK(!m.active(0x00002F00u));                    // 16 s in: over
}

TEST(rx_lcd_message_cancel_and_empty) {
  LcdMessage m;
  CHECK(!m.show("    ", 0, 5000, 2));           // nothing printable
  CHECK(!m.active(0));
  CHECK(!m.show(nullptr, 0, 5000, 2));
  CHECK(m.show("oi", 0, 5000, 2));
  m.cancel();                                    // an alert arrived
  CHECK(!m.active(1));
  // millis() wrap-around
  CHECK(m.show("oi", 0xFFFFF000u, 5000, 2));
  CHECK(m.active(0x00000100u));
  CHECK(!m.active(0x00000400u));
}

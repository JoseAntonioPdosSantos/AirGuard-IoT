// Message protocol: encoding, size limits, parsing and validation.
#include <string.h>

#include "../../src/communication/message_protocol.h"
#include "test_framework.h"

using namespace proto;

static ParseError P(const char* s, IncomingMessage& m) {
  return parse(reinterpret_cast<const uint8_t*>(s), strlen(s), m);
}

TEST(encode_alert_contains_spec_fields) {
  AlertEvent e;
  e.category = Category::AirQuality;
  e.severity = Severity::High;
  e.previous = Severity::Medium;
  e.eventId = 1024;
  e.uptimeS = 123456;
  e.snapshot.airValid = e.snapshot.airCalibrated = true;
  e.snapshot.airWarmingUp = false;
  e.snapshot.rawAdc = 2850;
  e.snapshot.airIndex = 85.2f;
  e.snapshot.envValid = true;
  e.snapshot.temperature = 32.5f;
  e.snapshot.humidity = 68;
  char buf[256];
  size_t n = encodeEvent(e, "KITCHEN_01", "GATEWAY_01", buf, sizeof buf);
  CHECK(n > 0 && n <= cfg::LORA_MAX_PAYLOAD);
  CHECK(strstr(buf, "\"type\":\"ALERT\""));
  CHECK(strstr(buf, "\"device\":\"KITCHEN_01\""));
  CHECK(strstr(buf, "\"event_id\":1024"));
  CHECK(strstr(buf, "\"category\":\"AIR_QUALITY\""));
  CHECK(strstr(buf, "\"severity\":\"HIGH\""));
  CHECK(strstr(buf, "\"sensor\":\"MQ135\""));
  CHECK(strstr(buf, "\"raw_adc\":2850"));
  CHECK(strstr(buf, "\"air_index\":85"));
  CHECK(strstr(buf, "\"temperature\":32.5"));
  CHECK(strstr(buf, "\"humidity\":68"));
  CHECK(strstr(buf, "\"timestamp\":123456"));
  CHECK_EQ(strlen(buf), n);
}

TEST(encode_recovery_and_negative_temperature) {
  AlertEvent e;
  e.kind = EventKind::Recovery;
  e.category = Category::Temperature;
  e.severity = Severity::Normal;
  e.previous = Severity::High;
  e.snapshot.envValid = true;
  e.snapshot.temperature = -3.25f;
  e.snapshot.humidity = 40;
  char buf[256];
  CHECK(encodeEvent(e, "KITCHEN_01", "GATEWAY_01", buf, sizeof buf) > 0);
  CHECK(strstr(buf, "\"type\":\"RECOVERY\""));
  CHECK(strstr(buf, "\"temperature\":-3.3") || strstr(buf, "\"temperature\":-3.2"));
  CHECK(!strstr(buf, "air_index"));  // not usable -> omitted, never faked
}

TEST(encode_worst_case_fits_radio_limit) {
  AlertEvent e;
  e.category = Category::Temperature;
  e.severity = Severity::Medium;
  e.eventId = 0xFFFFFFFFu;
  e.uptimeS = 0xFFFFFFFFu;
  e.reminder = true;
  e.snapshot.airValid = e.snapshot.airCalibrated = true;
  e.snapshot.airWarmingUp = false;
  e.snapshot.rawAdc = 4095;
  e.snapshot.airIndex = 100;
  e.snapshot.envValid = true;
  e.snapshot.temperature = -40;
  e.snapshot.humidity = 100;
  e.snapshot.heatIndex = 55.5f;
  char buf[400];
  size_t n = encodeEvent(e, "ABCDEFGHIJKLMNOPQRSTUVW", "ABCDEFGHIJKLMNOPQRSTUVW", buf, sizeof buf);
  CHECK(n > 0);
  CHECK(n <= cfg::LORA_MAX_PAYLOAD);  // 240 B (RYLR998)
}

TEST(encode_overflow_returns_zero) {
  char buf[20];
  CHECK_EQ(encodeAck("KITCHEN_01", "WHATSAPP_GATEWAY", 205, buf, sizeof buf), 0);
}

TEST(encode_ack) {
  char buf[128];
  size_t n = encodeAck("KITCHEN_01", "WHATSAPP_GATEWAY", 205, buf, sizeof buf);
  CHECK(n > 0);
  CHECK_STR(buf, "{\"v\":1,\"type\":\"ACK\",\"device\":\"KITCHEN_01\",\"dst\":\"WHATSAPP_GATEWAY\",\"ack_for\":205}");
}

TEST(parse_text_from_spec) {
  IncomingMessage m;
  const char* s =
      "{\"type\":\"TEXT\",\"source\":\"WHATSAPP_GATEWAY\",\"message_id\":205,"
      "\"text\":\"Aten\xc3\xa7\xc3\xa3o: verifique a cozinha.\"}";
  CHECK(P(s, m) == ParseError::Ok);
  CHECK(m.type == MsgType::Text);
  CHECK_STR(m.sender, "WHATSAPP_GATEWAY");
  CHECK_EQ(m.id, 205);
  CHECK_STR(m.text, "Aten\xc3\xa7\xc3\xa3o: verifique a cozinha.");
  CHECK(sourceAllowed(m.sender));
  CHECK(addressedToUs(m, "KITCHEN_01"));
}

TEST(parse_unicode_escape_and_whitespace) {
  IncomingMessage m;
  CHECK(P(" { \"type\" : \"TEXT\" , \"source\":\"GATEWAY_01\", \"message_id\" : 1 ,"
          " \"text\":\"Aten\\u00e7\\u00e3o\\n!\" , \"extra\":[1] }", m) == ParseError::Nested);
  CHECK(P("{\"type\":\"TEXT\",\"source\":\"GATEWAY_01\",\"message_id\":1,"
          "\"text\":\"Aten\\u00e7\\u00e3o\\n!\",\"x\":true,\"y\":null,\"z\":-1.5e3}", m) == ParseError::Ok);
  CHECK_STR(m.text, "Aten\xc3\xa7\xc3\xa3o !");
}

TEST(parse_ack) {
  IncomingMessage m;
  CHECK(P("{\"type\":\"ACK\",\"device\":\"GATEWAY_01\",\"dst\":\"KITCHEN_01\",\"ack_for\":65537}", m) ==
        ParseError::Ok);
  CHECK(m.type == MsgType::Ack);
  CHECK_EQ(m.id, 65537);
  CHECK(addressedToUs(m, "KITCHEN_01"));
  CHECK(!addressedToUs(m, "KITCHEN_02"));
}

TEST(parse_rejects_bad_input) {
  IncomingMessage m;
  CHECK(P("", m) == ParseError::Empty);
  CHECK(P("not json", m) == ParseError::Malformed);
  CHECK(P("{\"type\":\"TEXT\"", m) == ParseError::Malformed);                       // truncated
  CHECK(P("{\"type\":\"TEXT\",}", m) == ParseError::Malformed);                    // trailing comma
  CHECK(P("{\"type\":\"RUN\",\"source\":\"X\"}", m) == ParseError::UnknownType);   // no commands
  CHECK(P("{\"type\":\"TEXT\",\"source\":\"X\",\"text\":\"a\"}", m) == ParseError::MissingField);
  CHECK(P("{\"type\":\"TEXT\",\"source\":\"X\",\"message_id\":\"5\",\"text\":\"a\"}", m) ==
        ParseError::BadValue);
  CHECK(P("{\"type\":\"TEXT\",\"source\":\"X\",\"message_id\":-5,\"text\":\"a\"}", m) ==
        ParseError::BadValue);
  CHECK(P("{\"type\":\"TEXT\",\"source\":\"X\",\"message_id\":5,\"text\":\"\"}", m) ==
        ParseError::BadValue);
  CHECK(P("{\"type\":\"TEXT\",\"type\":\"ACK\"}", m) == ParseError::Malformed);    // dup key
  CHECK(P("{\"type\":\"TEXT\",\"source\":\"X\",\"message_id\":1,\"text\":\"a\nb\"}", m) ==
        ParseError::Malformed);                                                    // raw ctrl
  CHECK(P("{\"type\":\"ACK\",\"device\":\"G\",\"ack_for\":99999999999}", m) == ParseError::BadValue);
  CHECK(P("{\"type\":\"TEXT\",\"source\":\"X\",\"message_id\":1,\"text\":\"a\"} trailing", m) ==
        ParseError::Malformed);
}

TEST(parse_rejects_oversize_text_and_frame) {
  IncomingMessage m;
  char big[300];
  strcpy(big, "{\"type\":\"TEXT\",\"source\":\"X\",\"message_id\":1,\"text\":\"");
  size_t n = strlen(big);
  for (int i = 0; i < 150; ++i) big[n++] = 'a';
  big[n++] = '"'; big[n++] = '}'; big[n] = '\0';
  CHECK(P(big, m) == ParseError::FieldTooLong);
  uint8_t frame[256];
  memset(frame, ' ', sizeof frame);
  CHECK(parse(frame, sizeof frame, m) == ParseError::TooLong);
}

TEST(parse_rejects_long_identifier) {
  IncomingMessage m;
  CHECK(P("{\"type\":\"ACK\",\"device\":\"ABCDEFGHIJKLMNOPQRSTUVWXYZ\",\"ack_for\":1}", m) ==
        ParseError::FieldTooLong);
}

TEST(source_whitelist) {
  CHECK(sourceAllowed("WHATSAPP_GATEWAY"));
  CHECK(!sourceAllowed("EVIL_NODE"));
}

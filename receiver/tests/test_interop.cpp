// Interop with the REAL node code: node encoder -> receiver parser, and
// receiver ACK -> node parser (src/communication/message_protocol.cpp).
#include <string.h>

#include "../../src/communication/message_protocol.h"
#include "../src/event_parser.h"
#include "test_framework.h"

TEST(interop_node_alert_is_understood_by_receiver) {
  AlertEvent a;
  a.kind = EventKind::Alert;
  a.category = Category::AirQuality;
  a.severity = Severity::High;
  a.previous = Severity::Medium;
  a.eventId = 0x00130004;
  a.uptimeS = 42;
  a.snapshot.airValid = a.snapshot.airCalibrated = true;
  a.snapshot.airWarmingUp = false;
  a.snapshot.airIndex = 62.0f;
  a.snapshot.envValid = true;
  a.snapshot.temperature = 25.4f;
  a.snapshot.humidity = 61.0f;
  char buf[260];
  const size_t n = proto::encodeEvent(a, "KITCHEN_01", "GATEWAY_01", buf, sizeof buf);
  CHECK(n > 0);
  rx::Event e;
  CHECK(rx::parseEvent(buf, n, e) == rx::ParseResult::Ok);
  CHECK_EQ(e.eventId, 0x00130004u);
  CHECK(e.category == rx::Cat::Air && e.severity == rx::Sev::High);
  CHECK(e.hasAir && e.airIndex == 62);
  CHECK_NEAR(e.temperature, 25.4, 0.05);

  a.kind = EventKind::Recovery;
  a.severity = Severity::Normal;
  const size_t m = proto::encodeEvent(a, "KITCHEN_01", "GATEWAY_01", buf, sizeof buf);
  CHECK(rx::parseEvent(buf, m, e) == rx::ParseResult::Ok);
  CHECK(e.recovery && e.severity == rx::Sev::Normal);
}

TEST(interop_receiver_ack_is_accepted_by_node) {
  char ack[96];
  const size_t n = rx::buildAck(ack, sizeof ack, "GATEWAY_01", "KITCHEN_01", 1245188);
  proto::IncomingMessage m;
  CHECK(proto::parse(reinterpret_cast<const uint8_t*>(ack), n, m) == proto::ParseError::Ok);
  CHECK(m.type == proto::MsgType::Ack);
  CHECK_EQ(m.id, 1245188u);
  CHECK(proto::addressedToUs(m, "KITCHEN_01"));
}

TEST(interop_node_heartbeat_is_understood_by_receiver) {
  Severity lv[CATEGORY_COUNT] = {Severity::High, Severity::Normal, Severity::Low, Severity::Normal};
  SensorSnapshot s;
  s.airValid = s.airCalibrated = true;
  s.airWarmingUp = false;
  s.airIndex = 62.0f;
  s.envValid = true;
  s.temperature = 25.4f;
  s.humidity = 61.0f;
  char buf[260];
  const size_t n = proto::encodeHeartbeat("KITCHEN_01", "GATEWAY_01", 7, lv, s, 30, buf, sizeof buf);
  rx::Heartbeat h;
  CHECK(rx::parseHeartbeat(buf, n, h) == rx::ParseResult::Ok);
  CHECK(h.levels[0] == rx::Sev::High && h.levels[2] == rx::Sev::Low);
  CHECK(h.hasAir && h.airIndex == 62);
  CHECK_EQ(h.seq, 7u);
}

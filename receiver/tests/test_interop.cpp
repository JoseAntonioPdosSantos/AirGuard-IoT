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
  a.snapshot.airValid = true;
  a.snapshot.gasVolts = 3.100f;  // air_index 62.0
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
  const size_t n = rx::buildAck(ack, sizeof ack, "GATEWAY_01", "KITCHEN_01", 1245188, -42, -3);
  proto::IncomingMessage m;
  CHECK(proto::parse(reinterpret_cast<const uint8_t*>(ack), n, m) == proto::ParseError::Ok);
  CHECK(m.type == proto::MsgType::Ack);
  CHECK_EQ(m.id, 1245188u);
  CHECK(proto::addressedToUs(m, "KITCHEN_01"));
  CHECK(m.hasLink && m.rssi == -42 && m.snr == -3);
}

TEST(interop_receiver_stat_is_accepted_by_node) {
  rx::GwStat st;
  st.seq = 120; st.map = 0xFFFFFFFDu; st.hbs = 118; st.miss = 2; st.events = 3; st.dups = 1; st.bad = 4;
  st.rssi = -31; st.snr = 9; st.lcd = true; st.lora = true; st.lost = false; st.upS = 610;
  char b[240];
  const size_t n = rx::buildStat(b, sizeof b, "GATEWAY_01", "KITCHEN_01", st);
  proto::IncomingMessage m;
  CHECK(proto::parse(reinterpret_cast<const uint8_t*>(b), n, m) == proto::ParseError::Ok);
  CHECK(m.type == proto::MsgType::Stat);
  CHECK_STR(m.sender, "GATEWAY_01");
  CHECK(proto::addressedToUs(m, "KITCHEN_01"));
  CHECK_EQ(m.stat.seq, 120u);
  CHECK_EQ(m.stat.map, 0xFFFFFFFDu);
  CHECK_EQ(m.stat.hbs, 118u);
  CHECK_EQ(m.stat.miss, 2u);
  CHECK_EQ(m.stat.events, 3u);
  CHECK_EQ(m.stat.dups, 1u);
  CHECK_EQ(m.stat.bad, 4u);
  CHECK_EQ(m.stat.upS, 610u);
  CHECK(m.hasLink && m.rssi == -31 && m.snr == 9);
  CHECK(m.stat.lcd && m.stat.lora && !m.stat.lost);
}

TEST(interop_node_heartbeat_is_understood_by_receiver) {
  Severity lv[CATEGORY_COUNT] = {Severity::High, Severity::Normal, Severity::Low, Severity::Normal};
  SensorSnapshot s;
  s.airValid = true;
  s.gasVolts = 3.100f;  // air_index 62.0
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

TEST(interop_node_text_is_understood_by_receiver) {
  char buf[260];
  const char* msg = "Davi: \"Almoco\" as 12h \\ ok?";
  const size_t n = proto::encodeText("KITCHEN_01", "GATEWAY_01", 42, msg, buf, sizeof buf);
  CHECK(n > 0);
  rx::TextMsg t;
  CHECK(rx::parseText(buf, n, t) == rx::ParseResult::Ok);
  CHECK_EQ(t.id, 42u);
  CHECK_STR(t.device, "KITCHEN_01");
  CHECK_STR(t.dst, "GATEWAY_01");
  CHECK_STR(t.text, msg);
  // the longest frame the node can send still parses, and nothing overflows
  char longTxt[400];
  memset(longTxt, 'z', sizeof longTxt - 1);
  longTxt[sizeof longTxt - 1] = '\0';
  size_t used = 0;
  const size_t m = proto::encodeText("KITCHEN_01", "GATEWAY_01", 4294967295u, longTxt, buf, sizeof buf, &used);
  CHECK_EQ(m, cfg::LORA_MAX_PAYLOAD);
  CHECK(rx::parseText(buf, m, t) == rx::ParseResult::Ok);
  CHECK_EQ(strlen(t.text), used);
  // and the node itself does not take its own relayed text for a command
  rx::Event e;
  CHECK(rx::parseEvent(buf, m, e) == rx::ParseResult::NotEvent);
}

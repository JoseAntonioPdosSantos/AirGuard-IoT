// Telemetry lines for AirGuard Web: prefix, framing, escaping, size limits.
#include <string.h>

#include "../../src/communication/telemetry_codec.h"
#include "test_framework.h"

TEST(telemetry_line_has_prefix_fields_and_crlf) {
  char buf[tlm::SMALL_CAP];
  tlm::Line l(buf, sizeof buf, "node", "ack", 1234);
  l.w().unum("id", 77);
  const size_t n = l.finish();
  CHECK_EQ(n, strlen(buf));
  CHECK_STR(buf, "@T {\"src\":\"node\",\"k\":\"ack\",\"ms\":1234,\"id\":77}\r\n");
}

TEST(telemetry_frame_is_escaped_and_length_bounded) {
  // A real frame is not NUL-terminated: only `len` bytes may be copied.
  const char frame[] = "{\"type\":\"HB\",\"seq\":3}XXXX";
  char buf[tlm::LINE_CAP];
  tlm::Line l(buf, sizeof buf, "node", "tx", 5);
  l.w().strn("frame", frame, 21);
  CHECK(l.finish() > 0);
  CHECK(strstr(buf, "\"frame\":\"{\\\"type\\\":\\\"HB\\\",\\\"seq\\\":3}\"}"));
  CHECK(!strstr(buf, "XXXX"));
}

TEST(telemetry_max_frame_fits_line_cap) {
  char frame[cfg::LORA_MAX_PAYLOAD];
  for (size_t i = 0; i < sizeof frame; ++i) frame[i] = (i % 3) ? 'a' : '"';  // 1/3 quotes
  char buf[tlm::LINE_CAP];
  tlm::Line l(buf, sizeof buf, "node", "tx", 4000000000UL);
  l.w().unum("id", 0xE0000001UL);
  l.w().str("kind", "hb");
  l.w().unum("try", 1);
  l.w().unum("len", sizeof frame);
  l.w().strn("frame", frame, sizeof frame);
  CHECK(l.finish() > 0);
}

TEST(telemetry_overflow_returns_zero_and_empty_line) {
  char buf[24];
  tlm::Line l(buf, sizeof buf, "node", "env", 1);
  l.w().str("x", "this does not fit in 24 bytes");
  CHECK_EQ(l.finish(), 0);
  CHECK_STR(buf, "");
}

TEST(telemetry_env_record) {
  tlm::NodeEnv e;
  e.envValid = true;
  e.temperature = 25.44f;
  e.humidity = 61.2f;
  e.heatIndex = 25.9f;
  e.envState = "ok";
  e.airValid = true;
  e.gasVolts = 0.4219f;
  e.aoVolts = 0.43f;
  e.rawAdc = 512;
  e.airState = "ok";
  e.levels[0] = Severity::Medium;
  e.overall = Severity::Medium;
  e.relayOn = true;
  e.lora = "ok";
  e.uptimeS = 60;
  char buf[tlm::SMALL_CAP];
  const size_t n = tlm::encodeNodeEnv(e, 60000, buf, sizeof buf);
  CHECK(n > 0);
  const char* head = "@T {\"src\":\"node\",\"k\":\"env\",\"ms\":60000,";
  CHECK(strncmp(buf, head, strlen(head)) == 0);
  CHECK(strstr(buf, "\"temp\":25.4,\"rh\":61,\"hi\":25.9"));
  CHECK(strstr(buf, "\"gas\":0.422"));
  CHECK(strstr(buf, "\"lv\":\"MNNN\",\"ov\":\"MEDIUM\",\"relay\":true"));
}

TEST(telemetry_env_omits_invalid_readings) {
  tlm::NodeEnv e;  // nothing valid
  char buf[tlm::SMALL_CAP];
  CHECK(tlm::encodeNodeEnv(e, 1, buf, sizeof buf) > 0);
  CHECK(!strstr(buf, "\"temp\":"));
  CHECK(!strstr(buf, "\"gas\":"));
  CHECK(strstr(buf, "\"air\":\"invalid\""));
}

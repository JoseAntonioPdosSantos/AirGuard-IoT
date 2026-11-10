#include "telemetry_codec.h"

#include <string.h>

namespace tlm {

namespace {
size_t prefixLen() { return strlen(PREFIX); }
}  // namespace

Line::Line(char* buf, size_t cap, const char* src, const char* kind, uint32_t ms)
    : buf_(buf),
      cap_(cap),
      // JSON starts after the prefix and keeps 2 bytes free for "\r\n".
      w_(buf + (cap > prefixLen() + 2 ? prefixLen() : 0),
         cap > prefixLen() + 2 ? cap - prefixLen() - 2 : 0) {
  if (cap_ > prefixLen() + 2) memcpy(buf_, PREFIX, prefixLen());
  w_.str("src", src);
  w_.str("k", kind);
  w_.unum("ms", ms);
}

size_t Line::finish() {
  const size_t n = w_.finish();
  if (n == 0) {
    if (cap_) buf_[0] = '\0';
    return 0;
  }
  size_t len = prefixLen() + n;
  buf_[len++] = '\r';
  buf_[len++] = '\n';
  buf_[len] = '\0';
  return len;
}

void levelLetters(const Severity levels[CATEGORY_COUNT], char out[CATEGORY_COUNT + 1]) {
  static const char kLetter[] = {'N', 'L', 'M', 'H'};
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) out[i] = kLetter[static_cast<uint8_t>(levels[i]) & 3];
  out[CATEGORY_COUNT] = '\0';
}

size_t encodeNodeEnv(const NodeEnv& e, uint32_t ms, char* buf, size_t cap) {
  Line l(buf, cap, "node", "env", ms);
  proto::JsonWriter& w = l.w();
  if (e.envValid) {
    w.flt("temp", e.temperature, 1);
    w.flt("rh", e.humidity, 0);
    w.flt("hi", e.heatIndex, 1);
  }
  w.str("env", e.envState);
  if (e.airValid) w.flt("gas", e.gasVolts, 3);
  w.flt("ao", e.aoVolts, 3);
  w.unum("adc", e.rawAdc);
  w.str("air", e.airState);
  if (e.simulated) w.boolean("sim", true);
  char lv[CATEGORY_COUNT + 1];
  levelLetters(e.levels, lv);
  w.str("lv", lv);
  w.str("ov", severityName(e.overall));
  w.boolean("relay", e.relayOn);
  w.unum("flags", e.systemFlags);
  w.str("lora", e.lora);
  w.unum("q", e.txQueue);
  w.unum("wack", e.waitAck);
  w.unum("up", e.uptimeS);
  return l.finish();
}

}  // namespace tlm

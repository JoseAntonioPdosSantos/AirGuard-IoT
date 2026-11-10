#include "telemetry.h"

#include <Arduino.h>
#include <string.h>

#include "../communication/telemetry_codec.h"
#include "../net/uplink.h"

namespace {
bool g_echo = false;
}

namespace telemetry {

void setEcho(bool on) { g_echo = on; }
bool echo() { return g_echo; }

void write(const char* line, size_t len) {
  if (len == 0) return;
  // The uplink gets the bare JSON object: without "@T " and CRLF.
  const size_t pre = strlen(tlm::PREFIX);
  if (len > pre + 2 && strncmp(line, tlm::PREFIX, pre) == 0) uplink::push(line + pre, len - pre - 2);
  if (g_echo && Serial && Serial.availableForWrite() >= static_cast<int>(len))
    Serial.write(reinterpret_cast<const uint8_t*>(line), len);
}

}  // namespace telemetry

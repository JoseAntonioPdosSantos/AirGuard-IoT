#include "telemetry.h"

#include <Arduino.h>

namespace {
bool g_on = true;
}

namespace telemetry {

void setEnabled(bool on) { g_on = on; }
bool enabled() { return g_on; }

void write(const char* line, size_t len) {
  if (!g_on || len == 0 || !Serial) return;
  if (Serial.availableForWrite() < static_cast<int>(len)) return;  // never block the loop
  Serial.write(reinterpret_cast<const uint8_t*>(line), len);
}

}  // namespace telemetry

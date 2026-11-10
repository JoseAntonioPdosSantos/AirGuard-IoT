// AirGuard IoT - writes telemetry lines (communication/telemetry_codec.h) on
// the USB serial port for the AirGuard Web dashboard. Non-blocking: a line is
// dropped when the TX buffer has no room (no host reading), never delayed.
#pragma once
#include <stddef.h>

namespace telemetry {
void setEnabled(bool on);  // console: "tlm on|off" (default on)
bool enabled();
void write(const char* line, size_t len);
}  // namespace telemetry

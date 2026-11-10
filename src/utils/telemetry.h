// AirGuard IoT - telemetry records for the AirGuard Web dashboard
// (format: communication/telemetry_codec.h, docs/11_telemetry.md).
//
// Every record goes to the Wi-Fi uplink (net/uplink.h). With "tlm on" it is
// also echoed on the USB serial port as an "@T {json}" line (debug only:
// dropped, never delayed, when the TX buffer is full).
#pragma once
#include <stddef.h>

namespace telemetry {
void setEcho(bool on);  // console: "tlm on|off" (default off)
bool echo();
void write(const char* line, size_t len);  // a finished tlm::Line ("@T {...}\r\n")
}  // namespace telemetry

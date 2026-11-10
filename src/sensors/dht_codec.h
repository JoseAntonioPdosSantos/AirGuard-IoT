// AirGuard IoT - DHT11 frame decoder (platform independent, unit tested).
//
// A DHT11 frame is 40 bits = 5 bytes, MSB first:
//   [0] RH integer  [1] RH decimal  [2] T integer  [3] T decimal  [4] checksum
//   checksum = (b0 + b1 + b2 + b3) & 0xFF
//   RH = b0 + b1/10, T = b2 + (b3 & 0x0F)/10, negative if b3 bit 7 is set.
#pragma once
#include <stdint.h>

namespace dht {

// Returns false on checksum error or an all-zero frame (line stuck LOW).
bool decode(const uint8_t frame[5], float& temperatureC, float& humidityPct);

}  // namespace dht

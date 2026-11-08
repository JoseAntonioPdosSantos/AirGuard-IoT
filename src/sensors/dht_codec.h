// AirGuard IoT - DHT11 / DHT22 frame decoder (platform independent, unit tested).
//
// A DHTxx frame is 40 bits = 5 bytes, MSB first:
//   [0] RH high  [1] RH low  [2] T high  [3] T low  [4] checksum
//   checksum = (b0 + b1 + b2 + b3) & 0xFF
// DHT11: RH = b0 + b1/10, T = b2 + (b3 & 0x0F)/10, negative if b3 bit 7 set.
// DHT22: RH = (b0<<8 | b1)/10, T = ((b2 & 0x7F)<<8 | b3)/10, negative if b2 bit 7.
#pragma once
#include <stdint.h>

namespace dht {

enum class Model : uint8_t { DHT11, DHT22 };

// Returns false on checksum error or an all-zero frame (line stuck LOW).
bool decode(const uint8_t frame[5], Model model, float& temperatureC, float& humidityPct);

}  // namespace dht

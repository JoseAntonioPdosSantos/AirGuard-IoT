// AirGuard IoT - DHT11 temperature/humidity driver (pin in config/hardware_pins.h).
//
// DHT11: 0..50 C +-2 C, 20..90 %RH +-5 %. It cannot measure RH < 20 %, so the
// low-humidity HIGH level (<= 20 %) is not reliable with it
// (docs/05_environmental_thresholds.md).
#pragma once
#include <stdint.h>

struct EnvReading {
  bool  ok;
  float temperature;  // C
  float humidity;     // %RH
};

class TemperatureSensor {
 public:
  void begin();
  EnvReading read();  // call at most every cfg::ENV_SAMPLE_PERIOD_MS
  const char* model() const { return "DHT11"; }
};

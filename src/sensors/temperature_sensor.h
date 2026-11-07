// AirGuard IoT - temperature/humidity driver.
//
// Installed sensor: DHT11 module (3.3 V); pin in config/hardware_pins.h.
// Supported at compile time (platformio.ini build flag):
//   -D AIRGUARD_ENV_SENSOR_DHT11   (default env) 0..50 C +-2 C, 20..90 %RH +-5 %
//   -D AIRGUARD_ENV_SENSOR_DHT22   -40..80 C +-0.5 C, 0..100 %RH +-2..5 %
//   -D AIRGUARD_ENV_SENSOR_AHT20   I2C0 shared with the OLED (addr 0x38)
// DHT11 limitation: it cannot measure RH < 20 %, so the low-humidity HIGH
// level (<= 20 %) is not reliable with it (docs/05_environmental_thresholds.md).
#pragma once
#include <stdint.h>

struct EnvReading {
  bool  ok;
  float temperature;  // C
  float humidity;     // %RH
};

class TemperatureSensor {
 public:
  bool begin();
  EnvReading read();       // call at most every cfg::ENV_SAMPLE_PERIOD_MS
  const char* model() const;
};

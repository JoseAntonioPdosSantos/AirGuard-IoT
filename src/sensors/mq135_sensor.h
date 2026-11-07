// AirGuard IoT - MQ-135 hardware access (ADC only). All signal processing is
// in monitoring/air_quality_monitor (platform independent).
#pragma once
#include <stdint.h>

class Mq135Sensor {
 public:
  void begin();
  // Averages cfg::MQ135_OVERSAMPLE conversions (~30 us total, non-blocking).
  uint16_t readRaw();
};

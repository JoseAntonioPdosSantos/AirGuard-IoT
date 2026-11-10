// AirGuard IoT - temperature/humidity validation, fault detection, heat index.
// Platform independent: the hardware driver hands raw readings to addReading().
#pragma once
#include <stdint.h>

class EnvironmentMonitor {
 public:
  // ok=false when the driver failed (timeout, checksum, NaN).
  void addReading(bool ok, float tempC, float rh);

  bool  valid() const { return valid_; }            // last reading valid
  bool  faulted() const;                            // persistent failure
  float temperature() const { return temp_; }
  float humidity() const { return hum_; }
  float heatIndex() const { return heatIndex_; }
  // Apparent temperature used for the "hot" side: max(T, heat index).
  float apparent() const { return heatIndex_ > temp_ ? heatIndex_ : temp_; }

  // Simulation
  void simulateTemperature(bool enable, float t = 0) { simTempOn_ = enable; simTemp_ = t; }
  void simulateHumidity(bool enable, float h = 0) { simHumOn_ = enable; simHum_ = h; }
  void simulateFault(bool enable) { simFault_ = enable; }
  bool simulating() const { return simTempOn_ || simHumOn_ || simFault_; }

 private:
  bool    valid_ = false;
  uint8_t failCount_ = 0;
  float   temp_ = 0, hum_ = 0, heatIndex_ = 0;
  bool    simTempOn_ = false, simHumOn_ = false, simFault_ = false;
  float   simTemp_ = 0, simHum_ = 0;
};

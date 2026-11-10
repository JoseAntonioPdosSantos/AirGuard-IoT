// AirGuard IoT - MQ-135 signal chain (platform independent, unit tested).
//
//   raw ADC -> V(pin) -> V(AO) (divider undone) -> filter (instant attack,
//   median + EMA release) -> alarm voltage, classified by thr::GAS_VOLTS
//
// The MQ-135 is a broad-response metal-oxide sensor: it does NOT identify a
// gas or measure ppm. The AO voltage is a relative response only.
#pragma once
#include <stdint.h>

#include "../config/config.h"
#include "../config/thresholds.h"

struct AirSample {
  bool     valid = false;
  uint16_t raw = 0;        // averaged 12-bit ADC count
  float    vPin = 0;       // at the ESP32 ADC pin
  float    vAo = 0;        // at MQ-135 AO (divider undone), instant
  float    vAlarm = 0;     // filtered AO voltage used by the alarm
  bool     simulated = false;
};

class AirQualityMonitor {
 public:
  // ---- pure conversion helpers (unit tested) ------------------------------
  static float adcToVoltage(uint16_t raw);
  static float pinToSensorVoltage(float vPin);

  void begin(uint32_t nowMs) { bootMs_ = nowMs; }
  // Process one oversampled ADC value.
  void addSample(uint16_t rawAdc);

  bool     warmingUp(uint32_t nowMs) const;
  uint32_t warmupRemainingMs(uint32_t nowMs) const;
  void     skipWarmup() { warmupSkipped_ = true; }

  bool faulted() const { return faultLatched_; }
  // Usable for classification: valid, warmed up, no fault.
  bool usable(uint32_t nowMs) const;
  const AirSample& last() const { return last_; }

  // Simulation (serial console): override the AO voltage and/or raw ADC.
  void simulateVolts(bool enable, float volts = 0);
  void simulateAdc(bool enable, uint16_t raw = 0) { simAdcOn_ = enable; simAdc_ = raw; }
  void simulateFault(bool enable) { simFault_ = enable; }
  bool simulating() const { return simVoltsOn_ || simAdcOn_ || simFault_; }

 private:
  float filter(float vAo);

  uint32_t bootMs_ = 0;
  bool     warmupSkipped_ = false;

  float   window_[mq135::MEDIAN_WINDOW] = {};
  uint8_t windowCount_ = 0, windowPos_ = 0;
  float   ema_ = 0;
  bool    emaInit_ = false;

  uint8_t failCount_ = 0;
  uint8_t okCount_ = 0;
  bool    faultLatched_ = false;
  AirSample last_;

  bool     simVoltsOn_ = false;
  float    simVolts_ = 0;
  bool     simAdcOn_ = false;
  uint16_t simAdc_ = 0;
  bool     simFault_ = false;
};

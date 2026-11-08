// AirGuard IoT - MQ-135 signal chain (platform independent).
//
// raw ADC -> V(adc pin) -> V(AO, divider undone) -> Rs -> median + EMA filter
//         -> Rs/R0 (optionally T/RH compensated) -> air_index (0..100, relative)
//
// The MQ-135 is a broad-response metal-oxide sensor: it does NOT identify a
// gas. air_index is a relative response index versus the calibrated clean-air
// reference R0, never a concentration.
#pragma once
#include <stdint.h>

#include "../config/config.h"
#include "../config/thresholds.h"

struct AirSample {
  bool     valid = false;
  uint16_t raw = 0;
  float    vAdc = 0;
  float    vSensor = 0;
  float    vFiltered = 0;   // AO voltage from the filtered Rs (median + EMA)
  float    rsInstant = 0;
  float    rsFiltered = 0;
  float    ratio = 0;
  float    index = 0;
  bool     simulated = false;
};

enum class CalState : uint8_t { Idle, Running, Done, Failed };
enum class CalError : uint8_t { None, NotReady, Unstable, OutOfRange, TooFewSamples, Aborted };

class AirQualityMonitor {
 public:
  // ---- pure conversion helpers (unit tested) ------------------------------
  static float adcToVoltage(uint16_t raw);
  static float pinToSensorVoltage(float vAdc);
  // Rs = RL * (Vc - Vout) / Vout. Returns <0 if Vout is out of range.
  static float computeRs(float vSensor);
  static float indexFromRatio(float ratio);
  static float compensationFactor(float tempC, float rh);

  void begin(uint32_t nowMs, float r0, bool r0Valid);
  // Voltage mode: index = 100 * filtered AO voltage / Vc; no R0 required.
  void setVoltageMode(bool on) { voltageMode_ = on; }
  bool voltageMode() const { return voltageMode_; }
  static float voltageFromRs(float rs);

  // Process one oversampled ADC value. env* used only for compensation.
  void addSample(uint16_t rawAdc, float tempC, float rh, bool envValid, uint32_t nowMs);

  bool     warmingUp(uint32_t nowMs) const;
  uint32_t warmupRemainingMs(uint32_t nowMs) const;
  void     skipWarmup() { warmupSkipped_ = true; }

  bool  calibrated() const { return r0Valid_; }
  float r0() const { return r0_; }
  bool  setR0(float r0);

  // Non-blocking clean-air calibration (call addSample() as usual).
  bool     startCalibration(uint32_t nowMs);
  void     abortCalibration();
  CalState calState() const { return calState_; }
  CalError calError() const { return calError_; }
  float    calProgress(uint32_t nowMs) const;  // 0..1
  float    calMean() const { return calMean_; }
  float    calCv() const { return calCv_; }
  // Returns true once after a calibration finished OK (to persist R0).
  bool     takeNewR0(float& r0);

  bool faulted() const { return faultLatched_; }
  // Usable for classification: valid, warmed up, calibrated, no fault.
  bool usable(uint32_t nowMs) const;
  const AirSample& last() const { return last_; }

  // Simulation (serial console): override index and/or raw ADC.
  void simulateIndex(bool enable, float index = 0);
  void simulateAdc(bool enable, uint16_t raw = 0);
  void simulateFault(bool enable) { simFault_ = enable; }
  bool simulating() const { return simIndexOn_ || simAdcOn_ || simFault_; }

 private:
  float medianOfWindow() const;
  void  finishCalibration(uint32_t nowMs);

  uint32_t bootMs_ = 0;
  bool     voltageMode_ = false;
  bool     warmupSkipped_ = false;
  float    r0_ = 0;
  bool     r0Valid_ = false;

  float   window_[mq135::MEDIAN_WINDOW] = {};
  uint8_t windowCount_ = 0;
  uint8_t windowPos_ = 0;
  float   ema_ = 0;
  float   vWindow_[mq135::V_MEDIAN_WINDOW] = {};
  uint8_t vCount_ = 0, vPos_ = 0;
  float   vEma_ = 0;
  bool    vEmaInit_ = false;
  bool    emaInit_ = false;
  uint8_t failCount_ = 0;
  uint8_t okCount_ = 0;
  bool    faultLatched_ = false;
  AirSample last_;

  CalState calState_ = CalState::Idle;
  CalError calError_ = CalError::None;
  uint32_t calStartMs_ = 0;
  double   calSum_ = 0, calSumSq_ = 0;
  uint16_t calN_ = 0;
  float    calMean_ = 0, calCv_ = 0;
  bool     newR0_ = false;

  bool     simIndexOn_ = false;
  float    simIndex_ = 0;
  bool     simAdcOn_ = false;
  uint16_t simAdc_ = 0;
  bool     simFault_ = false;
};

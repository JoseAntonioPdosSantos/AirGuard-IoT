#include "air_quality_monitor.h"

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float AirQualityMonitor::adcToVoltage(uint16_t raw) {
  return (static_cast<float>(raw) / mq135::ADC_MAX_COUNTS) * mq135::ADC_VREF;
}

float AirQualityMonitor::pinToSensorVoltage(float vPin) {
  return vPin * (mq135::DIVIDER_R_TOP + mq135::DIVIDER_R_BOTTOM) / mq135::DIVIDER_R_BOTTOM;
}

bool AirQualityMonitor::warmingUp(uint32_t now) const {
  return !warmupSkipped_ && (uint32_t)(now - bootMs_) < cfg::MQ135_WARMUP_MS;
}

uint32_t AirQualityMonitor::warmupRemainingMs(uint32_t now) const {
  if (!warmingUp(now)) return 0;
  return cfg::MQ135_WARMUP_MS - (uint32_t)(now - bootMs_);
}

bool AirQualityMonitor::usable(uint32_t now) const {
  if (simVoltsOn_ && !simFault_) return true;  // bench test without a sensor
  return last_.valid && !faulted() && !warmingUp(now);
}

// Instant attack: a rising voltage is used at once (the gas alarm fires on
// the first sample above the threshold). Release: median + EMA.
float AirQualityMonitor::filter(float v) {
  window_[windowPos_] = v;
  windowPos_ = (windowPos_ + 1) % mq135::MEDIAN_WINDOW;
  if (windowCount_ < mq135::MEDIAN_WINDOW) ++windowCount_;

  float sorted[mq135::MEDIAN_WINDOW] = {};
  for (uint8_t i = 0; i < windowCount_; ++i) sorted[i] = window_[i];
  for (uint8_t i = 1; i < windowCount_; ++i) {  // insertion sort (n <= 9)
    const float key = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > key) { sorted[j + 1] = sorted[j]; --j; }
    sorted[j + 1] = key;
  }
  const float median = sorted[windowCount_ / 2];

  if (!emaInit_) { ema_ = median; emaInit_ = true; }
  else if (v > ema_) ema_ = v;
  else ema_ += mq135::EMA_ALPHA * (median - ema_);
  return ema_;
}

void AirQualityMonitor::addSample(uint16_t rawAdc) {
  if (simAdcOn_) rawAdc = simAdc_;
  AirSample s;
  s.raw = rawAdc;
  s.vPin = adcToVoltage(rawAdc);
  s.vAo = pinToSensorVoltage(s.vPin);
  s.simulated = simAdcOn_;

  const bool floorOk = !cfg::MQ135_FLOOR_IS_FAULT || rawAdc >= cfg::MQ135_ADC_MIN_VALID;
  const bool inRange = s.vAo > 0.001f && s.vAo < mq135::CIRCUIT_VOLTAGE;  // 0 = no reading
  if (simFault_ || !floorOk || !inRange) {
    if (failCount_ < 255) ++failCount_;
    okCount_ = 0;
    if (failCount_ >= cfg::MQ135_FAIL_LIMIT) faultLatched_ = true;
    s.valid = false;
    s.vAlarm = last_.vAlarm;  // kept for display only; never treated as normal
  } else {
    failCount_ = 0;
    if (faultLatched_ && ++okCount_ >= cfg::MQ135_OK_TO_CLEAR) {
      faultLatched_ = false;
      okCount_ = 0;
    }
    s.valid = true;
    s.vAlarm = filter(s.vAo);
  }

  if (simVoltsOn_) {
    s.vAo = s.vAlarm = simVolts_;
    s.simulated = true;
  }
  last_ = s;
}

void AirQualityMonitor::simulateVolts(bool enable, float volts) {
  simVoltsOn_ = enable;
  simVolts_ = clampf(volts, 0, mq135::CIRCUIT_VOLTAGE);
}

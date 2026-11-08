#include "air_quality_monitor.h"

#include <math.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float AirQualityMonitor::adcToVoltage(uint16_t raw) {
  return (static_cast<float>(raw) / mq135::ADC_MAX_COUNTS) * mq135::ADC_VREF;
}

float AirQualityMonitor::pinToSensorVoltage(float vAdc) {
  return vAdc * (mq135::DIVIDER_R_TOP + mq135::DIVIDER_R_BOTTOM) / mq135::DIVIDER_R_BOTTOM;
}

float AirQualityMonitor::computeRs(float vSensor) {
  // Vout must be strictly between 0 and Vc for the formula to be meaningful.
  if (vSensor <= 0.001f || vSensor >= mq135::CIRCUIT_VOLTAGE) return -1.0f;
  return mq135::LOAD_RESISTANCE * (mq135::CIRCUIT_VOLTAGE - vSensor) / vSensor;
}

float AirQualityMonitor::voltageFromRs(float rs) {
  if (isnan(rs) || rs < 0) return 0;
  return mq135::CIRCUIT_VOLTAGE * mq135::LOAD_RESISTANCE / (rs + mq135::LOAD_RESISTANCE);
}

float AirQualityMonitor::indexFromRatio(float ratio) {
  if (isnan(ratio) || ratio < 0) return 0;
  return clampf(100.0f * (1.0f - ratio), 0.0f, 100.0f);
}

float AirQualityMonitor::compensationFactor(float t, float h) {
  return mq135::CORR_A * t * t - mq135::CORR_B * t + mq135::CORR_C - (h - 33.0f) * mq135::CORR_D;
}

void AirQualityMonitor::begin(uint32_t now, float r0, bool r0Valid) {
  bootMs_ = now;
  r0Valid_ = false;
  if (r0Valid) setR0(r0);
}

bool AirQualityMonitor::setR0(float r0) {
  if (isnan(r0) || r0 < mq135::R0_MIN_PLAUSIBLE || r0 > mq135::R0_MAX_PLAUSIBLE) return false;
  r0_ = r0;
  r0Valid_ = true;
  return true;
}

bool AirQualityMonitor::warmingUp(uint32_t now) const {
  return !warmupSkipped_ && (uint32_t)(now - bootMs_) < cfg::MQ135_WARMUP_MS;
}

uint32_t AirQualityMonitor::warmupRemainingMs(uint32_t now) const {
  if (!warmingUp(now)) return 0;
  return cfg::MQ135_WARMUP_MS - (uint32_t)(now - bootMs_);
}

bool AirQualityMonitor::usable(uint32_t now) const {
  if (simIndexOn_ && !simFault_) return true;
  return last_.valid && !faulted() && !warmingUp(now) && (r0Valid_ || voltageMode_);
}

float AirQualityMonitor::medianOfWindow() const {
  float tmp[mq135::MEDIAN_WINDOW];
  for (uint8_t i = 0; i < windowCount_; ++i) tmp[i] = window_[i];
  for (uint8_t i = 1; i < windowCount_; ++i) {  // insertion sort (n <= 9)
    float key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > key) { tmp[j + 1] = tmp[j]; --j; }
    tmp[j + 1] = key;
  }
  return tmp[windowCount_ / 2];
}

void AirQualityMonitor::addSample(uint16_t rawAdc, float tempC, float rh, bool envValid,
                                  uint32_t now) {
  if (simAdcOn_) rawAdc = simAdc_;
  AirSample s;
  s.raw = rawAdc;
  s.vAdc = adcToVoltage(rawAdc);
  s.vSensor = pinToSensorVoltage(s.vAdc);
  s.rsInstant = computeRs(s.vSensor);

  const bool floorOk = !cfg::MQ135_FLOOR_IS_FAULT || rawAdc >= cfg::MQ135_ADC_MIN_VALID;
  const bool rangeOk = floorOk && rawAdc <= cfg::MQ135_ADC_MAX_VALID;
  if (simFault_ || !rangeOk || s.rsInstant <= 0) {
    if (failCount_ < 255) ++failCount_;
    okCount_ = 0;
    if (failCount_ >= cfg::MQ135_FAIL_LIMIT) faultLatched_ = true;
    s.valid = false;
    s.rsFiltered = last_.rsFiltered;
    s.ratio = last_.ratio;
    s.index = last_.index;  // kept for display only; never treated as normal
    if (simIndexOn_) { s.index = simIndex_; s.simulated = true; }  // bench test w/o sensor
    last_ = s;
    if (calState_ == CalState::Running && faulted()) {
      calState_ = CalState::Failed;
      calError_ = CalError::Aborted;
    }
    return;
  }
  failCount_ = 0;
  if (faultLatched_) {
    if (okCount_ < 255) ++okCount_;
    if (okCount_ >= cfg::MQ135_OK_TO_CLEAR) { faultLatched_ = false; okCount_ = 0; }
  }
  s.valid = true;

  // Median (spike rejection) followed by EMA (noise smoothing).
  window_[windowPos_] = s.rsInstant;
  windowPos_ = (windowPos_ + 1) % mq135::MEDIAN_WINDOW;
  if (windowCount_ < mq135::MEDIAN_WINDOW) ++windowCount_;
  const float med = medianOfWindow();
  if (!emaInit_) { ema_ = med; emaInit_ = true; }
  else ema_ += mq135::EMA_ALPHA * (med - ema_);
  s.rsFiltered = ema_;

  // Voltage-domain filter (voltage mode): short median + fast EMA on V(AO).
  vWindow_[vPos_] = s.vSensor;
  vPos_ = (vPos_ + 1) % mq135::V_MEDIAN_WINDOW;
  if (vCount_ < mq135::V_MEDIAN_WINDOW) ++vCount_;
  float vs[mq135::V_MEDIAN_WINDOW] = {};
  for (uint8_t i = 0; i < vCount_; ++i) vs[i] = vWindow_[i];
  for (uint8_t i = 1; i < vCount_; ++i) {
    const float key = vs[i];
    int j = i - 1;
    while (j >= 0 && vs[j] > key) { vs[j + 1] = vs[j]; --j; }
    vs[j + 1] = key;
  }
  const float vMed = vs[vCount_ / 2];
  if (!vEmaInit_) { vEma_ = vMed; vEmaInit_ = true; }
  else if (s.vSensor > vEma_) vEma_ = s.vSensor;  // instant attack: gas fires at once
  else vEma_ += mq135::V_EMA_ALPHA * (vMed - vEma_);  // smooth release
  s.vFiltered = vEma_;

  float comp = 1.0f;
  if (mq135::COMPENSATION_ENABLED && envValid) {
    comp = compensationFactor(tempC, rh);
    if (comp < 0.3f || comp > 3.0f) comp = 1.0f;  // outside fitted range
  }

  if (calState_ == CalState::Running) {
    const double rsNorm = s.rsInstant / comp;  // R0 stored normalised to 20 C/33 %RH
    calSum_ += rsNorm;
    calSumSq_ += rsNorm * rsNorm;
    ++calN_;
    if ((uint32_t)(now - calStartMs_) >= mq135::CAL_DURATION_MS) finishCalibration(now);
  }

  if (r0Valid_) s.ratio = (s.rsFiltered / comp) / r0_;
  if (voltageMode_) s.index = clampf(100.0f * s.vFiltered / mq135::CIRCUIT_VOLTAGE, 0.0f, 100.0f);
  else if (r0Valid_) s.index = indexFromRatio(s.ratio);
  if (simIndexOn_) { s.index = simIndex_; s.simulated = true; }
  if (simAdcOn_) s.simulated = true;
  last_ = s;
}

bool AirQualityMonitor::startCalibration(uint32_t now) {
  if (warmingUp(now) || faulted() || !last_.valid) {
    calState_ = CalState::Failed;
    calError_ = CalError::NotReady;
    return false;
  }
  calState_ = CalState::Running;
  calError_ = CalError::None;
  calStartMs_ = now;
  calSum_ = calSumSq_ = 0;
  calN_ = 0;
  return true;
}

void AirQualityMonitor::abortCalibration() {
  if (calState_ == CalState::Running) {
    calState_ = CalState::Failed;
    calError_ = CalError::Aborted;
  }
}

float AirQualityMonitor::calProgress(uint32_t now) const {
  if (calState_ != CalState::Running) return calState_ == CalState::Done ? 1.0f : 0.0f;
  return clampf((float)(uint32_t)(now - calStartMs_) / (float)mq135::CAL_DURATION_MS, 0, 1);
}

void AirQualityMonitor::finishCalibration(uint32_t) {
  if (calN_ < mq135::CAL_MIN_SAMPLES) {
    calState_ = CalState::Failed;
    calError_ = CalError::TooFewSamples;
    return;
  }
  const double mean = calSum_ / calN_;
  double var = calSumSq_ / calN_ - mean * mean;
  if (var < 0) var = 0;
  calMean_ = static_cast<float>(mean);
  calCv_ = mean > 0 ? static_cast<float>(sqrt(var) / mean) : 1.0f;
  if (calCv_ > mq135::CAL_MAX_CV) {
    calState_ = CalState::Failed;
    calError_ = CalError::Unstable;
    return;
  }
  if (!setR0(calMean_)) {
    calState_ = CalState::Failed;
    calError_ = CalError::OutOfRange;
    return;
  }
  calState_ = CalState::Done;
  newR0_ = true;
}

bool AirQualityMonitor::takeNewR0(float& r0) {
  if (!newR0_) return false;
  newR0_ = false;
  r0 = r0_;
  return true;
}

void AirQualityMonitor::simulateIndex(bool enable, float index) {
  simIndexOn_ = enable;
  simIndex_ = clampf(index, 0, 100);
}

void AirQualityMonitor::simulateAdc(bool enable, uint16_t raw) {
  simAdcOn_ = enable;
  simAdc_ = raw;
}

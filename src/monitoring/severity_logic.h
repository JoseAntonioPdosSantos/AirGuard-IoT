// AirGuard IoT - value hysteresis classifier + temporal persistence tracker.
// Pure logic, unit tested in tests/unit/test_severity.cpp.
#pragma once
#include <stdint.h>

#include "../config/thresholds.h"
#include "alert_types.h"

// Maps a value to NORMAL/LOW/MEDIUM/HIGH using a two-sided ThresholdSet with
// value hysteresis: an already-active level is kept until the value moves
// `hysteresis` units back past its entry threshold.
class HysteresisClassifier {
 public:
  explicit HysteresisClassifier(const ThresholdSet& t) : t_(t) {}
  // aboveValue is compared with the "above" side, belowValue with "below".
  Severity update(float aboveValue, float belowValue);
  Severity update(float value) { return update(value, value); }
  void reset() { lastAbove_ = lastBelow_ = Severity::Normal; }
  const ThresholdSet& thresholds() const { return t_; }

 private:
  static Severity classifySide(const LevelThresholds& lt, float v, Severity current,
                               float hyst, bool above);
  const ThresholdSet& t_;
  Severity lastAbove_ = Severity::Normal;
  Severity lastBelow_ = Severity::Normal;
};

// Confirms severity changes only after they persist:
//  - escalate to level L when the candidate stayed >= L for escalateMs;
//  - de-escalate to level D when the candidate stayed < D+1 for recoverMs.
// millis() wrap-around safe (unsigned subtraction).
class SeverityTracker {
 public:
  SeverityTracker(uint32_t escalateMs, uint32_t recoverMs)
      : escalateMs_(escalateMs), recoverMs_(recoverMs) {}

  // Feed a new candidate. Returns true when the confirmed level changed.
  bool update(Severity candidate, uint32_t nowMs);
  // Force a confirmed level (boot restore). Timers restart.
  void force(Severity level, uint32_t nowMs);
  Severity confirmed() const { return confirmed_; }

 private:
  uint32_t escalateMs_;
  uint32_t recoverMs_;
  Severity confirmed_ = Severity::Normal;
  bool     aboveActive_[4] = {false, false, false, false};
  uint32_t aboveSince_[4] = {0, 0, 0, 0};
  bool     belowActive_[4] = {false, false, false, false};
  uint32_t belowSince_[4] = {0, 0, 0, 0};
};

// NOAA/NWS heat index (Rothfusz regression + adjustments). Input/output in C.
float heatIndexC(float tempC, float rhPercent);

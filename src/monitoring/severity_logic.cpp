#include "severity_logic.h"

#include <math.h>

const char* severityName(Severity s) {
  switch (s) {
    case Severity::Normal: return "NORMAL";
    case Severity::Low: return "LOW";
    case Severity::Medium: return "MEDIUM";
    case Severity::High: return "HIGH";
  }
  return "?";
}

const char* categoryName(Category c) {
  switch (c) {
    case Category::AirQuality: return "AIR_QUALITY";
    case Category::Temperature: return "TEMPERATURE";
    case Category::Humidity: return "HUMIDITY";
    case Category::System: return "SYSTEM";
  }
  return "?";
}

const char* eventKindName(EventKind k) {
  return k == EventKind::Recovery ? "RECOVERY" : "ALERT";
}

// ---------------------------------------------------------------------------
Severity HysteresisClassifier::classifySide(const LevelThresholds& lt, float v,
                                            Severity current, float hyst, bool above) {
  if (!lt.enabled || isnan(v)) return Severity::Normal;
  const float th[4] = {0.0f, lt.low, lt.medium, lt.high};
  for (int level = 3; level >= 1; --level) {
    float entry = th[level];
    // Already at (or above) this level: relax the threshold to stay sticky.
    if (level <= static_cast<int>(current)) entry = above ? entry - hyst : entry + hyst;
    if (above ? (v >= entry) : (v <= entry)) return static_cast<Severity>(level);
  }
  return Severity::Normal;
}

Severity HysteresisClassifier::update(float aboveValue, float belowValue) {
  lastAbove_ = classifySide(t_.above, aboveValue, lastAbove_, t_.hysteresis, true);
  lastBelow_ = classifySide(t_.below, belowValue, lastBelow_, t_.hysteresis, false);
  return maxSeverity(lastAbove_, lastBelow_);
}

// ---------------------------------------------------------------------------
bool SeverityTracker::update(Severity candidate, uint32_t now) {
  const int cand = static_cast<int>(candidate);
  for (int level = 1; level <= 3; ++level) {
    if (cand >= level) {
      if (!aboveActive_[level]) { aboveActive_[level] = true; aboveSince_[level] = now; }
    } else {
      aboveActive_[level] = false;
    }
    if (cand < level) {
      if (!belowActive_[level]) { belowActive_[level] = true; belowSince_[level] = now; }
    } else {
      belowActive_[level] = false;
    }
  }

  const int conf = static_cast<int>(confirmed_);
  // Escalation: highest level that has persisted long enough.
  for (int level = 3; level > conf; --level) {
    if (aboveActive_[level] && (uint32_t)(now - aboveSince_[level]) >= escalateMs_) {
      confirmed_ = static_cast<Severity>(level);
      return true;
    }
  }
  // De-escalation: deepest level whose "below" condition is stable.
  for (int target = 0; target < conf; ++target) {
    const int above = target + 1;
    if (belowActive_[above] && (uint32_t)(now - belowSince_[above]) >= recoverMs_) {
      confirmed_ = static_cast<Severity>(target);
      return true;
    }
  }
  return false;
}

void SeverityTracker::force(Severity level, uint32_t now) {
  confirmed_ = level;
  for (int i = 0; i < 4; ++i) {
    aboveActive_[i] = belowActive_[i] = false;
    aboveSince_[i] = belowSince_[i] = now;
  }
}

// ---------------------------------------------------------------------------
float heatIndexC(float tempC, float rh) {
  if (isnan(tempC) || isnan(rh)) return NAN;
  const float t = tempC * 9.0f / 5.0f + 32.0f;
  float hi = 0.5f * (t + 61.0f + (t - 68.0f) * 1.2f + rh * 0.094f);
  if ((hi + t) / 2.0f >= 80.0f) {
    hi = -42.379f + 2.04901523f * t + 10.14333127f * rh - 0.22475541f * t * rh -
         0.00683783f * t * t - 0.05481717f * rh * rh + 0.00122874f * t * t * rh +
         0.00085282f * t * rh * rh - 0.00000199f * t * t * rh * rh;
    if (rh < 13.0f && t >= 80.0f && t <= 112.0f) {
      hi -= ((13.0f - rh) / 4.0f) * sqrtf((17.0f - fabsf(t - 95.0f)) / 17.0f);
    } else if (rh > 85.0f && t >= 80.0f && t <= 87.0f) {
      hi += ((rh - 85.0f) / 10.0f) * ((87.0f - t) / 5.0f);
    }
  }
  return (hi - 32.0f) * 5.0f / 9.0f;
}

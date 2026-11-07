#include "environment_monitor.h"

#include <math.h>

#include "../config/config.h"
#include "../config/thresholds.h"
#include "severity_logic.h"

bool EnvironmentMonitor::faulted() const { return failCount_ >= cfg::ENV_FAIL_LIMIT; }

void EnvironmentMonitor::addReading(bool ok, float t, float h, uint32_t) {
  if (simTempOn_) t = simTemp_;
  if (simHumOn_) h = simHum_;
  if (simTempOn_ && simHumOn_) ok = true;  // fully simulated: no hardware needed

  const bool plausible = ok && !isnan(t) && !isnan(h) &&
                         t >= thr::TEMP_MIN_PLAUSIBLE && t <= thr::TEMP_MAX_PLAUSIBLE &&
                         h >= thr::HUM_MIN_PLAUSIBLE && h <= thr::HUM_MAX_PLAUSIBLE;
  if (simFault_ || !plausible) {
    valid_ = false;
    if (failCount_ < 255) ++failCount_;
    return;  // keep last good values for display, flagged invalid
  }
  failCount_ = 0;
  valid_ = true;
  everValid_ = true;
  temp_ = t;
  hum_ = h;
  heatIndex_ = heatIndexC(t, h);
}

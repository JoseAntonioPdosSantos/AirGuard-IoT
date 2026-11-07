#include "buzzer_pattern.h"

#include "../config/config.h"

#define AG_COUNT(a) static_cast<uint8_t>(sizeof(a) / sizeof((a)[0]))

BuzzPattern patternFor(Severity s) {
  switch (s) {
    case Severity::Low: return {cfg::BUZZ_PATTERN_LOW, AG_COUNT(cfg::BUZZ_PATTERN_LOW)};
    case Severity::Medium: return {cfg::BUZZ_PATTERN_MEDIUM, AG_COUNT(cfg::BUZZ_PATTERN_MEDIUM)};
    case Severity::High: return {cfg::BUZZ_PATTERN_HIGH, AG_COUNT(cfg::BUZZ_PATTERN_HIGH)};
    default: return {nullptr, 0};
  }
}

bool patternOutput(const BuzzPattern& p, uint32_t elapsed) {
  if (!p.steps || p.count == 0) return false;
  uint32_t period = 0;
  for (uint8_t i = 0; i < p.count; ++i) period += p.steps[i];
  if (period == 0) return false;
  uint32_t t = elapsed % period;
  for (uint8_t i = 0; i < p.count; ++i) {
    if (t < p.steps[i]) return (i % 2) == 0;  // even index = ON
    t -= p.steps[i];
  }
  return false;
}

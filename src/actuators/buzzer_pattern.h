// AirGuard IoT - buzzer patterns as a pure function of time (unit tested).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../monitoring/alert_types.h"

struct BuzzPattern {
  const uint16_t* steps;  // ON, OFF, ON, OFF... (ms)
  uint8_t count;
};

BuzzPattern patternFor(Severity s);
// true = buzzer ON at `elapsedMs` since the pattern started.
bool patternOutput(const BuzzPattern& p, uint32_t elapsedMs);

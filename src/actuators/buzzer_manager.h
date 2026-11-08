// AirGuard IoT - non-blocking buzzer driver (pattern state machine).
// The pattern itself is a pure function of time (buzzer_pattern.h); this class
// only restarts it when the severity changes and writes the GPIO on edges.
#pragma once
#include <stdint.h>

#include "../monitoring/alert_types.h"
#include "buzzer_pattern.h"

class BuzzerManager {
 public:
  void begin();                              // output OFF
  void setSeverity(Severity s, uint32_t nowMs);
  void loop(uint32_t nowMs);
  // Short self-test beep at boot (non-blocking, ~150 ms).
  void chirp(uint32_t nowMs);
  // Bench test: drive the output directly (bypasses the pattern until the
  // next loop() call).
  void force(bool on) { write(on); }
  bool on() const { return on_; }
  Severity severity() const { return sev_; }

 private:
  void write(bool on);
  Severity    sev_ = Severity::Normal;
  BuzzPattern pat_ = {nullptr, 0};
  uint32_t    startMs_ = 0;
  uint32_t    chirpUntilMs_ = 0;
  bool        chirp_ = false;
  bool        on_ = false;
};

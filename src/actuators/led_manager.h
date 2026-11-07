// AirGuard IoT - status LEDs (red, yellow; pins in config/hardware_pins.h).
//
//   Startup (boot / MQ-135 warm-up, no alert) -> yellow slow blink (1 Hz)
//   NORMAL  -> both off
//   LOW     -> yellow ON
//   MEDIUM  -> yellow blinking (2 Hz)
//   HIGH    -> red blinking (2 Hz)
//
// The GREEN LED is wired in hardware: 3V3 -> 1k -> LED -> relay NC -> COM -> GND.
// It is lit while the relay is released and goes off when the relay is
// energised (HIGH), as required. The firmware does not drive it.
#pragma once
#include <stdint.h>

#include "../monitoring/alert_types.h"

class LedManager {
 public:
  void begin();
  void setState(Severity overall, bool startingUp) { sev_ = overall; starting_ = startingUp; }
  void loop(uint32_t nowMs);
  bool red() const { return red_; }
  bool yellow() const { return yellow_; }

 private:
  void write(uint8_t pin, bool on, bool& cur);
  Severity sev_ = Severity::Normal;
  bool starting_ = true;
  bool red_ = false, yellow_ = false;
  bool init_ = false;
};

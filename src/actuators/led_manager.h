// AirGuard IoT - LEDs driven by the firmware (pins in config/hardware_pins.h).
//
// Status (red, yellow):
//   Startup (boot / MQ-135 warm-up, no alert) -> yellow slow blink (1 Hz)
//   NORMAL  -> both off
//   LOW     -> yellow ON
//   MEDIUM  -> yellow blinking (2 Hz)
//   HIGH    -> red blinking (2 Hz)
//
// The GREEN LED is wired in hardware: 3V3 -> 1k -> LED -> relay NC -> COM -> GND.
// It is lit while the relay is released and goes off when the relay is
// energised (HIGH), as required. The firmware does not drive it.
//
// Message LED (blue, its own GPIO): off, except for a burst of fast blinks when a
// WhatsApp message arrives (notifyMessage). It has nothing to do with the
// severity; an alert ends the burst (cancelMessage).
#pragma once
#include <stdint.h>

#include "../monitoring/alert_types.h"

class LedManager {
 public:
  void begin();
  void setState(Severity overall, bool startingUp) { sev_ = overall; starting_ = startingUp; }
  void loop(uint32_t nowMs);
  // Bench test: drive both LEDs directly (bypasses the severity logic until
  // the next loop() call).
  void force(bool red, bool yellow);
  bool red() const { return red_; }
  bool yellow() const { return yellow_; }

  // A message arrived: blink the message LED for totalMs (a new message starts over).
  void notifyMessage(uint32_t nowMs, uint32_t totalMs, uint32_t halfMs);
  void cancelMessage() { msgActive_ = false; }
  bool message() const { return msg_; }
  // Level of the message LED `elapsedMs` after the message arrived: on first,
  // toggling every halfMs, off for good after totalMs. Pure (unit tested).
  static bool messageBlink(uint32_t elapsedMs, uint32_t totalMs, uint32_t halfMs) {
    return elapsedMs < totalMs && (halfMs == 0 || (elapsedMs / halfMs) % 2 == 0);
  }

 private:
  void write(uint8_t pin, bool on, bool& cur);
  Severity sev_ = Severity::Normal;
  bool starting_ = true;
  bool red_ = false, yellow_ = false, msg_ = false;
  bool init_ = false;
  bool     msgActive_ = false;
  uint32_t msgStartMs_ = 0, msgTotalMs_ = 0, msgHalfMs_ = 0;
};

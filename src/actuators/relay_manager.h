// AirGuard IoT - relay output with a defined safe state.
//
// Safe state = de-energised. It is applied before the pin becomes an output
// (no glitch at boot) and whenever the firmware cannot decide (see main).
// Polarity comes from cfg::RELAY_ACTIVE_HIGH: verify YOUR module first
// (docs/01_hardware.md, "Relé").
#pragma once
#include <stdint.h>

class RelayManager {
 public:
  void begin();
  void set(bool energised, uint32_t nowMs);
  bool energised() const { return on_; }
  uint32_t switchCount() const { return switches_; }

 private:
  void write(bool on);
  bool on_ = false;
  uint32_t switches_ = 0;
  uint32_t lastChangeMs_ = 0;
};

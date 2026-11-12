#include "relay_manager.h"

#include <Arduino.h>

#include "../config/config.h"
#include "../config/hardware_pins.h"
#include "../utils/logger.h"

void RelayManager::write(bool on) {
  const bool level = cfg::RELAY_ACTIVE_HIGH ? on : !on;
  digitalWrite(pins::RELAY, level ? HIGH : LOW);
}

void RelayManager::begin() {
  write(false);  // latch the inactive level first...
  pinMode(pins::RELAY, (cfg::RELAY_OPEN_DRAIN && !cfg::RELAY_ACTIVE_HIGH) ? OUTPUT_OPEN_DRAIN : OUTPUT);
  write(false);  // ...then drive it
  on_ = false;
}

void RelayManager::set(bool on, uint32_t now) {
  if (on == on_) return;
  on_ = on;
  write(on);
  LOGI("RELAY", "%s (after %lu ms in previous state)", on ? "ENERGISED" : "released",
       static_cast<unsigned long>(now - lastChangeMs_));
  lastChangeMs_ = now;
}

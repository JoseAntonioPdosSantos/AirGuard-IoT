#include "buzzer_manager.h"

#include <Arduino.h>

#include "../config/config.h"
#include "../config/hardware_pins.h"

static uint16_t freqFor(Severity s) {
  switch (s) {
    case Severity::Low: return cfg::BUZZER_FREQ_LOW_HZ;
    case Severity::Medium: return cfg::BUZZER_FREQ_MEDIUM_HZ;
    default: return cfg::BUZZER_FREQ_HIGH_HZ;
  }
}

void BuzzerManager::begin() {
  digitalWrite(pins::BUZZER, LOW);  // LOW = buzzer off (direct via 1k, or NPN driver)
  pinMode(pins::BUZZER, OUTPUT);
  digitalWrite(pins::BUZZER, LOW);
  on_ = false;
}

void BuzzerManager::write(bool on) {
  if (on == on_) return;
  on_ = on;
  if (cfg::BUZZER_IS_PASSIVE) {
    if (on) tone(pins::BUZZER, freqFor(chirp_ ? Severity::Low : sev_));
    else noTone(pins::BUZZER);
  } else {
    digitalWrite(pins::BUZZER, on ? HIGH : LOW);
  }
}

void BuzzerManager::setSeverity(Severity s, uint32_t now) {
  if (s == sev_) return;
  sev_ = s;
  pat_ = patternFor(s);
  startMs_ = now;  // a new (higher or lower) level starts its pattern at once
  if (cfg::BUZZER_IS_PASSIVE && on_) { write(false); }
}

void BuzzerManager::chirp(uint32_t now) {
  chirp_ = true;
  chirpUntilMs_ = now + 150;
}

void BuzzerManager::loop(uint32_t now) {
  if (chirp_) {
    if ((int32_t)(now - chirpUntilMs_) < 0) { write(true); return; }
    chirp_ = false;
    write(false);
  }
  write(patternOutput(pat_, now - startMs_));
}

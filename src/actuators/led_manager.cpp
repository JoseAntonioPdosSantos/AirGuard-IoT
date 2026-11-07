#include "led_manager.h"

#include <Arduino.h>

#include "../config/hardware_pins.h"

void LedManager::begin() {
  pinMode(pins::LED_RED, OUTPUT);
  pinMode(pins::LED_YELLOW, OUTPUT);
  digitalWrite(pins::LED_RED, LOW);
  digitalWrite(pins::LED_YELLOW, LOW);
  red_ = yellow_ = false;
  init_ = true;
}

void LedManager::write(uint8_t pin, bool on, bool& cur) {
  if (on == cur) return;
  cur = on;
  digitalWrite(pin, on ? HIGH : LOW);
}

void LedManager::loop(uint32_t now) {
  if (!init_) return;
  const bool blink2Hz = (now / 250) % 2 == 0;
  const bool blink1Hz = (now / 500) % 2 == 0;
  bool r = false, y = false;
  switch (sev_) {
    case Severity::High: r = blink2Hz; break;
    case Severity::Medium: y = blink2Hz; break;
    case Severity::Low: y = true; break;
    default: y = starting_ && blink1Hz; break;
  }
  write(pins::LED_RED, r, red_);
  write(pins::LED_YELLOW, y, yellow_);
}

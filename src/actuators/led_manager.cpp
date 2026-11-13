#include "led_manager.h"

#include <Arduino.h>

#include "../config/hardware_pins.h"

void LedManager::begin() {
  pinMode(pins::LED_RED, OUTPUT);
  pinMode(pins::LED_YELLOW, OUTPUT);
  digitalWrite(pins::LED_RED, LOW);
  digitalWrite(pins::LED_YELLOW, LOW);
  pinMode(pins::LED_MSG, OUTPUT);
  digitalWrite(pins::LED_MSG, LOW);
  red_ = yellow_ = msg_ = false;
  init_ = true;
}

void LedManager::write(uint8_t pin, bool on, bool& cur) {
  if (on == cur) return;
  cur = on;
  digitalWrite(pin, on ? HIGH : LOW);
}

void LedManager::force(bool red, bool yellow) {
  if (!init_) return;
  write(pins::LED_RED, red, red_);
  write(pins::LED_YELLOW, yellow, yellow_);
  write(pins::LED_MSG, false, msg_);
}

void LedManager::notifyMessage(uint32_t now, uint32_t totalMs, uint32_t halfMs) {
  msgActive_ = true;
  msgStartMs_ = now;
  msgTotalMs_ = totalMs;
  msgHalfMs_ = halfMs;
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

  const uint32_t elapsed = now - msgStartMs_;
  if (msgActive_ && elapsed >= msgTotalMs_) msgActive_ = false;
  write(pins::LED_MSG, msgActive_ && messageBlink(elapsed, msgTotalMs_, msgHalfMs_), msg_);
}

#include "mq135_sensor.h"

#include <Arduino.h>

#include "../config/config.h"
#include "../config/hardware_pins.h"

void Mq135Sensor::begin() {
  analogReadResolution(12);
  pinMode(pins::MQ135_ADC, INPUT);  // high impedance; no pull-ups on an ADC pin
}

uint16_t Mq135Sensor::readRaw() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < cfg::MQ135_OVERSAMPLE; ++i) sum += analogRead(pins::MQ135_ADC);
  return static_cast<uint16_t>(sum / cfg::MQ135_OVERSAMPLE);
}

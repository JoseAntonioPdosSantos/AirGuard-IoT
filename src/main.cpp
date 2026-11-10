// AirGuard IoT - SMART ENVIRONMENTAL MONITORING
// "Intelligent monitoring. Safer environments."
//
// Kitchen environmental monitor for the ESP32 DevKit:
// MQ-135 (relative air quality), DHT11 (temperature, humidity),
// REYAX RYLR998 LoRa (alerts + ACK, remote text), SSD1306 OLED, buzzer, relay, LEDs.
//
// Prototype for study/validation. It is NOT a certified gas or safety detector.
#include <Arduino.h>

#include "app/airguard_app.h"

static AirGuardApp g_app;

void setup() { g_app.setup(); }

void loop() { g_app.loop(); }

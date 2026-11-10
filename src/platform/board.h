// AirGuard IoT - ESP32 helpers (the ONLY file with ESP-IDF specific calls).
//
// Board: ESP32 DevKit (ESP32-WROOM-32, Arduino-ESP32 core). Everything else in
// the firmware calls board::* instead of esp_* so the rest stays plain Arduino.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <esp_idf_version.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "../config/hardware_pins.h"

#if !defined(ARDUINO_ARCH_ESP32)
#error "AirGuard IoT node: ESP32 only (the RP2040-Zero version is on branch main)"
#endif

namespace board {

constexpr const char* NAME = "ESP32 DevKit (ESP32-WROOM-32)";

inline HardwareSerial& loraSerial() { return Serial2; }

// ---- watchdog ---------------------------------------------------------------
inline void watchdogBegin(uint32_t timeoutMs) {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t c = {};
  c.timeout_ms = timeoutMs;
  c.idle_core_mask = 0;
  c.trigger_panic = true;  // panic -> reset
  if (esp_task_wdt_reconfigure(&c) != ESP_OK) esp_task_wdt_init(&c);
#else
  esp_task_wdt_init((timeoutMs + 999) / 1000, true);  // reconfigures if already running
#endif
  esp_task_wdt_add(nullptr);  // subscribe the loop task
}

inline void watchdogFeed() { esp_task_wdt_reset(); }

inline bool watchdogCausedReboot() {
  const esp_reset_reason_t r = esp_reset_reason();
  return r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT;
}

[[noreturn]] inline void reboot() {
  ESP.restart();
  while (true) {}
}

inline uint32_t hwRandom() { return esp_random(); }

// ---- short interrupt-free section (bit-banged DHT11 timing, <= ~9 ms) -----
inline portMUX_TYPE& criticalMux() {
  static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
  return m;
}
inline void criticalBegin() { portENTER_CRITICAL(&criticalMux()); }
inline void criticalEnd() { portEXIT_CRITICAL(&criticalMux()); }

// ---- I2C (OLED) ---------------------------------------------------------------
inline void i2cBegin(uint32_t clockHz) {
  Wire.begin(pins::I2C_SDA, pins::I2C_SCL);
  Wire.setClock(clockHz);
}

// ---- LoRa UART (RYLR998) ----------------------------------------------------
// swapped = true exchanges TX and RX (diagnostics only: "TXD/RXD wires crossed?").
inline void loraUartBegin(uint32_t baud, uint16_t rxBuffer, bool swapped = false) {
  const uint8_t tx = swapped ? pins::LORA_UART_RX : pins::LORA_UART_TX;
  const uint8_t rx = swapped ? pins::LORA_UART_TX : pins::LORA_UART_RX;
  Serial2.setRxBufferSize(rxBuffer);  // must precede begin()
  Serial2.begin(baud, SERIAL_8N1, rx, tx);
}

inline void loraUartEnd() { Serial2.end(); }

// ---- ADC (MQ-135) -----------------------------------------------------------
// Returns a 12-bit count of the pin voltage referenced to 3.3 V (thresholds.h:
// ADC_VREF / ADC_MAX_COUNTS). The ESP32 raw ADC is non-linear, so its
// eFuse-calibrated millivolt reading is used instead.
inline void adcBegin(uint8_t pin) {
  analogReadResolution(12);
  analogSetPinAttenuation(pin, ADC_11db);  // ~0.15-3.1 V usable range
  pinMode(pin, INPUT);
}

inline uint16_t adcRead12(uint8_t pin) {
  const uint32_t mv = analogReadMilliVolts(pin);
  const uint32_t counts = (mv * 4095UL + 1650UL) / 3300UL;
  return static_cast<uint16_t>(counts > 4095 ? 4095 : counts);
}

}  // namespace board

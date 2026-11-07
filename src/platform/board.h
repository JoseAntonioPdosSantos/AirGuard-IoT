// AirGuard IoT - board abstraction (the ONLY file with MCU-specific calls).
//
// Supported targets (selected automatically by the Arduino core in use):
//   - ESP32 DevKit (ESP32-WROOM-32, Arduino-ESP32 core)  -> ARDUINO_ARCH_ESP32
//   - Waveshare RP2040-Zero (Arduino-Pico core)           -> ARDUINO_ARCH_RP2040
//
// Everything else in the firmware calls board::* instead of rp2040.* / esp_*.
#pragma once
#include <Arduino.h>
#include <Wire.h>

#include "../config/hardware_pins.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_idf_version.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#elif defined(ARDUINO_ARCH_RP2040)
#include <hardware/watchdog.h>
#else
#error "AirGuard IoT: unsupported board (ESP32 or RP2040 only)"
#endif

namespace board {

#if defined(ARDUINO_ARCH_ESP32)
constexpr const char* NAME = "ESP32 DevKit (ESP32-WROOM-32)";
constexpr const char* PIN_PREFIX = "GPIO";
#define AIRGUARD_LORA_SERIAL Serial2
#else
constexpr const char* NAME = "Waveshare RP2040-Zero";
constexpr const char* PIN_PREFIX = "GP";
#define AIRGUARD_LORA_SERIAL Serial1
#endif

inline HardwareSerial& loraSerial() { return AIRGUARD_LORA_SERIAL; }

// ---- watchdog ---------------------------------------------------------------
inline void watchdogBegin(uint32_t timeoutMs) {
#if defined(ARDUINO_ARCH_ESP32)
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
#else
  rp2040.wdt_begin(timeoutMs);
#endif
}

inline void watchdogFeed() {
#if defined(ARDUINO_ARCH_ESP32)
  esp_task_wdt_reset();
#else
  rp2040.wdt_reset();
#endif
}

inline bool watchdogCausedReboot() {
#if defined(ARDUINO_ARCH_ESP32)
  const esp_reset_reason_t r = esp_reset_reason();
  return r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT;
#else
  return watchdog_caused_reboot();
#endif
}

[[noreturn]] inline void reboot() {
#if defined(ARDUINO_ARCH_ESP32)
  ESP.restart();
#else
  rp2040.reboot();
#endif
  while (true) {}
}

inline uint32_t hwRandom() {
#if defined(ARDUINO_ARCH_ESP32)
  return esp_random();
#else
  return rp2040.hwrand32();
#endif
}

// ---- I2C (OLED, optional AHT20) ------------------------------------------
inline void i2cBegin(uint32_t clockHz) {
#if defined(ARDUINO_ARCH_ESP32)
  Wire.begin(pins::I2C_SDA, pins::I2C_SCL);
#else
  Wire.setSDA(pins::I2C_SDA);
  Wire.setSCL(pins::I2C_SCL);
  Wire.begin();
#endif
  Wire.setClock(clockHz);
}

// ---- LoRa UART (RYLR998) ----------------------------------------------------
// swapped = true exchanges TX and RX (diagnostics only: "TXD/RXD wires crossed?").
inline void loraUartBegin(uint32_t baud, uint16_t rxBuffer, bool swapped = false) {
  const uint8_t tx = swapped ? pins::LORA_UART_RX : pins::LORA_UART_TX;
  const uint8_t rx = swapped ? pins::LORA_UART_TX : pins::LORA_UART_RX;
#if defined(ARDUINO_ARCH_ESP32)
  Serial2.setRxBufferSize(rxBuffer);  // must precede begin()
  Serial2.begin(baud, SERIAL_8N1, rx, tx);
#else
  // UART0 accepts only TX=GP0/RX=GP1; the swapped test uses a PIO UART instead.
  (void)tx;
  (void)rx;
  Serial1.setTX(pins::LORA_UART_TX);
  Serial1.setRX(pins::LORA_UART_RX);
  Serial1.setFIFOSize(rxBuffer);
  Serial1.begin(baud);
#endif
}

inline void loraUartEnd() { loraSerial().end(); }

// Can loraUartBegin(..., swapped=true) really swap the pins on this MCU?
#if defined(ARDUINO_ARCH_ESP32)
constexpr bool UART_CAN_SWAP = true;
#else
constexpr bool UART_CAN_SWAP = false;
#endif

// ---- ADC (MQ-135) -----------------------------------------------------------
// Returns a 12-bit-equivalent count of the pin voltage referenced to 3.3 V, so
// the platform-independent core (thresholds.h: ADC_VREF / ADC_MAX_COUNTS) is
// the same on both boards. The ESP32 raw ADC is non-linear; its eFuse-calibrated
// millivolt reading is used instead.
inline void adcBegin(uint8_t pin) {
  analogReadResolution(12);
#if defined(ARDUINO_ARCH_ESP32)
  analogSetPinAttenuation(pin, ADC_11db);  // ~0.15-3.1 V usable range
#endif
  pinMode(pin, INPUT);
}

inline uint16_t adcRead12(uint8_t pin) {
#if defined(ARDUINO_ARCH_ESP32)
  const uint32_t mv = analogReadMilliVolts(pin);
  const uint32_t counts = (mv * 4095UL + 1650UL) / 3300UL;
  return static_cast<uint16_t>(counts > 4095 ? 4095 : counts);
#else
  return static_cast<uint16_t>(analogRead(pin));
#endif
}

}  // namespace board

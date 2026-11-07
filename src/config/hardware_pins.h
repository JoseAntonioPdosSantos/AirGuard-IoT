// AirGuard IoT - GPIO map for the Waveshare RP2040-Zero (as wired by the user).
//
// Every pin choice below was checked against the RP2040 datasheet
// (GPIO function table, section 1.4.3) and the RP2040-Zero pinout:
//   - GP0..GP15 and GP26..GP29 are on the castellated edge pads.
//   - GP16 drives the on-board WS2812 RGB LED (do not reuse).
//   - GP26..GP29 are the only ADC-capable pins (ADC0..ADC3).
//   - ALL GPIOs are 3.3 V logic and are NOT 5 V tolerant.
// See docs/01_hardware.md for the wiring table and the electrical analysis.
#pragma once
#include <stdint.h>

namespace pins {

// ---- LoRa: REYAX RYLR998 (UART AT-command module) on UART0 = Serial1 ------
// UART0 valid mux: TX = GP0, RX = GP1.
constexpr uint8_t LORA_UART_TX = 0;  // RP2040 TX -> RYLR998 RXD
constexpr uint8_t LORA_UART_RX = 1;  // RP2040 RX <- RYLR998 TXD
constexpr uint8_t LORA_NRST    = 2;  // RYLR998 NRST (active LOW)

// ---- I2C0: OLED SSD1306 -----------------------------------------------------
// I2C0 valid mux: SDA = GP4, SCL = GP5.
constexpr uint8_t I2C_SDA = 4;
constexpr uint8_t I2C_SCL = 5;

// ---- Temperature / humidity (DHT11 module, single-wire data) ---------------
constexpr uint8_t DHT_DATA = 28;

// ---- Actuators / indicators -------------------------------------------------
constexpr uint8_t BUZZER     = 6;   // buzzer (+) through 1 kOhm
constexpr uint8_t LED_RED    = 7;   // through 1 kOhm
constexpr uint8_t LED_YELLOW = 12;  // through 1 kOhm
constexpr uint8_t RELAY      = 15;  // relay-module IN
// GREEN LED: NOT on a GPIO. It is powered from 3V3 through the relay's NC
// contact, so it is lit while the relay is released and goes off when the
// relay is energised (HIGH condition).

// ---- Analog ----------------------------------------------------------------
// MQ-135 AO -> 10k -> GP29 (ADC3) -> 2 x 10k -> GND. NEVER connect AO directly.
constexpr uint8_t MQ135_ADC = 29;

// ---- Reserved --------------------------------------------------------------
constexpr uint8_t ONBOARD_WS2812 = 16;

}  // namespace pins

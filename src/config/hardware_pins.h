// AirGuard IoT - GPIO maps. The map is chosen by the Arduino core in use:
//   ESP32 DevKit (ESP32-WROOM-32)  -> first block
//   Waveshare RP2040-Zero          -> second block (original wiring)
// ALL GPIOs of both MCUs are 3.3 V logic and are NOT 5 V tolerant.
// See docs/01_hardware.md (RP2040) and docs/11_esp32_devkit.md (ESP32).
#pragma once
#include <stdint.h>

#if defined(ARDUINO_ARCH_ESP32)
// ===========================================================================
// ESP32 DevKit (ESP32-WROOM-32, 30- or 38-pin board).
// Avoided on purpose:
//   GPIO6-11  wired to the SPI flash (using them crashes the chip)
//   GPIO1/3   UART0 = USB console (CP2102/CH340)
//   GPIO0/2/5/12/15 strapping pins (a module pulling them at reset can stop
//             the boot or change the flash voltage; GPIO12 HIGH = 1.8 V flash!)
//   GPIO14    outputs PWM during boot
//   GPIO34-39 input-only, no internal pull-ups (only MQ-135 uses one: analog)
//   ADC2 pins (GPIO 0,2,4,12-15,25-27) cannot be read while Wi-Fi is on, so
//             the analog input is on ADC1.
// ===========================================================================
namespace pins {

// ---- LoRa: REYAX RYLR998 on UART2 = Serial2 (pins are freely routable) ------
constexpr uint8_t LORA_UART_TX = 17;  // ESP32 TX2 -> RYLR998 RXD
constexpr uint8_t LORA_UART_RX = 16;  // ESP32 RX2 <- RYLR998 TXD
constexpr uint8_t LORA_NRST    = 4;   // RYLR998 NRST (active LOW)

// ---- I2C: OLED SSD1306 (default ESP32 Wire pins) -----------------------------
constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;

// ---- Temperature / humidity (DHT11 module, single-wire data) ---------------
constexpr uint8_t DHT_DATA = 27;

// ---- Actuators / indicators -------------------------------------------------
constexpr uint8_t BUZZER     = 25;  // buzzer (+) through 1 kOhm
constexpr uint8_t LED_RED    = 26;  // through 1 kOhm
constexpr uint8_t LED_YELLOW = 33;  // through 1 kOhm
constexpr uint8_t RELAY      = 23;  // relay-module IN
// GREEN LED: NOT on a GPIO (3V3 -> 1k -> LED -> relay NC contact), as before.

// ---- Analog ----------------------------------------------------------------
// MQ-135 AO -> 10k -> GPIO34 (ADC1_CH6) -> 20k -> GND. NEVER connect AO directly.
constexpr uint8_t MQ135_ADC = 34;

// ---- Reserved --------------------------------------------------------------
constexpr uint8_t ONBOARD_LED = 2;  // blue LED on most DevKits (strapping pin)

}  // namespace pins

#else
// ===========================================================================
// Waveshare RP2040-Zero (as wired by the user).
// Every pin choice below was checked against the RP2040 datasheet
// (GPIO function table, section 1.4.3) and the RP2040-Zero pinout:
//   - GP0..GP15 and GP26..GP29 are on the castellated edge pads.
//   - GP16 drives the on-board WS2812 RGB LED (do not reuse).
//   - GP26..GP29 are the only ADC-capable pins (ADC0..ADC3).
// ===========================================================================
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

#endif

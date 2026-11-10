// AirGuard IoT - GPIO map of the ESP32 DevKit (ESP32-WROOM-32, 30- or 38-pin).
// ALL GPIOs are 3.3 V logic and are NOT 5 V tolerant. Wiring: docs/01_hardware.md.
//
// Avoided on purpose:
//   GPIO6-11  wired to the SPI flash (using them crashes the chip)
//   GPIO1/3   UART0 = USB console (CP2102/CH340)
//   GPIO0/2/5/12/15 strapping pins (a module pulling them at reset can stop
//             the boot or change the flash voltage; GPIO12 HIGH = 1.8 V flash!)
//   GPIO14    outputs PWM during boot
//   GPIO34-39 input-only, no internal pull-ups (only MQ-135 uses one: analog)
//   ADC2 pins (GPIO 0,2,4,12-15,25-27) cannot be read while Wi-Fi is on, so
//             the analog input is on ADC1.
#pragma once
#include <stdint.h>

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
// GREEN LED: NOT on a GPIO. It is powered from 3V3 through the relay's NC
// contact, so it is lit while the relay is released and goes off when the
// relay is energised (HIGH condition).

// ---- Analog ----------------------------------------------------------------
// MQ-135 AO -> 10k -> GPIO34 (ADC1_CH6) -> 20k -> GND. NEVER connect AO directly.
constexpr uint8_t MQ135_ADC = 34;

}  // namespace pins


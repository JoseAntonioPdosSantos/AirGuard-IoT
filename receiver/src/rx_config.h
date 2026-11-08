// AirGuard Receiver - configuration (pins, identity, radio).
//
// Board: Waveshare RP2040-Zero. LCD 16x2 with PCF8574 I2C backpack.
// The radio settings MUST match the sensor node (../src/config/config.h).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace rxcfg {

// ---- identity ---------------------------------------------------------------
constexpr const char* GATEWAY_ID = "GATEWAY_01";  // the node sends alerts to this "dst"
constexpr const char* NODE_ID    = "KITCHEN_01";  // accepted sender ("device")

// ---- pins (RP2040-Zero) -------------------------------------------------------
constexpr uint8_t LORA_UART_TX = 0;  // RP2040 TX (GP0) -> RYLR998 RXD
constexpr uint8_t LORA_UART_RX = 1;  // RP2040 RX (GP1) <- RYLR998 TXD
constexpr uint8_t LORA_NRST    = 2;  // RYLR998 NRST (active LOW)
constexpr uint8_t I2C_SDA      = 4;  // LCD backpack SDA (I2C0)
constexpr uint8_t I2C_SCL      = 5;  // LCD backpack SCL (I2C0)
constexpr uint8_t LED_GREEN    = 6;  // through 220 ohm-1 kOhm
constexpr uint8_t LED_YELLOW   = 7;
constexpr uint8_t LED_RED      = 8;
// Bench probe (no multimeter): line -> 10k -> GP26 (ADC0) -> 10k -> GND, so the
// logged value x2 is the line voltage (max 2.5 V at the pin for 5 V).
constexpr uint8_t PROBE_ADC    = 26;

// ---- radio: identical to the node except the address ----------------------
constexpr uint32_t LORA_UART_BAUD    = 115200;
constexpr uint32_t LORA_BAND_HZ      = 915000000;
constexpr uint8_t  LORA_SF           = 8;
constexpr uint8_t  LORA_BW_CODE      = 7;
constexpr uint8_t  LORA_CR_CODE      = 1;
constexpr uint8_t  LORA_PREAMBLE     = 12;
constexpr uint8_t  LORA_NETWORK_ID   = 6;
constexpr uint16_t LORA_ADDRESS      = 1;   // this receiver (node is 101)
constexpr int8_t   LORA_TX_POWER_DBM = 14;
constexpr const char* LORA_CPIN      = "";  // same as the node ("" = not set)

constexpr uint32_t LORA_CMD_TIMEOUT_MS   = 1500;
constexpr uint32_t LORA_READY_TIMEOUT_MS = 3000;
constexpr uint32_t LORA_REINIT_PERIOD_MS = 30000;

// ---- behaviour ----------------------------------------------------------------
// The node in voltage mode sends air_index = 100 * V(AO) / 5 V, so V = index / 20.
constexpr bool     AIR_INDEX_IS_VOLTAGE = true;
constexpr uint32_t LCD_PAGE_MS          = 3000;  // line 2 alternates values / link info
constexpr uint32_t WAIT_BLINK_MS        = 500;   // green blink before the first message
constexpr uint32_t WATCHDOG_MS          = 8000;
// The node sends a heartbeat every 5 s: nothing for 15 s (3 missed) = lost signal.
constexpr uint32_t LINK_TIMEOUT_MS      = 15000;

}  // namespace rxcfg

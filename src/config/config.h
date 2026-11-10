// AirGuard IoT - central configuration (timing, identity, radio, policies).
//
// This header is platform independent (no Arduino includes) so the core
// logic can be unit tested on a PC. Alarm limits and the MQ-135 circuit live
// in thresholds.h; GPIOs live in hardware_pins.h.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define AIRGUARD_FW_VERSION "1.0.0"

namespace cfg {

// ---------------------------------------------------------------------------
// Identity / addressing
// ---------------------------------------------------------------------------
constexpr const char* DEVICE_ID  = "KITCHEN_01";  // our address ("device")
constexpr const char* GATEWAY_ID = "GATEWAY_01";  // destination of alerts ("dst")
// Senders allowed to deliver TEXT messages to this node (whitelist).
constexpr const char* ALLOWED_TEXT_SOURCES[] = {"WHATSAPP_GATEWAY", "GATEWAY_01"};
constexpr size_t ALLOWED_TEXT_SOURCES_COUNT =
    sizeof(ALLOWED_TEXT_SOURCES) / sizeof(ALLOWED_TEXT_SOURCES[0]);

// ---------------------------------------------------------------------------
// Scheduling (cooperative, non-blocking main loop)
// ---------------------------------------------------------------------------
constexpr uint32_t MQ135_SAMPLE_PERIOD_MS  = 200;   // 5 Hz: gas alarm reacts within ~0.2 s
constexpr uint8_t  MQ135_OVERSAMPLE        = 16;    // ADC reads averaged per sample
constexpr uint32_t ENV_SAMPLE_PERIOD_MS    = 2500;  // DHT11 needs >= 1 s between reads
constexpr uint32_t BOOT_SCREEN_MS          = 3000;  // brand/slogan splash
constexpr uint32_t DISPLAY_REFRESH_MS      = 50;   // 20 fps for the scrolling line
constexpr uint32_t DISPLAY_TICKER_PX_PER_S = 40;   // scrolling speed
constexpr uint32_t DISPLAY_BLINK_MS        = 500;
// Re-send the whole frame this often even if it did not change, so a frame
// corrupted on the I2C bus (seen on the bench: garbage in the yellow band,
// likely 3V3 dips during LoRa TX) is repaired within this time.
constexpr uint32_t DISPLAY_FULL_REFRESH_MS = 2000;
constexpr uint32_t STATUS_LOG_PERIOD_MS    = 10000;
constexpr uint32_t WATCHDOG_TIMEOUT_MS     = 8000;

// ---------------------------------------------------------------------------
// MQ-135 warm-up
// The datasheets ask for 24-48 h of initial burn-in for a NEW sensor
// (Hanwei: "over 24 h", Winsen: "over 48 h"). After the sensor has been
// burned in, a short warm-up after each power-on is still required before
// readings are trusted. 5 min is a prototype value: measure your sensor's
// settling time (docs/04_mq135.md) and adjust.
// ---------------------------------------------------------------------------
constexpr uint32_t MQ135_WARMUP_MS = 5UL * 60UL * 1000UL;
// DEVELOPMENT ONLY: skip the warm-up at every boot (alarms active at once,
// readings of a cold sensor are NOT trustworthy). Set to false before deploying.
constexpr bool DEV_SKIP_MQ135_WARMUP = true;

// ---------------------------------------------------------------------------
// Sensor fault detection
// ---------------------------------------------------------------------------
constexpr uint8_t  ENV_FAIL_LIMIT     = 3;   // consecutive bad reads -> fault (~7.5 s)
constexpr uint8_t  MQ135_FAIL_LIMIT   = 25;  // consecutive bad samples -> fault (~5 s at 5 Hz)
// ADC at its floor (AO below ~0.21 V) -> sensor FAULT?
//   true : safest; an open AO wire is detected (SYSTEM alert).
//   false: BENCH setting (user request). After hours powered, this sensor's
//          clean-air AO drifted below what the ESP32 ADC can measure, so the
//          floor is read as clean air. An open AO wire is then NOT detected.
constexpr bool MQ135_FLOOR_IS_FAULT = false;
// Consecutive GOOD samples needed to clear a fault (~2 s). With AO open, ADC
// noise sometimes crosses the floor for one sample: without this the fault
// flapped ALERT/RECOVERY every ~8 s and each event went out over LoRa.
constexpr uint8_t  MQ135_OK_TO_CLEAR  = 10;
// ESP32 ADC (11 dB attenuation) cannot read below ~0.14 V (it returns that
// floor for 0 V, measured on the bench: 176 counts), and saturates at ~3.1 V.
// A pin at the floor is indistinguishable from "AO disconnected / shorted".
// No upper fault limit: with the 10k/20k divider the pin saturates at ~3.1 V
// (AO ~4.7 V) only under STRONG gas, which must read as high, not as a fault.
constexpr uint16_t MQ135_ADC_MIN_VALID = 200;   // ~0.16 V at the pin

// ---------------------------------------------------------------------------
// Alert persistence (temporal hysteresis). See docs/06_alerts.md.
// ESCALATE: candidate level must persist this long before it is confirmed.
// RECOVER : the lower level must be stable this long before de-escalating.
// ---------------------------------------------------------------------------
constexpr uint32_t AIR_ESCALATE_MS  = 0;     // INSTANT (user request); was 5 s, then 2 s
// BENCH TEST values (user request): alarm on AND off immediately. For real use
// raise AIR_RECOVER_MS (e.g. 60000) so the alarm does not chatter.
constexpr uint32_t AIR_RECOVER_MS   = 0;     // was 60 s, then 10 s
constexpr uint32_t TEMP_ESCALATE_MS = 10000;
constexpr uint32_t TEMP_RECOVER_MS  = 60000;
constexpr uint32_t HUM_ESCALATE_MS  = 30000;
constexpr uint32_t HUM_RECOVER_MS   = 120000;

// Re-send an ALERT (new event_id, "reminder":1) while a condition at or above
// this severity persists. 0 disables reminders.
constexpr uint32_t ALERT_REMINDER_MS = 15UL * 60UL * 1000UL;
constexpr uint8_t  ALERT_REMINDER_MIN_SEVERITY = 2;  // MEDIUM

// ---------------------------------------------------------------------------
// Relay policy
// ---------------------------------------------------------------------------
// true  -> GPIO HIGH energises the relay (NPN driver / high-level trigger).
// false -> GPIO LOW energises the relay (low-level trigger module driven
//          directly; only valid if the module is 3.3 V compatible!).
constexpr bool RELAY_ACTIVE_HIGH = true;
// Which categories may switch the relay on HIGH (index = Category).
//                                   AIR   TEMP  HUM   SYSTEM
constexpr bool RELAY_ON_HIGH[4] = {true, true, true, false};
// Optional: also switch the relay on MEDIUM (spec: "salvo configuração").
constexpr bool RELAY_ON_MEDIUM = false;
// After a reboot that happened while some category was HIGH, restore that
// HIGH state (relay + alarms) until fresh readings prove recovery.
constexpr bool RESTORE_HIGH_AFTER_REBOOT = true;

// ---------------------------------------------------------------------------
// Buzzer
// ---------------------------------------------------------------------------
// Installed buzzer is ACTIVE (internal oscillator): the pin is just driven
// HIGH/LOW. Set true only for a passive buzzer, which needs tone() PWM.
constexpr bool BUZZER_IS_PASSIVE = false;
constexpr uint16_t BUZZER_FREQ_LOW_HZ    = 2000;  // only used when BUZZER_IS_PASSIVE
constexpr uint16_t BUZZER_FREQ_MEDIUM_HZ = 2500;
constexpr uint16_t BUZZER_FREQ_HIGH_HZ   = 3100;
// Patterns: alternating ON/OFF durations in ms, starting with ON, repeated.
constexpr uint16_t BUZZ_PATTERN_LOW[]    = {60, 140, 60, 29740};   // 2 short beeps / 30 s
constexpr uint16_t BUZZ_PATTERN_MEDIUM[] = {150, 150, 150, 150, 150, 4250};  // 3 beeps / 5 s
constexpr uint16_t BUZZ_PATTERN_HIGH[]   = {400, 200};             // continuous urgent

// ---------------------------------------------------------------------------
// OLED
// ---------------------------------------------------------------------------
constexpr uint8_t  OLED_I2C_ADDR   = 0x3C;  // 0x3D on some modules (check jumper)
constexpr uint8_t  OLED_WIDTH      = 128;
constexpr uint8_t  OLED_HEIGHT     = 64;
constexpr uint32_t I2C_CLOCK_HZ    = 400000;
constexpr uint32_t REMOTE_MSG_PAGE_MS = 3000;  // each page shown exactly 3 s
constexpr uint8_t  REMOTE_MSG_QUEUE   = 4;

// ---------------------------------------------------------------------------
// LoRa radio: REYAX RYLR998 (SX1262 inside, UART AT commands).
// See docs/03_lora_protocol.md (ANATEL notes!). BOTH nodes must use the same
// band, parameters, network id and password.
// ---------------------------------------------------------------------------
constexpr uint32_t LORA_UART_BAUD   = 115200;     // module default (AT+IPR)
constexpr uint32_t LORA_BAND_HZ     = 915000000;  // 915-928 MHz sub-band (Brazil)
// AT+PARAMETER=<SF>,<BW code>,<CR code>,<preamble>
// REYAX recommends SF8/125 kHz/CR1/12 for payloads > 100 bytes (ours: ~200).
constexpr uint8_t  LORA_SF          = 8;          // 5..11
constexpr uint8_t  LORA_BW_CODE     = 7;          // 7=125 kHz, 8=250 kHz, 9=500 kHz
constexpr uint8_t  LORA_CR_CODE     = 1;          // 1=4/5 ... 4=4/8
constexpr uint8_t  LORA_PREAMBLE    = 12;         // must be 12 unless NETWORKID=18
constexpr uint8_t  LORA_NETWORK_ID  = 6;          // 3..15 or 18 (default 18: avoid it)
constexpr uint16_t LORA_ADDRESS     = 101;        // this node (0..65535)
// Destination of every frame. 0 = broadcast to all modules of the network id;
// logical addressing is done by the JSON "dst" field.
constexpr uint16_t LORA_DEST_ADDRESS = 0;
constexpr int8_t   LORA_TX_POWER_DBM = 14;        // AT+CRFOP (0..22)
// AT+CPIN: 8 hex chars "00000001".."FFFFFFFF" shared with the gateway;
// frames with another password are not delivered. "" = no password.
constexpr const char* LORA_CPIN = "";

constexpr uint32_t LORA_REINIT_PERIOD_MS = 30000;  // retry after a fault
constexpr uint32_t LORA_CMD_TIMEOUT_MS   = 1500;   // wait for +OK
constexpr uint32_t LORA_READY_TIMEOUT_MS = 3000;   // wait for +READY after reset
constexpr uint32_t LORA_TX_TIMEOUT_MS    = 5000;   // AT+SEND must answer
constexpr uint32_t LORA_HEALTH_PERIOD_MS = 60000;  // idle "AT" ping
constexpr uint32_t LORA_BUSY_BACKOFF_MS  = 400;    // + random(0..backoff)

// ---------------------------------------------------------------------------
// Reliable delivery (ACK manager)
// ---------------------------------------------------------------------------
constexpr uint32_t ACK_TIMEOUT_MS   = 60000;  // spec: retransmit after 60 s
constexpr uint8_t  ACK_MAX_RETRIES  = 5;      // retransmissions after the first try
constexpr uint32_t TX_MIN_GAP_MS    = 2000;   // channel courtesy gap between TXs
constexpr uint8_t  TX_QUEUE_SIZE    = 8;
// Heartbeat ("estou vivo") to the receiver: levels + live readings, no ACK,
// lowest priority (an alert always goes first). 0 = disabled.
// Airtime ~0.4 s at SF8 -> ~8 % of the time on air with 5 s (user request).
constexpr uint32_t HEARTBEAT_PERIOD_MS = 5000;
constexpr uint8_t  RX_DEDUP_CACHE   = 16;

// ---------------------------------------------------------------------------
// Protocol limits
// ---------------------------------------------------------------------------
constexpr size_t LORA_MAX_PAYLOAD  = 240;  // RYLR998 AT+SEND limit (+ERR=13 above)
constexpr size_t MAX_TEXT_BYTES    = 120;  // UTF-8 bytes accepted in TEXT.text
constexpr size_t MAX_ID_LEN        = 23;   // device/source identifiers

// ---------------------------------------------------------------------------
// Persistent storage (EEPROM emulation in flash)
// ---------------------------------------------------------------------------
constexpr size_t   STORE_SIZE  = 256;
constexpr uint32_t STORE_MAGIC = 0x41474931UL;  // "AGI1"

}  // namespace cfg

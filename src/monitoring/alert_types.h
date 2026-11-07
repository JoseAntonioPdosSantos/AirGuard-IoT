// AirGuard IoT - shared alert enums and data snapshots (platform independent).
// Enumerator names use CamelCase on purpose: Arduino defines LOW/HIGH.
#pragma once
#include <stdint.h>

enum class Severity : uint8_t { Normal = 0, Low = 1, Medium = 2, High = 3 };
enum class Category : uint8_t { AirQuality = 0, Temperature = 1, Humidity = 2, System = 3 };
constexpr uint8_t CATEGORY_COUNT = 4;

enum class EventKind : uint8_t { Alert, Recovery };

// Debounced system conditions that drive the SYSTEM category.
enum SystemFlag : uint16_t {
  SYS_NONE             = 0,
  SYS_ENV_SENSOR_FAULT = 1 << 0,  // MEDIUM
  SYS_AIR_SENSOR_FAULT = 1 << 1,  // MEDIUM
  SYS_MQ135_UNCAL      = 1 << 2,  // LOW
  SYS_LORA_RADIO_FAULT = 1 << 3,  // LOW (cannot be reported by radio!)
  SYS_LORA_NO_ACK      = 1 << 4,  // LOW (delivery failures)
  SYS_DISPLAY_FAULT    = 1 << 5,  // LOW
};

inline Severity maxSeverity(Severity a, Severity b) { return a > b ? a : b; }

const char* severityName(Severity s);
const char* categoryName(Category c);
const char* eventKindName(EventKind k);

// Latest measurements, copied into every event so a message is self-contained.
struct SensorSnapshot {
  bool     airValid = false;
  bool     airWarmingUp = true;
  bool     airCalibrated = false;
  uint16_t rawAdc = 0;
  float    adcVoltage = 0;   // at the MCU ADC pin
  float    sensorVoltage = 0;// at MQ-135 AO (divider undone)
  float    rs = 0;           // ohms
  float    ratio = 0;        // Rs/R0 (compensated if enabled)
  float    airIndex = 0;     // 0..100 relative
  bool     envValid = false;
  float    temperature = 0;  // C
  float    humidity = 0;     // %RH
  float    heatIndex = 0;    // C
};

struct AlertEvent {
  EventKind kind = EventKind::Alert;
  Category  category = Category::System;
  Severity  severity = Severity::Normal;
  Severity  previous = Severity::Normal;
  uint32_t  eventId = 0;
  bool      escalation = false;
  bool      reminder = false;
  bool      restored = false;   // state restored after reboot
  uint16_t  systemFlags = 0;    // for SYSTEM events
  uint32_t  uptimeS = 0;
  SensorSnapshot snapshot;
};

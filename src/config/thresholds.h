// AirGuard IoT - alarm limits and the MQ-135 measurement circuit.
//
// IMPORTANT
//  * The gas limits are RELATIVE sensor-response limits (MQ-135 AO voltage),
//    not toxicological limits and not ppm of any specific gas. They must be
//    adjusted by observing the sensor in the installation site.
//  * Temperature/humidity limits are PROTOTYPE comfort/attention limits based
//    on WHO, EPA, ASHRAE and NOAA references (docs/05_environmental_thresholds.md).
//    They separate "discomfort" (LOW) from "attention" (MEDIUM) and
//    "critical for this prototype" (HIGH). They are not medical limits.
#pragma once
#include <stdint.h>

// One direction of a threshold set. Level L is entered when the value
// crosses `level[L]` (>= for ABOVE, <= for BELOW).
struct LevelThresholds {
  bool  enabled;
  float low;     // -> Severity LOW
  float medium;  // -> Severity MEDIUM
  float high;    // -> Severity HIGH
};

// Two-sided threshold set with value hysteresis. A level that is already
// active is only left when the value retreats `hysteresis` units past the
// entry threshold.
struct ThresholdSet {
  LevelThresholds above;
  LevelThresholds below;
  float hysteresis;
};

namespace thr {

// ---------------------------------------------------------------------------
// Gas: MQ-135 AO voltage in volts (0..5 V, divider already undone).
//   MEDIUM from 0.90 V, HIGH from 1.5 V; hysteresis 0.05 V (user request,
//   2026-11-08). 0.30 V was too close to the clean-air baseline (~0.2-0.6 V
//   on the bench) and gave false MEDIUM alarms. HIGH was raised from 1.0 V to
//   1.5 V (user request) so MEDIUM (0.90-1.5 V) is a real early warning.
//   No LOW level for gas (user request): LOW == MEDIUM threshold, so the
//   classifier goes NORMAL -> MEDIUM directly. Keep the hysteresis well
//   below 0.90 V.
// Bench values (ESP32, this sensor): clean air ~0.5 V, gas lighter near the
// sensor -> close to 5 V. INITIAL values: adjust after observing your kitchen.
// ---------------------------------------------------------------------------
constexpr ThresholdSet GAS_VOLTS = {
    /*above*/ {true, 0.90f, 0.90f, 1.5f},
    /*below*/ {false, 0, 0, 0},
    /*hyst */ 0.05f};

// ---------------------------------------------------------------------------
// Temperature. "above" uses the APPARENT temperature max(T, heat index)
// so humidity is considered; "below" uses the dry-bulb temperature.
//  above LOW    29.0 C         raised by the user from 26.7 C (80 F, NOAA
//                              "Caution"): the room sits at ~27 C apparent
//                              and LOW would stay on all the time
//  above MEDIUM 32.2 C (90 F)  NOAA "Extreme caution"
//  above HIGH   39.4 C (103 F) NOAA "Danger"
//  below LOW    18 C  WHO Housing & Health Guidelines (2018) minimum
//  below MEDIUM 16 C  UK cold-weather guidance (respiratory risk)
//  below HIGH   12 C  UK cold-weather guidance (cardiovascular strain)
// ---------------------------------------------------------------------------
constexpr ThresholdSet TEMPERATURE = {
    /*above*/ {true, 29.0f, 32.2f, 39.4f},
    /*below*/ {true, 18.0f, 16.0f, 12.0f},
    /*hyst */ 1.0f};

// ---------------------------------------------------------------------------
// Relative humidity (%)
//  30..60 %   EPA recommended indoor range (ideally 30-50 %)
//  > 75 % LOW  EPA says keep below 60 % (mould risk); raised to 75 % by the
//              user (local ambient RH ~65-70 % would keep LOW always on)
//  > 80 % MED  sustained RH >70-80 % favours mould growth on surfaces
//              (raised from 70 % by the user: local ambient RH is often ~70 %)
//  > 85 % HIGH prototype: condensation-prone, steam/leak suspected
//  < 30 % LOW, < 25 % MED, < 20 % HIGH: dryness/irritation (prototype)
// ---------------------------------------------------------------------------
constexpr ThresholdSet HUMIDITY = {
    /*above*/ {true, 75.0f, 80.0f, 85.0f},
    /*below*/ {true, 30.0f, 25.0f, 20.0f},
    /*hyst */ 3.0f};

// Sensor plausibility windows (outside -> invalid reading, never "normal").
constexpr float TEMP_MIN_PLAUSIBLE = -40.0f;
constexpr float TEMP_MAX_PLAUSIBLE = 80.0f;
constexpr float HUM_MIN_PLAUSIBLE  = 0.0f;
constexpr float HUM_MAX_PLAUSIBLE  = 100.0f;

}  // namespace thr

namespace mq135 {

// ---- Measurement circuit (MEASURE your module! docs/01_hardware.md) --------
constexpr float ADC_VREF         = 3.30f;    // board::adcRead12 scales the pin mV to 3.3 V
constexpr float ADC_MAX_COUNTS   = 4095.0f;  // 12-bit
constexpr float DIVIDER_R_TOP    = 10000.0f; // AO -> R_TOP -> ADC
constexpr float DIVIDER_R_BOTTOM = 20000.0f; // ADC -> R_BOTTOM -> GND (2 x 10k in SERIES)
constexpr float CIRCUIT_VOLTAGE  = 5.00f;    // module supply = maximum AO voltage

// ---- Filtering of the AO voltage --------------------------------------------
// INSTANT attack (a rising value is used as is, so the alarm fires on the
// first sample above the threshold) and median + EMA release.
// BENCH TEST: 1 / 1.0 = no filtering, the alarm follows the reading at once
// (5 Hz). For real use: e.g. 5 / 0.3 to ignore electrical noise.
constexpr uint8_t MEDIAN_WINDOW = 1;     // odd, <= 9 (samples at 5 Hz)
constexpr float   EMA_ALPHA     = 1.0f;  // 0..1, 1 = no smoothing

}  // namespace mq135

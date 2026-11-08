// AirGuard IoT - environmental limits and MQ-135 calibration constants.
//
// IMPORTANT
//  * Air-quality limits are RELATIVE sensor-response limits (air_index), not
//    toxicological limits and not ppm of any specific gas. They must be set
//    by experimental calibration in the installation site.
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
// Air quality (air_index: 0 = reference clean air, 100 = Rs -> 0)
// air_index = 100 * (1 - Rs/R0), clamped to [0, 100].
// Example: Rs/R0 = 0.70 -> 30. Initial values: PLACEHOLDERS for calibration.
// ---------------------------------------------------------------------------
constexpr ThresholdSet AIR_QUALITY = {
    /*above*/ {true, 15.0f, 30.0f, 50.0f},
    /*below*/ {false, 0, 0, 0},
    /*hyst */ 5.0f};

// ---------------------------------------------------------------------------
// Air quality by AO VOLTAGE (cfg::AIR_ALERT_BY_VOLTAGE = true, no R0 needed).
// air_index = 100 * V(AO) / 5 V, i.e. the AO voltage in percent of 5 V:
//   1.0 V = 20, 2.5 V = 50, 3.5 V = 70; hysteresis 0.3 V = 6.
// Bench values (ESP32, this sensor): clean air ~0.5 V, gas lighter near the
// sensor -> close to 5 V. INITIAL values: adjust after observing your kitchen.
// ---------------------------------------------------------------------------
constexpr float AO_LOW_V = 1.0f, AO_MEDIUM_V = 2.5f, AO_HIGH_V = 3.5f, AO_HYST_V = 0.3f;
constexpr ThresholdSet AIR_VOLTAGE = {
    /*above*/ {true, AO_LOW_V * 20.0f, AO_MEDIUM_V * 20.0f, AO_HIGH_V * 20.0f},
    /*below*/ {false, 0, 0, 0},
    /*hyst */ AO_HYST_V * 20.0f};

// ---------------------------------------------------------------------------
// Temperature. "above" uses the APPARENT temperature max(T, heat index)
// so humidity is considered; "below" uses the dry-bulb temperature.
//  above LOW    26.7 C (80 F)  NOAA heat-index "Caution"
//  above MEDIUM 32.2 C (90 F)  NOAA "Extreme caution"
//  above HIGH   39.4 C (103 F) NOAA "Danger"
//  below LOW    18 C  WHO Housing & Health Guidelines (2018) minimum
//  below MEDIUM 16 C  UK cold-weather guidance (respiratory risk)
//  below HIGH   12 C  UK cold-weather guidance (cardiovascular strain)
// ---------------------------------------------------------------------------
constexpr ThresholdSet TEMPERATURE = {
    /*above*/ {true, 26.7f, 32.2f, 39.4f},
    /*below*/ {true, 18.0f, 16.0f, 12.0f},
    /*hyst */ 1.0f};

// ---------------------------------------------------------------------------
// Relative humidity (%)
//  30..60 %   EPA recommended indoor range (ideally 30-50 %)
//  > 60 % LOW  EPA "keep below 60 %" (mould risk)
//  > 80 % MED  sustained RH >70-80 % favours mould growth on surfaces
//              (raised from 70 % by the user: local ambient RH is often ~70 %)
//  > 85 % HIGH prototype: condensation-prone, steam/leak suspected
//  < 30 % LOW, < 25 % MED, < 20 % HIGH: dryness/irritation (prototype)
// ---------------------------------------------------------------------------
constexpr ThresholdSet HUMIDITY = {
    /*above*/ {true, 60.0f, 80.0f, 85.0f},
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
constexpr float ADC_VREF        = 3.30f;    // RP2040: ADC_VREF = 3V3; ESP32: board::adcRead12 rescales mV to 3.3 V
constexpr float ADC_MAX_COUNTS  = 4095.0f;  // 12-bit
constexpr float DIVIDER_R_TOP   = 10000.0f; // AO -> R_TOP -> ADC
constexpr float DIVIDER_R_BOTTOM = 20000.0f;// ADC -> R_BOTTOM -> GND (2 x 10k in SERIES;
                                            // if they are in PARALLEL use 5000.0f)
constexpr float CIRCUIT_VOLTAGE = 5.00f;    // Vc across Rs + RL (measure it)
constexpr float LOAD_RESISTANCE = 10000.0f; // RL on the module (often 1k/10k/20k!)

// ---- Calibration -----------------------------------------------------------
constexpr uint32_t CAL_DURATION_MS   = 60000;  // clean-air averaging window
constexpr uint16_t CAL_MIN_SAMPLES   = 30;
constexpr float    CAL_MAX_CV        = 0.05f;  // max coefficient of variation
constexpr float    R0_MIN_PLAUSIBLE  = 500.0f;
constexpr float    R0_MAX_PLAUSIBLE  = 2.0e6f;

// ---- Filtering -------------------------------------------------------------
constexpr uint8_t MEDIAN_WINDOW = 5;     // odd, <= 9
constexpr float   EMA_ALPHA     = 0.25f; // 0..1, higher = faster
// Voltage mode filters the AO VOLTAGE itself (not Rs): averaging Rs and
// converting back to volts is dominated by the large clean-air Rs and made
// the alarm lag ~20 s behind the OLED value on the bench.
constexpr uint8_t V_MEDIAN_WINDOW = 3;   // odd, <= 9 (1 s samples -> 1 s delay)
constexpr float   V_EMA_ALPHA     = 0.5f;

// ---- Optional temperature/humidity compensation ---------------------------
// Polynomial fitted by the community (GeorgK MQ135 library) to the
// datasheet "Rs/R0 vs temperature/humidity" curves. NOT validated by us:
// keep disabled until you validate it with your own sensor.
//   f(T,H) = A*T^2 - B*T + C - (H - 33) * D     (f ~= 1 at 20 C / 33 %RH)
constexpr bool  COMPENSATION_ENABLED = false;
constexpr float CORR_A = 0.00035f;
constexpr float CORR_B = 0.02718f;
constexpr float CORR_C = 1.39538f;
constexpr float CORR_D = 0.0018f;

}  // namespace mq135

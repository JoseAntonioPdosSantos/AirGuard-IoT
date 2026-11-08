// MQ-135 signal chain, calibration, environment monitor, buzzer, display logic.
#include <string.h>

#include "../../src/actuators/buzzer_pattern.h"
#include "../../src/display/screen_logic.h"
#include "../../src/monitoring/air_quality_monitor.h"
#include "../../src/monitoring/environment_monitor.h"
#include "../../src/monitoring/severity_logic.h"
#include "../../src/utils/text_utils.h"
#include "test_framework.h"

// ---- MQ-135 math -----------------------------------------------------------
TEST(mq135_voltage_chain) {
  CHECK_NEAR(AirQualityMonitor::adcToVoltage(4095), 3.30, 1e-4);
  CHECK_NEAR(AirQualityMonitor::adcToVoltage(0), 0.0, 1e-6);
  // Divider undone: V_AO = V_pin * (Rtop + Rbottom) / Rbottom
  const float k = mq135::DIVIDER_R_BOTTOM / (mq135::DIVIDER_R_TOP + mq135::DIVIDER_R_BOTTOM);
  CHECK_NEAR(AirQualityMonitor::pinToSensorVoltage(5.0f * k), 5.0, 0.01);
  // Installed divider 10k / 2x10k (series): 5.0 V at AO -> 3.333 V at GP29
  CHECK_NEAR(5.0f * k, 3.333, 0.01);
}

TEST(mq135_rs_formula) {
  // RL = 10k, Vc = 5 V. Vout = 2.5 V -> Rs = RL
  CHECK_NEAR(AirQualityMonitor::computeRs(2.5f), 10000, 1);
  CHECK_NEAR(AirQualityMonitor::computeRs(1.0f), 40000, 1);
  CHECK(AirQualityMonitor::computeRs(0.0f) < 0);
  CHECK(AirQualityMonitor::computeRs(5.0f) < 0);
}

TEST(mq135_index_from_ratio) {
  CHECK_NEAR(AirQualityMonitor::indexFromRatio(1.0f), 0, 1e-6);
  CHECK_NEAR(AirQualityMonitor::indexFromRatio(1.4f), 0, 1e-6);   // cleaner than reference
  CHECK_NEAR(AirQualityMonitor::indexFromRatio(0.7f), 30, 1e-4);
  CHECK_NEAR(AirQualityMonitor::indexFromRatio(0.15f), 85, 1e-4);
  CHECK_NEAR(AirQualityMonitor::indexFromRatio(-1), 0, 1e-6);
}

TEST(mq135_compensation_near_unity_at_reference) {
  CHECK_NEAR(AirQualityMonitor::compensationFactor(20, 33), 1.0, 0.01);
}

static uint16_t adcForRs(float rs) {
  // inverse of the chain: Vout = Vc*RL/(Rs+RL); pin = Vout*R2/(R1+R2)
  float vout = mq135::CIRCUIT_VOLTAGE * mq135::LOAD_RESISTANCE / (rs + mq135::LOAD_RESISTANCE);
  float vpin = vout * mq135::DIVIDER_R_BOTTOM / (mq135::DIVIDER_R_TOP + mq135::DIVIDER_R_BOTTOM);
  return (uint16_t)(vpin / mq135::ADC_VREF * mq135::ADC_MAX_COUNTS + 0.5f);
}

TEST(mq135_warmup_blocks_usage) {
  AirQualityMonitor m;
  m.begin(0, 30000, true);
  m.addSample(adcForRs(30000), 20, 33, true, 1000);
  CHECK(m.warmingUp(1000));
  CHECK(!m.usable(1000));
  CHECK(m.usable(cfg::MQ135_WARMUP_MS + 1));
}

TEST(mq135_uncalibrated_not_usable) {
  AirQualityMonitor m;
  m.begin(0, 0, false);
  m.skipWarmup();
  m.addSample(adcForRs(30000), 20, 33, true, 1000);
  CHECK(!m.calibrated());
  CHECK(!m.usable(1000));
}

TEST(mq135_index_follows_rs_drop) {
  AirQualityMonitor m;
  m.begin(0, 30000, true);
  m.skipWarmup();
  uint32_t now = 0;
  for (int i = 0; i < 20; ++i, now += 1000) m.addSample(adcForRs(30000), 20, 33, true, now);
  CHECK_NEAR(m.last().index, 0, 2);
  for (int i = 0; i < 40; ++i, now += 1000) m.addSample(adcForRs(15000), 20, 33, true, now);
  CHECK_NEAR(m.last().ratio, 0.5, 0.03);
  CHECK_NEAR(m.last().index, 50, 3);
}

TEST(mq135_median_rejects_single_spike) {
  AirQualityMonitor m;
  m.begin(0, 30000, true);
  m.skipWarmup();
  uint32_t now = 0;
  for (int i = 0; i < 10; ++i, now += 1000) m.addSample(adcForRs(30000), 20, 33, true, now);
  m.addSample(adcForRs(3000), 20, 33, true, now);  // one-sample spike
  CHECK(m.last().index < 3);
}

TEST(mq135_open_circuit_fault) {
  AirQualityMonitor m;
  m.begin(0, 30000, true);
  m.skipWarmup();
  uint32_t now = 0;
  for (int i = 0; i < cfg::MQ135_FAIL_LIMIT; ++i, now += 1000) m.addSample(0, 20, 33, true, now);
  CHECK(m.faulted());
  CHECK(!m.usable(now));
  m.addSample(adcForRs(30000), 20, 33, true, now);
  CHECK(!m.faulted());
}

TEST(mq135_saturated_fault) {
  AirQualityMonitor m;
  m.begin(0, 30000, true);
  m.skipWarmup();
  for (int i = 0; i < cfg::MQ135_FAIL_LIMIT; ++i) m.addSample(4095, 20, 33, true, i * 1000);
  CHECK(m.faulted());
}

TEST(mq135_calibration_success) {
  AirQualityMonitor m;
  m.begin(0, 0, false);
  m.skipWarmup();
  m.addSample(adcForRs(42000), 20, 33, true, 0);
  CHECK(m.startCalibration(0));
  uint32_t now = 1000;
  for (; now <= mq135::CAL_DURATION_MS + 1000; now += 1000)
    m.addSample(adcForRs((now / 1000) % 2 ? 41500 : 42500), 20, 33, true, now);
  CHECK(m.calState() == CalState::Done);
  CHECK_NEAR(m.r0(), 42000, 600);
  float r0 = 0;
  CHECK(m.takeNewR0(r0));
  CHECK(!m.takeNewR0(r0));
  CHECK(m.calibrated());
}

TEST(mq135_calibration_unstable_fails) {
  AirQualityMonitor m;
  m.begin(0, 0, false);
  m.skipWarmup();
  m.addSample(adcForRs(30000), 20, 33, true, 0);
  m.startCalibration(0);
  for (uint32_t now = 1000; now <= mq135::CAL_DURATION_MS + 1000; now += 1000)
    m.addSample(adcForRs((now / 1000) % 2 ? 20000 : 40000), 20, 33, true, now);
  CHECK(m.calState() == CalState::Failed);
  CHECK(m.calError() == CalError::Unstable);
  CHECK(!m.calibrated());
}

TEST(mq135_calibration_refused_during_warmup) {
  AirQualityMonitor m;
  m.begin(0, 0, false);
  m.addSample(adcForRs(30000), 20, 33, true, 0);
  CHECK(!m.startCalibration(1000));
  CHECK(m.calError() == CalError::NotReady);
}

TEST(mq135_simulation_override) {
  AirQualityMonitor m;
  m.begin(0, 0, false);
  m.simulateIndex(true, 60);
  m.addSample(1000, 20, 33, true, 0);
  CHECK(m.usable(0));
  CHECK_NEAR(m.last().index, 60, 1e-4);
  m.simulateFault(true);
  CHECK(!m.usable(0));
}

// ---- Environment -----------------------------------------------------------
TEST(env_fault_after_consecutive_failures) {
  EnvironmentMonitor e;
  e.addReading(true, 25, 50, 0);
  CHECK(e.valid());
  for (int i = 0; i < cfg::ENV_FAIL_LIMIT; ++i) e.addReading(false, NAN, NAN, 0);
  CHECK(e.faulted());
  CHECK(!e.valid());
  CHECK_NEAR(e.temperature(), 25, 1e-6);  // last good value kept for display
  e.addReading(true, 26, 50, 0);
  CHECK(!e.faulted());
}

TEST(env_implausible_values_rejected) {
  EnvironmentMonitor e;
  e.addReading(true, 150, 50, 0);
  CHECK(!e.valid());
  e.addReading(true, 25, 120, 0);
  CHECK(!e.valid());
}

TEST(env_simulation) {
  EnvironmentMonitor e;
  e.simulateTemperature(true, 35);
  e.simulateHumidity(true, 80);
  e.addReading(false, NAN, NAN, 0);   // hardware absent
  CHECK(e.valid());
  CHECK_NEAR(e.temperature(), 35, 1e-6);
  CHECK(e.apparent() > 35);
}

// ---- Buzzer ----------------------------------------------------------------
TEST(buzzer_patterns) {
  BuzzPattern none = patternFor(Severity::Normal);
  CHECK(!patternOutput(none, 0));
  BuzzPattern hi = patternFor(Severity::High);
  CHECK(patternOutput(hi, 0));
  CHECK(patternOutput(hi, 399));
  CHECK(!patternOutput(hi, 400));
  CHECK(patternOutput(hi, 600));       // repeats
  BuzzPattern lo = patternFor(Severity::Low);
  CHECK(patternOutput(lo, 10));
  CHECK(!patternOutput(lo, 100));
  CHECK(patternOutput(lo, 210));
  CHECK(!patternOutput(lo, 5000));
  CHECK(patternOutput(lo, 30000));     // period 30 s
}

// ---- Text / display --------------------------------------------------------
TEST(text_transliteration) {
  char out[64];
  text::toDisplayAscii("Aten\xc3\xa7\xc3\xa3o: verifique a cozinha.", out, sizeof out);
  CHECK_STR(out, "Atencao: verifique a cozinha.");
  text::toDisplayAscii("  \xc3\x89 \x01 agora  \xe2\x80\x94 j\xc3\xa1 ", out, sizeof out);
  CHECK_STR(out, "E agora - ja");
  text::toDisplayAscii("\xf0\x9f\x94\xa5 fogo", out, sizeof out);   // emoji
  CHECK_STR(out, "? fogo");
  text::toDisplayAscii("bad\xc3", out, sizeof out);                 // truncated UTF-8
  CHECK_STR(out, "bad?");
  char small[5];
  text::toDisplayAscii("abcdefgh", small, sizeof small);
  CHECK_STR(small, "abcd");
}

TEST(text_wrap) {
  char lines[6][22];
  uint8_t n = text::wrap("Atencao: verifique a cozinha.", 21, lines, 6);
  CHECK_EQ(n, 2);
  CHECK_STR(lines[0], "Atencao: verifique a");
  CHECK_STR(lines[1], "cozinha.");
  n = text::wrap("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 21, lines, 6);  // 30 chars, no space
  CHECK_EQ(n, 2);
  CHECK_EQ(strlen(lines[0]), 21);
}

TEST(remote_message_exactly_3_seconds) {
  RemoteMessageDisplay d;
  CHECK(d.push("WHATSAPP_GATEWAY", "Teste", 1000));
  d.update(1000, false);
  CHECK(d.active());
  CHECK_STR(d.line(0), "Teste");
  d.update(3999, false);
  CHECK(d.active());
  d.update(4000, false);
  CHECK(!d.active());
  CHECK(selectScreen(false, Severity::Normal, d.active()) == Screen::Main);
}

TEST(remote_message_pages_and_queue) {
  RemoteMessageDisplay d;
  // ~140 chars -> 7+ lines -> 2 pages
  // 12-char words: one word per 21-col line -> 9 lines -> 2 pages
  const char* longTxt =
      "palavragrand palavragrand palavragrand palavragrand palavragrand palavragrand "
      "palavragrand palavragrand palavragrand";
  d.push("G", longTxt, 0);
  d.push("G", "segunda", 0);
  d.update(0, false);
  CHECK_EQ(d.pages(), 2);
  d.update(3000, false);
  CHECK_EQ(d.page(), 1);
  d.update(6000, false);
  CHECK(d.active());
  CHECK_STR(d.line(0), "segunda");
  d.update(9000, false);
  CHECK(!d.active());
}

TEST(remote_message_cannot_hide_high_alert) {
  RemoteMessageDisplay d;
  d.push("G", "um", 0);
  d.push("G", "dois", 0);
  d.update(0, true);
  CHECK(d.active());
  d.update(3000, true);         // first ends; HIGH -> alert screen gap
  CHECK(!d.active());
  CHECK(selectScreen(false, Severity::High, d.active()) == Screen::Alert);
  d.update(5999, true);
  CHECK(!d.active());
  d.update(6000, true);
  CHECK(d.active());
}

TEST(screen_priority) {
  CHECK(selectScreen(true, Severity::High, true) == Screen::Boot);
  CHECK(selectScreen(false, Severity::Low, false) == Screen::Alert);
  CHECK(selectScreen(false, Severity::Normal, false) == Screen::Main);
  CHECK(selectScreen(false, Severity::Medium, true) == Screen::Message);
}

// ---- voltage alert mode (cfg::AIR_ALERT_BY_VOLTAGE) -------------------------
TEST(mq135_voltage_mode_index_is_ao_percent) {
  AirQualityMonitor m;
  m.setVoltageMode(true);
  m.begin(0, 0, false);  // NOT calibrated
  m.skipWarmup();
  // AO = 2.5 V -> pin = 2.5 * Rb / (Rt + Rb)
  const float vPin = 2.5f * mq135::DIVIDER_R_BOTTOM / (mq135::DIVIDER_R_TOP + mq135::DIVIDER_R_BOTTOM);
  const uint16_t raw = static_cast<uint16_t>(vPin / mq135::ADC_VREF * mq135::ADC_MAX_COUNTS + 0.5f);
  for (int i = 0; i < 20; ++i) m.addSample(raw, 20, 33, false, 1000u * i);
  CHECK(m.usable(20000));  // usable without R0
  CHECK_NEAR(m.last().vFiltered, 2.5, 0.01);
  CHECK_NEAR(m.last().index, 50.0, 0.3);
}

TEST(mq135_voltage_thresholds_map_volts) {
  HysteresisClassifier c(thr::AIR_VOLTAGE);
  CHECK(c.update(100 * 0.5f / 5) == Severity::Normal);   // clean air ~0.5 V
  CHECK(c.update(100 * 0.9f / 5) == Severity::Normal);
  CHECK(c.update(100 * 1.1f / 5) == Severity::Low);     // LOW from 1.0 V
  CHECK(c.update(100 * 2.6f / 5) == Severity::Medium);
  CHECK(c.update(100 * 3.6f / 5) == Severity::High);
  CHECK(c.update(100 * 3.3f / 5) == Severity::High);     // within 0.3 V hysteresis
  CHECK(c.update(100 * 3.1f / 5) == Severity::Medium);
}

TEST(mq135_voltage_from_rs_inverts_rs_formula) {
  const float rs = AirQualityMonitor::computeRs(1.2f);
  CHECK_NEAR(AirQualityMonitor::voltageFromRs(rs), 1.2, 1e-3);
}

TEST(mq135_voltage_mode_follows_a_gas_step_quickly) {
  // Bench case: clean 0.45 V, gas brings AO to 3.0 V. The alarm value must
  // pass 2.5 V within a few seconds (it lagged ~20 s when Rs was averaged).
  AirQualityMonitor m;
  m.setVoltageMode(true);
  m.begin(0, 0, false);
  m.skipWarmup();
  auto rawFor = [](float vao) {
    const float vPin = vao * mq135::DIVIDER_R_BOTTOM / (mq135::DIVIDER_R_TOP + mq135::DIVIDER_R_BOTTOM);
    return static_cast<uint16_t>(vPin / mq135::ADC_VREF * mq135::ADC_MAX_COUNTS + 0.5f);
  };
  uint32_t t = 0;
  for (int i = 0; i < 10; ++i, t += 1000) m.addSample(rawFor(0.45f), 20, 33, false, t);
  int secs = 0;
  while (m.last().vFiltered < 2.5f && secs < 30) { m.addSample(rawFor(3.0f), 20, 33, false, t); t += 1000; ++secs; }
  CHECK(secs <= 5);
  CHECK(m.last().index >= 50.0f);
}

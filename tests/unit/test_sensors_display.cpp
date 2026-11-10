// MQ-135 signal chain, environment monitor, buzzer, display logic.
#include <string.h>

#include "../../src/actuators/buzzer_pattern.h"
#include "../../src/communication/message_protocol.h"
#include "../../src/display/screen_logic.h"
#include "../../src/monitoring/air_quality_monitor.h"
#include "../../src/monitoring/environment_monitor.h"
#include "../../src/monitoring/severity_logic.h"
#include "../../src/utils/text_utils.h"
#include "test_framework.h"

// ---- MQ-135 (alarm by AO voltage) ----------------------------------------
// ADC count that the ESP32 reports for a given AO voltage (inverse of the chain).
static uint16_t rawFor(float vao) {
  const float vPin = vao * mq135::DIVIDER_R_BOTTOM / (mq135::DIVIDER_R_TOP + mq135::DIVIDER_R_BOTTOM);
  return static_cast<uint16_t>(vPin / mq135::ADC_VREF * mq135::ADC_MAX_COUNTS + 0.5f);
}

TEST(mq135_voltage_chain) {
  CHECK_NEAR(AirQualityMonitor::adcToVoltage(4095), 3.30, 1e-4);
  CHECK_NEAR(AirQualityMonitor::adcToVoltage(0), 0.0, 1e-6);
  // Divider undone: V_AO = V_pin * (Rtop + Rbottom) / Rbottom
  const float k = mq135::DIVIDER_R_BOTTOM / (mq135::DIVIDER_R_TOP + mq135::DIVIDER_R_BOTTOM);
  CHECK_NEAR(AirQualityMonitor::pinToSensorVoltage(5.0f * k), 5.0, 0.01);
  // Installed divider 10k / 2x10k (series): 5.0 V at AO -> 3.333 V at the pin
  CHECK_NEAR(5.0f * k, 3.333, 0.01);
}

TEST(mq135_alarm_voltage_follows_ao) {
  AirQualityMonitor m;
  m.begin(0);
  m.skipWarmup();
  for (int i = 0; i < 20; ++i) m.addSample(rawFor(2.5f));
  CHECK(m.usable(20000));
  CHECK_NEAR(m.last().vAo, 2.5, 0.01);
  CHECK_NEAR(m.last().vAlarm, 2.5, 0.01);
}

TEST(mq135_air_index_on_the_radio_is_ao_percent) {
  CHECK_EQ(proto::airIndex(2.5f), 50u);
  CHECK_EQ(proto::airIndex(0.26f), 5u);
  CHECK_EQ(proto::airIndex(9.0f), 100u);  // clamped
  CHECK_EQ(proto::airIndex(-1.0f), 0u);
}

TEST(mq135_warmup_blocks_usage) {
  AirQualityMonitor m;
  m.begin(0);
  m.addSample(rawFor(0.5f));
  CHECK(m.warmingUp(1000));
  CHECK(!m.usable(1000));
  CHECK(m.usable(cfg::MQ135_WARMUP_MS + 1));
}

TEST(mq135_follows_a_gas_step_at_once) {
  // Bench case: clean 0.45 V, gas brings AO to 3.0 V. Instant attack: the
  // alarm value must pass 2.5 V on the first sample.
  AirQualityMonitor m;
  m.begin(0);
  m.skipWarmup();
  for (int i = 0; i < 10; ++i) m.addSample(rawFor(0.45f));
  m.addSample(rawFor(3.0f));
  CHECK(m.last().vAlarm >= 2.5f);
}

TEST(mq135_open_circuit_fault) {
  AirQualityMonitor m;
  m.begin(0);
  m.skipWarmup();
  for (int i = 0; i < cfg::MQ135_FAIL_LIMIT; ++i) m.addSample(0);
  CHECK(m.faulted());
  CHECK(!m.usable(0));
}

TEST(mq135_fault_needs_consecutive_good_samples_to_clear) {
  // Bench case: AO open -> ADC at the floor, with an occasional noisy sample
  // just above it. The fault must not flap on each noisy sample.
  AirQualityMonitor m;
  m.begin(0);
  m.skipWarmup();
  for (int i = 0; i < cfg::MQ135_FAIL_LIMIT; ++i) m.addSample(0);
  CHECK(m.faulted());
  m.addSample(1500);   // one good-looking sample
  CHECK(m.faulted());  // still faulted
  for (int i = 0; i < cfg::MQ135_OK_TO_CLEAR; ++i) m.addSample(1500);
  CHECK(!m.faulted());
}

TEST(mq135_saturated_pin_is_strong_gas_not_fault) {
  // With the 10k/20k divider the pin saturates only under STRONG gas.
  AirQualityMonitor m;
  m.begin(0);
  m.skipWarmup();
  for (int i = 0; i < cfg::MQ135_FAIL_LIMIT; ++i) m.addSample(4095);
  CHECK(!m.faulted());
  CHECK(m.last().vAlarm > 4.9f);
}

TEST(mq135_gas_thresholds_in_volts) {
  HysteresisClassifier c(thr::GAS_VOLTS);
  CHECK(c.update(0.21f) == Severity::Normal);   // clean air on the bench
  CHECK(c.update(0.58f) == Severity::Normal);   // was a false MEDIUM with 0.30 V
  CHECK(c.update(0.89f) == Severity::Normal);
  CHECK(c.update(0.91f) == Severity::Medium);   // no LOW: MEDIUM from 0.90 V
  CHECK(c.update(1.20f) == Severity::Medium);   // MEDIUM band 0.90-1.5 V
  CHECK(c.update(1.49f) == Severity::Medium);
  CHECK(c.update(1.55f) == Severity::High);     // HIGH from 1.5 V
  CHECK(c.update(1.47f) == Severity::High);     // within 0.05 V hysteresis
  CHECK(c.update(1.40f) == Severity::Medium);
  CHECK(c.update(0.86f) == Severity::Medium);   // stays until < 0.85 V
  CHECK(c.update(0.84f) == Severity::Normal);   // never passes through LOW
}

TEST(mq135_simulation_override) {
  AirQualityMonitor m;
  m.begin(0);  // still warming up, no sensor
  m.simulateVolts(true, 1.2f);
  m.addSample(0);
  CHECK(m.usable(0));
  CHECK_NEAR(m.last().vAlarm, 1.2, 1e-4);
  CHECK(m.last().simulated);
  m.simulateFault(true);
  CHECK(!m.usable(0));
}

// ---- Environment -----------------------------------------------------------
TEST(env_fault_after_consecutive_failures) {
  EnvironmentMonitor e;
  e.addReading(true, 25, 50);
  CHECK(e.valid());
  for (int i = 0; i < cfg::ENV_FAIL_LIMIT; ++i) e.addReading(false, NAN, NAN);
  CHECK(e.faulted());
  CHECK(!e.valid());
  CHECK_NEAR(e.temperature(), 25, 1e-6);  // last good value kept for display
  e.addReading(true, 26, 50);
  CHECK(!e.faulted());
}

TEST(env_implausible_values_rejected) {
  EnvironmentMonitor e;
  e.addReading(true, 150, 50);
  CHECK(!e.valid());
  e.addReading(true, 25, 120);
  CHECK(!e.valid());
}

TEST(env_simulation) {
  EnvironmentMonitor e;
  e.simulateTemperature(true, 35);
  e.simulateHumidity(true, 80);
  e.addReading(false, NAN, NAN);   // hardware absent
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
  CHECK(d.push("WHATSAPP_GATEWAY", "Teste"));
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
  d.push("G", longTxt);
  d.push("G", "segunda");
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
  d.push("G", "um");
  d.push("G", "dois");
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

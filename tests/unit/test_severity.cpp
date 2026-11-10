// Classification, hysteresis, persistence and heat index.
#include "../../src/config/thresholds.h"
#include "../../src/monitoring/severity_logic.h"
#include "test_framework.h"

// Generic 0..100 scale used to test the classifier itself (15 / 30 / 50, hyst 5).
static const ThresholdSet kScale = {{true, 15.0f, 30.0f, 50.0f}, {false, 0, 0, 0}, 5.0f};

TEST(classifier_air_levels) {
  HysteresisClassifier c(kScale);
  CHECK(c.update(0) == Severity::Normal);
  c.reset(); CHECK(c.update(15) == Severity::Low);
  c.reset(); CHECK(c.update(30) == Severity::Medium);
  c.reset(); CHECK(c.update(50) == Severity::High);
  c.reset(); CHECK(c.update(100) == Severity::High);
}

TEST(classifier_value_hysteresis) {
  HysteresisClassifier c(kScale);
  CHECK(c.update(31) == Severity::Medium);
  CHECK(c.update(28) == Severity::Medium);   // > 30-5 -> stays
  CHECK(c.update(25.5f) == Severity::Medium);
  CHECK(c.update(24.9f) == Severity::Low);   // < 25 -> leaves MEDIUM
  CHECK(c.update(29) == Severity::Low);      // re-entry needs >= 30
  CHECK(c.update(30) == Severity::Medium);
  CHECK(c.update(9.9f) == Severity::Normal); // < 15-5
}

TEST(classifier_two_sided_temperature) {
  HysteresisClassifier c(thr::TEMPERATURE);
  CHECK(c.update(22) == Severity::Normal);
  CHECK(c.update(17.5f) == Severity::Low);
  CHECK(c.update(18.5f) == Severity::Low);   // hysteresis 1.0 -> leave at > 19
  CHECK(c.update(19.1f) == Severity::Normal);
  CHECK(c.update(15) == Severity::Medium);
  CHECK(c.update(11) == Severity::High);
  c.reset();
  CHECK(c.update(27.0f) == Severity::Low);
  CHECK(c.update(33.0f) == Severity::Medium);
  CHECK(c.update(40.0f) == Severity::High);
  // apparent vs dry-bulb: hot apparent, normal dry-bulb
  c.reset();
  CHECK(c.update(41.0f, 31.0f) == Severity::High);
}

TEST(classifier_humidity) {
  HysteresisClassifier c(thr::HUMIDITY);
  CHECK(c.update(45) == Severity::Normal);
  CHECK(c.update(71) == Severity::Normal);  // LOW starts at 75 %
  CHECK(c.update(76) == Severity::Low);     // MEDIUM starts at 80 %
  CHECK(c.update(81) == Severity::Medium);
  CHECK(c.update(86) == Severity::High);
  c.reset();
  CHECK(c.update(29) == Severity::Low);
  CHECK(c.update(24) == Severity::Medium);
  CHECK(c.update(19) == Severity::High);
}

TEST(classifier_nan_is_not_classified) {
  HysteresisClassifier c(kScale);
  CHECK(c.update(NAN) == Severity::Normal);  // caller must not feed invalid data
}

TEST(tracker_escalation_requires_persistence) {
  SeverityTracker t(5000, 60000);
  CHECK(!t.update(Severity::High, 0));
  CHECK(!t.update(Severity::High, 4999));
  CHECK(t.update(Severity::High, 5000));
  CHECK(t.confirmed() == Severity::High);
}

TEST(tracker_spike_is_ignored) {
  SeverityTracker t(5000, 60000);
  t.update(Severity::High, 0);
  t.update(Severity::Normal, 1000);   // spike ended
  t.update(Severity::High, 2000);
  CHECK(!t.update(Severity::High, 6000));  // only 4 s continuous
  CHECK(t.update(Severity::High, 7000));
}

TEST(tracker_fluctuating_levels_escalate_to_sustained_minimum) {
  SeverityTracker t(5000, 60000);
  // alternates MEDIUM/HIGH: MEDIUM is sustained, HIGH is not
  uint32_t now = 0;
  for (int i = 0; i <= 6; ++i, now += 1000) t.update(i % 2 ? Severity::High : Severity::Medium, now);
  CHECK(t.confirmed() == Severity::Medium);
}

TEST(tracker_recovery_requires_stable_period) {
  SeverityTracker t(0, 60000);
  t.update(Severity::High, 0);
  CHECK(t.confirmed() == Severity::High);
  t.update(Severity::Normal, 1000);
  CHECK(!t.update(Severity::Normal, 60999));
  CHECK(t.update(Severity::Normal, 61000));
  CHECK(t.confirmed() == Severity::Normal);
}

TEST(tracker_recovery_interrupted_restarts) {
  SeverityTracker t(0, 60000);
  t.update(Severity::High, 0);
  t.update(Severity::Normal, 1000);
  t.update(Severity::High, 30000);     // relapse
  t.update(Severity::Normal, 31000);
  CHECK(!t.update(Severity::Normal, 61000));
  CHECK(t.update(Severity::Normal, 91000));
}

TEST(tracker_partial_recovery_to_medium) {
  SeverityTracker t(0, 10000);
  t.update(Severity::High, 0);
  t.update(Severity::Medium, 1000);
  CHECK(t.update(Severity::Medium, 11000));
  CHECK(t.confirmed() == Severity::Medium);
}

TEST(tracker_millis_wraparound) {
  SeverityTracker t(5000, 60000);
  const uint32_t base = 0xFFFFF000u;
  t.update(Severity::Low, base);
  CHECK(!t.update(Severity::Low, base + 4000));
  CHECK(t.update(Severity::Low, base + 5000));  // crosses 2^32
}

TEST(tracker_force_restore) {
  SeverityTracker t(5000, 60000);
  t.force(Severity::High, 1000);
  CHECK(t.confirmed() == Severity::High);
  t.update(Severity::Normal, 2000);
  CHECK(!t.update(Severity::Normal, 61000));
  CHECK(t.update(Severity::Normal, 62000));
}

TEST(heat_index_matches_noaa_table) {
  // NOAA table: 90 F / 70 % RH -> 106 F (41.1 C)
  CHECK_NEAR(heatIndexC(32.22f, 70), 41.1, 0.6);
  // 80 F / 40 % -> 80 F
  CHECK_NEAR(heatIndexC(26.67f, 40), 26.9, 0.6);
  // cool air: simple formula ~ T
  CHECK_NEAR(heatIndexC(20.0f, 50), 19.8, 1.0);
}

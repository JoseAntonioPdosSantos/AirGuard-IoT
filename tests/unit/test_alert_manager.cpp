// Alert state machine: events, recovery, reminders, relay policy, faults.
#include "../../src/monitoring/alert_manager.h"
#include "test_framework.h"

// The state machine is tested on a generic 0..100 "air" scale with a LOW level
// (15 / 30 / 50, hyst 5); the real gas limits (volts) are tested at the end.
static const ThresholdSet kAir = {{true, 15.0f, 30.0f, 50.0f}, {false, 0, 0, 0}, 5.0f};

static int drain(AlertManager& m, AlertEvent* last = nullptr) {
  int n = 0;
  AlertEvent e;
  while (m.popEvent(e)) { ++n; if (last) *last = e; }
  return n;
}

// Feeds a constant air value once per second for `ms`.
static void feedAir(AlertManager& m, float idx, uint32_t& now, uint32_t ms) {
  for (uint32_t t = 0; t <= ms; t += 1000, now += 1000) m.updateAir(true, idx, now);
}

TEST(alert_normal_generates_nothing) {
  AlertManager m(1, kAir);
  uint32_t now = 0;
  feedAir(m, 3, now, 60000);
  CHECK_EQ(drain(m), 0);
  CHECK(m.overall() == Severity::Normal);
  CHECK(!m.relayDemand());
}

TEST(alert_air_high_event_and_relay) {
  AlertManager m(7, kAir);
  uint32_t now = 0;
  feedAir(m, 70, now, 6000);
  AlertEvent e;
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.kind == EventKind::Alert);
  CHECK(e.category == Category::AirQuality);
  CHECK(e.severity == Severity::High);
  CHECK(e.escalation);
  CHECK_EQ(e.eventId >> 16, 7);  // boot counter in the high half
  CHECK(m.relayDemand());
  CHECK_EQ(m.highMask(), 1);
}

TEST(alert_medium_then_high_is_immediate_escalation_event) {
  AlertManager m(1, kAir);
  uint32_t now = 0;
  feedAir(m, 35, now, 6000);
  AlertEvent e;
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.severity == Severity::Medium);
  CHECK(!m.relayDemand());
  feedAir(m, 60, now, 6000);
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.severity == Severity::High);
  CHECK(e.previous == Severity::Medium);
  CHECK(e.escalation);
}

TEST(alert_recovery_event_after_stable_normal) {
  AlertManager m(1, kAir);
  uint32_t now = 0;
  feedAir(m, 70, now, 6000);
  drain(m);
  if (cfg::AIR_RECOVER_MS >= 2000) {  // (bench config uses 0 = immediate)
    feedAir(m, 2, now, cfg::AIR_RECOVER_MS - 1000);
    CHECK_EQ(drain(m), 0);          // not yet stable for AIR_RECOVER_MS
    CHECK(m.relayDemand());
  }
  feedAir(m, 2, now, 2000);
  AlertEvent e;
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.kind == EventKind::Recovery);
  CHECK(e.severity == Severity::Normal);
  CHECK(e.previous == Severity::High);
  CHECK(!m.relayDemand());
}

TEST(alert_oscillation_does_not_storm) {
  AlertManager m(1, kAir);
  uint32_t now = 0;
  // index oscillating around the LOW threshold (15) +-3 for 10 minutes
  for (int i = 0; i < 600; ++i, now += 1000) m.updateAir(true, (i % 2) ? 18 : 12, now);
  // Only the first LOW confirmation; hysteresis keeps it from flapping.
  CHECK(drain(m) <= 1);
}

TEST(alert_invalid_reading_holds_state) {
  AlertManager m(1, kAir);
  uint32_t now = 0;
  feedAir(m, 70, now, 6000);
  drain(m);
  for (int i = 0; i < 300; ++i, now += 1000) m.updateAir(false, 0, now);
  CHECK(m.category(Category::AirQuality) == Severity::High);  // NOT normal
  CHECK_EQ(drain(m), 0);
}

TEST(alert_temperature_and_humidity) {
  AlertManager m(1, kAir);
  uint32_t now = 0;
  for (int i = 0; i < 20; ++i, now += 1000) m.updateEnvironment(true, 33, 35, 50, now);
  AlertEvent e;
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.category == Category::Temperature);
  CHECK(e.severity == Severity::Medium);
  for (int i = 0; i < 40; ++i, now += 1000) m.updateEnvironment(true, 33, 35, 87, now);  // MEDIUM from 85 %
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.category == Category::Humidity);
  CHECK(e.severity == Severity::Medium);
  CHECK(m.overall() == Severity::Medium);
}

TEST(alert_low_humidity) {
  AlertManager m(1, kAir);
  uint32_t now = 0;
  for (int i = 0; i < 40; ++i, now += 1000) m.updateEnvironment(true, 22, 22, 27, now);
  AlertEvent e;
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.category == Category::Humidity);
  CHECK(e.severity == Severity::Low);
}

TEST(alert_system_flags) {
  AlertManager m(1, kAir);
  m.setSystemFlags(SYS_ENV_SENSOR_FAULT, 1000);
  AlertEvent e;
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.category == Category::System);
  CHECK(e.severity == Severity::Medium);
  CHECK_EQ(e.systemFlags, SYS_ENV_SENSOR_FAULT);
  m.setSystemFlags(SYS_ENV_SENSOR_FAULT, 2000);  // unchanged
  CHECK_EQ(drain(m), 0);
  m.setSystemFlags(0, 3000);
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.kind == EventKind::Recovery);
  CHECK(!m.relayDemand());  // SYSTEM never drives the relay by default
}

TEST(alert_reminder_while_persisting) {
  AlertManager m(1, kAir);
  uint32_t now = 0;
  feedAir(m, 70, now, 6000);
  drain(m);
  for (int i = 0; i < 16 * 60; ++i, now += 1000) { m.updateAir(true, 70, now); m.tick(now); }
  AlertEvent e;
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.reminder);
  CHECK(e.severity == Severity::High);
}

TEST(alert_restore_after_reboot) {
  AlertManager m(2, kAir);
  m.restoreHigh(0x01, 0);
  CHECK(m.category(Category::AirQuality) == Severity::High);
  CHECK(m.relayDemand());
  CHECK(m.restored(Category::AirQuality));
  uint32_t now = 0;
  // sensor warming up -> not usable -> still HIGH
  for (int i = 0; i < 300; ++i, now += 1000) m.updateAir(false, 0, now);
  CHECK(m.relayDemand());
  feedAir(m, 1, now, 61000);
  AlertEvent e;
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.kind == EventKind::Recovery);
  CHECK(e.restored);
  CHECK(!m.relayDemand());
}

TEST(event_ids_unique_and_monotonic) {
  EventIdGenerator g;
  g.setBootCount(3);
  uint32_t a = g.next(), b = g.next();
  CHECK(a != b);
  CHECK_EQ(a, (3u << 16) | 1);
  CHECK_EQ(b, (3u << 16) | 2);
}

TEST(alert_restore_announced_once) {
  AlertManager m(4, kAir);
  m.restoreHigh(0x02, 0);  // temperature was HIGH before the reboot
  m.announceRestored(3000);
  AlertEvent e;
  CHECK_EQ(drain(m, &e), 1);
  CHECK(e.kind == EventKind::Alert);
  CHECK(e.category == Category::Temperature);
  CHECK(e.severity == Severity::High);
  CHECK(e.restored);
  CHECK_EQ(e.eventId, (4u << 16) | 1);
}

TEST(alert_default_gas_limits_are_volts) {
  AlertManager m(1);  // thr::GAS_VOLTS: MEDIUM 0.90 V, HIGH 1.5 V, no LOW
  uint32_t now = 0;
  feedAir(m, 0.5f, now, 2000);
  CHECK(m.category(Category::AirQuality) == Severity::Normal);
  feedAir(m, 1.0f, now, 2000);
  CHECK(m.category(Category::AirQuality) == Severity::Medium);
  feedAir(m, 2.0f, now, 2000);
  CHECK(m.category(Category::AirQuality) == Severity::High);
  CHECK(m.relayDemand());
}

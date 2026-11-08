// AirGuard IoT - alert decision engine (platform independent).
//
// Responsibilities (and nothing else):
//   * per-category value hysteresis + temporal persistence;
//   * confirmed severity per category and overall;
//   * generation of ALERT / RECOVERY events with unique event_id;
//   * reminders while a condition persists;
//   * relay demand according to the configured policy.
// It does NOT read sensors, drive outputs or talk to the radio.
#pragma once
#include <stdint.h>

#include "../config/config.h"
#include "alert_types.h"
#include "severity_logic.h"

// event_id = (bootCount << 16) | sequence -> unique across reboots as long
// as the boot counter is persisted (see utils/persistent_store).
class EventIdGenerator {
 public:
  explicit EventIdGenerator(uint16_t bootCount) : boot_(bootCount) {}
  uint32_t next() {
    if (++seq_ == 0) seq_ = 1;
    return (static_cast<uint32_t>(boot_) << 16) | seq_;
  }
 private:
  uint16_t boot_;
  uint16_t seq_ = 0;
};

class AlertManager {
 public:
  explicit AlertManager(uint16_t bootCount, const ThresholdSet& airThresholds = thr::AIR_QUALITY);

  // usable=false -> the category keeps its last confirmed level (a bad
  // reading is never interpreted as "normal").
  void updateAir(bool usable, float airIndex, uint32_t nowMs);
  void updateEnvironment(bool usable, float tempC, float apparentC, float rh, uint32_t nowMs);
  void setSystemFlags(uint16_t flags, uint32_t nowMs);
  void setSnapshot(const SensorSnapshot& s) { snapshot_ = s; }
  void tick(uint32_t nowMs);  // reminders

  // Boot restore: categories in `highMask` start confirmed HIGH.
  void restoreHigh(uint8_t highMask, uint32_t nowMs);
  // Emits one ALERT (note RESTORED) per restored category so the gateway
  // learns the state was re-armed after a reboot. Call once, after the
  // first sensor snapshot is available.
  void announceRestored(uint32_t nowMs);

  bool popEvent(AlertEvent& out);
  uint8_t pendingEvents() const { return count_; }

  Severity category(Category c) const { return confirmed_[static_cast<uint8_t>(c)]; }
  Severity overall() const;
  Category worstCategory() const;
  bool     relayDemand() const;
  uint8_t  highMask() const;  // bit i = category i is HIGH
  uint16_t systemFlags() const { return sysFlags_; }
  bool     restored(Category c) const { return restored_[static_cast<uint8_t>(c)]; }
  uint32_t eventsGenerated() const { return generated_; }

  static Severity systemSeverityFor(uint16_t flags);

 private:
  void onConfirmedChange(Category c, Severity prev, Severity now, uint32_t nowMs);
  void emit(const AlertEvent& e);

  EventIdGenerator ids_;
  HysteresisClassifier airCls_, tempCls_, humCls_;
  SeverityTracker airTrk_, tempTrk_, humTrk_;
  Severity confirmed_[CATEGORY_COUNT] = {};
  bool     restored_[CATEGORY_COUNT] = {};
  uint32_t lastAlertMs_[CATEGORY_COUNT] = {};
  uint16_t sysFlags_ = 0;
  SensorSnapshot snapshot_;

  static constexpr uint8_t QUEUE = 8;
  AlertEvent queue_[QUEUE];
  uint8_t head_ = 0, count_ = 0;
  uint32_t generated_ = 0;
  uint32_t dropped_ = 0;
};

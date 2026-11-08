#include "alert_manager.h"

#include "../config/thresholds.h"

AlertManager::AlertManager(uint16_t bootCount, const ThresholdSet& airThresholds)
    : ids_(bootCount),
      airCls_(airThresholds),
      tempCls_(thr::TEMPERATURE),
      humCls_(thr::HUMIDITY),
      airTrk_(cfg::AIR_ESCALATE_MS, cfg::AIR_RECOVER_MS),
      tempTrk_(cfg::TEMP_ESCALATE_MS, cfg::TEMP_RECOVER_MS),
      humTrk_(cfg::HUM_ESCALATE_MS, cfg::HUM_RECOVER_MS) {}

Severity AlertManager::systemSeverityFor(uint16_t f) {
  if (f & (SYS_ENV_SENSOR_FAULT | SYS_AIR_SENSOR_FAULT)) return Severity::Medium;
  if (f) return Severity::Low;
  return Severity::Normal;
}

void AlertManager::updateAir(bool usable, float idx, uint32_t now) {
  if (!usable) return;  // hold last confirmed level
  const Severity prev = airTrk_.confirmed();
  if (airTrk_.update(airCls_.update(idx), now))
    onConfirmedChange(Category::AirQuality, prev, airTrk_.confirmed(), now);
}

void AlertManager::updateEnvironment(bool usable, float t, float apparent, float rh,
                                     uint32_t now) {
  if (!usable) return;
  Severity prev = tempTrk_.confirmed();
  if (tempTrk_.update(tempCls_.update(apparent, t), now))
    onConfirmedChange(Category::Temperature, prev, tempTrk_.confirmed(), now);
  prev = humTrk_.confirmed();
  if (humTrk_.update(humCls_.update(rh), now))
    onConfirmedChange(Category::Humidity, prev, humTrk_.confirmed(), now);
}

void AlertManager::setSystemFlags(uint16_t flags, uint32_t now) {
  if (flags == sysFlags_) return;
  const uint16_t added = flags & ~sysFlags_;
  const Severity prev = systemSeverityFor(sysFlags_);
  const Severity next = systemSeverityFor(flags);
  sysFlags_ = flags;
  // Report when severity changes or a new problem appears (removals that keep
  // the same severity are silent to avoid message churn).
  if (next != prev || added) {
    const uint8_t i = static_cast<uint8_t>(Category::System);
    confirmed_[i] = next;
    AlertEvent e;
    e.kind = next == Severity::Normal ? EventKind::Recovery : EventKind::Alert;
    e.category = Category::System;
    e.severity = next;
    e.previous = prev;
    e.escalation = next > prev;
    e.systemFlags = flags;
    e.uptimeS = now / 1000;
    e.snapshot = snapshot_;
    e.eventId = ids_.next();
    lastAlertMs_[i] = now;
    emit(e);
  }
}

void AlertManager::onConfirmedChange(Category c, Severity prev, Severity next, uint32_t now) {
  const uint8_t i = static_cast<uint8_t>(c);
  confirmed_[i] = next;
  const bool wasRestored = restored_[i];
  restored_[i] = false;
  AlertEvent e;
  e.kind = next == Severity::Normal ? EventKind::Recovery : EventKind::Alert;
  e.category = c;
  e.severity = next;
  e.previous = prev;
  e.escalation = next > prev;
  e.restored = wasRestored;
  e.uptimeS = now / 1000;
  e.snapshot = snapshot_;
  e.eventId = ids_.next();
  lastAlertMs_[i] = now;
  emit(e);
}

void AlertManager::tick(uint32_t now) {
  if (cfg::ALERT_REMINDER_MS == 0) return;
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) {
    if (static_cast<uint8_t>(confirmed_[i]) < cfg::ALERT_REMINDER_MIN_SEVERITY) continue;
    if ((uint32_t)(now - lastAlertMs_[i]) < cfg::ALERT_REMINDER_MS) continue;
    AlertEvent e;
    e.kind = EventKind::Alert;
    e.category = static_cast<Category>(i);
    e.severity = e.previous = confirmed_[i];
    e.reminder = true;
    e.systemFlags = (e.category == Category::System) ? sysFlags_ : 0;
    e.uptimeS = now / 1000;
    e.snapshot = snapshot_;
    e.eventId = ids_.next();
    lastAlertMs_[i] = now;
    emit(e);
  }
}

void AlertManager::restoreHigh(uint8_t mask, uint32_t now) {
  if (mask & (1 << 0)) { airTrk_.force(Severity::High, now); }
  if (mask & (1 << 1)) { tempTrk_.force(Severity::High, now); }
  if (mask & (1 << 2)) { humTrk_.force(Severity::High, now); }
  for (uint8_t i = 0; i < 3; ++i) {
    if (mask & (1 << i)) {
      confirmed_[i] = Severity::High;
      restored_[i] = true;
      lastAlertMs_[i] = now;
    }
  }
}

void AlertManager::announceRestored(uint32_t now) {
  for (uint8_t i = 0; i < 3; ++i) {
    if (!restored_[i] || confirmed_[i] != Severity::High) continue;
    AlertEvent e;
    e.kind = EventKind::Alert;
    e.category = static_cast<Category>(i);
    e.severity = e.previous = Severity::High;
    e.restored = true;
    e.uptimeS = now / 1000;
    e.snapshot = snapshot_;
    e.eventId = ids_.next();
    lastAlertMs_[i] = now;
    emit(e);
  }
}

void AlertManager::emit(const AlertEvent& e) {
  ++generated_;
  if (count_ == QUEUE) {  // overwrite oldest; the newest state matters most
    head_ = (head_ + 1) % QUEUE;
    --count_;
    ++dropped_;
  }
  queue_[(head_ + count_) % QUEUE] = e;
  ++count_;
}

bool AlertManager::popEvent(AlertEvent& out) {
  if (count_ == 0) return false;
  out = queue_[head_];
  head_ = (head_ + 1) % QUEUE;
  --count_;
  return true;
}

Severity AlertManager::overall() const {
  Severity s = Severity::Normal;
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) s = maxSeverity(s, confirmed_[i]);
  return s;
}

Category AlertManager::worstCategory() const {
  uint8_t best = 0;
  for (uint8_t i = 1; i < CATEGORY_COUNT; ++i)
    if (confirmed_[i] > confirmed_[best]) best = i;
  return static_cast<Category>(best);
}

bool AlertManager::relayDemand() const {
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) {
    if (confirmed_[i] == Severity::High && cfg::RELAY_ON_HIGH[i]) return true;
    if (cfg::RELAY_ON_MEDIUM && confirmed_[i] == Severity::Medium && cfg::RELAY_ON_HIGH[i])
      return true;
  }
  return false;
}

uint8_t AlertManager::highMask() const {
  uint8_t m = 0;
  for (uint8_t i = 0; i < 3; ++i)
    if (confirmed_[i] == Severity::High) m |= (1 << i);
  return m;
}

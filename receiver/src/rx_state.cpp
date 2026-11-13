#include "rx_state.h"

namespace rx {

bool ReceiverState::apply(const Event& e, uint32_t now) {
  const Sev before = overall();
  const bool hadAny = received_;
  sev_[static_cast<uint8_t>(e.category)] = e.severity;  // RECOVERY carries NORMAL
  if (e.hasTemp) { hasTemp_ = true; temp_ = e.temperature; }
  if (e.hasHum) { hasHum_ = true; hum_ = e.humidity; }
  if (e.hasAir) { hasAir_ = true; air_ = e.airIndex; }
  received_ = true;
  lastMs_ = now;
  ++events_;
  return !hadAny || overall() != before;
}

bool ReceiverState::apply(const Heartbeat& h, uint32_t now) {
  const Sev before = overall();
  const bool hadAny = received_;
  for (uint8_t i = 0; i < static_cast<uint8_t>(Cat::Count); ++i) sev_[i] = h.levels[i];
  if (h.hasTemp) { hasTemp_ = true; temp_ = h.temperature; }
  if (h.hasHum) { hasHum_ = true; hum_ = h.humidity; }
  if (h.hasAir) { hasAir_ = true; air_ = h.airIndex; }
  received_ = true;
  lastMs_ = now;
  ++heartbeats_;
  return !hadAny || overall() != before;
}

bool ReceiverState::lost(uint32_t now, uint32_t timeoutMs) const {
  return received_ && timeoutMs > 0 && (uint32_t)(now - lastMs_) > timeoutMs;
}

Sev ReceiverState::overall() const {
  Sev m = Sev::Normal;
  for (Sev s : sev_)
    if (static_cast<uint8_t>(s) > static_cast<uint8_t>(m)) m = s;
  return m;
}

Cat ReceiverState::worst() const {
  // Highest level; on a tie the more important category (gas first).
  uint8_t best = 0;
  for (uint8_t i = 1; i < static_cast<uint8_t>(Cat::Count); ++i)
    if (static_cast<uint8_t>(sev_[i]) > static_cast<uint8_t>(sev_[best])) best = i;
  return static_cast<Cat>(best);
}

Led ReceiverState::led(uint32_t now, uint32_t timeoutMs) const {
  if (!received_) return Led::WaitingBlink;
  if (lost(now, timeoutMs)) return Led::LostBlink;
  switch (overall()) {
    case Sev::High: return Led::Red;
    case Sev::Medium:
    case Sev::Low: return Led::Yellow;
    default: return Led::Green;
  }
}

bool greenLed(Led l, bool messageOnLcd, uint32_t now, uint32_t waitHalfMs, uint32_t msgHalfMs) {
  if (messageOnLcd && l != Led::Yellow && l != Led::Red) return msgHalfMs == 0 || (now / msgHalfMs) % 2 == 0;
  if (l == Led::WaitingBlink) return waitHalfMs == 0 || (now / waitHalfMs) % 2 == 0;
  return l == Led::Green;
}

void HbTracker::onHeartbeat(uint32_t seq) {
  if (any_ && seq == last_) return;  // same frame twice
  ++received_;
  if (!any_ || seq < last_) {        // first one, or the node rebooted
    any_ = true;
    last_ = seq;
    map_ = 1;
    return;
  }
  const uint32_t gap = seq - last_;
  if (gap - 1 < 100000) missed_ += gap - 1;
  map_ = gap >= 32 ? 1 : ((map_ << gap) | 1);
  last_ = seq;
}

}  // namespace rx

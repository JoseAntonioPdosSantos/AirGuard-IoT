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

Led ReceiverState::led() const {
  if (!received_) return Led::WaitingBlink;
  switch (overall()) {
    case Sev::High: return Led::Red;
    case Sev::Medium:
    case Sev::Low: return Led::Yellow;
    default: return Led::Green;
  }
}

}  // namespace rx

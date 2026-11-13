// AirGuard Receiver - what the receiver knows about the node (pure, unit tested).
//
// The node only transmits EVENTS (ALERT when a category changes level,
// RECOVERY when it returns to normal, REMINDER while an alert lasts). The
// receiver keeps the last level of each category; the overall level is the
// worst of them and drives the LEDs:
//   NORMAL -> green, LOW / MEDIUM -> yellow, HIGH -> red.
#pragma once
#include <stdint.h>

#include "event_parser.h"

namespace rx {

enum class Led : uint8_t { WaitingBlink, Green, Yellow, Red, LostBlink };

class ReceiverState {
 public:
  // Applies a decoded event. Returns true if the overall level changed.
  bool apply(const Event& e, uint32_t nowMs);
  // Heartbeat: re-synchronises every level and refreshes the readings.
  bool apply(const Heartbeat& h, uint32_t nowMs);
  // No frame for `timeoutMs` (after at least one was received).
  bool lost(uint32_t nowMs, uint32_t timeoutMs) const;

  bool     anyReceived() const { return received_; }
  Sev      overall() const;
  Cat      worst() const;                 // category with the highest level
  Sev      level(Cat c) const { return sev_[static_cast<uint8_t>(c)]; }
  Led      led(uint32_t nowMs = 0, uint32_t timeoutMs = 0) const;
  uint32_t heartbeats() const { return heartbeats_; }

  bool  hasTemp() const { return hasTemp_; }
  float temperature() const { return temp_; }
  bool  hasHum() const { return hasHum_; }
  float humidity() const { return hum_; }
  bool  hasAir() const { return hasAir_; }
  uint32_t airIndex() const { return air_; }
  uint32_t lastMsgMs() const { return lastMs_; }
  uint32_t events() const { return events_; }

 private:
  Sev      sev_[static_cast<uint8_t>(Cat::Count)] = {};
  bool     received_ = false;
  bool     hasTemp_ = false, hasHum_ = false, hasAir_ = false;
  float    temp_ = 0, hum_ = 0;
  uint32_t air_ = 0;
  uint32_t lastMs_ = 0, events_ = 0, heartbeats_ = 0;
};

// Level of the green LED. While a WhatsApp message is on the LCD it blinks
// fast (half period `msgHalfMs`), so it cannot be taken for the slow blink of
// "nothing received yet" (`waitHalfMs`); otherwise it follows `l` as before.
// A message is only on the LCD when no alert is active.
bool greenLed(Led l, bool messageOnLcd, uint32_t nowMs, uint32_t waitHalfMs, uint32_t msgHalfMs);

// Which heartbeats were heard: the node numbers them 1, 2, 3... A gap in the
// sequence = heartbeats lost on the air. A smaller seq = the node rebooted.
class HbTracker {
 public:
  void onHeartbeat(uint32_t seq);
  bool     any() const { return any_; }
  uint32_t lastSeq() const { return last_; }
  uint32_t map() const { return map_; }       // bit i = seq (lastSeq - i) heard
  uint32_t received() const { return received_; }
  uint32_t missed() const { return missed_; }

 private:
  bool     any_ = false;
  uint32_t last_ = 0, map_ = 0, received_ = 0, missed_ = 0;
};

}  // namespace rx

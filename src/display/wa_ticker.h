// AirGuard IoT - a WhatsApp message scrolling in the OLED's yellow band
// (pure, unit tested; drawn by OledManager::drawHeader).
//
// Rules (docs/12_whatsapp.md):
//   * "sender: text" scrolls right to left in the 2x font, slower than the
//     measurements below it (cfg::WA_TICKER_PX_PER_S), crosses the screen
//     cfg::WA_TICKER_PASSES times and then the normal header comes back;
//   * messages that arrive meanwhile wait in a small queue;
//   * while any alert is active nothing is shown and what was waiting is
//     discarded: an alert owns the display (the caller also refuses new
//     messages during an alert).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../config/config.h"

class WaTicker {
 public:
  enum class Push : uint8_t { Shown, Queued, Full, Empty };

  // utf8 sender / text as received; stored as display ASCII "sender: text".
  Push push(const char* sender, const char* utf8, uint32_t nowMs);
  // Advances the scroll. alertActive = true stops and empties everything.
  void update(uint32_t nowMs, bool alertActive);
  void clear();

  bool        active() const { return active_; }
  const char* text() const { return active_ ? cur_ : ""; }
  // x of the first character at nowMs (12 px per character, screen 128 px).
  int16_t     x(uint32_t nowMs) const;
  uint8_t     waiting() const { return count_; }
  // The text of the last accepted push (what is relayed to the receiver).
  const char* last() const { return last_; }

  // Time one message stays on screen (all passes), in ms.
  static uint32_t durationMs(size_t chars);

  static constexpr int16_t SCREEN_W = 128;
  static constexpr int16_t CHAR_W = 12;  // 2x font

 private:
  static constexpr size_t CAP = cfg::WA_TEXT_MAX + 1;
  void start(const char* text, uint32_t nowMs);

  char     q_[cfg::WA_MSG_QUEUE][CAP] = {};
  uint8_t  head_ = 0, count_ = 0;
  char     cur_[CAP] = {};
  char     last_[CAP] = {};
  bool     active_ = false;
  uint32_t startMs_ = 0;
  uint32_t durationMs_ = 0;
  int32_t  span_ = 0;  // px travelled in one pass (text width + screen width)
};

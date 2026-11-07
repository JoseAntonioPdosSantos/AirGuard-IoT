// AirGuard IoT - display decision logic (pure, unit tested).
//
// Visual priority: HIGH > MEDIUM > LOW > remote message > main screen.
// A remote message is shown for exactly REMOTE_MSG_PAGE_MS per page; while an
// alert is active the message screen keeps a blinking alert banner, and when
// the alert is HIGH the alert screen is shown for at least one page-time
// between two consecutive messages, so a critical alert is never hidden.
#pragma once
#include <stdint.h>

#include "../config/config.h"
#include "../monitoring/alert_types.h"

enum class Screen : uint8_t { Boot, Main, Alert, Message };

constexpr uint8_t MSG_COLS = 21;           // 128 px / 6 px per char
constexpr uint8_t MSG_LINES_PER_PAGE = 6;  // blue band rows (yellow band: header + alert banner)
constexpr uint8_t MSG_MAX_LINES = 12;      // 2 pages

class RemoteMessageDisplay {
 public:
  // utf8 text from the protocol layer; sanitised and wrapped here.
  bool push(const char* sender, const char* utf8, uint32_t nowMs);
  // Advance timers. `criticalActive` enforces the HIGH alert gap.
  void update(uint32_t nowMs, bool criticalActive);

  bool active() const { return active_; }
  uint8_t page() const { return page_; }
  uint8_t pages() const { return pages_; }
  uint8_t queued() const { return count_; }
  const char* sender() const { return cur_.sender; }
  // Line i (0..MSG_LINES_PER_PAGE-1) of the current page ("" if none).
  const char* line(uint8_t i) const;
  uint32_t shownSinceMs(uint32_t nowMs) const { return nowMs - pageStartMs_; }

 private:
  struct Item {
    char sender[cfg::MAX_ID_LEN + 1];
    char ascii[cfg::MAX_TEXT_BYTES + 1];
  };
  void startNext(uint32_t nowMs);

  Item    q_[cfg::REMOTE_MSG_QUEUE] = {};
  uint8_t head_ = 0, count_ = 0;
  Item    cur_ = {};
  char    lines_[MSG_MAX_LINES][22] = {};
  uint8_t totalLines_ = 0, pages_ = 0, page_ = 0;
  bool    active_ = false;
  uint32_t pageStartMs_ = 0;
  uint32_t lastEndMs_ = 0;
  bool     everEnded_ = false;
};

Screen selectScreen(bool booting, Severity overall, bool messageActive);

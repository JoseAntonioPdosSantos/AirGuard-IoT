// AirGuard Receiver - 16x2 LCD text (pure formatting, unit tested) + driver.
//
//   line 1: "AirGuard RX"            before the first message
//           "SEM SINAL"              no frame (heartbeat) for LINK_TIMEOUT_MS
//           "NORMAL"                 everything normal
//           "HIGH: GAS"              worst category and its level
//   line 2: "25.5C 61% 0.25V"        temperature, humidity, gas (AO volts)
//           "RSSI -45dBm  #12"       alternates every LCD_PAGE_MS (link info)
// A WhatsApp message relayed by the node (LcdMessage) takes both lines for a
// few seconds, then this screen comes back.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "rx_state.h"

namespace rx {

constexpr size_t LCD_COLS = 16;

// Both write exactly LCD_COLS chars (space padded) + NUL; `out` >= 17 bytes.
// `nowMs` / `timeoutMs` detect the lost signal ("SEM SINAL").
void formatLine1(const ReceiverState& s, uint32_t nowMs, uint32_t timeoutMs, char* out);
void formatLine2(const ReceiverState& s, bool linkPage, int lastRssi, uint32_t nowMs,
                 uint32_t timeoutMs, char* out);

// A text shown instead of the normal screen for a limited time. It is
// word-wrapped at 16 columns into pages of 2 lines; the pages share the time
// equally. What does not fit in `maxPages` pages is left out and the last line
// then ends with "...".
// It can appear more than once: `times` appearances of `totalMs` each, with
// the normal screen back for `gapMs` between them (so a one-page message is
// seen to come twice, like the two passes on the node's OLED).
class LcdMessage {
 public:
  // Returns false (nothing shown) if the text has no printable character.
  bool show(const char* text, uint32_t nowMs, uint32_t totalMs, uint8_t maxPages, uint8_t times = 1,
            uint32_t gapMs = 0);
  void cancel() { active_ = false; }
  // The text is on the LCD right now (false in the gap between two appearances).
  bool active(uint32_t nowMs) const;
  // Which appearance is on (or was last on) the LCD: 1..times.
  uint8_t appearance(uint32_t nowMs) const;
  uint8_t pages() const { return pages_; }
  bool    cut() const { return cut_; }  // the text did not fit
  // The two lines on screen at nowMs: LCD_COLS chars (space padded) + NUL each.
  void lines(uint32_t nowMs, char* l1, char* l2) const;

 private:
  static constexpr uint8_t MAX_LINES = 8;
  char     line_[MAX_LINES][LCD_COLS + 1] = {};
  uint8_t  count_ = 0, pages_ = 0;
  bool     active_ = false, cut_ = false;
  uint32_t startMs_ = 0, totalMs_ = 0, gapMs_ = 0;
  uint8_t  times_ = 1;
};

}  // namespace rx

#ifdef ARDUINO
class LcdView {
 public:
  bool begin();                         // Wire must be configured; finds the backpack
  void show(const char* l1, const char* l2);  // writes only lines that changed
  bool ok() const { return ok_; }
  // Checks that the backpack still ACKs; marks the LCD lost (and logs) if not.
  bool stillThere();
  uint8_t address() const { return addr_; }

 private:
  bool    ok_ = false;
  uint8_t addr_ = 0;
  char    shown_[2][17] = {};
};
#endif

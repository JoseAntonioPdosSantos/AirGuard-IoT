#include "wa_ticker.h"

#include <string.h>

#include "../utils/text_utils.h"

namespace {

// "sender: text" as display ASCII in out (cap bytes). A text that does not fit
// ends with "...". Returns the length (0 = nothing to show).
size_t compose(const char* sender, const char* utf8, char* out, size_t cap) {
  size_t n = 0;
  if (sender && sender[0]) {
    // the sender takes at most a third of the line
    n = text::toDisplayAscii(sender, out, cap / 3);
    if (n) { out[n++] = ':'; out[n++] = ' '; }
  }
  // Convert into a larger scratch first to know whether the text was cut.
  char body[cfg::WA_TEXT_MAX * 2];
  const size_t b = text::toDisplayAscii(utf8, body, sizeof body);
  if (b == 0) { out[0] = '\0'; return 0; }
  const size_t room = cap - 1 - n;
  if (b <= room) {
    memcpy(out + n, body, b);
    n += b;
  } else {
    memcpy(out + n, body, room);
    n += room;
    if (room >= 3) memcpy(out + n - 3, "...", 3);
  }
  out[n] = '\0';
  return n;
}

}  // namespace

uint32_t WaTicker::durationMs(size_t chars) {
  const uint32_t span = static_cast<uint32_t>(chars) * CHAR_W + SCREEN_W;
  return span * 1000UL * cfg::WA_TICKER_PASSES / cfg::WA_TICKER_PX_PER_S;
}

void WaTicker::start(const char* text, uint32_t now) {
  strncpy(cur_, text, CAP - 1);
  cur_[CAP - 1] = '\0';
  span_ = static_cast<int32_t>(strlen(cur_)) * CHAR_W + SCREEN_W;
  durationMs_ = durationMs(strlen(cur_));
  startMs_ = now;
  active_ = true;
}

WaTicker::Push WaTicker::push(const char* sender, const char* utf8, uint32_t now) {
  char t[CAP];
  if (compose(sender, utf8, t, sizeof t) == 0) return Push::Empty;
  if (!active_ && count_ == 0) {
    memcpy(last_, t, CAP);
    start(t, now);
    return Push::Shown;
  }
  if (count_ == cfg::WA_MSG_QUEUE) return Push::Full;  // the newest is refused
  memcpy(q_[(head_ + count_) % cfg::WA_MSG_QUEUE], t, CAP);
  memcpy(last_, t, CAP);
  ++count_;
  return Push::Queued;
}

void WaTicker::clear() {
  active_ = false;
  head_ = 0;
  count_ = 0;
}

void WaTicker::update(uint32_t now, bool alertActive) {
  if (alertActive) { clear(); return; }
  if (active_ && (uint32_t)(now - startMs_) >= durationMs_) {
    active_ = false;
    if (count_ > 0) {
      const uint32_t next = startMs_ + durationMs_;  // exact cadence, no drift
      const uint8_t h = head_;
      head_ = static_cast<uint8_t>((head_ + 1) % cfg::WA_MSG_QUEUE);
      --count_;
      start(q_[h], next);
    }
  }
}

int16_t WaTicker::x(uint32_t now) const {
  if (!active_ || span_ <= 0) return SCREEN_W;
  const uint32_t px = static_cast<uint32_t>((static_cast<uint64_t>(now - startMs_) * cfg::WA_TICKER_PX_PER_S) / 1000);
  return static_cast<int16_t>(SCREEN_W - static_cast<int32_t>(px % static_cast<uint32_t>(span_)));
}

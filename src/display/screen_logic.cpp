#include "screen_logic.h"

#include <string.h>

#include "../utils/text_utils.h"

bool RemoteMessageDisplay::push(const char* sender, const char* utf8) {
  if (count_ == cfg::REMOTE_MSG_QUEUE) return false;  // drop newest when full
  Item& it = q_[(head_ + count_) % cfg::REMOTE_MSG_QUEUE];
  strncpy(it.sender, sender ? sender : "", cfg::MAX_ID_LEN);
  it.sender[cfg::MAX_ID_LEN] = '\0';
  if (text::toDisplayAscii(utf8, it.ascii, sizeof it.ascii) == 0) return false;
  ++count_;  // display starts on the next update() call (same loop iteration)
  return true;
}

void RemoteMessageDisplay::startNext(uint32_t now) {
  cur_ = q_[head_];
  head_ = (head_ + 1) % cfg::REMOTE_MSG_QUEUE;
  --count_;
  memset(lines_, 0, sizeof lines_);
  totalLines_ = text::wrap(cur_.ascii, MSG_COLS, lines_, MSG_MAX_LINES);
  if (totalLines_ > MSG_MAX_LINES) {  // should not happen with MAX_TEXT_BYTES=120
    totalLines_ = MSG_MAX_LINES;
    char* last = lines_[MSG_MAX_LINES - 1];
    const size_t n = strlen(last);
    if (n >= 3) memcpy(last + (n > 18 ? 18 : n - 3), "...", 4);
  }
  pages_ = static_cast<uint8_t>((totalLines_ + MSG_LINES_PER_PAGE - 1) / MSG_LINES_PER_PAGE);
  if (pages_ == 0) pages_ = 1;
  page_ = 0;
  active_ = true;
  pageStartMs_ = now;
}

void RemoteMessageDisplay::update(uint32_t now, bool criticalActive) {
  if (active_ && (uint32_t)(now - pageStartMs_) >= cfg::REMOTE_MSG_PAGE_MS) {
    if (page_ + 1 < pages_) {
      ++page_;
      pageStartMs_ += cfg::REMOTE_MSG_PAGE_MS;  // exact cadence, no drift
    } else {
      active_ = false;
      everEnded_ = true;
      lastEndMs_ = pageStartMs_ + cfg::REMOTE_MSG_PAGE_MS;
    }
  }
  if (!active_ && count_ > 0) {
    // While HIGH is active, give the alert screen one full page-time between messages.
    if (criticalActive && everEnded_ && (uint32_t)(now - lastEndMs_) < cfg::REMOTE_MSG_PAGE_MS)
      return;
    startNext(now);
  }
}

const char* RemoteMessageDisplay::line(uint8_t i) const {
  const uint16_t idx = static_cast<uint16_t>(page_) * MSG_LINES_PER_PAGE + i;
  if (!active_ || i >= MSG_LINES_PER_PAGE || idx >= totalLines_) return "";
  return lines_[idx];
}

Screen selectScreen(bool booting, Severity overall, bool messageActive, bool codeActive) {
  if (booting) return Screen::Boot;
  if (messageActive) return Screen::Message;  // drawn WITH alert banner if any
  if (overall != Severity::Normal) return Screen::Alert;
  if (codeActive) return Screen::Code;
  return Screen::Main;
}

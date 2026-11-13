#include "lcd_view.h"

#include <stdio.h>
#include <string.h>

#include "rx_config.h"

namespace rx {

namespace {
void pad(char* out, const char* text) {
  size_t n = strlen(text);
  if (n > LCD_COLS) n = LCD_COLS;
  memcpy(out, text, n);
  memset(out + n, ' ', LCD_COLS - n);
  out[LCD_COLS] = '\0';
}
}  // namespace

void formatLine1(const ReceiverState& s, uint32_t now, uint32_t timeoutMs, char* out) {
  char t[24];
  if (!s.anyReceived()) snprintf(t, sizeof t, "AirGuard RX");
  else if (s.lost(now, timeoutMs)) snprintf(t, sizeof t, "SEM SINAL");
  else if (s.overall() == Sev::Normal) snprintf(t, sizeof t, "NORMAL");
  else snprintf(t, sizeof t, "%s: %s", sevName(s.overall()), catNamePt(s.worst()));
  pad(out, t);
}

void formatLine2(const ReceiverState& s, bool linkPage, int rssi, uint32_t now, uint32_t timeoutMs,
                 char* out) {
  char t[32];
  if (!s.anyReceived()) {
    snprintf(t, sizeof t, "Aguardando LoRa");
  } else if (s.lost(now, timeoutMs)) {
    snprintf(t, sizeof t, "ultimo ha %lus", static_cast<unsigned long>((now - s.lastMsgMs()) / 1000));
  } else if (linkPage) {
    snprintf(t, sizeof t, "RSSI %ddBm #%lu", rssi, static_cast<unsigned long>(s.events()));
  } else {
    char a[8], b[8], c[8];
    if (s.hasTemp()) snprintf(a, sizeof a, "%.1fC", s.temperature()); else snprintf(a, sizeof a, "--C");
    if (s.hasHum()) snprintf(b, sizeof b, "%.0f%%", s.humidity()); else snprintf(b, sizeof b, "--%%");
    if (!s.hasAir()) snprintf(c, sizeof c, "--");
    else if (rxcfg::AIR_INDEX_IS_VOLTAGE) snprintf(c, sizeof c, "%.2fV", s.airIndex() / 20.0f);
    else snprintf(c, sizeof c, "i%lu", static_cast<unsigned long>(s.airIndex()));
    snprintf(t, sizeof t, "%s %s %s", a, b, c);
  }
  pad(out, t);
}

bool LcdMessage::show(const char* text, uint32_t now, uint32_t totalMs, uint8_t maxPages, uint8_t times,
                      uint32_t gapMs) {
  uint8_t maxLines = static_cast<uint8_t>(maxPages * 2);
  if (maxLines == 0 || maxLines > MAX_LINES) maxLines = MAX_LINES;
  memset(line_, 0, sizeof line_);
  count_ = 0;
  cut_ = false;
  char cur[LCD_COLS + 1] = {};
  size_t curLen = 0;
  auto flush = [&]() {
    if (count_ < maxLines) memcpy(line_[count_++], cur, curLen + 1);
    else cut_ = true;
    curLen = 0;
    cur[0] = '\0';
  };
  const char* p = text ? text : "";
  while (*p && !cut_) {
    while (*p == ' ') ++p;
    const char* w = p;
    while (*p && *p != ' ') ++p;
    size_t wlen = static_cast<size_t>(p - w);
    while (wlen > 0 && !cut_) {
      const size_t space = curLen ? 1 : 0;
      if (curLen + space + wlen <= LCD_COLS) {
        if (space) cur[curLen++] = ' ';
        memcpy(cur + curLen, w, wlen);
        curLen += wlen;
        cur[curLen] = '\0';
        wlen = 0;
      } else if (curLen == 0) {  // a word longer than a line: hard split
        memcpy(cur, w, LCD_COLS);
        curLen = LCD_COLS;
        cur[curLen] = '\0';
        w += LCD_COLS;
        wlen -= LCD_COLS;
        flush();
      } else {
        flush();
      }
    }
  }
  if (curLen && !cut_) flush();
  if (cut_) {  // mark the last visible line
    char* last = line_[count_ - 1];
    size_t n = strlen(last);
    if (n > LCD_COLS - 3) n = LCD_COLS - 3;
    memcpy(last + n, "...", 4);
  }
  pages_ = static_cast<uint8_t>((count_ + 1) / 2);
  active_ = count_ > 0;
  startMs_ = now;
  totalMs_ = totalMs;
  times_ = times ? times : 1;
  gapMs_ = gapMs;
  return active_;
}

bool LcdMessage::active(uint32_t now) const {
  if (!active_ || totalMs_ == 0) return false;
  const uint32_t e = now - startMs_;
  const uint32_t cycle = totalMs_ + gapMs_;
  return e / cycle < times_ && e % cycle < totalMs_;
}

uint8_t LcdMessage::appearance(uint32_t now) const {
  const uint32_t cycle = totalMs_ + gapMs_;
  const uint32_t i = cycle ? (uint32_t)(now - startMs_) / cycle : 0;
  return static_cast<uint8_t>(i >= times_ ? times_ : i + 1);
}

void LcdMessage::lines(uint32_t now, char* l1, char* l2) const {
  uint8_t page = 0;
  if (pages_ > 1 && totalMs_ > 0) {
    const uint32_t per = totalMs_ / pages_;
    const uint32_t e = (uint32_t)(now - startMs_) % (totalMs_ + gapMs_);  // time into this appearance
    const uint32_t i = per ? e / per : 0;
    page = static_cast<uint8_t>(i >= pages_ ? pages_ - 1 : i);
  }
  const uint8_t a = static_cast<uint8_t>(page * 2);
  pad(l1, a < count_ ? line_[a] : "");
  pad(l2, a + 1 < count_ ? line_[a + 1] : "");
}

}  // namespace rx

#ifdef ARDUINO
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <new>

namespace {
alignas(LiquidCrystal_I2C) uint8_t g_lcdMem[sizeof(LiquidCrystal_I2C)];
LiquidCrystal_I2C* g_lcd = nullptr;

bool probe(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}
}  // namespace

bool LcdView::begin() {
  // PCF8574 backpacks: 0x27 / 0x3F are the usual addresses (A0-A2 open).
  static const uint8_t first[] = {0x27, 0x3F};
  for (uint8_t a : first)
    if (probe(a)) { addr_ = a; break; }
  for (uint8_t a = 0x20; !addr_ && a <= 0x3F; ++a)
    if ((a <= 0x27 || a >= 0x38) && probe(a)) addr_ = a;
  if (!addr_) {
    // List whatever answers, to tell "nothing on the bus" from "unexpected address".
    Serial.print("[LCD] no PCF8574 backpack found. I2C scan:");
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; ++a)
      if (probe(a)) { Serial.printf(" 0x%02X", a); ++found; }
    Serial.print(found ? "\r\n" : " nothing (check SDA->GP10, SCL->GP3, VCC, GND)\r\n");
    return false;
  }
  g_lcd = new (g_lcdMem) LiquidCrystal_I2C(addr_, 16, 2);
  g_lcd->init();
  g_lcd->backlight();
  g_lcd->clear();
  memset(shown_, 0, sizeof shown_);
  ok_ = true;
  Serial.printf("[LCD] 16x2 found at 0x%02X\r\n", addr_);
  return true;
}

bool LcdView::stillThere() {
  if (!ok_) return false;
  if (probe(addr_)) return true;
  ok_ = false;
  Serial.printf("[LCD] backpack 0x%02X stopped answering (loose SDA/SCL/VCC?)\r\n", addr_);
  return false;
}

void LcdView::show(const char* l1, const char* l2) {
  if (!ok_) return;
  const char* lines[2] = {l1, l2};
  for (uint8_t r = 0; r < 2; ++r) {
    if (!strcmp(shown_[r], lines[r])) continue;
    g_lcd->setCursor(0, r);
    g_lcd->print(lines[r]);
    snprintf(shown_[r], sizeof shown_[r], "%s", lines[r]);
  }
}
#endif

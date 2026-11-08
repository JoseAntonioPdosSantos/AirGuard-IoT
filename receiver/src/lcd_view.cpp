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
    Serial.print(found ? "\r\n" : " nothing (check SDA->GP4, SCL->GP5, VCC, GND)\r\n");
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

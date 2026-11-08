#include "oled_manager.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

#include "../config/config.h"
#include "../utils/logger.h"

static Adafruit_SSD1306 g_oled(cfg::OLED_WIDTH, cfg::OLED_HEIGHT, &Wire, -1,
                               cfg::I2C_CLOCK_HZ, cfg::I2C_CLOCK_HZ);

namespace {

constexpr uint32_t RETRY_MS = 30000;
constexpr uint32_t CHECK_MS = 5000;

void textAt(int16_t x, int16_t y, const char* s, uint8_t size = 1) {
  g_oled.setTextSize(size);
  g_oled.setCursor(x, y);
  g_oled.print(s);
}

void centered(int16_t y, const char* s, uint8_t size = 1) {
  const int16_t w = static_cast<int16_t>(strlen(s) * 6 * size);
  textAt(w >= 128 ? 0 : (128 - w) / 2, y, s, size);
}

const char* shortCat(Category c) {
  switch (c) {
    case Category::AirQuality: return "AIR";
    case Category::Temperature: return "TMP";
    case Category::Humidity: return "HUM";
    default: return "SYS";
  }
}

const char* shortSev(Severity s) {
  switch (s) {
    case Severity::Low: return "LOW";
    case Severity::Medium: return "MED";
    case Severity::High: return "HIGH";
    default: return "OK";
  }
}

const char* linkText(LinkState l) {
  switch (l) {
    case LinkState::NoRadio: return "NO RADIO";
    case LinkState::Sending: return "SENDING";
    case LinkState::WaitAck: return "WAIT ACK";
    case LinkState::Delivered: return "ACK OK";
    case LinkState::Failed: return "NO ACK";
    default: return "-";
  }
}

// First (most important) problem described by the SYSTEM flags.
const char* systemText(uint16_t f) {
  if (f & SYS_ENV_SENSOR_FAULT) return "TEMP/HUM SENSOR FAIL";
  if (f & SYS_AIR_SENSOR_FAULT) return "MQ-135 READ FAIL";
  if (f & SYS_MQ135_UNCAL) return "MQ-135 UNCALIBRATED";
  if (f & SYS_LORA_RADIO_FAULT) return "LORA RADIO FAULT";
  if (f & SYS_LORA_NO_ACK) return "LORA: NO ACK";
  if (f & SYS_DISPLAY_FAULT) return "DISPLAY FAULT";
  return "SYSTEM OK";
}

bool blinkPhase(uint32_t now) { return (now / cfg::DISPLAY_BLINK_MS) % 2 == 0; }

}  // namespace

bool OledManager::begin() {
  // Adafruit_SSD1306::begin() does not check that the display ACKs (it only
  // fails on allocation), so probe the address first to avoid a false "ok".
  Wire.beginTransmission(cfg::OLED_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    ok_ = false;
    LOGE("OLED", "SSD1306 not found at 0x%02X (check wiring/address)", cfg::OLED_I2C_ADDR);
    return false;
  }
  // periphBegin=false: Wire was configured (pins, clock) by the application.
  ok_ = g_oled.begin(SSD1306_SWITCHCAPVCC, cfg::OLED_I2C_ADDR, true, false);
  if (!ok_) {
    LOGE("OLED", "SSD1306 not found at 0x%02X (check wiring/address)", cfg::OLED_I2C_ADDR);
    return false;
  }
  g_oled.setTextColor(SSD1306_WHITE);
  g_oled.setTextWrap(false);
  g_oled.cp437(false);
  shadowValid_ = false;
  inverted_ = false;
  LOGI("OLED", "SSD1306 ok at 0x%02X", cfg::OLED_I2C_ADDR);
  return true;
}

void OledManager::retry(uint32_t now) {
  if (ok_ || (uint32_t)(now - lastRetryMs_) < RETRY_MS) return;
  lastRetryMs_ = now;
  begin();
}

void OledManager::checkBus(uint32_t now) {
  if (!ok_ || (uint32_t)(now - lastCheckMs_) < CHECK_MS) return;
  lastCheckMs_ = now;
  Wire.beginTransmission(cfg::OLED_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    ok_ = false;
    lastRetryMs_ = now;
    LOGE("OLED", "display stopped answering on I2C");
  }
}

void OledManager::flush() {
  const uint8_t* buf = g_oled.getBuffer();
  if (!buf) return;
  if (shadowValid_ && memcmp(buf, shadow_, sizeof shadow_) == 0) return;
  g_oled.display();
  memcpy(shadow_, buf, sizeof shadow_);
  shadowValid_ = true;
}

void OledManager::render(const DisplayModel& m) {
  if (!ok_) return;
  g_oled.clearDisplay();
  wantInvert_ = false;
  switch (m.screen) {
    case Screen::Boot: drawBoot(m); break;
    case Screen::Main: drawMain(m); break;
    case Screen::Alert: drawAlert(m); break;
    case Screen::Message: drawMessage(m); break;
  }
  flush();
  // Inversion is a controller command (not in the frame buffer): send on change only.
  if (wantInvert_ != inverted_) {
    g_oled.invertDisplay(wantInvert_);
    inverted_ = wantInvert_;
  }
}

// ---------------------------------------------------------------------------
// Layout for the two-colour 0.96" module: rows 0..15 are YELLOW, 16..63 BLUE
// (fixed by the panel, with a small physical gap between them). Titles and
// highlights live in the yellow band; data starts at y = BLUE_Y. The same
// layout works on single-colour panels.
constexpr int16_t BLUE_Y = 16;

void OledManager::drawBoot(const DisplayModel&) {
  centered(4, "AirGuard IoT");                       // yellow
  centered(BLUE_Y + 4, "SMART ENVIRONMENTAL");       // blue
  centered(BLUE_Y + 13, "MONITORING");
  centered(BLUE_Y + 28, "Intelligent monitoring.");
  centered(BLUE_Y + 37, "Safer environments.");
}

void OledManager::drawFooter(const DisplayModel& m) {
  char b[24];
  if (m.loraInit) snprintf(b, sizeof b, "LoRa: INIT");
  else if (!m.loraOk) snprintf(b, sizeof b, "LoRa: FAULT");
  else if (m.txPending) snprintf(b, sizeof b, "LoRa: OK  queue %u", m.txPending);
  else snprintf(b, sizeof b, "LoRa: OK");
  textAt(0, 56, b);
}

void OledManager::drawMain(const DisplayModel& m) {
  char b[32];
  // Yellow band: title + system status.
  textAt(0, 0, "KITCHEN MONITOR");
  if (m.simulation) textAt(110, 0, "SIM");
  const char* status = "MONITORING";
  if (m.envFault || m.airFault) status = "SENSOR FAULT";
  else if (m.airWarmingUp) status = "WARMING UP";
  if (m.calibrating) snprintf(b, sizeof b, "Status: CAL %u%%", m.calPercent);
  else snprintf(b, sizeof b, "Status: %s", status);
  textAt(0, 8, b);

  // Blue band: measurements.
  if (m.envValid) snprintf(b, sizeof b, "Temp: %.1f C", m.temperature);
  else snprintf(b, sizeof b, "Temp: --.- C");
  textAt(0, BLUE_Y + 1, b);
  if (m.envValid) snprintf(b, sizeof b, "Humidity: %.0f%%", m.humidity);
  else snprintf(b, sizeof b, "Humidity: --%%");
  textAt(0, BLUE_Y + 10, b);

  if (m.airFault) snprintf(b, sizeof b, "Air Quality: FAIL");
  else if (m.airWarmingUp)
    snprintf(b, sizeof b, "Air: WARMUP %lum%02lus", (unsigned long)(m.warmupLeftS / 60),
             (unsigned long)(m.warmupLeftS % 60));
  else if (!m.airCalibrated) snprintf(b, sizeof b, "Air Quality: NO CAL");
  else snprintf(b, sizeof b, "Air Quality: %s", severityName(m.airSeverity));
  textAt(0, BLUE_Y + 19, b);

  if (m.airUsable) {
    snprintf(b, sizeof b, "Air index: %.0f", m.airIndex);
    textAt(0, BLUE_Y + 28, b);
  }
  drawFooter(m);
}

void OledManager::drawAlert(const DisplayModel& m) {
  char b[28];
  const bool phase = blinkPhase(m.nowMs);

  // Yellow band: severity in the 2x font (exactly 16 px tall).
  // MEDIUM blinks the text, HIGH blinks the whole screen (inversion).
  if (m.overall != Severity::Medium || phase) centered(0, severityName(m.overall), 2);

  // Blue band: what, values, other categories, outputs.
  snprintf(b, sizeof b, "ALERT %s", categoryName(m.worst));
  textAt(0, BLUE_Y + 1, b);

  switch (m.worst) {
    case Category::AirQuality:
      snprintf(b, sizeof b, "Air index: %.0f (rel.)", m.airIndex);
      textAt(0, BLUE_Y + 11, b);
      break;
    case Category::Temperature:
      if (m.envValid) {
        snprintf(b, sizeof b, "Temp: %.1f C", m.temperature);
        textAt(0, BLUE_Y + 11, b);
        snprintf(b, sizeof b, "Feels: %.1f C", m.heatIndex);
        textAt(0, BLUE_Y + 20, b);
      }
      break;
    case Category::Humidity:
      if (m.envValid) {
        snprintf(b, sizeof b, "Humidity: %.0f%%", m.humidity);
        textAt(0, BLUE_Y + 11, b);
        snprintf(b, sizeof b, "Temp: %.1f C", m.temperature);
        textAt(0, BLUE_Y + 20, b);
      }
      break;
    default:
      textAt(0, BLUE_Y + 11, systemText(m.systemFlags));
      break;
  }

  // Other active categories (compact).
  char also[28] = "";
  size_t n = 0;
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) {
    const Category c = static_cast<Category>(i);
    if (c == m.worst || m.perCategory[i] == Severity::Normal) continue;
    n += snprintf(also + n, sizeof also - n, "%s%s:%s", n ? " " : "+", shortCat(c),
                  shortSev(m.perCategory[i]));
    if (n >= sizeof also) break;
  }
  if (also[0]) textAt(0, BLUE_Y + 30, also);

  // 21 columns max (128 px / 6 px): "Rly:OFF LoRa:WAIT ACK" is exactly 21.
  snprintf(b, sizeof b, "Rly:%s LoRa:%s", m.relayOn ? "ON " : "OFF", linkText(m.alertLink));
  textAt(0, 56, b);

  wantInvert_ = (m.overall == Severity::High && !phase);
}

void OledManager::drawMessage(const DisplayModel& m) {
  const RemoteMessageDisplay* d = m.msg;
  if (!d) return;
  char b[32];
  // Yellow band, row 0: inverted header.
  g_oled.fillRect(0, 0, 128, 8, SSD1306_WHITE);
  g_oled.setTextColor(SSD1306_BLACK);
  // 21 columns: "MSG " + sender (12) + " 1/2" -> sender cut to 12 chars so the
  // page indicator always stays visible.
  char sender[13];
  strncpy(sender, d->sender(), sizeof sender - 1);
  sender[sizeof sender - 1] = '\0';
  if (d->pages() > 1) snprintf(b, sizeof b, "MSG %s %u/%u", sender, d->page() + 1, d->pages());
  else snprintf(b, sizeof b, "MSG %s", sender);
  textAt(1, 0, b);
  g_oled.setTextColor(SSD1306_WHITE);

  // Yellow band, row 1: an active alert is never hidden -> blinking banner.
  if (m.overall != Severity::Normal && blinkPhase(m.nowMs)) {
    g_oled.fillRect(0, 8, 128, 8, SSD1306_WHITE);
    g_oled.setTextColor(SSD1306_BLACK);
    snprintf(b, sizeof b, "! %s %s !", shortSev(m.overall), shortCat(m.worst));
    centered(8, b);
    g_oled.setTextColor(SSD1306_WHITE);
  }

  // Blue band: 6 text lines (y = 16..63).
  for (uint8_t i = 0; i < MSG_LINES_PER_PAGE; ++i)
    textAt(0, BLUE_Y + 8 * i, d->line(i));
}

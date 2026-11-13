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
  const bool due = (uint32_t)(nowMs_ - lastFullMs_) >= cfg::DISPLAY_FULL_REFRESH_MS;
  if (shadowValid_ && !due && memcmp(buf, shadow_, sizeof shadow_) == 0) return;
  g_oled.display();  // also re-sends the address window: a corrupted pointer is reset
  lastFullMs_ = nowMs_;
  memcpy(shadow_, buf, sizeof shadow_);
  shadowValid_ = true;
}

void OledManager::render(const DisplayModel& m) {
  if (!ok_) return;
  nowMs_ = m.nowMs;
  g_oled.clearDisplay();
  wantInvert_ = false;
  if (testActive_ && (int32_t)(m.nowMs - testUntilMs_) < 0) {
    g_oled.fillRect(0, 0, 128, 64, SSD1306_WHITE);
    flush();
    if (inverted_) { g_oled.invertDisplay(false); inverted_ = false; }
    return;
  }
  testActive_ = false;
  switch (m.screen) {
    case Screen::Boot: drawBoot(m); break;
    case Screen::Main: drawMain(m); break;
    case Screen::Alert: drawAlert(m); break;
    case Screen::Message: drawMessage(m); break;
    case Screen::Code: drawCode(m); break;
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

// Yellow band, shared by the main and alert screens: the app name in normal
// operation, the alert level (as before) while an alert is active.
void OledManager::drawHeader(const DisplayModel& m) {
  if (m.overall == Severity::Normal) {
    // A WhatsApp message takes the band while it scrolls (2x font = 16 px);
    // the position comes from display/wa_ticker (slower than the ticker below).
    if (m.waText && m.waText[0]) {
      textAt(m.waX, 0, m.waText, 2);
      return;
    }
    textAt(5, 0, "AirGuard", 2);                        // 8 x 12 px = 96 px
    textAt(105, 8, m.simulation ? "SIM" : "IoT");       // bottom-aligned, small
    return;
  }
  // Severity in the 2x font (exactly 16 px tall). MEDIUM blinks the text,
  // HIGH blinks the whole screen (inversion, set in drawAlert).
  if (m.overall != Severity::Medium || blinkPhase(m.nowMs)) centered(0, severityName(m.overall), 2);
}

// Blue band, rows 20..35: measurements scrolling right-to-left in the 2x font
// (easier to read than the 6x8 font; each pixel is 2x2, so one weak panel row
// does not deform the letters). Position is derived from the clock, so the
// speed is constant whatever the loop timing.
void OledManager::drawTicker(const DisplayModel& m, const char* prefix) {
  char t[128];
  size_t n = 0;
  if (prefix && prefix[0]) n += snprintf(t + n, sizeof t - n, "%s  |  ", prefix);
  if (m.envValid) n += snprintf(t + n, sizeof t - n, "Temp %.1f C  |  Hum %.0f %%", m.temperature, m.humidity);
  else n += snprintf(t + n, sizeof t - n, "Temp --.- C  |  Hum -- %%");
  if (n < sizeof t) {
    if (m.airFault) snprintf(t + n, sizeof t - n, "  |  Gas FAIL");
    else if (!m.airAoValid) snprintf(t + n, sizeof t - n, "  |  Gas --- V");
    else snprintf(t + n, sizeof t - n, "  |  Gas %.2f V", m.airAoVolts);
  }
  const int32_t w = static_cast<int32_t>(strlen(t)) * 12;  // 2x font: 12 px per char
  const int32_t span = w + 128;
  const int32_t shift = static_cast<int32_t>((static_cast<uint64_t>(m.nowMs) *
                                              cfg::DISPLAY_TICKER_PX_PER_S / 1000) % span);
  textAt(static_cast<int16_t>(128 - shift), BLUE_Y + 4, t, 2);
}

void OledManager::drawMain(const DisplayModel& m) {
  char b[32];
  drawHeader(m);

  const char* status = nullptr;
  if (m.envFault || m.airFault) status = "SENSOR FAULT";
  else if (m.airWarmingUp) status = "WARMING UP";
  drawTicker(m, status);

  if (!m.airAoValid) snprintf(b, sizeof b, "MQ135 AO: --- V");
  else snprintf(b, sizeof b, "MQ135 AO: %.2f V", m.airAoVolts);
  textAt(0, BLUE_Y + 28, b);
  drawFooter(m);
}

void OledManager::drawAlert(const DisplayModel& m) {
  char b[40];
  const bool phase = blinkPhase(m.nowMs);
  drawHeader(m);

  // Scrolling line starts with what the alert is about.
  if (m.worst == Category::System) snprintf(b, sizeof b, "ALERT %s", systemText(m.systemFlags));
  else snprintf(b, sizeof b, "ALERT %s", categoryName(m.worst));
  drawTicker(m, b);

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

// WhatsApp pairing: the 8-character code to type on the phone (WhatsApp >
// Dispositivos conectados > Conectar dispositivo > "Conectar com numero de
// telefone"), in the 2x font and written XXXX-XXXX, the way WhatsApp shows it.
// Nothing else shares the screen but its title and one caption.
void OledManager::drawCode(const DisplayModel& m) {
  if (!m.waCode || strlen(m.waCode) != 8) return;
  centered(0, "WhatsApp", 2);                    // yellow band
  centered(BLUE_Y + 6, "Codigo de conexao");     // blue
  char b[12];
  snprintf(b, sizeof b, "%.4s-%s", m.waCode, m.waCode + 4);
  centered(BLUE_Y + 22, b, 2);                   // 9 characters x 12 px = 108 px
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

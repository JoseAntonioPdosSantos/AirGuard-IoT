// AirGuard IoT - SSD1306 128x64 renderer.
//
// The application fills a DisplayModel; OledManager decides nothing about
// priorities (screen_logic does) and only draws. A frame is pushed over I2C
// only when the frame buffer actually changed (~23 ms at 400 kHz), so the
// main loop is not stalled by redundant refreshes.
#pragma once
#include <stdint.h>

#include "../monitoring/alert_types.h"
#include "screen_logic.h"

enum class LinkState : uint8_t { NoRadio, Idle, Sending, WaitAck, Delivered, Failed };

struct DisplayModel {
  Screen   screen = Screen::Boot;
  uint32_t nowMs = 0;
  // main screen
  bool     envValid = false;
  float    temperature = 0, humidity = 0;
  bool     airUsable = false;
  bool     airWarmingUp = true;
  uint32_t warmupLeftS = 0;
  bool     airCalibrated = false;
  bool     airFault = false;
  bool     envFault = false;
  bool     calibrating = false;
  uint8_t  calPercent = 0;
  bool     simulation = false;
  float    airIndex = 0;
  float    airAoVolts = 0;   // MQ-135 AO voltage (before the divider), 0..5 V
  bool     airAoValid = false;
  Severity airSeverity = Severity::Normal;
  // alert screen
  Severity overall = Severity::Normal;
  Category worst = Category::System;
  Severity perCategory[CATEGORY_COUNT] = {};
  uint16_t systemFlags = 0;
  float    heatIndex = 0;
  bool     relayOn = false;
  LinkState alertLink = LinkState::Idle;
  // footer
  bool     loraOk = false;
  bool     loraInit = false;
  uint8_t  txPending = 0;
  // message screen
  const RemoteMessageDisplay* msg = nullptr;
};

class OledManager {
 public:
  bool begin();                         // Wire must already be configured
  void render(const DisplayModel& m);   // draws + flushes only on change
  bool ok() const { return ok_; }
  void retry(uint32_t nowMs);           // re-probe after a fault (every 30 s)
  void checkBus(uint32_t nowMs);        // periodic I2C presence check
  // Bench test: every pixel ON for `ms` (dead rows/columns show as dark lines).
  void startPixelTest(uint32_t nowMs, uint32_t ms) { testUntilMs_ = nowMs + ms; testActive_ = true; }

 private:
  void drawBoot(const DisplayModel& m);
  void drawMain(const DisplayModel& m);
  void drawAlert(const DisplayModel& m);
  void drawMessage(const DisplayModel& m);
  void drawFooter(const DisplayModel& m);
  void drawHeader(const DisplayModel& m);
  void drawTicker(const DisplayModel& m, const char* prefix);
  void flush();

  bool     ok_ = false;
  uint32_t lastRetryMs_ = 0;
  uint32_t lastCheckMs_ = 0;
  uint8_t  shadow_[128 * 64 / 8] = {};
  bool     shadowValid_ = false;
  uint32_t lastFullMs_ = 0;
  uint32_t nowMs_ = 0;
  bool     wantInvert_ = false, inverted_ = false;
  bool     testActive_ = false;
  uint32_t testUntilMs_ = 0;
};

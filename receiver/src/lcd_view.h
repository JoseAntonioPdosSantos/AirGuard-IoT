// AirGuard Receiver - 16x2 LCD text (pure formatting, unit tested) + driver.
//
//   line 1: "AirGuard RX"            before the first message
//           "SEM SINAL"              no frame (heartbeat) for LINK_TIMEOUT_MS
//           "NORMAL"                 everything normal
//           "HIGH: GAS"              worst category and its level
//   line 2: "25.5C 61% 0.25V"        temperature, humidity, gas (AO volts)
//           "RSSI -45dBm  #12"       alternates every LCD_PAGE_MS (link info)
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

}  // namespace rx

#ifdef ARDUINO
class LcdView {
 public:
  bool begin();                         // Wire must be configured; finds the backpack
  void show(const char* l1, const char* l2);  // writes only lines that changed
  bool ok() const { return ok_; }
  uint8_t address() const { return addr_; }

 private:
  bool    ok_ = false;
  uint8_t addr_ = 0;
  char    shown_[2][17] = {};
};
#endif

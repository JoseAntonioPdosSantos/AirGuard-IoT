// AirGuard Receiver - REYAX RYLR998 driver (UART AT commands).
//
// Init (reset + configuration) is blocking, ~1 s when the module answers,
// and feeds the watchdog while it waits. Reception and transmission are
// non-blocking: loop() collects lines; send() writes one AT+SEND and the
// link stays busy until "+OK" and the frame's airtime have passed.
#pragma once
#include <stddef.h>
#include <stdint.h>

class RadioLink {
 public:
  void begin();             // UART + pins, then init()
  bool init();              // reset + full AT configuration; false on any failure
  void loop(uint32_t nowMs);

  bool ok() const { return ok_; }
  bool busy(uint32_t nowMs) const;
  // Sends `data` to LoRa address `addr`. False if busy, not ok or invalid.
  bool send(uint16_t addr, const char* data, size_t len, uint32_t nowMs);

  // One received frame (copied). Returns false if none is waiting.
  bool take(char* buf, size_t cap, size_t& len, uint16_t& fromAddr, int& rssi, int& snr);

 private:
  bool command(const char* cmd);   // sends cmd, waits for +OK (blocking, bounded)
  void handleLine(uint32_t now);

  bool     ok_ = false;
  uint32_t faultMs_ = 0;
  char     line_[300] = {};
  size_t   lineLen_ = 0;
  uint32_t lineStartMs_ = 0;

  bool     txWait_ = false;      // waiting for +OK of AT+SEND
  uint32_t txStartMs_ = 0, txFreeAtMs_ = 0, txAirtimeMs_ = 0;

  bool     rxReady_ = false;
  char     rxBuf_[248] = {};
  size_t   rxLen_ = 0;
  uint16_t rxAddr_ = 0;
  int      rxRssi_ = 0, rxSnr_ = 0;
};

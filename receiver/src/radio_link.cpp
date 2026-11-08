#include "radio_link.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "../../src/communication/rylr998_codec.h"
#include "rx_config.h"

namespace {
constexpr uint32_t TX_OK_TIMEOUT_MS = 5000;
constexpr uint32_t LINE_TIMEOUT_MS = 500;

void feedWatchdog() { rp2040.wdt_reset(); }

// Waits up to `ms` for a line starting with `prefix`; returns true if seen.
bool waitFor(const char* prefix, uint32_t ms, char* last, size_t lastCap) {
  char buf[96];
  size_t n = 0;
  const uint32_t t0 = millis();
  while ((uint32_t)(millis() - t0) < ms) {
    feedWatchdog();
    while (Serial1.available()) {
      const int c = Serial1.read();
      if (c == '\n') {
        buf[n] = '\0';
        if (n && buf[n - 1] == '\r') buf[--n] = '\0';
        if (last && lastCap) snprintf(last, lastCap, "%s", buf);
        if (!strncmp(buf, prefix, strlen(prefix))) return true;
        if (!strncmp(buf, "+ERR", 4)) return false;
        n = 0;
      } else if (n + 1 < sizeof buf) {
        buf[n++] = static_cast<char>(c);
      }
    }
  }
  return false;
}
}  // namespace

void RadioLink::begin() {
  Serial1.setTX(rxcfg::LORA_UART_TX);
  Serial1.setRX(rxcfg::LORA_UART_RX);
  Serial1.setFIFOSize(512);
  Serial1.begin(rxcfg::LORA_UART_BAUD);
  pinMode(rxcfg::LORA_NRST, OUTPUT);
  digitalWrite(rxcfg::LORA_NRST, HIGH);
  init();
}

bool RadioLink::command(const char* cmd) {
  while (Serial1.available()) Serial1.read();
  Serial1.print(cmd);
  Serial1.print("\r\n");
  char last[96] = "";
  const bool good = waitFor("+OK", rxcfg::LORA_CMD_TIMEOUT_MS, last, sizeof last);
  if (!good) Serial.printf("[LORA] '%s' failed (%s)\r\n", cmd, last[0] ? last : "no answer");
  return good;
}

bool RadioLink::init() {
  ok_ = false;
  txWait_ = false;
  lineLen_ = 0;
  rxReady_ = false;
  digitalWrite(rxcfg::LORA_NRST, LOW);
  delay(100);
  digitalWrite(rxcfg::LORA_NRST, HIGH);
  if (!waitFor("+READY", rxcfg::LORA_READY_TIMEOUT_MS, nullptr, 0))
    Serial.print("[LORA] no +READY after reset, trying AT anyway\r\n");

  char c[64];
  bool good = command("AT") && command("AT+MODE=0");
  snprintf(c, sizeof c, "AT+ADDRESS=%u", rxcfg::LORA_ADDRESS);
  good = good && command(c);
  snprintf(c, sizeof c, "AT+NETWORKID=%u", rxcfg::LORA_NETWORK_ID);
  good = good && command(c);
  snprintf(c, sizeof c, "AT+BAND=%lu", static_cast<unsigned long>(rxcfg::LORA_BAND_HZ));
  good = good && command(c);
  snprintf(c, sizeof c, "AT+PARAMETER=%u,%u,%u,%u", rxcfg::LORA_SF, rxcfg::LORA_BW_CODE,
           rxcfg::LORA_CR_CODE, rxcfg::LORA_PREAMBLE);
  good = good && command(c);
  snprintf(c, sizeof c, "AT+CRFOP=%d", rxcfg::LORA_TX_POWER_DBM);
  good = good && command(c);
  if (good && rxcfg::LORA_CPIN[0]) {
    snprintf(c, sizeof c, "AT+CPIN=%s", rxcfg::LORA_CPIN);
    good = command(c);
  }
  ok_ = good;
  faultMs_ = millis();
  if (good)
    Serial.printf("[LORA] RYLR998 ok: %lu Hz SF%u BW%u CR%u pre %u net %u addr %u\r\n",
                  static_cast<unsigned long>(rxcfg::LORA_BAND_HZ), rxcfg::LORA_SF,
                  rxcfg::LORA_BW_CODE, rxcfg::LORA_CR_CODE, rxcfg::LORA_PREAMBLE,
                  rxcfg::LORA_NETWORK_ID, rxcfg::LORA_ADDRESS);
  else
    Serial.print("[LORA] RYLR998 init FAILED (check wiring); retry in 30 s\r\n");
  return good;
}

bool RadioLink::busy(uint32_t now) const {
  return txWait_ || (int32_t)(now - txFreeAtMs_) < 0;
}

bool RadioLink::send(uint16_t addr, const char* data, size_t len, uint32_t now) {
  if (!ok_ || busy(now)) return false;
  char out[rylr::MAX_PAYLOAD + 32];
  const size_t n = rylr::buildSend(out, sizeof out, addr, reinterpret_cast<const uint8_t*>(data), len);
  if (!n) return false;
  Serial1.write(reinterpret_cast<const uint8_t*>(out), n);
  txWait_ = true;
  txStartMs_ = now;
  txAirtimeMs_ = rylr::airtimeMs(rxcfg::LORA_SF, rxcfg::LORA_BW_CODE, rxcfg::LORA_CR_CODE,
                                 rxcfg::LORA_PREAMBLE, len);
  return true;
}

bool RadioLink::take(char* buf, size_t cap, size_t& len, uint16_t& from, int& rssi, int& snr) {
  if (!rxReady_ || cap == 0) return false;
  len = rxLen_ < cap - 1 ? rxLen_ : cap - 1;
  memcpy(buf, rxBuf_, len);
  buf[len] = '\0';
  from = rxAddr_;
  rssi = rxRssi_;
  snr = rxSnr_;
  rxReady_ = false;
  return true;
}

void RadioLink::handleLine(uint32_t now) {
  size_t n = lineLen_;
  if (n && line_[n - 1] == '\r') --n;
  if (n >= 5 && !memcmp(line_, "+RCV=", 5)) {
    rylr::RcvFrame f;
    const rylr::RcvResult r = rylr::parseRcv(line_, n, f);
    if (r == rylr::RcvResult::Incomplete) {  // '\n' inside the data: keep reading
      if (lineLen_ + 1 < sizeof line_) line_[lineLen_++] = '\n';
      return;
    }
    if (r == rylr::RcvResult::Ok && f.length <= sizeof rxBuf_) {
      memcpy(rxBuf_, f.data, f.length);
      rxLen_ = f.length;
      rxAddr_ = f.address;
      rxRssi_ = f.rssi;
      rxSnr_ = f.snr;
      rxReady_ = true;  // a newer frame overwrites an unread one (logged by the app)
    } else {
      Serial.print("[LORA] bad +RCV line dropped\r\n");
    }
  } else if (n >= 3 && !memcmp(line_, "+OK", 3)) {
    if (txWait_) {
      txWait_ = false;
      txFreeAtMs_ = txStartMs_ + txAirtimeMs_ + 50;
      if ((int32_t)(now - txFreeAtMs_) > 0) txFreeAtMs_ = now;
    }
  } else if (n >= 4 && !memcmp(line_, "+ERR", 4)) {
    Serial.printf("[LORA] %.*s\r\n", static_cast<int>(n), line_);
    if (txWait_) { txWait_ = false; txFreeAtMs_ = now + 300; }
  } else if (n >= 6 && !memcmp(line_, "+READY", 6)) {
    Serial.print("[LORA] module reset by itself: reconfiguring\r\n");
    ok_ = false;
    faultMs_ = now - rxcfg::LORA_REINIT_PERIOD_MS;  // re-init right away
  }
  lineLen_ = 0;
}

void RadioLink::loop(uint32_t now) {
  if (!ok_ && (uint32_t)(now - faultMs_) >= rxcfg::LORA_REINIT_PERIOD_MS) init();

  while (Serial1.available()) {
    const int c = Serial1.read();
    if (lineLen_ == 0) lineStartMs_ = now;
    if (c == '\n') { handleLine(now); continue; }
    if (lineLen_ + 1 < sizeof line_) line_[lineLen_++] = static_cast<char>(c);
    else lineLen_ = 0;  // overlong garbage
  }
  if (lineLen_ && (uint32_t)(now - lineStartMs_) > LINE_TIMEOUT_MS) lineLen_ = 0;

  if (txWait_ && (uint32_t)(now - txStartMs_) > TX_OK_TIMEOUT_MS) {
    Serial.print("[LORA] AT+SEND not answered: radio fault, re-init in 30 s\r\n");
    txWait_ = false;
    ok_ = false;
    faultMs_ = now;
  }
}

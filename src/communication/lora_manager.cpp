#include "lora_manager.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "../config/config.h"
#include "../config/hardware_pins.h"
#include "../utils/logger.h"
#include "rylr998_codec.h"

namespace {

constexpr uint32_t NRST_LOW_MS = 100;
constexpr uint32_t LINE_TIMEOUT_MS = 500;      // partial line without '\n' -> dropped
constexpr uint16_t UART_FIFO = 512;            // software RX buffer (bytes)
constexpr int ERR_RX_CRC = 12, ERR_RX_HEADER = 19;  // unsolicited RX errors

bool startsWith(const char* s, size_t len, const char* prefix) {
  const size_t n = strlen(prefix);
  return len >= n && memcmp(s, prefix, n) == 0;
}

}  // namespace

bool LoraManager::begin() {
  Serial1.setTX(pins::LORA_UART_TX);
  Serial1.setRX(pins::LORA_UART_RX);
  Serial1.setFIFOSize(UART_FIFO);
  Serial1.begin(cfg::LORA_UART_BAUD);
  pinMode(pins::LORA_NRST, OUTPUT);
  startInit(millis());
  return true;
}

void LoraManager::startInit(uint32_t now) {
  state_ = State::Init;
  phase_ = Phase::ResetLow;
  phaseMs_ = now;
  cfgStep_ = 0;
  cmdPending_ = healthPending_ = false;
  lineLen_ = 0;
  digitalWrite(pins::LORA_NRST, LOW);  // hardware reset: config restarts from scratch
}

bool LoraManager::nextConfigCommand(char* out, size_t cap) {
  while (true) {
    switch (cfgStep_) {
      case 0: snprintf(out, cap, "AT"); return true;
      case 1: snprintf(out, cap, "AT+MODE=0"); return true;  // transceiver mode
      case 2: snprintf(out, cap, "AT+ADDRESS=%u", cfg::LORA_ADDRESS); return true;
      case 3: snprintf(out, cap, "AT+NETWORKID=%u", cfg::LORA_NETWORK_ID); return true;
      case 4: snprintf(out, cap, "AT+BAND=%lu", static_cast<unsigned long>(cfg::LORA_BAND_HZ)); return true;
      case 5:
        snprintf(out, cap, "AT+PARAMETER=%u,%u,%u,%u", cfg::LORA_SF, cfg::LORA_BW_CODE,
                 cfg::LORA_CR_CODE, cfg::LORA_PREAMBLE);
        return true;
      case 6: snprintf(out, cap, "AT+CRFOP=%d", cfg::LORA_TX_POWER_DBM); return true;
      case 7:
        if (cfg::LORA_CPIN[0] == '\0') { ++cfgStep_; continue; }
        snprintf(out, cap, "AT+CPIN=%s", cfg::LORA_CPIN);
        return true;
      default: return false;
    }
  }
}

void LoraManager::sendCommand(const char* cmd, uint32_t now) {
  Serial1.write(reinterpret_cast<const uint8_t*>(cmd), strlen(cmd));
  Serial1.write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
  cmdPending_ = true;
  cmdSentMs_ = now;
  LOGD("LORA", "> %s", cmd);
}

void LoraManager::fault(const char* why, int code, uint32_t now) {
  if (state_ == State::Tx) {  // report the interrupted TX as failed
    txDone_ = true;
    txOk_ = false;
  }
  state_ = State::Fault;
  faultMs_ = now;
  cmdPending_ = healthPending_ = false;
  LOGE("LORA", "RYLR998 fault (%s, code %d); monitoring continues, retry in %lu s", why, code,
       static_cast<unsigned long>(cfg::LORA_REINIT_PERIOD_MS / 1000));
}

// ---------------------------------------------------------------------------
void LoraManager::loop(uint32_t now) {
  if (state_ == State::Uninit) return;
  pumpUart(now);

  switch (state_) {
    case State::Uninit:
      break;

    case State::Init:
      if (phase_ == Phase::ResetLow) {
        if ((uint32_t)(now - phaseMs_) >= NRST_LOW_MS) {
          digitalWrite(pins::LORA_NRST, HIGH);
          phase_ = Phase::WaitReady;
          phaseMs_ = now;
        }
      } else if (phase_ == Phase::WaitReady) {
        if ((uint32_t)(now - phaseMs_) >= cfg::LORA_READY_TIMEOUT_MS) {
          LOGW("LORA", "no +READY after reset, trying AT anyway");
          phase_ = Phase::Config;
        }
      } else if (!cmdPending_) {
        char cmd[64];
        if (nextConfigCommand(cmd, sizeof cmd)) {
          sendCommand(cmd, now);
        } else {
          state_ = State::Rx;
          lastActivityMs_ = now;
          LOGI("LORA", "RYLR998 ok: %lu Hz SF%u BW%u CR%u pre %u net %u addr %u -> %u, %d dBm%s",
               static_cast<unsigned long>(cfg::LORA_BAND_HZ), cfg::LORA_SF, cfg::LORA_BW_CODE,
               cfg::LORA_CR_CODE, cfg::LORA_PREAMBLE, cfg::LORA_NETWORK_ID, cfg::LORA_ADDRESS,
               cfg::LORA_DEST_ADDRESS, cfg::LORA_TX_POWER_DBM, cfg::LORA_CPIN[0] ? ", CPIN set" : "");
        }
      } else if ((uint32_t)(now - cmdSentMs_) >= cfg::LORA_CMD_TIMEOUT_MS) {
        fault("no answer during configuration", cfgStep_, now);
      }
      break;

    case State::Rx:
      if (cmdPending_) {
        if ((uint32_t)(now - cmdSentMs_) >= cfg::LORA_CMD_TIMEOUT_MS)
          fault("no answer to AT (module lost?)", 0, now);
      } else if ((uint32_t)(now - lastActivityMs_) >= cfg::LORA_HEALTH_PERIOD_MS) {
        healthPending_ = true;
        sendCommand("AT", now);
      }
      break;

    case State::Tx:
      if (txOkSeen_) {
        // The module answers +OK when it accepts the frame; the next AT+SEND
        // is only safe after the frame left the antenna (+ERR=17 otherwise).
        if ((uint32_t)(now - txStartMs_) >= lastAirtimeMs_ + 20) {
          txDone_ = true;
          txOk_ = true;
          ++txFrames_;
          state_ = State::Rx;
          lastActivityMs_ = now;
        }
      } else if ((uint32_t)(now - txStartMs_) >= cfg::LORA_TX_TIMEOUT_MS) {
        fault("no answer to AT+SEND", 0, now);
      }
      break;

    case State::Fault:
      if ((uint32_t)(now - faultMs_) >= cfg::LORA_REINIT_PERIOD_MS) {
        LOGI("LORA", "re-initialising RYLR998 (hardware reset)");
        startInit(now);
      }
      break;
  }
}

LoraManager::TxResult LoraManager::startTransmit(const uint8_t* data, size_t len, uint32_t now) {
  if (state_ == State::Tx || (state_ == State::Rx && cmdPending_) || state_ == State::Init)
    return TxResult::Busy;
  if (state_ != State::Rx) return TxResult::Error;
  char buf[rylr::MAX_PAYLOAD + 32];
  const size_t n = rylr::buildSend(buf, sizeof buf, cfg::LORA_DEST_ADDRESS, data, len);
  if (n == 0) {
    LOGE("LORA", "payload rejected (%u B, max %u, no CR/LF)", static_cast<unsigned>(len),
         static_cast<unsigned>(rylr::MAX_PAYLOAD));
    return TxResult::Error;
  }
  Serial1.write(reinterpret_cast<const uint8_t*>(buf), n);  // ~22 ms for 250 B @115200
  cmdPending_ = true;
  cmdSentMs_ = now;
  txStartMs_ = now;
  txOkSeen_ = false;
  txDone_ = false;
  lastAirtimeMs_ = rylr::airtimeMs(cfg::LORA_SF, cfg::LORA_BW_CODE, cfg::LORA_CR_CODE,
                                   cfg::LORA_PREAMBLE, len);
  state_ = State::Tx;
  return TxResult::Started;
}

// ---------------------------------------------------------------------------
void LoraManager::pumpUart(uint32_t now) {
  if (lineLen_ > 0 && (uint32_t)(now - lineStartMs_) >= LINE_TIMEOUT_MS) {
    ++rxErrors_;
    LOGW("LORA", "incomplete UART line dropped (%u B)", static_cast<unsigned>(lineLen_));
    lineLen_ = 0;
  }
  for (int i = 0; i < UART_FIFO && Serial1.available() > 0; ++i) {
    const int c = Serial1.read();
    if (c < 0) break;
    if (lineLen_ == 0) lineStartMs_ = now;
    if (c != '\n') {
      if (lineLen_ < sizeof line_) line_[lineLen_++] = static_cast<char>(c);
      else { ++rxErrors_; lineLen_ = 0; }  // overlong garbage
      continue;
    }
    size_t len = lineLen_;
    if (len > 0 && line_[len - 1] == '\r') --len;
    if (len == 0) { lineLen_ = 0; continue; }
    if (startsWith(line_, len, "+RCV=")) {
      rylr::RcvFrame f;
      const rylr::RcvResult r = rylr::parseRcv(line_, len, f);
      if (r == rylr::RcvResult::Incomplete && lineLen_ + 1 < sizeof line_) {
        line_[lineLen_++] = '\n';  // the '\n' belonged to the data
        continue;
      }
      if (r == rylr::RcvResult::Ok) {
        if (rxReady_) ++rxOverruns_;  // previous frame not consumed: keep newest
        memcpy(rxBuf_, f.data, f.length);
        rxLen_ = f.length;
        rxAddr_ = f.address;
        rxRssi_ = static_cast<float>(f.rssi);
        rxSnr_ = static_cast<float>(f.snr);
        rxReady_ = true;
        ++rxFrames_;
        LOGD("LORA", "< +RCV from %u, %u B, RSSI %d SNR %d", f.address,
             static_cast<unsigned>(f.length), f.rssi, f.snr);
      } else {
        ++rxErrors_;
        LOGW("LORA", "malformed +RCV line dropped");
      }
    } else {
      handleLine(line_, len, now);
    }
    lineLen_ = 0;
  }
}

void LoraManager::handleLine(const char* line, size_t len, uint32_t now) {
  if (startsWith(line, len, "+OK")) {
    onOk(now);
  } else if (startsWith(line, len, "+ERR=")) {
    onErr(rylr::parseErr(line, len), now);
  } else if (startsWith(line, len, "+READY")) {
    if (state_ == State::Init && phase_ == Phase::WaitReady) {
      phase_ = Phase::Config;
    } else if (state_ == State::Rx || state_ == State::Tx) {
      // Unexpected module reset (brown-out?): its RAM settings (e.g. CPIN) are gone.
      LOGW("LORA", "module restarted by itself: reconfiguring");
      if (state_ == State::Tx) { txDone_ = true; txOk_ = false; }
      state_ = State::Init;
      phase_ = Phase::Config;
      cfgStep_ = 0;
      cmdPending_ = healthPending_ = false;
    }
  } else if (!startsWith(line, len, "+RESET")) {
    char tmp[48];
    const size_t n = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
    memcpy(tmp, line, n);
    tmp[n] = '\0';
    LOGD("LORA", "< %s", tmp);
  }
}

void LoraManager::onOk(uint32_t now) {
  if (!cmdPending_) return;
  cmdPending_ = false;
  lastActivityMs_ = now;
  if (state_ == State::Init) ++cfgStep_;
  else if (state_ == State::Tx) txOkSeen_ = true;
  healthPending_ = false;
}

void LoraManager::onErr(int code, uint32_t now) {
  if (code == ERR_RX_CRC || code == ERR_RX_HEADER) {  // unsolicited RX errors
    ++rxErrors_;
    LOGD("LORA", "RX error +ERR=%d", code);
    return;
  }
  if (!cmdPending_) {
    LOGW("LORA", "unexpected +ERR=%d", code);
    return;
  }
  cmdPending_ = false;
  if (state_ == State::Init) {
    fault("configuration rejected (+ERR)", code, now);
  } else if (state_ == State::Tx) {
    LOGW("LORA", "AT+SEND rejected: +ERR=%d", code);
    txDone_ = true;
    txOk_ = false;
    state_ = State::Rx;
    lastActivityMs_ = now;
  } else {  // health "AT" answered with an error: the module is alive
    healthPending_ = false;
    lastActivityMs_ = now;
  }
}

bool LoraManager::takeTxDone(bool& ok) {
  if (!txDone_) return false;
  txDone_ = false;
  ok = txOk_;
  return true;
}

bool LoraManager::takeRx(uint8_t* buf, size_t cap, size_t& len, float& rssi, float& snr) {
  if (!rxReady_) return false;
  rxReady_ = false;
  if (rxLen_ > cap) return false;
  memcpy(buf, rxBuf_, rxLen_);
  len = rxLen_;
  rssi = rxRssi_;
  snr = rxSnr_;
  return true;
}

// AirGuard IoT - non-blocking driver for the REYAX RYLR998 LoRa module
// (UART AT commands; pins and UART in config/hardware_pins.h / platform/board.h).
//
// State machine:
//   Uninit --begin--> Init (NRST pulse, wait +READY, send config one command
//                           at a time, each waiting for +OK)
//   Init --all +OK--> Rx (idle, module is receiving; +RCV lines arrive any time)
//   Rx --startTransmit--> Tx (AT+SEND; done when +OK arrived AND the computed
//                             time on air elapsed) --> Rx
//   any --timeout / +ERR during config / no answer to the health "AT"--> Fault
//   Fault --every 30 s--> Init (hardware reset again)
// Only one AT command is outstanding at a time (the module requires it).
#pragma once
#include <stddef.h>
#include <stdint.h>

class LoraManager {
 public:
  enum class State : uint8_t { Uninit, Init, Rx, Tx, Fault };
  enum class TxResult : uint8_t { Started, Busy, Error };

  bool begin();               // starts the (non-blocking) init sequence
  void loop(uint32_t nowMs);

  // AT+SEND to cfg::LORA_DEST_ADDRESS. Busy -> retry later.
  TxResult startTransmit(const uint8_t* data, size_t len, uint32_t nowMs);
  // True once per finished TX. `ok` = module answered +OK.
  bool takeTxDone(bool& ok);
  // Copies one received frame. Returns false if none.
  bool takeRx(uint8_t* buf, size_t cap, size_t& len, float& rssi, float& snr);

  State state() const { return state_; }
  bool  ready() const { return state_ == State::Rx && !cmdPending_; }
  uint16_t lastRxAddress() const { return rxAddr_; }
  uint32_t crcErrors() const { return rxErrors_; }   // +ERR=12/19 and bad +RCV lines
  uint32_t rxFrames() const { return rxFrames_; }
  uint32_t txFrames() const { return txFrames_; }
  uint32_t rxOverruns() const { return rxOverruns_; }
  uint32_t lastAirtimeMs() const { return lastAirtimeMs_; }
  // Test hook (serial console): force a Fault to exercise "LoRa lost".
  void simulateFault(uint32_t nowMs) { fault("simulated", 0, nowMs); }

 private:
  enum class Phase : uint8_t { ResetLow, WaitReady, Config };

  void startInit(uint32_t nowMs);
  bool nextConfigCommand(char* out, size_t cap);
  void sendCommand(const char* cmd, uint32_t nowMs);
  void pumpUart(uint32_t nowMs);
  void handleLine(const char* line, size_t len, uint32_t nowMs);
  void onOk(uint32_t nowMs);
  void onErr(int code, uint32_t nowMs);
  void fault(const char* why, int code, uint32_t nowMs);

  State    state_ = State::Uninit;
  Phase    phase_ = Phase::ResetLow;
  uint32_t phaseMs_ = 0;
  uint8_t  cfgStep_ = 0;
  uint32_t faultMs_ = 0;

  bool     cmdPending_ = false;
  bool     healthPending_ = false;
  uint32_t cmdSentMs_ = 0;
  uint32_t lastActivityMs_ = 0;

  // TX
  uint32_t txStartMs_ = 0;
  bool     txOkSeen_ = false;
  bool     txDone_ = false, txOk_ = false;
  uint32_t lastAirtimeMs_ = 0;

  // UART line assembly
  char     line_[300];
  size_t   lineLen_ = 0;
  uint32_t lineStartMs_ = 0;

  // RX
  bool     rxReady_ = false;
  uint8_t  rxBuf_[256];
  size_t   rxLen_ = 0;
  uint16_t rxAddr_ = 0;
  float    rxRssi_ = 0, rxSnr_ = 0;
  uint32_t rxErrors_ = 0, rxFrames_ = 0, rxOverruns_ = 0, txFrames_ = 0;
};

// AirGuard IoT - REYAX RYLR998 AT-command framing (pure, unit tested).
//
// Source: REYAX "LoRa AT Command Guide RYLR998/RYLR498" (2021):
//   AT+SEND=<Address>,<Payload Length>,<Data>\r\n   -> +OK | +ERR=<n>
//   +RCV=<Address>,<Length>,<Data>,<RSSI>,<SNR>\r\n (unsolicited)
//   payload <= 240 bytes; "the payload part will increase more 8 bytes".
// <Data> may contain commas (JSON!), so +RCV is split by <Length>, never by
// searching for commas inside the data.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace rylr {

constexpr size_t MAX_PAYLOAD = 240;
constexpr size_t PAYLOAD_OVERHEAD = 8;  // added by the module firmware

struct RcvFrame {
  uint16_t       address = 0;  // transmitter address
  size_t         length = 0;
  const char*    data = nullptr;  // points into the line buffer (not NUL-terminated)
  int            rssi = 0;
  int            snr = 0;
};

enum class RcvResult : uint8_t {
  Ok,
  Incomplete,  // header ok but fewer than <Length> data bytes yet (a '\n' inside data)
  Invalid,
};

// `line` = bytes of one +RCV line WITHOUT the trailing "\r\n".
RcvResult parseRcv(const char* line, size_t len, RcvFrame& out);

// Builds "AT+SEND=<addr>,<len>,<data>\r\n". Returns length, 0 if it does not fit
// or len is 0 / > MAX_PAYLOAD / data contains CR or LF.
size_t buildSend(char* out, size_t cap, uint16_t address, const uint8_t* data, size_t len);

// "+ERR=<n>" -> n, otherwise -1.
int parseErr(const char* line, size_t len);

// LoRa time on air (Semtech formula, explicit header, CRC on) in ms, for the
// over-the-air size len + PAYLOAD_OVERHEAD. bwCode: 7=125, 8=250, 9=500 kHz.
uint32_t airtimeMs(uint8_t sf, uint8_t bwCode, uint8_t crCode, uint16_t preamble, size_t len);

}  // namespace rylr

#include "rylr998_codec.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace rylr {

namespace {

// Parses an optionally signed decimal integer starting at p (spaces allowed
// before it). Advances p. Returns false if no digit.
bool readInt(const char*& p, const char* end, long& v) {
  while (p < end && *p == ' ') ++p;
  bool neg = false;
  if (p < end && (*p == '-' || *p == '+')) { neg = *p == '-'; ++p; }
  if (p >= end || *p < '0' || *p > '9') return false;
  long x = 0;
  while (p < end && *p >= '0' && *p <= '9') {
    x = x * 10 + (*p - '0');
    if (x > 1000000) return false;
    ++p;
  }
  v = neg ? -x : x;
  return true;
}

bool expectComma(const char*& p, const char* end) {
  while (p < end && *p == ' ') ++p;
  if (p >= end || *p != ',') return false;
  ++p;
  return true;
}

}  // namespace

RcvResult parseRcv(const char* line, size_t len, RcvFrame& out) {
  out = RcvFrame();
  static const char kHdr[] = "+RCV=";
  if (len < sizeof kHdr - 1 || memcmp(line, kHdr, sizeof kHdr - 1) != 0) return RcvResult::Invalid;
  const char* p = line + sizeof kHdr - 1;
  const char* end = line + len;
  long addr, n;
  if (!readInt(p, end, addr) || addr < 0 || addr > 65535) return RcvResult::Invalid;
  if (!expectComma(p, end)) return RcvResult::Invalid;
  if (!readInt(p, end, n) || n <= 0 || n > static_cast<long>(MAX_PAYLOAD)) return RcvResult::Invalid;
  if (!expectComma(p, end)) return RcvResult::Invalid;

  // Data = exactly n bytes, followed by ",<rssi>,<snr>". The guide's example
  // shows a space after the commas ("+RCV=50, 5, HELLO, -99, 40"); real
  // modules do not send it, but accept a single leading space if the length
  // only matches that way.
  const char* data = p;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (end - data < n) return RcvResult::Incomplete;
    const char* q = data + n;
    long rssi, snr;
    if (expectComma(q, end) && readInt(q, end, rssi) && expectComma(q, end) &&
        readInt(q, end, snr)) {
      while (q < end && (*q == ' ' || *q == ',')) ++q;  // tolerate trailing ','
      if (q == end) {
        out.address = static_cast<uint16_t>(addr);
        out.length = static_cast<size_t>(n);
        out.data = data;
        out.rssi = static_cast<int>(rssi);
        out.snr = static_cast<int>(snr);
        return RcvResult::Ok;
      }
    }
    if (attempt == 0 && *data == ' ') { ++data; continue; }
    break;
  }
  // Fewer trailer fields than expected may mean the data contained "\n" and
  // the line is not finished yet; the caller keeps reading until a timeout or
  // buffer limit.
  if (end - data < n + 4) return RcvResult::Incomplete;
  return RcvResult::Invalid;
}

size_t buildSend(char* out, size_t cap, uint16_t address, const uint8_t* data, size_t len) {
  if (!out || !data || len == 0 || len > MAX_PAYLOAD) return 0;
  for (size_t i = 0; i < len; ++i)
    if (data[i] == '\r' || data[i] == '\n') return 0;
  const int h = snprintf(out, cap, "AT+SEND=%u,%u,", static_cast<unsigned>(address),
                         static_cast<unsigned>(len));
  if (h <= 0 || static_cast<size_t>(h) + len + 2 >= cap) return 0;
  memcpy(out + h, data, len);
  out[h + len] = '\r';
  out[h + len + 1] = '\n';
  out[h + len + 2] = '\0';
  return static_cast<size_t>(h) + len + 2;
}

int parseErr(const char* line, size_t len) {
  if (len < 6 || memcmp(line, "+ERR=", 5) != 0) return -1;
  const char* p = line + 5;
  long v;
  if (!readInt(p, line + len, v)) return -1;
  return static_cast<int>(v);
}

uint32_t airtimeMs(uint8_t sf, uint8_t bwCode, uint8_t crCode, uint16_t preamble, size_t len) {
  const double bwHz = bwCode == 9 ? 500000.0 : (bwCode == 8 ? 250000.0 : 125000.0);
  const double tsym = static_cast<double>(1UL << sf) / bwHz;  // s
  const int de = tsym > 0.016 ? 1 : 0;                         // low data rate optimisation
  const double pl = static_cast<double>(len + PAYLOAD_OVERHEAD);
  const double num = 8.0 * pl - 4.0 * sf + 28.0 + 16.0 /*CRC*/;
  const double den = 4.0 * (sf - 2.0 * de);
  double nPayload = ceil(num / den) * (crCode + 4);
  if (nPayload < 0) nPayload = 0;
  nPayload += 8;
  const double t = (preamble + 4.25) * tsym + nPayload * tsym;
  return static_cast<uint32_t>(ceil(t * 1000.0));
}

}  // namespace rylr

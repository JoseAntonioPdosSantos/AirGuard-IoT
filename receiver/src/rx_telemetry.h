// AirGuard Receiver - telemetry lines for the AirGuard Web dashboard (pure, unit tested).
//
// Same format as the node (../src/communication/telemetry_codec.h):
//   @T {"src":"gw","k":"rx","ms":12345,...}\r\n
// written on the USB serial port next to the normal log. The bridge on the
// laptop forwards them to the web server. Records: ../docs/11_telemetry.md.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace rx {

class TlmLine {
 public:
  static constexpr size_t CAP = 720;  // the frame (<= 240 B) is JSON-escaped
  TlmLine(const char* kind, uint32_t ms);
  void str(const char* k, const char* v);
  void strn(const char* k, const char* v, size_t n);  // v need not be NUL-terminated
  void num(const char* k, long v);
  void boolean(const char* k, bool v);
  // Closes the line ("}\r\n"). Returns the length, 0 if it did not fit.
  size_t finish();
  const char* c_str() const { return buf_; }

 private:
  void put(char c);
  void raw(const char* s);
  void key(const char* k);
  void quoted(const char* s, size_t n);
  char   buf_[CAP + 1] = {};
  size_t len_ = 0;
  bool   ok_ = true;
};

}  // namespace rx

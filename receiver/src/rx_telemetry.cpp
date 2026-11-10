#include "rx_telemetry.h"

#include <stdio.h>
#include <string.h>

namespace rx {

TlmLine::TlmLine(const char* kind, uint32_t ms) {
  raw("@T {\"src\":\"gw\"");
  str("k", kind);
  num("ms", static_cast<long>(ms));
}

void TlmLine::put(char c) {
  if (!ok_) return;
  if (len_ + 3 >= CAP) { ok_ = false; return; }  // keep room for "}\r\n"
  buf_[len_++] = c;
}

void TlmLine::raw(const char* s) { while (*s) put(*s++); }

void TlmLine::key(const char* k) {
  put(',');
  quoted(k, strlen(k));
  put(':');
}

void TlmLine::quoted(const char* s, size_t n) {
  put('"');
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '"' || c == '\\') { put('\\'); put(static_cast<char>(c)); }
    else if (c < 0x20) {
      char tmp[7];
      snprintf(tmp, sizeof tmp, "\\u%04x", c);
      raw(tmp);
    } else put(static_cast<char>(c));
  }
  put('"');
}

void TlmLine::str(const char* k, const char* v) { key(k); quoted(v, strlen(v)); }
void TlmLine::strn(const char* k, const char* v, size_t n) { key(k); quoted(v, n); }

void TlmLine::num(const char* k, long v) {
  char tmp[24];
  snprintf(tmp, sizeof tmp, "%ld", v);
  key(k);
  raw(tmp);
}

void TlmLine::boolean(const char* k, bool v) { key(k); raw(v ? "true" : "false"); }

size_t TlmLine::finish() {
  if (!ok_) { buf_[0] = '\0'; return 0; }
  buf_[len_++] = '}';
  buf_[len_++] = '\r';
  buf_[len_++] = '\n';
  buf_[len_] = '\0';
  return len_;
}

}  // namespace rx

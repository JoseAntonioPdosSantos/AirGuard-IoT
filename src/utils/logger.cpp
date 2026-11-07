#include "logger.h"

#include <stdarg.h>
#include <stdio.h>

namespace {
LogLevel g_level = LogLevel::Info;
const char* levelName(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return "DBG";
    case LogLevel::Info: return "INF";
    case LogLevel::Warn: return "WRN";
    default: return "ERR";
  }
}
}  // namespace

namespace logger {

void begin(uint32_t baud) { Serial.begin(baud); }  // USB CDC: never wait for a host
void setLevel(LogLevel l) { g_level = l; }
LogLevel level() { return g_level; }

void log(LogLevel l, const char* tag, const char* fmt, ...) {
  if (l < g_level) return;
  // Never block if no USB host is reading: drop the line instead.
  if (!Serial) return;
  char msg[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  char line[240];
  const int n = snprintf(line, sizeof line, "[%10lu] %s %-6s %s\r\n",
                         static_cast<unsigned long>(millis()), levelName(l), tag, msg);
  if (n <= 0) return;
  const size_t len = static_cast<size_t>(n) < sizeof line ? static_cast<size_t>(n) : sizeof line - 1;
  if (Serial.availableForWrite() < static_cast<int>(len)) return;  // non-blocking
  Serial.write(reinterpret_cast<const uint8_t*>(line), len);
}

}  // namespace logger

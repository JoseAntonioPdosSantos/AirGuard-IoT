// AirGuard IoT - leveled diagnostic logging over USB Serial.
// Format: "[uptime_ms] LEVEL tag: message"
#pragma once
#include <Arduino.h>

enum class LogLevel : uint8_t { Debug = 0, Info = 1, Warn = 2, Error = 3 };

namespace logger {
void begin(uint32_t baud);
void setLevel(LogLevel l);
LogLevel level();
void log(LogLevel l, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
}  // namespace logger

#define LOGD(tag, ...) logger::log(LogLevel::Debug, tag, __VA_ARGS__)
#define LOGI(tag, ...) logger::log(LogLevel::Info, tag, __VA_ARGS__)
#define LOGW(tag, ...) logger::log(LogLevel::Warn, tag, __VA_ARGS__)
#define LOGE(tag, ...) logger::log(LogLevel::Error, tag, __VA_ARGS__)

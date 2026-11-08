#include "event_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace rx {

namespace {

// Finds "key": in a flat JSON object and returns a pointer to the first char of
// the value (after optional spaces), or nullptr. Keys are matched only at
// object level positions (after '{' or ',').
const char* findValue(const char* s, size_t len, const char* key) {
  const size_t kl = strlen(key);
  bool inStr = false;
  for (size_t i = 0; i < len; ++i) {
    const char c = s[i];
    if (inStr) {
      if (c == '\\') { ++i; continue; }
      if (c == '"') inStr = false;
      continue;
    }
    if (c != '"') continue;
    // A key starts right after '{' or ',' (ignoring spaces).
    size_t j = i;
    while (j > 0 && (s[j - 1] == ' ')) --j;
    const bool atKey = (j > 0 && (s[j - 1] == '{' || s[j - 1] == ','));
    if (atKey && i + 1 + kl + 1 < len && memcmp(s + i + 1, key, kl) == 0 && s[i + 1 + kl] == '"') {
      size_t k = i + 2 + kl;
      while (k < len && s[k] == ' ') ++k;
      if (k >= len || s[k] != ':') return nullptr;
      ++k;
      while (k < len && s[k] == ' ') ++k;
      return k < len ? s + k : nullptr;
    }
    inStr = true;  // skip this string (a key we do not want, or a value)
  }
  return nullptr;
}

bool getString(const char* s, size_t len, const char* key, char* out, size_t cap) {
  const char* v = findValue(s, len, key);
  if (!v || *v != '"') return false;
  const char* end = s + len;
  size_t n = 0;
  for (const char* p = v + 1; p < end; ++p) {
    if (*p == '"') { out[n] = '\0'; return true; }
    if (*p == '\\') return false;  // ids/enums never need escapes
    if (static_cast<unsigned char>(*p) < 0x20) return false;
    if (n + 1 >= cap) return false;
    out[n++] = *p;
  }
  return false;
}

bool getNumber(const char* s, size_t len, const char* key, double& out) {
  const char* v = findValue(s, len, key);
  if (!v) return false;
  char tmp[24];
  size_t n = 0;
  const char* end = s + len;
  for (const char* p = v; p < end && n + 1 < sizeof tmp; ++p) {
    if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.' || *p == 'e' || *p == 'E' || *p == '+')
      tmp[n++] = *p;
    else break;
  }
  if (n == 0) return false;
  tmp[n] = '\0';
  char* e = nullptr;
  out = strtod(tmp, &e);
  return e && *e == '\0';
}

bool parseSev(const char* s, Sev& out) {
  if (!strcmp(s, "NORMAL")) out = Sev::Normal;
  else if (!strcmp(s, "LOW")) out = Sev::Low;
  else if (!strcmp(s, "MEDIUM")) out = Sev::Medium;
  else if (!strcmp(s, "HIGH")) out = Sev::High;
  else return false;
  return true;
}

bool parseCat(const char* s, Cat& out) {
  if (!strcmp(s, "AIR_QUALITY")) out = Cat::Air;
  else if (!strcmp(s, "TEMPERATURE")) out = Cat::Temp;
  else if (!strcmp(s, "HUMIDITY")) out = Cat::Hum;
  else if (!strcmp(s, "SYSTEM")) out = Cat::System;
  else return false;
  return true;
}

}  // namespace

ParseResult parseEvent(const char* d, size_t len, Event& e) {
  e = Event{};
  while (len > 0 && (d[len - 1] == ' ' || d[len - 1] == '\r' || d[len - 1] == '\n')) --len;
  if (len < 2 || d[0] != '{' || d[len - 1] != '}') return ParseResult::NotJson;

  char type[16];
  if (!getString(d, len, "type", type, sizeof type)) return ParseResult::MissingField;
  if (!strcmp(type, "RECOVERY")) e.recovery = true;
  else if (strcmp(type, "ALERT") != 0) return ParseResult::NotEvent;

  char cat[16], sev[12];
  double id = 0;
  if (!getString(d, len, "device", e.device, sizeof e.device) ||
      !getString(d, len, "category", cat, sizeof cat) ||
      !getString(d, len, "severity", sev, sizeof sev) || !getNumber(d, len, "event_id", id))
    return ParseResult::MissingField;
  getString(d, len, "dst", e.dst, sizeof e.dst);  // optional
  if (!parseCat(cat, e.category) || !parseSev(sev, e.severity) || id < 0 || id > 4294967295.0)
    return ParseResult::BadValue;
  e.eventId = static_cast<uint32_t>(id);
  if (e.recovery) e.severity = Sev::Normal;

  double v;
  if (getNumber(d, len, "air_index", v) && v >= 0 && v <= 100) { e.hasAir = true; e.airIndex = static_cast<uint32_t>(v); }
  if (getNumber(d, len, "temperature", v) && v > -60 && v < 100) { e.hasTemp = true; e.temperature = static_cast<float>(v); }
  if (getNumber(d, len, "humidity", v) && v >= 0 && v <= 100) { e.hasHum = true; e.humidity = static_cast<float>(v); }
  if (getNumber(d, len, "flags", v) && v >= 0) { e.hasFlags = true; e.flags = static_cast<uint32_t>(v); }
  char note[12];
  if (getString(d, len, "note", note, sizeof note)) e.reminder = !strcmp(note, "REMINDER");
  return ParseResult::Ok;
}

size_t buildAck(char* out, size_t cap, const char* gw, const char* node, uint32_t id) {
  const int n = snprintf(out, cap, "{\"type\":\"ACK\",\"device\":\"%s\",\"dst\":\"%s\",\"ack_for\":%lu}",
                         gw, node, static_cast<unsigned long>(id));
  return (n > 0 && static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : 0;
}

bool SeenIds::checkAndInsert(uint32_t id) {
  for (uint8_t i = 0; i < count_; ++i)
    if (ids_[i] == id) return true;
  ids_[pos_] = id;
  pos_ = static_cast<uint8_t>((pos_ + 1) % N);
  if (count_ < N) ++count_;
  return false;
}

const char* sevName(Sev s) {
  switch (s) {
    case Sev::Low: return "LOW";
    case Sev::Medium: return "MEDIUM";
    case Sev::High: return "HIGH";
    default: return "NORMAL";
  }
}

const char* catNamePt(Cat c) {
  switch (c) {
    case Cat::Air: return "GAS";
    case Cat::Temp: return "TEMP";
    case Cat::Hum: return "UMIDADE";
    default: return "SISTEMA";
  }
}

}  // namespace rx

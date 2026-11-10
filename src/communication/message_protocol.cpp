#include "message_protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../config/thresholds.h"
#include "../monitoring/alert_types.h"

namespace proto {

const char* parseErrorName(ParseError e) {
  switch (e) {
    case ParseError::Ok: return "OK";
    case ParseError::Empty: return "EMPTY";
    case ParseError::TooLong: return "TOO_LONG";
    case ParseError::Malformed: return "MALFORMED";
    case ParseError::Nested: return "NESTED";
    case ParseError::UnknownType: return "UNKNOWN_TYPE";
    case ParseError::MissingField: return "MISSING_FIELD";
    case ParseError::FieldTooLong: return "FIELD_TOO_LONG";
    case ParseError::BadValue: return "BAD_VALUE";
  }
  return "?";
}

// ===========================================================================
// JsonWriter
// ===========================================================================
JsonWriter::JsonWriter(char* buf, size_t cap) : buf_(buf), cap_(cap) {
  if (cap_ < 3) { ok_ = false; return; }
  put('{');
}

void JsonWriter::put(char c) {
  if (!ok_) return;
  if (len_ + 1 >= cap_) { ok_ = false; return; }  // keep room for '\0'
  buf_[len_++] = c;
  buf_[len_] = '\0';
}

void JsonWriter::raw(const char* s) { while (*s) put(*s++); }

void JsonWriter::escaped(const char* s) { escaped(s, strlen(s)); }

void JsonWriter::escaped(const char* s, size_t n) {
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

void JsonWriter::key(const char* k) {
  if (!first_) put(',');
  first_ = false;
  escaped(k);
  put(':');
}

void JsonWriter::str(const char* k, const char* v) { key(k); escaped(v); }
void JsonWriter::strn(const char* k, const char* v, size_t n) { key(k); escaped(v, n); }

void JsonWriter::num(const char* k, int32_t v) {
  char tmp[16];
  snprintf(tmp, sizeof tmp, "%ld", static_cast<long>(v));
  key(k); raw(tmp);
}

void JsonWriter::unum(const char* k, uint32_t v) {
  char tmp[16];
  snprintf(tmp, sizeof tmp, "%lu", static_cast<unsigned long>(v));
  key(k); raw(tmp);
}

void JsonWriter::flt(const char* k, float v, uint8_t decimals) {
  key(k);
  if (isnan(v) || isinf(v)) { raw("null"); return; }
  // Fixed-point formatting without relying on printf("%f") support.
  const bool neg = v < 0;
  float a = neg ? -v : v;
  if (a > 1.0e6f) a = 1.0e6f;            // keep the fixed-point value in range
  const int dec = decimals > 3 ? 3 : decimals;
  uint32_t scale = 1;
  for (int i = 0; i < dec; ++i) scale *= 10;
  const uint32_t fixed = static_cast<uint32_t>(a * scale + 0.5f);
  char tmp[40];
  if (dec == 0)
    snprintf(tmp, sizeof tmp, "%s%lu", neg && fixed ? "-" : "", (unsigned long)fixed);
  else
    snprintf(tmp, sizeof tmp, "%s%lu.%0*lu", neg && fixed ? "-" : "",
             (unsigned long)(fixed / scale), dec, (unsigned long)(fixed % scale));
  raw(tmp);
}

void JsonWriter::boolean(const char* k, bool v) { key(k); raw(v ? "true" : "false"); }

size_t JsonWriter::finish() {
  put('}');
  return ok_ ? len_ : 0;
}

// ===========================================================================
// Encoders
// ===========================================================================
uint32_t airIndex(float gasVolts) {
  float i = 100.0f * gasVolts / mq135::CIRCUIT_VOLTAGE;
  if (!(i > 0)) i = 0;  // also NaN
  if (i > 100) i = 100;
  return static_cast<uint32_t>(i + 0.5f);
}

static const char* sensorFor(Category c) {
  switch (c) {
    case Category::AirQuality: return "MQ135";
    case Category::Temperature:
    case Category::Humidity: return "DHT11";
    default: return "SYSTEM";
  }
}

// detail 0 = full, 1 = compact (drops optional diagnostics), 2 = minimal
// (also drops v/sensor/note). Guarantees cfg::LORA_MAX_PAYLOAD (240 B, RYLR998).
static size_t encodeEventDetail(const AlertEvent& e, const char* device, const char* dst,
                                char* buf, size_t cap, uint8_t detail) {
  JsonWriter w(buf, cap);
  if (detail < 2) w.num("v", VERSION);
  w.str("type", eventKindName(e.kind));
  w.str("device", device);
  w.str("dst", dst);
  w.unum("event_id", e.eventId);
  w.str("category", categoryName(e.category));
  w.str("severity", severityName(e.severity));
  if (detail == 0) w.str("prev", severityName(e.previous));
  if (detail < 2) w.str("sensor", sensorFor(e.category));
  const SensorSnapshot& s = e.snapshot;
  if (detail == 0 || e.category == Category::AirQuality) w.unum("raw_adc", s.rawAdc);
  if (s.airValid) w.unum("air_index", airIndex(s.gasVolts));
  if (s.envValid) {
    w.flt("temperature", s.temperature, 1);
    w.flt("humidity", s.humidity, 0);
    if (detail == 0 && e.category == Category::Temperature) w.flt("heat_index", s.heatIndex, 1);
  }
  if (e.category == Category::System) w.unum("flags", e.systemFlags);
  if (detail < 2) {
    if (e.reminder) w.str("note", "REMINDER");
    else if (e.restored) w.str("note", "RESTORED");
  }
  w.unum("timestamp", e.uptimeS);
  return w.finish();
}

size_t encodeEvent(const AlertEvent& e, const char* device, const char* dst, char* buf,
                   size_t cap) {
  if (cap > cfg::LORA_MAX_PAYLOAD + 1) cap = cfg::LORA_MAX_PAYLOAD + 1;
  size_t n = encodeEventDetail(e, device, dst, buf, cap, 0);
  if (n == 0) n = encodeEventDetail(e, device, dst, buf, cap, 1);
  if (n == 0) n = encodeEventDetail(e, device, dst, buf, cap, 2);
  return n;
}

size_t encodeAck(const char* device, const char* dst, uint32_t ackFor, char* buf, size_t cap) {
  if (cap > cfg::LORA_MAX_PAYLOAD + 1) cap = cfg::LORA_MAX_PAYLOAD + 1;
  JsonWriter w(buf, cap);
  w.num("v", VERSION);
  w.str("type", "ACK");
  w.str("device", device);
  w.str("dst", dst);
  w.unum("ack_for", ackFor);
  return w.finish();
}

size_t encodeHeartbeat(const char* device, const char* dst, uint32_t seq,
                       const Severity levels[CATEGORY_COUNT], const SensorSnapshot& s,
                       uint32_t uptimeS, char* buf, size_t cap) {
  if (cap > cfg::LORA_MAX_PAYLOAD + 1) cap = cfg::LORA_MAX_PAYLOAD + 1;
  static const char kLetter[] = {'N', 'L', 'M', 'H'};
  char lv[CATEGORY_COUNT + 1];
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) lv[i] = kLetter[static_cast<uint8_t>(levels[i]) & 3];
  lv[CATEGORY_COUNT] = '\0';
  JsonWriter w(buf, cap);
  w.num("v", VERSION);
  w.str("type", "HB");
  w.str("device", device);
  w.str("dst", dst);
  w.unum("seq", seq);
  w.str("levels", lv);
  if (s.airValid) w.unum("air_index", airIndex(s.gasVolts));
  if (s.envValid) {
    w.flt("temperature", s.temperature, 1);
    w.flt("humidity", s.humidity, 0);
  }
  w.unum("timestamp", uptimeS);
  return w.finish();
}

// ===========================================================================
// Parser (flat object only)
// ===========================================================================
namespace {

struct Cur {
  const char* p;
  const char* end;
  bool atEnd() const { return p >= end; }
  char peek() const { return atEnd() ? '\0' : *p; }
};

void skipWs(Cur& c) {
  while (!c.atEnd() && (*c.p == ' ' || *c.p == '\t' || *c.p == '\n' || *c.p == '\r')) ++c.p;
}

int hexVal(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

// Appends a code point as UTF-8. Returns false when it does not fit.
bool putUtf8(char* out, size_t cap, size_t& n, uint32_t cp) {
  char tmp[4];
  size_t k;
  if (cp < 0x80) { tmp[0] = (char)cp; k = 1; }
  else if (cp < 0x800) { tmp[0] = (char)(0xC0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
  else { tmp[0] = (char)(0xE0 | (cp >> 12)); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
         tmp[2] = (char)(0x80 | (cp & 0x3F)); k = 3; }
  if (n + k > cap) return false;
  for (size_t i = 0; i < k; ++i) out[n++] = tmp[i];
  return true;
}

// Parses a JSON string into out (cap = max bytes, excluding '\0').
// Returns Ok, Malformed or FieldTooLong. If out==nullptr the value is skipped.
ParseError parseString(Cur& c, char* out, size_t cap) {
  if (c.peek() != '"') return ParseError::Malformed;
  ++c.p;
  size_t n = 0;
  bool overflow = false;
  while (true) {
    if (c.atEnd()) return ParseError::Malformed;
    unsigned char ch = static_cast<unsigned char>(*c.p++);
    if (ch == '"') break;
    if (ch < 0x20) return ParseError::Malformed;  // raw control char
    uint32_t cp = ch;
    bool isRawByte = true;
    if (ch == '\\') {
      if (c.atEnd()) return ParseError::Malformed;
      const char e = *c.p++;
      isRawByte = false;
      switch (e) {
        case '"': cp = '"'; break;
        case '\\': cp = '\\'; break;
        case '/': cp = '/'; break;
        case 'b': case 'f': case 'n': case 'r': case 't': cp = ' '; break;  // whitespace
        case 'u': {
          if (c.end - c.p < 4) return ParseError::Malformed;
          cp = 0;
          for (int i = 0; i < 4; ++i) {
            const int h = hexVal(*c.p++);
            if (h < 0) return ParseError::Malformed;
            cp = (cp << 4) | (uint32_t)h;
          }
          if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';  // surrogates: not displayable
          if (cp < 0x20) cp = ' ';
          break;
        }
        default: return ParseError::Malformed;
      }
    }
    if (!out || overflow) continue;
    if (isRawByte) {
      if (n + 1 > cap) overflow = true; else out[n++] = static_cast<char>(ch);
    } else if (!putUtf8(out, cap, n, cp)) {
      overflow = true;
    }
  }
  if (out) out[n] = '\0';
  return overflow ? ParseError::FieldTooLong : ParseError::Ok;
}

// Parses a number; sets isUInt/value when it is a non-negative integer that fits.
ParseError parseNumber(Cur& c, bool& isUInt, uint32_t& value) {
  const char* start = c.p;
  if (c.peek() == '-') ++c.p;
  bool digits = false, frac = false;
  uint64_t v = 0;
  bool overflow = false;
  while (!c.atEnd() && *c.p >= '0' && *c.p <= '9') {
    v = v * 10 + (uint64_t)(*c.p - '0');
    if (v > 0xFFFFFFFFull) overflow = true;
    digits = true;
    ++c.p;
  }
  if (!digits) return ParseError::Malformed;
  if (c.peek() == '.') {
    frac = true; ++c.p;
    if (c.atEnd() || *c.p < '0' || *c.p > '9') return ParseError::Malformed;
    while (!c.atEnd() && *c.p >= '0' && *c.p <= '9') ++c.p;
  }
  if (c.peek() == 'e' || c.peek() == 'E') {
    frac = true; ++c.p;
    if (c.peek() == '+' || c.peek() == '-') ++c.p;
    if (c.atEnd() || *c.p < '0' || *c.p > '9') return ParseError::Malformed;
    while (!c.atEnd() && *c.p >= '0' && *c.p <= '9') ++c.p;
  }
  isUInt = (*start != '-') && !frac && !overflow;
  value = isUInt ? (uint32_t)v : 0;
  return ParseError::Ok;
}

bool matchLiteral(Cur& c, const char* lit) {
  const size_t n = strlen(lit);
  if ((size_t)(c.end - c.p) < n || strncmp(c.p, lit, n) != 0) return false;
  c.p += n;
  return true;
}

enum Field : uint8_t { F_TYPE = 1, F_SOURCE = 2, F_DEVICE = 4, F_DST = 8, F_MSGID = 16,
                       F_ACKFOR = 32, F_TEXT = 64 };

}  // namespace

ParseError parse(const uint8_t* data, size_t len, IncomingMessage& out) {
  out = IncomingMessage();
  if (!data || len == 0) return ParseError::Empty;
  if (len > cfg::LORA_MAX_PAYLOAD) return ParseError::TooLong;

  Cur c{reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + len};
  char type[12] = {};
  char source[cfg::MAX_ID_LEN + 1] = {};
  char device[cfg::MAX_ID_LEN + 1] = {};
  uint32_t msgId = 0, ackFor = 0;
  uint8_t seen = 0;

  skipWs(c);
  if (c.peek() != '{') return ParseError::Malformed;
  ++c.p;
  skipWs(c);
  bool firstMember = true;
  while (true) {
    skipWs(c);
    if (c.peek() == '}') {
      if (!firstMember) return ParseError::Malformed;  // trailing comma "{...,}"
      ++c.p;
      break;
    }
    char key[16];
    ParseError e = parseString(c, key, sizeof key - 1);
    if (e == ParseError::FieldTooLong) key[0] = '\0';  // unknown long key: ignore
    else if (e != ParseError::Ok) return e;
    skipWs(c);
    if (c.peek() != ':') return ParseError::Malformed;
    ++c.p;
    skipWs(c);

    // Identify target field.
    uint8_t field = 0;
    char* strTarget = nullptr;
    size_t strCap = 0;
    if (!strcmp(key, "type")) { field = F_TYPE; strTarget = type; strCap = sizeof type - 1; }
    else if (!strcmp(key, "source")) { field = F_SOURCE; strTarget = source; strCap = cfg::MAX_ID_LEN; }
    else if (!strcmp(key, "device")) { field = F_DEVICE; strTarget = device; strCap = cfg::MAX_ID_LEN; }
    else if (!strcmp(key, "dst")) { field = F_DST; strTarget = out.dst; strCap = cfg::MAX_ID_LEN; }
    else if (!strcmp(key, "text")) { field = F_TEXT; strTarget = out.text; strCap = cfg::MAX_TEXT_BYTES; }
    else if (!strcmp(key, "message_id")) field = F_MSGID;
    else if (!strcmp(key, "ack_for")) field = F_ACKFOR;

    if (field && (seen & field)) return ParseError::Malformed;  // duplicate key
    seen |= field;

    const char v = c.peek();
    if (v == '{' || v == '[') return ParseError::Nested;
    if (v == '"') {
      if (field == F_MSGID || field == F_ACKFOR) return ParseError::BadValue;
      e = parseString(c, strTarget, strCap);
      if (e != ParseError::Ok) return e;
    } else if (v == '-' || (v >= '0' && v <= '9')) {
      bool isUInt = false;
      uint32_t num = 0;
      e = parseNumber(c, isUInt, num);
      if (e != ParseError::Ok) return e;
      if (strTarget) return ParseError::BadValue;  // string field given a number
      if (field == F_MSGID || field == F_ACKFOR) {
        if (!isUInt) return ParseError::BadValue;
        (field == F_MSGID ? msgId : ackFor) = num;
      }
    } else if (matchLiteral(c, "true") || matchLiteral(c, "false") || matchLiteral(c, "null")) {
      if (field) return ParseError::BadValue;
    } else {
      return ParseError::Malformed;
    }
    firstMember = false;
    skipWs(c);
    if (c.peek() == ',') {
      ++c.p;
      skipWs(c);
      if (c.peek() == '}') return ParseError::Malformed;
      firstMember = true;  // a member must follow
      continue;
    }
    if (c.peek() == '}') { ++c.p; break; }
    return ParseError::Malformed;
  }
  skipWs(c);
  // Some senders append '\0'; anything else after the object is rejected.
  while (!c.atEnd() && *c.p == '\0') ++c.p;
  if (!c.atEnd()) return ParseError::Malformed;

  if (!(seen & F_TYPE)) return ParseError::MissingField;
  if (!strcmp(type, "TEXT")) {
    out.type = MsgType::Text;
    if (!(seen & F_MSGID) || !(seen & F_TEXT) || !(seen & (F_SOURCE | F_DEVICE)))
      return ParseError::MissingField;
    if (out.text[0] == '\0') return ParseError::BadValue;
    strcpy(out.sender, (seen & F_SOURCE) ? source : device);
    out.id = msgId;
  } else if (!strcmp(type, "ACK")) {
    out.type = MsgType::Ack;
    if (!(seen & F_ACKFOR) || !(seen & (F_SOURCE | F_DEVICE))) return ParseError::MissingField;
    strcpy(out.sender, (seen & F_DEVICE) ? device : source);
    out.id = ackFor;
    out.text[0] = '\0';
  } else {
    out.type = MsgType::Unknown;
    return ParseError::UnknownType;
  }
  if (out.sender[0] == '\0') return ParseError::BadValue;
  return ParseError::Ok;
}

bool addressedToUs(const IncomingMessage& m, const char* ownId) {
  return m.dst[0] == '\0' || strcmp(m.dst, ownId) == 0 || strcmp(m.dst, "*") == 0;
}

bool sourceAllowed(const char* sender) {
  for (size_t i = 0; i < cfg::ALLOWED_TEXT_SOURCES_COUNT; ++i)
    if (strcmp(sender, cfg::ALLOWED_TEXT_SOURCES[i]) == 0) return true;
  return false;
}

}  // namespace proto

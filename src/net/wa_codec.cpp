#include "wa_codec.h"

#include <string.h>

#include "../communication/message_protocol.h"
#include "../communication/telemetry_codec.h"

namespace wa {

const char* linkStateName(LinkState s) {
  switch (s) {
    case LinkState::Offline: return "offline";
    case LinkState::Idle: return "idle";
    case LinkState::Connecting: return "connecting";
    case LinkState::Qr: return "qr";
    case LinkState::Code: return "code";
    case LinkState::Open: return "open";
    default: return "unknown";
  }
}

// ===========================================================================
// Flat JSON object reader: {"key":"string"|number|true|false|null, ...}
// ===========================================================================
namespace {

struct Field {
  const char* key;
  char*       str = nullptr;      // decoded string value (NUL-terminated, cut at cap)
  size_t      strCap = 0;
  uint32_t*   num = nullptr;      // non-negative integer value
  const char** raw = nullptr;     // undecoded string value (no escapes allowed)
  size_t*     rawLen = nullptr;
  bool        seen = false;
};

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

bool hex4(Cur& c, uint32_t& cp) {
  if (c.end - c.p < 4) return false;
  cp = 0;
  for (int i = 0; i < 4; ++i) {
    const int h = hexVal(*c.p++);
    if (h < 0) return false;
    cp = (cp << 4) | static_cast<uint32_t>(h);
  }
  return true;
}

// Appends a code point as UTF-8 if the whole sequence fits; false = full.
bool putUtf8(char* out, size_t cap, size_t& n, uint32_t cp) {
  char t[4];
  size_t k;
  if (cp < 0x80) { t[0] = static_cast<char>(cp); k = 1; }
  else if (cp < 0x800) { t[0] = static_cast<char>(0xC0 | (cp >> 6)); t[1] = static_cast<char>(0x80 | (cp & 0x3F)); k = 2; }
  else if (cp < 0x10000) {
    t[0] = static_cast<char>(0xE0 | (cp >> 12)); t[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    t[2] = static_cast<char>(0x80 | (cp & 0x3F)); k = 3;
  } else {
    t[0] = static_cast<char>(0xF0 | (cp >> 18)); t[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    t[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); t[3] = static_cast<char>(0x80 | (cp & 0x3F)); k = 4;
  }
  if (n + k > cap) return false;
  memcpy(out + n, t, k);
  n += k;
  return true;
}

// Bytes of the UTF-8 sequence that starts with `lead` (1 for anything odd).
size_t utf8Len(unsigned char lead) {
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

// Reads a JSON string. `out` (cap bytes + NUL) gets the decoded text, cut at a
// character boundary when it does not fit; out == nullptr skips the value.
// `raw`/`rawLen` get the span between the quotes. False = malformed.
bool readString(Cur& c, char* out, size_t cap, const char** raw, size_t* rawLen) {
  if (c.peek() != '"') return false;
  ++c.p;
  const char* start = c.p;
  size_t n = 0;
  bool full = false;
  for (;;) {
    if (c.atEnd()) return false;
    const unsigned char ch = static_cast<unsigned char>(*c.p++);
    if (ch == '"') break;
    if (ch < 0x20) return false;  // raw control character
    if (ch != '\\') {
      if (!out || full) continue;
      // keep a multi-byte character whole
      const size_t k = utf8Len(ch);
      if (n + k > cap) { full = true; continue; }
      out[n++] = static_cast<char>(ch);
      continue;
    }
    if (c.atEnd()) return false;
    const char e = *c.p++;
    uint32_t cp;
    switch (e) {
      case '"': cp = '"'; break;
      case '\\': cp = '\\'; break;
      case '/': cp = '/'; break;
      case 'b': case 'f': case 'n': case 'r': case 't': cp = ' '; break;  // whitespace
      case 'u': {
        if (!hex4(c, cp)) return false;
        if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate pair -> one code point
          uint32_t lo = 0;
          if (c.end - c.p >= 6 && c.p[0] == '\\' && c.p[1] == 'u') {
            Cur t{c.p + 2, c.end};
            if (hex4(t, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              c.p = t.p;
            } else cp = '?';
          } else cp = '?';
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = '?';
        if (cp < 0x20) cp = ' ';
        break;
      }
      default: return false;
    }
    if (out && !full && !putUtf8(out, cap, n, cp)) full = true;
  }
  if (out) out[n] = '\0';
  if (raw) { *raw = start; *rawLen = static_cast<size_t>(c.p - 1 - start); }
  return true;
}

// Reads a number / true / false / null token. isUint: a non-negative integer
// that fits in 32 bits.
bool readScalar(Cur& c, bool& isUint, uint32_t& value) {
  const char* start = c.p;
  while (!c.atEnd() && *c.p != ',' && *c.p != '}' && *c.p != ' ' && *c.p != '\t' && *c.p != '\n' && *c.p != '\r') {
    const char ch = *c.p;
    if (ch == '{' || ch == '[' || ch == '"') return false;  // nested / garbage
    ++c.p;
  }
  const size_t n = static_cast<size_t>(c.p - start);
  if (n == 0 || n > 24) return false;
  isUint = true;
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i) {
    if (start[i] < '0' || start[i] > '9') { isUint = false; break; }
    v = v * 10 + static_cast<uint64_t>(start[i] - '0');
    if (v > 0xFFFFFFFFull) { isUint = false; break; }
  }
  value = isUint ? static_cast<uint32_t>(v) : 0;
  return true;
}

bool parseFlat(const char* json, size_t len, Field* fields, size_t count) {
  if (!json || len == 0) return false;
  Cur c{json, json + len};
  skipWs(c);
  if (c.peek() != '{') return false;
  ++c.p;
  skipWs(c);
  if (c.peek() == '}') { ++c.p; }
  else {
    for (;;) {
      skipWs(c);
      char key[16];
      if (!readString(c, key, sizeof key - 1, nullptr, nullptr)) return false;
      skipWs(c);
      if (c.peek() != ':') return false;
      ++c.p;
      skipWs(c);
      Field* f = nullptr;
      for (size_t i = 0; i < count; ++i)
        if (!strcmp(key, fields[i].key)) { f = &fields[i]; break; }
      if (f && f->seen) return false;  // duplicate key
      const char v = c.peek();
      if (v == '{' || v == '[') return false;  // not flat
      if (v == '"') {
        if (f && f->num) return false;  // a number was expected
        if (!readString(c, f ? f->str : nullptr, f ? f->strCap : 0, f ? f->raw : nullptr,
                        f ? f->rawLen : nullptr))
          return false;
        if (f) f->seen = true;
      } else {
        bool isUint = false;
        uint32_t num = 0;
        if (!readScalar(c, isUint, num)) return false;
        if (f) {
          if (!f->num || !isUint) return false;  // a string / unsigned was expected
          *f->num = num;
          f->seen = true;
        }
      }
      skipWs(c);
      if (c.peek() == ',') { ++c.p; continue; }
      if (c.peek() == '}') { ++c.p; break; }
      return false;
    }
  }
  skipWs(c);
  while (!c.atEnd() && *c.p == '\0') ++c.p;  // a sender may append NUL
  return c.atEnd();
}

}  // namespace

// ===========================================================================
// Payloads
// ===========================================================================
bool parseLink(const char* json, size_t len, Link& out) {
  out = Link();
  char state[12] = {};
  uint32_t ttl = 0;
  char code[CODE_LEN + 2] = {};  // one spare character: a longer code is seen, not cut
  Field f[4];
  f[0].key = "state"; f[0].str = state; f[0].strCap = sizeof state - 1;
  f[1].key = "ttl"; f[1].num = &ttl;
  f[2].key = "code"; f[2].str = code; f[2].strCap = sizeof code - 1;
  f[3].key = "me"; f[3].str = out.me; f[3].strCap = sizeof out.me - 1;
  if (!parseFlat(json, len, f, 4) || !f[0].seen) { out = Link(); return false; }

  if (!strcmp(state, "offline")) out.state = LinkState::Offline;
  else if (!strcmp(state, "idle")) out.state = LinkState::Idle;
  else if (!strcmp(state, "connecting")) out.state = LinkState::Connecting;
  else if (!strcmp(state, "qr")) out.state = LinkState::Qr;
  else if (!strcmp(state, "code")) out.state = LinkState::Code;
  else if (!strcmp(state, "open")) out.state = LinkState::Open;
  else { out = Link(); return false; }

  if (out.state != LinkState::Open) out.me[0] = '\0';
  if (out.state == LinkState::Code) {
    // Exactly 8 digits / capital letters, or the message is refused: a wrong
    // code on the display would only make the user type something useless.
    bool ok = strlen(code) == CODE_LEN;
    for (size_t i = 0; ok && i < CODE_LEN; ++i)
      ok = (code[i] >= '0' && code[i] <= '9') || (code[i] >= 'A' && code[i] <= 'Z');
    if (!ok) { out = Link(); return false; }
    memcpy(out.code, code, CODE_LEN + 1);
    out.ttlS = static_cast<uint16_t>(ttl > 3600 ? 3600 : ttl);
  }
  return true;
}

bool parseInbox(const char* json, size_t len, Inbox& out) {
  out = Inbox();
  Field f[3];
  f[0].key = "id"; f[0].num = &out.id;
  f[1].key = "from"; f[1].str = out.from; f[1].strCap = sizeof out.from - 1;
  f[2].key = "text"; f[2].str = out.text; f[2].strCap = sizeof out.text - 1;
  if (!parseFlat(json, len, f, 3) || !f[0].seen || !f[2].seen || out.text[0] == '\0') {
    out = Inbox();
    return false;
  }
  return true;
}

size_t encodeNodeState(const NodeState& s, char* buf, size_t cap) {
  proto::JsonWriter w(buf, cap);
  w.unum("boot", s.boot);
  w.unum("up", s.uptimeS);
  char lv[CATEGORY_COUNT + 1];
  tlm::levelLetters(s.levels, lv);
  w.str("lv", lv);
  w.str("ov", severityName(s.overall));
  w.str("worst", categoryName(s.worst));
  w.boolean("relay", s.relayOn);
  w.unum("flags", s.systemFlags);
  if (s.envValid) {
    w.flt("temp", s.temperature, 1);
    w.flt("rh", s.humidity, 0);
  }
  if (s.airValid) w.flt("gas", s.gasVolts, 2);
  if (s.simulated) w.boolean("sim", true);
  return w.finish();
}

uint32_t nodeStateSignature(const NodeState& s) {
  uint32_t sig = s.relayOn ? 1u : 0u;
  for (uint8_t i = 0; i < CATEGORY_COUNT; ++i) sig = (sig << 2) | (static_cast<uint32_t>(s.levels[i]) & 3u);
  return (sig << 16) | s.systemFlags | 0x80000000u;  // never 0: 0 = "nothing published yet"
}

}  // namespace wa

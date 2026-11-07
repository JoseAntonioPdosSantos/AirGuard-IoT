#include "text_utils.h"

#include <string.h>

namespace text {

static char transliterate(uint32_t cp) {
  if (cp >= 0x20 && cp < 0x7F) return static_cast<char>(cp);
  if (cp < 0x20 || cp == 0x7F) return ' ';
  switch (cp) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: return 'A';
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xAA: return 'a';
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'E';
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'I';
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 'O';
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xBA: return 'o';
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'U';
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
    case 0xC7: return 'C';
    case 0xE7: return 'c';
    case 0xD1: return 'N';
    case 0xF1: return 'n';
    case 0xB0: return 'o';   // degree sign
    case 0xA0: return ' ';   // nbsp
    case 0x2018: case 0x2019: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x2013: case 0x2014: return '-';
    case 0x2026: return '.';
    default: return '?';
  }
}

size_t toDisplayAscii(const char* s, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  size_t n = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s ? s : "");
  while (*p && n + 1 < cap) {
    uint32_t cp;
    int extra;
    if (*p < 0x80) { cp = *p; extra = 0; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
    else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
    else { cp = '?'; extra = -1; }  // stray continuation byte
    ++p;
    if (extra < 0) { /* keep '?' */ }
    for (int i = 0; i < extra; ++i) {
      if ((*p & 0xC0) != 0x80) { cp = '?'; extra = 0; break; }  // truncated sequence
      cp = (cp << 6) | (*p & 0x3F);
      ++p;
    }
    char c = (extra < 0) ? '?' : transliterate(cp);
    if (c == ' ' && (n == 0 || out[n - 1] == ' ')) continue;  // collapse spaces
    out[n++] = c;
  }
  while (n > 0 && out[n - 1] == ' ') --n;
  out[n] = '\0';
  return n;
}

uint8_t wrap(const char* in, uint8_t cols, char lines[][22], uint8_t maxLines) {
  if (cols == 0 || cols > 21) cols = 21;
  uint8_t lineCount = 0;
  char cur[22] = {};
  uint8_t curLen = 0;

  auto flush = [&]() {
    if (lineCount < maxLines) { memcpy(lines[lineCount], cur, curLen); lines[lineCount][curLen] = '\0'; }
    ++lineCount;
    curLen = 0;
    cur[0] = '\0';
  };

  const char* p = in ? in : "";
  while (*p) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char* w = p;
    while (*p && *p != ' ') ++p;
    size_t wlen = static_cast<size_t>(p - w);
    // Hard-split very long words.
    while (wlen > 0) {
      const uint8_t needSpace = curLen ? 1 : 0;
      if (curLen + needSpace + wlen <= cols) {
        if (needSpace) cur[curLen++] = ' ';
        memcpy(cur + curLen, w, wlen);
        curLen += static_cast<uint8_t>(wlen);
        wlen = 0;
      } else if (curLen == 0) {
        memcpy(cur, w, cols);
        curLen = cols;
        w += cols;
        wlen -= cols;
        flush();
      } else {
        flush();
      }
    }
  }
  if (curLen) flush();
  return lineCount;
}

}  // namespace text

// AirGuard IoT - text sanitising and word-wrapping for the OLED (pure, tested).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace text {

// Converts UTF-8 to printable ASCII for the GFX 5x7 font:
// Portuguese accents are transliterated (ç->c, ã->a, É->E...), other
// non-ASCII code points become '?', control chars become ' ', runs of
// spaces are collapsed. Always NUL-terminates. Returns output length.
size_t toDisplayAscii(const char* utf8, char* out, size_t cap);

// Word-wraps `in` into lines of at most `cols` chars. Words longer than a
// line are hard-split. Writes up to maxLines lines into lines[i] (each must
// hold cols+1 bytes). Returns the number of lines needed in total (may be
// larger than maxLines).
uint8_t wrap(const char* in, uint8_t cols, char lines[][22], uint8_t maxLines);

}  // namespace text

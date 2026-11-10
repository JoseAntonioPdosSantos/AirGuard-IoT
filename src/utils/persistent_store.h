// AirGuard IoT - persisted state in flash (Arduino EEPROM emulation).
// Stored: boot counter (unique event_id) and the HIGH-category mask.
// Writes are rare (boot, entering/leaving HIGH) to limit flash wear.
#pragma once
#include <stdint.h>

struct PersistedState {
  uint32_t magic;
  uint16_t version;
  uint16_t bootCount;
  uint8_t  highMask;      // categories that were HIGH (bit = Category)
  uint8_t  reserved[3];
  uint32_t crc;
};

namespace store {
bool begin();                       // loads or initialises defaults
PersistedState& state();
bool save();                        // CRC + commit (skips if unchanged)
uint16_t incrementBoot();
void setHighMask(uint8_t mask);
}  // namespace store

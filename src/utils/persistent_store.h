// AirGuard IoT - persisted state in flash (arduino-pico EEPROM emulation).
// Stored: boot counter (unique event_id), MQ-135 R0, HIGH-category mask.
// Writes are rare (boot, calibration, entering/leaving HIGH) to limit wear.
#pragma once
#include <stdint.h>

struct PersistedState {
  uint32_t magic;
  uint16_t version;
  uint16_t bootCount;
  float    r0;
  uint8_t  r0Valid;
  uint8_t  highMask;      // categories that were HIGH (bit = Category)
  uint16_t reserved;
  uint32_t crc;
};

namespace store {
bool begin();                       // loads or initialises defaults
PersistedState& state();
bool save();                        // CRC + commit (skips if unchanged)
uint16_t incrementBoot();
void setR0(float r0);
void setHighMask(uint8_t mask);
}  // namespace store

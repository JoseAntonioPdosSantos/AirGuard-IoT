#include "persistent_store.h"

#include <EEPROM.h>
#include <stddef.h>
#include <string.h>

#include "../config/config.h"
#include "logger.h"

namespace {
PersistedState g_state;
PersistedState g_saved;

uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    c ^= d[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

uint32_t crcOf(const PersistedState& s) {
  return crc32(reinterpret_cast<const uint8_t*>(&s), offsetof(PersistedState, crc));
}

void defaults() {
  memset(&g_state, 0, sizeof g_state);
  g_state.magic = cfg::STORE_MAGIC;
  g_state.version = 1;
}
}  // namespace

namespace store {

bool begin() {
  static_assert(sizeof(PersistedState) <= cfg::STORE_SIZE, "store too small");
  EEPROM.begin(cfg::STORE_SIZE);
  EEPROM.get(0, g_state);
  if (g_state.magic != cfg::STORE_MAGIC || g_state.version != 1 || g_state.crc != crcOf(g_state)) {
    LOGW("STORE", "no valid persisted state, using defaults");
    defaults();
    g_saved = g_state;
    return false;
  }
  g_saved = g_state;
  return true;
}

PersistedState& state() { return g_state; }

bool save() {
  g_state.crc = crcOf(g_state);
  if (memcmp(&g_state, &g_saved, sizeof g_state) == 0) return true;
  EEPROM.put(0, g_state);
  // commit() erases/programs one flash sector; it briefly stalls both cores.
  const bool ok = EEPROM.commit();
  if (ok) g_saved = g_state;
  else LOGE("STORE", "flash commit failed");
  return ok;
}

uint16_t incrementBoot() {
  if (++g_state.bootCount == 0) g_state.bootCount = 1;
  save();
  return g_state.bootCount;
}

void setR0(float r0) {
  g_state.r0 = r0;
  g_state.r0Valid = 1;
  save();
}

void setHighMask(uint8_t mask) {
  if (g_state.highMask == mask) return;
  g_state.highMask = mask;
  save();
}

}  // namespace store

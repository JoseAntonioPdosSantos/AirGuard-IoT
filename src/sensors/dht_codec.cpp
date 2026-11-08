#include "dht_codec.h"

namespace dht {

bool decode(const uint8_t f[5], Model model, float& t, float& h) {
  const uint8_t sum = static_cast<uint8_t>(f[0] + f[1] + f[2] + f[3]);
  if (sum != f[4]) return false;
  if ((f[0] | f[1] | f[2] | f[3] | f[4]) == 0) return false;  // no data at all
  if (model == Model::DHT11) {
    h = f[0] + f[1] * 0.1f;
    t = f[2] + (f[3] & 0x0F) * 0.1f;
    if (f[3] & 0x80) t = -t;
  } else {
    h = ((f[0] << 8) | f[1]) * 0.1f;
    t = (((f[2] & 0x7F) << 8) | f[3]) * 0.1f;
    if (f[2] & 0x80) t = -t;
  }
  return true;
}

}  // namespace dht

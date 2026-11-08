// DHT11 / DHT22 frame decoding (src/sensors/dht_codec).
#include "../../src/sensors/dht_codec.h"
#include "test_framework.h"

TEST(dht11_decodes_frame) {
  const uint8_t f[5] = {55, 0, 24, 3, 82};  // 55 %RH, 24.3 C
  float t = 0, h = 0;
  CHECK(dht::decode(f, dht::Model::DHT11, t, h));
  CHECK_NEAR(h, 55.0, 1e-4);
  CHECK_NEAR(t, 24.3, 1e-4);
}

TEST(dht11_negative_temperature_bit) {
  const uint8_t f[5] = {40, 0, 2, 0x85, static_cast<uint8_t>(40 + 2 + 0x85)};
  float t = 0, h = 0;
  CHECK(dht::decode(f, dht::Model::DHT11, t, h));
  CHECK_NEAR(t, -2.5, 1e-4);
}

TEST(dht22_decodes_frame_and_sign) {
  // 65.2 %RH = 652 = 0x028C ; -10.1 C = 101 = 0x0065 | 0x8000
  const uint8_t f[5] = {0x02, 0x8C, 0x80, 0x65, static_cast<uint8_t>(0x02 + 0x8C + 0x80 + 0x65)};
  float t = 0, h = 0;
  CHECK(dht::decode(f, dht::Model::DHT22, t, h));
  CHECK_NEAR(h, 65.2, 1e-4);
  CHECK_NEAR(t, -10.1, 1e-4);
}

TEST(dht_rejects_bad_checksum_and_empty_frame) {
  const uint8_t bad[5] = {55, 0, 24, 3, 83};
  const uint8_t zero[5] = {0, 0, 0, 0, 0};  // line stuck LOW: checksum "matches"
  float t = 1, h = 1;
  CHECK(!dht::decode(bad, dht::Model::DHT11, t, h));
  CHECK(!dht::decode(zero, dht::Model::DHT11, t, h));
}

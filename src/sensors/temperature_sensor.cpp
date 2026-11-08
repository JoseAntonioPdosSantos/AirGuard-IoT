#include "temperature_sensor.h"

#include <Arduino.h>
#include <math.h>

#include "../config/hardware_pins.h"
#include "../utils/logger.h"

#if defined(AIRGUARD_ENV_SENSOR_AHT20)
// ---------------------------------------------------------------------------
#include <Adafruit_AHTX0.h>
#include <Wire.h>
static Adafruit_AHTX0 g_aht;
static bool g_ahtOk = false;

bool TemperatureSensor::begin() {
  g_ahtOk = g_aht.begin(&Wire);  // Wire configured by the app (board::i2cBegin)
  if (!g_ahtOk) LOGE("ENV", "AHT20 not found at 0x38");
  return g_ahtOk;
}

EnvReading TemperatureSensor::read() {
  if (!g_ahtOk) { g_ahtOk = g_aht.begin(&Wire); if (!g_ahtOk) return {false, NAN, NAN}; }
  sensors_event_t h, t;
  // Note: the AHT20 conversion takes ~80 ms inside the library.
  if (!g_aht.getEvent(&h, &t)) return {false, NAN, NAN};
  return {true, t.temperature, h.relative_humidity};
}

const char* TemperatureSensor::model() const { return "AHT20"; }

#else
// ---------------------------------------------------------------------------
// Own bit-banged DHTxx driver. The Adafruit DHT library was replaced because,
// with a bad/unstable data line, its read loop keeps interrupts disabled for
// seconds: on the ESP32 that trips the 300 ms interrupt watchdog (observed on
// the bench: reboot loop). Here every edge has a 100 us timeout, so the
// interrupt-free window is at most ~8.5 ms even with a broken sensor.
#include "../platform/board.h"
#include "dht_codec.h"

#if defined(AIRGUARD_ENV_SENSOR_DHT11)
static constexpr dht::Model kDht = dht::Model::DHT11;
static constexpr uint32_t kStartLowMs = 20;  // DHT11: host start signal >= 18 ms
static const char* const kModel = "DHT11";
#else
static constexpr dht::Model kDht = dht::Model::DHT22;
static constexpr uint32_t kStartLowMs = 2;   // DHT22: >= 1 ms
static const char* const kModel = "DHT22";
#endif

namespace {

constexpr uint32_t EDGE_TIMEOUT_US = 100;

// Time (us) the line stays at `level`, or -1 if it does not change in time.
int32_t levelTime(uint8_t pin, int level) {
  const uint32_t t0 = micros();
  while (digitalRead(pin) == level) {
    if ((uint32_t)(micros() - t0) > EDGE_TIMEOUT_US) return -1;
  }
  return static_cast<int32_t>(micros() - t0);
}

enum class ReadError : uint8_t { None, NoResponse, Timeout, Checksum };
ReadError g_lastError = ReadError::None;

}  // namespace

bool TemperatureSensor::begin() {
  pinMode(pins::DHT_DATA, INPUT_PULLUP);  // idle HIGH (the module also has a pull-up)
  return true;                            // no presence check; first read() validates it
}

EnvReading TemperatureSensor::read() {
  const uint8_t pin = pins::DHT_DATA;
  uint8_t frame[5] = {0, 0, 0, 0, 0};

  // Host start signal: hold LOW, then release (blocking 2-20 ms, as before).
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(kStartLowMs);
  pinMode(pin, INPUT_PULLUP);  // released before the critical section (pinMode may log)

  ReadError err = ReadError::None;
  board::criticalBegin();
  // Sensor answer: (HIGH 20-40 us) -> LOW 80 us -> HIGH 80 us.
  if (levelTime(pin, HIGH) < 0 || levelTime(pin, LOW) < 0 || levelTime(pin, HIGH) < 0) {
    err = ReadError::NoResponse;
  } else {
    // 40 bits: LOW ~50 us, then HIGH ~26 us (0) or ~70 us (1).
    for (uint8_t i = 0; i < 40; ++i) {
      const int32_t low = levelTime(pin, LOW);
      const int32_t high = levelTime(pin, HIGH);
      if (low < 0 || high < 0) { err = ReadError::Timeout; break; }
      frame[i / 8] = static_cast<uint8_t>((frame[i / 8] << 1) | (high > low ? 1 : 0));
    }
  }
  board::criticalEnd();

  float t = NAN, h = NAN;
  if (err == ReadError::None && !dht::decode(frame, kDht, t, h)) err = ReadError::Checksum;
  if (err != g_lastError) {  // log only changes: no flood with a missing sensor
    static const char* const names[] = {"ok", "no response (check DATA/VCC/GND)",
                                        "timeout in the middle of the frame",
                                        "checksum error"};
    if (err == ReadError::None) LOGI("ENV", "%s read %s", kModel, names[0]);
    else LOGW("ENV", "%s read failed: %s", kModel, names[static_cast<uint8_t>(err)]);
    g_lastError = err;
  }
  if (err != ReadError::None) return {false, NAN, NAN};
  return {true, t, h};
}

const char* TemperatureSensor::model() const { return kModel; }
#endif

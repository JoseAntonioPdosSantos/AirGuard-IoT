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
  g_ahtOk = g_aht.begin(&Wire);  // Wire configured by the app (I2C0, GP4/GP5)
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
#include <DHT.h>
#if defined(AIRGUARD_ENV_SENSOR_DHT11)
static DHT g_dht(pins::DHT_DATA, DHT11);
static const char* const kModel = "DHT11";
#else
static DHT g_dht(pins::DHT_DATA, DHT22);
static const char* const kModel = "DHT22";
#endif

bool TemperatureSensor::begin() {
  g_dht.begin();
  return true;  // DHTxx has no presence check; first read() validates it
}

EnvReading TemperatureSensor::read() {
  // force=true: we already rate-limit to >= 2 s. Takes ~5 ms (bit-banged).
  const float t = g_dht.readTemperature(false, true);
  const float h = g_dht.readHumidity(false);
  const bool ok = !isnan(t) && !isnan(h);
  return {ok, t, h};
}

const char* TemperatureSensor::model() const { return kModel; }
#endif

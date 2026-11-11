#include "uplink.h"

#include <Arduino.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>

#include "../config/config.h"
#include "../utils/logger.h"
#include "uplink_queue.h"

#if __has_include("../config/net_secrets.h")
#include "../config/net_secrets.h"
#endif
#ifndef NET_WIFI_SSID
#define NET_WIFI_SSID ""
#endif
#ifndef NET_WIFI_PASS
#define NET_WIFI_PASS ""
#endif
#ifndef NET_MQTT_BROKER
#define NET_MQTT_BROKER ""
#endif
#ifndef NET_MQTT_USER
#define NET_MQTT_USER ""
#endif
#ifndef NET_MQTT_PASS
#define NET_MQTT_PASS ""
#endif

namespace {

constexpr const char* NVS_NS = "airguard-net";
constexpr const char* NVS_KEYS[] = {"ssid", "pass", "broker", "muser", "mpass"};

struct Config {
  char ssid[33] = {};
  char pass[65] = {};
  char broker[64] = {};  // host or host:port
  char user[33] = {};
  char mpass[65] = {};
};

Config            g_cfg;
SemaphoreHandle_t g_lock = nullptr;  // queue + config
uint8_t           g_store[cfg::UPLINK_QUEUE_BYTES];
UplinkQueue       g_queue(g_store, sizeof g_store);
volatile bool     g_reconnect = false;

// status (written by the uplink task, read by the console)
volatile uint32_t g_sent = 0, g_pubs = 0, g_errors = 0, g_lastOkMs = 0;
volatile int      g_mqttState = MQTT_DISCONNECTED;  // PubSubClient::state()
volatile uint8_t  g_lastReason = 0;                 // last Wi-Fi disconnect reason (wifi_err_reason_t)

char* field(Config& c, uplink::Setting s) {
  switch (s) {
    case uplink::Setting::Ssid: return c.ssid;
    case uplink::Setting::Password: return c.pass;
    case uplink::Setting::Broker: return c.broker;
    case uplink::Setting::User: return c.user;
    default: return c.mpass;
  }
}
size_t fieldCap(uplink::Setting s) {
  switch (s) {
    case uplink::Setting::Ssid: return sizeof Config::ssid;
    case uplink::Setting::Password: return sizeof Config::pass;
    case uplink::Setting::Broker: return sizeof Config::broker;
    case uplink::Setting::User: return sizeof Config::user;
    default: return sizeof Config::mpass;
  }
}

void copyStr(char* dst, size_t cap, const char* src) {
  strncpy(dst, src ? src : "", cap - 1);
  dst[cap - 1] = '\0';
}

void loadConfig() {
  copyStr(g_cfg.ssid, sizeof g_cfg.ssid, NET_WIFI_SSID);
  copyStr(g_cfg.pass, sizeof g_cfg.pass, NET_WIFI_PASS);
  copyStr(g_cfg.broker, sizeof g_cfg.broker, NET_MQTT_BROKER[0] ? NET_MQTT_BROKER : cfg::UPLINK_DEFAULT_BROKER);
  copyStr(g_cfg.user, sizeof g_cfg.user, NET_MQTT_USER);
  copyStr(g_cfg.mpass, sizeof g_cfg.mpass, NET_MQTT_PASS);
  Preferences p;
  if (!p.begin(NVS_NS, true)) return;  // namespace not created yet: defaults
  for (uint8_t i = 0; i < 5; ++i) {
    const auto s = static_cast<uplink::Setting>(i);
    if (p.isKey(NVS_KEYS[i])) p.getString(NVS_KEYS[i], field(g_cfg, s), fieldCap(s));
  }
  p.end();
}

void connectWifi(const Config& c) {
  WiFi.disconnect(false, false);
  if (!c.ssid[0]) return;
  WiFi.begin(c.ssid, c.pass);
}

// "host" or "host:port" -> host (in out) + port.
uint16_t splitBroker(const char* broker, char* host, size_t cap) {
  copyStr(host, cap, broker);
  char* colon = strrchr(host, ':');
  if (!colon) return cfg::UPLINK_MQTT_PORT;
  *colon = '\0';
  const long port = strtol(colon + 1, nullptr, 10);
  return (port > 0 && port < 65536) ? static_cast<uint16_t>(port) : cfg::UPLINK_MQTT_PORT;
}

const char* mqttStateText(int s) {
  switch (s) {
    case MQTT_CONNECTION_TIMEOUT: return "timeout";
    case MQTT_CONNECTION_LOST: return "connection lost";
    case MQTT_CONNECT_FAILED: return "TCP connect failed (broker address/port?)";
    case MQTT_DISCONNECTED: return "disconnected";
    case MQTT_CONNECTED: return "connected";
    case MQTT_CONNECT_BAD_PROTOCOL: return "bad protocol";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "client id rejected";
    case MQTT_CONNECT_UNAVAILABLE: return "broker unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "bad user/password";
    case MQTT_CONNECT_UNAUTHORIZED: return "not authorized";
    default: return "?";
  }
}

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
  const uint8_t r = info.wifi_sta_disconnected.reason;
  if (r != g_lastReason) LOGW("NET", "Wi-Fi disconnect reason %u", r);
  g_lastReason = r;
}

// Payload: {"now":<millis at send>,"records":[...]}. The server places each
// record in time with now - ms (the node has no clock).
char g_body[cfg::UPLINK_BATCH_BYTES];
char g_topic[64];
char g_host[64];  // PubSubClient keeps a pointer to it

void task(void*) {
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("airguard-node");
  snprintf(g_topic, sizeof g_topic, "airguard/%s/telemetry", cfg::DEVICE_ID);
  char clientId[40];
  snprintf(clientId, sizeof clientId, "airguard-%s", cfg::DEVICE_ID);

  Config c;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  c = g_cfg;
  xSemaphoreGive(g_lock);
  connectWifi(c);
  bool wasUp = false;
  uint32_t lastTry = 0, lastConnect = 0, downSince = millis();
  WiFiClient net;
  PubSubClient mqtt(net);
  mqtt.setKeepAlive(cfg::UPLINK_MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(cfg::UPLINK_MQTT_TIMEOUT_S);

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (g_reconnect) {
      g_reconnect = false;
      xSemaphoreTake(g_lock, portMAX_DELAY);
      c = g_cfg;
      xSemaphoreGive(g_lock);
      mqtt.disconnect();
      connectWifi(c);
      downSince = millis();
      lastConnect = 0;
    }
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up != wasUp) {
      wasUp = up;
      if (up) LOGI("NET", "Wi-Fi '%s' connected, IP %s, RSSI %d dBm", c.ssid,
                   WiFi.localIP().toString().c_str(), WiFi.RSSI());
      else { LOGW("NET", "Wi-Fi disconnected"); downSince = millis(); mqtt.disconnect(); }
    }
    if (!up) {
      // auto-reconnect normally handles it; kick it if it gave up
      if (c.ssid[0] && (uint32_t)(millis() - downSince) > 30000) { connectWifi(c); downSince = millis(); }
      continue;
    }
    if (!c.broker[0]) continue;

    if (!mqtt.connected()) {
      if (lastConnect && (uint32_t)(millis() - lastConnect) < cfg::UPLINK_MQTT_RETRY_MS) continue;
      lastConnect = millis();
      const uint16_t port = splitBroker(c.broker, g_host, sizeof g_host);
      mqtt.setServer(g_host, port);
      const bool ok = mqtt.connect(clientId, c.user[0] ? c.user : nullptr, c.mpass[0] ? c.mpass : nullptr);
      const int st = mqtt.state();
      if (st != g_mqttState || ok)
        LOGI("NET", "MQTT %s:%u: %s", g_host, port, ok ? "connected" : mqttStateText(st));
      g_mqttState = st;
      if (!ok) { ++g_errors; continue; }
    }
    mqtt.loop();  // keep-alive (PINGREQ / PINGRESP)
    g_mqttState = mqtt.state();

    if ((uint32_t)(millis() - lastTry) < cfg::UPLINK_PERIOD_MS) continue;
    lastTry = millis();

    uint32_t lastSeq = 0;
    size_t count = 0, n;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    const int head = snprintf(g_body, sizeof g_body, "{\"now\":%lu,\"records\":[",
                              static_cast<unsigned long>(millis()));
    n = g_queue.peekBatch(g_body + head, sizeof g_body - head - 3, cfg::UPLINK_BATCH_RECORDS, lastSeq, count);
    xSemaphoreGive(g_lock);
    if (!count) continue;
    n += head;
    g_body[n++] = ']';
    g_body[n++] = '}';

    // QoS 0, streamed straight from g_body (no second copy in PubSubClient's
    // buffer). Success = the whole packet was written to the TCP connection.
    ++g_pubs;
    const bool ok = mqtt.beginPublish(g_topic, n, false) &&
                    mqtt.write(reinterpret_cast<const uint8_t*>(g_body), n) == n && mqtt.endPublish();
    if (ok) {
      xSemaphoreTake(g_lock, portMAX_DELAY);
      g_queue.confirm(lastSeq);
      xSemaphoreGive(g_lock);
      g_sent += count;
      g_lastOkMs = millis();
    } else {
      ++g_errors;
      if (g_errors % 20 == 1) LOGW("NET", "MQTT publish to %s failed (%u B)", g_topic, static_cast<unsigned>(n));
      mqtt.disconnect();  // half-written packet: start a clean connection
    }
  }
}

}  // namespace

namespace uplink {

void begin() {
  g_lock = xSemaphoreCreateMutex();
  loadConfig();
  LOGI("NET", "uplink: Wi-Fi '%s'%s, MQTT %s, user %s", g_cfg.ssid[0] ? g_cfg.ssid : "(none)",
       g_cfg.ssid[0] ? "" : " - set with 'net ssid <name>'", g_cfg.broker[0] ? g_cfg.broker : "(none)",
       g_cfg.user[0] ? g_cfg.user : "(none)");
  xTaskCreatePinnedToCore(task, "uplink", cfg::UPLINK_TASK_STACK, nullptr, 1, nullptr, 0);
}

void push(const char* json, size_t len) {
  if (!g_lock) return;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;  // never stall the loop
  g_queue.push(json, len);
  xSemaphoreGive(g_lock);
}

bool connected() { return g_mqttState == MQTT_CONNECTED; }

bool set(Setting s, const char* v) {
  if (!g_lock) return false;
  const uint8_t i = static_cast<uint8_t>(s);
  if (i > 4 || strlen(v) >= fieldCap(s)) return false;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  copyStr(field(g_cfg, s), fieldCap(s), v);
  xSemaphoreGive(g_lock);
  Preferences p;
  bool ok = p.begin(NVS_NS, false);
  if (ok) {
    ok = p.putString(NVS_KEYS[i], v) == strlen(v) || !v[0];
    p.end();
  }
  g_reconnect = true;
  return ok;
}

void printStatus() {
  Config c;
  size_t queued, bytes;
  uint32_t dropped;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  c = g_cfg;
  queued = g_queue.count();
  bytes = g_queue.bytes();
  dropped = g_queue.dropped();
  xSemaphoreGive(g_lock);
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up)
    LOGI("NET", "Wi-Fi '%s': connected, IP %s, RSSI %d dBm", c.ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
  else
    LOGI("NET", "Wi-Fi '%s': not connected (status %d, last disconnect reason %u) | password %s",
         c.ssid[0] ? c.ssid : "(not set)", static_cast<int>(WiFi.status()), g_lastReason, c.pass[0] ? "set" : "empty");
  LOGI("NET", "MQTT %s | user %s | password %s | %s | topic airguard/%s/telemetry",
       c.broker[0] ? c.broker : "(not set)", c.user[0] ? c.user : "(none)", c.mpass[0] ? "set" : "empty",
       mqttStateText(g_mqttState), cfg::DEVICE_ID);
  LOGI("NET", "queue %u records (%u B, dropped %lu) | sent %lu in %lu publishes, errors %lu, last OK %lu s ago",
       static_cast<unsigned>(queued), static_cast<unsigned>(bytes), static_cast<unsigned long>(dropped),
       static_cast<unsigned long>(g_sent), static_cast<unsigned long>(g_pubs), static_cast<unsigned long>(g_errors),
       g_lastOkMs ? static_cast<unsigned long>((millis() - g_lastOkMs) / 1000) : 0UL);
}

}  // namespace uplink

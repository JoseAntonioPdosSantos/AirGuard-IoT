#include "uplink.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <freertos/semphr.h>
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
#ifndef NET_SERVER_URL
#define NET_SERVER_URL ""
#endif
#ifndef NET_DEVICE_KEY
#define NET_DEVICE_KEY ""
#endif

namespace {

constexpr const char* NVS_NS = "airguard-net";

struct Config {
  char ssid[33] = {};
  char pass[65] = {};
  char server[96] = {};
  char key[80] = {};
};

Config            g_cfg;
SemaphoreHandle_t g_lock = nullptr;  // queue + config
uint8_t           g_store[cfg::UPLINK_QUEUE_BYTES];
UplinkQueue       g_queue(g_store, sizeof g_store);
volatile bool     g_reconnect = false;

// status (written by the uplink task, read by the console)
volatile uint32_t g_sent = 0, g_posts = 0, g_errors = 0, g_lastOkMs = 0;
volatile int      g_lastHttp = 0;

void copyStr(char* dst, size_t cap, const char* src) {
  strncpy(dst, src ? src : "", cap - 1);
  dst[cap - 1] = '\0';
}

void loadConfig() {
  copyStr(g_cfg.ssid, sizeof g_cfg.ssid, NET_WIFI_SSID);
  copyStr(g_cfg.pass, sizeof g_cfg.pass, NET_WIFI_PASS);
  copyStr(g_cfg.server, sizeof g_cfg.server, NET_SERVER_URL[0] ? NET_SERVER_URL : cfg::UPLINK_DEFAULT_SERVER);
  copyStr(g_cfg.key, sizeof g_cfg.key, NET_DEVICE_KEY);
  Preferences p;
  if (!p.begin(NVS_NS, true)) return;  // namespace not created yet: defaults
  if (p.isKey("ssid")) p.getString("ssid", g_cfg.ssid, sizeof g_cfg.ssid);
  if (p.isKey("pass")) p.getString("pass", g_cfg.pass, sizeof g_cfg.pass);
  if (p.isKey("server")) p.getString("server", g_cfg.server, sizeof g_cfg.server);
  if (p.isKey("key")) p.getString("key", g_cfg.key, sizeof g_cfg.key);
  p.end();
}

void connectWifi(const Config& c) {
  WiFi.disconnect(false, false);
  if (!c.ssid[0]) return;
  WiFi.begin(c.ssid, c.pass);
}

// Body: {"now":<millis at send>,"records":[...]}. The server places each
// record in time with now - ms (the node has no clock).
char g_body[cfg::UPLINK_BATCH_BYTES];

void task(void*) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("airguard-node");
  Config c;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  c = g_cfg;
  xSemaphoreGive(g_lock);
  connectWifi(c);
  bool wasUp = false;
  uint32_t lastTry = 0, downSince = millis();
  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(cfg::UPLINK_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(cfg::UPLINK_HTTP_TIMEOUT_MS);

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (g_reconnect) {
      g_reconnect = false;
      xSemaphoreTake(g_lock, portMAX_DELAY);
      c = g_cfg;
      xSemaphoreGive(g_lock);
      http.end();
      connectWifi(c);
      downSince = millis();
    }
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up != wasUp) {
      wasUp = up;
      if (up) LOGI("NET", "Wi-Fi '%s' connected, IP %s, RSSI %d dBm", c.ssid,
                   WiFi.localIP().toString().c_str(), WiFi.RSSI());
      else { LOGW("NET", "Wi-Fi disconnected"); downSince = millis(); http.end(); }
    }
    if (!up) {
      // auto-reconnect normally handles it; kick it if it gave up
      if (c.ssid[0] && (uint32_t)(millis() - downSince) > 30000) { connectWifi(c); downSince = millis(); }
      continue;
    }
    if (!c.server[0] || !c.key[0]) continue;
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
    g_body[n] = '\0';

    char url[128];
    snprintf(url, sizeof url, "%s/api/telemetry/ingest", c.server);
    int code = -1;
    if (http.begin(url)) {
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-Device-Key", c.key);
      code = http.POST(reinterpret_cast<uint8_t*>(g_body), n);
      if (code <= 0) http.end();  // broken connection: open a new one next time
    }
    g_lastHttp = code;
    ++g_posts;
    if (code == 200) {
      xSemaphoreTake(g_lock, portMAX_DELAY);
      g_queue.confirm(lastSeq);
      xSemaphoreGive(g_lock);
      g_sent += count;
      g_lastOkMs = millis();
      http.getString();  // drain {"kept":n} so the connection can be reused
    } else {
      ++g_errors;
      if (g_errors % 20 == 1)
        LOGW("NET", "POST %s failed: %d%s", url, code,
             code == 401 ? " (device key not accepted: 'net key <key>')" : "");
      if (code == 401 || code == 400) {
        // rejected for good: do not resend the same batch forever
        if (code == 400) {
          xSemaphoreTake(g_lock, portMAX_DELAY);
          g_queue.confirm(lastSeq);
          xSemaphoreGive(g_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
      }
    }
  }
}

}  // namespace

namespace uplink {

void begin() {
  g_lock = xSemaphoreCreateMutex();
  loadConfig();
  LOGI("NET", "uplink: Wi-Fi '%s'%s, server %s, key %s", g_cfg.ssid[0] ? g_cfg.ssid : "(none)",
       g_cfg.ssid[0] ? "" : " - set with 'net ssid <name>'", g_cfg.server[0] ? g_cfg.server : "(none)",
       g_cfg.key[0] ? "set" : "NOT SET ('net key <key>')");
  xTaskCreatePinnedToCore(task, "uplink", cfg::UPLINK_TASK_STACK, nullptr, 1, nullptr, 0);
}

void push(const char* json, size_t len) {
  if (!g_lock) return;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;  // never stall the loop
  g_queue.push(json, len);
  xSemaphoreGive(g_lock);
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

bool set(Setting s, const char* v) {
  if (!g_lock) return false;
  const char* names[] = {"ssid", "pass", "server", "key"};
  const size_t caps[] = {sizeof g_cfg.ssid, sizeof g_cfg.pass, sizeof g_cfg.server, sizeof g_cfg.key};
  const uint8_t i = static_cast<uint8_t>(s);
  if (strlen(v) >= caps[i]) return false;
  if (s == Setting::Server && v[0] && strncmp(v, "http://", 7) != 0) return false;  // no TLS
  xSemaphoreTake(g_lock, portMAX_DELAY);
  char* dst[] = {g_cfg.ssid, g_cfg.pass, g_cfg.server, g_cfg.key};
  copyStr(dst[i], caps[i], v);
  // strip a trailing '/' from the server URL
  if (s == Setting::Server) { size_t n = strlen(g_cfg.server); if (n && g_cfg.server[n - 1] == '/') g_cfg.server[n - 1] = '\0'; }
  xSemaphoreGive(g_lock);
  Preferences p;
  bool ok = p.begin(NVS_NS, false);
  if (ok) {
    ok = p.putString(names[i], v) == strlen(v) || !v[0];
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
  LOGI("NET", "Wi-Fi '%s': %s%s%s", c.ssid[0] ? c.ssid : "(not set)", up ? "connected, IP " : "not connected",
       up ? WiFi.localIP().toString().c_str() : "", up ? "" : "");
  if (up) LOGI("NET", "Wi-Fi RSSI %d dBm", WiFi.RSSI());
  LOGI("NET", "server %s | key %s | password %s", c.server[0] ? c.server : "(not set)",
       c.key[0] ? "set" : "NOT SET", c.pass[0] ? "set" : "empty");
  LOGI("NET", "queue %u records (%u B, dropped %lu) | sent %lu in %lu POSTs, errors %lu, last HTTP %d, "
       "last OK %lu s ago",
       static_cast<unsigned>(queued), static_cast<unsigned>(bytes), static_cast<unsigned long>(dropped),
       static_cast<unsigned long>(g_sent), static_cast<unsigned long>(g_posts),
       static_cast<unsigned long>(g_errors), g_lastHttp,
       g_lastOkMs ? static_cast<unsigned long>((millis() - g_lastOkMs) / 1000) : 0UL);
}

}  // namespace uplink

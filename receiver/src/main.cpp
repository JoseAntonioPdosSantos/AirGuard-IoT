// AirGuard Receiver - receives the AirGuard IoT node's LoRa alerts, ACKs them,
// shows them on a 16x2 I2C LCD and on three LEDs:
//   green = normal, yellow = LOW / MEDIUM, red = HIGH (green blinks while
//   no message has been received yet).
// Board: Waveshare RP2040-Zero (Arduino-Pico core). Wiring: receiver/README.md.
#include <Arduino.h>
#include <Wire.h>

#include <initializer_list>

#include "event_parser.h"
#include "lcd_view.h"
#include "radio_link.h"
#include "rx_config.h"
#include "rx_state.h"

namespace {

RadioLink         g_radio;
LcdView           g_lcd;
rx::ReceiverState g_state;
rx::SeenIds       g_seen;
int               g_lastRssi = 0;

// ACKs waiting for the radio to be free (the node retransmits after 60 s).
struct PendingAck { uint32_t id; uint16_t addr; };
PendingAck g_ack[4];
uint8_t    g_ackCount = 0;

void queueAck(uint32_t id, uint16_t addr) {
  for (uint8_t i = 0; i < g_ackCount; ++i)
    if (g_ack[i].id == id) return;
  if (g_ackCount < 4) g_ack[g_ackCount++] = {id, addr};
  else Serial.printf("[ACK] queue full, ACK #%lu dropped (node will retransmit)\r\n",
                     static_cast<unsigned long>(id));
}

void sendPendingAck(uint32_t now) {
  if (!g_ackCount || g_radio.busy(now) || !g_radio.ok()) return;
  char json[96];
  const size_t n = rx::buildAck(json, sizeof json, rxcfg::GATEWAY_ID, rxcfg::NODE_ID, g_ack[0].id);
  if (n && g_radio.send(g_ack[0].addr, json, n, now)) {
    Serial.printf("[ACK] sent for #%lu to addr %u\r\n", static_cast<unsigned long>(g_ack[0].id),
                  g_ack[0].addr);
    for (uint8_t i = 1; i < g_ackCount; ++i) g_ack[i - 1] = g_ack[i];
    --g_ackCount;
  }
}

void handleFrame(const char* data, size_t len, uint16_t from, int rssi, int snr, uint32_t now) {
  rx::Heartbeat hb;
  if (rx::parseHeartbeat(data, len, hb) == rx::ParseResult::Ok) {
    if (strcmp(hb.device, rxcfg::NODE_ID) != 0) return;
    const bool wasLost = g_state.lost(now, rxcfg::LINK_TIMEOUT_MS);
    g_lastRssi = rssi;
    if (g_state.apply(hb, now) || wasLost)
      Serial.printf("[HB] #%lu levels %s/%s/%s/%s (RSSI %d) -> overall %s%s\r\n",
                    static_cast<unsigned long>(hb.seq), rx::sevName(hb.levels[0]), rx::sevName(hb.levels[1]),
                    rx::sevName(hb.levels[2]), rx::sevName(hb.levels[3]), rssi,
                    rx::sevName(g_state.overall()), wasLost ? " (signal back)" : "");
    return;
  }
  rx::Event e;
  const rx::ParseResult r = rx::parseEvent(data, len, e);
  if (r == rx::ParseResult::NotEvent) {
    Serial.printf("[RX] non-event frame ignored: %.*s\r\n", static_cast<int>(len), data);
    return;
  }
  if (r != rx::ParseResult::Ok) {
    Serial.printf("[RX] invalid frame (%u) from addr %u: %.*s\r\n", static_cast<unsigned>(r), from,
                  static_cast<int>(len), data);
    return;
  }
  if (strcmp(e.device, rxcfg::NODE_ID) != 0 || (e.dst[0] && strcmp(e.dst, rxcfg::GATEWAY_ID) != 0)) {
    Serial.printf("[RX] event from '%s' to '%s' ignored\r\n", e.device, e.dst);
    return;
  }
  g_lastRssi = rssi;
  queueAck(e.eventId, from);  // always ACK, also a duplicate (the node did not get our ACK)
  if (g_seen.checkAndInsert(e.eventId)) {
    Serial.printf("[RX] duplicate #%lu (retransmission): ACK again\r\n",
                  static_cast<unsigned long>(e.eventId));
    return;
  }
  g_state.apply(e, now);
  Serial.printf("[RX] #%lu %s %s %s (RSSI %d dBm, SNR %d) -> overall %s\r\n",
                static_cast<unsigned long>(e.eventId), e.recovery ? "RECOVERY" : "ALERT",
                rx::catNamePt(e.category), rx::sevName(e.severity), rssi, snr,
                rx::sevName(g_state.overall()));
}

void updateLeds(uint32_t now) {
  const rx::Led l = g_state.led(now, rxcfg::LINK_TIMEOUT_MS);
  const bool blink = (now / rxcfg::WAIT_BLINK_MS) % 2 == 0;
  digitalWrite(rxcfg::LED_GREEN, (l == rx::Led::Green || (l == rx::Led::WaitingBlink && blink)) ? HIGH : LOW);
  // Lost signal: yellow blinks (the last known state may be outdated).
  digitalWrite(rxcfg::LED_YELLOW, (l == rx::Led::Yellow || (l == rx::Led::LostBlink && blink)) ? HIGH : LOW);
  digitalWrite(rxcfg::LED_RED, l == rx::Led::Red ? HIGH : LOW);
}

}  // namespace

void setup() {
  for (uint8_t p : {rxcfg::LED_GREEN, rxcfg::LED_YELLOW, rxcfg::LED_RED}) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);  // lamp test: all on during start-up
  }
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (uint32_t)(millis() - t0) < 1500) {}
  Serial.print("\r\n[BOOT] AirGuard Receiver (LoRa -> LCD + LEDs)\r\n");

  Wire.setSDA(rxcfg::I2C_SDA);
  Wire.setSCL(rxcfg::I2C_SCL);
  Wire.begin();
  Wire.setClock(100000);
  g_lcd.begin();
  g_lcd.show("AirGuard RX     ", "Iniciando LoRa  ");

  rp2040.wdt_begin(rxcfg::WATCHDOG_MS);
  g_radio.begin();
  for (uint8_t p : {rxcfg::LED_GREEN, rxcfg::LED_YELLOW, rxcfg::LED_RED}) digitalWrite(p, LOW);
}

void loop() {
  const uint32_t now = millis();
  rp2040.wdt_reset();
  g_radio.loop(now);

  char buf[248];
  size_t len;
  uint16_t from;
  int rssi, snr;
  if (g_radio.take(buf, sizeof buf, len, from, rssi, snr)) handleFrame(buf, len, from, rssi, snr, now);
  sendPendingAck(now);

  updateLeds(now);

  static uint32_t lastLcd = 0;
  if ((uint32_t)(now - lastLcd) >= 200) {
    lastLcd = now;
    char l1[17], l2[17];
    rx::formatLine1(g_state, now, rxcfg::LINK_TIMEOUT_MS, l1);
    const bool linkPage = (now / rxcfg::LCD_PAGE_MS) % 2 == 1;
    rx::formatLine2(g_state, linkPage, g_lastRssi, now, rxcfg::LINK_TIMEOUT_MS, l2);
    if (!g_radio.ok() && g_state.anyReceived() == false) snprintf(l2, sizeof l2, "%-16s", "LoRa: FALHA");
    g_lcd.show(l1, l2);
  }
}

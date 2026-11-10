// AirGuard IoT - USB serial console: diagnostics, bench tests and the sensor /
// radio SIMULATION used by the experimental test plan (docs/07_test_plan.md).
// Commands never execute data received over LoRa; this console is local only.
#include <Arduino.h>
#include <Wire.h>
#include <stdlib.h>
#include <string.h>

#include "../config/config.h"
#include "../config/hardware_pins.h"
#include "../platform/board.h"
#include "../utils/logger.h"
#include "airguard_app.h"

namespace {

const char* HELP =
    "AirGuard IoT console\r\n"
    "  status | stats | help\r\n"
    "  diag                                 wiring diagnostics (I2C scan, line levels, LoRa baud probe)\r\n"
    "  oled test [s]                        all OLED pixels ON (default 10 s) to find dead rows\r\n"
    "  out test [s]                         cycle LED yellow -> red -> buzzer (default 9 s)\r\n"
    "  warmup skip                          skip MQ-135 warm-up (TESTS ONLY)\r\n"
    "  sim air <volts> | sim air off        simulate the MQ-135 AO voltage (0..5 V)\r\n"
    "  sim adc <0..4095> | sim adc off      simulate MQ-135 raw ADC\r\n"
    "  sim temp <C> | sim temp off          simulate temperature\r\n"
    "  sim hum <%> | sim hum off            simulate humidity\r\n"
    "  sim envfail on|off                   simulate temp/hum sensor failure\r\n"
    "  sim airfail on|off                   simulate MQ-135 read failure\r\n"
    "  sim lorafail                         force a LoRa radio fault (auto re-init)\r\n"
    "  sim rx <json>                        inject a received LoRa frame\r\n"
    "  sim ack <event_id>                   inject an ACK from the gateway\r\n"
    "  sim text <message>                   inject a TEXT from WHATSAPP_GATEWAY\r\n"
    "  sim off                              disable every simulation\r\n"
    "  log debug|info|warn                  log level\r\n"
    "  reboot                               reboot (test: reboot during alert)\r\n";

void writeRaw(const char* s) {
  if (Serial) Serial.write(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

bool parseFloat(const char* s, float& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  out = strtof(s, &end);
  return end && end != s && *end == '\0';
}

bool parseUint(const char* s, uint32_t& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  const unsigned long v = strtoul(s, &end, 10);
  if (!end || end == s || *end != '\0') return false;
  out = static_cast<uint32_t>(v);
  return true;
}

// Splits off the first space-separated word. Returns the rest (may be "").
char* nextWord(char*& p) {
  while (*p == ' ') ++p;
  char* w = p;
  while (*p && *p != ' ') ++p;
  if (*p) *p++ = '\0';
  while (*p == ' ') ++p;
  return w;
}

uint32_t g_simTextId = 900000;

}  // namespace

void AirGuardApp::handleCommand(char* line, uint32_t now) {
  char* p = line;
  const char* cmd = nextWord(p);
  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) { writeRaw(HELP); return; }
  if (!strcmp(cmd, "status")) { printStatus(now); return; }
  if (!strcmp(cmd, "stats")) { printStats(); return; }
  if (!strcmp(cmd, "diag")) { runDiagnostics(now); return; }
  if (!strcmp(cmd, "oled")) {
    if (!strcmp(nextWord(p), "test")) {
      const long s = atol(nextWord(p));
      const uint32_t secs = (s > 0 && s <= 600) ? static_cast<uint32_t>(s) : 10;
      oled_.startPixelTest(now, secs * 1000UL);
      LOGI("CON", "OLED pixel test: whole screen ON for %lu s", static_cast<unsigned long>(secs));
    } else {
      LOGW("CON", "usage: oled test");
    }
    return;
  }
  if (!strcmp(cmd, "out")) {
    if (!strcmp(nextWord(p), "test")) {
      const long s = atol(nextWord(p));
      const uint32_t secs = (s > 0 && s <= 600) ? static_cast<uint32_t>(s) : 9;
      outTest_ = true;
      outTestStartMs_ = now;
      outTestUntilMs_ = now + secs * 1000UL;
      LOGI("CON", "output test %lu s: yellow GPIO%u, red GPIO%u, buzzer GPIO%u (1 s each)",
           static_cast<unsigned long>(secs), pins::LED_YELLOW, pins::LED_RED, pins::BUZZER);
    } else {
      LOGW("CON", "usage: out test [s]");
    }
    return;
  }
  if (!strcmp(cmd, "reboot")) {
    LOGW("CON", "rebooting");
    delay(50);  // let the log line leave
    board::reboot();
    return;
  }

  if (!strcmp(cmd, "log")) {
    const char* lv = nextWord(p);
    if (!strcmp(lv, "debug")) logger::setLevel(LogLevel::Debug);
    else if (!strcmp(lv, "info")) logger::setLevel(LogLevel::Info);
    else if (!strcmp(lv, "warn")) logger::setLevel(LogLevel::Warn);
    else { LOGW("CON", "usage: log debug|info|warn"); return; }
    LOGI("CON", "log level %s", lv);
    return;
  }

  if (!strcmp(cmd, "warmup")) {
    if (!strcmp(nextWord(p), "skip")) {
      air_.skipWarmup();
      LOGW("CON", "MQ-135 warm-up skipped: readings are NOT trustworthy (tests only)");
    }
    return;
  }

  if (strcmp(cmd, "sim") != 0) { LOGW("CON", "unknown command '%s' (try help)", cmd); return; }

  // ---- simulation ---------------------------------------------------------
  const char* what = nextWord(p);
  if (!strcmp(what, "off")) {
    air_.simulateVolts(false);
    air_.simulateAdc(false);
    air_.simulateFault(false);
    env_.simulateTemperature(false);
    env_.simulateHumidity(false);
    env_.simulateFault(false);
    LOGI("SIM", "all simulations disabled");
    return;
  }
  if (!strcmp(what, "rx")) {
    if (!*p) { LOGW("SIM", "usage: sim rx {json}"); return; }
    LOGI("SIM", "injecting frame: %s", p);
    handleFrame(reinterpret_cast<const uint8_t*>(p), strlen(p), 0, 0, now);
    return;
  }
  if (!strcmp(what, "ack")) {
    uint32_t id;
    if (!parseUint(nextWord(p), id)) { LOGW("SIM", "usage: sim ack <event_id>"); return; }
    char buf[96];
    const size_t n = proto::encodeAck(cfg::GATEWAY_ID, cfg::DEVICE_ID, id, buf, sizeof buf);
    handleFrame(reinterpret_cast<const uint8_t*>(buf), n, 0, 0, now);
    return;
  }
  if (!strcmp(what, "text")) {
    if (!*p) { LOGW("SIM", "usage: sim text <message>"); return; }
    // Built with the protocol writer so quotes/backslashes are escaped.
    char buf[cfg::LORA_MAX_PAYLOAD + 1];
    proto::JsonWriter w(buf, sizeof buf);
    w.str("type", "TEXT");
    w.str("source", "WHATSAPP_GATEWAY");
    w.unum("message_id", ++g_simTextId);
    w.str("text", p);
    const size_t n = w.finish();
    if (n == 0) { LOGW("SIM", "text too long"); return; }
    handleFrame(reinterpret_cast<const uint8_t*>(buf), n, 0, 0, now);
    return;
  }
  if (!strcmp(what, "lorafail")) {
    lora_.simulateFault(now);
    return;
  }

  const char* arg = nextWord(p);
  const bool off = !strcmp(arg, "off");
  const bool on = !strcmp(arg, "on");
  float v = 0;
  const bool hasNum = parseFloat(arg, v);

  if (!strcmp(what, "air") && (off || (hasNum && v >= 0 && v <= 5))) {
    air_.simulateVolts(!off, v);
    LOGI("SIM", "MQ-135 AO voltage %s%s", off ? "real" : arg, off ? "" : " V");
  } else if (!strcmp(what, "adc") && (off || (hasNum && v >= 0 && v <= 4095))) {
    air_.simulateAdc(!off, static_cast<uint16_t>(v));
    LOGI("SIM", "MQ-135 ADC %s", off ? "real" : arg);
  } else if (!strcmp(what, "temp") && (off || hasNum)) {
    env_.simulateTemperature(!off, v);
    LOGI("SIM", "temperature %s", off ? "real" : arg);
  } else if (!strcmp(what, "hum") && (off || hasNum)) {
    env_.simulateHumidity(!off, v);
    LOGI("SIM", "humidity %s", off ? "real" : arg);
  } else if (!strcmp(what, "envfail") && (on || off)) {
    env_.simulateFault(on);
    LOGI("SIM", "env sensor failure %s", arg);
  } else if (!strcmp(what, "airfail") && (on || off)) {
    air_.simulateFault(on);
    LOGI("SIM", "MQ-135 failure %s", arg);
  } else {
    LOGW("SIM", "bad arguments (try help)");
  }
}

// ---------------------------------------------------------------------------
// Bench diagnostics (blocking, ~3 s; the watchdog is fed inside). Run with
// the hardware connected; it tells "no wire / no power" apart from "wrong
// pin / wrong address / wrong baud rate".
namespace {

// Idle level of a line with internal pulls disabled: a pull-up on the module
// (I2C, DHT) or an idle UART TX drives it HIGH; nothing connected -> floats.
const char* idleLevel(uint8_t pin) {
  pinMode(pin, INPUT);
  delay(2);
  int highs = 0;
  for (int i = 0; i < 8; ++i) { highs += digitalRead(pin); delayMicroseconds(200); }
  pinMode(pin, INPUT_PULLDOWN);
  delay(2);
  const int withPulldown = digitalRead(pin);
  pinMode(pin, INPUT);
  if (highs == 8 && withPulldown) return "HIGH (driven / pulled up: something is connected)";
  if (highs == 0 && !withPulldown) return "LOW (shorted to GND or module unpowered)";
  return "FLOATING (nothing connected or no pull-up)";
}

}  // namespace

void AirGuardApp::runDiagnostics(uint32_t now) {
  writeRaw("\r\n=== AirGuard diag ===\r\n");
  char b[200];
  snprintf(b, sizeof b, "board: %s\r\n", board::NAME);
  writeRaw(b);

  // ---- I2C (OLED) ----
  Wire.end();
  snprintf(b, sizeof b, "I2C SDA GPIO%u: %s\r\n", pins::I2C_SDA, idleLevel(pins::I2C_SDA));
  writeRaw(b);
  snprintf(b, sizeof b, "I2C SCL GPIO%u: %s\r\n", pins::I2C_SCL, idleLevel(pins::I2C_SCL));
  writeRaw(b);
  board::i2cBegin(100000);  // slow scan
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      snprintf(b, sizeof b, "I2C device at 0x%02X%s\r\n", a,
               a == cfg::OLED_I2C_ADDR ? " (expected OLED)" : (a == 0x3C || a == 0x3D ? " (OLED? set OLED_I2C_ADDR)" : ""));
      writeRaw(b);
      ++found;
    }
  }
  if (!found) writeRaw("I2C: no device answered (check VDD/GND, SDA<->SCL swap)\r\n");
  Wire.setClock(cfg::I2C_CLOCK_HZ);
  board::watchdogFeed();

  // ---- DHT data line ----
  snprintf(b, sizeof b, "DHT11 DATA GPIO%u: %s\r\n", pins::DHT_DATA, idleLevel(pins::DHT_DATA));
  writeRaw(b);
  pinMode(pins::DHT_DATA, INPUT_PULLUP);  // library default between reads

  // ---- RYLR998 ----
  lora_.simulateFault(now);  // stop the driver; it re-initialises by itself in 30 s
  board::loraUartEnd();
  snprintf(b, sizeof b, "RYLR998 TXD -> GPIO%u (idle must be HIGH): %s\r\n", pins::LORA_UART_RX,
           idleLevel(pins::LORA_UART_RX));
  writeRaw(b);
  digitalWrite(pins::LORA_NRST, HIGH);

  // Sends AT on the current UART and returns true on "+OK"; resp gets the text.
  auto probe = [](Stream& port, char* resp, size_t cap) {
    delay(5);
    while (port.available()) port.read();
    port.write("AT\r\n");
    size_t n = 0;
    const uint32_t t0 = millis();
    while (millis() - t0 < 400 && n < cap - 1) {
      if (port.available()) {
        const int c = port.read();
        if (c >= 0x20 && c < 0x7F) resp[n++] = static_cast<char>(c);
      }
    }
    resp[n] = '\0';
    board::watchdogFeed();
    return strstr(resp, "+OK") != nullptr;
  };

  static const uint32_t bauds[] = {115200, 9600, 57600, 38400, 19200};
  bool answered = false;
  for (uint32_t baud : bauds) {
    board::loraUartBegin(baud, 256);
    char resp[48];
    answered = probe(board::loraSerial(), resp, sizeof resp);
    board::loraUartEnd();
    snprintf(b, sizeof b, "RYLR998 @%lu baud: %s\r\n", static_cast<unsigned long>(baud),
             resp[0] ? resp : "(no answer)");
    writeRaw(b);
    if (answered) break;
  }
  if (!answered) {
    // Same test with TX/RX swapped: detects crossed TXD/RXD wires.
    char resp[48];
    board::loraUartBegin(cfg::LORA_UART_BAUD, 256, /*swapped=*/true);
    const bool swappedOk = probe(board::loraSerial(), resp, sizeof resp);
    board::loraUartEnd();
    if (swappedOk)
      snprintf(b, sizeof b,
               "RYLR998: answers with TX/RX SWAPPED -> exchange the TXD and RXD wires "
               "(module TXD must go to GPIO%u, RXD to GPIO%u)\r\n",
               pins::LORA_UART_RX, pins::LORA_UART_TX);
    else
      snprintf(b, sizeof b,
               "RYLR998: no +OK at any baud, also not swapped (check RXD wire to GPIO%u, "
               "3V3/GND at the module, NRST to GPIO%u)\r\n",
               pins::LORA_UART_TX, pins::LORA_NRST);
    writeRaw(b);
  }
  board::loraUartBegin(cfg::LORA_UART_BAUD, 512);
  writeRaw("=== end diag (LoRa driver retries in 30 s) ===\r\n");
}

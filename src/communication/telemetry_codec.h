// AirGuard IoT - telemetry lines for the AirGuard Web dashboard (platform independent).
//
// The node writes them on the USB serial port, mixed with the normal log:
//   @T {"src":"node","k":"env","ms":12345,...}\r\n
// A bridge on the PC (AirGuard-Web/bridge/serial_bridge.py) keeps only the
// lines that start with "@T ", stamps them with the PC clock and sends them to
// the web server. Record kinds and fields: docs/11_telemetry.md.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../monitoring/alert_types.h"
#include "message_protocol.h"

namespace tlm {

constexpr const char* PREFIX = "@T ";
// A "tx" record carries the LoRa frame (<= 240 B) JSON-escaped: worst case
// ~2x, plus the other fields.
constexpr size_t LINE_CAP = 720;
constexpr size_t SMALL_CAP = 320;  // every record without a frame

// One telemetry line: "@T {"src":<src>,"k":<kind>,"ms":<ms>, ...}\r\n".
// Add fields with w(), then finish(). Never writes past `cap`.
class Line {
 public:
  Line(char* buf, size_t cap, const char* src, const char* kind, uint32_t ms);
  proto::JsonWriter& w() { return w_; }
  // Closes the object and appends CRLF. Returns the total length, 0 if it did not fit.
  size_t finish();

 private:
  char* buf_;
  size_t cap_;
  proto::JsonWriter w_;
};

// Periodic snapshot of the node ("env" record).
struct NodeEnv {
  bool        envValid = false;
  float       temperature = 0, humidity = 0, heatIndex = 0;
  const char* envState = "invalid";   // ok | invalid | FAULT
  bool        airValid = false;       // gas reading usable (warm, no fault)
  float       gasVolts = 0;           // value compared with the alarm limits
  float       aoVolts = 0;            // MQ-135 AO voltage
  uint16_t    rawAdc = 0;
  const char* airState = "invalid";   // ok | WARMUP | invalid | FAULT
  bool        simulated = false;
  Severity    levels[CATEGORY_COUNT] = {};
  Severity    overall = Severity::Normal;
  bool        relayOn = false;
  uint16_t    systemFlags = 0;
  const char* lora = "INIT";          // ok | INIT | FAULT
  uint8_t     txQueue = 0, waitAck = 0;
  uint32_t    uptimeS = 0;
};
size_t encodeNodeEnv(const NodeEnv& e, uint32_t ms, char* buf, size_t cap);

// "NNLH": one letter (N/L/M/H) per category, in Category order.
void levelLetters(const Severity levels[CATEGORY_COUNT], char out[CATEGORY_COUNT + 1]);

}  // namespace tlm

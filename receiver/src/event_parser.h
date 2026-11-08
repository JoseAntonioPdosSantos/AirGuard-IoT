// AirGuard Receiver - decoder for the node's ALERT / RECOVERY frames
// (platform independent, unit tested in receiver/tests).
//
// Frames are flat JSON objects (protocol v1, docs/03_lora_protocol.md), e.g.
//   {"v":1,"type":"ALERT","device":"KITCHEN_01","dst":"GATEWAY_01",
//    "event_id":458764,"category":"AIR_QUALITY","severity":"HIGH",...}
// Received content is DATA only: nothing in it is executed.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace rx {

enum class Sev : uint8_t { Normal = 0, Low = 1, Medium = 2, High = 3 };
enum class Cat : uint8_t { Air = 0, Temp = 1, Hum = 2, System = 3, Count = 4 };

struct Event {
  bool     recovery = false;     // type RECOVERY (else ALERT)
  char     device[24] = {};
  char     dst[24] = {};
  uint32_t eventId = 0;
  Cat      category = Cat::System;
  Sev      severity = Sev::Normal;
  bool     hasAir = false;  uint32_t airIndex = 0;
  bool     hasTemp = false; float temperature = 0;
  bool     hasHum = false;  float humidity = 0;
  bool     hasFlags = false; uint32_t flags = 0;
  bool     reminder = false;
};

enum class ParseResult : uint8_t { Ok, NotJson, NotEvent, MissingField, BadValue };

// `data` need not be NUL-terminated. Unknown keys are ignored.
ParseResult parseEvent(const char* data, size_t len, Event& out);

// Builds {"type":"ACK","device":<gw>,"dst":<node>,"ack_for":<id>}. Returns the
// length (0 if it does not fit).
size_t buildAck(char* out, size_t cap, const char* gatewayId, const char* nodeId, uint32_t id);

// Small ring of recently seen (device, event_id) pairs: a retransmission keeps
// its event_id and must be ACKed again but not counted as a new event.
class SeenIds {
 public:
  bool checkAndInsert(uint32_t id);  // true if already seen
 private:
  static constexpr uint8_t N = 16;
  uint32_t ids_[N] = {};
  uint8_t  count_ = 0, pos_ = 0;
};

const char* sevName(Sev s);       // "NORMAL", "LOW", "MEDIUM", "HIGH"
const char* catNamePt(Cat c);     // "GAS", "TEMP", "UMIDADE", "SISTEMA" (for the LCD)

}  // namespace rx

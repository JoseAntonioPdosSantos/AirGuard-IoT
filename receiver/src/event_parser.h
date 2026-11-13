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

// Heartbeat ("estou vivo") sent by the node every few seconds, no ACK.
struct Heartbeat {
  char     device[24] = {};
  char     dst[24] = {};
  uint32_t seq = 0;
  Sev      levels[static_cast<uint8_t>(Cat::Count)] = {};
  bool     hasAir = false;  uint32_t airIndex = 0;
  bool     hasTemp = false; float temperature = 0;
  bool     hasHum = false;  float humidity = 0;
};
// Ok for a valid HB; NotEvent if the frame is another type.
ParseResult parseHeartbeat(const char* data, size_t len, Heartbeat& out);

// `data` need not be NUL-terminated. Unknown keys are ignored.
ParseResult parseEvent(const char* data, size_t len, Event& out);

// TEXT: a WhatsApp message the node relays for the LCD (no ACK), e.g.
//   {"type":"TEXT","device":"KITCHEN_01","dst":"GATEWAY_01","message_id":7,"text":"Maria: oi"}
// The text arrives as display ASCII; it is only shown, never interpreted.
struct TextMsg {
  char     device[24] = {};
  char     dst[24] = {};
  uint32_t id = 0;
  char     text[168] = {};  // a 240-byte frame carries at most ~150 characters
};
// Ok for a valid TEXT; NotEvent if the frame is another type.
ParseResult parseText(const char* data, size_t len, TextMsg& out);

// Builds {"type":"ACK","device":<gw>,"dst":<node>,"ack_for":<id>,"rssi":<dBm>,"snr":<dB>}
// (rssi/snr = how the receiver heard the event it confirms). Returns the
// length (0 if it does not fit).
size_t buildAck(char* out, size_t cap, const char* gatewayId, const char* nodeId, uint32_t id,
                int rssi, int snr);

// Receiver status sent to the node over LoRa (no ACK); the node forwards it to
// AirGuard Web. Telemetry of the receiver only travels this way (no USB log).
//   {"type":"STAT","device":"GATEWAY_01","dst":"KITCHEN_01","seq":120,"map":4294967295,
//    "hbs":118,"miss":2,"ev":3,"dup":0,"bad":0,"rssi":-31,"snr":9,"lcd":1,"lora":1,"lost":0,"up":610}
//   seq/map: last heartbeat seq heard and which of the 32 seqs up to it were heard
//   (bit i = seq - i); hbs/miss/ev/dup/bad: counters since the receiver booted;
//   rssi/snr: last frame from the node; up: receiver uptime (s).
struct GwStat {
  uint32_t seq = 0, map = 0, hbs = 0, miss = 0, events = 0, dups = 0, bad = 0, upS = 0;
  int      rssi = 0, snr = 0;
  bool     lcd = false, lora = false, lost = false;
};
size_t buildStat(char* out, size_t cap, const char* gatewayId, const char* nodeId, const GwStat& s);

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

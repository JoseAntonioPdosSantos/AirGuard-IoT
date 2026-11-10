// AirGuard IoT - LoRa application protocol v1 (compact JSON, platform independent).
// Full specification: docs/03_lora_protocol.md
//
// Outgoing: ALERT, RECOVERY (events, require ACK), ACK (reply, no ACK).
// Incoming: TEXT (display + ACK), ACK (confirms one of our events).
// The parser accepts only a flat JSON object with string/number/bool/null
// values; anything else (nesting, oversize, control chars) is rejected.
// Received content is DATA: it is never executed or interpreted as commands.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../config/config.h"
#include "../monitoring/alert_types.h"

namespace proto {

constexpr uint8_t VERSION = 1;

enum class MsgType : uint8_t { Unknown, Text, Ack };

enum class ParseError : uint8_t {
  Ok,
  Empty,
  TooLong,
  Malformed,
  Nested,
  UnknownType,
  MissingField,
  FieldTooLong,
  BadValue,
};
const char* parseErrorName(ParseError e);

struct IncomingMessage {
  MsgType  type = MsgType::Unknown;
  char     sender[cfg::MAX_ID_LEN + 1] = {};  // "source" (TEXT) or "device" (ACK)
  char     dst[cfg::MAX_ID_LEN + 1] = {};     // optional
  uint32_t id = 0;                            // message_id (TEXT) / ack_for (ACK)
  char     text[cfg::MAX_TEXT_BYTES + 1] = {};// UTF-8, TEXT only
};

// Bounded JSON writer: never writes past `cap`; ok() == false on overflow.
class JsonWriter {
 public:
  JsonWriter(char* buf, size_t cap);
  void key(const char* k);
  void str(const char* k, const char* v);
  void num(const char* k, int32_t v);
  void unum(const char* k, uint32_t v);
  void flt(const char* k, float v, uint8_t decimals);
  void boolean(const char* k, bool v);
  size_t finish();  // closes the object; returns length or 0 on overflow
  bool ok() const { return ok_; }

 private:
  void put(char c);
  void raw(const char* s);
  void escaped(const char* s);
  char* buf_;
  size_t cap_, len_ = 0;
  bool ok_ = true, first_ = true;
};

// "air_index" sent on the radio: MQ-135 AO voltage in % of 5 V (0..100),
// rounded. The receiver converts it back to volts (index / 20).
uint32_t airIndex(float gasVolts);

// Encoders return the payload length (0 = did not fit / invalid).
size_t encodeEvent(const AlertEvent& e, const char* device, const char* dst, char* buf,
                   size_t cap);
size_t encodeAck(const char* device, const char* dst, uint32_t ackFor, char* buf, size_t cap);
// Heartbeat ("estou vivo", no ACK): current level of every category as 4 chars
// in Category order (AIR, TEMP, HUM, SYS), N/L/M/H, plus the live readings.
//   {"v":1,"type":"HB","device":"KITCHEN_01","dst":"GATEWAY_01","seq":12,
//    "levels":"NNLN","air_index":5,"temperature":25.4,"humidity":61,"timestamp":60}
size_t encodeHeartbeat(const char* device, const char* dst, uint32_t seq,
                       const Severity levels[CATEGORY_COUNT], const SensorSnapshot& s,
                       uint32_t uptimeS, char* buf, size_t cap);

// Validates and decodes an incoming frame. `ownId` is used to drop frames
// explicitly addressed to another node (dst present and != ownId and != "*").
ParseError parse(const uint8_t* data, size_t len, IncomingMessage& out);
bool addressedToUs(const IncomingMessage& m, const char* ownId);
bool sourceAllowed(const char* sender);

}  // namespace proto

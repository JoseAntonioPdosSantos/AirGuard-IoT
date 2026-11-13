// AirGuard IoT - WhatsApp link: the MQTT payloads exchanged with AirGuard Web
// (platform independent, unit tested). Topics and fields: docs/12_whatsapp.md.
//
// AirGuard Web owns the WhatsApp session. This node receives
//   airguard/whatsapp/link   (retained) {"state":"code","code":"ABCD1234","ttl":165}
//                                       {"state":"open","me":"5511988887777"}
//                                       {"state":"idle"|"connecting"|"qr"|"offline"}
//   airguard/whatsapp/inbox             {"id":17,"from":"Maria","text":"Chego em 10 min"}
// and publishes its own state (alert levels, relay) for the alert messages.
// Received content is DATA: it is only displayed, never executed.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../config/config.h"
#include "../monitoring/alert_types.h"

namespace wa {

// Code: an 8-character pairing code is waiting to be typed on the phone (the
// OLED shows it). Qr: a pairing QR is waiting on the web page only: the OLED
// has 64 rows for its 61 modules, too small for a phone camera.
enum class LinkState : uint8_t { Unknown, Offline, Idle, Connecting, Qr, Code, Open };
const char* linkStateName(LinkState s);

constexpr uint8_t CODE_LEN = 8;

// State of the WhatsApp session on AirGuard Web.
struct Link {
  LinkState state = LinkState::Unknown;
  uint16_t  ttlS = 0;                // how long the code is valid (s), 0 = not given
  char      code[CODE_LEN + 1] = {}; // pairing code (state Code): digits and capital letters
  char      me[24] = {};             // linked number (state Open)
};

// One message received on WhatsApp.
struct Inbox {
  uint32_t id = 0;
  char     from[40] = {};   // sender name, UTF-8
  char     text[400] = {};  // UTF-8 (AirGuard Web already shortens it)
};

// Both return false (and leave `out` cleared) on anything that is not the
// expected flat JSON object.
bool parseLink(const char* json, size_t len, Link& out);
bool parseInbox(const char* json, size_t len, Inbox& out);

// What the node tells AirGuard Web about itself (retained on
// airguard/<device>/state); a change of level or of the relay becomes a
// WhatsApp message there.
struct NodeState {
  uint16_t boot = 0;
  uint32_t uptimeS = 0;
  Severity levels[CATEGORY_COUNT] = {};
  Severity overall = Severity::Normal;
  Category worst = Category::System;
  bool     relayOn = false;
  uint16_t systemFlags = 0;
  bool     envValid = false;
  float    temperature = 0, humidity = 0;
  bool     airValid = false;
  float    gasVolts = 0;
  bool     simulated = false;
};
// {"boot":12,"up":345,"lv":"NNHN","ov":"HIGH","worst":"AIR_QUALITY","relay":true,
//  "flags":0,"temp":27.5,"rh":61,"gas":1.82}. Returns the length, 0 if it did not fit.
size_t encodeNodeState(const NodeState& s, char* buf, size_t cap);
// Changes when something a WhatsApp message reports changes (levels, relay, flags).
uint32_t nodeStateSignature(const NodeState& s);

}  // namespace wa

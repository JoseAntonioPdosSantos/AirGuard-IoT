// AirGuard IoT - telemetry uplink to AirGuard Web over Wi-Fi + MQTT.
//
// The node is the only board on the network: it records what it sends and
// receives on the radio (and what the receiver reports in its STAT frames)
// and publishes it in batches to the MQTT broker on topic
// airguard/<DEVICE_ID>/telemetry (QoS 0); AirGuard Web subscribes to it.
//
// Runs in its own FreeRTOS task on core 0 (the application loop is on core 1):
// Wi-Fi connection, DNS and MQTT never delay the sensors, the alarm or LoRa.
// Records wait in a RAM queue (cfg::UPLINK_QUEUE_BYTES) while the network or
// the broker is down; when it fills, the oldest are dropped.
//
// Settings (NVS, survive reboots and re-flashing): console "net ..." commands;
// defaults from src/config/net_secrets.h when that file exists (gitignored).
//
// The same MQTT session carries the WhatsApp link (docs/12_whatsapp.md):
//   received  airguard/whatsapp/link   (retained) session state / pairing code
//             airguard/whatsapp/inbox             one message received on WhatsApp
//   published airguard/<DEVICE_ID>/state (retained) alert levels + relay
//             airguard/<DEVICE_ID>/event           hello after boot, message results
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "wa_codec.h"

namespace uplink {

void begin();
// Queues one record (a flat JSON object, no newline). Never blocks for long.
void push(const char* json, size_t len);

enum class Setting : uint8_t { Ssid, Password, Broker, User, MqttPassword };
bool set(Setting s, const char* value);  // saves to NVS and reconnects; "" clears
void printStatus();
bool connected();  // MQTT session up

// ---- WhatsApp link ------------------------------------------------------------
// The node's state as a flat JSON object: published (retained) when it is set
// and again after every MQTT (re)connection. Never blocks for long.
void setState(const char* json, size_t len);
// One event (flat JSON object) for AirGuard Web; waits in RAM while offline.
void event(const char* json, size_t len);
// Latest WhatsApp session state received. True (and `out` filled) when it
// changed since the last call.
bool takeWaLink(wa::Link& out);
// Next WhatsApp message received. False if none is waiting.
bool takeWaMessage(wa::Inbox& out);

}  // namespace uplink

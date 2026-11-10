// AirGuard IoT - telemetry uplink to AirGuard Web over Wi-Fi.
//
// The node is the only board on the network: it records what it sends and
// receives on the radio (and what the receiver reports in its STAT frames)
// and POSTs it to  <server>/api/telemetry/ingest  with header X-Device-Key.
//
// Runs in its own FreeRTOS task on core 0 (the application loop is on core 1):
// Wi-Fi connection, DNS and HTTP never delay the sensors, the alarm or LoRa.
// Records wait in a RAM queue (cfg::UPLINK_QUEUE_BYTES) while the network or
// the server is down; when it fills, the oldest are dropped.
//
// Settings (NVS, survive reboots and re-flashing): console "net ..." commands;
// defaults from src/config/net_secrets.h when that file exists (gitignored).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace uplink {

void begin();
// Queues one record (a flat JSON object, no newline). Never blocks for long.
void push(const char* json, size_t len);

enum class Setting : uint8_t { Ssid, Password, Server, Key };
bool set(Setting s, const char* value);  // saves to NVS and reconnects; "" clears
void printStatus();
bool connected();

}  // namespace uplink

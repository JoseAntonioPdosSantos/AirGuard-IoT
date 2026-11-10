// AirGuard IoT - default network settings for the telemetry uplink.
//
// Copy to net_secrets.h (gitignored) and fill in. Values set on the console
// ("net ssid|pass|server|key ...") are saved in flash and take precedence.
#pragma once
#define NET_WIFI_SSID  "my-wifi"
#define NET_WIFI_PASS  "my-password"
#define NET_SERVER_URL "http://192.168.1.10:8787"  // AirGuard Web (http only)
#define NET_DEVICE_KEY ""                         // node server/src/cli.js device:add "ESP32"

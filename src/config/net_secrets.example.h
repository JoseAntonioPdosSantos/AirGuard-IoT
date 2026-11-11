// AirGuard IoT - default network settings for the telemetry uplink.
//
// Copy to net_secrets.h (gitignored) and fill in. Values set on the console
// ("net ssid|pass|broker|user|mqttpass ...") are saved in flash and take precedence.
#pragma once
#define NET_WIFI_SSID   "my-wifi"
#define NET_WIFI_PASS   "my-password"
#define NET_MQTT_BROKER "192.168.1.10:1883"  // host[:port] of the MQTT broker
#define NET_MQTT_USER   "airguard"
#define NET_MQTT_PASS   ""                  // AIRGUARD_MQTT_PASSWORD of the broker

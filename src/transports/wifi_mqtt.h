// WiFiMqtt: sole Wi-Fi/MQTT owner, bounded backoff, never restarts sensing.
// Work package WP8 (Stage 5). Architecture §15.
#pragma once
#include <stdbool.h>

bool wifiMqttInit(void);          // does not wait for an access point
void wifiMqttRequestStop(void);   // preempts retry waits (power transition)
void wifiMqttRequestStart(void);

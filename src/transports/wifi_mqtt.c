#include "transports/wifi_mqtt.h"

// TODO(WP8): connection state machine, 1-60 s backoff with jitter,
// 60-entry RAM telemetry backlog.

bool wifiMqttInit(void) { return false; }
void wifiMqttRequestStop(void) {}
void wifiMqttRequestStart(void) {}

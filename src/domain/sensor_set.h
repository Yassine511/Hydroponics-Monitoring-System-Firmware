// Fixed sensor set as the app sees it (FirmwareContract.md v2, "Sensors").
// Pure C. Work package WP8 (Stage 5).
//
// Sensors are fixed by the board (pins in board_profile.h). The app can only
// enable or disable them; the enabled set is a SensorMask persisted by
// Configuration. Keys are the protocol names, stable across firmware versions.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domain/types.h"

typedef uint8_t SensorMask;  // bit per SensorId
#define SENSOR_MASK_ALL ((SensorMask)((1u << SENSOR_COUNT) - 1u))

static inline bool sensorMaskHas(SensorMask mask, SensorId s) {
  return s < SENSOR_COUNT && (mask & (1u << s)) != 0;
}

// Protocol key ("bme280", "ds18b20", "light", "hcsr04", "ph", "tds", "sgp30").
const char* sensorKey(SensorId s);
const char* sensorLabel(SensorId s);

// What disabling this sensor does to the others (app warning), "" if nothing.
const char* sensorDependsNote(SensorId s);

// SENSOR_COUNT if the key (len bytes, not NUL-terminated) is unknown.
SensorId sensorFromKey(const char* key, size_t len);

// Sensor that produces a metric; SENSOR_COUNT for an invalid metric.
SensorId sensorOfMetric(MetricId m);

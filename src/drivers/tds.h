// sen0244_tds driver. Work package WP5 (Stage 3). Architecture §8, §14.
//
// Switched 5 V rail, 0-2.3 V nominal output. Preserve raw voltage.
// Temperature compensation needs a fresh DS18B20 reading via
// tdsDriverSetWaterTemperature(); otherwise DependencyInvalid/uncompensated -
// never silently assume 25 C.
//
// Implements the SensorDriver contract in drivers/sensor_driver.h.
#pragma once
#include "drivers/sensor_driver.h"
#include "hal/adc_channel.h"
#include "domain/calibration.h"

typedef struct TdsConfig {
  AdcChannel* adc;
  LinearCalibration calibration;
} TdsConfig;

// Members after `base` are private to tds.c.
typedef struct TdsDriver {
  SensorDriver base;  // must stay first
  TdsConfig config_;
  bool initialised_;
  Reading waterTemperature_;
} TdsDriver;

// Constructor. Use &driver.base wherever a SensorDriver* is expected.
void tdsDriverConstruct(TdsDriver* self, const TdsConfig* config);

// Compensation input from the latest DS18B20 reading (copied, not shared).
void tdsDriverSetWaterTemperature(TdsDriver* self, const Reading* waterTemperature);

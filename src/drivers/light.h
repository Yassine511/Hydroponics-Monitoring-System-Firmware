// dfr0026_light driver. Work package WP3 (Stage 2). Architecture §3, §14.
//
// DFR0026 ambient light. Publish calibrated mV (UNIT_MILLIVOLT).
// No lux conversion until a reference-meter calibration exists (Uncalibrated).
// Characterise filter and saturation near the 3.3 V rail.
//
// Implements the SensorDriver contract in drivers/sensor_driver.h.
#pragma once
#include "drivers/sensor_driver.h"
#include "hal/adc_channel.h"

typedef struct LightConfig {
  AdcChannel* adc;
} LightConfig;

// Members after `base` are private to light.c.
typedef struct LightDriver {
  SensorDriver base;  // must stay first
  LightConfig config_;
  bool initialised_;
} LightDriver;

// Constructor. Use &driver.base wherever a SensorDriver* is expected.
void lightDriverConstruct(LightDriver* self, const LightConfig* config);

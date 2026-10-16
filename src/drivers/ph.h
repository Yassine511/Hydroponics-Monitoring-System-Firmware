// ph4502c driver. Work package WP5 (Stage 3). Architecture §3, §7, §14.
//
// Switched 5 V rail. ADC mV -> divider recovery -> measured pH fit.
// 5 V module output becomes 3 V at the pin: above the ~2.45 V ADC range (§3).
// Timestamped, spaced filter window; Uncalibrated until 2+ buffer points.
//
// Implements the SensorDriver contract in drivers/sensor_driver.h.
#pragma once
#include "drivers/sensor_driver.h"
#include "hal/adc_channel.h"
#include "domain/calibration.h"

typedef struct PhConfig {
  AdcChannel* adc;
  uint32_t dividerTopOhm;     // BOARD_PH_DIVIDER_TOP_OHM
  uint32_t dividerBottomOhm;  // BOARD_PH_DIVIDER_BOTTOM_OHM
  LinearCalibration calibration;
} PhConfig;

// Members after `base` are private to ph.c.
typedef struct PhDriver {
  SensorDriver base;  // must stay first
  PhConfig config_;
  bool initialised_;
} PhDriver;

// Constructor. Use &driver.base wherever a SensorDriver* is expected.
void phDriverConstruct(PhDriver* self, const PhConfig* config);

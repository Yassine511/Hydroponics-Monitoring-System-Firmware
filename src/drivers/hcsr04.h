// hcsr04 driver. Work package WP4 (Stage 3). Architecture §7, §12, §14.
//
// Switched 5 V rail. 10 us trigger, bounded echo capture, 30 ms timeout.
// No echo => Timeout, never zero distance.
// preparePowerOff(): TRIG low, no driving into the unpowered module.
//
// Implements the SensorDriver contract in drivers/sensor_driver.h.
#pragma once
#include "drivers/sensor_driver.h"
#include "hal/pulse_capture.h"

typedef struct Hcsr04Config {
  PulseCapture* capture;
  uint32_t timeoutMs;  // CFG_HCSR04_TIMEOUT_MS
} Hcsr04Config;

// Members after `base` are private to hcsr04.c.
typedef struct Hcsr04Driver {
  SensorDriver base;  // must stay first
  Hcsr04Config config_;
  bool initialised_;
} Hcsr04Driver;

// Constructor. Use &driver.base wherever a SensorDriver* is expected.
void hcsr04DriverConstruct(Hcsr04Driver* self, const Hcsr04Config* config);

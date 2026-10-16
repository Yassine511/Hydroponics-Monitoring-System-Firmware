// ds18b20 driver. Work package WP2 (Stage 2). Architecture §7, §14.
//
// External 3.3 V supply; select a stable ROM ID; 12-bit async conversion.
// start() issues Convert T and returns; poll() until 750 ms elapsed.
// Check scratchpad CRC. Never accept the 85 C power-on value blindly.
// First frame has no result: report Warming, never zero.
//
// Implements the SensorDriver contract in drivers/sensor_driver.h.
#pragma once
#include "drivers/sensor_driver.h"
#include "hal/onewire_bus.h"

typedef struct Ds18b20Config {
  OneWireBus* bus;
} Ds18b20Config;

// Members after `base` are private to ds18b20.c.
typedef struct Ds18b20Driver {
  SensorDriver base;  // must stay first
  Ds18b20Config config_;
  bool initialised_;
} Ds18b20Driver;

// Constructor. Use &driver.base wherever a SensorDriver* is expected.
void ds18b20DriverConstruct(Ds18b20Driver* self, const Ds18b20Config* config);

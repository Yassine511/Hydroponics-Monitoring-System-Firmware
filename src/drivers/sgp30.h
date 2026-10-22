// sgp30 driver. Work package WP6 (Stage 2). Architecture §9, §10, §16 (D18).
// Datasheet: Sensirion SGP30 (E13 - no local copy yet).
//
// Retained 3.3 V supply, shared I2C bus with the BME280 (address 0x58).
// Produces Tvoc (ppb) and Co2eq (ppm, an estimate - never a CO2 measurement).
// Run iaq_init after every power-up/reset, then one measure_iaq per second
// from the absolute frame: start() sends the command, poll() reads 6 bytes
// once the conversion is done and checks the CRC-8 of each word.
// First 15 s after init return fixed values (400 ppm / 0 ppb): report
// Warming, never publish them. Restore a stored baseline when available;
// optional absolute-humidity compensation from a fresh BME280 sample, else
// DependencyInvalid. The SGP30 uses 16-bit commands, not register access, so
// the I2C HAL needs a raw write/read pair before this can be implemented.
//
// Implements the SensorDriver contract in drivers/sensor_driver.h.
#pragma once
#include "drivers/sensor_driver.h"
#include "hal/i2c_bus.h"

typedef struct Sgp30Config {
  I2cBus* bus;
  uint8_t address;  // BOARD_SGP30_I2C_ADDRESS
} Sgp30Config;

#define SGP30_METRICS 2  // 0 TVOC, 1 CO2eq

// Members after `base` are private to sgp30.c.
typedef struct Sgp30Driver {
  SensorDriver base;  // must stay first
  Sgp30Config config_;
  bool initialised_;
} Sgp30Driver;

// Constructor. Use &driver.base wherever a SensorDriver* is expected.
void sgp30DriverConstruct(Sgp30Driver* self, const Sgp30Config* config);

// mq7 driver. Work package WP6 (Stage 3). Architecture §14 (MQ-7 cadence exception).
//
// Switched 5 V rail. Separate heater/conditioning state machine.
// Datasheet: 60 s @5 V / 90 s @1.4 V heater phases, 48 h preheat.
// Publish raw signal + phase; quantitative CO only in a qualified phase.
// Gate: confirm module identity and heater access before going further.
//
// Implements the SensorDriver contract in drivers/sensor_driver.h.
#pragma once
#include "drivers/sensor_driver.h"
#include "hal/adc_channel.h"

typedef struct Mq7Config {
  AdcChannel* adc;
  uint32_t dividerTopOhm;     // BOARD_MQ7_DIVIDER_TOP_OHM
  uint32_t dividerBottomOhm;  // BOARD_MQ7_DIVIDER_BOTTOM_OHM
  int8_t heaterPin;           // BOARD_PIN_MQ7_HEATER (unassigned)
} Mq7Config;

// Members after `base` are private to mq7.c.
typedef struct Mq7Driver {
  SensorDriver base;  // must stay first
  Mq7Config config_;
  bool initialised_;
} Mq7Driver;

// Constructor. Use &driver.base wherever a SensorDriver* is expected.
void mq7DriverConstruct(Mq7Driver* self, const Mq7Config* config);

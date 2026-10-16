// Bring-up entry point for ds18b20 (WP2).  pio run -e test_ds18b20 -t upload -t monitor
// 3.3 V rail. Conversion up to 750 ms runs inside the 1 s period.
// Replaces src/main.c in this env only; links the production driver.
#include <Arduino.h>
#include <stdio.h>

#include "app_config.h"
#include "board_profile.h"
#include "bringup_harness.h"
#include "hal/onewire_bus.h"
#include "drivers/ds18b20.h"

static OneWireBus s_bus = ONE_WIRE_BUS_INIT;
static Ds18b20Config s_config = {&s_bus};
static Ds18b20Driver s_driver;
static BringupContext s_ctx = {&s_driver.base, CFG_FRAME_PERIOD_MS};

void setup(void) {
  ds18b20DriverConstruct(&s_driver, &s_config);
  delay(200);
  printf("\n=== bring-up: %s ===\n", sensorDriverName(&s_driver.base));
  oneWireBusBegin(&s_bus, BOARD_PIN_ONE_WIRE);
  bringupStart(&s_ctx);
}

void loop(void) { vTaskDelay(portMAX_DELAY); }

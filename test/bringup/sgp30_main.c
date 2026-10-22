// Bring-up entry point for sgp30 (WP6).  pio run -e test_sgp30 -t upload -t monitor
// Retained 3.3 V supply, shared I2C bus. Expect 2 readings every 1 s; the first
// 15 s after init are Warming (fixed 400 ppm / 0 ppb), never published.
// Replaces src/main.c in this env only; links the production driver.
#include <Arduino.h>
#include <stdio.h>

#include "app_config.h"
#include "board_profile.h"
#include "bringup_harness.h"
#include "hal/i2c_bus.h"
#include "drivers/sgp30.h"

static I2cBus s_i2c = I2C_BUS_INIT;
static Sgp30Config s_config = {&s_i2c, BOARD_SGP30_I2C_ADDRESS};
static Sgp30Driver s_driver;
static BringupContext s_ctx = {&s_driver.base, CFG_FRAME_PERIOD_MS};

void setup(void) {
  sgp30DriverConstruct(&s_driver, &s_config);
  delay(200);
  printf("\n=== bring-up: %s (SDA %d, SCL %d) ===\n", sensorDriverName(&s_driver.base),
         BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL);
  i2cBusBegin(&s_i2c, BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL, BOARD_I2C_CLOCK_HZ);
  printf("probe 0x%02X -> %s\n", BOARD_SGP30_I2C_ADDRESS,
         i2cBusProbe(&s_i2c, BOARD_SGP30_I2C_ADDRESS) ? "ACK" : "-");
  bringupStart(&s_ctx);
}

void loop(void) { vTaskDelay(portMAX_DELAY); }

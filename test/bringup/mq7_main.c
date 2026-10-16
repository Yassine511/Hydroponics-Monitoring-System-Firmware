// Bring-up entry point for mq7 (WP6).  pio run -e test_mq7 -t upload -t monitor
// Switched 5 V rail must be ON. Raw signal only until the heater gate is closed.
// Replaces src/main.c in this env only; links the production driver.
#include <Arduino.h>
#include <stdio.h>

#include "app_config.h"
#include "board_profile.h"
#include "bringup_harness.h"
#include "hal/adc_channel.h"
#include "drivers/mq7.h"

static AdcChannel s_adc = ADC_CHANNEL_INIT;
static Mq7Config s_config = {&s_adc, BOARD_MQ7_DIVIDER_TOP_OHM, BOARD_MQ7_DIVIDER_BOTTOM_OHM,
                             BOARD_PIN_MQ7_HEATER};
static Mq7Driver s_driver;
static BringupContext s_ctx = {&s_driver.base, CFG_FRAME_PERIOD_MS};

void setup(void) {
  mq7DriverConstruct(&s_driver, &s_config);
  delay(200);
  printf("\n=== bring-up: %s ===\n", sensorDriverName(&s_driver.base));
  adcChannelBegin(&s_adc, BOARD_PIN_MQ7_ADC);
  bringupStart(&s_ctx);
}

void loop(void) { vTaskDelay(portMAX_DELAY); }

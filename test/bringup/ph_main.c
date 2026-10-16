// Bring-up entry point for ph (WP5).  pio run -e test_ph -t upload -t monitor
// Switched 5 V rail must be ON. Check ADC headroom (§3) before trusting values.
// Replaces src/main.c in this env only; links the production driver.
#include <Arduino.h>
#include <stdio.h>

#include "app_config.h"
#include "board_profile.h"
#include "bringup_harness.h"
#include "hal/adc_channel.h"
#include "drivers/ph.h"

static AdcChannel s_adc = ADC_CHANNEL_INIT;
static PhConfig s_config = {&s_adc, BOARD_PH_DIVIDER_TOP_OHM, BOARD_PH_DIVIDER_BOTTOM_OHM,
                            {1.0f, 0.0f, 0, false}};  // uncalibrated
static PhDriver s_driver;
static BringupContext s_ctx = {&s_driver.base, CFG_FRAME_PERIOD_MS};

void setup(void) {
  phDriverConstruct(&s_driver, &s_config);
  delay(200);
  printf("\n=== bring-up: %s ===\n", sensorDriverName(&s_driver.base));
  adcChannelBegin(&s_adc, BOARD_PIN_PH_ADC);
  bringupStart(&s_ctx);
}

void loop(void) { vTaskDelay(portMAX_DELAY); }

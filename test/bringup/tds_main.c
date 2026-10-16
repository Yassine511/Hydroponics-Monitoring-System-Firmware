// Bring-up entry point for tds (WP5).  pio run -e test_tds -t upload -t monitor
// Switched 5 V rail must be ON. No water temperature here => uncompensated.
// Replaces src/main.c in this env only; links the production driver.
#include <Arduino.h>
#include <stdio.h>

#include "app_config.h"
#include "board_profile.h"
#include "bringup_harness.h"
#include "hal/adc_channel.h"
#include "drivers/tds.h"

static AdcChannel s_adc = ADC_CHANNEL_INIT;
static TdsConfig s_config = {&s_adc, {1.0f, 0.0f, 0, false}};  // uncalibrated
static TdsDriver s_driver;
static BringupContext s_ctx = {&s_driver.base, CFG_FRAME_PERIOD_MS};

void setup(void) {
  tdsDriverConstruct(&s_driver, &s_config);
  delay(200);
  printf("\n=== bring-up: %s ===\n", sensorDriverName(&s_driver.base));
  adcChannelBegin(&s_adc, BOARD_PIN_TDS_ADC);
  bringupStart(&s_ctx);
}

void loop(void) { vTaskDelay(portMAX_DELAY); }

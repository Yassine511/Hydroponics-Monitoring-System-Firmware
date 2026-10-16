// Bring-up entry point for light (WP3).  pio run -e test_light -t upload -t monitor
// 3.3 V rail. Characterise dark/bright and saturation.
// Replaces src/main.c in this env only; links the production driver.
#include <Arduino.h>
#include <stdio.h>

#include "app_config.h"
#include "board_profile.h"
#include "bringup_harness.h"
#include "hal/adc_channel.h"
#include "drivers/light.h"

static AdcChannel s_adc = ADC_CHANNEL_INIT;
static LightConfig s_config = {&s_adc};
static LightDriver s_driver;
static BringupContext s_ctx = {&s_driver.base, CFG_FRAME_PERIOD_MS};

void setup(void) {
  lightDriverConstruct(&s_driver, &s_config);
  delay(200);
  printf("\n=== bring-up: %s ===\n", sensorDriverName(&s_driver.base));
  adcChannelBegin(&s_adc, BOARD_PIN_LIGHT_ADC);
  bringupStart(&s_ctx);
}

void loop(void) { vTaskDelay(portMAX_DELAY); }

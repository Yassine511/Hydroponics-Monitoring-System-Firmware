// Bring-up entry point for hcsr04 (WP4).  pio run -e test_hcsr04 -t upload -t monitor
// Switched 5 V rail must be ON. Verify the 1k/2k ECHO divider first.
// Replaces src/main.c in this env only; links the production driver.
#include <Arduino.h>
#include <stdio.h>

#include "app_config.h"
#include "board_profile.h"
#include "bringup_harness.h"
#include "hal/pulse_capture.h"
#include "drivers/hcsr04.h"

static PulseCapture s_capture = PULSE_CAPTURE_INIT;
static Hcsr04Config s_config = {&s_capture, CFG_HCSR04_TIMEOUT_MS};
static Hcsr04Driver s_driver;
static BringupContext s_ctx = {&s_driver.base, CFG_FRAME_PERIOD_MS};

void setup(void) {
  hcsr04DriverConstruct(&s_driver, &s_config);
  delay(200);
  printf("\n=== bring-up: %s ===\n", sensorDriverName(&s_driver.base));
  pulseCaptureBegin(&s_capture, BOARD_PIN_HCSR04_TRIG, BOARD_PIN_HCSR04_ECHO);
  bringupStart(&s_ctx);
}

void loop(void) { vTaskDelay(portMAX_DELAY); }

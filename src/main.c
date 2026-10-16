// Production entry point - bootstrap ONLY (Firmware Architecture §5).
// No sensing, reconnect or policy work belongs here or in loop().
#include <Arduino.h>

#include "app_config.h"
#include "board_profile.h"
#include "diagnostics/logger.h"
#include "hal/arduino_entry.h"
#include "hal/gpio_safe.h"
#include "services/snapshot_store.h"

void setup(void) {
  // 1. Safe output defaults before anything else.
  gpioApplySafeDefaults();

  // Serial console: UART0 at 115200 (CONFIG_ESP_CONSOLE_UART_BAUDRATE).
  logInit();

  // 2. Validate the board profile. Unassigned gates => not production ready.
  BoardValidation board = boardProfileValidate();
  logPrintf(LOG_LEVEL_INFO, "hydroponics firmware - stage 1 skeleton");
  logPrintf(LOG_LEVEL_INFO, "power sense %s, relay %s, production ready %s",
            board.powerSenseAssigned ? "yes" : "NO",
            board.relayAssigned ? "yes" : "NO",
            board.productionReady ? "yes" : "NO");

  // 3. Static queues.
  if (!snapshotStoreInit()) logPrintf(LOG_LEVEL_ERROR, "snapshot store init failed");

  // TODO(Stage 4+): start Safety and PowerCoordinator, sample the initial
  // power state, grant the acquisition lease, then start transports without
  // waiting for an access point. Init failure => reported safe fault state.
}

void loop(void) {
  // Compatibility stub: the Arduino loop task blocks forever.
  vTaskDelay(portMAX_DELAY);
}

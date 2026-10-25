// Production entry point - bootstrap ONLY (Firmware Architecture §5).
// No sensing, reconnect or policy work belongs here or in loop().
// Order (RTOS Architecture §5): safe outputs, logger, board gate, queues,
// Safety BEFORE acquisition (the first alarm event always has a receiver),
// then Supervisor. A failed step is logged and boot continues; the relay is
// already de-energised and only Safety can change it.
#include <Arduino.h>

#include "board_profile.h"
#include "diagnostics/logger.h"
#include "hal/arduino_entry.h"
#include "hal/gpio_safe.h"
#include "services/acquisition.h"
#include "services/safety.h"
#include "services/snapshot_store.h"
#include "services/supervisor.h"

void setup(void) {
  // 1. Safe output defaults before anything else.
  gpioApplySafeDefaults();

  // 2. Serial console: UART0 at 115200 (CONFIG_ESP_CONSOLE_UART_BAUDRATE).
  logInit();
  logPrintf(LOG_LEVEL_INFO, "hydroponics firmware - stage 4");

  // 3. Board gate: pins assigned and power sense measured. It does not mean
  // that power-mode handling (WP9) is running.
  BoardValidation board = boardProfileValidate();
  logPrintf(LOG_LEVEL_INFO, "board gate: power sense %s, relay %s -> %s",
            board.powerSenseAssigned ? "yes" : "NO",
            board.relayAssigned ? "yes" : "NO",
            board.productionReady ? "pass" : "FAIL");

  // 4. Static queues.
  if (!snapshotStoreInit()) logPrintf(LOG_LEVEL_ERROR, "snapshot store init failed");

  // 5-7. Tasks. Power coordination (WP9) and transports (WP8) come later.
  if (!safetyInit()) logPrintf(LOG_LEVEL_ERROR, "safety init failed - relay stays OFF");
  if (!acquisitionInit()) logPrintf(LOG_LEVEL_ERROR, "acquisition init failed");
  if (!supervisorInit()) logPrintf(LOG_LEVEL_ERROR, "supervisor init failed");
}

void loop(void) {
  // Compatibility stub: the Arduino loop task blocks forever.
  vTaskDelay(portMAX_DELAY);
}

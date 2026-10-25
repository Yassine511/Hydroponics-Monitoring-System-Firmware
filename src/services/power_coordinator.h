// PowerCoordinator: power-mode state machine and acquisition lease owner.
// Work package WP9 (Stage 6). Architecture §11, §12.
//
// Power sense: GPIO8, measured 25 Oct 2026 (BOARD_POWER_SENSE_VERIFIED). The
// ISR must disable itself on the first edge and the task must re-read the
// level after BOARD_POWER_SENSE_QUALIFY_MS (board_profile.h).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef uint8_t PowerMode;
enum {
  POWER_MODE_UNKNOWN = 0,
  POWER_MODE_NORMAL,
  POWER_MODE_TRANSITION_TO_LOW,
  POWER_MODE_LOW_POWER,
  POWER_MODE_TRANSITION_TO_NORMAL,
  POWER_MODE_DEGRADED
};

bool powerCoordinatorInit(void);
PowerMode powerModeCurrent(void);
const char* powerModeName(PowerMode m);

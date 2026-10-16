// PowerCoordinator: power-mode state machine and acquisition lease owner.
// Work package WP9 (Stage 6). Architecture §11, §12.
//
// Gate: the external-power-present GPIO is unassigned (board_profile.h).
// Until it is commissioned, bench builds run in Normal and report
// productionReady = false.
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

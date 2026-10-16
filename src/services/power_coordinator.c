#include "services/power_coordinator.h"

// TODO(WP9): sense ISR, debounce, QUIESCE/RELEASED/GRANT protocol, mode mailbox.

bool powerCoordinatorInit(void) { return false; }

PowerMode powerModeCurrent(void) { return POWER_MODE_UNKNOWN; }

const char* powerModeName(PowerMode m) {
  switch (m) {
    case POWER_MODE_UNKNOWN: return "UNKNOWN";
    case POWER_MODE_NORMAL: return "NORMAL";
    case POWER_MODE_TRANSITION_TO_LOW: return "TRANSITION_TO_LOW";
    case POWER_MODE_LOW_POWER: return "LOW_POWER";
    case POWER_MODE_TRANSITION_TO_NORMAL: return "TRANSITION_TO_NORMAL";
    case POWER_MODE_DEGRADED: return "DEGRADED";
  }
  return "?";
}

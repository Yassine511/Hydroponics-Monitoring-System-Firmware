#include "services/acquisition.h"

// TODO(WP7): absolute-deadline multi-rate scheduler (1 s frame, 250 ms BME280),
// lease handling, snapshot publishing and immediate rule evaluation.

bool acquisitionInit(void) { return false; }

bool acquisitionGrant(uint32_t generation, uint8_t allowedRailMask) {
  (void)generation;
  (void)allowedRailMask;
  return false;
}

bool acquisitionRequestQuiesce(uint32_t generation) {
  (void)generation;
  return false;
}

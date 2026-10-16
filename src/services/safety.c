#include "services/safety.h"

// TODO(WP7): pending-alarm store, detail queue, processing order of §10.

bool safetyInit(void) { return false; }

bool safetySubmitEvent(const AlarmEvent* event) {
  (void)event;
  return false;
}

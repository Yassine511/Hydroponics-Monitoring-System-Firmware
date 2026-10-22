#include "hal/gpio_safe.h"

#include "board_profile.h"

void gpioApplySafeDefaults(void) {
  // Relay 1 is GPIO15, active-high; its 10 kOhm gate pull-down keeps the coil
  // off until this runs. The load-safe state is still a commissioning gate
  // (§12, §20), so nothing is driven yet.
  // TODO(WP7): drive relays to their verified de-energised level here.
  // TODO(WP4): hold HC-SR04 TRIG low once the non-back-feeding config is verified.
}

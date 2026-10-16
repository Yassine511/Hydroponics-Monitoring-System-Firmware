#include "hal/gpio_safe.h"

#include "board_profile.h"

void gpioApplySafeDefaults(void) {
  // Relay pin, polarity and load-safe state are unresolved commissioning
  // gates (§10, §18). Nothing is driven until board_profile.h assigns them.
  // TODO(WP7): drive relays to their verified de-energised level here.
  // TODO(WP4): hold HC-SR04 TRIG low once the non-back-feeding config is verified.
}

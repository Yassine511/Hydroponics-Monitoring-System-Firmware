// Grow-lamp decision from the DFR0026 light level (bring-up, ahead of WP7).
// Pure C, host-tested. Decides only: the caller drives the relay.
//
// Hysteresis: ON below onBelowMv, OFF above offAboveMv, unchanged in between.
// Dwell: after a change the state is held for minOnMs / minOffMs, which also
// covers boot (init counts as a change to OFF), so a reset loop cannot cycle
// the lamp. The sensor may see the lamp it controls: offAboveMv must sit above
// the "night + lamp ON" reading measured at the final sensor position.
//
// Readings: OK and UNCALIBRATED (the light driver publishes uncalibrated mV)
// are used; a saturated one counts as bright. Anything else holds the state,
// and faultsToOff such readings in a row force OFF at once (safe state: relay
// de-energised, lamp on NO contact).
//
// Bring-up deviation from §10 (reading.h): this acts on UNCALIBRATED mV and
// does not check reading age. A stalled sensor (no READY result reaches the
// caller) freezes the last state, possibly ON. WP7 must either calibrate
// light or accept relative mV explicitly, and add a staleness -> OFF rule.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "domain/reading.h"
#include "domain/types.h"

typedef struct LampControlConfig {
  float onBelowMv;     // must be < offAboveMv
  float offAboveMv;
  uint32_t minOnMs;
  uint32_t minOffMs;
  uint8_t faultsToOff;  // >= 1
} LampControlConfig;

typedef struct LampControl {
  LampControlConfig config;
  bool on;
  bool waiting;  // the level asks for a change the dwell is still holding back
  MonoMs lastChangeMs;
  uint8_t faults;  // unusable readings in a row
} LampControl;

// Starts OFF, as if it had just switched OFF at nowMs.
void lampControlInit(LampControl* self, const LampControlConfig* config, MonoMs nowMs);

// Feed one light reading; returns the lamp state to drive.
bool lampControlUpdate(LampControl* self, const Reading* light, MonoMs nowMs);

// Time in the current state.
static inline uint32_t lampControlHeldMs(const LampControl* self, MonoMs nowMs) {
  return (uint32_t)(nowMs - self->lastChangeMs);
}

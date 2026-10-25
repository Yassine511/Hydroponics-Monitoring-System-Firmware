// Safety decision logic, pure C (§12). Work package WP7 (Stage 4).
// services/safety.c only waits, copies inputs in, calls safetyCoreStep() and
// writes the relay; every decision is here so it is host-tested with a fake
// clock.
//
// One step = one Safety wake, in the §12 order:
//   1 power validity      (always valid until WP9)
//   2 alarm events        TRIP latches (latching rules) / sets active; CLEAR
//                         releases only non-latching rules
//   3 freshness           new light result -> lamp_control; light result
//                         older than lightStaleMs -> lamp forced OFF; stale
//                         inputs of enabled rules are reported
//   4 commands            none until WP8
//   5 output              relay = auto enabled AND lamp ON AND no interlock
// Interlock: any enabled rule with action DEENERGIZE on ACTUATOR_RELAY1 that
// is latched or active, or that tripped in this step's events (so a TRIP
// followed by a CLEAR in the same batch still de-energises once).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "domain/alarm.h"
#include "domain/lamp_control.h"
#include "domain/reading.h"

#define SAFETY_MAX_RULES 8

typedef uint8_t LampReason;
enum {
  LAMP_REASON_NO_DATA = 0,  // no light result yet: lamp stays OFF
  LAMP_REASON_LEVEL,        // following the light level (ON or OFF)
  LAMP_REASON_DWELL,        // level asks for a change, dwell still holding
  LAMP_REASON_FAULT,        // unusable light results
  LAMP_REASON_STALE,        // light result too old: forced OFF
  LAMP_REASON_INTERLOCK,    // alarm interlock: relay de-energised
  LAMP_REASON_DISABLED      // POLICY_LAMP_AUTO_ENABLED = 0
};

typedef struct SafetyConfig {
  bool lampAutoEnabled;
  LampControlConfig lamp;
  uint32_t lightStaleMs;
} SafetyConfig;

typedef struct SafetyRuleState {
  bool active;        // last decision was TRIP and no CLEAR since
  bool latched;       // latching rule tripped; only safetyCoreResetLatch() clears
  uint32_t trips;     // TRIP events seen
  float worst;        // most extreme tripping value
  MonoMs firstTripMs; // sample time of the first TRIP (0 = never)
} SafetyRuleState;

typedef struct SafetyCore {
  SafetyConfig config;
  const AlarmRule* rules;
  uint8_t ruleCount;
  SafetyRuleState rule[SAFETY_MAX_RULES];
  LampControl lamp;
  bool haveLight;
  MonoMs lastLightEndMs;  // intervalEndMs of the last light result fed to lamp
} SafetyCore;

typedef struct SafetyInputs {
  const Snapshot* snap;       // NULL until the first publish
  const AlarmEvent* events;   // drained this wake, in arrival order
  uint8_t eventCount;
  MonoMs now;
} SafetyInputs;

typedef struct SafetyOutputs {
  bool relayOn;          // what relay 1 must be
  bool lampWantsOn;      // lamp policy decision before interlock / enable
  LampReason lampReason;
  bool interlock;
  uint16_t latchedMask;  // bit per rule index
  uint16_t activeMask;
  uint16_t staleMask;    // enabled rules whose input is not fresh and OK
} SafetyOutputs;

// rules may be NULL with ruleCount 0. At most SAFETY_MAX_RULES are used.
void safetyCoreInit(SafetyCore* self, const SafetyConfig* config, const AlarmRule* rules,
                    uint8_t ruleCount, MonoMs now);

void safetyCoreStep(SafetyCore* self, const SafetyInputs* in, SafetyOutputs* out);

// Explicit reset of a latched rule (WP8 command). Refused (false) while the
// rule is still active: clearing needs a valid recovery observation first.
bool safetyCoreResetLatch(SafetyCore* self, uint8_t ruleId);

const char* lampReasonName(LampReason r);

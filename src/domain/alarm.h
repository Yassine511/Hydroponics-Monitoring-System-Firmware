// Alarm rules and events (§10). Pure C.
// Work package WP7 (Stage 4). Thresholds are NOT commissioned: the 40/50 degC
// values in the original request are examples only (§1).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "domain/reading.h"

typedef uint8_t Severity;
enum { SEVERITY_INFO = 0, SEVERITY_WARNING, SEVERITY_CRITICAL };

typedef uint8_t Comparison;
enum { COMPARISON_ABOVE = 0, COMPARISON_BELOW };

typedef uint8_t ActuatorAction;
enum { ACTUATOR_ACTION_NONE = 0, ACTUATOR_ACTION_DEENERGIZE, ACTUATOR_ACTION_ENERGIZE };

typedef struct AlarmRule {
  uint8_t ruleId;
  MetricId metric;
  Comparison comparison;
  float tripThreshold;
  float clearThreshold;   // hysteresis
  uint32_t persistMs;     // 0 = trip on one valid sample
  Severity severity;
  bool latching;          // requires explicit reset after clearing
  uint8_t actuatorId;
  ActuatorAction action;
} AlarmRule;

typedef uint8_t RuleDecision;
enum {
  RULE_DECISION_NOT_ELIGIBLE = 0,
  RULE_DECISION_NO_CHANGE,
  RULE_DECISION_TRIP,
  RULE_DECISION_CLEAR
};

// Event submitted by acquisition to Safety immediately after a sample (§4).
typedef struct AlarmEvent {
  uint8_t ruleId;
  RuleDecision decision;
  Reading sample;
} AlarmEvent;

// Pure threshold evaluation for one sample. No side effects.
// TODO(WP7): implement hysteresis + persistence; currently a stub.
RuleDecision evaluateRule(const AlarmRule* rule, const Reading* sample,
                          bool currentlyTripped);

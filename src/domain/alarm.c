#include "domain/alarm.h"

RuleDecision evaluateRule(const AlarmRule* rule, const Reading* sample,
                          bool currentlyTripped) {
  (void)rule;
  (void)currentlyTripped;
  if (!readingIsActionable(sample)) return RULE_DECISION_NOT_ELIGIBLE;
  // TODO(WP7): trip/clear with hysteresis and persistMs (§10).
  return RULE_DECISION_NO_CHANGE;
}

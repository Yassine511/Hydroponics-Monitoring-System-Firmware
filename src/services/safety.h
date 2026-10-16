// Safety: latched alarms, interlocks, sole relay-output owner.
// Work package WP7 (Stage 4). Architecture §9, §10.
//
// Core 0, priority 6. Never waits on network I/O or NVS. Automatic actuation
// stays disabled until relay polarity/load policy is commissioned (§10).
#pragma once
#include <stdbool.h>
#include "domain/alarm.h"

bool safetyInit(void);

// Zero-wait. On a full detail queue the sticky alarm state is still kept
// and the overflow is counted (§9). Safe from any task.
bool safetySubmitEvent(const AlarmEvent* event);

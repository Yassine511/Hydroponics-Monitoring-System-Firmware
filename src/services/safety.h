// Safety: latched alarms, interlocks, sole relay-output owner.
// Work package WP7 (Stage 4). Architecture §11, §12; decisions in
// domain/safety_core (host-tested). This file only waits, copies and writes.
//
// Task: core 0, priority 6 (cfgTaskSpec(TASK_ID_SAFETY)). Wakes on an alarm
// event or every CFG_SAFETY_HEALTH_CHECK_MS. Never waits on network I/O,
// NVS or a mutex. Grow-lamp auto control follows POLICY_LAMP_AUTO_ENABLED.
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdint.h>

#include "domain/alarm.h"
#include "domain/safety_core.h"

// Creates the detail queue, status mailbox and the task. Relay stays OFF.
bool safetyInit(void);

// Zero-wait, safe from any task (not from an ISR). The per-rule sticky slot
// is always updated; the detail queue may be full, which is counted (§9).
// Wakes Safety. Returns false only if the detail record was dropped.
bool safetySubmitEvent(const AlarmEvent* event);

// Published after every Safety pass (length-1 mailbox, §8).
typedef struct SafetyStatus {
  MonoMs atMs;
  bool relayOn;
  bool lampWantsOn;
  LampReason lampReason;
  bool interlock;
  uint16_t latchedMask;
  uint16_t activeMask;
  uint16_t staleMask;
  uint32_t detailOverflow;  // alarm detail records dropped since boot
  uint32_t passes;
} SafetyStatus;

// Zero-wait copy of the latest status. false before the first pass.
bool safetyStatusPeek(SafetyStatus* out);

// For Supervisor's stack check. NULL before safetyInit().
TaskHandle_t safetyTaskHandle(void);

#ifdef FW_ALARM_LATENCY_TEST
// Test build only: acquisition records the esp_timer time (us) at which it
// submitted an event; Safety measures until it has applied the action.
void safetyLatencyMarkSubmit(void);
#endif

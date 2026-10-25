// Acquisition engine: the one serial owner of all sensor buses.
// Work package WP7 (Stage 4). Architecture §6, §7, §9, §12.
//
// Stage 4 (decision D15): ONE worker, AcqNormal (core 1, priority 4), owns
// every driver and bus. It runs an absolute-deadline schedule (1 s frame +
// BME280 every 250 ms, domain/schedule), stamps each completed reading,
// evaluates the alarm rules for it, submits TRIP/CLEAR to Safety at once and
// publishes the snapshot after every completed reading and at each frame.
// The second worker and the lease protocol arrive with WP9 (§14). Until then
// readings are stamped with the rail epoch but NOT gated on rail validity:
// pH, TDS and HC-SR04 are read as if the external 5 V were present, and a
// power cut shows up only as their own UNPOWERED / FAULT / TIMEOUT verdicts.
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdint.h>

#include "domain/types.h"

bool acquisitionInit(void);  // construct drivers, create AcqNormal; HALs begin in the task

// Called only by PowerCoordinator (§14 lease protocol). Stubs until WP9.
bool acquisitionGrant(uint32_t generation, uint8_t allowedRailMask);
bool acquisitionRequestQuiesce(uint32_t generation);

// Counters for Supervisor (copied under a lock; zero-wait).
typedef struct AcquisitionStats {
  uint32_t frames;
  uint32_t frameLate;      // frame releases more than SCHEDULE_LATE_TOLERANCE_MS late
  uint32_t frameSkipped;
  uint32_t frameJitterMaxMs;
  uint32_t bmeLate;
  uint32_t bmeSkipped;
  uint32_t guardTimeouts;  // steps cancelled by the frame / slot guard
  uint32_t driverFailures; // start/poll results other than OK/PENDING/READY
  uint32_t ruleEvents;     // TRIP + CLEAR submitted to Safety
} AcquisitionStats;

void acquisitionGetStats(AcquisitionStats* out);

// For Supervisor's stack check. NULL before acquisitionInit().
TaskHandle_t acquisitionTaskHandle(void);

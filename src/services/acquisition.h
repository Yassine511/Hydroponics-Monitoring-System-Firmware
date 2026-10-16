// Acquisition engine: the one serial owner of all sensor buses.
// Work package WP7 (Stage 4). Architecture §6, §7, §12.
//
// Two pre-created workers run the same engine: AcqNormal (core 1) and
// AcqLowPower (core 0). Only the worker holding the lease (generation +
// allowed rail mask) touches drivers; the inactive one blocks on its grant.
// Each completed valid sample is checked against rules immediately and
// alarms go to Safety before the snapshot boundary (§4).
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool acquisitionInit(void);  // create workers (blocked) and grant queues

// Called only by PowerCoordinator (§12 lease protocol).
bool acquisitionGrant(uint32_t generation, uint8_t allowedRailMask);
bool acquisitionRequestQuiesce(uint32_t generation);

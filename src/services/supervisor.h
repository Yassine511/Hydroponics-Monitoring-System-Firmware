// Supervisor: progress deadlines, counters, stack/heap health, every 1 s.
// Work package WP10 (Stage 4). Architecture §16, §18.
//
// Core 0, priority 1: it only observes and reports. It never resets the task
// watchdog for another task (AcqNormal and Safety reset it for themselves).
//   every 1 s   progress age per watched task (warn once per stall episode,
//               "progressing again" on recovery); loss/lateness counters
//               (frame/BME280 late and skipped, guard timeouts, alarm detail
//               overflow, log drops): one WARN line with the deltas
//   every 10 s  stack high-water marks and internal heap: WARN when low
//   every 60 s  the same as an INFO report
//   every 5 s   status line: readings, lamp, counters, "health OK" or
//               "DEGRADED <first cause>" for the last 5 s
//   boot        reset reason (a TASK_WDT here means a watched task stalled)
#pragma once
#include <stdbool.h>

#define SUPERVISOR_STATUS_EVERY_S 5
#define SUPERVISOR_MEMORY_EVERY_S 10
#define SUPERVISOR_MEMORY_REPORT_EVERY_S 60

// Progress deadlines. Safety passes every 100 ms, AcqNormal at least every
// 250 ms; the task watchdog (5 s) fires well after these warnings.
#define SUPERVISOR_SAFETY_DEADLINE_MS 1000u
#define SUPERVISOR_ACQ_DEADLINE_MS 2000u
#define SUPERVISOR_LOGGER_DEADLINE_MS 2000u
// Configuration wakes every 500 ms and marks progress while it waits for a
// flash-gate slot or for acquisition to apply a change (<= 3 s).
#define SUPERVISOR_CONFIG_DEADLINE_MS 3000u
// BleTransport wakes every 50 ms; one frame send retries for at most ~200 ms per chunk.
#define SUPERVISOR_BLE_DEADLINE_MS 2000u
// WifiMqtt wakes every 50 ms; every socket call is non-blocking.
#define SUPERVISOR_WIFI_DEADLINE_MS 2000u
// PowerCoord wakes at least every 100 ms (WP9).
#define SUPERVISOR_POWER_DEADLINE_MS 1000u
#define SUPERVISOR_BOOT_GRACE_MS 5000u

#define SUPERVISOR_STACK_WARN_BYTES 512u  // §16: keep >= 25 % headroom; warn below this
#define SUPERVISOR_HEAP_WARN_BYTES 20000u // internal heap minimum ever

bool supervisorInit(void);

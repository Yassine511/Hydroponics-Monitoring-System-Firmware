#include "services/supervisor.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "diagnostics/logger.h"
#include "diagnostics/progress.h"
#include "domain/reading.h"
#include "hal/clock.h"
#include "services/acquisition.h"
#include "services/configuration.h"
#include "services/power_coordinator.h"
#include "services/safety.h"
#include "services/snapshot_store.h"
#include "transports/ble_transport.h"
#include "transports/wifi_mqtt.h"

static StaticTask_t s_tcb;
static StackType_t s_stack[3 * 1024];  // cfgTaskSpec(TASK_ID_SUPERVISOR)->stackBytes
static TaskHandle_t s_task = NULL;
static Snapshot s_snap;  // static: 584 B does not belong on a 3 KiB stack

// ---- Progress deadlines (§18) ---------------------------------------------------
typedef struct Watched {
  TaskId id;
  const char* name;
  uint32_t deadlineMs;   // no progress for longer than this = stalled
  bool onlyWhenBusy;     // SerialLogger: stalled only if records are waiting
  bool stalled;          // current episode, warned once
} Watched;

static Watched s_watched[] = {
    {TASK_ID_SAFETY, "safety", SUPERVISOR_SAFETY_DEADLINE_MS, false, false},
    {TASK_ID_ACQUISITION_NORMAL, "acq", SUPERVISOR_ACQ_DEADLINE_MS, false, false},
    {TASK_ID_SERIAL_LOGGER, "logger", SUPERVISOR_LOGGER_DEADLINE_MS, true, false},
    {TASK_ID_CONFIGURATION, "cfg", SUPERVISOR_CONFIG_DEADLINE_MS, false, false},
    {TASK_ID_BLE_TRANSPORT, "ble", SUPERVISOR_BLE_DEADLINE_MS, false, false},
    {TASK_ID_WIFI_MQTT, "wifi", SUPERVISOR_WIFI_DEADLINE_MS, false, false},
    {TASK_ID_POWER_COORDINATOR, "power", SUPERVISOR_POWER_DEADLINE_MS, false, false},
};
#define WATCHED_COUNT (sizeof(s_watched) / sizeof(s_watched[0]))

// Health summary shown in the status line: OK until something degraded in
// the last status period.
static char s_degraded[48];

static void markDegraded(const char* what) {
  if (s_degraded[0] == '\0') snprintf(s_degraded, sizeof(s_degraded), "%s", what);
}

static void checkProgress(MonoMs now) {
  for (size_t i = 0; i < WATCHED_COUNT; ++i) {
    Watched* w = &s_watched[i];
    TaskId id = w->id;
    if (id == TASK_ID_ACQUISITION_NORMAL) {
      // WP9: whichever worker holds the lease owes progress; nobody during
      // the hand-over (PowerCoordinator reports a stuck one as DEGRADED).
      id = acquisitionActiveTaskId();
      if (id == TASK_ID_COUNT) continue;
    }
    const MonoMs last = progressLastMs(id);  // 0 = never stamped: age from boot
    const MonoMs age = now > last ? now - last : 0;
    const bool idleLogger = w->onlyWhenBusy && logQueueDepth() == 0;
    const bool late = !idleLogger && now > SUPERVISOR_BOOT_GRACE_MS && age > w->deadlineMs;
    if (late) {
      markDegraded(w->name);
      if (!w->stalled) {
        w->stalled = true;
        logPrintf(LOG_LEVEL_WARN, "sup: %s no progress for %lu ms (deadline %lu)", w->name,
                  (unsigned long)age, (unsigned long)w->deadlineMs);
      }
    } else if (w->stalled) {
      w->stalled = false;
      logPrintf(LOG_LEVEL_INFO, "sup: %s progressing again", w->name);
    }
  }
}

// ---- Counters: warn when a loss / lateness counter goes up ------------------------
typedef struct Counters {
  uint32_t frameLate, frameSkipped, bmeLate, bmeSkipped, guard, alarmOverflow, logDrops;
} Counters;

static Counters s_prev;

static void readCounters(Counters* c) {
  AcquisitionStats a;
  acquisitionGetStats(&a);
  SafetyStatus st;
  memset(&st, 0, sizeof(st));
  safetyStatusPeek(&st);
  c->frameLate = a.frameLate;
  c->frameSkipped = a.frameSkipped;
  c->bmeLate = a.bmeLate;
  c->bmeSkipped = a.bmeSkipped;
  c->guard = a.guardTimeouts;
  c->alarmOverflow = st.detailOverflow;
  c->logDrops = logDroppedCount();
}

static void checkCounters(void) {
  Counters c;
  readCounters(&c);
  if (memcmp(&c, &s_prev, sizeof(c)) != 0) {
    // One line per second at most; deltas since the last check.
    static char line[LOG_TEXT_LEN];  // task-private: keeps the 3 KiB stack free
    size_t used = (size_t)snprintf(line, sizeof(line), "sup: counters up:");
    const struct { const char* name; uint32_t now, before; } items[] = {
        {"frameLate", c.frameLate, s_prev.frameLate},
        {"frameSkip", c.frameSkipped, s_prev.frameSkipped},
        {"bmeLate", c.bmeLate, s_prev.bmeLate},
        {"bmeSkip", c.bmeSkipped, s_prev.bmeSkipped},
        {"guard", c.guard, s_prev.guard},
        {"alarmOverflow", c.alarmOverflow, s_prev.alarmOverflow},
        {"logDrops", c.logDrops, s_prev.logDrops},
    };
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]) && used < sizeof(line); ++i) {
      if (items[i].now == items[i].before) continue;
      const int n = snprintf(line + used, sizeof(line) - used, " %s +%lu", items[i].name,
                             (unsigned long)(items[i].now - items[i].before));
      if (n > 0) used += (size_t)n;
      markDegraded(items[i].name);
    }
    logPrintf(LOG_LEVEL_WARN, "%s", line);
    s_prev = c;
  }
}

// ---- Stacks and heap ------------------------------------------------------------------
static uint32_t stackFree(TaskHandle_t t) {
  // ESP-IDF: StackType_t is a byte, so the high-water mark is in bytes.
  return t != NULL ? (uint32_t)uxTaskGetStackHighWaterMark(t) : 0;
}

static void checkMemory(bool report) {
  const struct { const char* name; TaskHandle_t handle; } tasks[] = {
      {"safety", safetyTaskHandle()},
      {"acq", acquisitionNormalTaskHandle()},
      {"log", (TaskHandle_t)logTaskHandle()},
      {"sup", s_task},
      {"cfg", (TaskHandle_t)configurationTaskHandle()},
      {"ble", (TaskHandle_t)bleTransportTaskHandle()},
      {"wifi", (TaskHandle_t)wifiMqttTaskHandle()},
      {"acqlp", acquisitionLowPowerTaskHandle()},
      {"pwr", (TaskHandle_t)powerCoordinatorTaskHandle()},
  };
  uint32_t freeB[9];
  bool low = false;
  for (size_t i = 0; i < 9; ++i) {
    freeB[i] = stackFree(tasks[i].handle);
    if (tasks[i].handle != NULL && freeB[i] < SUPERVISOR_STACK_WARN_BYTES) {
      low = true;
      markDegraded("stack");
    }
  }
  const uint32_t heapFree = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t heapMin = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const bool heapLow = heapMin < SUPERVISOR_HEAP_WARN_BYTES;
  if (heapLow) markDegraded("heap");
  if (report || low || heapLow) {
    logPrintf(low || heapLow ? LOG_LEVEL_WARN : LOG_LEVEL_INFO,
              // Fits LOG_TEXT_LEN (128) with every value at 6 digits.
              "sup: stack B saf %lu acq %lu log %lu sup %lu cfg %lu ble %lu wifi %lu | heap %lu min %lu big %lu",
              (unsigned long)freeB[0], (unsigned long)freeB[1], (unsigned long)freeB[2],
              (unsigned long)freeB[3], (unsigned long)freeB[4], (unsigned long)freeB[5], (unsigned long)freeB[6], (unsigned long)heapFree, (unsigned long)heapMin,
              (unsigned long)largest);
    logPrintf(low ? LOG_LEVEL_WARN : LOG_LEVEL_INFO, "sup: stack B acqlp %lu pwr %lu", (unsigned long)freeB[7],
              (unsigned long)freeB[8]);
  }
}

// ---- Status line ------------------------------------------------------------------------
static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

// "airT 23.1" for a usable value, "airT FAULT" otherwise.
static int cell(char* buf, size_t len, const char* label, const Reading* r, int decimals) {
  if (r->health == HEALTH_OK || r->health == HEALTH_UNCALIBRATED) {
    return snprintf(buf, len, " %s %.*f%s", label, decimals, (double)r->value,
                    r->health == HEALTH_UNCALIBRATED ? "~" : "");
  }
  // Failures show their raw value when they have one, e.g. HC-SR04 TIMEOUT
  // with raw = us elapsed (ECHO rose, no echo back) vs no raw (never rose).
  if (r->hasRaw && (r->health == HEALTH_FAULT || r->health == HEALTH_TIMEOUT)) {
    return snprintf(buf, len, " %s %s(raw %ld)", label, healthName(r->health), (long)r->raw);
  }
  return snprintf(buf, len, " %s %s", label, healthName(r->health));
}

static void printStatus(MonoMs now) {
  static char line[LOG_TEXT_LEN];  // task-private: keeps the 3 KiB stack free
  size_t used = (size_t)snprintf(line, sizeof(line), "t=%lus", (unsigned long)(now / 1000));
  if (snapshotPeek(&s_snap)) {
    const Reading* r = s_snap.readings;
    struct { const char* label; MetricId m; int dec; } cols[] = {
        {"airT", METRIC_AIR_TEMPERATURE, 1}, {"RH", METRIC_AIR_HUMIDITY, 0},
        {"waterT", METRIC_WATER_TEMPERATURE, 1}, {"light", METRIC_LIGHT_LEVEL, 0},
        {"pH", METRIC_PH, 2}, {"TDS", METRIC_TDS, 0}, {"dist", METRIC_WATER_DISTANCE, 0},
        {"CO2eq", METRIC_CO2EQ, 0}};
    for (size_t i = 0; i < sizeof(cols) / sizeof(cols[0]) && used < sizeof(line); ++i) {
      const int n = cell(line + used, sizeof(line) - used, cols[i].label, &r[cols[i].m], cols[i].dec);
      if (n > 0) used += (size_t)n;
    }
  } else if (used < sizeof(line)) {
    used += (size_t)snprintf(line + used, sizeof(line) - used, " no snapshot");
  }
  logPrintf(LOG_LEVEL_INFO, "%s", line);

  SafetyStatus st;
  AcquisitionStats a;
  acquisitionGetStats(&a);
  if (safetyStatusPeek(&st)) {
    logPrintf(LOG_LEVEL_INFO,
              "     lamp %s (%s)%s | latched 0x%04x | frames %lu late %lu skip %lu jitter %lu ms"
              " | guard %lu fail %lu | %s%s",
              st.relayOn ? "ON" : "OFF", lampReasonName(st.lampReason),
              st.lampWantsOn && !st.relayOn ? " wants ON" : "", (unsigned)st.latchedMask,
              (unsigned long)a.frames, (unsigned long)a.frameLate, (unsigned long)a.frameSkipped,
              (unsigned long)a.frameJitterMaxMs, (unsigned long)a.guardTimeouts,
              (unsigned long)a.driverFailures, s_degraded[0] ? "DEGRADED " : "health OK",
              s_degraded);
  }
  s_degraded[0] = '\0';  // a new period starts
}

static void supervisorTask(void* arg) {
  (void)arg;
  logPrintf(LOG_LEVEL_INFO, "reset reason: %s", resetReasonName(esp_reset_reason()));
  readCounters(&s_prev);
  TickType_t last = xTaskGetTickCount();
  uint32_t seconds = 0;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(CFG_SUPERVISOR_PERIOD_MS));
    const MonoMs now = monoNowMs();
    ++seconds;
    checkProgress(now);
    checkCounters();
    if (seconds % SUPERVISOR_MEMORY_EVERY_S == 0) {
      checkMemory(seconds % SUPERVISOR_MEMORY_REPORT_EVERY_S == 0);
    }
    if (seconds % SUPERVISOR_STATUS_EVERY_S == 0) printStatus(now);
    progressMark(TASK_ID_SUPERVISOR);
  }
}

bool supervisorInit(void) {
  if (s_task != NULL) return true;
  const TaskSpec* spec = cfgTaskSpec(TASK_ID_SUPERVISOR);
  s_task = xTaskCreateStaticPinnedToCore(supervisorTask, spec->name, sizeof(s_stack), NULL,
                                         spec->priority, s_stack, &s_tcb, spec->core);
  return s_task != NULL;
}

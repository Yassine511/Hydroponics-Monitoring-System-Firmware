#include "services/acquisition.h"

#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "app_config.h"
#include "board_profile.h"
#include "diagnostics/logger.h"
#include "diagnostics/progress.h"
#include "domain/alarm.h"
#include "domain/schedule.h"
#include "drivers/bme280.h"
#include "drivers/ds18b20.h"
#include "drivers/hcsr04.h"
#include "drivers/light.h"
#include "drivers/ph.h"
#include "drivers/sgp30.h"
#include "drivers/tds.h"
#include "hal/adc_channel.h"
#include "hal/clock.h"
#include "hal/i2c_bus.h"
#include "hal/onewire_bus.h"
#include "hal/pulse_capture.h"
#include "policy_config.h"
#include "services/rail_state.h"
#include "services/safety.h"
#include "services/snapshot_store.h"

// ---- Buses and drivers: owned by AcqNormal only (§11) ------------------------
static I2cBus s_i2c = I2C_BUS_INIT;
static OneWireBus s_oneWire = ONE_WIRE_BUS_INIT;
static AdcChannel s_adcLight = ADC_CHANNEL_INIT;
static AdcChannel s_adcTds = ADC_CHANNEL_INIT;
static AdcChannel s_adcPh = ADC_CHANNEL_INIT;
static PulseCapture s_capture = PULSE_CAPTURE_INIT;

static Bme280Config s_bmeConfig = {&s_i2c, BOARD_BME280_I2C_ADDRESS};
static Sgp30Config s_sgpConfig = {&s_i2c, BOARD_SGP30_I2C_ADDRESS};
static Ds18b20Config s_dsConfig = {&s_oneWire};
static LightConfig s_lightConfig = {&s_adcLight};
static TdsConfig s_tdsConfig = {&s_adcTds, {1.0f, 0.0f, 0, false}};  // nominal curve (D13)
static PhConfig s_phConfig = {&s_adcPh, BOARD_PH_DIVIDER_TOP_OHM, BOARD_PH_DIVIDER_BOTTOM_OHM,
                              {POLICY_PH_NOMINAL_PH_PER_MV,
                               7.0f - POLICY_PH_NOMINAL_PH_PER_MV * POLICY_PH_NOMINAL_MV_AT_PH7,
                               0, true},
                              true};  // provisional fit: published UNCALIBRATED
static Hcsr04Config s_hcConfig = {&s_capture, CFG_HCSR04_TIMEOUT_MS};

static Bme280Driver s_bme;
static Sgp30Driver s_sgp;
static Ds18b20Driver s_ds;
static LightDriver s_light;
static TdsDriver s_tds;
static PhDriver s_ph;
static Hcsr04Driver s_hc;

// ---- Per-driver stepping (init -> start -> poll), as test/bringup/water_main.c
enum { RUN_BME280 = 0, RUN_DS18B20, RUN_SGP30, RUN_HCSR04, RUN_TDS, RUN_PH, RUN_LIGHT, RUN_COUNT };

typedef struct DriverRun {
  SensorDriver* driver;
  uint32_t initBudgetMs;   // worst-case blocking of init()
  uint32_t stepBudgetMs;   // worst-case blocking of one start()/poll(); > 2 = "blocking"
  bool initialised;
  bool pending;            // started, not yet READY or failed
  bool wantStart;          // due this frame, not started yet
  MonoMs nextPollMs;
  MonoMs retryAtMs;        // earliest init() retry after a failure
  DriverStatus lastFailure;  // DRIVER_OK = none; logged on change only
} DriverRun;

// Worst-case blocking from each driver header (§16 budgets).
static DriverRun s_runs[RUN_COUNT] = {
    [RUN_BME280] = {&s_bme.base, 30, 1, false, false, false, 0, 0, DRIVER_OK},
    [RUN_DS18B20] = {&s_ds.base, 50, 13, false, false, false, 0, 0, DRIVER_OK},
    [RUN_SGP30] = {&s_sgp.base, 45, 1, false, false, false, 0, 0, DRIVER_OK},
    [RUN_HCSR04] = {&s_hc.base, 1, 1, false, false, false, 0, 0, DRIVER_OK},
    [RUN_TDS] = {&s_tds.base, 1, 1, false, false, false, 0, 0, DRIVER_OK},
    [RUN_PH] = {&s_ph.base, 1, 1, false, false, false, 0, 0, DRIVER_OK},
    [RUN_LIGHT] = {&s_light.base, 1, 1, false, false, false, 0, 0, DRIVER_OK},
};

// One-second jobs, started in this order at each frame (§9): the 1-Wire work
// comes before the HC-SR04 ping because 1-Wire slots mask interrupts and
// would delay the echo timestamps (onewire_bus.h).
static const uint8_t kFrameOrder[] = {RUN_DS18B20, RUN_SGP30, RUN_HCSR04, RUN_TDS, RUN_PH, RUN_LIGHT};
#define FRAME_ORDER_COUNT ((uint8_t)(sizeof(kFrameOrder) / sizeof(kFrameOrder[0])))

enum { JOB_FRAME = 0, JOB_BME280, JOB_COUNT };
static ScheduleJob s_jobs[JOB_COUNT];

// ---- Working snapshot and rule state (task-private) --------------------------
static Snapshot s_work;
static AlarmRuleState s_ruleState[POLICY_RULE_COUNT];

static portMUX_TYPE s_statsMux = portMUX_INITIALIZER_UNLOCKED;
static AcquisitionStats s_stats;

static StaticTask_t s_tcb;
static StackType_t s_stack[4 * 1024];  // cfgTaskSpec(TASK_ID_ACQUISITION_NORMAL)->stackBytes
static TaskHandle_t s_task = NULL;

#define STATS_ADD(field, n)        \
  do {                             \
    portENTER_CRITICAL(&s_statsMux); \
    s_stats.field += (n);          \
    portEXIT_CRITICAL(&s_statsMux);  \
  } while (0)

// ---- Bookkeeping after a result ----------------------------------------------

static void evaluateRules(const Reading* r, MonoMs now) {
  for (uint8_t i = 0; i < POLICY_RULE_COUNT; ++i) {
    const AlarmRule* rule = &kPolicyRules[i];
    if (!rule->enabled || rule->metric != r->metric) continue;
    const RuleDecision d = evaluateRule(rule, r, now, alarmStaleLimitMs(r->metric), &s_ruleState[i]);
    if (d != RULE_DECISION_TRIP && d != RULE_DECISION_CLEAR) continue;
    AlarmEvent e;
    e.ruleId = i;
    e.decision = d;
    e.sample = *r;
#ifdef FW_ALARM_LATENCY_TEST
    safetyLatencyMarkSubmit();
    s_ruleState[i].tripped = false;  // re-arm: the next sample trips again
#endif
    safetySubmitEvent(&e);  // zero wait; wakes Safety now (§4)
    STATS_ADD(ruleEvents, 1);
  }
}

// Stamp, store, check rules, for every reading the driver returned.
static void storeResults(DriverRun* run, MonoMs now, bool guardTimeout) {
  Reading out[4];
  const uint8_t n = sensorDriverReadResult(run->driver, out, 4);
  const uint32_t epoch = railStateRead().epoch;
  for (uint8_t k = 0; k < n; ++k) {
    Reading* r = &out[k];
    if (r->metric >= METRIC_COUNT) continue;
    if (guardTimeout) {  // abandoned at its deadline: a new, failed result
      r->health = HEALTH_TIMEOUT;
      r->qualityFlags = (uint16_t)(r->qualityFlags | kQualityLate);
      r->intervalEndMs = now;
    }
    r->powerEpoch = epoch;
    r->configRevision = 0;
    s_work.readings[r->metric] = *r;
    evaluateRules(r, now);
  }
  s_work.publishedMs = now;
  s_work.powerEpoch = epoch;
  snapshotPublish(&s_work);
}

static void noteFailure(DriverRun* run, DriverStatus st, const char* what) {
  if (st == run->lastFailure) return;  // log transitions only
  run->lastFailure = st;
  logPrintf(LOG_LEVEL_WARN, "acq: %s %s -> %s", sensorDriverName(run->driver), what,
            driverStatusName(st));
}

static void noteRecovered(DriverRun* run) {
  if (run->lastFailure == DRIVER_OK) return;
  run->lastFailure = DRIVER_OK;
  logPrintf(LOG_LEVEL_INFO, "acq: %s OK", sensorDriverName(run->driver));
}

static void finishRun(DriverRun* run, MonoMs now, DriverStatus st) {
  run->pending = false;
  if (st == DRIVER_READY) {
    noteRecovered(run);
  } else {
    noteFailure(run, st, "result");
    STATS_ADD(driverFailures, 1);
    if (st == DRIVER_NOT_PRESENT || st == DRIVER_NOT_IMPLEMENTED) {
      run->initialised = false;
      run->retryAtMs = now;  // re-init at its next due time
    }
  }
  storeResults(run, now, false);
}

static void guardTimeout(DriverRun* run, MonoMs now) {
  sensorDriverCancel(run->driver);
  run->pending = false;
  noteFailure(run, DRIVER_TIMEOUT, "guard");
  STATS_ADD(guardTimeouts, 1);
  storeResults(run, now, true);
}

// ---- Blocking rules (§9) -------------------------------------------------------

static bool isBlocking(uint32_t budgetMs) { return budgetMs > 2; }

// May a step blocking the worker for budgetMs run now? Not while a BME280
// conversion is pending (its poll would come too late and time out), not
// while an HC-SR04 echo is in flight (1-Wire masks interrupts), and only if
// it ends before the next BME280 slot.
static bool blockingAllowed(uint8_t runId, uint32_t budgetMs, MonoMs now) {
  if (!isBlocking(budgetMs)) return true;
  if (runId != RUN_BME280 && s_runs[RUN_BME280].pending) return false;
  if (s_runs[RUN_HCSR04].pending) return false;
  if (runId == RUN_BME280) return true;
  return scheduleFitsBefore(now, budgetMs, s_jobs[JOB_BME280].nextDueMs);
}

// The ping must not overlap a DS18B20 call: defer it if one is due soon.
static bool pingAllowed(MonoMs now) {
  const DriverRun* ds = &s_runs[RUN_DS18B20];
  if (ds->wantStart) return false;
  return !ds->pending || ds->nextPollMs > now + CFG_HCSR04_TIMEOUT_MS + 2;
}

// ---- Start and poll ------------------------------------------------------------

static void prepareInputs(uint8_t runId, MonoMs now) {
  if (runId == RUN_SGP30) {
    sgp30DriverSetHumidity(&s_sgp, &s_work.readings[METRIC_AIR_TEMPERATURE],
                           &s_work.readings[METRIC_AIR_HUMIDITY], now);
  } else if (runId == RUN_TDS) {
    tdsDriverSetWaterTemperature(&s_tds, &s_work.readings[METRIC_WATER_TEMPERATURE]);
  }
}

// Returns false if the start must wait (blocking rule); the job stays wanted.
static bool tryStart(uint8_t runId, MonoMs now) {
  DriverRun* run = &s_runs[runId];
  if (runId == RUN_HCSR04 && !pingAllowed(now)) return false;
  if (!run->initialised) {
    if (!monoReached(now, run->retryAtMs)) {  // back-off: skip this period
      run->wantStart = false;
      return true;
    }
    if (!blockingAllowed(runId, run->initBudgetMs, now)) return false;
    const DriverStatus st = sensorDriverInit(run->driver);
    if (st != DRIVER_OK) {
      noteFailure(run, st, "init");
      run->retryAtMs = now + POLICY_INIT_RETRY_MS;
      run->wantStart = false;
      return true;
    }
    run->initialised = true;
    logPrintf(LOG_LEVEL_INFO, "acq: %s init OK", sensorDriverName(run->driver));
    now = monoNowMs();  // init may have taken tens of ms
  }
  if (!blockingAllowed(runId, run->stepBudgetMs, now)) return false;
  prepareInputs(runId, now);
  run->wantStart = false;
  const DriverStatus st = sensorDriverStart(run->driver, now);
  if (st == DRIVER_OK) {
    run->pending = true;
    run->nextPollMs = now;
  } else {
    if (st != DRIVER_PENDING) run->initialised = false;
    finishRun(run, now, st);
  }
  return true;
}

// Returns true if the run made progress.
static bool tryPoll(uint8_t runId, MonoMs now) {
  DriverRun* run = &s_runs[runId];
  if (!run->pending || now < run->nextPollMs) return false;
  if (!blockingAllowed(runId, run->stepBudgetMs, now)) return false;
  const PollResult pr = sensorDriverPoll(run->driver, now);
  if (pr.status == DRIVER_PENDING) {
    run->nextPollMs = pr.nextPollMs > now ? pr.nextPollMs : now + 1;
    return true;
  }
  finishRun(run, monoNowMs(), pr.status);
  return true;
}

// ---- Frame and slot boundaries ---------------------------------------------------

static void startFrame(MonoMs now) {
  ScheduleJob* job = &s_jobs[JOB_FRAME];
  const uint32_t lateBefore = job->late;
  const uint32_t skippedBefore = job->skipped;
  scheduleJobMarkRun(job, now);

  // Frame guard: a one-second job still running from the last frame is
  // abandoned, never retried inside this frame (§18).
  for (uint8_t i = 0; i < FRAME_ORDER_COUNT; ++i) {
    DriverRun* run = &s_runs[kFrameOrder[i]];
    if (run->pending) guardTimeout(run, now);
    run->wantStart = true;
  }

  ++s_work.frameSequence;
  s_work.publishedMs = now;
  snapshotPublish(&s_work);

  portENTER_CRITICAL(&s_statsMux);
  ++s_stats.frames;
  s_stats.frameLate += job->late - lateBefore;
  s_stats.frameSkipped += job->skipped - skippedBefore;
  s_stats.frameJitterMaxMs = job->maxLateMs;
  portEXIT_CRITICAL(&s_statsMux);
}

static void startBmeSlot(MonoMs now) {
  ScheduleJob* job = &s_jobs[JOB_BME280];
  const uint32_t lateBefore = job->late;
  const uint32_t skippedBefore = job->skipped;
  scheduleJobMarkRun(job, now);
  if (s_runs[RUN_BME280].pending) guardTimeout(&s_runs[RUN_BME280], now);
  s_runs[RUN_BME280].wantStart = true;
  portENTER_CRITICAL(&s_statsMux);
  s_stats.bmeLate += job->late - lateBefore;
  s_stats.bmeSkipped += job->skipped - skippedBefore;
  portEXIT_CRITICAL(&s_statsMux);
}

// One pass: boundaries first, the BME280 fast lane next, then every due poll
// and the frame starts in order. Repeats while something progressed (each
// step is bounded, so the pass is too).
static void runPass(MonoMs now) {
  if (scheduleJobDue(&s_jobs[JOB_FRAME], now)) startFrame(now);
  if (scheduleJobDue(&s_jobs[JOB_BME280], now)) startBmeSlot(now);

  for (uint8_t iter = 0; iter < 8; ++iter) {
    bool progress = false;
    now = monoNowMs();
    if (s_runs[RUN_BME280].wantStart) progress |= tryStart(RUN_BME280, now);
    for (uint8_t r = 0; r < RUN_COUNT; ++r) progress |= tryPoll(r, monoNowMs());
    for (uint8_t i = 0; i < FRAME_ORDER_COUNT; ++i) {
      const uint8_t r = kFrameOrder[i];
      if (!s_runs[r].wantStart) continue;
      if (!tryStart(r, monoNowMs())) break;  // keep the order: later ones wait too
      progress = true;
    }
    if (!progress) break;
  }
}

// Next wake: the earliest job deadline or poll time that is not blocked
// (a blocked step is woken by whatever blocks it, which is in this set).
static MonoMs nextWake(MonoMs now) {
  MonoMs due = scheduleEarliestDue(s_jobs, JOB_COUNT);
  for (uint8_t r = 0; r < RUN_COUNT; ++r) {
    const DriverRun* run = &s_runs[r];
    if (!run->pending) continue;
    const MonoMs t = run->nextPollMs > now ? run->nextPollMs : now + 1;
    if (blockingAllowed(r, run->stepBudgetMs, t) && t < due) due = t;
  }
  return due;
}

static void acqTask(void* arg) {
  (void)arg;
  // HALs begin here, on core 1: the echo ISR is installed on this core.
  if (!i2cBusBegin(&s_i2c, BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL, BOARD_I2C_CLOCK_HZ)) {
    logPrintf(LOG_LEVEL_ERROR, "acq: I2C begin failed");
  }
  if (!oneWireBusBegin(&s_oneWire, BOARD_PIN_ONE_WIRE)) logPrintf(LOG_LEVEL_ERROR, "acq: 1-Wire begin failed");
  if (!adcChannelBegin(&s_adcLight, BOARD_PIN_LIGHT_ADC)) logPrintf(LOG_LEVEL_ERROR, "acq: light ADC failed");
  if (!adcChannelBegin(&s_adcTds, BOARD_PIN_TDS_ADC)) logPrintf(LOG_LEVEL_ERROR, "acq: TDS ADC failed");
  if (!adcChannelBegin(&s_adcPh, BOARD_PIN_PH_ADC)) logPrintf(LOG_LEVEL_ERROR, "acq: pH ADC failed");
  if (!pulseCaptureBegin(&s_capture, BOARD_PIN_HCSR04_TRIG, BOARD_PIN_HCSR04_ECHO)) {
    logPrintf(LOG_LEVEL_ERROR, "acq: HC-SR04 capture failed");
  }

  const MonoMs t0 = monoNowMs();
  scheduleJobInit(&s_jobs[JOB_FRAME], CFG_FRAME_PERIOD_MS, CFG_FRAME_RESERVATION_MS, t0);
  scheduleJobInit(&s_jobs[JOB_BME280], CFG_BME280_PERIOD_MS, CFG_BME280_BUDGET_MS, t0);

  // Task watchdog (§18): this task resets it for itself after each pass. A
  // pass happens at least every 250 ms (BME280 slot), so 5 s of silence means
  // the task is stuck inside a call.
  if (esp_task_wdt_add(NULL) != ESP_OK) logPrintf(LOG_LEVEL_ERROR, "acq: task watchdog add failed");

  for (;;) {
    const MonoMs now = monoNowMs();
    const MonoMs due = nextWake(now);
    ulTaskNotifyTake(pdTRUE, due > now ? pdMS_TO_TICKS((uint32_t)(due - now)) : 0);
    runPass(monoNowMs());
#ifdef FW_INJECT_STALL_ACQ
    // Acceptance test (§19 "Stall injection"): block forever after N ms.
    if (monoNowMs() - t0 >= (MonoMs)(FW_INJECT_STALL_ACQ)) {
      logPrintf(LOG_LEVEL_WARN, "acq: INJECTED STALL - blocking forever");
      vTaskDelay(portMAX_DELAY);
    }
#endif
    progressMark(TASK_ID_ACQUISITION_NORMAL);
    esp_task_wdt_reset();
  }
}

TaskHandle_t acquisitionTaskHandle(void) { return s_task; }

bool acquisitionInit(void) {
  if (s_task != NULL) return true;
  bme280DriverConstruct(&s_bme, &s_bmeConfig);
  sgp30DriverConstruct(&s_sgp, &s_sgpConfig);
  ds18b20DriverConstruct(&s_ds, &s_dsConfig);
  lightDriverConstruct(&s_light, &s_lightConfig);
  tdsDriverConstruct(&s_tds, &s_tdsConfig);
  phDriverConstruct(&s_ph, &s_phConfig);
  hcsr04DriverConstruct(&s_hc, &s_hcConfig);
  snapshotReset(&s_work);
  memset(s_ruleState, 0, sizeof(s_ruleState));

  const TaskSpec* spec = cfgTaskSpec(TASK_ID_ACQUISITION_NORMAL);
  if (spec->stackBytes != sizeof(s_stack)) {
    logPrintf(LOG_LEVEL_WARN, "acq: stack %u B, task table says %lu B", (unsigned)sizeof(s_stack),
              (unsigned long)spec->stackBytes);
  }
  s_task = xTaskCreateStaticPinnedToCore(acqTask, spec->name, sizeof(s_stack), NULL,
                                         spec->priority, s_stack, &s_tcb, spec->core);
  if (s_task == NULL) return false;
  logPrintf(LOG_LEVEL_INFO, "acq: started (core %u, prio %u), %u drivers", (unsigned)spec->core,
            (unsigned)spec->priority, (unsigned)RUN_COUNT);
  return true;
}

// D15: single worker until WP9; the lease protocol is not implemented yet.
bool acquisitionGrant(uint32_t generation, uint8_t allowedRailMask) {
  (void)generation;
  (void)allowedRailMask;
  return false;
}

bool acquisitionRequestQuiesce(uint32_t generation) {
  (void)generation;
  return false;
}

void acquisitionGetStats(AcquisitionStats* out) {
  portENTER_CRITICAL(&s_statsMux);
  *out = s_stats;
  portEXIT_CRITICAL(&s_statsMux);
}

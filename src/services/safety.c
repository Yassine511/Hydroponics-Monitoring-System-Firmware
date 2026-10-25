#include "services/safety.h"

#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include "app_config.h"
#include "diagnostics/logger.h"
#include "diagnostics/progress.h"
#include "hal/clock.h"
#include "hal/relay.h"
#include "policy_config.h"
#include "services/snapshot_store.h"

// ---- Sticky per-rule store (§11): survives a full detail queue -------------
typedef struct StickySlot {
  bool tripSinceDrain;      // a TRIP was submitted since Safety last drained
  bool lost;                // a detail record of this rule was dropped
  RuleDecision last;        // last decision submitted (TRIP or CLEAR)
  AlarmEvent trip;          // worst TRIP since the last drain
  AlarmEvent clear;         // the last CLEAR
} StickySlot;

static portMUX_TYPE s_stickyMux = portMUX_INITIALIZER_UNLOCKED;
static StickySlot s_sticky[POLICY_RULE_COUNT];
static volatile uint32_t s_detailOverflow;  // single writer per increment site, read for status only

// ---- Queues and task (all static, §16) --------------------------------------
static StaticQueue_t s_detailControl;
static uint8_t s_detailStorage[CFG_ALARM_DETAIL_QUEUE_LEN * sizeof(AlarmEvent)];
static QueueHandle_t s_detail = NULL;

static StaticQueue_t s_statusControl;
static uint8_t s_statusStorage[sizeof(SafetyStatus)];
static QueueHandle_t s_status = NULL;

static StaticTask_t s_tcb;
static StackType_t s_stack[3 * 1024];  // cfgTaskSpec(TASK_ID_SAFETY)->stackBytes
static TaskHandle_t s_task = NULL;

// Task-private state: static, never on the 3 KiB stack.
static SafetyCore s_core;
static Snapshot s_snap;
static AlarmEvent s_events[CFG_ALARM_DETAIL_QUEUE_LEN + 2 * POLICY_RULE_COUNT];
static SafetyStatus s_statusOut;

#ifdef FW_ALARM_LATENCY_TEST
// ---- Alarm latency test (env:alarm_latency, §19) -----------------------------
// submit -> action: esp_timer us from acquisition's submit to the end of the
//                   Safety pass that applied the decision (relay written).
// sample -> action: ms from the sample's intervalEndMs (BME280 read done).
static portMUX_TYPE s_latMux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_submitUs;  // 0 = no event waiting

typedef struct LatencyStats {
  uint32_t n;
  int64_t minUs, maxUs, sumUs;
  uint32_t minMs, maxMs, sumMs;
} LatencyStats;
static LatencyStats s_lat;
static uint32_t s_latReports;

void safetyLatencyMarkSubmit(void) {
  const int64_t t = esp_timer_get_time();
  portENTER_CRITICAL(&s_latMux);
  s_submitUs = t;
  portEXIT_CRITICAL(&s_latMux);
}

static void latencyRecord(const AlarmEvent* events, uint8_t n) {
  const AlarmEvent* trip = NULL;
  for (uint8_t i = 0; i < n; ++i) {
    if (events[i].decision == RULE_DECISION_TRIP) trip = &events[i];
  }
  portENTER_CRITICAL(&s_latMux);
  const int64_t submitted = s_submitUs;
  s_submitUs = 0;
  portEXIT_CRITICAL(&s_latMux);
  if (trip == NULL || submitted == 0) return;

  const int64_t us = esp_timer_get_time() - submitted;
  const MonoMs now = monoNowMs();
  const uint32_t ms = now > trip->sample.intervalEndMs ? (uint32_t)(now - trip->sample.intervalEndMs) : 0;
  if (s_lat.n == 0 || us < s_lat.minUs) s_lat.minUs = us;
  if (s_lat.n == 0 || us > s_lat.maxUs) s_lat.maxUs = us;
  if (s_lat.n == 0 || ms < s_lat.minMs) s_lat.minMs = ms;
  if (s_lat.n == 0 || ms > s_lat.maxMs) s_lat.maxMs = ms;
  s_lat.sumUs += us;
  s_lat.sumMs += ms;
  if (++s_lat.n == 20) {
    logPrintf(LOG_LEVEL_INFO,
              "latency #%lu (20 TRIPs): submit->action us min %lld avg %lld max %lld | "
              "sample->action ms min %lu avg %lu max %lu",
              (unsigned long)++s_latReports, (long long)s_lat.minUs, (long long)(s_lat.sumUs / 20),
              (long long)s_lat.maxUs, (unsigned long)s_lat.minMs, (unsigned long)(s_lat.sumMs / 20),
              (unsigned long)s_lat.maxMs);
    memset(&s_lat, 0, sizeof(s_lat));
  }
}
#endif

static bool moreExtreme(const AlarmRule* rule, float a, float b) {
  return rule->comparison == COMPARISON_ABOVE ? a > b : a < b;
}

bool safetySubmitEvent(const AlarmEvent* e) {
  if (e == NULL || e->ruleId >= POLICY_RULE_COUNT) return false;
  if (e->decision != RULE_DECISION_TRIP && e->decision != RULE_DECISION_CLEAR) return false;
  const AlarmRule* rule = &kPolicyRules[e->ruleId];

  portENTER_CRITICAL(&s_stickyMux);
  StickySlot* slot = &s_sticky[e->ruleId];
  slot->last = e->decision;
  if (e->decision == RULE_DECISION_TRIP) {
    if (!slot->tripSinceDrain || moreExtreme(rule, e->sample.value, slot->trip.sample.value)) {
      slot->trip = *e;
    }
    slot->tripSinceDrain = true;
  } else {
    slot->clear = *e;
  }
  portEXIT_CRITICAL(&s_stickyMux);

  bool queued = s_detail != NULL && xQueueSend(s_detail, e, 0) == pdTRUE;
  if (!queued) {
    portENTER_CRITICAL(&s_stickyMux);
    slot->lost = true;
    ++s_detailOverflow;
    portEXIT_CRITICAL(&s_stickyMux);
  }
  if (s_task != NULL) xTaskNotifyGive(s_task);
  return queued;
}

// Drain the detail queue, then, only for a rule whose detail record was
// dropped, let its sticky slot re-supply what may be missing: the TRIP (if
// none came through the queue) and, if the rule's last decision was CLEAR,
// that CLEAR too, so the rule cannot stay active forever. A record that was
// queued arrives through the queue, so nothing is processed twice.
static uint8_t drainEvents(void) {
  uint8_t n = 0;
  while (n < CFG_ALARM_DETAIL_QUEUE_LEN && xQueueReceive(s_detail, &s_events[n], 0) == pdTRUE) ++n;

  bool sawTrip[POLICY_RULE_COUNT] = {false};
  for (uint8_t i = 0; i < n; ++i) {
    if (s_events[i].ruleId < POLICY_RULE_COUNT && s_events[i].decision == RULE_DECISION_TRIP) {
      sawTrip[s_events[i].ruleId] = true;
    }
  }
  portENTER_CRITICAL(&s_stickyMux);
  for (uint8_t r = 0; r < POLICY_RULE_COUNT; ++r) {
    StickySlot* slot = &s_sticky[r];
    if (slot->lost) {
      if (slot->tripSinceDrain && !sawTrip[r]) s_events[n++] = slot->trip;
      if (slot->last == RULE_DECISION_CLEAR) s_events[n++] = slot->clear;
    }
    slot->tripSinceDrain = false;
    slot->lost = false;
  }
  portEXIT_CRITICAL(&s_stickyMux);
  return n;
}

static void safetyTask(void* arg) {
  (void)arg;
  SafetyOutputs out;
  bool relayOn = false;
  LampReason lastReason = LAMP_REASON_NO_DATA;
  uint32_t passes = 0;

  // Task watchdog (§18): reset by this task only, after each completed pass
  // (at least every 100 ms). Supervisor never resets it on Safety's behalf.
  if (esp_task_wdt_add(NULL) != ESP_OK) logPrintf(LOG_LEVEL_ERROR, "safety: task watchdog add failed");

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CFG_SAFETY_HEALTH_CHECK_MS));
#ifdef FW_INJECT_STALL_SAFETY
    // Acceptance test (§19 "Stall injection"): block forever after N ms.
    if (monoNowMs() >= (MonoMs)(FW_INJECT_STALL_SAFETY)) {
      logPrintf(LOG_LEVEL_WARN, "safety: INJECTED STALL - blocking forever");
      vTaskDelay(portMAX_DELAY);
    }
#endif
    const MonoMs now = monoNowMs();
    const uint8_t n = drainEvents();
    const bool haveSnap = snapshotPeek(&s_snap);
    const SafetyInputs in = {haveSnap ? &s_snap : NULL, s_events, n, now};
    safetyCoreStep(&s_core, &in, &out);

    const bool relayChanged = out.relayOn != relayOn;
    if (relayChanged) {
      relayWrite(out.relayOn);
      relayOn = out.relayOn;
    }
#ifdef FW_ALARM_LATENCY_TEST
    latencyRecord(s_events, n);  // the action is applied: stop the clock
#endif
    if (relayChanged) {
      logPrintf(LOG_LEVEL_INFO, "safety: relay %s (%s)", relayOn ? "ON" : "OFF",
                lampReasonName(out.lampReason));
    } else if (out.lampReason != lastReason &&
               (out.lampReason == LAMP_REASON_STALE || out.lampReason == LAMP_REASON_FAULT ||
                out.lampReason == LAMP_REASON_INTERLOCK)) {
      logPrintf(LOG_LEVEL_WARN, "safety: lamp %s", lampReasonName(out.lampReason));
    }
    lastReason = out.lampReason;
    for (uint8_t i = 0; i < n; ++i) {
#ifdef FW_ALARM_LATENCY_TEST
      if (s_events[i].decision == RULE_DECISION_TRIP && s_events[i].ruleId == 0) continue;  // 4 per s
#endif
      logPrintf(LOG_LEVEL_WARN, "safety: rule %u %s (%s = %.2f)", (unsigned)s_events[i].ruleId,
                s_events[i].decision == RULE_DECISION_TRIP ? "TRIP" : "CLEAR",
                metricName(s_events[i].sample.metric), (double)s_events[i].sample.value);
    }

    s_statusOut.atMs = now;
    s_statusOut.relayOn = relayOn;
    s_statusOut.lampWantsOn = out.lampWantsOn;
    s_statusOut.lampReason = out.lampReason;
    s_statusOut.interlock = out.interlock;
    s_statusOut.latchedMask = out.latchedMask;
    s_statusOut.activeMask = out.activeMask;
    s_statusOut.staleMask = out.staleMask;
    s_statusOut.detailOverflow = s_detailOverflow;
    s_statusOut.passes = ++passes;
    xQueueOverwrite(s_status, &s_statusOut);
    progressMark(TASK_ID_SAFETY);
    esp_task_wdt_reset();
  }
}

TaskHandle_t safetyTaskHandle(void) { return s_task; }

bool safetyInit(void) {
  if (s_task != NULL) return true;
  s_detail = xQueueCreateStatic(CFG_ALARM_DETAIL_QUEUE_LEN, sizeof(AlarmEvent), s_detailStorage,
                                &s_detailControl);
  s_status = xQueueCreateStatic(1, sizeof(SafetyStatus), s_statusStorage, &s_statusControl);
  if (s_detail == NULL || s_status == NULL) return false;

  static const SafetyConfig kConfig = {
      POLICY_LAMP_AUTO_ENABLED != 0,
      {POLICY_LAMP_ON_BELOW_MV, POLICY_LAMP_OFF_ABOVE_MV, POLICY_LAMP_MIN_ON_MS,
       POLICY_LAMP_MIN_OFF_MS, POLICY_LAMP_FAULTS_TO_OFF},
      POLICY_LIGHT_STALE_MS};
  safetyCoreInit(&s_core, &kConfig, kPolicyRules, POLICY_RULE_COUNT, monoNowMs());
  relayWrite(false);

  const TaskSpec* spec = cfgTaskSpec(TASK_ID_SAFETY);
  if (spec->stackBytes != sizeof(s_stack)) {
    logPrintf(LOG_LEVEL_WARN, "safety: stack %u B, task table says %lu B",
              (unsigned)sizeof(s_stack), (unsigned long)spec->stackBytes);
  }
  s_task = xTaskCreateStaticPinnedToCore(safetyTask, spec->name, sizeof(s_stack), NULL,
                                         spec->priority, s_stack, &s_tcb, spec->core);
  if (s_task == NULL) return false;
  logPrintf(LOG_LEVEL_INFO, "safety: started (core %u, prio %u), lamp auto %s, relay OFF",
            (unsigned)spec->core, (unsigned)spec->priority,
            POLICY_LAMP_AUTO_ENABLED ? "ON" : "off");
  return true;
}

bool safetyStatusPeek(SafetyStatus* out) {
  if (s_status == NULL || out == NULL) return false;
  return xQueuePeek(s_status, out, 0) == pdTRUE;
}

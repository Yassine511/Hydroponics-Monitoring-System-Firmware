#include "drivers/tds.h"

#include <string.h>

// TODO(WP5): Stage 1 stub - replace with the real implementation.

static inline TdsDriver* asTds(SensorDriver* d) { return (TdsDriver*)d; }

static SensorId tdsId(const SensorDriver* d) { (void)d; return SENSOR_TDS; }
static const char* tdsName(const SensorDriver* d) { (void)d; return "sen0244_tds"; }
static uint8_t tdsMetricCount(const SensorDriver* d) { (void)d; return 1; }

static DriverStatus tdsInit(SensorDriver* d) {
  asTds(d)->initialised_ = false;
  return DRIVER_NOT_IMPLEMENTED;
}

static DriverStatus tdsStart(SensorDriver* d, MonoMs now) {
  (void)d;
  (void)now;
  return DRIVER_NOT_IMPLEMENTED;
}

static PollResult tdsPoll(SensorDriver* d, MonoMs now) {
  (void)d;
  PollResult r;
  r.status = DRIVER_NOT_IMPLEMENTED;
  r.nextPollMs = now;
  return r;
}

static uint8_t tdsReadResult(SensorDriver* d, Reading* out, uint8_t maxOut) {
  (void)d;
  (void)out;
  (void)maxOut;
  return 0;
}

static void tdsCancel(SensorDriver* d) { (void)d; }

static void tdsPreparePowerOff(SensorDriver* d) { (void)d; }

static const SensorDriverOps kTdsOps = {
    .id = tdsId,
    .name = tdsName,
    .metricCount = tdsMetricCount,
    .init = tdsInit,
    .start = tdsStart,
    .poll = tdsPoll,
    .readResult = tdsReadResult,
    .cancel = tdsCancel,
    .preparePowerOff = tdsPreparePowerOff,
};

void tdsDriverConstruct(TdsDriver* self, const TdsConfig* config) {
  self->base.ops = &kTdsOps;
  self->config_ = *config;
  self->initialised_ = false;
  memset(&self->waterTemperature_, 0, sizeof(self->waterTemperature_));
}

void tdsDriverSetWaterTemperature(TdsDriver* self, const Reading* waterTemperature) {
  self->waterTemperature_ = *waterTemperature;
}

#include "drivers/ph.h"

// TODO(WP5): Stage 1 stub - replace with the real implementation.

static inline PhDriver* asPh(SensorDriver* d) { return (PhDriver*)d; }

static SensorId phId(const SensorDriver* d) { (void)d; return SENSOR_PH; }
static const char* phName(const SensorDriver* d) { (void)d; return "ph4502c"; }
static uint8_t phMetricCount(const SensorDriver* d) { (void)d; return 1; }

static DriverStatus phInit(SensorDriver* d) {
  asPh(d)->initialised_ = false;
  return DRIVER_NOT_IMPLEMENTED;
}

static DriverStatus phStart(SensorDriver* d, MonoMs now) {
  (void)d;
  (void)now;
  return DRIVER_NOT_IMPLEMENTED;
}

static PollResult phPoll(SensorDriver* d, MonoMs now) {
  (void)d;
  PollResult r;
  r.status = DRIVER_NOT_IMPLEMENTED;
  r.nextPollMs = now;
  return r;
}

static uint8_t phReadResult(SensorDriver* d, Reading* out, uint8_t maxOut) {
  (void)d;
  (void)out;
  (void)maxOut;
  return 0;
}

static void phCancel(SensorDriver* d) { (void)d; }

static void phPreparePowerOff(SensorDriver* d) { (void)d; }

static const SensorDriverOps kPhOps = {
    .id = phId,
    .name = phName,
    .metricCount = phMetricCount,
    .init = phInit,
    .start = phStart,
    .poll = phPoll,
    .readResult = phReadResult,
    .cancel = phCancel,
    .preparePowerOff = phPreparePowerOff,
};

void phDriverConstruct(PhDriver* self, const PhConfig* config) {
  self->base.ops = &kPhOps;
  self->config_ = *config;
  self->initialised_ = false;
}

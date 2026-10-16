#include "drivers/light.h"

// TODO(WP3): Stage 1 stub - replace with the real implementation.

static inline LightDriver* asLight(SensorDriver* d) { return (LightDriver*)d; }

static SensorId lightId(const SensorDriver* d) { (void)d; return SENSOR_LIGHT; }
static const char* lightName(const SensorDriver* d) { (void)d; return "dfr0026_light"; }
static uint8_t lightMetricCount(const SensorDriver* d) { (void)d; return 1; }

static DriverStatus lightInit(SensorDriver* d) {
  asLight(d)->initialised_ = false;
  return DRIVER_NOT_IMPLEMENTED;
}

static DriverStatus lightStart(SensorDriver* d, MonoMs now) {
  (void)d;
  (void)now;
  return DRIVER_NOT_IMPLEMENTED;
}

static PollResult lightPoll(SensorDriver* d, MonoMs now) {
  (void)d;
  PollResult r;
  r.status = DRIVER_NOT_IMPLEMENTED;
  r.nextPollMs = now;
  return r;
}

static uint8_t lightReadResult(SensorDriver* d, Reading* out, uint8_t maxOut) {
  (void)d;
  (void)out;
  (void)maxOut;
  return 0;
}

static void lightCancel(SensorDriver* d) { (void)d; }

static void lightPreparePowerOff(SensorDriver* d) { (void)d; }

static const SensorDriverOps kLightOps = {
    .id = lightId,
    .name = lightName,
    .metricCount = lightMetricCount,
    .init = lightInit,
    .start = lightStart,
    .poll = lightPoll,
    .readResult = lightReadResult,
    .cancel = lightCancel,
    .preparePowerOff = lightPreparePowerOff,
};

void lightDriverConstruct(LightDriver* self, const LightConfig* config) {
  self->base.ops = &kLightOps;
  self->config_ = *config;
  self->initialised_ = false;
}

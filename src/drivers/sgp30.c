#include "drivers/sgp30.h"

// TODO(WP6): Stage 1 stub - replace with the real implementation.

static inline Sgp30Driver* asSgp30(SensorDriver* d) { return (Sgp30Driver*)d; }

static SensorId sgp30Id(const SensorDriver* d) { (void)d; return SENSOR_SGP30; }
static const char* sgp30Name(const SensorDriver* d) { (void)d; return "sgp30"; }
static uint8_t sgp30MetricCount(const SensorDriver* d) { (void)d; return SGP30_METRICS; }

static DriverStatus sgp30Init(SensorDriver* d) {
  asSgp30(d)->initialised_ = false;
  return DRIVER_NOT_IMPLEMENTED;
}

static DriverStatus sgp30Start(SensorDriver* d, MonoMs now) {
  (void)d;
  (void)now;
  return DRIVER_NOT_IMPLEMENTED;
}

static PollResult sgp30Poll(SensorDriver* d, MonoMs now) {
  (void)d;
  PollResult r;
  r.status = DRIVER_NOT_IMPLEMENTED;
  r.nextPollMs = now;
  return r;
}

static uint8_t sgp30ReadResult(SensorDriver* d, Reading* out, uint8_t maxOut) {
  (void)d;
  (void)out;
  (void)maxOut;
  return 0;
}

static void sgp30Cancel(SensorDriver* d) { (void)d; }

static void sgp30PreparePowerOff(SensorDriver* d) { (void)d; }

static const SensorDriverOps kSgp30Ops = {
    .id = sgp30Id,
    .name = sgp30Name,
    .metricCount = sgp30MetricCount,
    .init = sgp30Init,
    .start = sgp30Start,
    .poll = sgp30Poll,
    .readResult = sgp30ReadResult,
    .cancel = sgp30Cancel,
    .preparePowerOff = sgp30PreparePowerOff,
};

void sgp30DriverConstruct(Sgp30Driver* self, const Sgp30Config* config) {
  self->base.ops = &kSgp30Ops;
  self->config_ = *config;
  self->initialised_ = false;
}

#include "drivers/ds18b20.h"

// TODO(WP2): Stage 1 stub - replace with the real implementation.

static inline Ds18b20Driver* asDs18b20(SensorDriver* d) { return (Ds18b20Driver*)d; }

static SensorId ds18b20Id(const SensorDriver* d) { (void)d; return SENSOR_DS18B20; }
static const char* ds18b20Name(const SensorDriver* d) { (void)d; return "ds18b20"; }
static uint8_t ds18b20MetricCount(const SensorDriver* d) { (void)d; return 1; }

static DriverStatus ds18b20Init(SensorDriver* d) {
  asDs18b20(d)->initialised_ = false;
  return DRIVER_NOT_IMPLEMENTED;
}

static DriverStatus ds18b20Start(SensorDriver* d, MonoMs now) {
  (void)d;
  (void)now;
  return DRIVER_NOT_IMPLEMENTED;
}

static PollResult ds18b20Poll(SensorDriver* d, MonoMs now) {
  (void)d;
  PollResult r;
  r.status = DRIVER_NOT_IMPLEMENTED;
  r.nextPollMs = now;
  return r;
}

static uint8_t ds18b20ReadResult(SensorDriver* d, Reading* out, uint8_t maxOut) {
  (void)d;
  (void)out;
  (void)maxOut;
  return 0;
}

static void ds18b20Cancel(SensorDriver* d) { (void)d; }

static void ds18b20PreparePowerOff(SensorDriver* d) { (void)d; }

static const SensorDriverOps kDs18b20Ops = {
    .id = ds18b20Id,
    .name = ds18b20Name,
    .metricCount = ds18b20MetricCount,
    .init = ds18b20Init,
    .start = ds18b20Start,
    .poll = ds18b20Poll,
    .readResult = ds18b20ReadResult,
    .cancel = ds18b20Cancel,
    .preparePowerOff = ds18b20PreparePowerOff,
};

void ds18b20DriverConstruct(Ds18b20Driver* self, const Ds18b20Config* config) {
  self->base.ops = &kDs18b20Ops;
  self->config_ = *config;
  self->initialised_ = false;
}

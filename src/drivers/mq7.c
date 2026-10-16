#include "drivers/mq7.h"

// TODO(WP6): Stage 1 stub - replace with the real implementation.

static inline Mq7Driver* asMq7(SensorDriver* d) { return (Mq7Driver*)d; }

static SensorId mq7Id(const SensorDriver* d) { (void)d; return SENSOR_MQ7; }
static const char* mq7Name(const SensorDriver* d) { (void)d; return "mq7"; }
static uint8_t mq7MetricCount(const SensorDriver* d) { (void)d; return 1; }

static DriverStatus mq7Init(SensorDriver* d) {
  asMq7(d)->initialised_ = false;
  return DRIVER_NOT_IMPLEMENTED;
}

static DriverStatus mq7Start(SensorDriver* d, MonoMs now) {
  (void)d;
  (void)now;
  return DRIVER_NOT_IMPLEMENTED;
}

static PollResult mq7Poll(SensorDriver* d, MonoMs now) {
  (void)d;
  PollResult r;
  r.status = DRIVER_NOT_IMPLEMENTED;
  r.nextPollMs = now;
  return r;
}

static uint8_t mq7ReadResult(SensorDriver* d, Reading* out, uint8_t maxOut) {
  (void)d;
  (void)out;
  (void)maxOut;
  return 0;
}

static void mq7Cancel(SensorDriver* d) { (void)d; }

static void mq7PreparePowerOff(SensorDriver* d) { (void)d; }

static const SensorDriverOps kMq7Ops = {
    .id = mq7Id,
    .name = mq7Name,
    .metricCount = mq7MetricCount,
    .init = mq7Init,
    .start = mq7Start,
    .poll = mq7Poll,
    .readResult = mq7ReadResult,
    .cancel = mq7Cancel,
    .preparePowerOff = mq7PreparePowerOff,
};

void mq7DriverConstruct(Mq7Driver* self, const Mq7Config* config) {
  self->base.ops = &kMq7Ops;
  self->config_ = *config;
  self->initialised_ = false;
}

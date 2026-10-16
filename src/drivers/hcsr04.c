#include "drivers/hcsr04.h"

// TODO(WP4): Stage 1 stub - replace with the real implementation.

static inline Hcsr04Driver* asHcsr04(SensorDriver* d) { return (Hcsr04Driver*)d; }

static SensorId hcsr04Id(const SensorDriver* d) { (void)d; return SENSOR_HCSR04; }
static const char* hcsr04Name(const SensorDriver* d) { (void)d; return "hcsr04"; }
static uint8_t hcsr04MetricCount(const SensorDriver* d) { (void)d; return 1; }

static DriverStatus hcsr04Init(SensorDriver* d) {
  asHcsr04(d)->initialised_ = false;
  return DRIVER_NOT_IMPLEMENTED;
}

static DriverStatus hcsr04Start(SensorDriver* d, MonoMs now) {
  (void)d;
  (void)now;
  return DRIVER_NOT_IMPLEMENTED;
}

static PollResult hcsr04Poll(SensorDriver* d, MonoMs now) {
  (void)d;
  PollResult r;
  r.status = DRIVER_NOT_IMPLEMENTED;
  r.nextPollMs = now;
  return r;
}

static uint8_t hcsr04ReadResult(SensorDriver* d, Reading* out, uint8_t maxOut) {
  (void)d;
  (void)out;
  (void)maxOut;
  return 0;
}

static void hcsr04Cancel(SensorDriver* d) { (void)d; }

static void hcsr04PreparePowerOff(SensorDriver* d) { (void)d; }

static const SensorDriverOps kHcsr04Ops = {
    .id = hcsr04Id,
    .name = hcsr04Name,
    .metricCount = hcsr04MetricCount,
    .init = hcsr04Init,
    .start = hcsr04Start,
    .poll = hcsr04Poll,
    .readResult = hcsr04ReadResult,
    .cancel = hcsr04Cancel,
    .preparePowerOff = hcsr04PreparePowerOff,
};

void hcsr04DriverConstruct(Hcsr04Driver* self, const Hcsr04Config* config) {
  self->base.ops = &kHcsr04Ops;
  self->config_ = *config;
  self->initialised_ = false;
}

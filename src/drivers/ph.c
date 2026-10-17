#include "drivers/ph.h"

static const float kPhMin = 0.0f, kPhMax = 14.0f;

static inline PhDriver* asPh(SensorDriver* d) { return (PhDriver*)d; }

static PollResult finish(PhDriver* self, MonoMs now);

static SensorId phId(const SensorDriver* d) { (void)d; return SENSOR_PH; }
static const char* phName(const SensorDriver* d) { (void)d; return "ph4502c"; }
static uint8_t phMetricCount(const SensorDriver* d) { (void)d; return 1; }

static DriverStatus phInit(SensorDriver* d) {
  PhDriver* self = asPh(d);
  self->initialised_ = false;
  self->busy_ = false;
  if (self->config_.adc == 0 || self->config_.dividerBottomOhm == 0) return DRIVER_FAULT;
  // Throwaway conversion: fails if the channel was never begun on an ADC1 pin,
  // and keeps any one-time ADC characterisation cost out of poll().
  uint32_t mv = 0;
  bool saturated = false;
  if (!adcChannelReadMilliVolts(self->config_.adc, &mv, &saturated)) return DRIVER_FAULT;
  self->initialised_ = true;
  return DRIVER_OK;
}

// Take one sample of the window. false on an ADC error.
static bool sample(PhDriver* self) {
  uint32_t mv = 0;
  bool saturated = false;
  if (!adcChannelReadMilliVolts(self->config_.adc, &mv, &saturated)) return false;
  self->samples_[self->count_++] = mv;
  self->saturated_ = self->saturated_ || saturated;
  return true;
}

static DriverStatus phStart(SensorDriver* d, MonoMs now) {
  PhDriver* self = asPh(d);
  if (!self->initialised_) return DRIVER_FAULT;
  self->count_ = 0;
  self->saturated_ = false;
  self->startMs_ = now;
  if (!sample(self)) {
    self->initialised_ = false;
    return DRIVER_FAULT;
  }
  self->busy_ = true;
  return DRIVER_OK;
}

static PollResult phPoll(SensorDriver* d, MonoMs now) {
  PhDriver* self = asPh(d);
  PollResult r = {DRIVER_FAULT, now};
  if (!self->busy_) return r;

  const MonoMs due = self->startMs_ + (MonoMs)self->count_ * PH_SAMPLE_SPACING_MS;
  if (now < due) {
    r.status = DRIVER_PENDING;
    r.nextPollMs = due;
    return r;
  }
  if (!sample(self)) {
    self->busy_ = false;
    self->initialised_ = false;
    self->result_.health = HEALTH_FAULT;
    self->result_.intervalStartMs = self->startMs_;
    self->result_.intervalEndMs = now;
    return r;
  }
  if (self->count_ < PH_SAMPLES) {
    r.status = DRIVER_PENDING;
    r.nextPollMs = self->startMs_ + (MonoMs)self->count_ * PH_SAMPLE_SPACING_MS;
    return r;
  }
  return finish(self, now);
}

static PollResult finish(PhDriver* self, MonoMs now) {
  self->busy_ = false;
  const PhConfig* cfg = &self->config_;
  const float pinMv = trimmedMeanU32(self->samples_, PH_SAMPLES, PH_TRIM);
  const float moduleMv = dividerRecoverMv(pinMv, cfg->dividerTopOhm, cfg->dividerBottomOhm);

  Reading* r = &self->result_;
  r->intervalStartMs = self->startMs_;
  r->intervalEndMs = now;
  r->hasRaw = true;
  r->raw = (int32_t)(pinMv + 0.5f);
  r->qualityFlags = self->saturated_ ? kQualitySaturated : kQualityNone;
  r->calibrationRevision = cfg->calibration.revision;

  // READY in every case below: health carries the verdict, raw the level.
  PollResult pr = {DRIVER_READY, now};
  if (pinMv < (float)PH_NO_SIGNAL_PIN_MV) {
    r->health = HEALTH_UNPOWERED;
    return pr;
  }
  if (self->saturated_) {
    r->health = HEALTH_FAULT;
    return pr;
  }

  if (!cfg->calibration.valid) {
    r->unit = UNIT_MILLIVOLT;
    r->value = moduleMv;
    r->health = HEALTH_UNCALIBRATED;
    ++r->sequence;  // a fresh sample, in raw units
    return pr;
  }
  const float ph = linearApply(&cfg->calibration, moduleMv);
  r->unit = UNIT_PH;
  if (ph < kPhMin || ph > kPhMax) {
    r->health = HEALTH_FAULT;
    return pr;
  }
  r->value = ph;
  ++r->sequence;
  if (cfg->calibrationProvisional) {  // pH units, but not buffer-verified (D13)
    r->health = HEALTH_UNCALIBRATED;
    return pr;
  }
  r->health = HEALTH_OK;
  r->lastGoodMs = now;
  return pr;
}

static uint8_t phReadResult(SensorDriver* d, Reading* out, uint8_t maxOut) {
  if (maxOut < 1) return 0;
  out[0] = asPh(d)->result_;
  return 1;
}

static void phCancel(SensorDriver* d) {
  PhDriver* self = asPh(d);
  self->busy_ = false;
  self->count_ = 0;
}

// The ADC pin is a high-impedance input behind the divider: nothing to drive
// before the 5 V rail goes down. Drop any partial window.
static void phPreparePowerOff(SensorDriver* d) { phCancel(d); }

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
  self->busy_ = false;
  self->saturated_ = false;
  self->count_ = 0;
  self->startMs_ = 0;
  readingReset(&self->result_, SENSOR_PH, METRIC_PH, UNIT_PH);
  self->result_.calibrationRevision = config->calibration.revision;
}

#include "domain/calibration.h"

float dividerRecoverMv(float adcMv, uint32_t rTopOhm, uint32_t rBottomOhm) {
  if (rBottomOhm == 0) return 0.0f;
  return adcMv * (float)(rTopOhm + rBottomOhm) / (float)rBottomOhm;
}

float linearApply(const LinearCalibration* c, float x) {
  return c->slope * x + c->offset;
}

bool linearFromTwoPoints(float x1, float y1, float x2, float y2, uint16_t revision,
                         LinearCalibration* out) {
  out->slope = 1.0f;
  out->offset = 0.0f;
  out->revision = revision;
  out->valid = false;
  const float dx = x2 - x1;
  if (dx > -1e-3f && dx < 1e-3f) return false;
  out->slope = (y2 - y1) / dx;
  out->offset = y1 - out->slope * x1;
  out->valid = true;
  return true;
}

float trimmedMeanU32(uint32_t* samples, uint8_t n, uint8_t trim) {
  if (n <= 2u * trim) return 0.0f;
  for (uint8_t i = 1; i < n; ++i) {  // insertion sort: n is ~10
    const uint32_t v = samples[i];
    uint8_t j = i;
    while (j > 0 && samples[j - 1] > v) {
      samples[j] = samples[j - 1];
      --j;
    }
    samples[j] = v;
  }
  uint64_t sum = 0;
  for (uint8_t i = trim; i < n - trim; ++i) sum += samples[i];
  return (float)sum / (float)(n - 2u * trim);
}

#include "domain/calibration.h"

float dividerRecoverMv(float adcMv, uint32_t rTopOhm, uint32_t rBottomOhm) {
  if (rBottomOhm == 0) return 0.0f;
  return adcMv * (float)(rTopOhm + rBottomOhm) / (float)rBottomOhm;
}

float linearApply(const LinearCalibration* c, float x) {
  return c->slope * x + c->offset;
}

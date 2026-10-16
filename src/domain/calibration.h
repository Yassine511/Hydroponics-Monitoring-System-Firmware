// Unit conversion and calibration stages (§3, §14). Pure C.
// Work packages: WP5 (pH/TDS), WP6 (MQ-7), WP3 (light).
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Recover the module output voltage from the ADC pin voltage through a
// resistor divider: Vmodule = Vadc * (Rtop + Rbottom) / Rbottom.
// Use MEASURED resistor values once available (§3).
float dividerRecoverMv(float adcMv, uint32_t rTopOhm, uint32_t rBottomOhm);

// Linear fit y = slope * x + offset, with a revision for traceability.
typedef struct LinearCalibration {
  float slope;
  float offset;
  uint16_t revision;
  bool valid;  // false => publish raw with HEALTH_UNCALIBRATED
} LinearCalibration;

float linearApply(const LinearCalibration* c, float x);

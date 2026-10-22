// Unit conversion and calibration stages (§3, §14). Pure C.
// Work packages: WP5 (pH/TDS), WP3 (light).
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

// Two-point fit through (x1, y1) and (x2, y2), e.g. module mV read in two pH
// buffers. false (and *out left invalid) if the x values are too close.
bool linearFromTwoPoints(float x1, float y1, float x2, float y2, uint16_t revision,
                         LinearCalibration* out);

// Sort `samples` in place, drop `trim` values at each end and return the mean
// of the rest (spike-rejecting filter, pH-4502C tutorial: 10 samples, mean of
// the middle 6). Returns 0 if n <= 2 * trim.
float trimmedMeanU32(uint32_t* samples, uint8_t n, uint8_t trim);

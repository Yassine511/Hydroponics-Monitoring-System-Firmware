#include "hal/adc_channel.h"

#include <stddef.h>

// TODO(WP3): analogSetPinAttenuation + analogReadMilliVolts (eFuse calibration);
// characterise the useful range (~2.45 V upper at 11 dB, §3).

bool adcChannelBegin(AdcChannel* ch, int8_t pin) { ch->pin_ = pin; return false; }

bool adcChannelReadMilliVolts(AdcChannel* ch, uint32_t* mv, bool* saturated) {
  (void)ch;
  if (mv) *mv = 0;
  if (saturated) *saturated = false;
  return false;
}

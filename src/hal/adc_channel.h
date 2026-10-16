// ADC1 channel adapter (HAL). Work package WP3 (shared by light, pH, TDS, MQ-7).
// ADC1 only: ADC2 conflicts with Wi-Fi (§3). Returns calibrated millivolts at
// the ESP32 pin; divider recovery and units belong to domain/calibration.h.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Treat the members as private to adc_channel.c. Initialise with ADC_CHANNEL_INIT.
typedef struct AdcChannel {
  int8_t pin_;
} AdcChannel;

#define ADC_CHANNEL_INIT {-1}

bool adcChannelBegin(AdcChannel* ch, int8_t pin);
// One conversion. false on error. Reports saturation near full scale.
bool adcChannelReadMilliVolts(AdcChannel* ch, uint32_t* mv, bool* saturated);

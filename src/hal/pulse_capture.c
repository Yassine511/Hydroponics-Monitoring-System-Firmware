#include "hal/pulse_capture.h"

#include <stddef.h>

// TODO(WP4): implement with RMT RX or GPIO edge ISR + esp_timer timestamps.

bool pulseCaptureBegin(PulseCapture* pc, int8_t trigPin, int8_t echoPin) {
  pc->trig_ = trigPin;
  pc->echo_ = echoPin;
  return false;
}
bool pulseCaptureTrigger(PulseCapture* pc) { (void)pc; return false; }
CaptureState pulseCapturePoll(PulseCapture* pc, uint32_t* widthUs) {
  (void)pc;
  if (widthUs) *widthUs = 0;
  return CAPTURE_ERROR;
}
void pulseCaptureCancel(PulseCapture* pc) { (void)pc; }
void pulseCaptureSetSafe(PulseCapture* pc) { (void)pc; }

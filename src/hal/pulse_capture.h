// Echo-pulse capture (HAL). Work package WP4 (with HC-SR04).
// Use RMT or an audited edge-capture ISR - not pulseIn() (§2, §7).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef uint8_t CaptureState;
enum { CAPTURE_IDLE = 0, CAPTURE_WAITING, CAPTURE_DONE, CAPTURE_TIMEOUT, CAPTURE_ERROR };

// Treat the members as private to pulse_capture.c. Initialise with PULSE_CAPTURE_INIT.
typedef struct PulseCapture {
  int8_t trig_;
  int8_t echo_;
} PulseCapture;

#define PULSE_CAPTURE_INIT {-1, -1}

bool pulseCaptureBegin(PulseCapture* pc, int8_t trigPin, int8_t echoPin);
bool pulseCaptureTrigger(PulseCapture* pc);                     // 10 us trigger pulse, arm capture
CaptureState pulseCapturePoll(PulseCapture* pc, uint32_t* widthUs);  // non-blocking
void pulseCaptureCancel(PulseCapture* pc);
void pulseCaptureSetSafe(PulseCapture* pc);                     // TRIG low, ECHO input, no pulls (§12)

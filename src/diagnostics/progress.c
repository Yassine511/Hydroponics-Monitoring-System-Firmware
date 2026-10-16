#include "diagnostics/progress.h"

#include "hal/clock.h"

// TODO(WP10): make cross-core safe (64-bit values need portMUX) and track
// expected membership for the inactive acquisition worker.
static MonoMs s_lastMs[TASK_ID_COUNT];

void progressMark(TaskId task) {
  s_lastMs[task] = monoNowMs();
}

MonoMs progressLastMs(TaskId task) {
  return s_lastMs[task];
}

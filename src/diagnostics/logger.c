#include "diagnostics/logger.h"

#include <stdarg.h>
#include <stdio.h>

void logInit(void) {}

void logPrintf(LogLevel level, const char* fmt, ...) {
  static const char* const kTags[] = {"E", "W", "I", "D"};
  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  // TEMPORARY direct write (Stage 1). Must not be called from ISRs.
  printf("[%s] %s\n", kTags[level], buf);
}

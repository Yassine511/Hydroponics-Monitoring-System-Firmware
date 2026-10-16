// Logger: bounded queue to Serial, drop-and-count, never blocks callers.
// Work package WP10 (Stage 4). Architecture §9, §16.
//
// Stage 1: logPrintf() writes straight to the serial console (UART0, the
// port Arduino's Serial used) so bring-ups have output. WP10 replaces it with
// the 32-entry queue + SerialLogger task (core 1).
#pragma once

typedef unsigned char LogLevel;
enum { LOG_LEVEL_ERROR = 0, LOG_LEVEL_WARN, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG };

void logInit(void);
void logPrintf(LogLevel level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

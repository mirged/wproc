#ifndef WPROC_UTIL_TIME_H
#define WPROC_UTIL_TIME_H

#include "util/types.h"

// Monotonic clock.
u64  time_now_ns(void);
u64  time_now_ms(void);

// Sleeps the full duration, resuming after signal interruptions.
void time_sleep_ms(u64 ms);

// "3d 04:12:09" or "04:12:09". `out` needs 32 bytes; returns length.
usize time_format_uptime(char *out, u64 seconds);

// top-style TIME+: "123:45.67" (minutes:seconds.hundredths).
// `out` needs 32 bytes; returns length.
usize time_format_cpu(char *out, u64 ms);

#endif // WPROC_UTIL_TIME_H

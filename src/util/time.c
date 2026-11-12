#include "util/time.h"
#include "util/string.h"
#include "platform/linux.h"

#define NS_PER_SEC 1000000000UL
#define NS_PER_MS  1000000UL

u64 time_now_ns(void) {
    struct timespec ts;
    if (sys_clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    return (u64)ts.tv_sec * NS_PER_SEC + (u64)ts.tv_nsec;
}

u64 time_now_ms(void) {
    return time_now_ns() / NS_PER_MS;
}

void time_sleep_ms(u64 ms) {
    struct timespec ts = {
        .tv_sec = (i64)(ms / 1000),
        .tv_nsec = (i64)((ms % 1000) * NS_PER_MS),
    };
    while (sys_nanosleep(&ts, &ts) == -EINTR) {
    }
}

static usize two_digits(char *out, u64 v) {
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
    return 2;
}

usize time_format_uptime(char *out, u64 seconds) {
    u64 days = seconds / 86400;
    u64 h = (seconds / 3600) % 24;
    u64 m = (seconds / 60) % 60;
    u64 s = seconds % 60;

    usize n = 0;
    if (days) {
        n += str_from_u64(out + n, days);
        out[n++] = 'd';
        out[n++] = ' ';
    }
    n += two_digits(out + n, h);
    out[n++] = ':';
    n += two_digits(out + n, m);
    out[n++] = ':';
    n += two_digits(out + n, s);
    return n;
}

usize time_format_cpu(char *out, u64 ms) {
    u64 cs = ms / 10;
    usize n = str_from_u64(out, cs / 6000);
    out[n++] = ':';
    n += two_digits(out + n, (cs / 100) % 60);
    out[n++] = '.';
    n += two_digits(out + n, cs % 100);
    return n;
}

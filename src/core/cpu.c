#include "core/cpu.h"

u64 cpu_times_total(const struct procfs_cpu_times *t) {
    return t->user + t->nice + t->system + t->idle +
           t->iowait + t->irq + t->softirq + t->steal;
}

// Counters are monotonic in theory. CPU hotplug disagrees.
static u64 delta(u64 prev, u64 cur) {
    return cur > prev ? cur - prev : 0;
}

static u32 permille(u64 part, u64 whole) {
    if (whole == 0) return 0;
    u64 v = part * 1000 / whole;
    return (u32)MIN(v, 1000UL);
}

void cpu_usage_compute(const struct procfs_cpu_times *prev,
                       const struct procfs_cpu_times *cur,
                       u32 ncpu, struct cpu_usage *out) {
    u64 user   = delta(prev->user, cur->user) + delta(prev->nice, cur->nice);
    u64 system = delta(prev->system, cur->system) +
                 delta(prev->irq, cur->irq) +
                 delta(prev->softirq, cur->softirq);
    u64 idle   = delta(prev->idle, cur->idle);
    u64 iowait = delta(prev->iowait, cur->iowait);
    u64 steal  = delta(prev->steal, cur->steal);
    u64 total  = user + system + idle + iowait + steal;

    out->user_permille   = permille(user, total);
    out->system_permille = permille(system, total);
    out->iowait_permille = permille(iowait, total);
    out->steal_permille  = permille(steal, total);
    out->total_permille  = permille(user + system + steal, total);
    out->ncpu = ncpu;
}

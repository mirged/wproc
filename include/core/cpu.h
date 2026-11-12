#ifndef WPROC_CORE_CPU_H
#define WPROC_CORE_CPU_H

#include "util/types.h"
#include "platform/procfs.h"

// System-wide CPU utilisation over one interval, in permille of all CPUs.
struct cpu_usage {
    u32 total_permille;    // everything except idle and iowait
    u32 user_permille;     // user + nice
    u32 system_permille;   // system + irq + softirq
    u32 iowait_permille;
    u32 steal_permille;
    u32 ncpu;
};

u64  cpu_times_total(const struct procfs_cpu_times *t);

void cpu_usage_compute(const struct procfs_cpu_times *prev,
                       const struct procfs_cpu_times *cur,
                       u32 ncpu, struct cpu_usage *out);

#endif // WPROC_CORE_CPU_H

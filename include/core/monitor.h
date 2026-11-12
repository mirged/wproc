#ifndef WPROC_CORE_MONITOR_H
#define WPROC_CORE_MONITOR_H

#include "util/types.h"
#include "util/buffer.h"
#include "util/vector.h"
#include "core/process.h"
#include "core/cpu.h"
#include "core/memory.h"

// Everything a frontend needs to draw one frame. Produced by the monitor
// (live) or by the recording player (playback); the UI does not care which.
struct snapshot {
    const char *hostname;    // borrowed, never NULL
    u64 time_ms;             // monotonic timestamp of the sample
    u64 uptime_s;
    u32 load_x100[3];
    u32 tasks_total;
    u32 tasks_running;
    u32 spawned;
    u32 exited;
    struct cpu_usage cpu;
    struct mem_usage mem;
    const struct vector *procs;  // of struct process, borrowed from the producer
};

struct monitor {
    struct process_table table;
    struct procfs_cpu    cpu_prev;
    bool                 have_cpu_prev;
    u64                  clk_tck;
    u64                  page_kb;
    struct vector        pids;     // scratch, of u32
    struct buffer        scratch;  // scratch for file reads
    struct snapshot      snap;
    u64                  updates;
    char                 hostname[PROCFS_HOSTNAME_MAX];
};

bool monitor_init(struct monitor *m, u64 clk_tck, u64 page_size);
void monitor_free(struct monitor *m);

// Take a new sample. The first update establishes baselines, so CPU
// percentages are only meaningful from the second one onwards.
bool monitor_update(struct monitor *m);

const struct snapshot *monitor_snapshot(const struct monitor *m);

#endif // WPROC_CORE_MONITOR_H

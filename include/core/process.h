#ifndef WPROC_CORE_PROCESS_H
#define WPROC_CORE_PROCESS_H

#include "util/types.h"
#include "util/vector.h"
#include "platform/procfs.h"

#define PROCESS_CMDLINE_MAX 256

// A tracked process. Values are already converted to display-friendly
// units; tick counts never leave core/.
struct process {
    u32  pid;
    u32  ppid;
    char state;
    char comm[PROCFS_COMM_MAX];
    char cmdline[PROCESS_CMDLINE_MAX];
    i32  nice;
    u32  threads;
    u32  cpu_permille;   // share of ONE cpu over the last interval (may exceed 1000)
    u64  cpu_time_ms;    // total user+system time
    u64  rss_kb;
    u64  vsize_kb;
    u64  start_ticks;    // identifies the process across pid reuse

    // internal bookkeeping
    u64  cpu_ticks;
    u64  generation;
};

// All live processes, kept sorted by pid.
struct process_table {
    struct vector procs;   // of struct process
    u64 generation;
    u32 spawned;           // during the last update
    u32 exited;
};

void process_table_init(struct process_table *t);
void process_table_free(struct process_table *t);

struct process *process_table_find(const struct process_table *t, u32 pid);

// An update is: begin, observe each sample, sweep.
void process_table_begin(struct process_table *t);

// Record a sample. `elapsed_ticks` is the per-cpu interval length in clock
// ticks (0 on the first update). Returns the entry, or NULL on OOM.
// `is_new` reports whether this pid (or a reused pid) was not seen before;
// the returned pointer is only valid until the next observe.
struct process *process_table_observe(struct process_table *t,
                                      const struct procfs_process *sample,
                                      u64 elapsed_ticks, u64 clk_tck, u64 page_kb,
                                      bool *is_new);

// Drop every process that was not observed since begin.
void process_table_sweep(struct process_table *t);

#endif // WPROC_CORE_PROCESS_H

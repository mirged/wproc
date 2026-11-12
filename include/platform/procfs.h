#ifndef WPROC_PLATFORM_PROCFS_H
#define WPROC_PLATFORM_PROCFS_H

#include "util/types.h"
#include "util/buffer.h"
#include "util/vector.h"

// The Linux backend. Everything above this header works on the structs
// below and never sees raw /proc text. Every reader takes a caller-owned
// scratch buffer so nothing here allocates on its own.

// ---- paths & enumeration -------------------------------------------------

// "/proc/<pid>/<leaf>" into out. Returns length (0 if it did not fit).
usize procfs_pid_path(char *out, usize cap, u32 pid, const char *leaf);

// Fill `pids` (vector of u32) with every numeric entry in /proc.
bool procfs_list_pids(struct vector *pids);

// ---- per-process ---------------------------------------------------------

#define PROCFS_COMM_MAX 16  // TASK_COMM_LEN, including the terminator

struct procfs_process {
    u32  pid;
    u32  ppid;
    char state;               // R, S, D, Z, T, I, ...
    char comm[PROCFS_COMM_MAX];
    i32  nice;
    u32  threads;
    u64  utime;               // clock ticks
    u64  stime;               // clock ticks
    u64  start_time;          // clock ticks since boot
    u64  vsize;               // bytes
    u64  rss_pages;
};

// Parse /proc/<pid>/stat. Returns false if the process vanished
// (a normal, constant race) or the file was malformed.
bool procfs_read_process(u32 pid, struct procfs_process *out, struct buffer *scratch);

// /proc/<pid>/cmdline with NULs turned into spaces. Kernel threads have
// an empty command line; that is reported as success with out[0] == 0.
bool procfs_read_cmdline(u32 pid, char *out, usize cap, struct buffer *scratch);

// ---- system CPU ----------------------------------------------------------

struct procfs_cpu_times {
    u64 user, nice, system, idle, iowait, irq, softirq, steal;
};

struct procfs_cpu {
    struct procfs_cpu_times total;  // aggregate "cpu" line
    u32 ncpu;                       // number of "cpuN" lines
    u64 procs_running;
    u64 procs_blocked;
};

bool procfs_read_cpu(struct procfs_cpu *out, struct buffer *scratch);

struct procfs_loadavg {
    u64 load_x100[3];   // 1, 5, 15 minutes, fixed point
    u64 running;
    u64 total;
};

bool procfs_read_loadavg(struct procfs_loadavg *out, struct buffer *scratch);
bool procfs_read_uptime(u64 *seconds, struct buffer *scratch);

// ---- system memory -------------------------------------------------------

struct procfs_meminfo {
    u64  total_kb;
    u64  free_kb;
    u64  available_kb;
    u64  buffers_kb;
    u64  cached_kb;
    u64  sreclaimable_kb;
    u64  shmem_kb;
    u64  swap_total_kb;
    u64  swap_free_kb;
    bool has_available;   // MemAvailable appeared in Linux 3.14
};

bool procfs_read_meminfo(struct procfs_meminfo *out, struct buffer *scratch);

#endif // WPROC_PLATFORM_PROCFS_H

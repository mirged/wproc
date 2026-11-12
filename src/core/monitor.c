#include "core/monitor.h"
#include "util/string.h"
#include "util/time.h"

bool monitor_init(struct monitor *m, u64 clk_tck, u64 page_size) {
    mem_set(m, 0, sizeof(*m));
    process_table_init(&m->table);
    vec_init(&m->pids, sizeof(u32));
    buf_init(&m->scratch);
    m->clk_tck = clk_tck ? clk_tck : 100;
    m->page_kb = page_size >= 1024 ? page_size / 1024 : 4;
    m->snap.procs = &m->table.procs;
    return true;
}

void monitor_free(struct monitor *m) {
    process_table_free(&m->table);
    vec_free(&m->pids);
    buf_free(&m->scratch);
}

static u64 sample_cpu(struct monitor *m, const struct procfs_cpu *cpu) {
    if (!m->have_cpu_prev) {
        mem_set(&m->snap.cpu, 0, sizeof(m->snap.cpu));
        m->snap.cpu.ncpu = cpu->ncpu;
        return 0;
    }

    cpu_usage_compute(&m->cpu_prev.total, &cpu->total, cpu->ncpu, &m->snap.cpu);

    u64 before = cpu_times_total(&m->cpu_prev.total);
    u64 now = cpu_times_total(&cpu->total);
    u64 elapsed = now > before ? now - before : 0;
    return elapsed / (cpu->ncpu ? cpu->ncpu : 1);
}

static bool sample_processes(struct monitor *m, u64 elapsed_ticks) {
    if (!procfs_list_pids(&m->pids)) return false;

    process_table_begin(&m->table);
    u32 running = 0;

    for (usize i = 0; i < m->pids.len; i++) {
        u32 pid = *(u32 *)vec_at(&m->pids, i);

        struct procfs_process raw;
        if (!procfs_read_process(pid, &raw, &m->scratch)) continue;  // exited mid-scan

        bool fresh;
        struct process *p = process_table_observe(&m->table, &raw, elapsed_ticks,
                                                  m->clk_tck, m->page_kb, &fresh);
        if (!p) return false;

        // cmdline does not change after exec() often enough to re-read
        // hundreds of files every second.
        if (fresh) procfs_read_cmdline(pid, p->cmdline, sizeof(p->cmdline), &m->scratch);
        if (raw.state == 'R') running++;
    }

    process_table_sweep(&m->table);

    m->snap.tasks_total = (u32)m->table.procs.len;
    m->snap.tasks_running = running;
    m->snap.spawned = m->table.spawned;
    m->snap.exited = m->table.exited;
    return true;
}

bool monitor_update(struct monitor *m) {
    struct procfs_cpu cpu;
    if (!procfs_read_cpu(&cpu, &m->scratch)) return false;
    u64 elapsed = sample_cpu(m, &cpu);
    m->cpu_prev = cpu;
    m->have_cpu_prev = true;

    struct procfs_meminfo mem;
    if (!procfs_read_meminfo(&mem, &m->scratch)) return false;
    mem_usage_compute(&mem, &m->snap.mem);

    // Nice to have; a missing /proc/loadavg should not stop the show.
    struct procfs_loadavg load;
    if (procfs_read_loadavg(&load, &m->scratch)) {
        for (usize i = 0; i < 3; i++) m->snap.load_x100[i] = (u32)load.load_x100[i];
    }
    procfs_read_uptime(&m->snap.uptime_s, &m->scratch);

    if (!sample_processes(m, elapsed)) return false;

    m->snap.time_ms = time_now_ms();
    m->snap.procs = &m->table.procs;
    m->updates++;
    return true;
}

const struct snapshot *monitor_snapshot(const struct monitor *m) {
    return &m->snap;
}

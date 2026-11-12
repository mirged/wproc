#include "core/process.h"
#include "util/string.h"

void process_table_init(struct process_table *t) {
    vec_init(&t->procs, sizeof(struct process));
    t->generation = 0;
    t->spawned = 0;
    t->exited = 0;
}

void process_table_free(struct process_table *t) {
    vec_free(&t->procs);
}

static struct process *at(const struct process_table *t, usize i) {
    return vec_at(&t->procs, i);
}

// First index whose pid is >= `pid`.
static usize lower_bound(const struct process_table *t, u32 pid) {
    usize lo = 0, hi = t->procs.len;
    while (lo < hi) {
        usize mid = lo + (hi - lo) / 2;
        if (at(t, mid)->pid < pid) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

struct process *process_table_find(const struct process_table *t, u32 pid) {
    struct process *p = at(t, lower_bound(t, pid));
    return (p && p->pid == pid) ? p : NULL;
}

void process_table_begin(struct process_table *t) {
    t->generation++;
    t->spawned = 0;
    t->exited = 0;
}

struct process *process_table_observe(struct process_table *t,
                                      const struct procfs_process *s,
                                      u64 elapsed_ticks, u64 clk_tck, u64 page_kb,
                                      bool *is_new) {
    usize i = lower_bound(t, s->pid);
    struct process *p = at(t, i);
    bool fresh = false;

    if (!p || p->pid != s->pid) {
        p = vec_insert(&t->procs, i);
        if (!p) return NULL;
        fresh = true;
    } else if (p->start_ticks != s->start_time) {
        // Same pid, different start time: the old one died and the pid
        // was handed out again between two updates.
        mem_set(p, 0, sizeof(*p));
        t->exited++;
        fresh = true;
    }

    // Everything before the first update counts as "already running".
    bool initial_scan = t->generation <= 1;
    if (fresh && !initial_scan) t->spawned++;

    u64 ticks = s->utime + s->stime;
    u64 delta = 0;
    if (!fresh) {
        delta = ticks >= p->cpu_ticks ? ticks - p->cpu_ticks : 0;
    } else if (!initial_scan) {
        // Started during the interval, so all of its time is ours.
        delta = ticks;
    }
    p->cpu_permille = elapsed_ticks ? (u32)MIN(delta * 1000 / elapsed_ticks, (u64)U32_MAX) : 0;

    p->pid = s->pid;
    p->ppid = s->ppid;
    p->state = s->state;
    p->nice = s->nice;
    p->threads = s->threads;
    p->cpu_ticks = ticks;
    p->cpu_time_ms = clk_tck ? ticks * 1000 / clk_tck : 0;
    p->rss_kb = s->rss_pages * page_kb;
    p->vsize_kb = s->vsize / 1024;
    p->start_ticks = s->start_time;
    p->generation = t->generation;
    str_copy(p->comm, sizeof(p->comm), s->comm);

    *is_new = fresh;
    return p;
}

void process_table_sweep(struct process_table *t) {
    usize w = 0;
    for (usize r = 0; r < t->procs.len; r++) {
        struct process *p = at(t, r);
        if (p->generation != t->generation) {
            t->exited++;
            continue;
        }
        if (w != r) mem_copy(at(t, w), p, sizeof(*p));
        w++;
    }
    vec_truncate(&t->procs, w);
}

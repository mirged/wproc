#include "platform/procfs.h"
#include "platform/file.h"
#include "util/parse.h"
#include "util/string.h"

static bool is_cpu_n(const char *word, usize len) {
    if (len < 4 || !str_starts_with(word, "cpu")) return false;
    for (usize i = 3; i < len; i++) {
        if (word[i] < '0' || word[i] > '9') return false;
    }
    return true;
}

static void read_times(struct cursor *c, struct procfs_cpu_times *t) {
    // Older kernels have fewer columns; missing ones stay zero.
    u64 *fields[] = {
        &t->user, &t->nice, &t->system, &t->idle,
        &t->iowait, &t->irq, &t->softirq, &t->steal,
    };
    for (usize i = 0; i < ARRAY_LEN(fields); i++) {
        if (!cur_u64(c, fields[i])) break;
    }
}

bool procfs_read_cpu(struct procfs_cpu *out, struct buffer *scratch) {
    if (!file_read_all("/proc/stat", scratch)) return false;
    mem_set(out, 0, sizeof(*out));

    struct cursor c;
    cur_init(&c, (const char *)scratch->data, scratch->len);
    bool have_total = false;

    while (!cur_eof(&c)) {
        const char *word;
        usize len;
        if (cur_word(&c, &word, &len)) {
            if (str_eq_n(word, len, "cpu")) {
                read_times(&c, &out->total);
                have_total = true;
            } else if (is_cpu_n(word, len)) {
                out->ncpu++;
            } else if (str_eq_n(word, len, "procs_running")) {
                cur_u64(&c, &out->procs_running);
            } else if (str_eq_n(word, len, "procs_blocked")) {
                cur_u64(&c, &out->procs_blocked);
            }
        }
        cur_skip_line(&c);
    }

    if (out->ncpu == 0) out->ncpu = 1;
    return have_total;
}

// "0.52 0.48 0.40 2/312 12345"
bool procfs_read_loadavg(struct procfs_loadavg *out, struct buffer *scratch) {
    if (!file_read_all("/proc/loadavg", scratch)) return false;
    mem_set(out, 0, sizeof(*out));

    struct cursor c;
    cur_init(&c, (const char *)scratch->data, scratch->len);
    for (usize i = 0; i < 3; i++) {
        if (!cur_fixed(&c, 2, &out->load_x100[i])) return false;
    }
    if (!cur_u64(&c, &out->running)) return false;
    if (!cur_expect(&c, '/')) return false;
    return cur_u64(&c, &out->total);
}

// "12345.67 98765.43"
bool procfs_read_uptime(u64 *seconds, struct buffer *scratch) {
    if (!file_read_all("/proc/uptime", scratch)) return false;
    struct cursor c;
    cur_init(&c, (const char *)scratch->data, scratch->len);
    return cur_u64(&c, seconds);
}

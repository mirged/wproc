#include "platform/procfs.h"
#include "platform/file.h"
#include "util/parse.h"
#include "util/string.h"

// /proc/<pid>/statm: "size resident shared text lib data dt", in pages.
static bool read_statm_rss(u32 pid, struct buffer *scratch, u64 *out) {
    char path[48];
    if (!procfs_pid_path(path, sizeof(path), pid, "statm")) return false;
    if (!file_read_all(path, scratch)) return false;

    struct cursor c;
    cur_init(&c, (const char *)scratch->data, scratch->len);
    return cur_skip_field(&c) && cur_u64(&c, out);
}

// /proc/<pid>/stat looks like:
//
//   1234 (some name) S 1 1234 1234 0 -1 4194560 ...
//
// The name may contain spaces and parentheses, so the only reliable
// delimiter is the LAST ')' in the line. Fields after it are numbered
// from 3 (state) as in proc(5).
bool procfs_read_process(u32 pid, struct procfs_process *out, struct buffer *scratch) {
    char path[48];
    if (!procfs_pid_path(path, sizeof(path), pid, "stat")) return false;
    if (!file_read_all(path, scratch)) return false;

    const char *data = (const char *)scratch->data;
    usize len = scratch->len;

    const char *open = NULL, *close = NULL;
    for (usize i = 0; i < len; i++) {
        if (data[i] == '(') {
            open = data + i;
            break;
        }
    }
    for (usize i = len; i-- > 0;) {
        if (data[i] == ')') {
            close = data + i;
            break;
        }
    }
    if (!open || !close || close <= open) return false;

    mem_set(out, 0, sizeof(*out));
    out->pid = pid;
    str_copy_n(out->comm, sizeof(out->comm), open + 1, (usize)(close - open - 1));

    struct cursor c;
    cur_init(&c, close + 1, (usize)(data + len - (close + 1)));

    const char *word;
    usize wlen;
    u64 ppid, threads, rss_raw;
    i64 nice, rss;

    if (!cur_word(&c, &word, &wlen)) return false;            // 3  state
    out->state = word[0];
    if (!cur_u64(&c, &ppid)) return false;                    // 4  ppid
    if (!cur_skip_fields(&c, 9)) return false;                // 5..13
    if (!cur_u64(&c, &out->utime)) return false;              // 14 utime
    if (!cur_u64(&c, &out->stime)) return false;              // 15 stime
    if (!cur_skip_fields(&c, 3)) return false;                // 16..18
    if (!cur_i64(&c, &nice)) return false;                    // 19 nice
    if (!cur_u64(&c, &threads)) return false;                 // 20 num_threads
    if (!cur_skip_field(&c)) return false;                    // 21 itrealvalue
    if (!cur_u64(&c, &out->start_time)) return false;         // 22 starttime
    if (!cur_u64(&c, &out->vsize)) return false;              // 23 vsize
    if (!cur_i64(&c, &rss)) return false;                     // 24 rss

    rss_raw = rss < 0 ? 0 : (u64)rss;
    out->ppid = (u32)ppid;
    out->nice = (i32)nice;
    out->threads = (u32)threads;

    // stat's rss comes from the kernel's approximate per-cpu counters and
    // can be off by megabytes (or read 0 for small processes). statm sums
    // them properly. Keep the stat value only as a fallback.
    u64 statm_rss;
    out->rss_pages = read_statm_rss(pid, scratch, &statm_rss) ? statm_rss : rss_raw;
    return true;
}

bool procfs_read_cmdline(u32 pid, char *out, usize cap, struct buffer *scratch) {
    if (cap == 0) return false;
    out[0] = '\0';

    char path[48];
    if (!procfs_pid_path(path, sizeof(path), pid, "cmdline")) return false;
    if (!file_read_all(path, scratch)) return false;

    usize n = MIN(scratch->len, cap - 1);
    for (usize i = 0; i < n; i++) {
        char ch = (char)scratch->data[i];
        out[i] = ch == '\0' ? ' ' : ch;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    return true;
}

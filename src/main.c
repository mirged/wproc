#include "wproc.h"
#include "platform/file.h"
#include "core/monitor.h"
#include "ui/render.h"
#include "util/buffer.h"
#include "util/parse.h"
#include "util/string.h"
#include "util/time.h"

#define STDOUT 1
#define STDERR 2

#define DEFAULT_INTERVAL_MS 1000
#define MIN_INTERVAL_MS     100
#define PRIME_DELAY_MS      250

struct options {
    u64   interval_ms;
    u64   count;        // 0 = forever
    u64   rows;         // batch mode row limit, 0 = all
    bool  batch;
    enum sort_key sort;
};

static const char usage[] =
    "usage: wproc [options]\n"
    "\n"
    "  -b          batch mode: print plain snapshots to stdout\n"
    "  -d <ms>     refresh interval in milliseconds (default 1000)\n"
    "  -n <count>  exit after this many updates\n"
    "  -l <rows>   batch mode: limit processes per snapshot\n"
    "  -s <key>    sort by cpu, mem, pid, time or name\n"
    "  -h          show this help\n"
    "  -v          show version\n";

static void die_usage(const char *msg, const char *arg) {
    file_write_str(STDERR, "wproc: ");
    file_write_str(STDERR, msg);
    if (arg) {
        file_write_str(STDERR, ": ");
        file_write_str(STDERR, arg);
    }
    file_write_str(STDERR, "\n\n");
    file_write_str(STDERR, usage);
}

static bool parse_sort(const char *s, enum sort_key *out) {
    static const enum sort_key keys[] = { SORT_CPU, SORT_MEM, SORT_PID, SORT_TIME, SORT_NAME };
    for (usize i = 0; i < ARRAY_LEN(keys); i++) {
        if (str_eq(s, sort_key_name(keys[i]))) {
            *out = keys[i];
            return true;
        }
    }
    return false;
}

// Returns 0 to continue, 1 to exit successfully, -1 on error.
static i32 parse_args(i32 argc, char **argv, struct options *o) {
    o->interval_ms = DEFAULT_INTERVAL_MS;
    o->count = 0;
    o->rows = 0;
    o->batch = false;
    o->sort = SORT_CPU;

    for (i32 i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;

        if (str_eq(a, "-h") || str_eq(a, "--help")) {
            file_write_str(STDOUT, usage);
            return 1;
        } else if (str_eq(a, "-v") || str_eq(a, "--version")) {
            file_write_str(STDOUT, "wproc " WPROC_VERSION "\n");
            return 1;
        } else if (str_eq(a, "-b")) {
            o->batch = true;
        } else if (str_eq(a, "-d") || str_eq(a, "-n") || str_eq(a, "-l")) {
            u64 v;
            if (!val || !parse_u64(val, &v)) {
                die_usage("expected a number after", a);
                return -1;
            }
            if (a[1] == 'd') o->interval_ms = MAX(v, (u64)MIN_INTERVAL_MS);
            if (a[1] == 'n') o->count = v;
            if (a[1] == 'l') o->rows = v;
            i++;
        } else if (str_eq(a, "-s")) {
            if (!val || !parse_sort(val, &o->sort)) {
                die_usage("unknown sort key", val ? val : "(none)");
                return -1;
            }
            i++;
        } else {
            die_usage("unknown option", a);
            return -1;
        }
    }
    return 0;
}

static bool update_or_complain(struct monitor *m) {
    if (monitor_update(m)) return true;
    file_write_str(STDERR, "wproc: failed to read /proc\n");
    return false;
}

static i32 run_batch(struct monitor *m, const struct options *o) {
    struct view_state vs;
    view_state_init(&vs);
    vs.sort = o->sort;
    vs.mode = "batch";

    struct buffer out;
    struct vector order;
    buf_init(&out);
    vec_init(&order, sizeof(const struct process *));

    i32 rc = 0;
    u64 count = o->count ? o->count : 1;
    for (u64 i = 0; i < count; i++) {
        if (i > 0) {
            time_sleep_ms(o->interval_ms);
            if (!update_or_complain(m)) {
                rc = 1;
                break;
            }
        }
        const struct snapshot *snap = monitor_snapshot(m);
        buf_clear(&out);
        if (!render_order(&order, snap, vs.sort, vs.reverse)) {
            rc = 1;
            break;
        }
        render_plain(&out, snap, &vs, &order, o->rows);
        if (out.failed || !file_write_all(STDOUT, out.data, out.len)) {
            rc = 1;
            break;
        }
    }

    vec_free(&order);
    buf_free(&out);
    return rc;
}

i32 wproc_main(const struct wproc_startup *s) {
    struct options o;
    i32 parsed = parse_args(s->argc, s->argv, &o);
    if (parsed != 0) return parsed > 0 ? 0 : 2;

    struct monitor m;
    monitor_init(&m, s->clk_tck, s->page_size);

    // Two samples so the first frame already has CPU deltas.
    i32 rc = 1;
    if (update_or_complain(&m)) {
        time_sleep_ms(PRIME_DELAY_MS);
        if (update_or_complain(&m)) rc = run_batch(&m, &o);
    }

    monitor_free(&m);
    return rc;
}

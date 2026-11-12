#include "wproc.h"
#include "platform/file.h"
#include "core/monitor.h"
#include "ui/input.h"
#include "ui/render.h"
#include "ui/terminal.h"
#include "util/buffer.h"
#include "util/parse.h"
#include "util/string.h"
#include "util/time.h"

#define STDOUT 1
#define STDERR 2

#define DEFAULT_INTERVAL_MS 1000
#define MIN_INTERVAL_MS     100
#define PRIME_DELAY_MS      250
#define MAX_INTERVAL_MS     60000
#define POLL_SLICE_MS       200   // how often we notice terminal resizes

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
    "              (implied when stdout is not a terminal)\n"
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

// ---- interactive ---------------------------------------------------------

#define KEYS_HINT " q quit  P/M/N/T/A sort  r rev  c cmd  space pause  +/- speed"

struct app {
    struct terminal   term;
    struct view_state vs;
    struct buffer     out;
    struct vector     order;
    char              mode[64];
    bool              paused;
    bool              quit;
    bool              resort;
    u64               interval_ms;
};

static void app_init(struct app *a, const struct options *o) {
    view_state_init(&a->vs);
    a->vs.sort = o->sort;
    a->vs.mode = a->mode;
    a->vs.keys_hint = KEYS_HINT;
    buf_init(&a->out);
    vec_init(&a->order, sizeof(const struct process *));
    a->mode[0] = '\0';
    a->paused = false;
    a->quit = false;
    a->resort = true;
    a->interval_ms = o->interval_ms;
}

static void app_free(struct app *a) {
    buf_free(&a->out);
    vec_free(&a->order);
}

static void set_mode(struct app *a, const char *label) {
    struct buffer b;
    buf_init(&b);
    buf_append_str(&b, label);
    buf_append_str(&b, a->paused ? " paused" : "");
    buf_append_str(&b, " ");
    buf_append_u64(&b, a->interval_ms);
    buf_append_str(&b, "ms");
    str_copy(a->mode, sizeof(a->mode), buf_cstr(&b));
    buf_free(&b);
}

static bool draw(struct app *a, const struct snapshot *snap) {
    if (a->resort) {
        if (!render_order(&a->order, snap, a->vs.sort, a->vs.reverse)) return false;
        a->resort = false;
    }
    render_screen(&a->out, snap, &a->vs, &a->order, a->term.rows, a->term.cols);
    return !a->out.failed && term_write(&a->term, a->out.data, a->out.len);
}

static void set_sort(struct app *a, enum sort_key key) {
    if (a->vs.sort == key) {
        a->vs.reverse = !a->vs.reverse;
    } else {
        a->vs.sort = key;
        a->vs.reverse = false;
    }
    a->vs.scroll = 0;
    a->resort = true;
}

// Keys shared by every interactive mode. Returns false if not handled.
static bool handle_key(struct app *a, const struct key_event *ev) {
    usize page = render_visible_rows(a->term.rows);
    if (page == 0) page = 1;
    usize *scroll = &a->vs.scroll;

    switch (ev->code) {
    case KEY_UP:        if (*scroll) (*scroll)--; return true;
    case KEY_DOWN:      (*scroll)++; return true;  // clamped by the renderer
    case KEY_PAGE_UP:   *scroll = *scroll > page ? *scroll - page : 0; return true;
    case KEY_PAGE_DOWN: *scroll += page; return true;
    case KEY_HOME:      *scroll = 0; return true;
    case KEY_END:       *scroll = (usize)-1 / 2; return true;
    case KEY_CHAR:      break;
    default:            return false;
    }

    switch (ev->ch) {
    case 'q': case 'Q': case KEY_CTRL('c'):
        a->quit = true;
        return true;
    case 'P': case 'p': set_sort(a, SORT_CPU);  return true;
    case 'M': case 'm': set_sort(a, SORT_MEM);  return true;
    case 'N': case 'n': set_sort(a, SORT_PID);  return true;
    case 'T': case 't': set_sort(a, SORT_TIME); return true;
    case 'A': case 'a': set_sort(a, SORT_NAME); return true;
    case 'r':
        a->vs.reverse = !a->vs.reverse;
        a->resort = true;
        return true;
    case 'c':
        a->vs.show_cmdline = !a->vs.show_cmdline;
        return true;
    case 'k': if (*scroll) (*scroll)--; return true;
    case 'j': (*scroll)++; return true;
    case 'g': *scroll = 0; return true;
    case 'G': *scroll = (usize)-1 / 2; return true;
    case ' ':
        a->paused = !a->paused;
        a->vs.message = a->paused ? "paused. the processes have not noticed." : NULL;
        return true;
    case '+': case '=':
        a->interval_ms = MIN(a->interval_ms * 2, (u64)MAX_INTERVAL_MS);
        return true;
    case '-': case '_':
        a->interval_ms = MAX(a->interval_ms / 2, (u64)MIN_INTERVAL_MS);
        return true;
    }
    return false;
}

static i32 run_interactive(struct app *a, struct monitor *m, const struct options *o) {
    u64 updates = 0;
    u64 next = time_now_ms() + a->interval_ms;
    bool dirty = true;

    while (!a->quit) {
        if (dirty) {
            set_mode(a, "live");
            if (!draw(a, monitor_snapshot(m))) return 1;
            dirty = false;
        }

        u64 now = time_now_ms();
        if (now >= next) {
            next = now + a->interval_ms;
            if (a->paused) continue;
            if (!monitor_update(m)) {
                a->vs.message = "failed to read /proc. retrying.";
            } else {
                a->vs.message = NULL;
                if (o->count && ++updates >= o->count) break;
            }
            a->resort = true;
            dirty = true;
            continue;
        }

        struct key_event evs[16];
        i32 wait = (i32)MIN(next - now, (u64)POLL_SLICE_MS);
        usize n = input_read(a->term.in_fd, wait, evs, ARRAY_LEN(evs));
        for (usize i = 0; i < n; i++) {
            if (handle_key(a, &evs[i])) dirty = true;
        }
        if (term_update_size(&a->term)) dirty = true;
    }
    return 0;
}

// ---- entry ---------------------------------------------------------------

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
        if (update_or_complain(&m)) {
            struct app a;
            app_init(&a, &o);
            if (!o.batch && term_init(&a.term, 0, STDOUT)) {
                rc = run_interactive(&a, &m, &o);
                term_restore(&a.term);
            } else {
                rc = run_batch(&m, &o);
            }
            app_free(&a);
        }
    }

    monitor_free(&m);
    return rc;
}

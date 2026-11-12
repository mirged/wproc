#include "wproc.h"
#include "platform/file.h"
#include "core/monitor.h"
#include "ui/input.h"
#include "ui/render.h"
#include "ui/terminal.h"
#include "storage/recording.h"
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
    const char *record_path;
    const char *play_path;
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
    "  -r <file>   record the session to a file\n"
    "  -p <file>   play back a recording\n"
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
    o->record_path = NULL;
    o->play_path = NULL;

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
        } else if (str_eq(a, "-r") || str_eq(a, "-p")) {
            if (!val) {
                die_usage("expected a file name after", a);
                return -1;
            }
            if (a[1] == 'r') o->record_path = val;
            else o->play_path = val;
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
    if (o->record_path && o->play_path) {
        die_usage("cannot record and play back at the same time", NULL);
        return -1;
    }
    return 0;
}

// ---- frame sources -------------------------------------------------------
//
// The UI loops do not care where snapshots come from: the live monitor
// (optionally teeing into a recorder) or a recording being played back.

struct source {
    struct monitor  *monitor;
    struct recorder *recorder;
    struct player   *player;
    struct snapshot  play_snap;
    usize            frame;
    bool             at_end;
};

static const struct snapshot *source_snapshot(const struct source *src) {
    return src->player ? &src->play_snap : monitor_snapshot(src->monitor);
}

static bool source_seek(struct source *src, usize frame) {
    usize count = player_frame_count(src->player);
    if (frame >= count) frame = count - 1;
    if (!player_load(src->player, frame, &src->play_snap)) return false;
    src->frame = frame;
    src->at_end = frame + 1 >= count;
    return true;
}

static bool source_record(struct source *src) {
    if (!src->recorder) return true;
    if (recorder_write(src->recorder, monitor_snapshot(src->monitor))) return true;
    file_write_str(STDERR, "wproc: recording failed, disk full or file gone\n");
    return false;
}

// Move to the next snapshot. False on a hard error.
static bool source_advance(struct source *src) {
    if (src->player) return src->at_end || source_seek(src, src->frame + 1);
    return monitor_update(src->monitor) && source_record(src);
}

static bool update_or_complain(struct monitor *m) {
    if (monitor_update(m)) return true;
    file_write_str(STDERR, "wproc: failed to read /proc\n");
    return false;
}

static i32 run_batch(struct source *src, const struct options *o) {
    struct view_state vs;
    view_state_init(&vs);
    vs.sort = o->sort;
    vs.mode = src->player ? "playback" : (src->recorder ? "batch rec" : "batch");

    struct buffer out;
    struct vector order;
    buf_init(&out);
    vec_init(&order, sizeof(const struct process *));

    i32 rc = 0;
    u64 count = o->count ? o->count : 1;
    if (src->player) {
        usize frames = player_frame_count(src->player);
        count = o->count ? MIN(o->count, (u64)frames) : frames;
    }

    for (u64 i = 0; i < count; i++) {
        if (i > 0) {
            if (!src->player) time_sleep_ms(o->interval_ms);
            if (!source_advance(src)) {
                file_write_str(STDERR, "wproc: failed to read the next snapshot\n");
                rc = 1;
                break;
            }
        }
        const struct snapshot *snap = source_snapshot(src);
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

#define KEYS_HINT      " q quit  P/M/N/T/A sort  r rev  c cmd  space pause  +/- speed"
#define KEYS_HINT_PLAY " q quit  P/M/N/T/A sort  r rev  c cmd  space pause  <-/-> step  [/] skip 10"

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

static void set_mode(struct app *a, const struct source *src) {
    struct buffer b;
    buf_init(&b);
    if (src->player) {
        char t[32];
        u64 elapsed = src->play_snap.time_ms - src->player->first_time_ms;
        buf_append_str(&b, "play ");
        buf_append_u64(&b, src->frame + 1);
        buf_append_char(&b, '/');
        buf_append_u64(&b, player_frame_count(src->player));
        buf_append_str(&b, " +");
        buf_append(&b, t, time_format_uptime(t, elapsed / 1000));
    } else {
        buf_append_str(&b, src->recorder ? "rec" : "live");
    }
    buf_append_str(&b, a->paused ? " paused " : " ");
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

// Playback-only keys: stepping through frames.
static bool handle_play_key(struct app *a, struct source *src, const struct key_event *ev) {
    if (!src->player) return false;

    isize step = 0;
    if (ev->code == KEY_LEFT) step = -1;
    else if (ev->code == KEY_RIGHT) step = 1;
    else if (ev->code == KEY_CHAR && ev->ch == '[') step = -10;
    else if (ev->code == KEY_CHAR && ev->ch == ']') step = 10;
    else return false;

    usize target = step < 0
        ? (src->frame > (usize)-step ? src->frame - (usize)-step : 0)
        : src->frame + (usize)step;
    if (source_seek(src, target)) {
        a->paused = true;
        a->vs.message = src->at_end ? "end of recording" : NULL;
        a->resort = true;
    }
    return true;
}

static i32 run_interactive(struct app *a, struct source *src, const struct options *o) {
    u64 updates = 0;
    u64 next = time_now_ms() + a->interval_ms;
    bool dirty = true;

    if (src->player) a->vs.keys_hint = KEYS_HINT_PLAY;

    while (!a->quit) {
        if (dirty) {
            set_mode(a, src);
            if (!draw(a, source_snapshot(src))) return 1;
            dirty = false;
        }

        u64 now = time_now_ms();
        if (now >= next) {
            next = now + a->interval_ms;
            if (a->paused) continue;
            if (src->player && src->at_end) {
                a->paused = true;
                a->vs.message = "end of recording. it was all downhill from here.";
            } else if (!source_advance(src)) {
                a->vs.message = src->player ? "corrupt frame. skipping is not an option."
                                            : "failed to read /proc. retrying.";
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
            if (handle_play_key(a, src, &evs[i]) || handle_key(a, &evs[i])) dirty = true;
        }
        if (term_update_size(&a->term)) dirty = true;
    }
    return 0;
}

static i32 run(struct source *src, const struct options *o) {
    struct app a;
    app_init(&a, o);
    i32 rc;
    if (!o->batch && term_init(&a.term, 0, STDOUT)) {
        rc = run_interactive(&a, src, o);
        term_restore(&a.term);
    } else {
        rc = run_batch(src, o);
    }
    app_free(&a);
    return rc;
}

static i32 run_playback(const struct options *o) {
    struct player player;
    const char *error = NULL;
    if (!player_open(&player, o->play_path, &error)) {
        file_write_str(STDERR, "wproc: ");
        file_write_str(STDERR, o->play_path);
        file_write_str(STDERR, ": ");
        file_write_str(STDERR, error ? error : "unknown error");
        file_write_str(STDERR, "\n");
        player_close(&player);
        return 1;
    }
    if (player.truncated) {
        file_write_str(STDERR, "wproc: recording ends mid-frame, ignoring the tail\n");
    }

    struct options po = *o;
    if (player.header.interval_ms >= MIN_INTERVAL_MS) po.interval_ms = player.header.interval_ms;

    struct source src = { .player = &player };
    i32 rc = source_seek(&src, 0) ? run(&src, &po) : 1;
    player_close(&player);
    return rc;
}

static i32 run_live(const struct wproc_startup *s, const struct options *o) {
    struct monitor m;
    monitor_init(&m, s->clk_tck, s->page_size);

    struct recorder rec;
    struct source src = { .monitor = &m };
    i32 rc = 1;

    if (o->record_path) {
        if (!recorder_open(&rec, o->record_path, (u32)o->interval_ms, m.hostname)) {
            file_write_str(STDERR, "wproc: cannot create ");
            file_write_str(STDERR, o->record_path);
            file_write_str(STDERR, "\n");
            monitor_free(&m);
            return 1;
        }
        src.recorder = &rec;
    }

    // Two samples so the first frame already has CPU deltas.
    if (update_or_complain(&m)) {
        time_sleep_ms(PRIME_DELAY_MS);
        if (update_or_complain(&m) && source_record(&src)) rc = run(&src, o);
    }

    if (src.recorder) recorder_close(&rec);
    monitor_free(&m);
    return rc;
}

// ---- entry ---------------------------------------------------------------

i32 wproc_main(const struct wproc_startup *s) {
    struct options o;
    i32 parsed = parse_args(s->argc, s->argv, &o);
    if (parsed != 0) return parsed > 0 ? 0 : 2;
    return o.play_path ? run_playback(&o) : run_live(s, &o);
}

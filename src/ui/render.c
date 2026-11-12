#include "ui/render.h"
#include "util/string.h"
#include "util/time.h"

// Screen layout (1-based rows):
//
//   1        title strip: name, host, uptime, tasks ............ mode
//   2        -
//   3        cpu  sparkline of recent history, breakdown, load
//   4        mem  one segmented line: used / cache / free
//   5        status message
//   6        column labels
//   7..n-1   processes, busiest first, idle ones folded under a divider
//   n        "? keys" .......................... sort / position
#define CHROME_ROWS 7
#define MIN_ROWS    (CHROME_ROWS + 1)
#define MIN_COLS    48
#define PLAIN_WIDTH 4096

// ---- palette (xterm-256) -------------------------------------------------

#define RESET   "\x1b[0m"
#define BOLD    "\x1b[1m"
#define DIM     "\x1b[38;5;245m"
#define FAINT   "\x1b[38;5;240m"
#define ACCENT  "\x1b[38;5;214m"
#define MEM_FG  "\x1b[38;5;75m"
#define CACHE_FG "\x1b[38;5;24m"
#define FREE_FG "\x1b[38;5;237m"
#define ALERT   "\x1b[38;5;203m"
#define GAUGE_BG "\x1b[48;5;235m"

// Cool to hot. Index chosen by heat_sgr().
static const char *heat_sgr(u32 permille) {
    if (permille < 20)  return "\x1b[38;5;240m";
    if (permille < 100) return "\x1b[38;5;109m";
    if (permille < 300) return "\x1b[38;5;150m";
    if (permille < 600) return "\x1b[38;5;221m";
    if (permille < 900) return "\x1b[38;5;208m";
    return "\x1b[38;5;203m";
}

static const char *const spark_glyph[8] = {
    "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█",
};

// Left-aligned partial blocks, 1/8 .. 7/8 of a cell.
static const char *const eighth_glyph[8] = {
    " ", "▏", "▎", "▍", "▌", "▋", "▊", "▉",
};

#define FULL_BLOCK "█"
#define HEAVY_LINE "━"
#define LIGHT_LINE "─"
#define MID_DOT    "·"

// ---- view state & history ------------------------------------------------

void view_state_init(struct view_state *vs) {
    vs->sort = SORT_CPU;
    vs->reverse = false;
    vs->cmd = CMD_ARGS;
    vs->show_help = false;
    vs->playback = false;
    vs->scroll = 0;
    vs->mode = NULL;
    vs->message = NULL;
    vs->history = NULL;
}

void history_clear(struct history *h) {
    h->len = 0;
}

void history_push(struct history *h, const struct snapshot *snap) {
    if (h->len == HISTORY_MAX) {
        mem_move(h->cpu, h->cpu + 1, (HISTORY_MAX - 1) * sizeof(h->cpu[0]));
        h->len--;
    }
    h->cpu[h->len++] = (u16)MIN(snap->cpu.total_permille, 1000u);
}

const char *sort_key_name(enum sort_key key) {
    switch (key) {
    case SORT_CPU:  return "cpu";
    case SORT_MEM:  return "mem";
    case SORT_PID:  return "pid";
    case SORT_TIME: return "time";
    case SORT_NAME: return "name";
    }
    return "?";
}

static bool sort_descending(enum sort_key key, bool reverse) {
    bool natural = key == SORT_CPU || key == SORT_MEM || key == SORT_TIME;
    return natural != reverse;
}

usize render_visible_rows(u16 rows) {
    return rows > CHROME_ROWS ? (usize)rows - CHROME_ROWS : 0;
}

// ---- sorting -------------------------------------------------------------

struct sort_ctx {
    enum sort_key key;
    bool reverse;
};

static i32 cmp_u64(u64 a, u64 b) {
    return a < b ? -1 : (a > b ? 1 : 0);
}

static i32 cmp_proc(const void *pa, const void *pb, void *vctx) {
    const struct process *a = *(const struct process *const *)pa;
    const struct process *b = *(const struct process *const *)pb;
    const struct sort_ctx *ctx = vctx;

    i32 r = 0;
    switch (ctx->key) {
    case SORT_CPU:  r = cmp_u64(a->cpu_permille, b->cpu_permille); break;
    case SORT_MEM:  r = cmp_u64(a->rss_kb, b->rss_kb); break;
    case SORT_TIME: r = cmp_u64(a->cpu_time_ms, b->cpu_time_ms); break;
    case SORT_PID:  r = cmp_u64(a->pid, b->pid); break;
    case SORT_NAME: r = str_cmp_ascii_nocase(a->comm, b->comm); break;
    }
    if (sort_descending(ctx->key, ctx->reverse)) r = -r;
    return r ? r : cmp_u64(a->pid, b->pid);
}

bool render_order(struct vector *order, const struct snapshot *snap,
                  enum sort_key key, bool reverse) {
    vec_clear(order);
    if (!vec_reserve(order, snap->procs->len)) return false;
    for (usize i = 0; i < snap->procs->len; i++) {
        const struct process **slot = vec_push(order);
        *slot = vec_at(snap->procs, i);
    }
    struct sort_ctx ctx = { key, reverse };
    vec_sort(order, cmp_proc, &ctx);
    return true;
}

// ---- line builder --------------------------------------------------------
//
// Tracks visible columns so text can be clipped and right-aligned.
// Escape sequences and glyphs are appended verbatim; process-supplied text
// goes through ln_text, which turns anything but printable ASCII into '?'
// so one byte is always one column.

struct line {
    struct buffer *b;
    usize used;
    usize width;
    bool  plain;
};

static void ln_begin_at(struct line *l, struct buffer *b, usize row, usize col,
                        usize width, bool plain) {
    l->b = b;
    l->used = 0;
    l->width = width;
    l->plain = plain;
    if (!plain) {
        buf_append_str(b, "\x1b[");
        buf_append_u64(b, row);
        buf_append_char(b, ';');
        buf_append_u64(b, col);
        buf_append_char(b, 'H');
    }
}

static void ln_begin(struct line *l, struct buffer *b, usize row, usize width, bool plain) {
    ln_begin_at(l, b, row, 1, width, plain);
}

static usize ln_room(const struct line *l) {
    return l->width - l->used;
}

static void ln_text(struct line *l, const char *s, usize n) {
    for (usize i = 0; i < n && l->used < l->width; i++) {
        u8 c = (u8)s[i];
        if (c < 0x20 || c >= 0x7f) c = '?';
        buf_append_char(l->b, (char)c);
        l->used++;
    }
}

static void ln_str(struct line *l, const char *s) {
    ln_text(l, s, str_len(s));
}

// One column wide, whatever its encoding.
static void ln_glyph(struct line *l, const char *utf8) {
    if (l->used >= l->width) return;
    buf_append_str(l->b, utf8);
    l->used++;
}

static void ln_spaces(struct line *l, usize n) {
    n = MIN(n, ln_room(l));
    buf_append_repeat(l->b, ' ', n);
    l->used += n;
}

static void ln_pad_to(struct line *l, usize col) {
    if (col > l->used) ln_spaces(l, col - l->used);
}

// width 0 means "as wide as the text".
static void ln_field(struct line *l, const char *s, usize n, usize width, bool right) {
    if (width == 0) width = n;
    if (n > width) n = width;
    if (right) ln_spaces(l, width - n);
    ln_text(l, s, n);
    if (!right) ln_spaces(l, width - n);
}

static void ln_esc(struct line *l, const char *seq) {
    if (!l->plain) buf_append_str(l->b, seq);
}

static void ln_end(struct line *l) {
    if (l->plain) buf_append_char(l->b, '\n');
    else buf_append_str(l->b, RESET "\x1b[K");
}

// Thin separator between title strip items.
static void ln_sep(struct line *l) {
    ln_esc(l, FAINT);
    if (l->plain) ln_str(l, "  ");
    else {
        ln_str(l, "  ");
        ln_glyph(l, MID_DOT);
        ln_str(l, "  ");
    }
    ln_esc(l, RESET);
}

// ---- formatting ----------------------------------------------------------

static usize fmt_kb(char *out, u64 kb) {
    static const char units[] = "KMGTP";
    u64 x10 = kb * 10;
    usize u = 0;
    while (x10 >= 10240 && u < 4) {
        x10 /= 1024;
        u++;
    }
    usize n = (u > 0 && x10 < 100) ? str_from_fixed(out, x10, 1)
                                   : str_from_u64(out, x10 / 10);
    out[n++] = units[u];
    return n;
}

static usize fmt_percent(char *out, u32 permille) {
    usize n = str_from_fixed(out, permille, 1);
    out[n++] = '%';
    return n;
}

static void ln_kb(struct line *l, u64 kb, usize width) {
    char t[24];
    ln_field(l, t, fmt_kb(t, kb), width, true);
}

static void ln_percent(struct line *l, u32 permille, usize width) {
    char t[24];
    ln_field(l, t, fmt_percent(t, permille), width, true);
}

static void ln_u64(struct line *l, u64 v, usize width) {
    char t[20];
    ln_field(l, t, str_from_u64(t, v), width, true);
}

static void ln_interval(struct line *l, const char *label, u32 permille) {
    ln_esc(l, FAINT);
    ln_str(l, label);
    ln_esc(l, DIM);
    ln_str(l, " ");
    ln_percent(l, permille, 0);
    ln_esc(l, RESET);
}

// ---- summary -------------------------------------------------------------

// Returns false if the mode did not fit and still needs a home.
static bool render_title(struct buffer *b, const struct snapshot *snap,
                         const struct view_state *vs, usize row, usize width, bool plain) {
    struct line l;
    char t[32];

    ln_begin(&l, b, row, width, plain);
    ln_str(&l, " ");
    ln_esc(&l, ACCENT BOLD);
    ln_str(&l, "wproc");
    ln_esc(&l, RESET);
    if (snap->hostname[0]) {
        ln_str(&l, "  ");
        ln_esc(&l, BOLD);
        ln_str(&l, snap->hostname);
        ln_esc(&l, RESET);
    }
    ln_sep(&l);
    ln_esc(&l, FAINT);
    ln_str(&l, "up ");
    ln_esc(&l, RESET);
    ln_text(&l, t, time_format_uptime(t, snap->uptime_s));
    ln_sep(&l);
    ln_esc(&l, FAINT);
    ln_str(&l, "load ");
    ln_esc(&l, RESET);
    for (usize i = 0; i < 3; i++) {
        if (i) ln_str(&l, " ");
        ln_text(&l, t, str_from_fixed(t, snap->load_x100[i], 2));
    }
    ln_sep(&l);
    ln_u64(&l, snap->tasks_total, 0);
    ln_esc(&l, FAINT);
    ln_str(&l, " tasks, ");
    ln_esc(&l, RESET);
    ln_u64(&l, snap->tasks_running, 0);
    ln_esc(&l, FAINT);
    ln_str(&l, " running");
    ln_esc(&l, RESET);
    if (snap->spawned || snap->exited) {
        ln_sep(&l);
        ln_esc(&l, FAINT);
        ln_str(&l, "+");
        ln_u64(&l, snap->spawned, 0);
        ln_str(&l, " -");
        ln_u64(&l, snap->exited, 0);
        ln_esc(&l, RESET);
    }

    bool placed = true;
    if (vs->mode) {
        usize n = str_len(vs->mode);
        if (plain || l.used + n + 3 <= width) {
            if (plain) ln_str(&l, "  ");
            else ln_pad_to(&l, width - n - 1);
            ln_esc(&l, DIM);
            ln_str(&l, vs->mode);
        } else {
            placed = false;
        }
    }
    ln_end(&l);
    return placed;
}

// Whatever the text after the meters leaves over (about 64 columns).
static usize meter_width(usize cols) {
    return MAX(8u, MIN(cols >= 72 ? cols - 64 : 0, (usize)HISTORY_MAX));
}

static void render_cpu(struct buffer *b, const struct snapshot *snap,
                       const struct view_state *vs, usize row, usize width, bool plain) {
    const struct cpu_usage *c = &snap->cpu;
    struct line l;
    char t[24];

    ln_begin(&l, b, row, width, plain);
    ln_esc(&l, DIM);
    ln_str(&l, " cpu  ");
    ln_esc(&l, RESET);

    if (!plain) {
        // Sparkline, newest sample on the right.
        usize w = meter_width(width);
        usize have = vs->history ? MIN(vs->history->len, w) : 0;
        ln_spaces(&l, w - have);
        for (usize i = 0; i < have; i++) {
            u16 v = vs->history->cpu[vs->history->len - have + i];
            ln_esc(&l, heat_sgr(v));
            ln_glyph(&l, spark_glyph[MIN((usize)v * 8 / 1001, 7u)]);
        }
        ln_esc(&l, RESET);
        ln_str(&l, "  ");
    }

    ln_esc(&l, BOLD);
    ln_esc(&l, heat_sgr(c->total_permille));
    ln_field(&l, t, fmt_percent(t, c->total_permille), plain ? 0 : 6, true);
    ln_esc(&l, RESET);
    ln_str(&l, "   ");
    ln_interval(&l, "usr", c->user_permille);
    ln_str(&l, "  ");
    ln_interval(&l, "sys", c->system_permille);
    ln_str(&l, "  ");
    ln_interval(&l, "io", c->iowait_permille);
    if (c->steal_permille) {
        ln_str(&l, "  ");
        ln_interval(&l, "steal", c->steal_permille);
    }

    ln_str(&l, "   ");
    ln_esc(&l, FAINT);
    ln_u64(&l, c->ncpu, 0);
    ln_str(&l, c->ncpu == 1 ? " core" : " cores");
    ln_esc(&l, RESET);
    ln_end(&l);
}

static void render_mem(struct buffer *b, const struct snapshot *snap,
                       usize row, usize width, bool plain) {
    const struct mem_usage *m = &snap->mem;
    struct line l;

    u32 used = mem_permille(m->used_kb, m->total_kb);
    u32 reclaim = mem_permille(m->available_kb - m->free_kb, m->total_kb);

    ln_begin(&l, b, row, width, plain);
    ln_esc(&l, DIM);
    ln_str(&l, " mem  ");
    ln_esc(&l, RESET);

    if (!plain) {
        // One line, three weights: used, reclaimable, free.
        usize w = meter_width(width);
        usize a = (usize)used * w / 1000;
        usize c = MIN((usize)(used + reclaim) * w / 1000, w) - a;
        ln_esc(&l, MEM_FG);
        for (usize i = 0; i < a; i++) ln_glyph(&l, HEAVY_LINE);
        ln_esc(&l, CACHE_FG);
        for (usize i = 0; i < c; i++) ln_glyph(&l, HEAVY_LINE);
        ln_esc(&l, FREE_FG);
        for (usize i = a + c; i < w; i++) ln_glyph(&l, LIGHT_LINE);
        ln_esc(&l, RESET);
        ln_str(&l, "  ");
    }

    ln_esc(&l, BOLD MEM_FG);
    ln_percent(&l, used, plain ? 0 : 6);
    ln_esc(&l, RESET);
    ln_str(&l, "   ");
    ln_kb(&l, m->used_kb, 0);
    ln_esc(&l, FAINT);
    ln_str(&l, " of ");
    ln_esc(&l, RESET);
    ln_kb(&l, m->total_kb, 0);
    ln_str(&l, "  ");
    ln_esc(&l, FAINT);
    ln_str(&l, "cache ");
    ln_esc(&l, DIM);
    ln_kb(&l, m->cached_kb, 0);
    ln_esc(&l, FAINT);
    ln_str(&l, "   swap ");
    ln_esc(&l, m->swap_used_kb ? ALERT : DIM);
    ln_kb(&l, m->swap_used_kb, 0);
    ln_esc(&l, FAINT);
    ln_str(&l, " of ");
    ln_esc(&l, DIM);
    ln_kb(&l, m->swap_total_kb, 0);
    ln_esc(&l, RESET);
    ln_end(&l);
}

// ---- process list --------------------------------------------------------
//
//   ████▍  28.0%  ● steamwebhelper    33192    410M    4:29.41  --type=zygote ...
//   gauge   cpu   st name             pid      mem     time     command

#define GAUGE_W 5
#define CPU_W   6
#define NAME_W  16
#define PID_W   7
#define MEM_W   6
#define TIME_W  9

static void label(struct line *l, const struct view_state *vs, enum sort_key key,
                  const char *text, usize width, bool right) {
    bool active = vs->sort == key;
    usize n = str_len(text) + (active && !l->plain ? 1 : 0);
    if (width < n) width = n;
    if (right) ln_spaces(l, width - n);
    if (active && !l->plain) {
        ln_esc(l, ACCENT);
        ln_str(l, text);
        ln_glyph(l, sort_descending(key, vs->reverse) ? "↓" : "↑");
        ln_esc(l, FAINT);
    } else {
        ln_str(l, text);
    }
    if (!right) ln_spaces(l, width - n);
}

static void render_labels(struct buffer *b, const struct view_state *vs,
                          usize row, usize width, bool plain) {
    struct line l;
    ln_begin(&l, b, row, width, plain);
    ln_esc(&l, FAINT);
    ln_str(&l, " ");
    label(&l, vs, SORT_CPU, "cpu", plain ? CPU_W : GAUGE_W + 1 + CPU_W, true);
    ln_str(&l, "    ");
    label(&l, vs, SORT_NAME, "name", NAME_W, false);
    ln_str(&l, " ");
    label(&l, vs, SORT_PID, "pid", PID_W, true);
    ln_str(&l, "  ");
    label(&l, vs, SORT_MEM, "mem", MEM_W, true);
    ln_str(&l, "  ");
    label(&l, vs, SORT_TIME, "time", TIME_W, true);
    if (vs->cmd != CMD_HIDDEN) ln_str(&l, "  command");
    ln_end(&l);
}

static void gauge(struct line *l, u32 permille) {
    usize eighths = (usize)MIN(permille, 1000u) * (GAUGE_W * 8) / 1000;
    if (permille > 0 && eighths == 0) eighths = 1;  // show signs of life
    ln_esc(l, GAUGE_BG);
    ln_esc(l, heat_sgr(MAX(permille, 100u)));
    for (usize i = 0; i < GAUGE_W; i++) {
        usize fill = eighths > i * 8 ? MIN(eighths - i * 8, 8u) : 0;
        ln_glyph(l, fill == 8 ? FULL_BLOCK : eighth_glyph[fill]);
    }
    ln_esc(l, RESET);
}

static void state_mark(struct line *l, char state) {
    if (l->plain) {
        ln_text(l, &state, 1);
        return;
    }
    switch (state) {
    case 'R': ln_esc(l, ACCENT); ln_glyph(l, "●"); break;  // ●
    case 'D': ln_esc(l, ALERT);  ln_glyph(l, "◆"); break;  // ◆
    case 'Z': ln_esc(l, FAINT);  ln_glyph(l, "✕"); break;  // ✕
    case 'T': case 't':
              ln_esc(l, DIM);    ln_glyph(l, "‖"); break;  // ‖
    default:  ln_str(l, " ");
    }
    ln_esc(l, RESET);
}

// The name column already shows comm; skip argv[0] when it is just the
// path to the same program, and show only what makes this one different.
static const char *command_args(const struct process *p) {
    const char *s = p->cmdline;
    const char *end = s;
    while (*end && *end != ' ') end++;

    const char *base = s;
    for (const char *q = s; q < end; q++) {
        if (*q == '/') base = q + 1;
    }

    usize base_len = (usize)(end - base);
    usize comm_len = str_len(p->comm);
    usize n = MIN(base_len, comm_len);
    if (n == 0 || mem_cmp(base, p->comm, n) != 0) return s;

    while (*end == ' ') end++;
    return end;
}

static void render_process(struct buffer *b, const struct process *p, const struct snapshot *snap,
                           const struct view_state *vs, usize row, usize width, bool plain) {
    struct line l;
    char t[32];
    bool kthread = p->cmdline[0] == '\0';
    bool idle = p->cpu_permille == 0;

    ln_begin(&l, b, row, width, plain);
    ln_str(&l, " ");
    if (!plain) {
        gauge(&l, p->cpu_permille);
        ln_str(&l, " ");
    }

    ln_esc(&l, heat_sgr(p->cpu_permille));
    ln_percent(&l, p->cpu_permille, CPU_W);
    ln_esc(&l, RESET);
    ln_str(&l, "  ");
    state_mark(&l, p->state);
    ln_str(&l, " ");

    if (kthread) ln_esc(&l, FAINT);
    else if (p->state == 'R') ln_esc(&l, BOLD);
    else if (idle) ln_esc(&l, DIM);
    ln_field(&l, p->comm, str_len(p->comm), NAME_W, false);
    ln_esc(&l, RESET);
    ln_str(&l, " ");

    ln_esc(&l, FAINT);
    ln_u64(&l, p->pid, PID_W);
    ln_esc(&l, RESET);
    ln_str(&l, "  ");

    u32 mem = mem_permille(p->rss_kb, snap->mem.total_kb);
    ln_esc(&l, mem >= 100 ? MEM_FG BOLD : (mem >= 10 ? MEM_FG : DIM));
    ln_kb(&l, p->rss_kb, MEM_W);
    ln_esc(&l, RESET);
    ln_str(&l, "  ");

    ln_esc(&l, DIM);
    ln_field(&l, t, time_format_cpu(t, p->cpu_time_ms), TIME_W, true);
    ln_esc(&l, RESET);

    const char *cmd = vs->cmd == CMD_FULL ? p->cmdline : command_args(p);
    if (vs->cmd != CMD_HIDDEN && !kthread && cmd[0]) {
        ln_str(&l, "  ");
        ln_esc(&l, FAINT);
        ln_str(&l, cmd);
    }
    ln_end(&l);
}

// In cpu order, everything after the busy processes is folded under a
// single divider: nobody needs 300 rows of 0.0%.
static usize fold_index(const struct view_state *vs, const struct vector *order) {
    if (vs->sort != SORT_CPU || vs->reverse) return (usize)-1;
    for (usize i = 0; i < order->len; i++) {
        const struct process *const *pp = vec_at(order, i);
        if ((*pp)->cpu_permille == 0) return i;
    }
    return (usize)-1;
}

static void render_fold(struct buffer *b, usize idle, usize row, usize width) {
    struct line l;
    ln_begin(&l, b, row, width, false);
    ln_esc(&l, FAINT);
    ln_spaces(&l, GAUGE_W + 1 + CPU_W + 5);
    for (usize i = 0; i < 3; i++) {
        ln_glyph(&l, MID_DOT);
        ln_str(&l, " ");
    }
    ln_str(&l, " ");
    ln_u64(&l, idle, 0);
    ln_str(&l, " more, doing nothing in particular");
    ln_end(&l);
}

// ---- help ----------------------------------------------------------------

static const char *const help_lines[] = {
    "p m n t a   sort by cpu, mem, pid, time, name",
    "            (again to reverse)",
    "r           reverse",
    "c           command: args, full, hidden",
    "space       pause",
    "+ -         slower, faster",
    "arrows      scroll  (also j k, pgup pgdn, g G)",
    "?           close this",
    "q           quit",
};

static const char *const help_play_lines[] = {
    "left right  step one frame",
    "[ ]         skip ten frames",
};

static void help_row(struct buffer *b, usize row, usize col, usize inner, const char *text,
                     bool title) {
    struct line l;
    ln_begin_at(&l, b, row, col, inner + 4, false);
    ln_esc(&l, FAINT);
    ln_glyph(&l, title ? "╭" : (text ? "│" : "╰"));
    if (title || !text) {
        usize n = 0;
        if (title) {
            ln_glyph(&l, "─");
            ln_esc(&l, ACCENT);
            ln_str(&l, " keys ");
            ln_esc(&l, FAINT);
            n = 7;
        }
        while (n++ < inner + 2) ln_glyph(&l, "─");
        ln_glyph(&l, title ? "╮" : "╯");
    } else {
        ln_esc(&l, RESET);
        ln_str(&l, " ");
        ln_str(&l, text);
        ln_pad_to(&l, inner + 3);
        ln_esc(&l, FAINT);
        ln_glyph(&l, "│");
    }
    buf_append_str(b, RESET);
}

static void render_help(struct buffer *b, const struct view_state *vs, u16 rows, u16 cols) {
    usize inner = 0;
    for (usize i = 0; i < ARRAY_LEN(help_lines); i++) inner = MAX(inner, str_len(help_lines[i]));
    for (usize i = 0; i < ARRAY_LEN(help_play_lines); i++) inner = MAX(inner, str_len(help_play_lines[i]));

    usize count = ARRAY_LEN(help_lines) + (vs->playback ? ARRAY_LEN(help_play_lines) : 0);
    usize height = count + 2;
    if (inner + 4 > cols || height > rows) return;

    usize col = (cols - (inner + 4)) / 2 + 1;
    usize row = (rows - height) / 2 + 1;

    help_row(b, row++, col, inner, NULL, true);
    for (usize i = 0; i < ARRAY_LEN(help_lines); i++) help_row(b, row++, col, inner, help_lines[i], false);
    if (vs->playback) {
        for (usize i = 0; i < ARRAY_LEN(help_play_lines); i++) {
            help_row(b, row++, col, inner, help_play_lines[i], false);
        }
    }
    help_row(b, row, col, inner, NULL, false);
}

// ---- frames --------------------------------------------------------------

static void render_footer(struct buffer *b, const struct view_state *vs, usize first, usize shown,
                          usize total, usize row, usize width) {
    struct line l;
    ln_begin(&l, b, row, width, false);
    ln_esc(&l, FAINT);
    ln_str(&l, " ? ");
    ln_esc(&l, DIM);
    ln_str(&l, "keys");

    // Right side: where we are in the list.
    char t[64];
    usize n = 0;
    if (shown < total) {
        n += str_from_u64(t + n, first + 1);
        t[n++] = '-';
        n += str_from_u64(t + n, first + shown);
        mem_copy(t + n, " of ", 4);
        n += 4;
    }
    n += str_from_u64(t + n, total);
    if (l.used + n + 2 <= width) {
        ln_pad_to(&l, width - n - 1);
        ln_esc(&l, FAINT);
        ln_text(&l, t, n);
    }
    (void)vs;
    ln_end(&l);
}

void render_screen(struct buffer *out, const struct snapshot *snap,
                   struct view_state *vs, const struct vector *order,
                   u16 rows, u16 cols) {
    buf_clear(out);

    if (rows < MIN_ROWS || cols < MIN_COLS) {
        buf_append_str(out, "\x1b[H\x1b[2J" "this terminal is too small to hold all of this");
        return;
    }

    usize width = cols;
    bool mode_placed = render_title(out, snap, vs, 1, width, false);

    // Row 2 is breathing room, unless the mode needs somewhere to sit.
    struct line l;
    ln_begin(&l, out, 2, width, false);
    if (!mode_placed) {
        ln_pad_to(&l, width > str_len(vs->mode) + 1 ? width - str_len(vs->mode) - 1 : 0);
        ln_esc(&l, DIM);
        ln_str(&l, vs->mode);
    }
    ln_end(&l);

    render_cpu(out, snap, vs, 3, width, false);
    render_mem(out, snap, 4, width, false);

    ln_begin(&l, out, 5, width, false);
    if (vs->message) {
        ln_str(&l, " ");
        ln_esc(&l, ACCENT);
        ln_str(&l, vs->message);
    }
    ln_end(&l);

    render_labels(out, vs, 6, width, false);

    // The list is the processes plus, possibly, the fold divider.
    usize fold = fold_index(vs, order);
    bool has_fold = fold < order->len;
    usize total = order->len + (has_fold ? 1 : 0);

    usize visible = render_visible_rows(rows);
    usize max_scroll = total > visible ? total - visible : 0;
    if (vs->scroll > max_scroll) vs->scroll = max_scroll;

    usize row = CHROME_ROWS;
    for (usize i = 0; i < visible; i++, row++) {
        usize idx = vs->scroll + i;
        if (idx >= total) {
            ln_begin(&l, out, row, width, false);
            ln_end(&l);
            continue;
        }
        if (has_fold && idx == fold) {
            render_fold(out, order->len - fold, row, width);
            continue;
        }
        if (has_fold && idx > fold) idx--;
        const struct process *const *pp = vec_at(order, idx);
        render_process(out, *pp, snap, vs, row, width, false);
    }

    render_footer(out, vs, vs->scroll, MIN(visible, total), total, rows, width);
    if (vs->show_help) render_help(out, vs, rows, cols);
}

void render_plain(struct buffer *out, const struct snapshot *snap,
                  const struct view_state *vs, const struct vector *order,
                  usize max_rows) {
    render_title(out, snap, vs, 0, PLAIN_WIDTH, true);
    render_cpu(out, snap, vs, 0, PLAIN_WIDTH, true);
    render_mem(out, snap, 0, PLAIN_WIDTH, true);
    buf_append_char(out, '\n');
    render_labels(out, vs, 0, PLAIN_WIDTH, true);

    usize n = order->len;
    if (max_rows && max_rows < n) n = max_rows;
    for (usize i = 0; i < n; i++) {
        const struct process *const *pp = vec_at(order, i);
        render_process(out, *pp, snap, vs, 0, PLAIN_WIDTH, true);
    }
    buf_append_char(out, '\n');
}

#include "ui/render.h"
#include "util/string.h"
#include "util/time.h"

// Screen layout (1-based rows):
//   1   summary        4  swap bar
//   2   cpu bar        5  status message
//   3   memory bar     6  column header
//   7 .. rows-1        process list
//   rows               key hints
#define CHROME_ROWS 7
#define MIN_ROWS    (CHROME_ROWS + 1)
#define MIN_COLS    40
#define PLAIN_WIDTH 4096

#define SGR_RESET   "\x1b[0m"
#define SGR_INVERSE "\x1b[7m"
#define SGR_BOLD    "\x1b[1m"
#define SGR_GREEN   "\x1b[32m"
#define SGR_RED     "\x1b[31m"
#define SGR_BLUE    "\x1b[34m"
#define SGR_YELLOW  "\x1b[33m"

void view_state_init(struct view_state *vs) {
    vs->sort = SORT_CPU;
    vs->reverse = false;
    vs->show_cmdline = true;
    vs->scroll = 0;
    vs->mode = "live";
    vs->message = NULL;
    vs->keys_hint = NULL;
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
    bool descending = true;
    switch (ctx->key) {
    case SORT_CPU:  r = cmp_u64(a->cpu_permille, b->cpu_permille); break;
    case SORT_MEM:  r = cmp_u64(a->rss_kb, b->rss_kb); break;
    case SORT_TIME: r = cmp_u64(a->cpu_time_ms, b->cpu_time_ms); break;
    case SORT_PID:  r = cmp_u64(a->pid, b->pid); descending = false; break;
    case SORT_NAME: r = str_cmp_ascii_nocase(a->comm, b->comm); descending = false; break;
    }
    if (descending) r = -r;
    if (r == 0) r = cmp_u64(a->pid, b->pid);
    return ctx->reverse ? -r : r;
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
// Visible text is clipped to the line width; escape sequences are passed
// through without counting. Anything that is not printable ASCII becomes
// '?' so that one byte is always one column.

struct line {
    struct buffer *b;
    usize used;
    usize width;
    bool  plain;
};

static void ln_begin(struct line *l, struct buffer *b, usize row, usize width, bool plain) {
    l->b = b;
    l->used = 0;
    l->width = width;
    l->plain = plain;
    if (!plain) {
        buf_append_str(b, "\x1b[");
        buf_append_u64(b, row);
        buf_append_str(b, ";1H");
    }
}

static void ln_bytes(struct line *l, const char *s, usize n) {
    for (usize i = 0; i < n && l->used < l->width; i++) {
        u8 c = (u8)s[i];
        if (c < 0x20 || c >= 0x7f) c = '?';
        buf_append_char(l->b, (char)c);
        l->used++;
    }
}

static void ln_str(struct line *l, const char *s) {
    ln_bytes(l, s, str_len(s));
}

static void ln_repeat(struct line *l, char c, usize n) {
    usize room = l->width - l->used;
    if (n > room) n = room;
    buf_append_repeat(l->b, c, n);
    l->used += n;
}

// width 0 means "as wide as the text".
static void ln_field(struct line *l, const char *s, usize n, usize width, bool right) {
    if (width == 0) width = n;
    if (n > width) n = width;
    if (right) ln_repeat(l, ' ', width - n);
    ln_bytes(l, s, n);
    if (!right) ln_repeat(l, ' ', width - n);
}

static void ln_esc(struct line *l, const char *seq) {
    if (!l->plain) buf_append_str(l->b, seq);
}

static void ln_end(struct line *l, bool fill) {
    if (fill) ln_repeat(l, ' ', l->width - l->used);
    if (l->plain) {
        buf_append_char(l->b, '\n');
    } else {
        buf_append_str(l->b, SGR_RESET "\x1b[K");
    }
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

static void ln_kb(struct line *l, u64 kb) {
    char t[24];
    ln_bytes(l, t, fmt_kb(t, kb));
}

static void ln_percent(struct line *l, u32 permille, usize width) {
    char t[24];
    ln_field(l, t, fmt_percent(t, permille), width, true);
}

static void ln_u64(struct line *l, u64 v, usize width) {
    char t[20];
    ln_field(l, t, str_from_u64(t, v), width, true);
}

static void ln_i64(struct line *l, i64 v, usize width) {
    char t[21];
    ln_field(l, t, str_from_i64(t, v), width, true);
}

static void ln_load(struct line *l, u32 x100) {
    char t[24];
    ln_bytes(l, t, str_from_fixed(t, x100, 2));
}

// Two-segment meter: [|||||||||||||        ]
static void ln_bar(struct line *l, const char *label, usize bar_w,
                   u32 first, const char *first_sgr,
                   u32 second, const char *second_sgr) {
    usize a = (usize)MIN(first, 1000u) * bar_w / 1000;
    usize ab = (usize)MIN(first + second, 1000u) * bar_w / 1000;
    usize b = ab > a ? ab - a : 0;

    ln_esc(l, SGR_BOLD);
    ln_str(l, label);
    ln_esc(l, SGR_RESET);
    ln_str(l, " [");
    ln_esc(l, first_sgr);
    ln_repeat(l, '|', a);
    ln_esc(l, second_sgr);
    ln_repeat(l, '|', b);
    ln_esc(l, SGR_RESET);
    ln_repeat(l, ' ', bar_w - a - b);
    ln_str(l, "] ");
}

// ---- sections ------------------------------------------------------------

static void render_summary(struct buffer *b, const struct snapshot *snap, const char *mode,
                           usize width, usize bar_w, bool plain, usize row) {
    struct line l;
    char t[32];

    ln_begin(&l, b, row++, width, plain);
    ln_esc(&l, SGR_BOLD);
    ln_str(&l, "wproc");
    ln_esc(&l, SGR_RESET);
    ln_str(&l, "  up ");
    ln_bytes(&l, t, time_format_uptime(t, snap->uptime_s));
    ln_str(&l, "  load ");
    for (usize i = 0; i < 3; i++) {
        if (i) ln_str(&l, " ");
        ln_load(&l, snap->load_x100[i]);
    }
    ln_str(&l, "  tasks ");
    ln_u64(&l, snap->tasks_total, 0);
    ln_str(&l, ", ");
    ln_u64(&l, snap->tasks_running, 0);
    ln_str(&l, " running  +");
    ln_u64(&l, snap->spawned, 0);
    ln_str(&l, "/-");
    ln_u64(&l, snap->exited, 0);
    if (mode) {
        ln_str(&l, "  [");
        ln_esc(&l, SGR_YELLOW);
        ln_str(&l, mode);
        ln_esc(&l, SGR_RESET);
        ln_str(&l, "]");
    }
    ln_end(&l, false);

    const struct cpu_usage *c = &snap->cpu;
    ln_begin(&l, b, row++, width, plain);
    ln_bar(&l, "cpu", bar_w, c->user_permille, SGR_GREEN,
           c->system_permille + c->steal_permille, SGR_RED);
    ln_percent(&l, c->total_permille, 6);
    ln_str(&l, "  usr ");
    ln_percent(&l, c->user_permille, 0);
    ln_str(&l, "  sys ");
    ln_percent(&l, c->system_permille, 0);
    ln_str(&l, "  io ");
    ln_percent(&l, c->iowait_permille, 0);
    if (c->steal_permille) {
        ln_str(&l, "  steal ");
        ln_percent(&l, c->steal_permille, 0);
    }
    ln_str(&l, "  ");
    ln_u64(&l, c->ncpu, 0);
    ln_str(&l, c->ncpu == 1 ? " cpu" : " cpus");
    ln_end(&l, false);

    const struct mem_usage *m = &snap->mem;
    ln_begin(&l, b, row++, width, plain);
    // Second segment: memory that is in use but reclaimable (cache etc).
    ln_bar(&l, "mem", bar_w, mem_permille(m->used_kb, m->total_kb), SGR_GREEN,
           mem_permille(m->available_kb - m->free_kb, m->total_kb), SGR_BLUE);
    ln_percent(&l, mem_permille(m->used_kb, m->total_kb), 6);
    ln_str(&l, "  ");
    ln_kb(&l, m->used_kb);
    ln_str(&l, " / ");
    ln_kb(&l, m->total_kb);
    ln_str(&l, "  avail ");
    ln_kb(&l, m->available_kb);
    ln_str(&l, "  cache ");
    ln_kb(&l, m->cached_kb);
    ln_end(&l, false);

    ln_begin(&l, b, row++, width, plain);
    ln_bar(&l, "swp", bar_w, mem_permille(m->swap_used_kb, m->swap_total_kb), SGR_RED, 0, "");
    ln_percent(&l, mem_permille(m->swap_used_kb, m->swap_total_kb), 6);
    ln_str(&l, "  ");
    ln_kb(&l, m->swap_used_kb);
    ln_str(&l, " / ");
    ln_kb(&l, m->swap_total_kb);
    ln_end(&l, false);
}

static void render_columns(struct buffer *b, usize row, usize width, bool plain) {
    struct line l;
    ln_begin(&l, b, row, width, plain);
    ln_esc(&l, SGR_INVERSE);
    ln_str(&l, "    PID    PPID S  NI  THR   CPU%  MEM%    RSS      TIME+ COMMAND");
    ln_end(&l, !plain);
}

static void render_process(struct buffer *b, const struct process *p, const struct snapshot *snap,
                           const struct view_state *vs, usize row, usize width, bool plain) {
    struct line l;
    char t[32];

    ln_begin(&l, b, row, width, plain);
    ln_u64(&l, p->pid, 7);
    ln_str(&l, " ");
    ln_u64(&l, p->ppid, 7);
    ln_str(&l, " ");

    bool running = p->state == 'R';
    if (running) ln_esc(&l, SGR_GREEN SGR_BOLD);
    else if (p->state == 'D' || p->state == 'Z') ln_esc(&l, SGR_RED SGR_BOLD);
    ln_bytes(&l, &p->state, 1);
    ln_esc(&l, SGR_RESET);

    ln_str(&l, " ");
    ln_i64(&l, p->nice, 3);
    ln_str(&l, " ");
    ln_u64(&l, p->threads, 4);
    ln_str(&l, " ");
    ln_percent(&l, p->cpu_permille, 6);
    ln_str(&l, " ");
    ln_percent(&l, mem_permille(p->rss_kb, snap->mem.total_kb), 5);
    ln_str(&l, " ");
    ln_field(&l, t, fmt_kb(t, p->rss_kb), 6, true);
    ln_str(&l, " ");
    ln_field(&l, t, time_format_cpu(t, p->cpu_time_ms), 10, true);
    ln_str(&l, " ");

    if (!p->cmdline[0]) {
        // Kernel threads have no command line; ps shows them bracketed.
        ln_esc(&l, SGR_BLUE);
        ln_str(&l, "[");
        ln_str(&l, p->comm);
        ln_str(&l, "]");
    } else if (vs->show_cmdline) {
        ln_str(&l, p->cmdline);
    } else {
        ln_str(&l, p->comm);
    }
    ln_end(&l, false);
}

static usize bar_width(usize cols) {
    usize w = cols / 3;
    return MAX(10u, MIN(w, 40u));
}

void render_screen(struct buffer *out, const struct snapshot *snap,
                   struct view_state *vs, const struct vector *order,
                   u16 rows, u16 cols) {
    buf_clear(out);

    if (rows < MIN_ROWS || cols < MIN_COLS) {
        buf_append_str(out, "\x1b[H\x1b[2Jterminal too small");
        return;
    }

    usize width = cols;
    render_summary(out, snap, vs->mode, width, bar_width(width), false, 1);

    struct line l;
    ln_begin(&l, out, 5, width, false);
    if (vs->message) {
        ln_esc(&l, SGR_YELLOW);
        ln_str(&l, vs->message);
    }
    ln_end(&l, false);

    render_columns(out, 6, width, false);

    usize visible = render_visible_rows(rows);
    usize max_scroll = order->len > visible ? order->len - visible : 0;
    if (vs->scroll > max_scroll) vs->scroll = max_scroll;

    usize row = CHROME_ROWS;
    for (usize i = 0; i < visible; i++, row++) {
        const struct process **pp = vec_at(order, vs->scroll + i);
        if (pp) {
            render_process(out, *pp, snap, vs, row, width, false);
        } else {
            ln_begin(&l, out, row, width, false);
            ln_end(&l, false);
        }
    }

    ln_begin(&l, out, rows, width, false);
    ln_esc(&l, SGR_INVERSE);
    ln_str(&l, vs->keys_hint ? vs->keys_hint : "");
    ln_str(&l, "  sort ");
    ln_str(&l, sort_key_name(vs->sort));
    if (vs->reverse) ln_str(&l, " (rev)");
    ln_end(&l, true);
}

void render_plain(struct buffer *out, const struct snapshot *snap,
                  const struct view_state *vs, const struct vector *order,
                  usize max_rows) {
    render_summary(out, snap, vs->mode, PLAIN_WIDTH, 20, true, 0);
    buf_append_char(out, '\n');
    render_columns(out, 0, PLAIN_WIDTH, true);

    usize n = order->len;
    if (max_rows && max_rows < n) n = max_rows;
    for (usize i = 0; i < n; i++) {
        const struct process **pp = vec_at(order, i);
        render_process(out, *pp, snap, vs, 0, PLAIN_WIDTH, true);
    }
    buf_append_char(out, '\n');
}

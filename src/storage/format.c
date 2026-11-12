#include "storage/format.h"
#include "util/string.h"

// Smallest possible encoded process: fixed fields + two empty strings.
#define MIN_PROCESS_BYTES (4 + 4 + 1 + 4 + 4 + 4 + 8 * 4 + 2 + 2)

// ---- reader --------------------------------------------------------------

void rd_init(struct reader *r, const void *data, usize len) {
    r->p = data;
    r->end = r->p + len;
    r->failed = false;
}

usize rd_remaining(const struct reader *r) {
    return r->failed ? 0 : (usize)(r->end - r->p);
}

bool rd_skip(struct reader *r, usize n) {
    if (r->failed || n > rd_remaining(r)) {
        r->failed = true;
        return false;
    }
    r->p += n;
    return true;
}

static u64 rd_le(struct reader *r, usize n) {
    const u8 *p = r->p;
    if (!rd_skip(r, n)) return 0;
    u64 v = 0;
    for (usize i = 0; i < n; i++) v |= (u64)p[i] << (8 * i);
    return v;
}

u8  rd_u8(struct reader *r)  { return (u8)rd_le(r, 1); }
u16 rd_u16(struct reader *r) { return (u16)rd_le(r, 2); }
u32 rd_u32(struct reader *r) { return (u32)rd_le(r, 4); }
u64 rd_u64(struct reader *r) { return rd_le(r, 8); }

void rd_str(struct reader *r, char *out, usize cap) {
    usize len = rd_u16(r);
    const char *src = (const char *)r->p;
    if (!rd_skip(r, len)) {
        if (cap) out[0] = '\0';
        return;
    }
    str_copy_n(out, cap, src, len);
}

// ---- writer --------------------------------------------------------------

static void wr_le(struct buffer *b, u64 v, usize n) {
    u8 bytes[8];
    for (usize i = 0; i < n; i++) bytes[i] = (u8)(v >> (8 * i));
    buf_append(b, bytes, n);
}

void wr_u8(struct buffer *b, u8 v)   { wr_le(b, v, 1); }
void wr_u16(struct buffer *b, u16 v) { wr_le(b, v, 2); }
void wr_u32(struct buffer *b, u32 v) { wr_le(b, v, 4); }
void wr_u64(struct buffer *b, u64 v) { wr_le(b, v, 8); }

void wr_str(struct buffer *b, const char *s) {
    usize len = MIN(str_len(s), (usize)0xffff);
    wr_u16(b, (u16)len);
    buf_append(b, s, len);
}

// ---- header --------------------------------------------------------------

void format_write_header(struct buffer *out, const struct format_header *h) {
    usize name_len = str_len(h->hostname);
    buf_append(out, FORMAT_MAGIC, FORMAT_MAGIC_LEN);
    wr_u16(out, FORMAT_VERSION);
    wr_u16(out, (u16)(FORMAT_HEADER_SIZE + 2 + name_len));
    wr_u32(out, h->interval_ms);
    wr_str(out, h->hostname);
}

bool format_read_header(struct reader *r, struct format_header *h) {
    const u8 *start = r->p;
    if (rd_remaining(r) < FORMAT_MAGIC_LEN) return false;
    if (mem_cmp(r->p, FORMAT_MAGIC, FORMAT_MAGIC_LEN) != 0) return false;
    rd_skip(r, FORMAT_MAGIC_LEN);

    h->version = rd_u16(r);
    u16 size = rd_u16(r);
    h->interval_ms = rd_u32(r);

    if (r->failed || h->version != FORMAT_VERSION || size < FORMAT_HEADER_SIZE) return false;

    // Optional fields, bounded by the declared header size.
    struct reader rest;
    usize consumed = (usize)(r->p - start);
    if (size - consumed > rd_remaining(r)) return false;
    rd_init(&rest, r->p, size - consumed);
    h->hostname[0] = '\0';
    if (rd_remaining(&rest) >= 2) rd_str(&rest, h->hostname, sizeof(h->hostname));
    if (rest.failed) h->hostname[0] = '\0';

    return rd_skip(r, size - consumed);
}

// ---- frames --------------------------------------------------------------

static void write_process(struct buffer *b, const struct process *p) {
    wr_u32(b, p->pid);
    wr_u32(b, p->ppid);
    wr_u8(b, (u8)p->state);
    wr_u32(b, (u32)p->nice);
    wr_u32(b, p->threads);
    wr_u32(b, p->cpu_permille);
    wr_u64(b, p->cpu_time_ms);
    wr_u64(b, p->rss_kb);
    wr_u64(b, p->vsize_kb);
    wr_u64(b, p->start_ticks);
    wr_str(b, p->comm);
    wr_str(b, p->cmdline);
}

static void read_process(struct reader *r, struct process *p) {
    p->pid = rd_u32(r);
    p->ppid = rd_u32(r);
    p->state = (char)rd_u8(r);
    p->nice = (i32)rd_u32(r);
    p->threads = rd_u32(r);
    p->cpu_permille = rd_u32(r);
    p->cpu_time_ms = rd_u64(r);
    p->rss_kb = rd_u64(r);
    p->vsize_kb = rd_u64(r);
    p->start_ticks = rd_u64(r);
    rd_str(r, p->comm, sizeof(p->comm));
    rd_str(r, p->cmdline, sizeof(p->cmdline));
}

void format_write_frame(struct buffer *b, const struct snapshot *s) {
    usize len_at = b->len;
    wr_u32(b, 0);  // patched below

    wr_u64(b, s->time_ms);
    wr_u64(b, s->uptime_s);
    for (usize i = 0; i < 3; i++) wr_u32(b, s->load_x100[i]);
    wr_u32(b, s->tasks_total);
    wr_u32(b, s->tasks_running);
    wr_u32(b, s->spawned);
    wr_u32(b, s->exited);

    wr_u32(b, s->cpu.total_permille);
    wr_u32(b, s->cpu.user_permille);
    wr_u32(b, s->cpu.system_permille);
    wr_u32(b, s->cpu.iowait_permille);
    wr_u32(b, s->cpu.steal_permille);
    wr_u32(b, s->cpu.ncpu);

    wr_u64(b, s->mem.total_kb);
    wr_u64(b, s->mem.used_kb);
    wr_u64(b, s->mem.available_kb);
    wr_u64(b, s->mem.free_kb);
    wr_u64(b, s->mem.cached_kb);
    wr_u64(b, s->mem.buffers_kb);
    wr_u64(b, s->mem.swap_total_kb);
    wr_u64(b, s->mem.swap_used_kb);

    wr_u32(b, (u32)s->procs->len);
    for (usize i = 0; i < s->procs->len; i++) {
        write_process(b, vec_at(s->procs, i));
    }

    if (b->failed) return;
    u8 len[4];
    u32 payload = (u32)(b->len - len_at - 4);
    for (usize i = 0; i < 4; i++) len[i] = (u8)(payload >> (8 * i));
    buf_patch(b, len_at, len, 4);
}

bool format_skip_frame(struct reader *r) {
    u32 len = rd_u32(r);
    return !r->failed && rd_skip(r, len);
}

bool format_read_frame(struct reader *outer, struct snapshot *s, struct vector *procs) {
    u32 len = rd_u32(outer);
    const u8 *payload = outer->p;
    if (!rd_skip(outer, len)) return false;

    struct reader rr;
    struct reader *r = &rr;
    rd_init(r, payload, len);

    mem_set(s, 0, sizeof(*s));
    s->hostname = "";
    s->time_ms = rd_u64(r);
    s->uptime_s = rd_u64(r);
    for (usize i = 0; i < 3; i++) s->load_x100[i] = rd_u32(r);
    s->tasks_total = rd_u32(r);
    s->tasks_running = rd_u32(r);
    s->spawned = rd_u32(r);
    s->exited = rd_u32(r);

    s->cpu.total_permille = rd_u32(r);
    s->cpu.user_permille = rd_u32(r);
    s->cpu.system_permille = rd_u32(r);
    s->cpu.iowait_permille = rd_u32(r);
    s->cpu.steal_permille = rd_u32(r);
    s->cpu.ncpu = rd_u32(r);

    s->mem.total_kb = rd_u64(r);
    s->mem.used_kb = rd_u64(r);
    s->mem.available_kb = rd_u64(r);
    s->mem.free_kb = rd_u64(r);
    s->mem.cached_kb = rd_u64(r);
    s->mem.buffers_kb = rd_u64(r);
    s->mem.swap_total_kb = rd_u64(r);
    s->mem.swap_used_kb = rd_u64(r);

    u32 count = rd_u32(r);
    // Refuse counts that cannot possibly fit, before allocating for them.
    if (r->failed || count > rd_remaining(r) / MIN_PROCESS_BYTES) return false;

    vec_clear(procs);
    if (!vec_reserve(procs, count)) return false;
    for (u32 i = 0; i < count; i++) {
        struct process *p = vec_push(procs);
        read_process(r, p);
    }

    s->procs = procs;
    return !r->failed;
}

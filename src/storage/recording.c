#include "storage/recording.h"
#include "platform/file.h"
#include "util/string.h"

// ---- recording -----------------------------------------------------------

static bool flush(struct recorder *r) {
    bool ok = !r->buf.failed && file_write_all(r->fd, r->buf.data, r->buf.len);
    buf_clear(&r->buf);
    return ok;
}

bool recorder_open(struct recorder *r, const char *path, u32 interval_ms, const char *hostname) {
    buf_init(&r->buf);
    r->frames = 0;
    r->fd = file_create(path);
    if (r->fd < 0) return false;

    struct format_header h = { .version = FORMAT_VERSION, .interval_ms = interval_ms };
    str_copy(h.hostname, sizeof(h.hostname), hostname ? hostname : "");
    format_write_header(&r->buf, &h);
    if (!flush(r)) {
        recorder_close(r);
        return false;
    }
    return true;
}

bool recorder_write(struct recorder *r, const struct snapshot *snap) {
    if (r->fd < 0) return false;
    format_write_frame(&r->buf, snap);
    if (!flush(r)) return false;
    r->frames++;
    return true;
}

void recorder_close(struct recorder *r) {
    file_close(r->fd);
    r->fd = -1;
    buf_free(&r->buf);
}

// ---- playback ------------------------------------------------------------

static bool fail(const char **error, const char *msg) {
    if (error) *error = msg;
    return false;
}

bool player_open(struct player *p, const char *path, const char **error) {
    mem_set(p, 0, sizeof(*p));
    buf_init(&p->file);
    vec_init(&p->offsets, sizeof(usize));
    vec_init(&p->procs, sizeof(struct process));

    if (!file_read_all(path, &p->file)) return fail(error, "cannot read file");

    struct reader r;
    rd_init(&r, p->file.data, p->file.len);
    if (!format_read_header(&r, &p->header)) {
        return fail(error, "not a wproc recording (or a newer version)");
    }

    while (rd_remaining(&r) > 0) {
        usize offset = (usize)(r.p - p->file.data);
        if (!format_skip_frame(&r)) {
            p->truncated = true;
            break;
        }
        usize *slot = vec_push(&p->offsets);
        if (!slot) return fail(error, "out of memory");
        *slot = offset;
    }

    if (p->offsets.len == 0) return fail(error, "recording has no frames");

    struct snapshot first;
    if (!player_load(p, 0, &first)) return fail(error, "first frame is corrupt");
    p->first_time_ms = first.time_ms;
    return true;
}

usize player_frame_count(const struct player *p) {
    return p->offsets.len;
}

bool player_load(struct player *p, usize index, struct snapshot *out) {
    const usize *offset = vec_at(&p->offsets, index);
    if (!offset) return false;

    struct reader r;
    rd_init(&r, p->file.data + *offset, p->file.len - *offset);
    if (!format_read_frame(&r, out, &p->procs)) return false;
    out->hostname = p->header.hostname;
    return true;
}

void player_close(struct player *p) {
    buf_free(&p->file);
    vec_free(&p->offsets);
    vec_free(&p->procs);
}

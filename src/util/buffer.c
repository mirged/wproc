#include "util/buffer.h"
#include "util/alloc.h"
#include "util/string.h"

#define BUF_MIN_CAP 256

void buf_init(struct buffer *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->failed = false;
}

void buf_free(struct buffer *b) {
    mem_free(b->data);
    buf_init(b);
}

void buf_clear(struct buffer *b) {
    b->len = 0;
    b->failed = false;
}

bool buf_reserve(struct buffer *b, usize extra) {
    if (b->failed) return false;
    if (extra > (usize)-1 / 4 - b->len) {
        b->failed = true;
        return false;
    }

    usize need = b->len + extra + 1;  // +1: room for a terminator
    if (need <= b->cap) return true;

    usize cap = b->cap ? b->cap : BUF_MIN_CAP;
    while (cap < need) cap *= 2;

    u8 *p = mem_realloc(b->data, cap);
    if (!p) {
        b->failed = true;
        return false;
    }
    b->data = p;
    b->cap = cap;
    return true;
}

bool buf_append(struct buffer *b, const void *data, usize n) {
    if (!buf_reserve(b, n)) return false;
    mem_copy(b->data + b->len, data, n);
    b->len += n;
    return true;
}

bool buf_append_str(struct buffer *b, const char *s) {
    return buf_append(b, s, str_len(s));
}

bool buf_append_char(struct buffer *b, char c) {
    if (!buf_reserve(b, 1)) return false;
    b->data[b->len++] = (u8)c;
    return true;
}

bool buf_append_repeat(struct buffer *b, char c, usize n) {
    if (!buf_reserve(b, n)) return false;
    mem_set(b->data + b->len, c, n);
    b->len += n;
    return true;
}

bool buf_append_u64(struct buffer *b, u64 v) {
    char tmp[20];
    return buf_append(b, tmp, str_from_u64(tmp, v));
}

bool buf_append_i64(struct buffer *b, i64 v) {
    char tmp[21];
    return buf_append(b, tmp, str_from_i64(tmp, v));
}

bool buf_patch(struct buffer *b, usize offset, const void *data, usize n) {
    if (b->failed || offset > b->len || n > b->len - offset) return false;
    mem_copy(b->data + offset, data, n);
    return true;
}

const char *buf_cstr(struct buffer *b) {
    if (!buf_reserve(b, 0)) return "";
    b->data[b->len] = '\0';
    return (const char *)b->data;
}

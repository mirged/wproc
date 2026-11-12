#include "util/parse.h"

static bool is_ws(char ch)    { return ch == ' ' || ch == '\t'; }
static bool is_digit(char ch) { return ch >= '0' && ch <= '9'; }

void cur_init(struct cursor *c, const char *data, usize len) {
    c->p = data;
    c->end = data + len;
}

bool cur_eof(const struct cursor *c) {
    return c->p >= c->end;
}

char cur_peek(const struct cursor *c) {
    return cur_eof(c) ? '\0' : *c->p;
}

void cur_skip_ws(struct cursor *c) {
    while (c->p < c->end && is_ws(*c->p)) c->p++;
}

void cur_skip_line(struct cursor *c) {
    while (c->p < c->end && *c->p != '\n') c->p++;
    if (c->p < c->end) c->p++;
}

bool cur_expect(struct cursor *c, char ch) {
    if (c->p < c->end && *c->p == ch) {
        c->p++;
        return true;
    }
    return false;
}

bool cur_word(struct cursor *c, const char **start, usize *len) {
    cur_skip_ws(c);
    const char *s = c->p;
    while (c->p < c->end && !is_ws(*c->p) && *c->p != '\n') c->p++;
    *start = s;
    *len = (usize)(c->p - s);
    return *len > 0;
}

bool cur_skip_field(struct cursor *c) {
    const char *s;
    usize n;
    return cur_word(c, &s, &n);
}

bool cur_skip_fields(struct cursor *c, u32 count) {
    while (count--) {
        if (!cur_skip_field(c)) return false;
    }
    return true;
}

// Digits only, no whitespace skipping.
static bool take_digits(struct cursor *c, u64 *out) {
    if (c->p >= c->end || !is_digit(*c->p)) return false;
    u64 v = 0;
    while (c->p < c->end && is_digit(*c->p)) {
        u64 d = (u64)(*c->p - '0');
        if (v > (U64_MAX - d) / 10) return false;
        v = v * 10 + d;
        c->p++;
    }
    *out = v;
    return true;
}

bool cur_u64(struct cursor *c, u64 *out) {
    cur_skip_ws(c);
    return take_digits(c, out);
}

bool cur_i64(struct cursor *c, i64 *out) {
    cur_skip_ws(c);
    bool neg = cur_expect(c, '-');
    u64 mag;
    if (!take_digits(c, &mag)) return false;
    if (neg) {
        if (mag > (u64)I64_MAX + 1) return false;
        *out = (i64)((u64)0 - mag);
    } else {
        if (mag > (u64)I64_MAX) return false;
        *out = (i64)mag;
    }
    return true;
}

bool cur_fixed(struct cursor *c, u32 digits, u64 *out) {
    u64 whole;
    if (!cur_u64(c, &whole)) return false;

    u64 scale = 1;
    for (u32 i = 0; i < digits; i++) scale *= 10;
    if (whole > U64_MAX / scale) return false;

    u64 frac = 0;
    u32 taken = 0;
    if (cur_expect(c, '.')) {
        while (c->p < c->end && is_digit(*c->p)) {
            if (taken < digits) {
                frac = frac * 10 + (u64)(*c->p - '0');
                taken++;
            }
            c->p++;
        }
    }
    while (taken < digits) {
        frac *= 10;
        taken++;
    }
    *out = whole * scale + frac;
    return true;
}

bool parse_u64(const char *s, u64 *out) {
    const char *e = s;
    while (*e) e++;
    struct cursor c;
    cur_init(&c, s, (usize)(e - s));
    return take_digits(&c, out) && cur_eof(&c);
}

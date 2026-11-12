#include "util/string.h"

void *mem_copy(void *dst, const void *src, usize n) {
    u8 *d = dst;
    const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *mem_move(void *dst, const void *src, usize n) {
    u8 *d = dst;
    const u8 *s = src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *mem_set(void *dst, i32 c, usize n) {
    u8 *d = dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

i32 mem_cmp(const void *a, const void *b, usize n) {
    const u8 *x = a, *y = b;
    for (usize i = 0; i < n; i++) {
        if (x[i] != y[i]) return (i32)x[i] - (i32)y[i];
    }
    return 0;
}

// GCC may emit calls to these four even in freestanding mode
// (struct copies, large zero-initialisers). Provide them.
void *memcpy(void *dst, const void *src, usize n);
void *memmove(void *dst, const void *src, usize n);
void *memset(void *dst, int c, usize n);
int   memcmp(const void *a, const void *b, usize n);

void *memcpy(void *dst, const void *src, usize n)  { return mem_copy(dst, src, n); }
void *memmove(void *dst, const void *src, usize n) { return mem_move(dst, src, n); }
void *memset(void *dst, int c, usize n)            { return mem_set(dst, c, n); }
int   memcmp(const void *a, const void *b, usize n) { return mem_cmp(a, b, n); }

usize str_len(const char *s) {
    usize n = 0;
    while (s[n]) n++;
    return n;
}

i32 str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (i32)(u8)*a - (i32)(u8)*b;
}

bool str_eq(const char *a, const char *b) {
    return str_cmp(a, b) == 0;
}

bool str_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return false;
    }
    return true;
}

bool str_eq_n(const char *s, usize len, const char *cstr) {
    for (usize i = 0; i < len; i++) {
        if (cstr[i] == '\0' || cstr[i] != s[i]) return false;
    }
    return cstr[len] == '\0';
}

usize str_copy_n(char *dst, usize cap, const char *src, usize len) {
    if (cap == 0) return 0;
    usize n = MIN(len, cap - 1);
    mem_copy(dst, src, n);
    dst[n] = '\0';
    return n;
}

usize str_copy(char *dst, usize cap, const char *src) {
    return str_copy_n(dst, cap, src, str_len(src));
}

static char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

i32 str_cmp_ascii_nocase(const char *a, const char *b) {
    while (*a && ascii_lower(*a) == ascii_lower(*b)) {
        a++;
        b++;
    }
    return (i32)(u8)ascii_lower(*a) - (i32)(u8)ascii_lower(*b);
}

usize str_from_u64(char *out, u64 v) {
    char tmp[20];
    usize n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (usize i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

usize str_from_i64(char *out, i64 v) {
    if (v < 0) {
        out[0] = '-';
        // Negate in unsigned space so I64_MIN does not overflow.
        return 1 + str_from_u64(out + 1, (u64)0 - (u64)v);
    }
    return str_from_u64(out, (u64)v);
}

usize str_from_fixed(char *out, u64 value, u32 digits) {
    u64 div = 1;
    for (u32 i = 0; i < digits; i++) div *= 10;

    usize n = str_from_u64(out, value / div);
    if (digits == 0) return n;

    out[n++] = '.';
    u64 frac = value % div;
    for (u32 i = digits; i-- > 0;) {
        u64 p = 1;
        for (u32 j = 0; j < i; j++) p *= 10;
        out[n++] = (char)('0' + (frac / p) % 10);
    }
    return n;
}

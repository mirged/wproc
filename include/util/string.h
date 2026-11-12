#ifndef WPROC_UTIL_STRING_H
#define WPROC_UTIL_STRING_H

#include "util/types.h"

// Raw memory
void *mem_copy(void *dst, const void *src, usize n);
void *mem_move(void *dst, const void *src, usize n);
void *mem_set(void *dst, i32 c, usize n);
i32   mem_cmp(const void *a, const void *b, usize n);

// NUL-terminated strings
usize str_len(const char *s);
i32   str_cmp(const char *a, const char *b);
bool  str_eq(const char *a, const char *b);
bool  str_starts_with(const char *s, const char *prefix);

// Compare a counted (non-terminated) slice against a C string.
bool  str_eq_n(const char *s, usize len, const char *cstr);

// Bounded copy. Always NUL-terminates when cap > 0.
// Returns the number of bytes copied (excluding the terminator).
usize str_copy(char *dst, usize cap, const char *src);
usize str_copy_n(char *dst, usize cap, const char *src, usize len);

// Case-insensitive ASCII comparison.
i32   str_cmp_ascii_nocase(const char *a, const char *b);

// Number formatting. `out` must have room for 20 bytes (u64)
// or 21 bytes (i64). No terminator is written; the length is returned.
usize str_from_u64(char *out, u64 v);
usize str_from_i64(char *out, i64 v);

// Fixed-point formatting: value / 10^digits, e.g. (234, 1) -> "23.4".
// `out` must have room for 22 bytes.
usize str_from_fixed(char *out, u64 value, u32 digits);

#endif // WPROC_UTIL_STRING_H

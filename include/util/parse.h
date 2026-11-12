#ifndef WPROC_UTIL_PARSE_H
#define WPROC_UTIL_PARSE_H

#include "util/types.h"

// A read-only cursor over a byte range. Nothing here allocates and
// nothing requires NUL termination.
//
// "Whitespace" means spaces and tabs only. Newlines are significant
// for line-oriented /proc files, so they are never skipped implicitly.
struct cursor {
    const char *p;
    const char *end;
};

void cur_init(struct cursor *c, const char *data, usize len);
bool cur_eof(const struct cursor *c);
char cur_peek(const struct cursor *c);   // '\0' at end

void cur_skip_ws(struct cursor *c);
void cur_skip_line(struct cursor *c);
bool cur_expect(struct cursor *c, char ch);

// Skip whitespace, then take a run of non-whitespace, non-newline bytes.
bool cur_word(struct cursor *c, const char **start, usize *len);
bool cur_skip_field(struct cursor *c);
bool cur_skip_fields(struct cursor *c, u32 count);

// Skip whitespace, then parse. Fail on no digits or overflow.
bool cur_u64(struct cursor *c, u64 *out);
bool cur_i64(struct cursor *c, i64 *out);

// Parse "12.34" as fixed point with `digits` fractional digits:
// cur_fixed(.., 2) on "0.52" -> 52. Extra digits are truncated.
bool cur_fixed(struct cursor *c, u32 digits, u64 *out);

// Parse an entire NUL-terminated string as a u64.
bool parse_u64(const char *s, u64 *out);

#endif // WPROC_UTIL_PARSE_H

#ifndef WPROC_UTIL_BUFFER_H
#define WPROC_UTIL_BUFFER_H

#include "util/types.h"

// Growable byte buffer.
//
// Allocation failure is sticky: once `failed` is set every further append
// is a no-op, so callers can build a whole frame and check once at the end.
// The buffer always keeps one spare byte so buf_cstr() never reallocates.
struct buffer {
    u8   *data;
    usize len;
    usize cap;
    bool  failed;
};

void  buf_init(struct buffer *b);
void  buf_free(struct buffer *b);
void  buf_clear(struct buffer *b);
bool  buf_reserve(struct buffer *b, usize extra);

bool  buf_append(struct buffer *b, const void *data, usize n);
bool  buf_append_str(struct buffer *b, const char *s);
bool  buf_append_char(struct buffer *b, char c);
bool  buf_append_repeat(struct buffer *b, char c, usize n);
bool  buf_append_u64(struct buffer *b, u64 v);
bool  buf_append_i64(struct buffer *b, i64 v);

// Overwrite bytes already in the buffer (used for back-patching lengths).
bool  buf_patch(struct buffer *b, usize offset, const void *data, usize n);

// NUL-terminated view of the contents. Valid until the next append.
const char *buf_cstr(struct buffer *b);

#endif // WPROC_UTIL_BUFFER_H

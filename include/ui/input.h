#ifndef WPROC_UI_INPUT_H
#define WPROC_UI_INPUT_H

#include "util/types.h"

enum key_code {
    KEY_NONE,       // unrecognised escape sequence, ignore
    KEY_CHAR,
    KEY_ESCAPE,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_HOME,
    KEY_END,
};

struct key_event {
    enum key_code code;
    char ch;        // for KEY_CHAR
};

#define KEY_CTRL(c) ((char)((c) & 0x1f))

// Wait up to timeout_ms for input, then decode whatever arrived.
// Returns the number of events written (0 on timeout).
usize input_read(i32 fd, i32 timeout_ms, struct key_event *out, usize max);

// Decode one key from raw bytes. Returns bytes consumed (>= 1).
// Exposed for testing.
usize input_decode(const u8 *s, usize n, struct key_event *ev);

#endif // WPROC_UI_INPUT_H

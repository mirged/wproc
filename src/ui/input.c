#include "ui/input.h"
#include "platform/linux.h"

static bool is_digit(u8 c) { return c >= '0' && c <= '9'; }

// VT-style "ESC [ <n> ~" keys.
static enum key_code tilde_key(u32 n) {
    switch (n) {
    case 1: case 7: return KEY_HOME;
    case 4: case 8: return KEY_END;
    case 5:         return KEY_PAGE_UP;
    case 6:         return KEY_PAGE_DOWN;
    default:        return KEY_NONE;
    }
}

usize input_decode(const u8 *s, usize n, struct key_event *ev) {
    ev->ch = 0;

    if (s[0] != 0x1b) {
        ev->code = KEY_CHAR;
        ev->ch = (char)s[0];
        return 1;
    }

    // A lone ESC, or ESC followed by something that is not CSI/SS3.
    if (n < 3 || (s[1] != '[' && s[1] != 'O')) {
        ev->code = KEY_ESCAPE;
        return 1;
    }

    switch (s[2]) {
    case 'A': ev->code = KEY_UP;    return 3;
    case 'B': ev->code = KEY_DOWN;  return 3;
    case 'C': ev->code = KEY_RIGHT; return 3;
    case 'D': ev->code = KEY_LEFT;  return 3;
    case 'H': ev->code = KEY_HOME;  return 3;
    case 'F': ev->code = KEY_END;   return 3;
    }

    usize i = 2;
    u32 num = 0;
    while (i < n && is_digit(s[i])) {
        num = num * 10 + (u32)(s[i] - '0');
        i++;
    }
    if (i < n && s[i] == '~') {
        ev->code = tilde_key(num);
        return i + 1;
    }

    // Unknown CSI sequence: skip to its final byte so it does not
    // get replayed as a string of ordinary keypresses.
    while (i < n && !(s[i] >= 0x40 && s[i] <= 0x7e)) i++;
    ev->code = KEY_NONE;
    return i < n ? i + 1 : n;
}

usize input_read(i32 fd, i32 timeout_ms, struct key_event *out, usize max) {
    struct pollfd p = { .fd = fd, .events = POLLIN, .revents = 0 };
    if (sys_poll(&p, 1, timeout_ms) <= 0 || !(p.revents & POLLIN)) return 0;

    u8 buf[64];
    isize n = sys_read(fd, buf, sizeof(buf));
    if (n <= 0) return 0;

    usize count = 0;
    for (usize i = 0; i < (usize)n && count < max;) {
        i += input_decode(buf + i, (usize)n - i, &out[count]);
        if (out[count].code != KEY_NONE) count++;
    }
    return count;
}

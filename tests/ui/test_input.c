#include "test.h"
#include "ui/input.h"

static struct key_event decode(const char *s, usize *used) {
    struct key_event ev;
    *used = input_decode((const u8 *)s, str_len(s), &ev);
    return ev;
}

TEST(test_plain_keys) {
    usize n;
    struct key_event ev = decode("q", &n);
    CHECK(ev.code == KEY_CHAR && ev.ch == 'q' && n == 1);
    ev = decode("\x03", &n);
    CHECK(ev.code == KEY_CHAR && ev.ch == KEY_CTRL('c'));
    ev = decode("\x1b", &n);
    CHECK(ev.code == KEY_ESCAPE && n == 1);
}

TEST(test_escape_sequences) {
    usize n;
    CHECK(decode("\x1b[A", &n).code == KEY_UP && n == 3);
    CHECK(decode("\x1b[B", &n).code == KEY_DOWN);
    CHECK(decode("\x1bOC", &n).code == KEY_RIGHT);   // SS3 form
    CHECK(decode("\x1b[D", &n).code == KEY_LEFT);
    CHECK(decode("\x1b[5~", &n).code == KEY_PAGE_UP && n == 4);
    CHECK(decode("\x1b[6~", &n).code == KEY_PAGE_DOWN);
    CHECK(decode("\x1b[1~", &n).code == KEY_HOME);
    CHECK(decode("\x1b[4~", &n).code == KEY_END);
    CHECK(decode("\x1b[H", &n).code == KEY_HOME);
}

TEST(test_unknown_sequences_are_swallowed) {
    usize n;
    // Ctrl+Up in xterm: ESC [ 1 ; 5 A. Must not leak "5A" as keypresses.
    struct key_event ev = decode("\x1b[1;5Aq", &n);
    CHECK(ev.code == KEY_NONE && n == 6);
    ev = decode("\x1b[99~", &n);
    CHECK(ev.code == KEY_NONE && n == 5);
}

TEST_MAIN("ui/input",
    T(test_plain_keys),
    T(test_escape_sequences),
    T(test_unknown_sequences_are_swallowed))

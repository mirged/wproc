#ifndef WPROC_UI_TERMINAL_H
#define WPROC_UI_TERMINAL_H

#include "util/types.h"
#include "platform/linux.h"

struct terminal {
    i32 in_fd;
    i32 out_fd;
    u16 rows;
    u16 cols;
    bool active;
    struct kernel_termios saved;
};

// True if fd refers to a terminal.
bool term_is_tty(i32 fd);

// Raw mode, alternate screen, hidden cursor. Fails if not a tty.
bool term_init(struct terminal *t, i32 in_fd, i32 out_fd);

// Undo everything term_init did. Safe to call more than once.
void term_restore(struct terminal *t);

// Re-query the window size. Returns true if it changed.
bool term_update_size(struct terminal *t);

bool term_write(struct terminal *t, const void *data, usize len);

#endif // WPROC_UI_TERMINAL_H

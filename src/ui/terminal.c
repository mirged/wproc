#include "ui/terminal.h"
#include "platform/file.h"

#define ENTER_SCREEN "\x1b[?1049h\x1b[?25l\x1b[H\x1b[2J"
#define LEAVE_SCREEN "\x1b[0m\x1b[?25h\x1b[?1049l"

bool term_is_tty(i32 fd) {
    struct kernel_termios tio;
    return sys_ioctl(fd, TCGETS, &tio) == 0;
}

bool term_init(struct terminal *t, i32 in_fd, i32 out_fd) {
    t->in_fd = in_fd;
    t->out_fd = out_fd;
    t->rows = 24;
    t->cols = 80;
    t->active = false;

    if (!term_is_tty(out_fd)) return false;
    if (sys_ioctl(in_fd, TCGETS, &t->saved) < 0) return false;

    struct kernel_termios raw = t->saved;
    raw.c_iflag &= ~(u32)(TERM_IXON | TERM_ICRNL | TERM_BRKINT | TERM_INPCK | TERM_ISTRIP);
    // ISIG off: Ctrl-C arrives as a byte and we restore the terminal ourselves
    // instead of leaving it raw from inside a signal handler we don't have.
    raw.c_lflag &= ~(u32)(TERM_ECHO | TERM_ICANON | TERM_ISIG | TERM_IEXTEN);
    raw.c_cc[TERM_VMIN] = 0;
    raw.c_cc[TERM_VTIME] = 0;

    if (sys_ioctl(in_fd, TCSETS, &raw) < 0) return false;
    t->active = true;

    term_update_size(t);
    return term_write(t, ENTER_SCREEN, sizeof(ENTER_SCREEN) - 1);
}

void term_restore(struct terminal *t) {
    if (!t->active) return;
    term_write(t, LEAVE_SCREEN, sizeof(LEAVE_SCREEN) - 1);
    sys_ioctl(t->in_fd, TCSETS, &t->saved);
    t->active = false;
}

bool term_update_size(struct terminal *t) {
    struct winsize ws;
    if (sys_ioctl(t->out_fd, TIOCGWINSZ, &ws) < 0 || ws.ws_row == 0 || ws.ws_col == 0) {
        return false;
    }
    bool changed = ws.ws_row != t->rows || ws.ws_col != t->cols;
    t->rows = ws.ws_row;
    t->cols = ws.ws_col;
    return changed;
}

bool term_write(struct terminal *t, const void *data, usize len) {
    return file_write_all(t->out_fd, data, len);
}

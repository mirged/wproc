#include "platform/linux.h"

static usize str_len(const char *s) {
    usize len = 0;
    while (s[len]) len++;
    return len;
}

void entry(i64 argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/proc";

    i32 fd = sys_openat(AT_FDCWD, path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        sys_exit_group(1);
    }

    char buf[4096];
    isize nread;

    while ((nread = sys_getdents64(fd, buf, sizeof(buf))) > 0) {
        for (isize bpos = 0; bpos < nread;) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            sys_write(STDOUT_FILENO, d->d_name, str_len(d->d_name));
            sys_write(STDOUT_FILENO, "\n", 1);
            bpos += d->d_reclen;
        }
    }

    sys_close(fd);
    sys_exit_group(0);
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile (
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "and $-16, %rsp\n"
        "call entry\n"
    );
}

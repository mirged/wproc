#ifndef WPROC_PLATFORM_LINUX_H
#define WPROC_PLATFORM_LINUX_H

#include "util/types.h"

// File & directory flags
#define AT_FDCWD        -100
#define O_RDONLY        0
#define O_WRONLY        1
#define O_RDWR          2
#define O_CREAT         0100
#define O_TRUNC         01000
#define O_DIRECTORY     0200000
#define O_CLOEXEC       02000000

// Standard file descriptors
#define STDIN_FILENO     0
#define STDOUT_FILENO    1
#define STDERR_FILENO    2

// mmap flags (for src/util/alloc.c)
#define PROT_READ        0x1
#define PROT_WRITE       0x2
#define MAP_PRIVATE      0x02
#define MAP_ANONYMOUS    0x20
#define MAP_FAILED       ((void *)-1)
#define MREMAP_MAYMOVE   1

// errno values we actually care about
#define EINTR            4
#define EAGAIN           11

// Clocks
#define CLOCK_MONOTONIC  1

// poll
#define POLLIN           0x001

// Directory entry types
#define DT_DIR           4

// Auxiliary vector keys
#define AT_NULL          0
#define AT_PAGESZ        6
#define AT_CLKTCK        17

// Terminal ioctl constants (for src/ui/terminal.c)
#define TCGETS           0x5401
#define TCSETS           0x5402
#define TIOCGWINSZ       0x5413

// termios flags (asm-generic/termbits.h)
#define TERM_ISIG        0000001
#define TERM_ICANON      0000002
#define TERM_ECHO        0000010
#define TERM_IEXTEN      0100000
#define TERM_BRKINT      0000002
#define TERM_INPCK       0000020
#define TERM_ISTRIP      0000040
#define TERM_ICRNL       0000400
#define TERM_IXON        0002000
#define TERM_VTIME       5
#define TERM_VMIN        6
#define TERM_NCCS        19

// x86_64 Linux syscall numbers
#define SYS_read         0
#define SYS_write        1
#define SYS_close        3
#define SYS_poll         7
#define SYS_mmap         9
#define SYS_munmap       11
#define SYS_mremap       25
#define SYS_ioctl        16
#define SYS_nanosleep    35
#define SYS_getdents64   217
#define SYS_clock_gettime 228
#define SYS_exit_group   231
#define SYS_openat       257

// Kernel structs
struct linux_dirent64 {
    u64  d_ino;
    i64  d_off;
    u16  d_reclen;
    u8   d_type;
    char d_name[];
};

struct timespec {
    i64 tv_sec;
    i64 tv_nsec;
};

// The kernel's termios, not libc's (which is larger and differently laid out).
struct kernel_termios {
    u32 c_iflag;
    u32 c_oflag;
    u32 c_cflag;
    u32 c_lflag;
    u8  c_line;
    u8  c_cc[TERM_NCCS];
};

struct pollfd {
    i32 fd;
    i16 events;
    i16 revents;
};

struct winsize {
    u16 ws_row;
    u16 ws_col;
    u16 ws_xpixel;
    u16 ws_ypixel;
};

// Raw x86_64 syscall primitives
static inline i64 syscall1(i64 n, i64 a1) {
    i64 ret;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "rcx", "r11", "memory");
    return ret;
}

static inline i64 syscall2(i64 n, i64 a1, i64 a2) {
    i64 ret;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory");
    return ret;
}

static inline i64 syscall3(i64 n, i64 a1, i64 a2, i64 a3) {
    i64 ret;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

static inline i64 syscall4(i64 n, i64 a1, i64 a2, i64 a3, i64 a4) {
    i64 ret;
    register i64 r10 __asm__("r10") = a4;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory");
    return ret;
}

static inline i64 syscall6(i64 n, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5, i64 a6) {
    i64 ret;
    register i64 r10 __asm__("r10") = a4;
    register i64 r8  __asm__("r8")  = a5;
    register i64 r9  __asm__("r9")  = a6;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

// Typed, readable wrappers for the rest of wproc
static inline isize sys_read(i32 fd, void *buf, usize count) {
    return (isize)syscall3(SYS_read, fd, (i64)buf, (i64)count);
}

static inline isize sys_write(i32 fd, const void *buf, usize count) {
    return (isize)syscall3(SYS_write, fd, (i64)buf, (i64)count);
}

static inline i32 sys_openat(i32 dirfd, const char *path, i32 flags, u32 mode) {
    return (i32)syscall4(SYS_openat, dirfd, (i64)path, flags, mode);
}

static inline i32 sys_close(i32 fd) {
    return (i32)syscall1(SYS_close, fd);
}

static inline isize sys_getdents64(i32 fd, void *dirp, usize count) {
    return (isize)syscall3(SYS_getdents64, fd, (i64)dirp, (i64)count);
}

static inline i32 sys_ioctl(i32 fd, u64 req, void *arg) {
    return (i32)syscall3(SYS_ioctl, fd, (i64)req, (i64)arg);
}

static inline i32 sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    return (i32)syscall2(SYS_nanosleep, (i64)req, (i64)rem);
}

static inline void *sys_mmap(void *addr, usize len, i32 prot, i32 flags, i32 fd, isize offset) {
    return (void *)syscall6(SYS_mmap, (i64)addr, (i64)len, prot, flags, fd, offset);
}

static inline void *sys_mremap(void *old, usize old_len, usize new_len, i32 flags) {
    return (void *)syscall4(SYS_mremap, (i64)old, (i64)old_len, (i64)new_len, flags);
}

static inline i32 sys_clock_gettime(i32 clock, struct timespec *ts) {
    return (i32)syscall2(SYS_clock_gettime, clock, (i64)ts);
}

static inline i32 sys_poll(struct pollfd *fds, u64 nfds, i32 timeout_ms) {
    return (i32)syscall3(SYS_poll, (i64)fds, (i64)nfds, timeout_ms);
}

// Syscalls returning pointers report errors as values in [-4095, -1].
static inline bool sys_ptr_failed(const void *p) {
    return (u64)p > (u64)-4096;
}

static inline i32 sys_munmap(void *addr, usize len) {
    return (i32)syscall2(SYS_munmap, (i64)addr, (i64)len);
}

__attribute__((noreturn))
static inline void sys_exit_group(i32 status) {
    syscall1(SYS_exit_group, status);
    __builtin_unreachable();
}

#endif // WPROC_PLATFORM_LINUX_H

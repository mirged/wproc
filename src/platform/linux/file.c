#include "platform/file.h"
#include "platform/linux.h"
#include "util/string.h"

#define READ_CHUNK 4096

bool file_read_all(const char *path, struct buffer *out) {
    buf_clear(out);

    i32 fd = sys_openat(AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return false;

    bool ok = true;
    for (;;) {
        if (!buf_reserve(out, READ_CHUNK)) {
            ok = false;
            break;
        }
        isize n = sys_read(fd, out->data + out->len, READ_CHUNK);
        if (n == -EINTR) continue;
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) break;
        out->len += (usize)n;
    }

    sys_close(fd);
    return ok;
}

i32 file_create(const char *path) {
    i32 fd = sys_openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    return fd < 0 ? -1 : fd;
}

bool file_write_all(i32 fd, const void *data, usize len) {
    const u8 *p = data;
    while (len > 0) {
        isize n = sys_write(fd, p, len);
        if (n == -EINTR || n == -EAGAIN) continue;
        if (n <= 0) return false;
        p += n;
        len -= (usize)n;
    }
    return true;
}

bool file_write_str(i32 fd, const char *s) {
    return file_write_all(fd, s, str_len(s));
}

void file_close(i32 fd) {
    if (fd >= 0) sys_close(fd);
}

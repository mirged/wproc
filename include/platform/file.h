#ifndef WPROC_PLATFORM_FILE_H
#define WPROC_PLATFORM_FILE_H

#include "util/types.h"
#include "util/buffer.h"

// Minimal file I/O on top of raw syscalls.

// Replace the buffer's contents with the whole file. Works for /proc
// files, which report a size of zero and must be read until EOF.
bool file_read_all(const char *path, struct buffer *out);

// O_WRONLY | O_CREAT | O_TRUNC, mode 0644. Returns fd or -1.
i32  file_create(const char *path);

// Retries partial writes and EINTR.
bool file_write_all(i32 fd, const void *data, usize len);
bool file_write_str(i32 fd, const char *s);

void file_close(i32 fd);

#endif // WPROC_PLATFORM_FILE_H

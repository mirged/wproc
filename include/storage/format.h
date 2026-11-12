#ifndef WPROC_STORAGE_FORMAT_H
#define WPROC_STORAGE_FORMAT_H

#include "util/types.h"
#include "util/buffer.h"
#include "util/vector.h"
#include "core/monitor.h"

// Recording file format, version 1. All integers little-endian, written
// field by field: the layout never depends on C struct padding.
//
//   header:
//     u8[8]  magic "WPROCREC"
//     u16    version
//     u16    header size in bytes (from the start of the file)
//     u32    interval_ms
//     str    hostname                    (added later; absent if size == 16)
//
//   frame (repeated until EOF):
//     u32    payload size
//     payload:
//       u64 time_ms, u64 uptime_s, u32 load_x100[3],
//       u32 tasks_total, tasks_running, spawned, exited,
//       u32 cpu total, user, system, iowait, steal, ncpu (permille)
//       u64 mem total, used, available, free, cached, buffers,
//           swap_total, swap_used (kB)
//       u32 process count, then per process:
//         u32 pid, ppid; u8 state; u32 nice (two's complement);
//         u32 threads, cpu_permille;
//         u64 cpu_time_ms, rss_kb, vsize_kb, start_ticks;
//         str comm, str cmdline          (str = u16 length + bytes)
//
// Readers skip unknown trailing bytes in the header and in each frame,
// so fields can be appended without bumping the version.

#define FORMAT_MAGIC       "WPROCREC"
#define FORMAT_MAGIC_LEN   8
#define FORMAT_VERSION     1
#define FORMAT_HEADER_SIZE 16

struct format_header {
    u16  version;
    u32  interval_ms;
    char hostname[PROCFS_HOSTNAME_MAX];
};

// ---- primitives ----------------------------------------------------------

struct reader {
    const u8 *p;
    const u8 *end;
    bool      failed;   // sticky, like struct buffer
};

void  rd_init(struct reader *r, const void *data, usize len);
usize rd_remaining(const struct reader *r);
bool  rd_skip(struct reader *r, usize n);
u8    rd_u8(struct reader *r);
u16   rd_u16(struct reader *r);
u32   rd_u32(struct reader *r);
u64   rd_u64(struct reader *r);
// Reads a length-prefixed string, truncating to cap-1 bytes if needed.
void  rd_str(struct reader *r, char *out, usize cap);

void  wr_u8(struct buffer *b, u8 v);
void  wr_u16(struct buffer *b, u16 v);
void  wr_u32(struct buffer *b, u32 v);
void  wr_u64(struct buffer *b, u64 v);
void  wr_str(struct buffer *b, const char *s);

// ---- header & frames -----------------------------------------------------

void format_write_header(struct buffer *out, const struct format_header *h);
bool format_read_header(struct reader *r, struct format_header *h);

void format_write_frame(struct buffer *out, const struct snapshot *snap);

// Decode one frame. Processes go into `procs` (vector of struct process),
// and snap->procs is pointed at it.
bool format_read_frame(struct reader *r, struct snapshot *snap, struct vector *procs);

// Step over one frame without decoding it. False if truncated.
bool format_skip_frame(struct reader *r);

#endif // WPROC_STORAGE_FORMAT_H

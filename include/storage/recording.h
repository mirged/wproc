#ifndef WPROC_STORAGE_RECORDING_H
#define WPROC_STORAGE_RECORDING_H

#include "util/types.h"
#include "util/buffer.h"
#include "util/vector.h"
#include "core/monitor.h"
#include "storage/format.h"

// ---- recording -----------------------------------------------------------

struct recorder {
    i32           fd;
    struct buffer buf;
    u64           frames;
};

bool recorder_open(struct recorder *r, const char *path, u32 interval_ms);
bool recorder_write(struct recorder *r, const struct snapshot *snap);
void recorder_close(struct recorder *r);

// ---- playback ------------------------------------------------------------
//
// The whole file is loaded and indexed up front; frames are decoded on
// demand, one at a time, into a vector the player owns.

struct player {
    struct buffer        file;
    struct vector        offsets;   // of usize, start of each frame
    struct vector        procs;     // of struct process, current frame
    struct format_header header;
    u64                  first_time_ms;
    bool                 truncated; // last frame was cut short (crash mid-write)
};

// On failure `*error` (if non-NULL) is set to a static message.
bool  player_open(struct player *p, const char *path, const char **error);
usize player_frame_count(const struct player *p);
bool  player_load(struct player *p, usize index, struct snapshot *out);
void  player_close(struct player *p);

#endif // WPROC_STORAGE_RECORDING_H

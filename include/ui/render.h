#ifndef WPROC_UI_RENDER_H
#define WPROC_UI_RENDER_H

#include "util/types.h"
#include "util/buffer.h"
#include "util/vector.h"
#include "core/monitor.h"

enum sort_key {
    SORT_CPU,
    SORT_MEM,
    SORT_PID,
    SORT_TIME,
    SORT_NAME,
};

struct view_state {
    enum sort_key sort;
    bool          reverse;
    bool          show_cmdline;
    usize         scroll;
    const char   *mode;       // shown in the summary line, e.g. "live"
    const char   *message;    // optional status line
    const char   *keys_hint;  // footer text
};

void        view_state_init(struct view_state *vs);
const char *sort_key_name(enum sort_key key);

// Rows left for the process list on a terminal of `rows` lines.
usize render_visible_rows(u16 rows);

// Fill `order` (vector of const struct process *) in display order.
bool render_order(struct vector *order, const struct snapshot *snap,
                  enum sort_key key, bool reverse);

// Full-screen frame with cursor addressing; replaces `out`'s contents.
// Clamps vs->scroll to the available range.
void render_screen(struct buffer *out, const struct snapshot *snap,
                   struct view_state *vs, const struct vector *order,
                   u16 rows, u16 cols);

// Plain text for pipes and batch mode; appends to `out`.
// max_rows == 0 means every process.
void render_plain(struct buffer *out, const struct snapshot *snap,
                  const struct view_state *vs, const struct vector *order,
                  usize max_rows);

#endif // WPROC_UI_RENDER_H

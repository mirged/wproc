#include "core/memory.h"

void mem_usage_compute(const struct procfs_meminfo *in, struct mem_usage *out) {
    u64 available = in->available_kb;
    if (!in->has_available) {
        // Pre-3.14 estimate: what the kernel could drop without swapping.
        available = in->free_kb + in->buffers_kb + in->cached_kb + in->sreclaimable_kb;
    }
    available = MIN(available, in->total_kb);

    out->total_kb      = in->total_kb;
    out->available_kb  = available;
    out->free_kb       = MIN(in->free_kb, available);
    out->used_kb       = in->total_kb - available;
    out->cached_kb     = in->cached_kb + in->sreclaimable_kb;
    out->buffers_kb    = in->buffers_kb;
    out->swap_total_kb = in->swap_total_kb;
    out->swap_used_kb  = in->swap_total_kb > in->swap_free_kb
                       ? in->swap_total_kb - in->swap_free_kb : 0;
}

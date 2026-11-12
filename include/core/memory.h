#ifndef WPROC_CORE_MEMORY_H
#define WPROC_CORE_MEMORY_H

#include "util/types.h"
#include "platform/procfs.h"

struct mem_usage {
    u64 total_kb;
    u64 used_kb;        // total - available
    u64 available_kb;
    u64 free_kb;        // completely unused
    u64 cached_kb;      // page cache + reclaimable slab
    u64 buffers_kb;
    u64 swap_total_kb;
    u64 swap_used_kb;
};

void mem_usage_compute(const struct procfs_meminfo *in, struct mem_usage *out);

static inline u32 mem_permille(u64 part, u64 whole) {
    return whole ? (u32)MIN(part * 1000 / whole, 1000UL) : 0;
}

#endif // WPROC_CORE_MEMORY_H

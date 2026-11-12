#ifndef WPROC_UTIL_ALLOC_H
#define WPROC_UTIL_ALLOC_H

#include "util/types.h"

// Page-granular allocator backed directly by mmap.
//
// Every allocation is its own mapping, so this is only suitable for a
// small number of long-lived, growable blocks (vectors, buffers) — which
// is exactly how wproc uses it. Small objects live inline in those blocks.
//
// mem_alloc returns zeroed memory. mem_realloc preserves contents up to the
// smaller of the two sizes; bytes past the old size are NOT guaranteed zero.

void *mem_alloc(usize size);
void *mem_realloc(void *ptr, usize size);
void  mem_free(void *ptr);

#endif // WPROC_UTIL_ALLOC_H

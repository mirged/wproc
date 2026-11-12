#include "util/alloc.h"
#include "platform/linux.h"

// The allocator is the one utility that has to talk to the kernel.
// It only uses the header-only syscall wrappers, nothing else from platform/.

#define ALLOC_PAGE 4096UL

// 16 bytes, so user pointers keep 16-byte alignment.
struct alloc_header {
    usize mapped;  // total bytes in the mapping, header included
    usize size;    // bytes requested by the caller
};

static usize round_to_page(usize n) {
    return (n + ALLOC_PAGE - 1) & ~(ALLOC_PAGE - 1);
}

static struct alloc_header *header_of(void *ptr) {
    return (struct alloc_header *)ptr - 1;
}

void *mem_alloc(usize size) {
    if (size > (usize)-1 / 2) return NULL;

    usize total = round_to_page(size + sizeof(struct alloc_header));
    void *p = sys_mmap(NULL, total, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (sys_ptr_failed(p)) return NULL;

    struct alloc_header *h = p;
    h->mapped = total;
    h->size = size;
    return h + 1;
}

void *mem_realloc(void *ptr, usize size) {
    if (!ptr) return mem_alloc(size);
    if (size > (usize)-1 / 2) return NULL;

    struct alloc_header *h = header_of(ptr);
    usize needed = size + sizeof(struct alloc_header);

    if (needed <= h->mapped) {
        h->size = size;
        return ptr;
    }

    // Let the kernel move the pages instead of copying them ourselves.
    usize total = round_to_page(needed);
    void *p = sys_mremap(h, h->mapped, total, MREMAP_MAYMOVE);
    if (sys_ptr_failed(p)) return NULL;

    h = p;
    h->mapped = total;
    h->size = size;
    return h + 1;
}

void mem_free(void *ptr) {
    if (!ptr) return;
    struct alloc_header *h = header_of(ptr);
    sys_munmap(h, h->mapped);
}

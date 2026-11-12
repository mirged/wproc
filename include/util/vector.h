#ifndef WPROC_UTIL_VECTOR_H
#define WPROC_UTIL_VECTOR_H

#include "util/types.h"

// Type-erased dynamic array of fixed-size elements.
// Pointers returned by vec_push / vec_insert / vec_at are invalidated by
// any later operation that may grow the vector.
struct vector {
    u8   *data;
    usize len;
    usize cap;
    usize elem_size;
};

typedef i32 (*vec_cmp_fn)(const void *a, const void *b, void *ctx);

void  vec_init(struct vector *v, usize elem_size);
void  vec_free(struct vector *v);
void  vec_clear(struct vector *v);
bool  vec_reserve(struct vector *v, usize count);

// Append / insert a zeroed element and return it (NULL on OOM).
void *vec_push(struct vector *v);
void *vec_insert(struct vector *v, usize index);

void  vec_remove(struct vector *v, usize index);
void  vec_truncate(struct vector *v, usize len);

void *vec_at(const struct vector *v, usize index);

// In-place heapsort. Not stable: break ties in the comparator.
void  vec_sort(struct vector *v, vec_cmp_fn cmp, void *ctx);

#endif // WPROC_UTIL_VECTOR_H

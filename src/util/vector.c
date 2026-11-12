#include "util/vector.h"
#include "util/alloc.h"
#include "util/string.h"

#define VEC_MIN_CAP 16

void vec_init(struct vector *v, usize elem_size) {
    v->data = NULL;
    v->len = 0;
    v->cap = 0;
    v->elem_size = elem_size;
}

void vec_free(struct vector *v) {
    mem_free(v->data);
    vec_init(v, v->elem_size);
}

void vec_clear(struct vector *v) {
    v->len = 0;
}

bool vec_reserve(struct vector *v, usize count) {
    if (count <= v->cap) return true;

    usize cap = v->cap ? v->cap : VEC_MIN_CAP;
    while (cap < count) {
        if (cap > (usize)-1 / 2) return false;
        cap *= 2;
    }
    if (cap > (usize)-1 / v->elem_size) return false;

    void *p = mem_realloc(v->data, cap * v->elem_size);
    if (!p) return false;
    v->data = p;
    v->cap = cap;
    return true;
}

static u8 *slot(const struct vector *v, usize index) {
    return v->data + index * v->elem_size;
}

void *vec_push(struct vector *v) {
    return vec_insert(v, v->len);
}

void *vec_insert(struct vector *v, usize index) {
    if (index > v->len) return NULL;
    if (!vec_reserve(v, v->len + 1)) return NULL;

    u8 *at = slot(v, index);
    mem_move(at + v->elem_size, at, (v->len - index) * v->elem_size);
    mem_set(at, 0, v->elem_size);
    v->len++;
    return at;
}

void vec_remove(struct vector *v, usize index) {
    if (index >= v->len) return;
    u8 *at = slot(v, index);
    mem_move(at, at + v->elem_size, (v->len - index - 1) * v->elem_size);
    v->len--;
}

void vec_truncate(struct vector *v, usize len) {
    if (len < v->len) v->len = len;
}

void *vec_at(const struct vector *v, usize index) {
    return index < v->len ? slot(v, index) : NULL;
}

static void swap_bytes(u8 *a, u8 *b, usize n) {
    while (n--) {
        u8 t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

static void sift_down(struct vector *v, usize root, usize end, vec_cmp_fn cmp, void *ctx) {
    for (;;) {
        usize child = 2 * root + 1;
        if (child >= end) return;
        if (child + 1 < end && cmp(slot(v, child), slot(v, child + 1), ctx) < 0) child++;
        if (cmp(slot(v, root), slot(v, child), ctx) >= 0) return;
        swap_bytes(slot(v, root), slot(v, child), v->elem_size);
        root = child;
    }
}

void vec_sort(struct vector *v, vec_cmp_fn cmp, void *ctx) {
    usize n = v->len;
    if (n < 2) return;

    for (usize i = n / 2; i-- > 0;) sift_down(v, i, n, cmp, ctx);
    for (usize end = n - 1; end > 0; end--) {
        swap_bytes(slot(v, 0), slot(v, end), v->elem_size);
        sift_down(v, 0, end, cmp, ctx);
    }
}

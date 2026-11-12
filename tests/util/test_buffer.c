#include "test.h"
#include "util/vector.h"

TEST(test_append_and_grow) {
    struct buffer b;
    buf_init(&b);
    buf_append_str(&b, "pid ");
    buf_append_u64(&b, 1234);
    buf_append_char(&b, ' ');
    buf_append_i64(&b, -5);
    CHECK_STR(buf_cstr(&b), "pid 1234 -5");

    // Well past the initial capacity.
    for (usize i = 0; i < 10000; i++) buf_append_char(&b, 'x');
    CHECK(b.len == 11 + 10000);
    CHECK(b.cap > b.len);
    CHECK(!b.failed);
    CHECK(b.data[11] == 'x' && b.data[b.len - 1] == 'x');

    buf_clear(&b);
    CHECK(b.len == 0);
    CHECK_STR(buf_cstr(&b), "");
    buf_free(&b);
    CHECK(b.data == NULL);
}

TEST(test_repeat_and_patch) {
    struct buffer b;
    buf_init(&b);
    buf_append_repeat(&b, '-', 5);
    CHECK_STR(buf_cstr(&b), "-----");
    CHECK(buf_patch(&b, 1, "ab", 2));
    CHECK_STR(buf_cstr(&b), "-ab--");
    CHECK(!buf_patch(&b, 4, "ab", 2));   // would run past the end
    CHECK(!buf_patch(&b, 6, "a", 1));
    buf_free(&b);
}

TEST(test_vector_push_insert_remove) {
    struct vector v;
    vec_init(&v, sizeof(u32));
    for (u32 i = 0; i < 1000; i++) *(u32 *)vec_push(&v) = i;
    CHECK(v.len == 1000);
    CHECK(*(u32 *)vec_at(&v, 999) == 999);
    CHECK(vec_at(&v, 1000) == NULL);

    u32 *front = vec_insert(&v, 0);
    CHECK(front && *front == 0);   // inserted slots are zeroed
    *front = 7777;
    CHECK(*(u32 *)vec_at(&v, 1) == 0);
    CHECK(*(u32 *)vec_at(&v, 1000) == 999);

    vec_remove(&v, 0);
    CHECK(*(u32 *)vec_at(&v, 0) == 0);
    CHECK(v.len == 1000);

    vec_truncate(&v, 10);
    CHECK(v.len == 10);
    vec_free(&v);
}

static i32 cmp_u32(const void *a, const void *b, void *ctx) {
    (void)ctx;
    u32 x = *(const u32 *)a, y = *(const u32 *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

TEST(test_vector_sort) {
    struct vector v;
    vec_init(&v, sizeof(u32));
    u32 seed = 12345;
    for (usize i = 0; i < 500; i++) {
        seed = seed * 1103515245u + 12345u;
        *(u32 *)vec_push(&v) = seed % 1000;
    }
    vec_sort(&v, cmp_u32, NULL);

    bool sorted = true;
    for (usize i = 1; i < v.len; i++) {
        if (*(u32 *)vec_at(&v, i - 1) > *(u32 *)vec_at(&v, i)) sorted = false;
    }
    CHECK(sorted);
    CHECK(v.len == 500);

    vec_clear(&v);
    vec_sort(&v, cmp_u32, NULL);   // empty is fine
    *(u32 *)vec_push(&v) = 1;
    vec_sort(&v, cmp_u32, NULL);   // one element is fine
    CHECK(*(u32 *)vec_at(&v, 0) == 1);
    vec_free(&v);
}

TEST_MAIN("util/buffer+vector",
    T(test_append_and_grow),
    T(test_repeat_and_patch),
    T(test_vector_push_insert_remove),
    T(test_vector_sort))

#ifndef WPROC_TEST_H
#define WPROC_TEST_H

// A test framework in one header, because the alternative was a dependency.
//
//   TEST(test_something) { CHECK(1 + 1 == 2); }
//   TEST_MAIN("suite", T(test_something))

#include "wproc.h"
#include "platform/file.h"
#include "util/buffer.h"
#include "util/string.h"

static u32 t_checks;
static u32 t_failures;

static void t_fail(const char *file, u32 line, const char *expr) {
    struct buffer b;
    buf_init(&b);
    buf_append_str(&b, "  FAIL ");
    buf_append_str(&b, file);
    buf_append_char(&b, ':');
    buf_append_u64(&b, line);
    buf_append_str(&b, ": ");
    buf_append_str(&b, expr);
    buf_append_char(&b, '\n');
    file_write_all(2, b.data, b.len);
    buf_free(&b);
}

#define CHECK(cond)                                         \
    do {                                                    \
        t_checks++;                                         \
        if (!(cond)) {                                      \
            t_failures++;                                   \
            t_fail(__FILE__, __LINE__, #cond);              \
        }                                                   \
    } while (0)

#define CHECK_STR(a, b) CHECK(str_eq((a), (b)))

typedef void (*test_fn)(void);

struct test_case {
    const char *name;
    test_fn     fn;
};

#define TEST(name) static void name(void)
#define T(fn)      { #fn, fn }

static i32 t_run(const char *suite, const struct test_case *cases, usize n) {
    for (usize i = 0; i < n; i++) {
        u32 before = t_failures;
        cases[i].fn();
        if (t_failures != before) {
            file_write_str(2, "  ^ in ");
            file_write_str(2, cases[i].name);
            file_write_str(2, "\n");
        }
    }

    struct buffer b;
    buf_init(&b);
    buf_append_str(&b, t_failures ? "FAIL " : "ok   ");
    buf_append_str(&b, suite);
    buf_append_str(&b, ": ");
    buf_append_u64(&b, n);
    buf_append_str(&b, " tests, ");
    buf_append_u64(&b, t_checks);
    buf_append_str(&b, " checks");
    if (t_failures) {
        buf_append_str(&b, ", ");
        buf_append_u64(&b, t_failures);
        buf_append_str(&b, " failed");
    }
    buf_append_char(&b, '\n');
    file_write_all(1, b.data, b.len);
    buf_free(&b);
    return t_failures ? 1 : 0;
}

#define TEST_MAIN(suite, ...)                                           \
    i32 wproc_main(const struct wproc_startup *startup) {               \
        (void)startup;                                                  \
        static const struct test_case cases[] = { __VA_ARGS__ };        \
        return t_run(suite, cases, ARRAY_LEN(cases));                   \
    }

#endif // WPROC_TEST_H

#include "test.h"
#include "util/parse.h"
#include "util/time.h"

static struct cursor cur(const char *s) {
    struct cursor c;
    cur_init(&c, s, str_len(s));
    return c;
}

TEST(test_u64_lines) {
    struct cursor c = cur("  123\t456\n789");
    u64 v;
    CHECK(cur_u64(&c, &v) && v == 123);
    CHECK(cur_u64(&c, &v) && v == 456);
    CHECK(!cur_u64(&c, &v));          // newlines are not whitespace
    cur_skip_line(&c);
    CHECK(cur_u64(&c, &v) && v == 789);
    CHECK(cur_eof(&c));
}

TEST(test_u64_limits) {
    struct cursor c = cur("18446744073709551615");
    u64 v;
    CHECK(cur_u64(&c, &v) && v == U64_MAX);
    c = cur("18446744073709551616");
    CHECK(!cur_u64(&c, &v));
    c = cur("x1");
    CHECK(!cur_u64(&c, &v));
}

TEST(test_i64) {
    i64 v;
    struct cursor c = cur("-5 17 -9223372036854775808 9223372036854775808");
    CHECK(cur_i64(&c, &v) && v == -5);
    CHECK(cur_i64(&c, &v) && v == 17);
    CHECK(cur_i64(&c, &v) && v == -I64_MAX - 1);
    CHECK(!cur_i64(&c, &v));
}

TEST(test_fixed) {
    u64 v;
    struct cursor c = cur("0.52 12 1.2345 3.5");
    CHECK(cur_fixed(&c, 2, &v) && v == 52);
    CHECK(cur_fixed(&c, 2, &v) && v == 1200);
    CHECK(cur_fixed(&c, 2, &v) && v == 123);
    CHECK(cur_fixed(&c, 2, &v) && v == 350);
}

TEST(test_words) {
    struct cursor c = cur("cpu  10 20\ncpu0 1");
    const char *w;
    usize n;
    CHECK(cur_word(&c, &w, &n) && str_eq_n(w, n, "cpu"));
    CHECK(cur_skip_fields(&c, 2));
    CHECK(!cur_skip_field(&c));
    cur_skip_line(&c);
    CHECK(cur_word(&c, &w, &n) && str_eq_n(w, n, "cpu0"));
    CHECK(cur_peek(&c) == ' ');
}

TEST(test_parse_whole) {
    u64 v;
    CHECK(parse_u64("123", &v) && v == 123);
    CHECK(!parse_u64("12a", &v));
    CHECK(!parse_u64("", &v));
    CHECK(!parse_u64("-1", &v));
}

static void check_fmt(usize (*fmt)(char *, u64), u64 v, const char *want) {
    char out[32];
    usize n = fmt(out, v);
    out[n] = '\0';
    CHECK_STR(out, want);
}

TEST(test_time_formatting) {
    check_fmt(time_format_uptime, 0, "00:00:00");
    check_fmt(time_format_uptime, 3661, "01:01:01");
    check_fmt(time_format_uptime, 3 * 86400 + 4 * 3600 + 12 * 60 + 9, "3d 04:12:09");
    check_fmt(time_format_cpu, 0, "0:00.00");
    check_fmt(time_format_cpu, 75430, "1:15.43");
    check_fmt(time_format_cpu, 123 * 60000 + 45670, "123:45.67");
}

TEST_MAIN("util/parse+time",
    T(test_u64_lines),
    T(test_u64_limits),
    T(test_i64),
    T(test_fixed),
    T(test_words),
    T(test_parse_whole),
    T(test_time_formatting))

#include "test.h"

TEST(test_len_and_compare) {
    CHECK(str_len("") == 0);
    CHECK(str_len("wproc") == 5);
    CHECK(str_eq("abc", "abc"));
    CHECK(!str_eq("abc", "abd"));
    CHECK(str_cmp("a", "b") < 0);
    CHECK(str_cmp("b", "a") > 0);
    CHECK(str_cmp("ab", "a") > 0);
    CHECK(str_starts_with("cpu12", "cpu"));
    CHECK(!str_starts_with("cp", "cpu"));
    CHECK(str_cmp_ascii_nocase("Bash", "bash") == 0);
    CHECK(str_cmp_ascii_nocase("apple", "Banana") < 0);
}

TEST(test_eq_n) {
    CHECK(str_eq_n("MemTotal: 1", 9, "MemTotal:"));
    CHECK(!str_eq_n("Mem", 3, "MemTotal:"));       // prefix is not equality
    CHECK(!str_eq_n("MemTotal:", 9, "Mem"));
    CHECK(str_eq_n("", 0, ""));
}

TEST(test_copy_truncates) {
    char buf[4];
    CHECK(str_copy(buf, sizeof(buf), "hello") == 3);
    CHECK_STR(buf, "hel");
    CHECK(str_copy(buf, sizeof(buf), "hi") == 2);
    CHECK_STR(buf, "hi");
    CHECK(str_copy(buf, 0, "nope") == 0);
}

TEST(test_mem_move_overlap) {
    char s[] = "abcdef";
    mem_move(s + 2, s, 4);
    CHECK(mem_cmp(s, "ababcd", 6) == 0);

    char t[] = "abcdef";
    mem_move(t, t + 2, 4);
    CHECK(mem_cmp(t, "cdefef", 6) == 0);
}

static void check_u64(u64 v, const char *want) {
    char out[24];
    usize n = str_from_u64(out, v);
    out[n] = '\0';
    CHECK_STR(out, want);
}

static void check_i64(i64 v, const char *want) {
    char out[24];
    usize n = str_from_i64(out, v);
    out[n] = '\0';
    CHECK_STR(out, want);
}

static void check_fixed(u64 v, u32 digits, const char *want) {
    char out[24];
    usize n = str_from_fixed(out, v, digits);
    out[n] = '\0';
    CHECK_STR(out, want);
}

TEST(test_number_formatting) {
    check_u64(0, "0");
    check_u64(42, "42");
    check_u64(U64_MAX, "18446744073709551615");
    check_i64(-42, "-42");
    check_i64(-I64_MAX - 1, "-9223372036854775808");
    check_fixed(234, 1, "23.4");
    check_fixed(5, 2, "0.05");
    check_fixed(1000, 1, "100.0");
    check_fixed(7, 0, "7");
}

TEST_MAIN("util/string",
    T(test_len_and_compare),
    T(test_eq_n),
    T(test_copy_truncates),
    T(test_mem_move_overlap),
    T(test_number_formatting))

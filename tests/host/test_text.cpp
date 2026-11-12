// Host unit tests for libk text helpers.
//
// These cover the parsing and comparison primitives that used to be
// hand-rolled inside shell/shell.cpp. Each helper here replaced a source of
// duplicated, subtly different argument parsing (defect D24).

#include <cstdio>
#include <cstring>
#include <cstdint>

#include <tinyos/libk/text.hpp>

namespace
{
    int g_failures = 0;
    int g_checks = 0;

    void check(bool condition, const char* description)
    {
        ++g_checks;
        if (!condition)
        {
            ++g_failures;
            std::printf("  FAIL  %s\n", description);
        }
    }

    void test_length_and_compare()
    {
        check(tinyos::libk::text::length("") == 0, "empty length");
        check(tinyos::libk::text::length("abc") == 3, "simple length");
        check(tinyos::libk::text::length(nullptr) == 0, "null length is zero");

        check(tinyos::libk::text::compare("abc", "abc") == 0, "equal compare");
        check(tinyos::libk::text::compare("abc", "abd") < 0, "less compare");
        check(tinyos::libk::text::compare("abd", "abc") > 0, "greater compare");
        check(tinyos::libk::text::compare("ab", "abc") < 0, "prefix compare");
        check(tinyos::libk::text::compare(nullptr, nullptr) == 0, "null equals null");
        check(tinyos::libk::text::compare(nullptr, "a") < 0, "null sorts first");

        check(tinyos::libk::text::equals("x", "x"), "equals true");
        check(!tinyos::libk::text::equals("x", "y"), "equals false");
        check(!tinyos::libk::text::equals(nullptr, "x"), "equals null false");

        check(tinyos::libk::text::compare_n("abcdef", "abcxyz", 3) == 0, "compare_n prefix");
        check(tinyos::libk::text::compare_n("abcdef", "abcxyz", 4) != 0, "compare_n divergence");
        check(tinyos::libk::text::compare_n("abc", "abc", 10) == 0, "compare_n stops at NUL");
    }

    void test_prefix_suffix_find()
    {
        check(tinyos::libk::text::starts_with("hello", "he"), "starts_with yes");
        check(!tinyos::libk::text::starts_with("hello", "lo"), "starts_with no");
        check(tinyos::libk::text::starts_with("hello", ""), "starts_with empty");
        check(!tinyos::libk::text::starts_with(nullptr, "a"), "starts_with null");

        check(tinyos::libk::text::ends_with("hello", "lo"), "ends_with yes");
        check(!tinyos::libk::text::ends_with("hello", "he"), "ends_with no");
        check(tinyos::libk::text::ends_with("hello", "hello"), "ends_with whole");
        check(!tinyos::libk::text::ends_with("hi", "hello"), "ends_with too long");

        const char* found = tinyos::libk::text::find("a=b=c", "=");
        check(found != nullptr && found[0] == '=', "find first");
        check(tinyos::libk::text::find("abc", "z") == nullptr, "find missing");
        check(tinyos::libk::text::find("abc", "") != nullptr, "find empty needle");
    }

    void test_whitespace()
    {
        const char* skipped = tinyos::libk::text::skip_spaces("  \t ab");
        check(skipped != nullptr && skipped[0] == 'a', "skip_spaces mixed");

        char buffer[16];
        std::strcpy(buffer, "value   ");
        const size_t trimmed = tinyos::libk::text::trim_right(buffer);
        check(trimmed == 5 && std::strcmp(buffer, "value") == 0, "trim_right");

        char empty[4];
        std::strcpy(empty, "   ");
        check(tinyos::libk::text::trim_right(empty) == 0 && empty[0] == '\0', "trim_right all spaces");
    }

    void test_constant_time_compare()
    {
        const unsigned char left[4] = { 1, 2, 3, 4 };
        const unsigned char same[4] = { 1, 2, 3, 4 };
        const unsigned char early[4] = { 9, 2, 3, 4 };
        const unsigned char late[4] = { 1, 2, 3, 9 };

        check(tinyos::libk::text::equals_constant_time(left, same, 4), "constant time equal");
        check(!tinyos::libk::text::equals_constant_time(left, early, 4), "constant time early diff");
        check(!tinyos::libk::text::equals_constant_time(left, late, 4), "constant time late diff");
        check(tinyos::libk::text::equals_constant_time(left, same, 0), "constant time zero length");
        check(!tinyos::libk::text::equals_constant_time(nullptr, same, 4), "constant time null");
    }

    void test_parse_uint64()
    {
        uint64_t value = 0;

        check(tinyos::libk::text::parse_uint64("123", value, true) && value == 123, "parse uint simple");
        check(tinyos::libk::text::parse_uint64("0", value, true) && value == 0, "parse uint zero");
        check(tinyos::libk::text::parse_uint64("  42  ", value, true) && value == 42, "parse uint spaces");
        check(tinyos::libk::text::parse_uint64("+7", value, true) && value == 7, "parse uint plus");

        check(!tinyos::libk::text::parse_uint64("", value, true), "parse uint empty");
        check(!tinyos::libk::text::parse_uint64("-1", value, true), "parse uint negative");
        check(!tinyos::libk::text::parse_uint64("12a", value, true), "parse uint trailing strict");
        check(tinyos::libk::text::parse_uint64("12a", value, false), "parse uint trailing loose");

        // Overflow must be rejected rather than wrapping.
        check(!tinyos::libk::text::parse_uint64("18446744073709551616", value, true), "parse uint overflow");
        check(tinyos::libk::text::parse_uint64("18446744073709551615", value, true), "parse uint max");
    }

    void test_parse_int64()
    {
        int64_t value = 0;

        check(tinyos::libk::text::parse_int64("-5", value, true) && value == -5, "parse int negative");
        check(tinyos::libk::text::parse_int64("+5", value, true) && value == 5, "parse int plus");
        check(tinyos::libk::text::parse_int64("0", value, true) && value == 0, "parse int zero");
        check(tinyos::libk::text::parse_int64("-9223372036854775808", value, true) &&
            value == INT64_MIN, "parse int min");
        check(tinyos::libk::text::parse_int64("9223372036854775807", value, true) &&
            value == INT64_MAX, "parse int max");

        check(!tinyos::libk::text::parse_int64("9223372036854775808", value, true), "parse int overflow");
        check(!tinyos::libk::text::parse_int64("-9223372036854775809", value, true), "parse int underflow");
        check(!tinyos::libk::text::parse_int64("-", value, true), "parse int bare sign");
        check(!tinyos::libk::text::parse_int64("abc", value, true), "parse int text");
    }

    void test_parse_octal()
    {
        uint16_t mode = 0;

        check(tinyos::libk::text::parse_octal("0644", mode) && mode == 0644, "octal 0644");
        check(tinyos::libk::text::parse_octal("755", mode) && mode == 0755, "octal 755");
        check(tinyos::libk::text::parse_octal("0", mode) && mode == 0, "octal zero");
        check(tinyos::libk::text::parse_octal("777", mode) && mode == 0777, "octal 777");

        check(!tinyos::libk::text::parse_octal("8", mode), "octal digit 8 rejected");
        check(!tinyos::libk::text::parse_octal("0789", mode), "octal invalid tail rejected");
        check(!tinyos::libk::text::parse_octal("", mode), "octal empty rejected");
        check(!tinyos::libk::text::parse_octal("777777", mode), "octal overflow rejected");
    }

    void test_split()
    {
        const char* begins[8] = {};
        size_t lengths[8] = {};

        size_t parts = tinyos::libk::text::split("a:b:c", ':', begins, lengths, 8);
        check(parts == 3, "split three parts");
        check(lengths[0] == 1 && begins[0][0] == 'a', "split part 0");
        check(lengths[1] == 1 && begins[1][0] == 'b', "split part 1");
        check(lengths[2] == 1 && begins[2][0] == 'c', "split part 2");

        parts = tinyos::libk::text::split("a::b", ':', begins, lengths, 8);
        check(parts == 3 && lengths[1] == 0, "split keeps empty middle");

        parts = tinyos::libk::text::split("", ':', begins, lengths, 8);
        check(parts == 1 && lengths[0] == 0, "split empty yields one empty part");

        parts = tinyos::libk::text::split("a:b:c:d", ':', begins, lengths, 2);
        check(parts == 2, "split respects max_parts");
    }
}

int main()
{
    test_length_and_compare();
    test_prefix_suffix_find();
    test_whitespace();
    test_constant_time_compare();
    test_parse_uint64();
    test_parse_int64();
    test_parse_octal();
    test_split();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

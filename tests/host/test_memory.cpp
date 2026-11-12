// Host unit tests for the safe copy helpers.
//
// The interesting cases are the ones that used to fail silently: a null source
// dereference (D11) and a truncated copy that the caller could not detect
// (D12).

#include <cstdio>
#include <cstring>

#include <tinyos/core/memory.hpp>

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

    void test_null_source()
    {
        char buffer[8];
        std::memset(buffer, 'x', sizeof(buffer));

        // Must not fault, and must produce an empty string (D11).
        const size_t written = tinyos::core::memory::string_copy_safe(buffer, sizeof(buffer), nullptr);
        check(written == 0, "null source writes zero");
        check(buffer[0] == '\0', "null source yields empty string");

        // A null source is tolerated and produces an empty string, matching
        // string_copy_safe() above and removing the D11 fault class entirely.
        check(tinyos::core::memory::string_copy_exact(buffer, sizeof(buffer), nullptr), "null exact tolerated");
        check(buffer[0] == '\0', "null exact yields empty string");
    }

    void test_truncation_is_visible()
    {
        char small[4];
        const char* long_source = "abcdefgh";

        // copy_safe truncates and reports how much landed.
        const size_t copied = tinyos::core::memory::copy_safe(small, sizeof(small), long_source, 8);
        check(copied == sizeof(small), "copy_safe reports truncation count");

        // copy_exact refuses and leaves the buffer alone (D12).
        char untouched[4];
        std::memset(untouched, 'z', sizeof(untouched));
        check(!tinyos::core::memory::copy_exact(untouched, sizeof(untouched), long_source, 8),
            "copy_exact rejects too-small buffer");
        check(untouched[0] == 'z', "copy_exact leaves buffer untouched");

        check(tinyos::core::memory::copy_exact(untouched, sizeof(untouched), "abc", 3),
            "copy_exact accepts fitting data");
    }

    void test_string_copy_exact()
    {
        char buffer[8];

        check(tinyos::core::memory::string_copy_exact(buffer, sizeof(buffer), "abc"), "fits exactly");
        check(std::strcmp(buffer, "abc") == 0, "content correct");

        // Seven characters plus a terminator do not fit in eight bytes, but six
        // do.
        check(!tinyos::core::memory::string_copy_exact(buffer, sizeof(buffer), "abcdefgh"),
            "overflow rejected");
        check(buffer[0] == '\0', "overflow clears destination");

        check(tinyos::core::memory::string_copy_exact(buffer, sizeof(buffer), "abcdefg"),
            "seven fits in eight");
    }

    void test_zero_sizes()
    {
        char buffer[4];
        check(tinyos::core::memory::string_copy_safe(buffer, 0, "abc") == 0, "zero destination size");
        check(tinyos::core::memory::copy_safe(buffer, sizeof(buffer), "abc", 0) == 0, "zero count copy");
        check(tinyos::core::memory::copy_exact(buffer, sizeof(buffer), "abc", 0), "zero count exact");
        check(!tinyos::core::memory::string_copy_exact(buffer, 0, "abc"), "zero size exact string");
    }

    void test_set()
    {
        unsigned char buffer[4] = { 0, 0, 0, 0 };
        tinyos::core::memory::set(buffer, 0xAB, sizeof(buffer));
        check(buffer[0] == 0xAB && buffer[3] == 0xAB, "set fills");
        check(tinyos::core::memory::set(nullptr, 0, 4) == nullptr, "set null tolerated");
    }
}

int main()
{
    test_null_source();
    test_truncation_is_visible();
    test_string_copy_exact();
    test_zero_sizes();
    test_set();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

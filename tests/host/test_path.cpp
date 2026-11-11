// Host unit tests for the canonical VFS path rules.
//
// These compile libk/path.cpp for the host, without the kernel, so a
// security-relevant validator can be tested in milliseconds instead of by
// booting QEMU and typing at a shell.
//
// Build and run:  make test-host

#include <cstdio>
#include <cstring>

#include <tinyos/libk/path.hpp>

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

    void check_valid(const char* path)
    {
        check(tinyos::libk::path::is_valid(path), path);
    }

    void check_invalid(const char* path, const char* description)
    {
        check(!tinyos::libk::path::is_valid(path), description);
    }

    void test_accepted_paths()
    {
        std::printf("accepted paths\n");
        check_valid("/");
        check_valid("/users");
        check_valid("/users/notes.txt");
        check_valid("/volumes/ram-block0/volume.txt");
        check_valid("/system/profile.txt");
        check_valid("/boot/initrd-placeholder");
        check_valid("/a-b_c.d");
    }

    void test_rejected_paths()
    {
        std::printf("rejected paths\n");

        check(!tinyos::libk::path::is_valid(nullptr), "null pointer");
        check_invalid("", "empty string");
        check_invalid("users/notes.txt", "relative path");
        check_invalid("/users/../system/tapp.txt", "dot-dot segment");
        check_invalid("/users/./notes.txt", "dot segment");
        check_invalid("/users//notes.txt", "repeated separator");
        check_invalid("/users/notes.txt/", "trailing separator");
        check_invalid("/users/notes.txt;reboot", "shell metacharacter");
        check_invalid("/users/notes.txt $(reboot)", "embedded command substitution");
        check_invalid("/users/notes.txt\n", "embedded newline");
        check_invalid("/users/notes\t.txt", "embedded tab");
        check_invalid("/users/notes.txt|cat", "pipe");
        check_invalid("/users/notes&.txt", "ampersand");
        check_invalid("/users/../", "dot-dot only");
        check_invalid("/users/..", "trailing dot-dot");
    }

    void test_length_limits()
    {
        std::printf("length limits\n");

        // A segment exactly at the limit must be accepted; one byte more must not.
        char at_limit[tinyos::libk::path::MaxSegmentBytes + 3];
        at_limit[0] = '/';
        for (size_t index = 0; index < tinyos::libk::path::MaxSegmentBytes; ++index)
        {
            at_limit[index + 1] = 'a';
        }
        at_limit[tinyos::libk::path::MaxSegmentBytes + 1] = '\0';
        check(tinyos::libk::path::is_valid(at_limit), "segment at the limit");

        char over_limit[tinyos::libk::path::MaxSegmentBytes + 4];
        over_limit[0] = '/';
        for (size_t index = 0; index < tinyos::libk::path::MaxSegmentBytes + 1; ++index)
        {
            over_limit[index + 1] = 'a';
        }
        over_limit[tinyos::libk::path::MaxSegmentBytes + 2] = '\0';
        check(!tinyos::libk::path::is_valid(over_limit), "segment over the limit");

        // A whole path at the limit must still pass.
        char long_path[tinyos::libk::path::MaxPathBytes + 2];
        size_t position = 0;
        long_path[position++] = '/';
        while (position < tinyos::libk::path::MaxPathBytes - 1)
        {
            long_path[position++] = 'a';
        }
        long_path[position] = '\0';
        // This path exceeds the segment limit, so it must be rejected for that
        // reason even though the total length fits.
        check(!tinyos::libk::path::is_valid(long_path), "long single segment rejected");
    }

    void test_prefix_rules()
    {
        std::printf("prefix rules\n");

        check(tinyos::libk::path::has_prefix("/mnt/volume.txt", "/mnt"), "exact child");
        check(tinyos::libk::path::has_prefix("/mnt", "/mnt"), "exact prefix");
        check(!tinyos::libk::path::has_prefix("/mntx/volume.txt", "/mnt"), "prefix must end at a separator");
        check(!tinyos::libk::path::has_prefix("/mnt", "/mntx"), "shorter path is not a prefix match");
        check(!tinyos::libk::path::has_prefix("/mnt", ""), "empty prefix rejected");
        check(!tinyos::libk::path::has_prefix(nullptr, "/mnt"), "null path rejected");
    }

    void test_length_helper()
    {
        std::printf("length helper\n");
        check(tinyos::libk::path::length("/") == 1, "root length");
        check(tinyos::libk::path::length("/users") == 6, "path length");
        check(tinyos::libk::path::length(nullptr) == 0, "null length");
    }
}

int main()
{
    test_accepted_paths();
    test_rejected_paths();
    test_length_limits();
    test_prefix_rules();
    test_length_helper();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

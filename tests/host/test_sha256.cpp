// Host unit tests for SHA-256, HMAC-SHA256 and PBKDF2.
//
// The test vectors are the published NIST / RFC values. That matters: a hash
// implementation that is self-consistent but wrong passes every round-trip test
// and silently produces digests that no other system agrees with. These vectors
// are the only way to know the implementation is SHA-256 and not merely a
// deterministic 32-byte function.

#include <cstdio>
#include <cstring>

#include <tinyos/libk/sha256.hpp>

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

    void check_hash(const char* input, const char* expected_hex, const char* description)
    {
        uint8_t digest[tinyos::libk::sha256::DigestBytes];
        char hex[tinyos::libk::sha256::DigestBytes * 2 + 1];

        tinyos::libk::sha256::hash(input, std::strlen(input), digest);
        tinyos::libk::sha256::to_hex(digest, hex, sizeof(hex));

        const bool matches = std::strcmp(hex, expected_hex) == 0;
        check(matches, description);

        if (!matches)
        {
            std::printf("        expected %s\n", expected_hex);
            std::printf("        actual   %s\n", hex);
        }
    }

    // FIPS 180-4 / NIST published vectors.
    void test_known_vectors()
    {
        std::printf("known vectors\n");

        check_hash("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "empty input");

        check_hash("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "abc");

        check_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "448-bit message");
    }

    void test_streaming_matches_oneshot()
    {
        std::printf("streaming\n");

        const char* message = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        const size_t length = std::strlen(message);

        uint8_t one_shot[tinyos::libk::sha256::DigestBytes];
        tinyos::libk::sha256::hash(message, length, one_shot);

        // Feeding the same data in every possible chunking must give the same
        // digest; a bug in the partial-block path shows up here and nowhere else.
        for (size_t chunk = 1; chunk <= 17; ++chunk)
        {
            tinyos::libk::sha256::Context context;
            tinyos::libk::sha256::initialize(context);

            size_t offset = 0;
            while (offset < length)
            {
                const size_t take = (length - offset) < chunk ? (length - offset) : chunk;
                tinyos::libk::sha256::update(context, message + offset, take);
                offset += take;
            }

            uint8_t streamed[tinyos::libk::sha256::DigestBytes];
            tinyos::libk::sha256::finalize(context, streamed);

            check(tinyos::libk::sha256::equal_constant_time(one_shot, streamed,
                    tinyos::libk::sha256::DigestBytes),
                "streamed digest matches one-shot");
        }
    }

    void test_boundary_lengths()
    {
        std::printf("boundary lengths\n");

        // Block boundaries are where padding logic breaks. 55, 56, 63, 64 and 65
        // catch the "does the length fit before the block ends" decision.
        const size_t lengths[] = { 54, 55, 56, 57, 63, 64, 65, 127, 128, 129 };

        for (size_t index = 0; index < sizeof(lengths) / sizeof(lengths[0]); ++index)
        {
            const size_t length = lengths[index];

            char buffer[160];
            for (size_t position = 0; position < length; ++position)
            {
                buffer[position] = static_cast<char>('a' + (position % 26));
            }

            uint8_t one_shot[tinyos::libk::sha256::DigestBytes];
            tinyos::libk::sha256::hash(buffer, length, one_shot);

            // Same data, byte at a time.
            tinyos::libk::sha256::Context context;
            tinyos::libk::sha256::initialize(context);
            for (size_t position = 0; position < length; ++position)
            {
                tinyos::libk::sha256::update(context, buffer + position, 1);
            }

            uint8_t streamed[tinyos::libk::sha256::DigestBytes];
            tinyos::libk::sha256::finalize(context, streamed);

            check(tinyos::libk::sha256::equal_constant_time(one_shot, streamed,
                    tinyos::libk::sha256::DigestBytes),
                "byte-at-a-time matches one-shot at boundary length");
        }
    }

    void test_million_a()
    {
        std::printf("million a\n");

        // The long-message NIST vector: one million 'a' characters.
        tinyos::libk::sha256::Context context;
        tinyos::libk::sha256::initialize(context);

        char block[1000];
        std::memset(block, 'a', sizeof(block));

        for (size_t round = 0; round < 1000; ++round)
        {
            tinyos::libk::sha256::update(context, block, sizeof(block));
        }

        uint8_t digest[tinyos::libk::sha256::DigestBytes];
        tinyos::libk::sha256::finalize(context, digest);

        char hex[tinyos::libk::sha256::DigestBytes * 2 + 1];
        tinyos::libk::sha256::to_hex(digest, hex, sizeof(hex));

        check(std::strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0,
            "one million 'a' characters");
    }

    // RFC 4231 test vectors for HMAC-SHA256.
    void test_hmac_vectors()
    {
        std::printf("hmac vectors\n");

        {
            // Case 1: key = 0x0b x 20, data = "Hi There"
            uint8_t key[20];
            std::memset(key, 0x0b, sizeof(key));

            uint8_t digest[tinyos::libk::sha256::DigestBytes];
            tinyos::libk::sha256::hmac(key, sizeof(key), "Hi There", 8, digest);

            char hex[tinyos::libk::sha256::DigestBytes * 2 + 1];
            tinyos::libk::sha256::to_hex(digest, hex, sizeof(hex));
            check(std::strcmp(hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") == 0,
                "RFC 4231 case 1");
        }

        {
            // Case 2: key = "Jefe", data = "what do ya want for nothing?"
            uint8_t digest[tinyos::libk::sha256::DigestBytes];
            tinyos::libk::sha256::hmac("Jefe", 4, "what do ya want for nothing?", 28, digest);

            char hex[tinyos::libk::sha256::DigestBytes * 2 + 1];
            tinyos::libk::sha256::to_hex(digest, hex, sizeof(hex));
            check(std::strcmp(hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") == 0,
                "RFC 4231 case 2");
        }

        {
            // Case 3: long key (131 bytes), exercises the key-hashing path.
            uint8_t key[131];
            std::memset(key, 0xaa, sizeof(key));

            uint8_t digest[tinyos::libk::sha256::DigestBytes];
            tinyos::libk::sha256::hmac(key, sizeof(key), "Test Using Larger Than Block-Size Key - Hash Key First", 54,
                digest);

            char hex[tinyos::libk::sha256::DigestBytes * 2 + 1];
            tinyos::libk::sha256::to_hex(digest, hex, sizeof(hex));
            check(std::strcmp(hex, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54") == 0,
                "RFC 4231 case 6 (long key)");
        }
    }

    // Verified against Python's hashlib.pbkdf2_hmac (OpenSSL), so these are
    // independent of this implementation. A round-trip test alone would pass for
    // any deterministic function; only external vectors prove it is PBKDF2.
    void test_pbkdf2_vectors()
    {
        std::printf("pbkdf2 vectors\n");

        auto check_vector = [](const char* password, const char* salt, uint32_t iterations,
                               const char* expected, const char* description) {
            uint8_t output[32];
            if (!tinyos::libk::sha256::pbkdf2(password, std::strlen(password), salt, std::strlen(salt),
                    iterations, output, sizeof(output)))
            {
                check(false, description);
                return;
            }

            char hex[tinyos::libk::sha256::DigestBytes * 2 + 1];
            tinyos::libk::sha256::to_hex(output, hex, sizeof(hex));

            const bool matches = std::strcmp(hex, expected) == 0;
            check(matches, description);
            if (!matches)
            {
                std::printf("        expected %s\n", expected);
                std::printf("        actual   %s\n", hex);
            }
        };

        check_vector("passwd", "salt", 1000,
            "fa9c158e678bfe94189b0b1a707a0921672fdc9f11fa978805969df312c2c8ef",
            "passwd/salt/1000");

        check_vector("password", "salt", 1000,
            "632c2812e46d4604102ba7618e9d6d7d2f8128f6266b4a03264d2a0460b7dcb3",
            "password/salt/1000");

        check_vector("secret", "salt-one", 1000,
            "7a1fb962e437bd0a545baf8c59d108568bdc943b24f0aa290b3f66d1a8b43446",
            "secret/salt-one/1000");

        // Empty password and salt: the block-index path must still work.
        check_vector("", "", 1000,
            "4fc58a21c100ce1835b8f9991d738b56965d14b24e1761fbdffc69ac5e0b667a",
            "empty password and salt");

        // Iteration count must actually change the output.
        {
            uint8_t thousand[32];
            uint8_t two_thousand[32];
            tinyos::libk::sha256::pbkdf2("secret", 6, "salt-one", 8, 1000, thousand, sizeof(thousand));
            tinyos::libk::sha256::pbkdf2("secret", 6, "salt-one", 8, 2000, two_thousand, sizeof(two_thousand));
            check(!tinyos::libk::sha256::equal_constant_time(thousand, two_thousand, 32),
                "2000 iterations differs from 1000");
        }
    }

    void test_pbkdf2_properties()
    {
        std::printf("pbkdf2 properties\n");

        // A weak iteration count is refused rather than silently accepted.
        uint8_t output[32];
        check(!tinyos::libk::sha256::pbkdf2("passwd", 6, "salt", 4, 1, output, sizeof(output)),
            "iteration count below minimum refused");
        check(!tinyos::libk::sha256::pbkdf2("passwd", 6, "salt", 4, 999, output, sizeof(output)),
            "999 iterations refused");
        check(tinyos::libk::sha256::pbkdf2("passwd", 6, "salt", 4, 1000, output, sizeof(output)),
            "1000 iterations accepted");

        // Determinism and salt sensitivity are the properties the credential
        // store depends on.
        uint8_t first[32];
        uint8_t second[32];
        uint8_t different_salt[32];

        tinyos::libk::sha256::pbkdf2("secret", 6, "salt-one", 8, 1000, first, sizeof(first));
        tinyos::libk::sha256::pbkdf2("secret", 6, "salt-one", 8, 1000, second, sizeof(second));
        tinyos::libk::sha256::pbkdf2("secret", 6, "salt-two", 8, 1000, different_salt, sizeof(different_salt));

        check(tinyos::libk::sha256::equal_constant_time(first, second, 32), "pbkdf2 is deterministic");
        check(!tinyos::libk::sha256::equal_constant_time(first, different_salt, 32),
            "different salt gives a different hash");

        uint8_t other_password[32];
        tinyos::libk::sha256::pbkdf2("secreT", 6, "salt-one", 8, 1000, other_password, sizeof(other_password));
        check(!tinyos::libk::sha256::equal_constant_time(first, other_password, 32),
            "different password gives a different hash");

        // Null output is rejected.
        check(!tinyos::libk::sha256::pbkdf2("secret", 6, "salt", 4, 1000, nullptr, 32), "null output refused");
        check(!tinyos::libk::sha256::pbkdf2("secret", 6, "salt", 4, 1000, first, 0), "zero length refused");
    }

    void test_hex_output()
    {
        std::printf("hex output\n");

        const uint8_t digest[32] = {
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
            0x0f, 0x1f, 0x2f, 0x3f, 0x4f, 0x5f, 0x6f, 0x7f,
            0x8f, 0x9f, 0xaf, 0xbf, 0xcf, 0xdf, 0xef, 0xff
        };

        char hex[65];
        tinyos::libk::sha256::to_hex(digest, hex, sizeof(hex));
        check(std::strlen(hex) == 64, "hex is 64 characters");
        check(hex[0] == '0' && hex[1] == '0' && hex[2] == '1' && hex[3] == '1', "hex leading bytes");

        // A short buffer must truncate safely and stay terminated.
        char small[9];
        tinyos::libk::sha256::to_hex(digest, small, sizeof(small));
        check(std::strlen(small) == 8, "short buffer truncates to even length");
        check(small[8] == '\0', "short buffer is terminated");
    }

    void test_constant_time_compare()
    {
        const uint8_t left[4] = { 1, 2, 3, 4 };
        const uint8_t same[4] = { 1, 2, 3, 4 };
        const uint8_t late[4] = { 1, 2, 3, 9 };

        check(tinyos::libk::sha256::equal_constant_time(left, same, 4), "equal digests compare equal");
        check(!tinyos::libk::sha256::equal_constant_time(left, late, 4), "differing digests compare unequal");
        check(!tinyos::libk::sha256::equal_constant_time(nullptr, same, 4), "null rejected");
    }
}

int main()
{
    test_known_vectors();
    test_streaming_matches_oneshot();
    test_boundary_lengths();
    test_million_a();
    test_hmac_vectors();
    test_pbkdf2_vectors();
    test_pbkdf2_properties();
    test_hex_output();
    test_constant_time_compare();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

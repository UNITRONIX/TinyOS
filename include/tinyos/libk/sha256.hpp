#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::libk::sha256
{
    // SHA-256 (FIPS 180-4).
    //
    // Needed for two things that were previously impossible on the target:
    // hashing payloads to verify a package against its manifest, and deriving
    // password hashes with PBKDF2. Before this existed the whole crypto story
    // lived in scripts/tinyos-image.sh on the host, and
    // kernel/app/package_verifier.cpp only inspected enum flags — it never
    // computed a digest, so "verified" meant "a field said so".
    //
    // Pure functions with no kernel dependencies, so host tests can link them.

    inline constexpr size_t DigestBytes = 32;
    inline constexpr size_t BlockBytes = 64;

    // Streaming context. Kept a plain struct so it can live on the stack or in
    // static storage without allocation or a constructor.
    struct Context
    {
        uint32_t state[8];
        uint64_t total_bytes;
        uint8_t buffer[BlockBytes];
        size_t buffer_used;
    };

    void initialize(Context& context);

    // Feeds `length` bytes into the digest. A null pointer with a non-zero
    // length is rejected rather than dereferenced, matching the rest of libk.
    void update(Context& context, const void* data, size_t length);

    // Writes the 32-byte digest into `digest` and wipes the context. The context
    // must not be reused after this without calling initialize() again.
    void finalize(Context& context, uint8_t digest[DigestBytes]);

    // One-shot convenience wrapper.
    void hash(const void* data, size_t length, uint8_t digest[DigestBytes]);

    // Lowercase hex of a digest, NUL-terminated. Buffer must hold 65 bytes.
    void to_hex(const uint8_t digest[DigestBytes], char* output, size_t output_size);

    // HMAC-SHA256 (RFC 2104). Required by PBKDF2.
    void hmac(const void* key, size_t key_length, const void* data, size_t data_length,
        uint8_t digest[DigestBytes]);

    // PBKDF2-HMAC-SHA256 (RFC 8018).
    //
    // `iterations` must be at least MinPbkdf2Iterations. The function returns
    // false rather than silently using a weak parameter: a caller that asks for
    // a useless iteration count has a bug, and accepting it would produce
    // password hashes that look strong and are not.
    inline constexpr uint32_t MinPbkdf2Iterations = 1000;
    bool pbkdf2(const void* password, size_t password_length,
        const void* salt, size_t salt_length,
        uint32_t iterations,
        uint8_t* output, size_t output_length);

    // Compares two digests without an early exit, so a timing measurement cannot
    // reveal how many leading bytes matched.
    bool equal_constant_time(const uint8_t* left, const uint8_t* right, size_t length);
}

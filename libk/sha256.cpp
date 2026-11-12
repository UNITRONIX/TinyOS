#include <tinyos/core/memory.hpp>
#include <tinyos/libk/sha256.hpp>

namespace
{
    using tinyos::libk::sha256::BlockBytes;
    using tinyos::libk::sha256::Context;
    using tinyos::libk::sha256::DigestBytes;

    // Round constants: the first 32 bits of the fractional parts of the cube
    // roots of the first 64 primes (FIPS 180-4 section 4.2.2).
    constexpr uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };

    constexpr uint64_t MinPbkdf2Iterations = tinyos::libk::sha256::MinPbkdf2Iterations;

    // The value is referenced by pbkdf2() below; the alias exists so the
    // comparison reads as a limit rather than a magic number.
    static_assert(MinPbkdf2Iterations >= 1000, "PBKDF2 iteration floor must not be lowered silently");

    inline uint32_t rotate_right(uint32_t value, uint32_t count)
    {
        return (value >> count) | (value << (32 - count));
    }

    inline uint32_t choose(uint32_t x, uint32_t y, uint32_t z)
    {
        return (x & y) ^ (~x & z);
    }

    inline uint32_t majority(uint32_t x, uint32_t y, uint32_t z)
    {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    inline uint32_t big_sigma0(uint32_t x)
    {
        return rotate_right(x, 2) ^ rotate_right(x, 13) ^ rotate_right(x, 22);
    }

    inline uint32_t big_sigma1(uint32_t x)
    {
        return rotate_right(x, 6) ^ rotate_right(x, 11) ^ rotate_right(x, 25);
    }

    inline uint32_t small_sigma0(uint32_t x)
    {
        return rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3);
    }

    inline uint32_t small_sigma1(uint32_t x)
    {
        return rotate_right(x, 17) ^ rotate_right(x, 19) ^ (x >> 10);
    }

    // Processes one 64-byte block. Separated from update() so the padding and
    // finalisation paths share exactly one compression implementation.
    void compress(Context& context, const uint8_t block[BlockBytes])
    {
        uint32_t schedule[64];

        for (size_t index = 0; index < 16; ++index)
        {
            schedule[index] =
                (static_cast<uint32_t>(block[index * 4]) << 24) |
                (static_cast<uint32_t>(block[index * 4 + 1]) << 16) |
                (static_cast<uint32_t>(block[index * 4 + 2]) << 8) |
                (static_cast<uint32_t>(block[index * 4 + 3]));
        }

        for (size_t index = 16; index < 64; ++index)
        {
            schedule[index] = small_sigma1(schedule[index - 2]) + schedule[index - 7] +
                small_sigma0(schedule[index - 15]) + schedule[index - 16];
        }

        uint32_t a = context.state[0];
        uint32_t b = context.state[1];
        uint32_t c = context.state[2];
        uint32_t d = context.state[3];
        uint32_t e = context.state[4];
        uint32_t f = context.state[5];
        uint32_t g = context.state[6];
        uint32_t h = context.state[7];

        for (size_t index = 0; index < 64; ++index)
        {
            const uint32_t temp1 = h + big_sigma1(e) + choose(e, f, g) + K[index] + schedule[index];
            const uint32_t temp2 = big_sigma0(a) + majority(a, b, c);

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        context.state[0] += a;
        context.state[1] += b;
        context.state[2] += c;
        context.state[3] += d;
        context.state[4] += e;
        context.state[5] += f;
        context.state[6] += g;
        context.state[7] += h;
    }
}

namespace tinyos::libk::sha256
{
    void initialize(Context& context)
    {
        // Initial hash values: fractional parts of the square roots of the first
        // eight primes (FIPS 180-4 section 5.3.3).
        context.state[0] = 0x6a09e667u;
        context.state[1] = 0xbb67ae85u;
        context.state[2] = 0x3c6ef372u;
        context.state[3] = 0xa54ff53au;
        context.state[4] = 0x510e527fu;
        context.state[5] = 0x9b05688cu;
        context.state[6] = 0x1f83d9abu;
        context.state[7] = 0x5be0cd19u;
        context.total_bytes = 0;
        context.buffer_used = 0;

        for (size_t index = 0; index < BlockBytes; ++index)
        {
            context.buffer[index] = 0;
        }
    }

    void update(Context& context, const void* data, size_t length)
    {
        if (data == nullptr || length == 0)
        {
            return;
        }

        const auto* bytes = static_cast<const uint8_t*>(data);
        context.total_bytes += length;

        size_t offset = 0;

        // Fill a partial block first, if one is pending.
        if (context.buffer_used != 0)
        {
            const size_t space = BlockBytes - context.buffer_used;
            const size_t take = length < space ? length : space;

            for (size_t index = 0; index < take; ++index)
            {
                context.buffer[context.buffer_used + index] = bytes[index];
            }

            context.buffer_used += take;
            offset += take;

            if (context.buffer_used == BlockBytes)
            {
                compress(context, context.buffer);
                context.buffer_used = 0;
            }
        }

        // Compress whole blocks directly from the input, avoiding a copy.
        while (length - offset >= BlockBytes)
        {
            compress(context, bytes + offset);
            offset += BlockBytes;
        }

        // Buffer the remainder.
        for (size_t index = offset; index < length; ++index)
        {
            context.buffer[context.buffer_used] = bytes[index];
            ++context.buffer_used;
        }
    }

    void finalize(Context& context, uint8_t digest[DigestBytes])
    {
        if (digest == nullptr)
        {
            return;
        }

        const uint64_t bit_length = context.total_bytes * 8ull;

        // Append 0x80, then zeros, then the 64-bit big-endian length. Two blocks
        // are needed when the length does not fit in the remaining space.
        context.buffer[context.buffer_used] = 0x80;
        ++context.buffer_used;

        if (context.buffer_used > BlockBytes - 8)
        {
            while (context.buffer_used < BlockBytes)
            {
                context.buffer[context.buffer_used] = 0;
                ++context.buffer_used;
            }

            compress(context, context.buffer);
            context.buffer_used = 0;
        }

        while (context.buffer_used < BlockBytes - 8)
        {
            context.buffer[context.buffer_used] = 0;
            ++context.buffer_used;
        }

        for (size_t index = 0; index < 8; ++index)
        {
            context.buffer[BlockBytes - 8 + index] =
                static_cast<uint8_t>((bit_length >> (56 - index * 8)) & 0xFFu);
        }

        compress(context, context.buffer);

        for (size_t index = 0; index < 8; ++index)
        {
            digest[index * 4] = static_cast<uint8_t>((context.state[index] >> 24) & 0xFFu);
            digest[index * 4 + 1] = static_cast<uint8_t>((context.state[index] >> 16) & 0xFFu);
            digest[index * 4 + 2] = static_cast<uint8_t>((context.state[index] >> 8) & 0xFFu);
            digest[index * 4 + 3] = static_cast<uint8_t>(context.state[index] & 0xFFu);
        }

        // Wipe intermediate state so a stack dump does not leak key material in
        // the PBKDF2 path.
        tinyos::core::memory::set(&context, 0, sizeof(context));
    }

    void hash(const void* data, size_t length, uint8_t digest[DigestBytes])
    {
        Context context;
        initialize(context);
        update(context, data, length);
        finalize(context, digest);
    }

    void to_hex(const uint8_t digest[DigestBytes], char* output, size_t output_size)
    {
        if (digest == nullptr || output == nullptr || output_size == 0)
        {
            return;
        }

        constexpr char Digits[] = "0123456789abcdef";

        size_t written = 0;
        for (size_t index = 0; index < DigestBytes; ++index)
        {
            if (written + 2 >= output_size)
            {
                break;
            }

            output[written] = Digits[(digest[index] >> 4) & 0x0Fu];
            ++written;
            output[written] = Digits[digest[index] & 0x0Fu];
            ++written;
        }

        output[written] = '\0';
    }

    void hmac(const void* key, size_t key_length, const void* data, size_t data_length,
        uint8_t digest[DigestBytes])
    {
        if (digest == nullptr)
        {
            return;
        }

        uint8_t normalized[BlockBytes];

        // A key longer than the block size is hashed; a shorter one is padded
        // with zeros (RFC 2104).
        if (key != nullptr && key_length > BlockBytes)
        {
            hash(key, key_length, normalized);
            for (size_t index = DigestBytes; index < BlockBytes; ++index)
            {
                normalized[index] = 0;
            }
        }
        else
        {
            for (size_t index = 0; index < BlockBytes; ++index)
            {
                normalized[index] = (key != nullptr && index < key_length)
                    ? static_cast<const uint8_t*>(key)[index]
                    : 0;
            }
        }

        uint8_t inner_pad[BlockBytes];
        uint8_t outer_pad[BlockBytes];
        for (size_t index = 0; index < BlockBytes; ++index)
        {
            inner_pad[index] = static_cast<uint8_t>(normalized[index] ^ 0x36u);
            outer_pad[index] = static_cast<uint8_t>(normalized[index] ^ 0x5cu);
        }

        uint8_t inner_digest[DigestBytes];
        {
            Context context;
            initialize(context);
            update(context, inner_pad, BlockBytes);
            update(context, data, data_length);
            finalize(context, inner_digest);
        }

        {
            Context context;
            initialize(context);
            update(context, outer_pad, BlockBytes);
            update(context, inner_digest, DigestBytes);
            finalize(context, digest);
        }

        // Remove key material from the stack.
        tinyos::core::memory::set(normalized, 0, sizeof(normalized));
        tinyos::core::memory::set(inner_pad, 0, sizeof(inner_pad));
        tinyos::core::memory::set(outer_pad, 0, sizeof(outer_pad));
        tinyos::core::memory::set(inner_digest, 0, sizeof(inner_digest));
    }

    bool pbkdf2(const void* password, size_t password_length,
        const void* salt, size_t salt_length,
        uint32_t iterations,
        uint8_t* output, size_t output_length)
    {
        // A low iteration count produces a hash that looks strong and is not.
        // Refusing is the only honest response to a caller that asks for one.
        if (iterations < MinPbkdf2Iterations)
        {
            return false;
        }

        if (output == nullptr || output_length == 0)
        {
            return false;
        }

        uint8_t block[DigestBytes];
        uint8_t work[DigestBytes];

        uint32_t block_index = 1;
        size_t produced = 0;

        while (produced < output_length)
        {
            // U1 = HMAC(password, salt || INT_BE32(block_index))
            {
                uint8_t salt_block[128];
                if (salt_length + 4 > sizeof(salt_block))
                {
                    return false;
                }

                if (salt != nullptr && salt_length != 0)
                {
                    for (size_t index = 0; index < salt_length; ++index)
                    {
                        salt_block[index] = static_cast<const uint8_t*>(salt)[index];
                    }
                }

                salt_block[salt_length] = static_cast<uint8_t>((block_index >> 24) & 0xFFu);
                salt_block[salt_length + 1] = static_cast<uint8_t>((block_index >> 16) & 0xFFu);
                salt_block[salt_length + 2] = static_cast<uint8_t>((block_index >> 8) & 0xFFu);
                salt_block[salt_length + 3] = static_cast<uint8_t>(block_index & 0xFFu);

                hmac(password, password_length, salt_block, salt_length + 4, block);
                tinyos::core::memory::set(salt_block, 0, sizeof(salt_block));
            }

            for (size_t index = 0; index < DigestBytes; ++index)
            {
                work[index] = block[index];
            }

            // Un = HMAC(password, Un-1), XORed into the accumulator.
            for (uint32_t round = 1; round < iterations; ++round)
            {
                uint8_t next[DigestBytes];
                hmac(password, password_length, block, DigestBytes, next);

                for (size_t index = 0; index < DigestBytes; ++index)
                {
                    block[index] = next[index];
                    work[index] = static_cast<uint8_t>(work[index] ^ next[index]);
                }

                tinyos::core::memory::set(next, 0, sizeof(next));
            }

            const size_t take = (output_length - produced) < DigestBytes
                ? (output_length - produced)
                : DigestBytes;

            for (size_t index = 0; index < take; ++index)
            {
                output[produced + index] = work[index];
            }

            produced += take;
            ++block_index;
        }

        tinyos::core::memory::set(block, 0, sizeof(block));
        tinyos::core::memory::set(work, 0, sizeof(work));
        return true;
    }

    bool equal_constant_time(const uint8_t* left, const uint8_t* right, size_t length)
    {
        if (left == nullptr || right == nullptr)
        {
            return false;
        }

        uint8_t difference = 0;
        for (size_t index = 0; index < length; ++index)
        {
            difference = static_cast<uint8_t>(difference | (left[index] ^ right[index]));
        }

        return difference == 0;
    }
}

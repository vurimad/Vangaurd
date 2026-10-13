#include <vanguard/crypto/crypto.hpp>

#include <vanguard/system/platform.hpp>

#if VG_PLATFORM_WINDOWS
#include <Windows.h>
#include <bcrypt.h>
#elif VG_PLATFORM_LINUX
#include <cerrno>
#include <sys/random.h>
#endif

#include <cstring>

namespace
{
    using namespace vanguard;

    constexpr u32 BlockSize = 64;

    constexpr u32 RoundConstants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu,
        0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau,
        0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u,
        0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
        0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    [[nodiscard]] constexpr u32 RotateRight(const u32 value, const u32 amount) noexcept
    {
        return (value >> amount) | (value << (32u - amount));
    }

    [[nodiscard]] constexpr u32 Choose(const u32 x, const u32 y, const u32 z) noexcept
    {
        return (x & y) ^ (~x & z);
    }

    [[nodiscard]] constexpr u32 Majority(const u32 x, const u32 y, const u32 z) noexcept
    {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    [[nodiscard]] constexpr u32 BigSigma0(const u32 value) noexcept
    {
        return RotateRight(value, 2) ^ RotateRight(value, 13) ^ RotateRight(value, 22);
    }

    [[nodiscard]] constexpr u32 BigSigma1(const u32 value) noexcept
    {
        return RotateRight(value, 6) ^ RotateRight(value, 11) ^ RotateRight(value, 25);
    }

    [[nodiscard]] constexpr u32 SmallSigma0(const u32 value) noexcept
    {
        return RotateRight(value, 7) ^ RotateRight(value, 18) ^ (value >> 3u);
    }

    [[nodiscard]] constexpr u32 SmallSigma1(const u32 value) noexcept
    {
        return RotateRight(value, 17) ^ RotateRight(value, 19) ^ (value >> 10u);
    }

    [[nodiscard]] constexpr u32 LoadBigEndian32(const u8* const bytes) noexcept
    {
        return (static_cast<u32>(bytes[0]) << 24u) | (static_cast<u32>(bytes[1]) << 16u) | (static_cast<u32>(bytes[2]) << 8u) | static_cast<u32>(bytes[3]);
    }

    constexpr void StoreBigEndian32(u8* const bytes, const u32 value) noexcept
    {
        bytes[0] = static_cast<u8>(value >> 24u);
        bytes[1] = static_cast<u8>(value >> 16u);
        bytes[2] = static_cast<u8>(value >> 8u);
        bytes[3] = static_cast<u8>(value);
    }

    constexpr void StoreBigEndian64(u8* const bytes, const u64 value) noexcept
    {
        for (u32 index = 0; index < 8; ++index)
        {
            bytes[index] = static_cast<u8>(value >> ((7u - index) * 8u));
        }
    }

    void CopyBytes(u8* const destination, const u8* const source, const usize size) noexcept
    {
        for (usize index = 0; index < size; ++index)
        {
            destination[index] = source[index];
        }
    }
} // namespace

namespace vanguard::crypto
{
    bool SecureRandom(void* const destination, const usize size) noexcept
    {
        if (size == 0)
            return true;
        if (destination == nullptr || size > static_cast<usize>(0xffffffffu))
            return false;
#if VG_PLATFORM_WINDOWS
        const NTSTATUS status = ::BCryptGenRandom(nullptr, static_cast<PUCHAR>(destination), static_cast<ULONG>(size),
                                                  BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status >= 0)
            return true;
#elif VG_PLATFORM_LINUX
        usize offset = 0;
        while (offset < size)
        {
            const ssize_t read = ::getrandom(static_cast<u8*>(destination) + offset, size - offset, 0);
            if (read > 0)
            {
                offset += static_cast<usize>(read);
                continue;
            }
            if (read < 0 && errno == EINTR)
                continue;
            break;
        }
        if (offset == size)
            return true;
#endif
        std::memset(destination, 0, size);
        return false;
    }

    bool Digest256::IsEmpty() const noexcept
    {
        for (const u8 byte : bytes)
        {
            if (byte != 0)
            {
                return false;
            }
        }
        return true;
    }

    bool operator==(const Digest256& left, const Digest256& right) noexcept
    {
        for (u32 index = 0; index < Digest256::ByteCount; ++index)
        {
            if (left.bytes[index] != right.bytes[index])
            {
                return false;
            }
        }
        return true;
    }

    bool operator<(const Digest256& left, const Digest256& right) noexcept
    {
        for (u32 index = 0; index < Digest256::ByteCount; ++index)
        {
            if (left.bytes[index] != right.bytes[index])
            {
                return left.bytes[index] < right.bytes[index];
            }
        }
        return false;
    }

    Sha256Builder::Sha256Builder() noexcept
    {
        Reset();
    }

    void Sha256Builder::Reset() noexcept
    {
        m_state[0] = 0x6a09e667u;
        m_state[1] = 0xbb67ae85u;
        m_state[2] = 0x3c6ef372u;
        m_state[3] = 0xa54ff53au;
        m_state[4] = 0x510e527fu;
        m_state[5] = 0x9b05688cu;
        m_state[6] = 0x1f83d9abu;
        m_state[7] = 0x5be0cd19u;
        m_totalBytes = 0;
        m_bufferedBytes = 0;
        m_finalized = false;
        for (u8& byte : m_block)
        {
            byte = 0;
        }
    }

    bool Sha256Builder::Update(const void* const data, const usize size) noexcept
    {
        if (m_finalized || (size != 0 && data == nullptr) || size > (~u64{0} - m_totalBytes))
        {
            return false;
        }

        const auto* source = static_cast<const u8*>(data);
        usize remaining = size;
        m_totalBytes += size;

        if (m_bufferedBytes != 0)
        {
            const u32 available = BlockSize - m_bufferedBytes;
            const usize copied = remaining < available ? remaining : available;
            CopyBytes(m_block + m_bufferedBytes, source, copied);
            m_bufferedBytes += static_cast<u32>(copied);
            source += copied;
            remaining -= copied;
            if (m_bufferedBytes == BlockSize)
            {
                Transform(m_block, 1);
                m_bufferedBytes = 0;
            }
        }

        const u64 fullBlocks = remaining / BlockSize;
        if (fullBlocks != 0)
        {
            Transform(source, fullBlocks);
            const usize consumed = static_cast<usize>(fullBlocks * BlockSize);
            source += consumed;
            remaining -= consumed;
        }

        if (remaining != 0)
        {
            CopyBytes(m_block, source, remaining);
            m_bufferedBytes = static_cast<u32>(remaining);
        }
        return true;
    }

    bool Sha256Builder::Finalize(Digest256& digest) noexcept
    {
        if (m_finalized || m_totalBytes > (~u64{0} >> 3u))
        {
            return false;
        }

        const u64 totalBits = m_totalBytes << 3u;
        m_block[m_bufferedBytes++] = 0x80u;
        const u32 finalBlockBytes = m_bufferedBytes <= 56u ? 64u : 128u;
        while (m_bufferedBytes < finalBlockBytes)
        {
            m_block[m_bufferedBytes++] = 0;
        }
        StoreBigEndian64(m_block + finalBlockBytes - 8u, totalBits);
        Transform(m_block, finalBlockBytes / BlockSize);

        for (u32 index = 0; index < 8; ++index)
        {
            StoreBigEndian32(digest.bytes + index * 4u, m_state[index]);
        }
        m_finalized = true;
        return true;
    }

    bool Sha256Builder::IsFinalized() const noexcept
    {
        return m_finalized;
    }

    void Sha256Builder::Transform(const u8* const message, const u64 blockCount) noexcept
    {
        for (u64 blockIndex = 0; blockIndex < blockCount; ++blockIndex)
        {
            const u8* const block = message + blockIndex * BlockSize;
            u32 schedule[64];
            for (u32 index = 0; index < 16; ++index)
            {
                schedule[index] = LoadBigEndian32(block + index * 4u);
            }
            for (u32 index = 16; index < 64; ++index)
            {
                schedule[index] = SmallSigma1(schedule[index - 2u]) + schedule[index - 7u] + SmallSigma0(schedule[index - 15u]) + schedule[index - 16u];
            }

            u32 a = m_state[0];
            u32 b = m_state[1];
            u32 c = m_state[2];
            u32 d = m_state[3];
            u32 e = m_state[4];
            u32 f = m_state[5];
            u32 g = m_state[6];
            u32 h = m_state[7];

            for (u32 index = 0; index < 64; ++index)
            {
                const u32 first = h + BigSigma1(e) + Choose(e, f, g) + RoundConstants[index] + schedule[index];
                const u32 second = BigSigma0(a) + Majority(a, b, c);
                h = g;
                g = f;
                f = e;
                e = d + first;
                d = c;
                c = b;
                b = a;
                a = first + second;
            }

            m_state[0] += a;
            m_state[1] += b;
            m_state[2] += c;
            m_state[3] += d;
            m_state[4] += e;
            m_state[5] += f;
            m_state[6] += g;
            m_state[7] += h;
        }
    }

    Digest256 Sha256(const void* const data, const usize size) noexcept
    {
#if VG_PLATFORM_WINDOWS
        // Large model/source fingerprints are hashed repeatedly during durable
        // authoring. Keep the allocation-free portable incremental builder as
        // the baseline, but use Windows CNG's optimized SHA-256 for large
        // one-shot inputs. Any provider/hash failure falls back to that exact
        // reference implementation; the on-disk fingerprint format is unchanged.
        if (data != nullptr && size >= (1u << 20u) && size <= 0xffffffffull)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            if (::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0u) >= 0)
            {
                BCRYPT_HASH_HANDLE hash = nullptr;
                if (::BCryptCreateHash(algorithm, &hash, nullptr, 0u, nullptr, 0u, 0u) >= 0)
                {
                    Digest256 accelerated;
                    const bool hashed =
                        ::BCryptHashData(hash, static_cast<PUCHAR>(const_cast<void*>(data)),
                                         static_cast<ULONG>(size), 0u) >= 0 &&
                        ::BCryptFinishHash(hash, accelerated.bytes, Digest256::ByteCount, 0u) >= 0;
                    ::BCryptDestroyHash(hash);
                    ::BCryptCloseAlgorithmProvider(algorithm, 0u);
                    if (hashed)
                        return accelerated;
                }
                else
                    ::BCryptCloseAlgorithmProvider(algorithm, 0u);
            }
        }
#endif
        Sha256Builder builder;
        Digest256 digest;
        if (!builder.Update(data, size) || !builder.Finalize(digest))
        {
            return {};
        }
        return digest;
    }
} // namespace vanguard::crypto

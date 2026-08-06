#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::crypto
{
    struct Digest256
    {
        static constexpr u32 ByteCount = 32;

        u8 bytes[ByteCount]{};

        [[nodiscard]] bool IsEmpty() const noexcept;

        [[nodiscard]] friend bool operator==(const Digest256& left, const Digest256& right) noexcept;
        [[nodiscard]] friend bool operator<(const Digest256& left, const Digest256& right) noexcept;
    };

    // Incremental SHA-256 adapted from RED's redCrypto implementation. The
    // Vanguard contract preserves the complete 64-bit message length and does
    // not allocate.
    class Sha256Builder final
    {
    public:
        Sha256Builder() noexcept;

        void Reset() noexcept;
        [[nodiscard]] bool Update(const void* data, usize size) noexcept;
        [[nodiscard]] bool Finalize(Digest256& digest) noexcept;
        [[nodiscard]] bool IsFinalized() const noexcept;

    private:
        void Transform(const u8* message, u64 blockCount) noexcept;

        u64 m_totalBytes = 0;
        u32 m_bufferedBytes = 0;
        u8 m_block[128]{};
        u32 m_state[8]{};
        bool m_finalized = false;
    };

    [[nodiscard]] Digest256 Sha256(const void* data, usize size) noexcept;
} // namespace vanguard::crypto

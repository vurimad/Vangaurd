#include <vanguard/serialization/serialization.hpp>

#include "../../imported/common/redSystem/include/crc.h"

namespace vanguard::serialization
{
    u32 Crc32(const void* const data, const usize size, const u32 existing) noexcept
    {
        const auto* cursor = static_cast<const u8*>(data);
        usize remaining = size;
        u32 result = existing;
        constexpr u32 maximumChunk = static_cast<u32>(-1);

        while (remaining != 0)
        {
            const u32 chunk = remaining > maximumChunk ? maximumChunk : static_cast<u32>(remaining);
            result = ::red::CalculateCRC32(cursor, chunk, result);
            cursor += chunk;
            remaining -= chunk;
        }
        return result;
    }

    u64 Crc64(const void* const data, const usize size, const u64 existing) noexcept
    {
        const auto* cursor = static_cast<const u8*>(data);
        usize remaining = size;
        u64 result = existing;
        constexpr u32 maximumChunk = static_cast<u32>(-1);

        while (remaining != 0)
        {
            const u32 chunk = remaining > maximumChunk ? maximumChunk : static_cast<u32>(remaining);
            result = ::red::CalculateCRC64(cursor, chunk, result);
            cursor += chunk;
            remaining -= chunk;
        }
        return result;
    }
} // namespace vanguard::serialization

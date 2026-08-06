#include "../src/package_codec_backend.hpp"

#include "../../imported/common/redCompression/src/lz4/lz4.h"

#include <limits>

namespace vanguard::packages::backend
{
    Result CompressLz4(
        const void* const source,
        const usize sourceSize,
        containers::DynamicArray<u8>& destination) noexcept
    {
        if ((sourceSize != 0 && source == nullptr) ||
            sourceSize > static_cast<usize>(std::numeric_limits<int>::max()))
        {
            return Result::InvalidArgument;
        }
        if (sourceSize == 0)
        {
            destination.Clear();
            return Result::Success;
        }

        const int inputSize = static_cast<int>(sourceSize);
        const int bound = LZ4_compressBound(inputSize);
        if (bound <= 0)
        {
            return Result::CompressionFailure;
        }

        destination.Resize(static_cast<u32>(bound));
        const int compressedSize = LZ4_compress_limitedOutput(
            static_cast<const char*>(source),
            reinterpret_cast<char*>(destination.Data()),
            inputSize,
            bound);
        if (compressedSize <= 0)
        {
            destination.Clear();
            return Result::CompressionFailure;
        }
        destination.Resize(static_cast<u32>(compressedSize));
        return Result::Success;
    }

    Result DecompressLz4(
        const void* const source,
        const usize sourceSize,
        void* const destination,
        const usize destinationSize) noexcept
    {
        if ((sourceSize != 0 && source == nullptr) ||
            (destinationSize != 0 && destination == nullptr) ||
            sourceSize > static_cast<usize>(std::numeric_limits<int>::max()) ||
            destinationSize >
                static_cast<usize>(std::numeric_limits<int>::max()))
        {
            return Result::InvalidArgument;
        }
        if (destinationSize == 0)
        {
            return sourceSize == 0
                ? Result::Success
                : Result::CompressionFailure;
        }

        const int decompressedSize = LZ4_decompress_safe(
            static_cast<const char*>(source),
            static_cast<char*>(destination),
            static_cast<int>(sourceSize),
            static_cast<int>(destinationSize));
        return decompressedSize == static_cast<int>(destinationSize)
            ? Result::Success
            : Result::CompressionFailure;
    }
}

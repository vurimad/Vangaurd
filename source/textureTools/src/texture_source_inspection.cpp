#include <vanguard/texture_tools/texture_import.hpp>

#include <cstring>

namespace
{
    using namespace vanguard;
    namespace tools = vanguard::texture_tools;

    [[nodiscard]] bool Add(const u64 left, const u64 right, u64& value) noexcept
    {
        if (left > ~0ull - right)
            return false;
        value = left + right;
        return true;
    }

    [[nodiscard]] bool Multiply(const u64 left, const u64 right, u64& value) noexcept
    {
        if (left != 0 && right > ~0ull / left)
            return false;
        value = left * right;
        return true;
    }

    [[nodiscard]] u16 Read16(const u8* data, const bool bigEndian) noexcept
    {
        return bigEndian ? static_cast<u16>(static_cast<u16>(data[0]) << 8u | data[1])
                         : static_cast<u16>(data[0] | static_cast<u16>(data[1]) << 8u);
    }

    [[nodiscard]] u32 Read32(const u8* data, const bool bigEndian) noexcept
    {
        return bigEndian ? static_cast<u32>(data[0]) << 24u | static_cast<u32>(data[1]) << 16u | static_cast<u32>(data[2]) << 8u | data[3]
                         : static_cast<u32>(data[0]) | static_cast<u32>(data[1]) << 8u | static_cast<u32>(data[2]) << 16u |
                               static_cast<u32>(data[3]) << 24u;
    }

    [[nodiscard]] u64 Read64(const u8* data, const bool bigEndian) noexcept
    {
        const u32 first = Read32(data, bigEndian);
        const u32 second = Read32(data + 4, bigEndian);
        return bigEndian ? static_cast<u64>(first) << 32u | second : static_cast<u64>(first) | static_cast<u64>(second) << 32u;
    }

    [[nodiscard]] bool FinishOrdinary(const tools::TextureSourceKind kind, const u32 width, const u32 height,
                                      tools::TextureSourceInspection& output) noexcept
    {
        u64 texels = 0;
        if (width == 0 || height == 0 || !Multiply(width, height, texels) || !Multiply(texels, 16u, output.decodedBytes))
            return false;
        output.kind = kind;
        output.width = width;
        output.height = height;
        output.depth = 1;
        output.arrayLayers = 1;
        output.faceCount = 1;
        output.mipCount = 1;
        output.payloadBytes = 0;
        return true;
    }

    [[nodiscard]] bool InspectPng(const containers::ArraySpan<const u8> bytes, tools::TextureSourceInspection& output) noexcept
    {
        static constexpr u8 signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
        return bytes.Size() >= 24 && std::memcmp(bytes.Data(), signature, sizeof(signature)) == 0 && Read32(bytes.Data() + 8, true) == 13 &&
               std::memcmp(bytes.Data() + 12, "IHDR", 4) == 0 &&
               FinishOrdinary(tools::TextureSourceKind::Png, Read32(bytes.Data() + 16, true), Read32(bytes.Data() + 20, true), output);
    }

    [[nodiscard]] bool IsJpegStartOfFrame(const u8 marker) noexcept
    {
        return (marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) || (marker >= 0xc9 && marker <= 0xcb) ||
               (marker >= 0xcd && marker <= 0xcf);
    }

    [[nodiscard]] bool InspectJpeg(const containers::ArraySpan<const u8> bytes, tools::TextureSourceInspection& output) noexcept
    {
        if (bytes.Size() < 4 || bytes[0] != 0xff || bytes[1] != 0xd8)
            return false;
        u64 offset = 2;
        while (offset + 4 <= bytes.Size())
        {
            while (offset < bytes.Size() && bytes[static_cast<u32>(offset)] == 0xff)
                ++offset;
            if (offset >= bytes.Size())
                return false;
            const u8 marker = bytes[static_cast<u32>(offset++)];
            if (marker == 0xd9 || marker == 0xda)
                return false;
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
                continue;
            if (offset + 2 > bytes.Size())
                return false;
            const u16 length = Read16(bytes.Data() + offset, true);
            if (length < 2 || offset + length > bytes.Size())
                return false;
            if (IsJpegStartOfFrame(marker))
            {
                if (length < 7)
                    return false;
                return FinishOrdinary(tools::TextureSourceKind::Jpeg, Read16(bytes.Data() + offset + 5, true),
                                      Read16(bytes.Data() + offset + 3, true), output);
            }
            offset += length;
        }
        return false;
    }

    [[nodiscard]] bool InspectTiff(const containers::ArraySpan<const u8> bytes, tools::TextureSourceInspection& output) noexcept
    {
        if (bytes.Size() < 8)
            return false;
        const bool bigEndian = bytes[0] == 'M' && bytes[1] == 'M';
        if (!bigEndian && !(bytes[0] == 'I' && bytes[1] == 'I'))
            return false;
        const u16 version = Read16(bytes.Data() + 2, bigEndian);
        const bool bigTiff = version == 43;
        if (version != 42 && !bigTiff)
            return false;
        u64 ifdOffset = 0;
        if (bigTiff)
        {
            if (bytes.Size() < 16 || Read16(bytes.Data() + 4, bigEndian) != 8 || Read16(bytes.Data() + 6, bigEndian) != 0)
                return false;
            ifdOffset = Read64(bytes.Data() + 8, bigEndian);
        }
        else
            ifdOffset = Read32(bytes.Data() + 4, bigEndian);
        const u64 countBytes = bigTiff ? 8u : 2u;
        const u64 entryBytes = bigTiff ? 20u : 12u;
        if (ifdOffset > bytes.Size() || countBytes > bytes.Size() - ifdOffset)
            return false;
        const u64 count = bigTiff ? Read64(bytes.Data() + ifdOffset, bigEndian) : Read16(bytes.Data() + ifdOffset, bigEndian);
        u64 tableBytes = 0;
        u64 tableEnd = 0;
        if (!Multiply(count, entryBytes, tableBytes) || !Add(ifdOffset + countBytes, tableBytes, tableEnd) || tableEnd > bytes.Size())
            return false;
        u32 width = 0;
        u32 height = 0;
        u16 orientation = 1;
        for (u64 index = 0; index < count; ++index)
        {
            const u8* const entry = bytes.Data() + ifdOffset + countBytes + index * entryBytes;
            const u16 tag = Read16(entry, bigEndian);
            if (tag != 256 && tag != 257 && tag != 274)
                continue;
            const u16 type = Read16(entry + 2, bigEndian);
            const u64 valueCount = bigTiff ? Read64(entry + 4, bigEndian) : Read32(entry + 4, bigEndian);
            if (valueCount != 1 || (type != 3 && type != 4))
                return false;
            const u8* const inlineValue = entry + (bigTiff ? 12 : 8);
            const u32 value = type == 3 ? Read16(inlineValue, bigEndian) : Read32(inlineValue, bigEndian);
            if (tag == 256)
                width = value;
            else if (tag == 257)
                height = value;
            else
                orientation = static_cast<u16>(value);
        }
        if (orientation >= 5 && orientation <= 8)
        {
            const u32 temporary = width;
            width = height;
            height = temporary;
        }
        return FinishOrdinary(tools::TextureSourceKind::Tiff, width, height, output);
    }

    [[nodiscard]] bool InspectOpenExr(const containers::ArraySpan<const u8> bytes, tools::TextureSourceInspection& output) noexcept
    {
        if (bytes.Size() < 12 || Read32(bytes.Data(), false) != 0x762f3101u)
            return false;
        u64 offset = 8;
        while (offset < bytes.Size())
        {
            const u64 nameStart = offset;
            while (offset < bytes.Size() && bytes[static_cast<u32>(offset)] != 0)
                ++offset;
            if (offset >= bytes.Size())
                return false;
            const u64 nameLength = offset - nameStart;
            ++offset;
            if (nameLength == 0)
                return false;
            const u64 typeStart = offset;
            while (offset < bytes.Size() && bytes[static_cast<u32>(offset)] != 0)
                ++offset;
            if (offset + 5 > bytes.Size())
                return false;
            const u64 typeLength = offset - typeStart;
            ++offset;
            const u32 valueSize = Read32(bytes.Data() + offset, false);
            offset += 4;
            if (valueSize > bytes.Size() - offset)
                return false;
            if (nameLength == 10 && std::memcmp(bytes.Data() + nameStart, "dataWindow", 10) == 0 && typeLength == 5 &&
                std::memcmp(bytes.Data() + typeStart, "box2i", 5) == 0 && valueSize == 16)
            {
                const i32 minimumX = static_cast<i32>(Read32(bytes.Data() + offset, false));
                const i32 minimumY = static_cast<i32>(Read32(bytes.Data() + offset + 4, false));
                const i32 maximumX = static_cast<i32>(Read32(bytes.Data() + offset + 8, false));
                const i32 maximumY = static_cast<i32>(Read32(bytes.Data() + offset + 12, false));
                if (maximumX < minimumX || maximumY < minimumY)
                    return false;
                return FinishOrdinary(tools::TextureSourceKind::OpenExr, static_cast<u32>(static_cast<i64>(maximumX) - minimumX + 1),
                                      static_cast<u32>(static_cast<i64>(maximumY) - minimumY + 1), output);
            }
            offset += valueSize;
        }
        return false;
    }

    [[nodiscard]] bool InspectDds(const containers::ArraySpan<const u8> bytes, tools::TextureSourceInspection& output) noexcept
    {
        if (bytes.Size() < 128 || Read32(bytes.Data(), false) != 0x20534444u || Read32(bytes.Data() + 4, false) != 124 ||
            Read32(bytes.Data() + 76, false) != 32)
            return false;
        const u32 fourCc = Read32(bytes.Data() + 84, false);
        const bool dx10 = fourCc == 0x30315844u;
        const u64 payloadOffset = dx10 ? 148u : 128u;
        if (payloadOffset > bytes.Size())
            return false;
        output.kind = tools::TextureSourceKind::Dds;
        output.width = Read32(bytes.Data() + 16, false);
        output.height = Read32(bytes.Data() + 12, false);
        output.depth = Read32(bytes.Data() + 24, false);
        if (output.depth == 0)
            output.depth = 1;
        const u32 mipCount = Read32(bytes.Data() + 28, false);
        if (mipCount > 255)
            return false;
        output.mipCount = static_cast<u8>(mipCount == 0 ? 1u : mipCount);
        output.arrayLayers = 1;
        output.faceCount = (Read32(bytes.Data() + 112, false) & 0x00000200u) != 0 ? 6u : 1u;
        if (dx10)
        {
            const u32 arraySize = Read32(bytes.Data() + 140, false);
            const u32 miscFlags = Read32(bytes.Data() + 136, false);
            if (arraySize == 0 || arraySize > 65535)
                return false;
            output.arrayLayers = static_cast<u16>(arraySize);
            if ((miscFlags & 0x4u) != 0)
                output.faceCount = 6;
        }
        output.payloadBytes = bytes.Size() - payloadOffset;
        output.decodedBytes = output.payloadBytes;
        return output.width != 0 && output.height != 0 && output.mipCount != 0 && output.payloadBytes != 0;
    }
} // namespace

namespace vanguard::texture_tools
{
    TextureImportResult InspectTextureSource(const containers::ArraySpan<const u8> encoded, TextureSourceInspection& inspection) noexcept
    {
        inspection = {};
        if (encoded.Empty() || encoded.Data() == nullptr)
            return TextureImportResult::InvalidArgument;
        if (InspectPng(encoded, inspection) || InspectJpeg(encoded, inspection) || InspectTiff(encoded, inspection) || InspectOpenExr(encoded, inspection) ||
            InspectDds(encoded, inspection))
            return TextureImportResult::Success;
        inspection = {};
        return TextureImportResult::UnsupportedFormat;
    }
} // namespace vanguard::texture_tools

#include <vanguard/resources/resources.hpp>

#include <vanguard/serialization/serialization.hpp>

namespace
{
    using namespace vanguard;
    using namespace vanguard::resources;

    [[nodiscard]] constexpr bool IsSeparator(const u8 byte) noexcept
    {
        return byte == '/' || byte == '\\';
    }

    [[nodiscard]] constexpr bool IsForbiddenPathByte(const u8 byte) noexcept
    {
        return byte == 0 || byte < 0x20u || byte == ':' || byte == '*' || byte == '?' || byte == '"' || byte == '<' || byte == '>' || byte == '|';
    }

    [[nodiscard]] constexpr u8 CanonicalPathByte(const u8 byte) noexcept
    {
        if (byte == '\\')
        {
            return '/';
        }
        if (byte >= 'A' && byte <= 'Z')
        {
            return static_cast<u8>(byte + ('a' - 'A'));
        }
        return byte;
    }

    [[nodiscard]] Result ValidatePath(const containers::StringView path) noexcept
    {
        const u32 length = path.Length();
        if (length == 0 || length > MaximumResourcePathBytes)
        {
            return Result::InvalidPath;
        }

        u32 segmentStart = 0;
        bool previousWasSeparator = false;
        for (u32 index = 0; index < length; ++index)
        {
            const u8 byte = static_cast<u8>(path[index]);
            const bool separator = IsSeparator(byte);
            if (IsForbiddenPathByte(byte) || (separator && (index == 0 || previousWasSeparator)))
            {
                return Result::InvalidPath;
            }

            if (separator)
            {
                const u32 segmentLength = index - segmentStart;
                if ((segmentLength == 1 && path[segmentStart] == '.') || (segmentLength == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.'))
                {
                    return Result::InvalidPath;
                }
                segmentStart = index + 1;
            }
            previousWasSeparator = separator;
        }

        if (previousWasSeparator)
        {
            return Result::InvalidPath;
        }

        const u32 finalLength = length - segmentStart;
        if ((finalLength == 1 && path[segmentStart] == '.') || (finalLength == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.'))
        {
            return Result::InvalidPath;
        }
        return Result::Success;
    }

    [[nodiscard]] constexpr bool IsValidTypeByte(const u8 byte) noexcept
    {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '-';
    }
} // namespace

namespace vanguard::resources
{
    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success:
            return "Success";
        case Result::InvalidArgument:
            return "InvalidArgument";
        case Result::InvalidPath:
            return "InvalidPath";
        case Result::BufferTooSmall:
            return "BufferTooSmall";
        }
        return "Unknown";
    }

    Result CanonicalizePath(const containers::StringView path, char* const destination, const usize capacity, usize& written) noexcept
    {
        written = 0;
        const Result validation = ValidatePath(path);
        if (validation != Result::Success)
        {
            return validation;
        }
        if (destination == nullptr)
        {
            return Result::InvalidArgument;
        }
        if (capacity < path.Length())
        {
            return Result::BufferTooSmall;
        }

        for (u32 index = 0; index < path.Length(); ++index)
        {
            destination[index] = static_cast<char>(CanonicalPathByte(static_cast<u8>(path[index])));
        }
        written = path.Length();
        return Result::Success;
    }

    ResourceId HashPath(const containers::StringView path) noexcept
    {
        if (ValidatePath(path) != Result::Success)
        {
            return InvalidResourceId;
        }

        ResourceId result = 0;
        u8 batch[128];
        usize count = 0;
        for (const char character : path)
        {
            batch[count++] = CanonicalPathByte(static_cast<u8>(character));
            if (count == sizeof(batch))
            {
                result = serialization::Crc64(batch, count, result);
                count = 0;
            }
        }
        if (count != 0)
        {
            result = serialization::Crc64(batch, count, result);
        }
        return result;
    }

    ResourceTypeId HashTypeName(const containers::StringView typeName) noexcept
    {
        if (typeName.Empty() || typeName.Length() > 255 || typeName.Front() == '.' || typeName.Back() == '.')
        {
            return InvalidResourceTypeId;
        }

        u8 canonical[255];
        bool previousWasDot = false;
        for (u32 index = 0; index < typeName.Length(); ++index)
        {
            const u8 byte = static_cast<u8>(typeName[index]);
            if (!IsValidTypeByte(byte) || (byte == '.' && previousWasDot))
            {
                return InvalidResourceTypeId;
            }
            canonical[index] = CanonicalPathByte(byte);
            previousWasDot = byte == '.';
        }

        return serialization::Crc32(canonical, typeName.Length());
    }
} // namespace vanguard::resources

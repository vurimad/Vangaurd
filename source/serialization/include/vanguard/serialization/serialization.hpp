#pragma once

#include <vanguard/filesystem/filesystem.hpp>

#include <bit>

namespace vanguard::serialization
{
    enum class Result : u8
    {
        Success,
        InvalidArgument,
        WrongStreamMode,
        EndOfStream,
        IoFailure,
        Overflow,
        InvalidEncoding,
        InvalidMagic,
        UnsupportedByteOrder,
        UnsupportedHeader,
        UnsupportedVersion,
        InvalidLayout,
        IntegrityFailure,
        LimitExceeded
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    [[nodiscard]] constexpr u32 MakeFourCC(const char a, const char b, const char c, const char d) noexcept
    {
        return static_cast<u32>(static_cast<u8>(a)) | (static_cast<u32>(static_cast<u8>(b)) << 8u) | (static_cast<u32>(static_cast<u8>(c)) << 16u) |
               (static_cast<u32>(static_cast<u8>(d)) << 24u);
    }

    struct Version
    {
        u16 major = 0;
        u16 minor = 0;

        [[nodiscard]] friend constexpr bool operator==(const Version& left, const Version& right) noexcept = default;
    };

    struct VersionRange
    {
        u16 major = 0;
        u16 minimumMinor = 0;
        u16 maximumMinor = 0;

        [[nodiscard]] constexpr bool Accepts(const Version version) const noexcept
        {
            return version.major == major && version.minor >= minimumMinor && version.minor <= maximumMinor;
        }
    };

    class BinaryReader final
    {
    public:
        explicit BinaryReader(filesystem::IFile& file) noexcept;

        [[nodiscard]] Result GetStatus() const noexcept;
        [[nodiscard]] bool IsGood() const noexcept;
        [[nodiscard]] u64 Position() const noexcept;
        [[nodiscard]] u64 Size() const noexcept;
        [[nodiscard]] u64 GetRemaining() const noexcept;

        [[nodiscard]] bool Seek(u64 position) noexcept;
        [[nodiscard]] bool Skip(u64 size) noexcept;
        [[nodiscard]] bool Align(u32 alignment) noexcept;
        [[nodiscard]] bool ReadBytes(void* destination, usize size) noexcept;

        [[nodiscard]] bool ReadBool(bool& value) noexcept;
        [[nodiscard]] bool ReadU8(u8& value) noexcept;
        [[nodiscard]] bool ReadU16(u16& value) noexcept;
        [[nodiscard]] bool ReadU32(u32& value) noexcept;
        [[nodiscard]] bool ReadU64(u64& value) noexcept;
        [[nodiscard]] bool ReadI8(i8& value) noexcept;
        [[nodiscard]] bool ReadI16(i16& value) noexcept;
        [[nodiscard]] bool ReadI32(i32& value) noexcept;
        [[nodiscard]] bool ReadI64(i64& value) noexcept;
        [[nodiscard]] bool ReadF32(f32& value) noexcept;
        [[nodiscard]] bool ReadF64(f64& value) noexcept;
        [[nodiscard]] bool ReadVarUInt(u64& value) noexcept;
        [[nodiscard]] bool ReadVarInt(i64& value) noexcept;

    private:
        bool Fail(Result result) noexcept;

        filesystem::IFile* m_file = nullptr;
        Result m_status = Result::Success;
    };

    class BinaryWriter final
    {
    public:
        explicit BinaryWriter(filesystem::IFile& file) noexcept;

        [[nodiscard]] Result GetStatus() const noexcept;
        [[nodiscard]] bool IsGood() const noexcept;
        [[nodiscard]] u64 Position() const noexcept;
        [[nodiscard]] u64 Size() const noexcept;

        [[nodiscard]] bool Seek(u64 position) noexcept;
        [[nodiscard]] bool Align(u32 alignment) noexcept;
        [[nodiscard]] bool WriteBytes(const void* source, usize size) noexcept;
        [[nodiscard]] bool WriteBytesAt(u64 position, const void* source, usize size) noexcept;

        [[nodiscard]] bool WriteBool(bool value) noexcept;
        [[nodiscard]] bool WriteU8(u8 value) noexcept;
        [[nodiscard]] bool WriteU16(u16 value) noexcept;
        [[nodiscard]] bool WriteU32(u32 value) noexcept;
        [[nodiscard]] bool WriteU64(u64 value) noexcept;
        [[nodiscard]] bool WriteI8(i8 value) noexcept;
        [[nodiscard]] bool WriteI16(i16 value) noexcept;
        [[nodiscard]] bool WriteI32(i32 value) noexcept;
        [[nodiscard]] bool WriteI64(i64 value) noexcept;
        [[nodiscard]] bool WriteF32(f32 value) noexcept;
        [[nodiscard]] bool WriteF64(f64 value) noexcept;
        [[nodiscard]] bool WriteVarUInt(u64 value) noexcept;
        [[nodiscard]] bool WriteVarInt(i64 value) noexcept;
        [[nodiscard]] bool Flush() noexcept;

    private:
        bool Fail(Result result) noexcept;

        filesystem::IFile* m_file = nullptr;
        Result m_status = Result::Success;
    };

    [[nodiscard]] u32 Crc32(const void* data, usize size, u32 existing = 0) noexcept;
    [[nodiscard]] u64 Crc64(const void* data, usize size, u64 existing = 0) noexcept;

    enum class DocumentFlags : u16
    {
        None = 0,
        HasEditorData = 1u << 0u,
        Deterministic = 1u << 1u
    };

    enum class SectionFlags : u32
    {
        None = 0,
        Optional = 1u << 0u,
        EditorOnly = 1u << 1u,
        Streamable = 1u << 2u
    };

    enum class Codec : u16
    {
        None = 0,
        Lz4 = 1,
        Kraken = 2
    };

    [[nodiscard]] constexpr DocumentFlags operator|(const DocumentFlags left, const DocumentFlags right) noexcept
    {
        return static_cast<DocumentFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    [[nodiscard]] constexpr SectionFlags operator|(const SectionFlags left, const SectionFlags right) noexcept
    {
        return static_cast<SectionFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    struct DocumentHeader
    {
        static constexpr u8 EncodingVersion = 1;
        static constexpr u8 LittleEndian = 1;
        static constexpr u16 WireSize = 40;

        u32 magic = 0;
        u16 headerSize = WireSize;
        Version version;
        DocumentFlags flags = DocumentFlags::None;
        u64 fileSize = 0;
        u64 sectionTableOffset = 0;
        u32 sectionCount = 0;
    };

    struct SectionDescriptor
    {
        static constexpr u16 WireSize = 48;

        u32 id = 0;
        Version version;
        SectionFlags flags = SectionFlags::None;
        Codec codec = Codec::None;
        u8 alignmentLog2 = 0;
        u64 offset = 0;
        u64 storedSize = 0;
        u64 logicalSize = 0;
        u64 storedCrc64 = 0;
    };

    struct ReadLimits
    {
        u64 maximumFileSize = 64ull * 1024ull * 1024ull * 1024ull;
        u32 maximumSections = 65536;
        u8 maximumAlignmentLog2 = 20;
    };

    [[nodiscard]] Result WriteDocumentHeader(BinaryWriter& writer, const DocumentHeader& header) noexcept;
    [[nodiscard]] Result ReadDocumentHeader(BinaryReader& reader, u32 expectedMagic, VersionRange supportedVersions, const ReadLimits& limits,
                                            DocumentHeader& header) noexcept;

    [[nodiscard]] Result WriteSectionDescriptor(BinaryWriter& writer, const SectionDescriptor& section) noexcept;
    [[nodiscard]] Result ReadSectionTable(BinaryReader& reader, const DocumentHeader& header, const ReadLimits& limits,
                                          containers::DynamicArray<SectionDescriptor>& sections) noexcept;

    [[nodiscard]] Result ValidateSectionChecksum(BinaryReader& reader, const SectionDescriptor& section, void* scratch, usize scratchSize) noexcept;
} // namespace vanguard::serialization

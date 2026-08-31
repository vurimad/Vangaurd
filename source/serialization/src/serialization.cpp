#include <vanguard/serialization/serialization.hpp>

namespace
{
    using namespace vanguard;

    [[nodiscard]] constexpr bool IsPowerOfTwo(const u32 value) noexcept
    {
        return value != 0 && (value & (value - 1u)) == 0;
    }

    [[nodiscard]] constexpr bool AddWouldOverflow(const u64 left, const u64 right) noexcept
    {
        return right > ~u64{0} - left;
    }

    void StoreU16(u8* const destination, const u16 value) noexcept
    {
        destination[0] = static_cast<u8>(value);
        destination[1] = static_cast<u8>(value >> 8u);
    }

    void StoreU32(u8* const destination, const u32 value) noexcept
    {
        destination[0] = static_cast<u8>(value);
        destination[1] = static_cast<u8>(value >> 8u);
        destination[2] = static_cast<u8>(value >> 16u);
        destination[3] = static_cast<u8>(value >> 24u);
    }

    void StoreU64(u8* const destination, const u64 value) noexcept
    {
        for (u32 index = 0; index < 8; ++index)
        {
            destination[index] = static_cast<u8>(value >> (index * 8u));
        }
    }

    [[nodiscard]] u16 LoadU16(const u8* const source) noexcept
    {
        return static_cast<u16>(static_cast<u16>(source[0]) | (static_cast<u16>(source[1]) << 8u));
    }

    [[nodiscard]] u32 LoadU32(const u8* const source) noexcept
    {
        return static_cast<u32>(source[0]) | (static_cast<u32>(source[1]) << 8u) | (static_cast<u32>(source[2]) << 16u) | (static_cast<u32>(source[3]) << 24u);
    }

    [[nodiscard]] u64 LoadU64(const u8* const source) noexcept
    {
        u64 value = 0;
        for (u32 index = 0; index < 8; ++index)
        {
            value |= static_cast<u64>(source[index]) << (index * 8u);
        }
        return value;
    }

    [[nodiscard]] vanguard::serialization::Result WriterResult(const vanguard::serialization::BinaryWriter& writer) noexcept
    {
        return writer.IsGood() ? vanguard::serialization::Result::Success : writer.GetStatus();
    }

    [[nodiscard]] vanguard::serialization::Result ReaderResult(const vanguard::serialization::BinaryReader& reader) noexcept
    {
        return reader.IsGood() ? vanguard::serialization::Result::Success : reader.GetStatus();
    }

    [[nodiscard]] bool RangesOverlap(const u64 firstOffset, const u64 firstSize, const u64 secondOffset, const u64 secondSize) noexcept
    {
        return firstOffset < secondOffset + secondSize && secondOffset < firstOffset + firstSize;
    }
} // namespace

namespace vanguard::serialization
{
    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success:
            return "Success";
        case Result::InvalidArgument:
            return "InvalidArgument";
        case Result::WrongStreamMode:
            return "WrongStreamMode";
        case Result::EndOfStream:
            return "EndOfStream";
        case Result::IoFailure:
            return "IoFailure";
        case Result::Overflow:
            return "Overflow";
        case Result::InvalidEncoding:
            return "InvalidEncoding";
        case Result::InvalidMagic:
            return "InvalidMagic";
        case Result::UnsupportedByteOrder:
            return "UnsupportedByteOrder";
        case Result::UnsupportedHeader:
            return "UnsupportedHeader";
        case Result::UnsupportedVersion:
            return "UnsupportedVersion";
        case Result::InvalidLayout:
            return "InvalidLayout";
        case Result::IntegrityFailure:
            return "IntegrityFailure";
        case Result::LimitExceeded:
            return "LimitExceeded";
        }
        return "Unknown";
    }

    BinaryReader::BinaryReader(filesystem::IFile& file) noexcept : m_file(&file)
    {
        if (!file.IsReader())
        {
            m_status = Result::WrongStreamMode;
        }
    }

    Result BinaryReader::GetStatus() const noexcept
    {
        return m_status;
    }

    bool BinaryReader::IsGood() const noexcept
    {
        return m_status == Result::Success;
    }

    u64 BinaryReader::Position() const noexcept
    {
        return m_file ? m_file->GetOffset() : 0;
    }

    u64 BinaryReader::Size() const noexcept
    {
        return m_file ? m_file->GetSize() : 0;
    }

    u64 BinaryReader::GetRemaining() const noexcept
    {
        const u64 position = Position();
        const u64 size = Size();
        return position <= size ? size - position : 0;
    }

    bool BinaryReader::Fail(const Result result) noexcept
    {
        if (m_status == Result::Success)
        {
            m_status = result;
        }
        return false;
    }

    bool BinaryReader::Seek(const u64 position) noexcept
    {
        if (!IsGood())
        {
            return false;
        }
        constexpr u64 maximumSignedOffset = (~u64{0}) >> 1u;
        if (position > Size() || position > maximumSignedOffset)
        {
            return Fail(Result::EndOfStream);
        }
        m_file->Seek(static_cast<i64>(position));
        return m_file->HasErrors() ? Fail(Result::IoFailure) : true;
    }

    bool BinaryReader::Skip(const u64 size) noexcept
    {
        if (AddWouldOverflow(Position(), size))
        {
            return Fail(Result::Overflow);
        }
        return Seek(Position() + size);
    }

    bool BinaryReader::Align(const u32 alignment) noexcept
    {
        if (!IsPowerOfTwo(alignment))
        {
            return Fail(Result::InvalidArgument);
        }

        const u64 padding = (alignment - (Position() & (alignment - 1u))) & (alignment - 1u);
        u8 bytes[64] = {};
        u64 remaining = padding;
        while (remaining != 0)
        {
            const usize batch = remaining > sizeof(bytes) ? sizeof(bytes) : static_cast<usize>(remaining);
            if (!ReadBytes(bytes, batch))
            {
                return false;
            }
            for (usize index = 0; index < batch; ++index)
            {
                if (bytes[index] != 0)
                {
                    return Fail(Result::InvalidEncoding);
                }
            }
            remaining -= batch;
        }
        return true;
    }

    bool BinaryReader::ReadBytes(void* const destination, const usize size) noexcept
    {
        if (!IsGood())
        {
            return false;
        }
        if (size != 0 && destination == nullptr)
        {
            return Fail(Result::InvalidArgument);
        }
        if (static_cast<u64>(size) > GetRemaining())
        {
            return Fail(Result::EndOfStream);
        }
        auto* cursor = static_cast<u8*>(destination);
        usize remaining = size;
        constexpr usize maximumBatch = 0x7fffffffu;
        while (remaining != 0)
        {
            const usize batch = remaining > maximumBatch ? maximumBatch : remaining;
            m_file->Serialize(cursor, batch);
            if (m_file->HasErrors())
            {
                return Fail(Result::IoFailure);
            }
            cursor += batch;
            remaining -= batch;
        }
        return true;
    }

    bool BinaryReader::ReadU8(u8& value) noexcept
    {
        return ReadBytes(&value, sizeof(value));
    }

    bool BinaryReader::ReadU16(u16& value) noexcept
    {
        u8 bytes[2];
        if (!ReadBytes(bytes, sizeof(bytes)))
        {
            return false;
        }
        value = LoadU16(bytes);
        return true;
    }

    bool BinaryReader::ReadU32(u32& value) noexcept
    {
        u8 bytes[4];
        if (!ReadBytes(bytes, sizeof(bytes)))
        {
            return false;
        }
        value = LoadU32(bytes);
        return true;
    }

    bool BinaryReader::ReadU64(u64& value) noexcept
    {
        u8 bytes[8];
        if (!ReadBytes(bytes, sizeof(bytes)))
        {
            return false;
        }
        value = LoadU64(bytes);
        return true;
    }

    bool BinaryReader::ReadI8(i8& value) noexcept
    {
        u8 bits = 0;
        if (!ReadU8(bits))
        {
            return false;
        }
        value = std::bit_cast<i8>(bits);
        return true;
    }

    bool BinaryReader::ReadI16(i16& value) noexcept
    {
        u16 bits = 0;
        if (!ReadU16(bits))
        {
            return false;
        }
        value = std::bit_cast<i16>(bits);
        return true;
    }

    bool BinaryReader::ReadI32(i32& value) noexcept
    {
        u32 bits = 0;
        if (!ReadU32(bits))
        {
            return false;
        }
        value = std::bit_cast<i32>(bits);
        return true;
    }

    bool BinaryReader::ReadI64(i64& value) noexcept
    {
        u64 bits = 0;
        if (!ReadU64(bits))
        {
            return false;
        }
        value = std::bit_cast<i64>(bits);
        return true;
    }

    bool BinaryReader::ReadF32(f32& value) noexcept
    {
        u32 bits = 0;
        if (!ReadU32(bits))
        {
            return false;
        }
        value = std::bit_cast<f32>(bits);
        return true;
    }

    bool BinaryReader::ReadF64(f64& value) noexcept
    {
        u64 bits = 0;
        if (!ReadU64(bits))
        {
            return false;
        }
        value = std::bit_cast<f64>(bits);
        return true;
    }

    bool BinaryReader::ReadBool(bool& value) noexcept
    {
        u8 encoded = 0;
        if (!ReadU8(encoded))
        {
            return false;
        }
        if (encoded > 1)
        {
            return Fail(Result::InvalidEncoding);
        }
        value = encoded != 0;
        return true;
    }

    bool BinaryReader::ReadVarUInt(u64& value) noexcept
    {
        value = 0;
        for (u32 index = 0; index < 10; ++index)
        {
            u8 byte = 0;
            if (!ReadU8(byte))
            {
                return false;
            }
            if (index == 9 && byte > 1)
            {
                return Fail(Result::Overflow);
            }

            value |= static_cast<u64>(byte & 0x7fu) << (index * 7u);
            if ((byte & 0x80u) == 0)
            {
                if (index != 0 && byte == 0)
                {
                    return Fail(Result::InvalidEncoding);
                }
                return true;
            }
        }
        return Fail(Result::Overflow);
    }

    bool BinaryReader::ReadVarInt(i64& value) noexcept
    {
        u64 encoded = 0;
        if (!ReadVarUInt(encoded))
        {
            return false;
        }
        const u64 sign = static_cast<u64>(0) - (encoded & 1u);
        value = std::bit_cast<i64>((encoded >> 1u) ^ sign);
        return true;
    }

    BinaryWriter::BinaryWriter(filesystem::IFile& file) noexcept : m_file(&file)
    {
        if (!file.IsWriter())
        {
            m_status = Result::WrongStreamMode;
        }
    }

    Result BinaryWriter::GetStatus() const noexcept
    {
        return m_status;
    }

    bool BinaryWriter::IsGood() const noexcept
    {
        return m_status == Result::Success;
    }

    u64 BinaryWriter::Position() const noexcept
    {
        return m_file ? m_file->GetOffset() : 0;
    }

    u64 BinaryWriter::Size() const noexcept
    {
        return m_file ? m_file->GetSize() : 0;
    }

    bool BinaryWriter::Fail(const Result result) noexcept
    {
        if (m_status == Result::Success)
        {
            m_status = result;
        }
        return false;
    }

    bool BinaryWriter::Seek(const u64 position) noexcept
    {
        if (!IsGood())
        {
            return false;
        }
        constexpr u64 maximumSignedOffset = (~u64{0}) >> 1u;
        if (position > maximumSignedOffset || position > Size())
        {
            return Fail(position > maximumSignedOffset ? Result::Overflow : Result::InvalidLayout);
        }
        m_file->Seek(static_cast<i64>(position));
        return m_file->HasErrors() ? Fail(Result::IoFailure) : true;
    }

    bool BinaryWriter::Align(const u32 alignment) noexcept
    {
        if (!IsPowerOfTwo(alignment))
        {
            return Fail(Result::InvalidArgument);
        }

        const u64 padding = (alignment - (Position() & (alignment - 1u))) & (alignment - 1u);
        constexpr u8 zeros[64] = {};
        u64 remaining = padding;
        while (remaining != 0)
        {
            const usize batch = remaining > sizeof(zeros) ? sizeof(zeros) : static_cast<usize>(remaining);
            if (!WriteBytes(zeros, batch))
            {
                return false;
            }
            remaining -= batch;
        }
        return true;
    }

    bool BinaryWriter::WriteBytes(const void* const source, const usize size) noexcept
    {
        if (!IsGood())
        {
            return false;
        }
        if (size != 0 && source == nullptr)
        {
            return Fail(Result::InvalidArgument);
        }
        const auto* cursor = static_cast<const u8*>(source);
        usize remaining = size;
        constexpr usize maximumBatch = 0x7fffffffu;
        while (remaining != 0)
        {
            const usize batch = remaining > maximumBatch ? maximumBatch : remaining;
            m_file->Serialize(const_cast<u8*>(cursor), batch);
            if (m_file->HasErrors())
            {
                return Fail(Result::IoFailure);
            }
            cursor += batch;
            remaining -= batch;
        }
        return true;
    }

    bool BinaryWriter::WriteBytesAt(const u64 position, const void* const source, const usize size) noexcept
    {
        const u64 restorePosition = Position();
        if (static_cast<u64>(size) > Size() || position > Size() - static_cast<u64>(size))
        {
            return Fail(Result::InvalidLayout);
        }
        if (!Seek(position) || !WriteBytes(source, size))
        {
            return false;
        }
        return Seek(restorePosition);
    }

    bool BinaryWriter::WriteU8(const u8 value) noexcept
    {
        return WriteBytes(&value, sizeof(value));
    }

    bool BinaryWriter::WriteU16(const u16 value) noexcept
    {
        u8 bytes[2];
        StoreU16(bytes, value);
        return WriteBytes(bytes, sizeof(bytes));
    }

    bool BinaryWriter::WriteU32(const u32 value) noexcept
    {
        u8 bytes[4];
        StoreU32(bytes, value);
        return WriteBytes(bytes, sizeof(bytes));
    }

    bool BinaryWriter::WriteU64(const u64 value) noexcept
    {
        u8 bytes[8];
        StoreU64(bytes, value);
        return WriteBytes(bytes, sizeof(bytes));
    }

    bool BinaryWriter::WriteI8(const i8 value) noexcept
    {
        return WriteU8(std::bit_cast<u8>(value));
    }

    bool BinaryWriter::WriteI16(const i16 value) noexcept
    {
        return WriteU16(std::bit_cast<u16>(value));
    }

    bool BinaryWriter::WriteI32(const i32 value) noexcept
    {
        return WriteU32(std::bit_cast<u32>(value));
    }

    bool BinaryWriter::WriteI64(const i64 value) noexcept
    {
        return WriteU64(std::bit_cast<u64>(value));
    }

    bool BinaryWriter::WriteF32(const f32 value) noexcept
    {
        return WriteU32(std::bit_cast<u32>(value));
    }

    bool BinaryWriter::WriteF64(const f64 value) noexcept
    {
        return WriteU64(std::bit_cast<u64>(value));
    }

    bool BinaryWriter::WriteBool(const bool value) noexcept
    {
        return WriteU8(value ? 1u : 0u);
    }

    bool BinaryWriter::WriteVarUInt(u64 value) noexcept
    {
        do
        {
            u8 byte = static_cast<u8>(value & 0x7fu);
            value >>= 7u;
            if (value != 0)
            {
                byte |= 0x80u;
            }
            if (!WriteU8(byte))
            {
                return false;
            }
        } while (value != 0);
        return true;
    }

    bool BinaryWriter::WriteVarInt(const i64 value) noexcept
    {
        const u64 bits = std::bit_cast<u64>(value);
        const u64 sign = static_cast<u64>(0) - static_cast<u64>(value < 0);
        return WriteVarUInt((bits << 1u) ^ sign);
    }

    bool BinaryWriter::Flush() noexcept
    {
        if (!IsGood())
        {
            return false;
        }
        m_file->Flush();
        return m_file->HasErrors() ? Fail(Result::IoFailure) : true;
    }

    Result WriteDocumentHeader(BinaryWriter& writer, const DocumentHeader& header) noexcept
    {
        u8 bytes[DocumentHeader::WireSize] = {};
        if (header.magic == 0 || header.headerSize != DocumentHeader::WireSize || header.fileSize < header.headerSize ||
            header.sectionTableOffset < header.headerSize)
        {
            return Result::InvalidArgument;
        }
        const u64 tableSize = static_cast<u64>(header.sectionCount) * SectionDescriptor::WireSize;
        if (AddWouldOverflow(header.sectionTableOffset, tableSize) || header.sectionTableOffset + tableSize > header.fileSize)
        {
            return Result::InvalidLayout;
        }
        StoreU32(bytes + 0, header.magic);
        bytes[4] = DocumentHeader::LittleEndian;
        bytes[5] = DocumentHeader::EncodingVersion;
        StoreU16(bytes + 6, header.headerSize);
        StoreU16(bytes + 8, header.version.major);
        StoreU16(bytes + 10, header.version.minor);
        StoreU16(bytes + 12, static_cast<u16>(header.flags));
        StoreU16(bytes + 14, 0);
        StoreU64(bytes + 16, header.fileSize);
        StoreU64(bytes + 24, header.sectionTableOffset);
        StoreU32(bytes + 32, header.sectionCount);
        StoreU32(bytes + 36, Crc32(bytes, 36));
        return writer.WriteBytes(bytes, sizeof(bytes)) ? Result::Success : WriterResult(writer);
    }

    Result ReadDocumentHeader(BinaryReader& reader, const u32 expectedMagic, const VersionRange supportedVersions, const ReadLimits& limits,
                              DocumentHeader& header) noexcept
    {
        u8 bytes[DocumentHeader::WireSize] = {};
        if (!reader.Seek(0) || !reader.ReadBytes(bytes, sizeof(bytes)))
        {
            return ReaderResult(reader);
        }
        if (LoadU32(bytes + 36) != Crc32(bytes, 36))
        {
            return Result::IntegrityFailure;
        }
        if (LoadU32(bytes + 0) != expectedMagic)
        {
            return Result::InvalidMagic;
        }
        if (bytes[4] != DocumentHeader::LittleEndian)
        {
            return Result::UnsupportedByteOrder;
        }
        if (bytes[5] != DocumentHeader::EncodingVersion || LoadU16(bytes + 6) < DocumentHeader::WireSize)
        {
            return Result::UnsupportedHeader;
        }
        if (LoadU16(bytes + 14) != 0)
        {
            return Result::InvalidEncoding;
        }
        constexpr u16 knownFlags = static_cast<u16>(DocumentFlags::HasEditorData) | static_cast<u16>(DocumentFlags::Deterministic);
        if ((LoadU16(bytes + 12) & ~knownFlags) != 0)
        {
            return Result::InvalidEncoding;
        }

        header.magic = LoadU32(bytes + 0);
        header.headerSize = LoadU16(bytes + 6);
        header.version = {LoadU16(bytes + 8), LoadU16(bytes + 10)};
        header.flags = static_cast<DocumentFlags>(LoadU16(bytes + 12));
        header.fileSize = LoadU64(bytes + 16);
        header.sectionTableOffset = LoadU64(bytes + 24);
        header.sectionCount = LoadU32(bytes + 32);

        if (!supportedVersions.Accepts(header.version))
        {
            return Result::UnsupportedVersion;
        }
        if (header.fileSize != reader.Size() || header.fileSize > limits.maximumFileSize)
        {
            return header.fileSize > limits.maximumFileSize ? Result::LimitExceeded : Result::InvalidLayout;
        }
        if (header.sectionCount > limits.maximumSections)
        {
            return Result::LimitExceeded;
        }

        const u64 tableSize = static_cast<u64>(header.sectionCount) * SectionDescriptor::WireSize;
        if (AddWouldOverflow(header.sectionTableOffset, tableSize) || header.sectionTableOffset < LoadU16(bytes + 6) ||
            header.sectionTableOffset + tableSize > header.fileSize)
        {
            return Result::InvalidLayout;
        }
        return Result::Success;
    }

    Result WriteSectionDescriptor(BinaryWriter& writer, const SectionDescriptor& section) noexcept
    {
        if (section.id == 0 || section.alignmentLog2 >= 64)
        {
            return Result::InvalidArgument;
        }
        const u64 alignment = u64{1} << section.alignmentLog2;
        if ((section.offset & (alignment - 1u)) != 0 || AddWouldOverflow(section.offset, section.storedSize) ||
            (section.codec == Codec::None && section.storedSize != section.logicalSize))
        {
            return Result::InvalidLayout;
        }
        u8 bytes[SectionDescriptor::WireSize] = {};
        StoreU32(bytes + 0, section.id);
        StoreU16(bytes + 4, section.version.major);
        StoreU16(bytes + 6, section.version.minor);
        StoreU32(bytes + 8, static_cast<u32>(section.flags));
        StoreU16(bytes + 12, static_cast<u16>(section.codec));
        bytes[14] = section.alignmentLog2;
        bytes[15] = 0;
        StoreU64(bytes + 16, section.offset);
        StoreU64(bytes + 24, section.storedSize);
        StoreU64(bytes + 32, section.logicalSize);
        StoreU64(bytes + 40, section.storedCrc64);
        return writer.WriteBytes(bytes, sizeof(bytes)) ? Result::Success : WriterResult(writer);
    }

    Result ReadSectionTable(BinaryReader& reader, const DocumentHeader& header, const ReadLimits& limits,
                            containers::DynamicArray<SectionDescriptor>& sections) noexcept
    {
        if (header.sectionCount > limits.maximumSections)
        {
            return Result::LimitExceeded;
        }
        const u64 tableSize = static_cast<u64>(header.sectionCount) * SectionDescriptor::WireSize;
        if (AddWouldOverflow(header.sectionTableOffset, tableSize) || header.sectionTableOffset < header.headerSize ||
            header.sectionTableOffset + tableSize > header.fileSize || header.fileSize != reader.Size())
        {
            return Result::InvalidLayout;
        }
        if (!reader.Seek(header.sectionTableOffset))
        {
            return ReaderResult(reader);
        }

        sections.Clear();
        sections.Resize(header.sectionCount);
        u64 previousEnd = header.headerSize;
        for (u32 index = 0; index < header.sectionCount; ++index)
        {
            u8 bytes[SectionDescriptor::WireSize] = {};
            if (!reader.ReadBytes(bytes, sizeof(bytes)))
            {
                return ReaderResult(reader);
            }

            SectionDescriptor& section = sections[index];
            section.id = LoadU32(bytes + 0);
            section.version = {LoadU16(bytes + 4), LoadU16(bytes + 6)};
            section.flags = static_cast<SectionFlags>(LoadU32(bytes + 8));
            section.codec = static_cast<Codec>(LoadU16(bytes + 12));
            section.alignmentLog2 = bytes[14];
            section.offset = LoadU64(bytes + 16);
            section.storedSize = LoadU64(bytes + 24);
            section.logicalSize = LoadU64(bytes + 32);
            section.storedCrc64 = LoadU64(bytes + 40);

            if (bytes[15] != 0)
            {
                return Result::InvalidEncoding;
            }
            constexpr u32 knownFlags =
                static_cast<u32>(SectionFlags::Optional) | static_cast<u32>(SectionFlags::EditorOnly) | static_cast<u32>(SectionFlags::Streamable);
            if ((static_cast<u32>(section.flags) & ~knownFlags) != 0)
            {
                return Result::InvalidEncoding;
            }
            if (section.alignmentLog2 > limits.maximumAlignmentLog2)
            {
                return Result::LimitExceeded;
            }
            const u64 alignment = u64{1} << section.alignmentLog2;
            if ((section.offset & (alignment - 1u)) != 0 || AddWouldOverflow(section.offset, section.storedSize) || section.offset < previousEnd ||
                section.offset + section.storedSize > header.fileSize)
            {
                return Result::InvalidLayout;
            }
            if (section.codec == Codec::None && section.storedSize != section.logicalSize)
            {
                return Result::InvalidLayout;
            }
            if (RangesOverlap(section.offset, section.storedSize, header.sectionTableOffset,
                              static_cast<u64>(header.sectionCount) * SectionDescriptor::WireSize))
            {
                return Result::InvalidLayout;
            }
            previousEnd = section.offset + section.storedSize;
        }
        return Result::Success;
    }

    Result ValidateSectionChecksum(BinaryReader& reader, const SectionDescriptor& section, void* const scratch, const usize scratchSize) noexcept
    {
        if (scratch == nullptr || scratchSize == 0)
        {
            return Result::InvalidArgument;
        }
        if (!reader.Seek(section.offset))
        {
            return ReaderResult(reader);
        }

        u64 remaining = section.storedSize;
        u64 checksum = 0;
        while (remaining != 0)
        {
            const usize batch = remaining > scratchSize ? scratchSize : static_cast<usize>(remaining);
            if (!reader.ReadBytes(scratch, batch))
            {
                return ReaderResult(reader);
            }
            checksum = Crc64(scratch, batch, checksum);
            remaining -= batch;
        }
        return checksum == section.storedCrc64 ? Result::Success : Result::IntegrityFailure;
    }
} // namespace vanguard::serialization

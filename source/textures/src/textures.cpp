#include <vanguard/textures/textures.hpp>

#include <algorithm>

namespace
{
    using namespace vanguard;
    namespace texture = vanguard::textures;
    namespace serialization = vanguard::serialization;

    constexpr serialization::Version FileVersion{1, 0};
    constexpr u32 MetadataSection = serialization::MakeFourCC('M', 'E', 'T', 'A');
    constexpr u32 TextureDataSection = serialization::MakeFourCC('T', 'E', 'X', 'D');
    constexpr u32 MetadataWireVersion = 1;
    constexpr u64 ContentFingerprintOffset = 60;
    constexpr u8 DataAlignmentLog2 = 6;
    constexpr u64 DataAlignment = 1ull << DataAlignmentLog2;
    constexpr u16 KnownTextureFlags = static_cast<u16>(texture::TextureFlags::Streamable) |
                                      static_cast<u16>(texture::TextureFlags::DirectGpuUpload);
    constexpr u16 KnownSubresourceFlags = static_cast<u16>(texture::SubresourceFlags::MipTail) |
                                          static_cast<u16>(texture::SubresourceFlags::DirectGpuUpload);

    using ByteArray = containers::DynamicArray<u8>;

    struct CanonicalData
    {
        CanonicalData() noexcept
            : builds(memory::pools::Rendering::GetInstance()), records(memory::pools::Rendering::GetInstance())
        {
        }

        containers::DynamicArray<texture::SubresourceBuildRecord> builds;
        containers::DynamicArray<texture::SubresourceRecord> records;
        u64 dataSize = 0;
    };

    [[nodiscard]] texture::Result ConvertSerializationResult(const serialization::Result result) noexcept
    {
        switch (result)
        {
        case serialization::Result::Success:
            return texture::Result::Success;
        case serialization::Result::InvalidMagic:
            return texture::Result::InvalidMagic;
        case serialization::Result::UnsupportedVersion:
            return texture::Result::UnsupportedVersion;
        case serialization::Result::IntegrityFailure:
            return texture::Result::IntegrityFailure;
        case serialization::Result::LimitExceeded:
        case serialization::Result::Overflow:
            return texture::Result::LimitExceeded;
        case serialization::Result::InvalidArgument:
            return texture::Result::InvalidArgument;
        case serialization::Result::IoFailure:
        case serialization::Result::WrongStreamMode:
        case serialization::Result::EndOfStream:
            return texture::Result::IoFailure;
        default:
            return texture::Result::InvalidLayout;
        }
    }

    [[nodiscard]] texture::Result WriterResult(const serialization::BinaryWriter& writer) noexcept
    {
        return writer.Good() ? texture::Result::Success : ConvertSerializationResult(writer.Status());
    }

    [[nodiscard]] texture::Result ReaderResult(const serialization::BinaryReader& reader) noexcept
    {
        return reader.Good() ? texture::Result::Success : ConvertSerializationResult(reader.Status());
    }

    [[nodiscard]] u64 AlignUp(const u64 value, const u64 alignment) noexcept
    {
        if (alignment == 0 || value > ~u64{0} - (alignment - 1))
        {
            return ~u64{0};
        }
        return (value + alignment - 1) & ~(alignment - 1);
    }

    [[nodiscard]] bool WriteDigest(serialization::BinaryWriter& writer, const crypto::Digest256& digest) noexcept
    {
        return writer.WriteBytes(digest.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool ReadDigest(serialization::BinaryReader& reader, crypto::Digest256& digest) noexcept
    {
        return reader.ReadBytes(digest.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] u8 FaceCount(const texture::TextureDimension dimension) noexcept
    {
        return dimension == texture::TextureDimension::Cube ? 6 : 1;
    }

    [[nodiscard]] bool IsValidDimension(const texture::TextureDimension dimension) noexcept
    {
        return dimension <= texture::TextureDimension::Cube;
    }

    [[nodiscard]] bool IsValidColorSpace(const texture::ColorSpace colorSpace) noexcept
    {
        return colorSpace <= texture::ColorSpace::SRgb;
    }

    [[nodiscard]] bool IsValidFormat(const texture::PixelFormat format) noexcept
    {
        return format < texture::PixelFormat::Count && texture::GetFormatInfo(format).bytesPerBlock != 0;
    }

    [[nodiscard]] bool LessSubresource(const texture::SubresourceBuildRecord& left,
                                       const texture::SubresourceBuildRecord& right) noexcept
    {
        if (left.mipLevel != right.mipLevel)
        {
            return left.mipLevel < right.mipLevel;
        }
        if (left.arrayLayer != right.arrayLayer)
        {
            return left.arrayLayer < right.arrayLayer;
        }
        return left.face < right.face;
    }

    [[nodiscard]] bool SameSubresource(const texture::SubresourceBuildRecord& left,
                                       const texture::SubresourceBuildRecord& right) noexcept
    {
        return left.mipLevel == right.mipLevel && left.arrayLayer == right.arrayLayer && left.face == right.face;
    }

    [[nodiscard]] texture::Result ValidateDescription(const texture::BuildDescription& description) noexcept
    {
        if (!IsValidDimension(description.dimension))
        {
            return texture::Result::InvalidDimension;
        }
        if (!IsValidFormat(description.format) || !IsValidColorSpace(description.colorSpace) ||
            (description.colorSpace == texture::ColorSpace::SRgb && !texture::GetFormatInfo(description.format).supportsSRgb))
        {
            return texture::Result::InvalidFormat;
        }
        if ((static_cast<u16>(description.flags) & ~KnownTextureFlags) != 0 || description.width == 0 ||
            description.height == 0 || description.depth == 0 || description.arrayLayers == 0)
        {
            return texture::Result::InvalidArgument;
        }
        if ((description.dimension == texture::TextureDimension::Texture1D &&
             (description.height != 1 || description.depth != 1)) ||
            (description.dimension == texture::TextureDimension::Texture2D && description.depth != 1) ||
            (description.dimension == texture::TextureDimension::Texture3D && description.arrayLayers != 1) ||
            (description.dimension == texture::TextureDimension::Cube &&
             (description.width != description.height || description.depth != 1)))
        {
            return texture::Result::InvalidDimension;
        }
        const u32 fullMipCount = texture::CalculateMipCount(description.width, description.height, description.depth);
        if (description.mipCount == 0 || description.mipCount > fullMipCount ||
            description.mipTailFirstLevel >= description.mipCount ||
            (!texture::HasFlag(description.flags, texture::TextureFlags::Streamable) && description.mipTailFirstLevel != 0))
        {
            return texture::Result::InvalidMipChain;
        }
        const u64 expectedSubresources = static_cast<u64>(description.mipCount) * description.arrayLayers *
                                         FaceCount(description.dimension);
        if (expectedSubresources > ~u32{0} || description.subresources.Size() != expectedSubresources)
        {
            return texture::Result::MissingSubresource;
        }
        return texture::Result::Success;
    }

    [[nodiscard]] texture::Result BuildCanonicalData(const texture::BuildDescription& description,
                                                     CanonicalData& data) noexcept
    {
        const texture::Result descriptionResult = ValidateDescription(description);
        if (descriptionResult != texture::Result::Success)
        {
            return descriptionResult;
        }

        data.builds.Reserve(description.subresources.Size());
        for (u32 index = 0; index < description.subresources.Size(); ++index)
        {
            data.builds.PushBack(description.subresources[index]);
        }
        std::sort(data.builds.Begin(), data.builds.End(), LessSubresource);

        data.records.Reserve(data.builds.Size());
        u64 dataCursor = 0;
        const u8 expectedFaces = FaceCount(description.dimension);
        for (u32 index = 0; index < data.builds.Size(); ++index)
        {
            const texture::SubresourceBuildRecord& build = data.builds[index];
            if (index != 0 && SameSubresource(data.builds[index - 1], build))
            {
                return texture::Result::DuplicateSubresource;
            }

            const u8 expectedMip = static_cast<u8>(index / (static_cast<u32>(description.arrayLayers) * expectedFaces));
            const u32 withinMip = index % (static_cast<u32>(description.arrayLayers) * expectedFaces);
            const u16 expectedLayer = static_cast<u16>(withinMip / expectedFaces);
            const u8 expectedFace = static_cast<u8>(withinMip % expectedFaces);
            if (build.mipLevel != expectedMip || build.arrayLayer != expectedLayer || build.face != expectedFace)
            {
                return texture::Result::MissingSubresource;
            }

            const u32 width = texture::CalculateMipExtent(description.width, build.mipLevel);
            const u32 height = texture::CalculateMipExtent(description.height, build.mipLevel);
            const u32 depth = texture::CalculateMipExtent(description.depth, build.mipLevel);
            const u32 minimumRowPitch = texture::CalculateMinimumRowPitch(description.format, width);
            const u32 minimumSlicePitch = texture::CalculateMinimumSlicePitch(description.format, width, height);
            const texture::FormatInfo format = texture::GetFormatInfo(description.format);
            const u32 rowCount = (height + format.blockHeight - 1) / format.blockHeight;
            if (build.data == nullptr || build.byteSize == 0 || build.rowPitch < minimumRowPitch ||
                build.slicePitch < minimumSlicePitch || build.slicePitch < static_cast<u64>(build.rowPitch) * rowCount ||
                build.byteSize != static_cast<u64>(build.slicePitch) * depth)
            {
                return texture::Result::InvalidSubresource;
            }

            dataCursor = AlignUp(dataCursor, DataAlignment);
            if (dataCursor == ~u64{0} || build.byteSize > ~u64{0} - dataCursor)
            {
                return texture::Result::LimitExceeded;
            }

            texture::SubresourceRecord record;
            record.mipLevel = build.mipLevel;
            record.arrayLayer = build.arrayLayer;
            record.face = build.face;
            record.flags = build.mipLevel >= description.mipTailFirstLevel ? texture::SubresourceFlags::MipTail :
                                                                            texture::SubresourceFlags::None;
            if (texture::HasFlag(description.flags, texture::TextureFlags::DirectGpuUpload))
            {
                record.flags = record.flags | texture::SubresourceFlags::DirectGpuUpload;
            }
            record.width = width;
            record.height = height;
            record.depth = depth;
            record.rowPitch = build.rowPitch;
            record.slicePitch = build.slicePitch;
            record.dataOffset = dataCursor;
            record.byteSize = build.byteSize;
            record.digest = crypto::Sha256(build.data, build.byteSize);
            data.records.PushBack(record);
            dataCursor += build.byteSize;
        }
        data.dataSize = dataCursor;
        return texture::Result::Success;
    }

    [[nodiscard]] texture::Result WriteMetadata(ByteArray& metadata, const texture::BuildDescription& description,
                                                const CanonicalData& data,
                                                const crypto::Digest256& contentFingerprint) noexcept
    {
        metadata.Clear();
        filesystem::MemoryFileWriter file(metadata);
        serialization::BinaryWriter writer(file);
        if (!writer.WriteU32(MetadataWireVersion) || !writer.WriteU8(static_cast<u8>(description.dimension)) ||
            !writer.WriteU8(static_cast<u8>(description.format)) || !writer.WriteU8(static_cast<u8>(description.colorSpace)) ||
            !writer.WriteU8(0) || !writer.WriteU32(description.width) || !writer.WriteU32(description.height) ||
            !writer.WriteU32(description.depth) || !writer.WriteU16(description.arrayLayers) ||
            !writer.WriteU8(description.mipCount) || !writer.WriteU8(description.mipTailFirstLevel) ||
            !writer.WriteU16(static_cast<u16>(description.flags)) || !writer.WriteU16(0) ||
            !WriteDigest(writer, description.sourceFingerprint) || !WriteDigest(writer, contentFingerprint) ||
            !writer.WriteU32(data.records.Size()) || !writer.WriteU32(0))
        {
            return WriterResult(writer);
        }
        for (const texture::SubresourceRecord& record : data.records)
        {
            if (!writer.WriteU8(record.mipLevel) || !writer.WriteU8(record.face) ||
                !writer.WriteU16(static_cast<u16>(record.flags)) || !writer.WriteU16(record.arrayLayer) || !writer.WriteU16(0) ||
                !writer.WriteU32(record.width) || !writer.WriteU32(record.height) || !writer.WriteU32(record.depth) ||
                !writer.WriteU32(record.rowPitch) || !writer.WriteU32(record.slicePitch) ||
                !writer.WriteU64(record.dataOffset) || !writer.WriteU64(record.byteSize) || !WriteDigest(writer, record.digest))
            {
                return WriterResult(writer);
            }
        }
        return writer.Flush() ? texture::Result::Success : WriterResult(writer);
    }

    [[nodiscard]] bool WriteZeroBytes(serialization::BinaryWriter& writer, u64 count, u64& checksum) noexcept
    {
        constexpr u8 zeros[4096]{};
        while (count != 0)
        {
            const usize batch = count > sizeof(zeros) ? sizeof(zeros) : static_cast<usize>(count);
            if (!writer.WriteBytes(zeros, batch))
            {
                return false;
            }
            checksum = serialization::Crc64(zeros, batch, checksum);
            count -= batch;
        }
        return true;
    }

    [[nodiscard]] texture::Result WriteDocument(filesystem::IFile& file, const ByteArray& metadata,
                                                const CanonicalData& data) noexcept
    {
        serialization::BinaryWriter writer(file);
        serialization::DocumentHeader header;
        header.magic = texture::TextureMagic;
        header.version = FileVersion;
        header.flags = serialization::DocumentFlags::Deterministic;
        header.sectionCount = 2;
        constexpr u8 emptyHeader[serialization::DocumentHeader::WireSize]{};
        constexpr u8 emptySectionTable[serialization::SectionDescriptor::WireSize * 2]{};
        if (!writer.WriteBytes(emptyHeader, sizeof(emptyHeader)))
        {
            return WriterResult(writer);
        }
        header.sectionTableOffset = writer.Position();
        if (!writer.WriteBytes(emptySectionTable, sizeof(emptySectionTable)) || !writer.Align(16))
        {
            return WriterResult(writer);
        }

        serialization::SectionDescriptor sections[2];
        sections[0].id = MetadataSection;
        sections[0].version = {1, 0};
        sections[0].alignmentLog2 = 4;
        sections[0].offset = writer.Position();
        sections[0].storedSize = metadata.Size();
        sections[0].logicalSize = metadata.Size();
        sections[0].storedCrc64 = serialization::Crc64(metadata.Data(), metadata.Size());
        if (!writer.WriteBytes(metadata.Data(), metadata.Size()) || !writer.Align(DataAlignment))
        {
            return WriterResult(writer);
        }

        sections[1].id = TextureDataSection;
        sections[1].version = {1, 0};
        sections[1].flags = serialization::SectionFlags::Streamable;
        sections[1].alignmentLog2 = DataAlignmentLog2;
        sections[1].offset = writer.Position();
        sections[1].storedSize = data.dataSize;
        sections[1].logicalSize = data.dataSize;

        u64 dataCursor = 0;
        u64 dataChecksum = 0;
        for (u32 index = 0; index < data.records.Size(); ++index)
        {
            const texture::SubresourceRecord& record = data.records[index];
            if (record.dataOffset < dataCursor ||
                !WriteZeroBytes(writer, record.dataOffset - dataCursor, dataChecksum) ||
                !writer.WriteBytes(data.builds[index].data, data.builds[index].byteSize))
            {
                return WriterResult(writer);
            }
            dataChecksum = serialization::Crc64(data.builds[index].data, data.builds[index].byteSize, dataChecksum);
            dataCursor = record.dataOffset + record.byteSize;
        }
        if (dataCursor != data.dataSize || !writer.Align(DataAlignment))
        {
            return texture::Result::InvalidState;
        }
        sections[1].storedCrc64 = dataChecksum;

        header.fileSize = writer.Position();
        if (!writer.Seek(header.sectionTableOffset))
        {
            return WriterResult(writer);
        }
        for (const serialization::SectionDescriptor& section : sections)
        {
            if (serialization::WriteSectionDescriptor(writer, section) != serialization::Result::Success)
            {
                return WriterResult(writer);
            }
        }
        if (!writer.Seek(0) || serialization::WriteDocumentHeader(writer, header) != serialization::Result::Success ||
            !writer.Seek(header.fileSize) || !writer.Flush())
        {
            return WriterResult(writer);
        }
        return texture::Result::Success;
    }

    [[nodiscard]] const serialization::SectionDescriptor* FindSection(
        const containers::ArraySpan<const serialization::SectionDescriptor> sections, const u32 id) noexcept
    {
        for (const serialization::SectionDescriptor& section : sections)
        {
            if (section.id == id)
            {
                return &section;
            }
        }
        return nullptr;
    }
} // namespace

namespace vanguard::textures
{
    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success: return "Success";
        case Result::InvalidArgument: return "InvalidArgument";
        case Result::InvalidState: return "InvalidState";
        case Result::InvalidMagic: return "InvalidMagic";
        case Result::UnsupportedVersion: return "UnsupportedVersion";
        case Result::InvalidLayout: return "InvalidLayout";
        case Result::IntegrityFailure: return "IntegrityFailure";
        case Result::LimitExceeded: return "LimitExceeded";
        case Result::InvalidDimension: return "InvalidDimension";
        case Result::InvalidFormat: return "InvalidFormat";
        case Result::InvalidMipChain: return "InvalidMipChain";
        case Result::InvalidSubresource: return "InvalidSubresource";
        case Result::DuplicateSubresource: return "DuplicateSubresource";
        case Result::MissingSubresource: return "MissingSubresource";
        case Result::BufferTooSmall: return "BufferTooSmall";
        case Result::IoFailure: return "IoFailure";
        default: return "Unknown";
        }
    }

    FormatInfo GetFormatInfo(const PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::R8UNorm: return {1, 1, 1, false, false};
        case PixelFormat::R8SNorm: return {1, 1, 1, false, false};
        case PixelFormat::R8UInt: return {1, 1, 1, false, false};
        case PixelFormat::R8G8UNorm: return {1, 1, 2, false, false};
        case PixelFormat::R8G8SNorm: return {1, 1, 2, false, false};
        case PixelFormat::R8G8UInt: return {1, 1, 2, false, false};
        case PixelFormat::R8G8B8A8UNorm: return {1, 1, 4, false, true};
        case PixelFormat::R8G8B8A8SNorm: return {1, 1, 4, false, false};
        case PixelFormat::R8G8B8A8UInt: return {1, 1, 4, false, false};
        case PixelFormat::B8G8R8A8UNorm: return {1, 1, 4, false, true};
        case PixelFormat::R16UNorm: return {1, 1, 2, false, false};
        case PixelFormat::R16SNorm: return {1, 1, 2, false, false};
        case PixelFormat::R16Float: return {1, 1, 2, false, false};
        case PixelFormat::R16G16UNorm: return {1, 1, 4, false, false};
        case PixelFormat::R16G16SNorm: return {1, 1, 4, false, false};
        case PixelFormat::R16G16Float: return {1, 1, 4, false, false};
        case PixelFormat::R16G16B16A16UNorm: return {1, 1, 8, false, false};
        case PixelFormat::R16G16B16A16SNorm: return {1, 1, 8, false, false};
        case PixelFormat::R16G16B16A16Float: return {1, 1, 8, false, false};
        case PixelFormat::R32Float: return {1, 1, 4, false, false};
        case PixelFormat::R32G32Float: return {1, 1, 8, false, false};
        case PixelFormat::R32G32B32A32Float: return {1, 1, 16, false, false};
        case PixelFormat::R10G10B10A2UNorm: return {1, 1, 4, false, false};
        case PixelFormat::R11G11B10Float: return {1, 1, 4, false, false};
        case PixelFormat::R9G9B9E5SharedExponent: return {1, 1, 4, false, false};
        case PixelFormat::BC1UNorm: return {4, 4, 8, true, true};
        case PixelFormat::BC2UNorm: return {4, 4, 16, true, true};
        case PixelFormat::BC3UNorm: return {4, 4, 16, true, true};
        case PixelFormat::BC4UNorm: return {4, 4, 8, true, false};
        case PixelFormat::BC4SNorm: return {4, 4, 8, true, false};
        case PixelFormat::BC5UNorm: return {4, 4, 16, true, false};
        case PixelFormat::BC5SNorm: return {4, 4, 16, true, false};
        case PixelFormat::BC6HUFloat: return {4, 4, 16, true, false};
        case PixelFormat::BC6HSFloat: return {4, 4, 16, true, false};
        case PixelFormat::BC7UNorm: return {4, 4, 16, true, true};
        default: return {};
        }
    }

    u32 CalculateMipCount(u32 width, u32 height, u32 depth) noexcept
    {
        if (width == 0 || height == 0 || depth == 0)
        {
            return 0;
        }
        u32 largest = width > height ? width : height;
        largest = largest > depth ? largest : depth;
        u32 count = 1;
        while (largest > 1)
        {
            largest >>= 1;
            ++count;
        }
        return count;
    }

    u32 CalculateMipExtent(const u32 baseExtent, const u8 mipLevel) noexcept
    {
        return mipLevel >= 32 ? 1 : ((baseExtent >> mipLevel) != 0 ? baseExtent >> mipLevel : 1);
    }

    u32 CalculateMinimumRowPitch(const PixelFormat format, const u32 width) noexcept
    {
        const FormatInfo info = GetFormatInfo(format);
        if (info.bytesPerBlock == 0 || width == 0)
        {
            return 0;
        }
        const u64 blockCount = (static_cast<u64>(width) + info.blockWidth - 1) / info.blockWidth;
        const u64 pitch = blockCount * info.bytesPerBlock;
        return pitch <= ~u32{0} ? static_cast<u32>(pitch) : 0;
    }

    u32 CalculateMinimumSlicePitch(const PixelFormat format, const u32 width, const u32 height) noexcept
    {
        const FormatInfo info = GetFormatInfo(format);
        const u32 rowPitch = CalculateMinimumRowPitch(format, width);
        if (rowPitch == 0 || height == 0)
        {
            return 0;
        }
        const u64 rowCount = (static_cast<u64>(height) + info.blockHeight - 1) / info.blockHeight;
        const u64 pitch = static_cast<u64>(rowPitch) * rowCount;
        return pitch <= ~u32{0} ? static_cast<u32>(pitch) : 0;
    }

    TextureFile::TextureFile() noexcept : m_subresources(memory::pools::Rendering::GetInstance())
    {
    }

    Result TextureFile::Open(filesystem::IFile& reader, const ReadLimits& limits) noexcept
    {
        Close();
        serialization::BinaryReader documentReader(reader);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 2;
        const serialization::Result headerResult =
            serialization::ReadDocumentHeader(documentReader, TextureMagic, {1, 0, 0}, documentLimits, header);
        if (headerResult != serialization::Result::Success)
        {
            return ConvertSerializationResult(headerResult);
        }

        containers::DynamicArray<serialization::SectionDescriptor> sections(memory::pools::Serialization::GetInstance());
        const serialization::Result tableResult = serialization::ReadSectionTable(documentReader, header, documentLimits, sections);
        if (tableResult != serialization::Result::Success)
        {
            return ConvertSerializationResult(tableResult);
        }
        const serialization::SectionDescriptor* metadataSection = FindSection(sections, MetadataSection);
        const serialization::SectionDescriptor* dataSection = FindSection(sections, TextureDataSection);
        if (metadataSection == nullptr || dataSection == nullptr || metadataSection->version != serialization::Version{1, 0} ||
            dataSection->version != serialization::Version{1, 0} || metadataSection->codec != serialization::Codec::None ||
            dataSection->codec != serialization::Codec::None || metadataSection->storedSize != metadataSection->logicalSize ||
            dataSection->storedSize != dataSection->logicalSize ||
            (static_cast<u32>(dataSection->flags) & static_cast<u32>(serialization::SectionFlags::Streamable)) == 0 ||
            metadataSection->storedSize > limits.maximumMetadataBytes || dataSection->storedSize > limits.maximumTextureBytes ||
            metadataSection->storedSize > ~u32{0})
        {
            return Result::InvalidLayout;
        }

        ByteArray metadata(memory::pools::Rendering::GetInstance());
        metadata.Resize(static_cast<u32>(metadataSection->storedSize));
        if (!documentReader.Seek(metadataSection->offset) || !documentReader.ReadBytes(metadata.Data(), metadata.Size()))
        {
            return ReaderResult(documentReader);
        }
        if (serialization::Crc64(metadata.Data(), metadata.Size()) != metadataSection->storedCrc64)
        {
            return Result::IntegrityFailure;
        }

        filesystem::MemoryFileReader metadataFile(metadata, 0);
        serialization::BinaryReader metadataReader(metadataFile);
        u32 wireVersion = 0;
        u8 dimension = 0;
        u8 format = 0;
        u8 colorSpace = 0;
        u8 reserved8 = 0;
        u16 flags = 0;
        u16 reserved16 = 0;
        u32 subresourceCount = 0;
        u32 reserved32 = 0;
        if (!metadataReader.ReadU32(wireVersion) || !metadataReader.ReadU8(dimension) || !metadataReader.ReadU8(format) ||
            !metadataReader.ReadU8(colorSpace) || !metadataReader.ReadU8(reserved8) || !metadataReader.ReadU32(m_width) ||
            !metadataReader.ReadU32(m_height) || !metadataReader.ReadU32(m_depth) || !metadataReader.ReadU16(m_arrayLayers) ||
            !metadataReader.ReadU8(m_mipCount) || !metadataReader.ReadU8(m_mipTailFirstLevel) ||
            !metadataReader.ReadU16(flags) || !metadataReader.ReadU16(reserved16) ||
            !ReadDigest(metadataReader, m_sourceFingerprint) || !ReadDigest(metadataReader, m_contentFingerprint) ||
            !metadataReader.ReadU32(subresourceCount) || !metadataReader.ReadU32(reserved32))
        {
            return ReaderResult(metadataReader);
        }
        m_dimension = static_cast<TextureDimension>(dimension);
        m_format = static_cast<PixelFormat>(format);
        m_colorSpace = static_cast<ColorSpace>(colorSpace);
        m_flags = static_cast<TextureFlags>(flags);
        if (wireVersion != MetadataWireVersion || reserved8 != 0 || reserved16 != 0 || reserved32 != 0 ||
            !IsValidDimension(m_dimension) || !IsValidFormat(m_format) || !IsValidColorSpace(m_colorSpace) ||
            (m_colorSpace == ColorSpace::SRgb && !GetFormatInfo(m_format).supportsSRgb) ||
            (flags & ~KnownTextureFlags) != 0 || m_width == 0 || m_height == 0 || m_depth == 0 || m_arrayLayers == 0 ||
            m_width > limits.maximumDimension || m_height > limits.maximumDimension || m_depth > limits.maximumDimension ||
            m_arrayLayers > limits.maximumArrayLayers || subresourceCount > limits.maximumSubresources)
        {
            return Result::InvalidLayout;
        }

        const u64 expectedCount = static_cast<u64>(m_mipCount) * m_arrayLayers * FaceCount(m_dimension);
        if (m_mipCount == 0 || m_mipCount > CalculateMipCount(m_width, m_height, m_depth) ||
            m_mipTailFirstLevel >= m_mipCount || (!HasFlag(m_flags, TextureFlags::Streamable) && m_mipTailFirstLevel != 0) ||
            expectedCount != subresourceCount ||
            (m_dimension == TextureDimension::Texture1D && (m_height != 1 || m_depth != 1)) ||
            (m_dimension == TextureDimension::Texture2D && m_depth != 1) ||
            (m_dimension == TextureDimension::Texture3D && m_arrayLayers != 1) ||
            (m_dimension == TextureDimension::Cube && (m_width != m_height || m_depth != 1)))
        {
            return Result::InvalidLayout;
        }

        m_subresources.Reserve(subresourceCount);
        u64 previousEnd = 0;
        const u8 faceCount = FaceCount(m_dimension);
        for (u32 index = 0; index < subresourceCount; ++index)
        {
            SubresourceRecord record;
            u16 recordFlags = 0;
            u16 recordReserved = 0;
            if (!metadataReader.ReadU8(record.mipLevel) || !metadataReader.ReadU8(record.face) ||
                !metadataReader.ReadU16(recordFlags) || !metadataReader.ReadU16(record.arrayLayer) ||
                !metadataReader.ReadU16(recordReserved) || !metadataReader.ReadU32(record.width) ||
                !metadataReader.ReadU32(record.height) || !metadataReader.ReadU32(record.depth) ||
                !metadataReader.ReadU32(record.rowPitch) || !metadataReader.ReadU32(record.slicePitch) ||
                !metadataReader.ReadU64(record.dataOffset) || !metadataReader.ReadU64(record.byteSize) ||
                !ReadDigest(metadataReader, record.digest))
            {
                return ReaderResult(metadataReader);
            }
            record.flags = static_cast<SubresourceFlags>(recordFlags);
            const u8 expectedMip = static_cast<u8>(index / (static_cast<u32>(m_arrayLayers) * faceCount));
            const u32 withinMip = index % (static_cast<u32>(m_arrayLayers) * faceCount);
            const u16 expectedLayer = static_cast<u16>(withinMip / faceCount);
            const u8 expectedFace = static_cast<u8>(withinMip % faceCount);
            const FormatInfo info = GetFormatInfo(m_format);
            const u32 rowCount = (record.height + info.blockHeight - 1) / info.blockHeight;
            const bool expectedMipTail = expectedMip >= m_mipTailFirstLevel;
            const bool expectedDirectUpload = HasFlag(m_flags, TextureFlags::DirectGpuUpload);
            if (recordReserved != 0 || (recordFlags & ~KnownSubresourceFlags) != 0 || record.mipLevel != expectedMip ||
                record.arrayLayer != expectedLayer || record.face != expectedFace ||
                record.width != CalculateMipExtent(m_width, expectedMip) ||
                record.height != CalculateMipExtent(m_height, expectedMip) ||
                record.depth != CalculateMipExtent(m_depth, expectedMip) ||
                HasFlag(record.flags, SubresourceFlags::MipTail) != expectedMipTail ||
                HasFlag(record.flags, SubresourceFlags::DirectGpuUpload) != expectedDirectUpload ||
                record.rowPitch < CalculateMinimumRowPitch(m_format, record.width) ||
                record.slicePitch < CalculateMinimumSlicePitch(m_format, record.width, record.height) ||
                record.slicePitch < static_cast<u64>(record.rowPitch) * rowCount ||
                record.byteSize != static_cast<u64>(record.slicePitch) * record.depth ||
                record.byteSize > limits.maximumSubresourceBytes || record.dataOffset < previousEnd ||
                (record.dataOffset & (DataAlignment - 1)) != 0 || record.dataOffset > dataSection->storedSize ||
                record.byteSize > dataSection->storedSize - record.dataOffset)
            {
                return Result::InvalidSubresource;
            }
            previousEnd = record.dataOffset + record.byteSize;
            m_subresources.PushBack(record);
        }
        if (metadataReader.Position() != metadataReader.Size() || metadata.Size() < ContentFingerprintOffset + crypto::Digest256::ByteCount)
        {
            return Result::InvalidLayout;
        }

        const crypto::Digest256 declaredFingerprint = m_contentFingerprint;
        for (u32 index = 0; index < crypto::Digest256::ByteCount; ++index)
        {
            metadata[static_cast<u32>(ContentFingerprintOffset) + index] = 0;
        }
        if (crypto::Sha256(metadata.Data(), metadata.Size()) != declaredFingerprint)
        {
            return Result::IntegrityFailure;
        }

        m_dataOffset = dataSection->offset;
        m_dataSize = dataSection->storedSize;
        m_open = true;
        return Result::Success;
    }

    void TextureFile::Close() noexcept
    {
        m_open = false;
        m_dimension = TextureDimension::Texture2D;
        m_format = PixelFormat::R8G8B8A8UNorm;
        m_colorSpace = ColorSpace::Linear;
        m_flags = TextureFlags::None;
        m_width = m_height = m_depth = 0;
        m_arrayLayers = 0;
        m_mipCount = m_mipTailFirstLevel = 0;
        m_sourceFingerprint = {};
        m_contentFingerprint = {};
        m_dataOffset = m_dataSize = 0;
        m_subresources.Clear();
    }

    bool TextureFile::IsOpen() const noexcept { return m_open; }
    TextureDimension TextureFile::Dimension() const noexcept { return m_dimension; }
    PixelFormat TextureFile::Format() const noexcept { return m_format; }
    ColorSpace TextureFile::Space() const noexcept { return m_colorSpace; }
    TextureFlags TextureFile::Flags() const noexcept { return m_flags; }
    u32 TextureFile::Width() const noexcept { return m_width; }
    u32 TextureFile::Height() const noexcept { return m_height; }
    u32 TextureFile::Depth() const noexcept { return m_depth; }
    u16 TextureFile::ArrayLayers() const noexcept { return m_arrayLayers; }
    u8 TextureFile::MipCount() const noexcept { return m_mipCount; }
    u8 TextureFile::MipTailFirstLevel() const noexcept { return m_mipTailFirstLevel; }
    const crypto::Digest256& TextureFile::SourceFingerprint() const noexcept { return m_sourceFingerprint; }
    const crypto::Digest256& TextureFile::ContentFingerprint() const noexcept { return m_contentFingerprint; }
    containers::ArraySpan<const SubresourceRecord> TextureFile::Subresources() const noexcept { return m_subresources; }
    u64 TextureFile::TextureDataOffset() const noexcept { return m_dataOffset; }
    u64 TextureFile::TextureDataSize() const noexcept { return m_dataSize; }

    u32 TextureFile::FindSubresource(const u8 mipLevel, const u16 arrayLayer, const u8 face) const noexcept
    {
        if (!m_open || mipLevel >= m_mipCount || arrayLayer >= m_arrayLayers || face >= FaceCount(m_dimension))
        {
            return InvalidSubresourceIndex;
        }
        return (static_cast<u32>(mipLevel) * m_arrayLayers + arrayLayer) * FaceCount(m_dimension) + face;
    }

    Result TextureFile::ReadSubresource(filesystem::IFile& reader, const u32 index, void* destination,
                                        const usize capacity) const noexcept
    {
        if (!m_open || index >= m_subresources.Size() || destination == nullptr)
        {
            return Result::InvalidArgument;
        }
        const SubresourceRecord& record = m_subresources[index];
        if (record.byteSize > capacity)
        {
            return Result::BufferTooSmall;
        }
        serialization::BinaryReader binaryReader(reader);
        if (!binaryReader.Seek(m_dataOffset + record.dataOffset) ||
            !binaryReader.ReadBytes(destination, static_cast<usize>(record.byteSize)))
        {
            return ReaderResult(binaryReader);
        }
        return crypto::Sha256(destination, static_cast<usize>(record.byteSize)) == record.digest ? Result::Success :
                                                                                                 Result::IntegrityFailure;
    }

    Result WriteTexture(filesystem::IFile& writer, const BuildDescription& description) noexcept
    {
        CanonicalData data;
        const Result buildResult = BuildCanonicalData(description, data);
        if (buildResult != Result::Success)
        {
            return buildResult;
        }
        ByteArray metadata(memory::pools::Rendering::GetInstance());
        crypto::Digest256 emptyFingerprint;
        Result metadataResult = WriteMetadata(metadata, description, data, emptyFingerprint);
        if (metadataResult != Result::Success)
        {
            return metadataResult;
        }
        const crypto::Digest256 contentFingerprint = crypto::Sha256(metadata.Data(), metadata.Size());
        metadataResult = WriteMetadata(metadata, description, data, contentFingerprint);
        return metadataResult == Result::Success ? WriteDocument(writer, metadata, data) : metadataResult;
    }

    Result BuildStorageSegments(const TextureFile& texture, const u64 documentSize,
                                containers::DynamicArray<StorageSegment>& segments, const u32 maximumSegments) noexcept
    {
        segments.Clear();
        if (!texture.IsOpen() || documentSize < texture.TextureDataOffset() + texture.TextureDataSize() ||
            texture.Subresources().Size() == 0 || texture.Subresources().Size() + 1ull > maximumSegments)
        {
            return Result::InvalidArgument;
        }
        const u64 firstSubresourceOffset = texture.TextureDataOffset() + texture.Subresources()[0].dataOffset;
        if (firstSubresourceOffset == 0 || firstSubresourceOffset > documentSize)
        {
            return Result::InvalidLayout;
        }
        StorageSegment metadata;
        metadata.offset = 0;
        metadata.byteSize = firstSubresourceOffset;
        metadata.alignmentLog2 = 4;
        metadata.flags = StorageSegmentFlags::Metadata;
        segments.PushBack(metadata);
        for (u32 index = 0; index < texture.Subresources().Size(); ++index)
        {
            const SubresourceRecord& record = texture.Subresources()[index];
            StorageSegment segment;
            segment.offset = texture.TextureDataOffset() + record.dataOffset;
            const u64 end = index + 1u < texture.Subresources().Size()
                ? texture.TextureDataOffset() + texture.Subresources()[index + 1u].dataOffset
                : documentSize;
            if (segment.offset >= end || record.byteSize > end - segment.offset)
            {
                segments.Clear();
                return Result::InvalidLayout;
            }
            segment.byteSize = end - segment.offset;
            segment.alignmentLog2 = DataAlignmentLog2;
            segment.flags = StorageSegmentFlags::Streamable;
            if (HasFlag(record.flags, SubresourceFlags::MipTail))
            {
                segment.flags = segment.flags | StorageSegmentFlags::RequiredForMipTail;
            }
            segment.subresource = index;
            segments.PushBack(segment);
        }
        return Result::Success;
    }
} // namespace vanguard::textures

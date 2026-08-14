#include <vanguard/packages/packages.hpp>

#include <vanguard/packages/package_codec_backend.hpp>

#include <vanguard/crypto/crypto.hpp>

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
    using namespace vanguard;
    using namespace vanguard::packages;
    namespace vgser = vanguard::serialization;

    constexpr u32 IndexMagic = vgser::MakeFourCC('V', 'P', 'K', 'I');
    constexpr u16 IndexHeaderWireSize = 32;
    constexpr u16 PackageSetHeaderWireSize = PackageSet::HeaderWireSize;
    constexpr u8 PackageSetEncodingVersion = 1;
    constexpr u8 LittleEndian = 1;
    constexpr u16 DependencyWireSize = Dependency::WireSize;
    constexpr u32 KnownPackageFlags = static_cast<u32>(PackageFlags::Deterministic) | static_cast<u32>(PackageFlags::HasDebugPaths) |
                                      static_cast<u32>(PackageFlags::HasPackageSet);
    constexpr u32 KnownPackageSetEntryFlags = static_cast<u32>(PackageSetEntryFlags::Required) |
                                              static_cast<u32>(PackageSetEntryFlags::Optional) |
                                              static_cast<u32>(PackageSetEntryFlags::Override);
    constexpr u32 KnownResourceFlags = static_cast<u32>(ResourceFlags::Startup) | static_cast<u32>(ResourceFlags::Optional) |
                                       static_cast<u32>(ResourceFlags::Streamable) | static_cast<u32>(ResourceFlags::EditorOnly);
    constexpr u8 KnownSegmentFlags =
        static_cast<u8>(SegmentFlags::Inline) | static_cast<u8>(SegmentFlags::Streamable) | static_cast<u8>(SegmentFlags::MemoryResident);

    [[nodiscard]] constexpr bool HasFlag(const PackageFlags value, const PackageFlags flag) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
    }

    [[nodiscard]] constexpr bool IsPowerOfTwo(const u64 value) noexcept
    {
        return value != 0 && (value & (value - 1u)) == 0;
    }

    [[nodiscard]] constexpr bool AddOverflow(const u64 left, const u64 right) noexcept
    {
        return right > std::numeric_limits<u64>::max() - left;
    }

    [[nodiscard]] constexpr bool MultiplyOverflow(const u64 left, const u64 right) noexcept
    {
        return left != 0 && right > std::numeric_limits<u64>::max() / left;
    }

    [[nodiscard]] u64 DigestWord(const crypto::Digest256& digest, const u32 word) noexcept
    {
        const u32 offset = word * 8u;
        return static_cast<u64>(digest.bytes[offset]) | (static_cast<u64>(digest.bytes[offset + 1u]) << 8u) |
               (static_cast<u64>(digest.bytes[offset + 2u]) << 16u) | (static_cast<u64>(digest.bytes[offset + 3u]) << 24u) |
               (static_cast<u64>(digest.bytes[offset + 4u]) << 32u) | (static_cast<u64>(digest.bytes[offset + 5u]) << 40u) |
               (static_cast<u64>(digest.bytes[offset + 6u]) << 48u) | (static_cast<u64>(digest.bytes[offset + 7u]) << 56u);
    }

    [[nodiscard]] Result WriteHeader(vgser::BinaryWriter& writer, const PackageHeader& header) noexcept
    {
        if (header.fileSize < PackageHeader::WireSize || header.indexOffset < PackageHeader::WireSize ||
            AddOverflow(header.indexOffset, header.indexSize) || header.indexOffset + header.indexSize != header.fileSize)
        {
            return Result::InvalidArgument;
        }

        u8 bytes[PackageHeader::WireSize] = {};
        filesystem::MemoryFileWriterExternalBuffer memoryWriter(bytes, static_cast<u32>(sizeof(bytes)));
        vgser::BinaryWriter encoded(memoryWriter);
        const bool written =
            encoded.WriteU32(PackageMagic) && encoded.WriteU8(PackageHeader::LittleEndian) &&
            encoded.WriteU8(PackageHeader::EncodingVersion) && encoded.WriteU16(PackageHeader::WireSize) &&
            encoded.WriteU16(header.version.major) && encoded.WriteU16(header.version.minor) &&
            encoded.WriteU32(static_cast<u32>(header.flags)) && encoded.WriteU64(header.fileSize) && encoded.WriteU64(header.indexOffset) &&
            encoded.WriteU64(header.indexSize) && encoded.WriteU64(header.packageId) && encoded.WriteU64(header.buildId) &&
            encoded.WriteU32(header.resourceCount) && encoded.WriteU32(header.segmentCount) && encoded.WriteU32(header.dependencyCount) &&
            encoded.WriteU32(header.debugPathBytes) && encoded.WriteU64(header.indexCrc64) && encoded.WriteU64(header.packageSetOffset) &&
            encoded.WriteU32(vgser::Crc32(bytes, 88)) && encoded.WriteU32(0);
        if (!written || encoded.Position() != sizeof(bytes))
        {
            return Result::IoFailure;
        }
        if (!writer.WriteBytesAt(0, bytes, sizeof(bytes)))
        {
            return Result::IoFailure;
        }
        return Result::Success;
    }

    [[nodiscard]] Result ReadHeader(filesystem::IFile& file, const ReadLimits& limits, PackageHeader& header) noexcept
    {
        if (file.GetSize() < PackageHeader::WireSize)
        {
            return Result::InvalidLayout;
        }

        u8 bytes[PackageHeader::WireSize] = {};
        vgser::BinaryReader source(file);
        if (!source.Seek(0) || !source.ReadBytes(bytes, sizeof(bytes)))
        {
            return Result::IoFailure;
        }
        const u32 expectedCrc = static_cast<u32>(bytes[88]) | (static_cast<u32>(bytes[89]) << 8u) | (static_cast<u32>(bytes[90]) << 16u) |
                                (static_cast<u32>(bytes[91]) << 24u);
        if (vgser::Crc32(bytes, 88) != expectedCrc)
        {
            return Result::IntegrityFailure;
        }

        filesystem::MemoryFileReader memoryReader(bytes, sizeof(bytes), 0);
        vgser::BinaryReader reader(memoryReader);
        u32 magic = 0;
        u8 byteOrder = 0;
        u8 encoding = 0;
        u16 headerSize = 0;
        u32 flags = 0;
        u32 ignoredCrc = 0;
        u32 reserved32 = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU8(byteOrder) || !reader.ReadU8(encoding) || !reader.ReadU16(headerSize) ||
            !reader.ReadU16(header.version.major) || !reader.ReadU16(header.version.minor) || !reader.ReadU32(flags) ||
            !reader.ReadU64(header.fileSize) || !reader.ReadU64(header.indexOffset) || !reader.ReadU64(header.indexSize) ||
            !reader.ReadU64(header.packageId) || !reader.ReadU64(header.buildId) || !reader.ReadU32(header.resourceCount) ||
            !reader.ReadU32(header.segmentCount) || !reader.ReadU32(header.dependencyCount) || !reader.ReadU32(header.debugPathBytes) ||
            !reader.ReadU64(header.indexCrc64) || !reader.ReadU64(header.packageSetOffset) || !reader.ReadU32(ignoredCrc) ||
            !reader.ReadU32(reserved32))
        {
            return Result::InvalidLayout;
        }

        if (magic != PackageMagic)
        {
            return Result::InvalidMagic;
        }
        if (byteOrder != PackageHeader::LittleEndian || encoding != PackageHeader::EncodingVersion ||
            headerSize != PackageHeader::WireSize || reserved32 != 0 || (flags & ~KnownPackageFlags) != 0)
        {
            return Result::InvalidLayout;
        }
        if (header.version.major != 1 || header.version.minor != 0)
        {
            return Result::UnsupportedVersion;
        }
        header.flags = static_cast<PackageFlags>(flags);
        const bool hasPackageSet = HasFlag(header.flags, PackageFlags::HasPackageSet);
        if (hasPackageSet != (header.packageSetOffset != 0) || (hasPackageSet && header.packageSetOffset != PackageHeader::WireSize))
        {
            return Result::InvalidLayout;
        }

        if (header.fileSize != file.GetSize())
        {
            return Result::InvalidLayout;
        }
        if (header.fileSize > limits.maximumFileSize || header.indexSize > limits.maximumIndexSize ||
            header.resourceCount > limits.maximumResources || header.segmentCount > limits.maximumSegments ||
            header.dependencyCount > limits.maximumDependencies || header.debugPathBytes > limits.maximumDebugPathBytes)
        {
            return Result::LimitExceeded;
        }
        if (header.indexOffset < PackageHeader::WireSize || AddOverflow(header.indexOffset, header.indexSize) ||
            header.indexOffset + header.indexSize != header.fileSize)
        {
            return Result::InvalidLayout;
        }
        return Result::Success;
    }

    [[nodiscard]] constexpr u64 PackageSetRecordSize(const u32 packageCount) noexcept
    {
        return PackageSetHeaderWireSize + static_cast<u64>(packageCount) * PackageSetEntry::WireSize;
    }

    [[nodiscard]] bool HasExactlyOneAvailabilityFlag(const PackageSetEntryFlags flags) noexcept
    {
        const u32 availability = static_cast<u32>(flags) &
                                 (static_cast<u32>(PackageSetEntryFlags::Required) | static_cast<u32>(PackageSetEntryFlags::Optional));
        return availability == static_cast<u32>(PackageSetEntryFlags::Required) ||
               availability == static_cast<u32>(PackageSetEntryFlags::Optional);
    }

    [[nodiscard]] bool ValidatePackageSetEntry(const PackageSetEntry& entry, const u32 previousNumber) noexcept
    {
        bool hasDigest = false;
        for (const u8 byte : entry.contentDigest)
        {
            hasDigest = hasDigest || byte != 0;
        }
        return entry.packageNumber > previousNumber && entry.packageNumber <= MaximumPackageNumber &&
               (static_cast<u32>(entry.flags) & ~KnownPackageSetEntryFlags) == 0 && HasExactlyOneAvailabilityFlag(entry.flags) &&
               entry.packageId != 0 && entry.buildId != 0 && entry.fileSize >= PackageHeader::WireSize && entry.indexCrc64 != 0 &&
               hasDigest;
    }

    [[nodiscard]] Result RejectPackageSet(PackageSet& output, const Result result) noexcept
    {
        output.Reset();
        return result;
    }

    [[nodiscard]] Result WritePackageSetRecord(vgser::BinaryWriter& writer, const PackageSetBuild& build, const u64 buildId,
                                               u64& recordOffset, u64& recordEnd) noexcept
    {
        if (build.gameId == 0 || buildId == 0 || build.targetPlatformId == 0 || build.startupWorld == InvalidResourceId ||
            build.startupWorldType == resources::InvalidResourceTypeId || build.defaultInput == InvalidResourceId ||
            build.defaultInputType == resources::InvalidResourceTypeId || build.packages.Count() > MaximumPackageNumber)
        {
            return Result::InvalidArgument;
        }

        u32 previousNumber = 0;
        for (const PackageSetEntry& entry : build.packages)
        {
            if (!ValidatePackageSetEntry(entry, previousNumber))
            {
                return Result::InvalidArgument;
            }
            previousNumber = entry.packageNumber;
        }

        containers::DynamicArray<u8> entries(memory::pools::Resources::GetInstance());
        entries.Reserve(build.packages.Count() * PackageSetEntry::WireSize);
        filesystem::MemoryFileWriter entriesFile(entries);
        vgser::BinaryWriter entriesWriter(entriesFile);
        for (const PackageSetEntry& entry : build.packages)
        {
            if (!entriesWriter.WriteU32(entry.packageNumber) || !entriesWriter.WriteU32(static_cast<u32>(entry.flags)) ||
                !entriesWriter.WriteI32(entry.mountPriority) || !entriesWriter.WriteU32(0) || !entriesWriter.WriteU64(entry.packageId) ||
                !entriesWriter.WriteU64(entry.buildId) || !entriesWriter.WriteU64(entry.fileSize) ||
                !entriesWriter.WriteU64(entry.indexCrc64) ||
                !entriesWriter.WriteBytes(entry.contentDigest, sizeof(entry.contentDigest)))
            {
                return Result::IoFailure;
            }
        }

        const u64 recordSize = PackageSetRecordSize(build.packages.Count());
        if (entriesWriter.Position() != recordSize - PackageSetHeaderWireSize)
        {
            return Result::InvalidLayout;
        }

        u8 headerBytes[PackageSetHeaderWireSize]{};
        filesystem::MemoryFileWriterExternalBuffer headerFile(headerBytes, sizeof(headerBytes));
        vgser::BinaryWriter headerWriter(headerFile);
        constexpr u8 emptyEntries = 0;
        const void* const entryBytes = entries.Empty() ? &emptyEntries : entries.Data();
        const u64 entriesCrc64 = vgser::Crc64(entryBytes, entries.Size());
        const bool headerWritten =
            headerWriter.WriteU32(PackageSetMagic) && headerWriter.WriteU8(LittleEndian) &&
            headerWriter.WriteU8(PackageSetEncodingVersion) && headerWriter.WriteU16(PackageSetHeaderWireSize) &&
            headerWriter.WriteU16(1) && headerWriter.WriteU16(1) && headerWriter.WriteU32(0) && headerWriter.WriteU64(recordSize) &&
            headerWriter.WriteU64(build.gameId) && headerWriter.WriteU64(buildId) && headerWriter.WriteU64(build.startupWorld) &&
            headerWriter.WriteU32(build.startupWorldType) && headerWriter.WriteU32(build.targetPlatformId) &&
            headerWriter.WriteU64(build.defaultInput) && headerWriter.WriteU32(build.defaultInputType) &&
            headerWriter.WriteU32(build.packages.Count()) && headerWriter.WriteU16(PackageSetEntry::WireSize) &&
            headerWriter.WriteU16(0) && headerWriter.WriteU64(entriesCrc64) && headerWriter.WriteU32(0) &&
            headerWriter.WriteU32(vgser::Crc32(headerBytes, 88)) && headerWriter.WriteU32(0);
        if (!headerWritten || headerWriter.Position() != sizeof(headerBytes))
        {
            return Result::IoFailure;
        }

        recordOffset = writer.Position();
        if (!writer.WriteBytes(headerBytes, sizeof(headerBytes)) || !writer.WriteBytes(entries.Data(), entries.Size()))
        {
            return Result::IoFailure;
        }
        recordEnd = writer.Position();
        return Result::Success;
    }

    [[nodiscard]] Result ReadPackageSetRecord(filesystem::IFile& file, const PackageHeader& packageHeader,
                                              const PackageSetReadLimits& limits, PackageSet& output) noexcept
    {
        output.Reset();
        if (!HasFlag(packageHeader.flags, PackageFlags::HasPackageSet))
        {
            return RejectPackageSet(output, Result::MissingPackageSet);
        }
        if (limits.maximumRecordSize < PackageSetHeaderWireSize || packageHeader.packageSetOffset != PackageHeader::WireSize)
        {
            return RejectPackageSet(output, Result::InvalidArgument);
        }

        u8 headerBytes[PackageSetHeaderWireSize]{};
        vgser::BinaryReader source(file);
        if (!source.Seek(packageHeader.packageSetOffset) || !source.ReadBytes(headerBytes, sizeof(headerBytes)))
        {
            return RejectPackageSet(output, Result::IoFailure);
        }
        const u32 expectedHeaderCrc = static_cast<u32>(headerBytes[88]) | (static_cast<u32>(headerBytes[89]) << 8u) |
                                      (static_cast<u32>(headerBytes[90]) << 16u) | (static_cast<u32>(headerBytes[91]) << 24u);
        if (vgser::Crc32(headerBytes, 88) != expectedHeaderCrc)
        {
            return RejectPackageSet(output, Result::IntegrityFailure);
        }

        filesystem::MemoryFileReader headerFile(headerBytes, sizeof(headerBytes), 0);
        vgser::BinaryReader reader(headerFile);
        u32 magic = 0;
        u8 byteOrder = 0;
        u8 encoding = 0;
        u16 headerSize = 0;
        u32 flags = 0;
        u64 recordSize = 0;
        u32 packageCount = 0;
        u16 entrySize = 0;
        u16 reserved16 = 0;
        u64 entriesCrc64 = 0;
        u32 reservedBeforeCrc = 0;
        u32 ignoredHeaderCrc = 0;
        u32 reserved32 = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU8(byteOrder) || !reader.ReadU8(encoding) || !reader.ReadU16(headerSize) ||
            !reader.ReadU16(output.version.major) || !reader.ReadU16(output.version.minor) || !reader.ReadU32(flags) ||
            !reader.ReadU64(recordSize) || !reader.ReadU64(output.gameId) || !reader.ReadU64(output.buildId) ||
            !reader.ReadU64(output.startupWorld) || !reader.ReadU32(output.startupWorldType) ||
            !reader.ReadU32(output.targetPlatformId) || !reader.ReadU64(output.defaultInput) ||
            !reader.ReadU32(output.defaultInputType) || !reader.ReadU32(packageCount) || !reader.ReadU16(entrySize) ||
            !reader.ReadU16(reserved16) || !reader.ReadU64(entriesCrc64) || !reader.ReadU32(reservedBeforeCrc) ||
            !reader.ReadU32(ignoredHeaderCrc) || !reader.ReadU32(reserved32))
        {
            return RejectPackageSet(output, Result::InvalidLayout);
        }
        if (magic != PackageSetMagic)
        {
            return RejectPackageSet(output, Result::InvalidMagic);
        }
        if (output.version != vgser::Version{1, 1})
        {
            return RejectPackageSet(output, Result::UnsupportedVersion);
        }
        if (byteOrder != LittleEndian || encoding != PackageSetEncodingVersion || headerSize != PackageSetHeaderWireSize ||
            flags != 0 || entrySize != PackageSetEntry::WireSize || reserved16 != 0 || reservedBeforeCrc != 0 ||
            reserved32 != 0 || output.gameId == 0 ||
            output.buildId == 0 ||
            output.buildId != packageHeader.buildId || output.targetPlatformId == 0 || output.startupWorld == InvalidResourceId ||
            output.startupWorldType == resources::InvalidResourceTypeId || output.defaultInput == InvalidResourceId ||
            output.defaultInputType == resources::InvalidResourceTypeId)
        {
            return RejectPackageSet(output, Result::InvalidLayout);
        }
        if (packageCount > MaximumPackageNumber || packageCount > limits.maximumPackages || recordSize > limits.maximumRecordSize)
        {
            return RejectPackageSet(output, Result::LimitExceeded);
        }
        if (recordSize != PackageSetRecordSize(packageCount) || AddOverflow(packageHeader.packageSetOffset, recordSize) ||
            packageHeader.packageSetOffset + recordSize > packageHeader.indexOffset ||
            recordSize - PackageSetHeaderWireSize > std::numeric_limits<u32>::max())
        {
            return RejectPackageSet(output, Result::InvalidLayout);
        }

        containers::DynamicArray<u8> entries(memory::pools::Resources::GetInstance());
        entries.Resize(static_cast<u32>(recordSize - PackageSetHeaderWireSize));
        if (!source.Seek(packageHeader.packageSetOffset + PackageSetHeaderWireSize) ||
            !source.ReadBytes(entries.Data(), entries.Size()))
        {
            return RejectPackageSet(output, Result::IoFailure);
        }
        constexpr u8 emptyEntries = 0;
        const void* const entryBytes = entries.Empty() ? &emptyEntries : entries.Data();
        if (vgser::Crc64(entryBytes, entries.Size()) != entriesCrc64)
        {
            return RejectPackageSet(output, Result::IntegrityFailure);
        }

        filesystem::MemoryFileReader entriesFile(entries, 0);
        vgser::BinaryReader entriesReader(entriesFile);
        output.packages.Resize(packageCount);
        u32 previousNumber = 0;
        for (PackageSetEntry& entry : output.packages)
        {
            u32 entryFlags = 0;
            u32 entryReserved = 0;
            if (!entriesReader.ReadU32(entry.packageNumber) || !entriesReader.ReadU32(entryFlags) ||
                !entriesReader.ReadI32(entry.mountPriority) || !entriesReader.ReadU32(entryReserved) ||
                !entriesReader.ReadU64(entry.packageId) || !entriesReader.ReadU64(entry.buildId) ||
                !entriesReader.ReadU64(entry.fileSize) || !entriesReader.ReadU64(entry.indexCrc64) ||
                !entriesReader.ReadBytes(entry.contentDigest, sizeof(entry.contentDigest)) || entryReserved != 0)
            {
                return RejectPackageSet(output, Result::InvalidLayout);
            }
            entry.flags = static_cast<PackageSetEntryFlags>(entryFlags);
            if (!ValidatePackageSetEntry(entry, previousNumber))
            {
                return RejectPackageSet(output, Result::InvalidLayout);
            }
            previousNumber = entry.packageNumber;
        }
        return Result::Success;
    }

    [[nodiscard]] bool WriteIndexHeader(vgser::BinaryWriter& writer, const PackageHeader& header) noexcept
    {
        return writer.WriteU32(IndexMagic) && writer.WriteU16(header.version.major) && writer.WriteU16(header.version.minor) &&
               writer.WriteU16(IndexHeaderWireSize) && writer.WriteU16(Resource::WireSize) && writer.WriteU16(Segment::WireSize) &&
               writer.WriteU16(DependencyWireSize) && writer.WriteU32(header.resourceCount) && writer.WriteU32(header.segmentCount) &&
               writer.WriteU32(header.dependencyCount) && writer.WriteU32(header.debugPathBytes);
    }

    [[nodiscard]] bool WriteResource(vgser::BinaryWriter& writer, const Resource& resource) noexcept
    {
        return writer.WriteU64(resource.id) && writer.WriteU32(resource.type) && writer.WriteU32(static_cast<u32>(resource.flags)) &&
               writer.WriteU64(resource.logicalSize) && writer.WriteU32(resource.firstSegment) && writer.WriteU32(resource.segmentCount) &&
               writer.WriteU32(resource.firstDependency) && writer.WriteU32(resource.dependencyCount) &&
               writer.WriteU32(resource.debugPathOffset) && writer.WriteU32(resource.debugPathSize) &&
               writer.WriteU64(resource.contentCrc64) && writer.WriteU64(0);
    }

    [[nodiscard]] bool WriteDependency(vgser::BinaryWriter& writer, const Dependency& dependency) noexcept
    {
        return writer.WriteU64(dependency.id) && writer.WriteU32(dependency.type) && writer.WriteU8(static_cast<u8>(dependency.kind)) &&
               writer.WriteU8(0) && writer.WriteU16(0);
    }

    [[nodiscard]] bool ReadDependency(vgser::BinaryReader& reader, Dependency& dependency) noexcept
    {
        u8 kind = 0;
        u8 reserved8 = 0;
        u16 reserved16 = 0;
        if (!reader.ReadU64(dependency.id) || !reader.ReadU32(dependency.type) || !reader.ReadU8(kind) || !reader.ReadU8(reserved8) ||
            !reader.ReadU16(reserved16) || kind > static_cast<u8>(resources::DependencyKind::Soft) || reserved8 != 0 || reserved16 != 0)
        {
            return false;
        }
        dependency.kind = static_cast<resources::DependencyKind>(kind);
        return true;
    }

    [[nodiscard]] bool ReadResource(vgser::BinaryReader& reader, Resource& resource) noexcept
    {
        u32 flags = 0;
        u64 reserved = 0;
        if (!reader.ReadU64(resource.id) || !reader.ReadU32(resource.type) || !reader.ReadU32(flags) ||
            !reader.ReadU64(resource.logicalSize) || !reader.ReadU32(resource.firstSegment) || !reader.ReadU32(resource.segmentCount) ||
            !reader.ReadU32(resource.firstDependency) || !reader.ReadU32(resource.dependencyCount) ||
            !reader.ReadU32(resource.debugPathOffset) || !reader.ReadU32(resource.debugPathSize) ||
            !reader.ReadU64(resource.contentCrc64) || !reader.ReadU64(reserved))
        {
            return false;
        }
        if ((flags & ~KnownResourceFlags) != 0 || reserved != 0)
        {
            return false;
        }
        resource.flags = static_cast<ResourceFlags>(flags);
        return true;
    }

    [[nodiscard]] bool WriteSegment(vgser::BinaryWriter& writer, const Segment& segment) noexcept
    {
        return writer.WriteU64(segment.offset) && writer.WriteU64(segment.storedSize) && writer.WriteU64(segment.logicalSize) &&
               writer.WriteU64(segment.storedCrc64) && writer.WriteU8(static_cast<u8>(segment.codec)) &&
               writer.WriteU8(segment.alignmentLog2) && writer.WriteU8(static_cast<u8>(segment.flags)) && writer.WriteU8(0) &&
               writer.WriteU32(0);
    }

    [[nodiscard]] bool ReadSegment(vgser::BinaryReader& reader, Segment& segment) noexcept
    {
        u8 codec = 0;
        u8 flags = 0;
        u8 reserved8 = 0;
        u32 reserved32 = 0;
        if (!reader.ReadU64(segment.offset) || !reader.ReadU64(segment.storedSize) || !reader.ReadU64(segment.logicalSize) ||
            !reader.ReadU64(segment.storedCrc64) || !reader.ReadU8(codec) || !reader.ReadU8(segment.alignmentLog2) ||
            !reader.ReadU8(flags) || !reader.ReadU8(reserved8) || !reader.ReadU32(reserved32))
        {
            return false;
        }
        if (codec > static_cast<u8>(Codec::Lz4) || (flags & ~KnownSegmentFlags) != 0 || reserved8 != 0 || reserved32 != 0)
        {
            return false;
        }
        segment.codec = static_cast<Codec>(codec);
        segment.flags = static_cast<SegmentFlags>(flags);
        return true;
    }

    [[nodiscard]] Result ReadStoredBytes(filesystem::IFile& file, const Segment& segment, void* destination) noexcept
    {
        vgser::BinaryReader reader(file);
        if (!reader.Seek(segment.offset) || !reader.ReadBytes(destination, static_cast<usize>(segment.storedSize)))
        {
            return Result::IoFailure;
        }
        if (vgser::Crc64(destination, static_cast<usize>(segment.storedSize)) != segment.storedCrc64)
        {
            return Result::IntegrityFailure;
        }
        return Result::Success;
    }
} // namespace

namespace vanguard::packages
{
    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success:
            return "Success";
        case Result::InvalidArgument:
            return "InvalidArgument";
        case Result::InvalidState:
            return "InvalidState";
        case Result::InvalidPath:
            return "InvalidPath";
        case Result::DuplicateResource:
            return "DuplicateResource";
        case Result::ResourceIdCollision:
            return "ResourceIdCollision";
        case Result::ResourceNotFound:
            return "ResourceNotFound";
        case Result::InvalidMagic:
            return "InvalidMagic";
        case Result::UnsupportedVersion:
            return "UnsupportedVersion";
        case Result::InvalidLayout:
            return "InvalidLayout";
        case Result::IntegrityFailure:
            return "IntegrityFailure";
        case Result::LimitExceeded:
            return "LimitExceeded";
        case Result::UnsupportedCodec:
            return "UnsupportedCodec";
        case Result::BufferTooSmall:
            return "BufferTooSmall";
        case Result::CompressionFailure:
            return "CompressionFailure";
        case Result::IoFailure:
            return "IoFailure";
        case Result::MissingPackageSet:
            return "MissingPackageSet";
        }
        return "Unknown";
    }

    Result CanonicalizeResourcePath(const containers::StringView path, char* const destination, const usize capacity,
                                    usize& written) noexcept
    {
        const resources::Result result = resources::CanonicalizePath(path, destination, capacity, written);
        switch (result)
        {
        case resources::Result::Success:
            return Result::Success;
        case resources::Result::BufferTooSmall:
            return Result::BufferTooSmall;
        case resources::Result::InvalidArgument:
            return Result::InvalidArgument;
        case resources::Result::InvalidPath:
            return Result::InvalidPath;
        }
        return Result::InvalidPath;
    }

    ResourceId HashResourcePath(const containers::StringView path) noexcept
    {
        return resources::HashPath(path);
    }

    PackageSet::PackageSet() noexcept : packages(memory::pools::Resources::GetInstance()) {}

    void PackageSet::Reset() noexcept
    {
        version = {1, 1};
        gameId = 0;
        buildId = 0;
        targetPlatformId = 0;
        startupWorld = InvalidResourceId;
        startupWorldType = resources::InvalidResourceTypeId;
        defaultInput = InvalidResourceId;
        defaultInputType = resources::InvalidResourceTypeId;
        packages.Clear();
    }

    bool PackageSet::IsValid() const noexcept
    {
        if (version != serialization::Version{1, 1} || gameId == 0 || buildId == 0 || targetPlatformId == 0 ||
            startupWorld == InvalidResourceId || startupWorldType == resources::InvalidResourceTypeId ||
            defaultInput == InvalidResourceId || defaultInputType == resources::InvalidResourceTypeId ||
            packages.Size() > MaximumPackageNumber)
        {
            return false;
        }
        u32 previousNumber = 0;
        for (const PackageSetEntry& entry : packages)
        {
            if (!ValidatePackageSetEntry(entry, previousNumber))
            {
                return false;
            }
            previousNumber = entry.packageNumber;
        }
        return true;
    }

    Result FormatPackageFileName(const u32 packageNumber, char* const destination, const usize capacity, usize& written) noexcept
    {
        constexpr usize NameSize = 12;
        written = 0;
        if (packageNumber > MaximumPackageNumber || destination == nullptr)
        {
            return Result::InvalidArgument;
        }
        if (capacity < NameSize + 1u)
        {
            return Result::BufferTooSmall;
        }
        destination[0] = 'D';
        destination[1] = 'A';
        destination[2] = 'T';
        destination[3] = 'A';
        destination[4] = static_cast<char>('0' + (packageNumber / 100u));
        destination[5] = static_cast<char>('0' + ((packageNumber / 10u) % 10u));
        destination[6] = static_cast<char>('0' + (packageNumber % 10u));
        destination[7] = '.';
        destination[8] = 'v';
        destination[9] = 'p';
        destination[10] = 'a';
        destination[11] = 'k';
        destination[12] = '\0';
        written = NameSize;
        return Result::Success;
    }

    Result ReadPackageSet(filesystem::IFile& reader, PackageSet& packageSet, const ReadLimits& packageLimits,
                          const PackageSetReadLimits& packageSetLimits) noexcept
    {
        packageSet.Reset();
        if (!reader.IsReader())
        {
            return Result::InvalidArgument;
        }
        PackageHeader header;
        const Result headerResult = ReadHeader(reader, packageLimits, header);
        return headerResult == Result::Success ? ReadPackageSetRecord(reader, header, packageSetLimits, packageSet) : headerResult;
    }

    Result OpenCatalogPackage(filesystem::IFile& file, const PackageSetEntry& entry, PackageReader& package,
                              const CatalogVerification verification, const ReadLimits& limits, const u32 hashBufferBytes) noexcept
    {
        package.Close();
        if (!file.IsReader() || !ValidatePackageSetEntry(entry, 0) || verification > CatalogVerification::WholeFileDigest ||
            hashBufferBytes == 0)
        {
            return Result::InvalidArgument;
        }
        if (file.GetSize() != entry.fileSize)
        {
            return Result::IntegrityFailure;
        }
        const Result opened = package.Open(file, limits);
        if (opened != Result::Success)
        {
            return opened;
        }
        const PackageHeader& header = package.Header();
        if (package.HasPackageSet() || header.packageId != entry.packageId || header.buildId != entry.buildId ||
            header.fileSize != entry.fileSize || header.indexCrc64 != entry.indexCrc64)
        {
            package.Close();
            return Result::IntegrityFailure;
        }
        if (verification == CatalogVerification::IndexAndIdentity)
        {
            return Result::Success;
        }

        containers::DynamicArray<u8> buffer(memory::pools::Resources::GetInstance());
        buffer.Resize(hashBufferBytes);
        if (buffer.Size() != hashBufferBytes)
        {
            package.Close();
            return Result::LimitExceeded;
        }
        crypto::Sha256Builder hash;
        file.Seek(0);
        u64 remaining = entry.fileSize;
        while (remaining != 0)
        {
            const usize batch = remaining > buffer.Size() ? buffer.Size() : static_cast<usize>(remaining);
            file.Serialize(buffer.Data(), batch);
            if (file.HasErrors() || !hash.Update(buffer.Data(), batch))
            {
                package.Close();
                return Result::IoFailure;
            }
            remaining -= batch;
        }
        crypto::Digest256 digest;
        if (!hash.Finalize(digest))
        {
            package.Close();
            return Result::InvalidState;
        }
        for (u32 byte = 0; byte < crypto::Digest256::ByteCount; ++byte)
        {
            if (digest.bytes[byte] != entry.contentDigest[byte])
            {
                package.Close();
                return Result::IntegrityFailure;
            }
        }
        return Result::Success;
    }

    PackageWriter::PackageWriter() noexcept
        : m_resources(memory::pools::Resources::GetInstance()), m_segments(memory::pools::Resources::GetInstance()),
          m_dependencies(memory::pools::Resources::GetInstance()), m_debugPaths(memory::pools::Resources::GetInstance()),
          m_compressionScratch(memory::pools::Resources::GetInstance()), m_storedPayloads(memory::pools::Resources::GetInstance()),
          m_storedPayloadLookup(memory::pools::Resources::GetInstance())
    {
    }

    Result PackageWriter::Begin(filesystem::IFile& writer, const BuildOptions& options) noexcept
    {
        if (m_building || !writer.IsWriter() || writer.GetOffset() != 0 || writer.GetSize() != 0 || options.dataAlignmentLog2 > 20 ||
            (static_cast<u32>(options.flags) & ~KnownPackageFlags) != 0 || HasFlag(options.flags, PackageFlags::HasPackageSet))
        {
            return Result::InvalidArgument;
        }

        Reset();
        m_writer = &writer;
        m_options = options;

        vgser::BinaryWriter binary(writer);
        u8 emptyHeader[PackageHeader::WireSize] = {};
        if (!binary.WriteBytes(emptyHeader, sizeof(emptyHeader)) || !binary.Align(1u << options.dataAlignmentLog2))
        {
            Reset();
            return Result::IoFailure;
        }
        m_building = true;
        return Result::Success;
    }

    Result PackageWriter::Begin(filesystem::IFile& writer, const BuildOptions& options, const PackageSetBuild& packageSet) noexcept
    {
        if (m_building || !writer.IsWriter() || writer.GetOffset() != 0 || writer.GetSize() != 0 || options.dataAlignmentLog2 > 20 ||
            (static_cast<u32>(options.flags) & ~KnownPackageFlags) != 0 || HasFlag(options.flags, PackageFlags::HasPackageSet))
        {
            return Result::InvalidArgument;
        }

        Reset();
        m_writer = &writer;
        m_options = options;
        m_options.flags = m_options.flags | PackageFlags::HasPackageSet;

        vgser::BinaryWriter binary(writer);
        u8 emptyHeader[PackageHeader::WireSize] = {};
        if (!binary.WriteBytes(emptyHeader, sizeof(emptyHeader)))
        {
            Reset();
            return Result::IoFailure;
        }
        const Result packageSetResult = WritePackageSetRecord(binary, packageSet, options.buildId, m_packageSetOffset, m_packageSetEnd);
        if (packageSetResult != Result::Success)
        {
            Reset();
            return packageSetResult;
        }
        if (!binary.Align(1u << options.dataAlignmentLog2))
        {
            Reset();
            return Result::IoFailure;
        }
        m_building = true;
        return Result::Success;
    }

    Result PackageWriter::Add(const BuildResource& build) noexcept
    {
        if (!m_building || m_writer == nullptr)
        {
            return Result::InvalidState;
        }
        if (build.type == 0 || build.segments.Empty())
        {
            return Result::InvalidArgument;
        }

        char canonical[MaximumResourcePathBytes];
        usize canonicalSize = 0;
        const Result pathResult = CanonicalizeResourcePath(build.path, canonical, sizeof(canonical), canonicalSize);
        if (pathResult != Result::Success)
        {
            return pathResult;
        }
        const containers::StringView canonicalPath(canonical, canonicalSize);
        const ResourceId id = HashResourcePath(canonicalPath);
        if (id == InvalidResourceId)
        {
            return Result::InvalidPath;
        }

        for (const Resource& existing : m_resources)
        {
            if (existing.id != id)
            {
                continue;
            }
            const containers::StringView existingPath(m_debugPaths.TypedData() + existing.debugPathOffset, existing.debugPathSize);
            return existingPath == canonicalPath ? Result::DuplicateResource : Result::ResourceIdCollision;
        }
        if (HasFlag(m_options.flags, PackageFlags::Deterministic) && !m_resources.Empty() && m_resources.Back().id >= id)
        {
            return Result::InvalidArgument;
        }
        if (m_resources.Size() == std::numeric_limits<u32>::max() ||
            build.segments.Count() > std::numeric_limits<u32>::max() - m_segments.Size() ||
            build.dependencies.Count() > std::numeric_limits<u32>::max() - m_dependencies.Size() ||
            canonicalSize > std::numeric_limits<u32>::max() - m_debugPaths.Size())
        {
            return Result::LimitExceeded;
        }

        Resource resource;
        resource.id = id;
        resource.type = build.type;
        resource.flags = build.flags;
        resource.firstSegment = m_segments.Size();
        resource.segmentCount = build.segments.Count();
        resource.firstDependency = m_dependencies.Size();
        resource.dependencyCount = build.dependencies.Count();
        resource.debugPathOffset = m_debugPaths.Size();
        resource.debugPathSize = static_cast<u32>(canonicalSize);

        for (const Dependency& dependency : build.dependencies)
        {
            if (dependency.id == InvalidResourceId || dependency.type == resources::InvalidResourceTypeId ||
                static_cast<u8>(dependency.kind) > static_cast<u8>(resources::DependencyKind::Soft) || dependency.id == id)
            {
                return Result::InvalidArgument;
            }
        }
        for (const BuildSegment& input : build.segments)
        {
            if ((input.size != 0 && input.data == nullptr) || input.alignmentLog2 > 20 || input.size > std::numeric_limits<u32>::max() ||
                static_cast<u8>(input.codec) > static_cast<u8>(Codec::Lz4))
            {
                return Result::InvalidArgument;
            }
        }

        vgser::BinaryWriter writer(*m_writer);
        for (const BuildSegment& input : build.segments)
        {
            const u8 effectiveAlignment =
                input.alignmentLog2 > m_options.dataAlignmentLog2 ? input.alignmentLog2 : m_options.dataAlignmentLog2;
            const void* storedData = input.data;
            usize storedSize = input.size;
            Codec storedCodec = input.codec;
            if (input.codec == Codec::Lz4 && input.size != 0)
            {
                const Result compressed = backend::CompressLz4(input.data, input.size, m_compressionScratch);
                if (compressed != Result::Success)
                {
                    Reset();
                    return compressed;
                }
                if (m_compressionScratch.Size() < input.size)
                {
                    storedData = m_compressionScratch.TypedData();
                    storedSize = m_compressionScratch.Size();
                }
                else
                {
                    storedCodec = Codec::None;
                }
            }

            Segment segment;
            segment.storedSize = storedSize;
            segment.logicalSize = input.size;
            segment.storedCrc64 = vgser::Crc64(storedData, storedSize);
            segment.codec = storedCodec;
            segment.alignmentLog2 = effectiveAlignment;
            segment.flags = input.flags;

            const crypto::Digest256 digest = crypto::Sha256(storedData, storedSize);
            const u64 digestKey = DigestWord(digest, 0);
            u32 payloadIndex = 0xffffffffu;
            u32 candidate = 0xffffffffu;
            if (m_storedPayloadLookup.Find(digestKey, candidate))
            {
                while (candidate != 0xffffffffu)
                {
                    const StoredPayload& stored = m_storedPayloads[candidate];
                    bool sameDigest = true;
                    for (u32 word = 0; word < 4; ++word)
                    {
                        sameDigest = sameDigest && stored.digest[word] == DigestWord(digest, word);
                    }
                    if (sameDigest && stored.segment.storedSize == storedSize && stored.segment.logicalSize == input.size &&
                        stored.segment.storedCrc64 == segment.storedCrc64 && stored.segment.codec == storedCodec &&
                        (stored.segment.offset & ((u64{1} << effectiveAlignment) - 1u)) == 0)
                    {
                        payloadIndex = candidate;
                        break;
                    }
                    candidate = stored.next;
                }
            }

            if (payloadIndex != 0xffffffffu)
            {
                segment.offset = m_storedPayloads[payloadIndex].segment.offset;
                ++m_deduplicatedSegments;
            }
            else
            {
                if (!writer.Align(1u << effectiveAlignment))
                {
                    Reset();
                    return Result::IoFailure;
                }
                segment.offset = writer.Position();
                if (!writer.WriteBytes(storedData, storedSize))
                {
                    Reset();
                    return Result::IoFailure;
                }

                StoredPayload payload;
                for (u32 word = 0; word < 4; ++word)
                {
                    payload.digest[word] = DigestWord(digest, word);
                }
                payload.segment = segment;
                u32 previousHead = 0xffffffffu;
                const bool hasHead = m_storedPayloadLookup.Find(digestKey, previousHead);
                payload.next = hasHead ? previousHead : 0xffffffffu;
                const u32 newIndex = m_storedPayloads.Size();
                m_storedPayloads.PushBack(payload);
                if (m_storedPayloads.Size() != newIndex + 1u)
                {
                    Reset();
                    return Result::LimitExceeded;
                }
                if (hasHead)
                {
                    m_storedPayloadLookup[digestKey] = newIndex;
                }
                else if (!m_storedPayloadLookup.Insert(digestKey, newIndex).IsSuccessful())
                {
                    Reset();
                    return Result::LimitExceeded;
                }
                m_storedPayloadBytes += storedSize;
            }
            m_segments.PushBack(segment);
            resource.logicalSize += input.size;
            resource.contentCrc64 = vgser::Crc64(input.data, input.size, resource.contentCrc64);
        }

        for (const Dependency& dependency : build.dependencies)
        {
            m_dependencies.PushBack(dependency);
        }
        for (const char character : canonicalPath)
        {
            m_debugPaths.PushBack(character);
        }
        m_resources.PushBack(resource);
        return Result::Success;
    }

    Result PackageWriter::Finalize() noexcept
    {
        if (!m_building || m_writer == nullptr)
        {
            return Result::InvalidState;
        }
        if (m_resources.Empty())
        {
            return Result::InvalidArgument;
        }
        if (HasFlag(m_options.flags, PackageFlags::HasPackageSet) &&
            (m_packageSetOffset != PackageHeader::WireSize || m_packageSetEnd <= m_packageSetOffset))
        {
            return Result::InvalidState;
        }

        if (!HasFlag(m_options.flags, PackageFlags::Deterministic))
        {
            std::sort(m_resources.Begin(), m_resources.End(),
                      [](const Resource& left, const Resource& right) { return left.id < right.id; });
        }

        PackageHeader header;
        header.version = {1, 0};
        header.flags = m_options.flags;
        header.packageId = m_options.packageId;
        header.buildId = m_options.buildId;
        header.resourceCount = m_resources.Size();
        header.segmentCount = m_segments.Size();
        header.dependencyCount = m_dependencies.Size();
        header.debugPathBytes = HasFlag(header.flags, PackageFlags::HasDebugPaths) ? m_debugPaths.Size() : 0;
        header.packageSetOffset = m_packageSetOffset;

        u64 indexSize = IndexHeaderWireSize;
        if (MultiplyOverflow(header.resourceCount, Resource::WireSize) ||
            AddOverflow(indexSize, static_cast<u64>(header.resourceCount) * Resource::WireSize))
        {
            return Result::LimitExceeded;
        }
        indexSize += static_cast<u64>(header.resourceCount) * Resource::WireSize;
        if (MultiplyOverflow(header.segmentCount, Segment::WireSize) ||
            AddOverflow(indexSize, static_cast<u64>(header.segmentCount) * Segment::WireSize))
        {
            return Result::LimitExceeded;
        }
        indexSize += static_cast<u64>(header.segmentCount) * Segment::WireSize;
        if (MultiplyOverflow(header.dependencyCount, DependencyWireSize) ||
            AddOverflow(indexSize, static_cast<u64>(header.dependencyCount) * DependencyWireSize) ||
            AddOverflow(indexSize, header.debugPathBytes) || indexSize + header.debugPathBytes > std::numeric_limits<u32>::max())
        {
            return Result::LimitExceeded;
        }
        indexSize += static_cast<u64>(header.dependencyCount) * DependencyWireSize;
        indexSize += header.debugPathBytes;

        containers::DynamicArray<u8> index(memory::pools::Resources::GetInstance());
        index.Reserve(static_cast<u32>(indexSize));
        filesystem::MemoryFileWriter indexFile(index);
        vgser::BinaryWriter indexWriter(indexFile);
        if (!::WriteIndexHeader(indexWriter, header))
        {
            return Result::IoFailure;
        }
        for (Resource resource : m_resources)
        {
            if (!HasFlag(header.flags, PackageFlags::HasDebugPaths))
            {
                resource.debugPathOffset = 0;
                resource.debugPathSize = 0;
            }
            if (!::WriteResource(indexWriter, resource))
            {
                return Result::IoFailure;
            }
        }
        for (const Segment& segment : m_segments)
        {
            if (!::WriteSegment(indexWriter, segment))
            {
                return Result::IoFailure;
            }
        }
        for (const Dependency& dependency : m_dependencies)
        {
            if (!::WriteDependency(indexWriter, dependency))
            {
                return Result::IoFailure;
            }
        }
        if (header.debugPathBytes != 0 && !indexWriter.WriteBytes(m_debugPaths.TypedData(), header.debugPathBytes))
        {
            return Result::IoFailure;
        }
        if (indexWriter.Position() != indexSize)
        {
            return Result::InvalidLayout;
        }

        vgser::BinaryWriter writer(*m_writer);
        if (!writer.Align(16))
        {
            Reset();
            return Result::IoFailure;
        }
        header.indexOffset = writer.Position();
        header.indexSize = indexSize;
        header.indexCrc64 = vgser::Crc64(index.Data(), index.Size());
        if (!writer.WriteBytes(index.Data(), index.Size()))
        {
            Reset();
            return Result::IoFailure;
        }
        header.fileSize = writer.Position();
        const Result headerResult = ::WriteHeader(writer, header);
        if (headerResult != Result::Success || !writer.Flush())
        {
            Reset();
            return Result::IoFailure;
        }

        m_building = false;
        m_writer = nullptr;
        return Result::Success;
    }

    void PackageWriter::Reset() noexcept
    {
        m_writer = nullptr;
        m_resources.Clear();
        m_segments.Clear();
        m_dependencies.Clear();
        m_debugPaths.Clear();
        m_compressionScratch.Clear();
        m_storedPayloads.Clear();
        m_storedPayloadLookup.Clear();
        m_storedPayloadBytes = 0;
        m_packageSetOffset = 0;
        m_packageSetEnd = PackageHeader::WireSize;
        m_deduplicatedSegments = 0;
        m_building = false;
    }

    bool PackageWriter::IsBuilding() const noexcept
    {
        return m_building;
    }

    u32 PackageWriter::ResourceCount() const noexcept
    {
        return m_resources.Size();
    }

    u32 PackageWriter::DeduplicatedSegmentCount() const noexcept
    {
        return m_deduplicatedSegments;
    }

    u64 PackageWriter::StoredPayloadBytes() const noexcept
    {
        return m_storedPayloadBytes;
    }

    PackageReader::PackageReader() noexcept
        : m_resources(memory::pools::Resources::GetInstance()), m_segments(memory::pools::Resources::GetInstance()),
          m_dependencies(memory::pools::Resources::GetInstance()), m_debugPaths(memory::pools::Resources::GetInstance())
    {
    }

    Result PackageReader::Open(filesystem::IFile& file, const ReadLimits& limits,
                               const PackageSetReadLimits& packageSetLimits) noexcept
    {
        if (m_open || !file.IsReader() || limits.maximumAlignmentLog2 > 31)
        {
            return Result::InvalidArgument;
        }
        Close();

        Result result = ::ReadHeader(file, limits, m_header);
        if (result != Result::Success)
        {
            return result;
        }

        if (HasFlag(m_header.flags, PackageFlags::HasPackageSet))
        {
            result = ReadPackageSetRecord(file, m_header, packageSetLimits, m_packageSet);
            if (result != Result::Success)
            {
                Close();
                return result;
            }
        }

        containers::DynamicArray<u8> index(memory::pools::Resources::GetInstance());
        if (m_header.indexSize > std::numeric_limits<u32>::max())
        {
            return Result::LimitExceeded;
        }
        index.Resize(static_cast<u32>(m_header.indexSize));
        vgser::BinaryReader source(file);
        if (!source.Seek(m_header.indexOffset) || !source.ReadBytes(index.Data(), index.Size()))
        {
            return Result::IoFailure;
        }
        if (vgser::Crc64(index.Data(), index.Size()) != m_header.indexCrc64)
        {
            return Result::IntegrityFailure;
        }

        u64 expectedSize = IndexHeaderWireSize;
        if (MultiplyOverflow(m_header.resourceCount, Resource::WireSize) || MultiplyOverflow(m_header.segmentCount, Segment::WireSize) ||
            MultiplyOverflow(m_header.dependencyCount, DependencyWireSize))
        {
            return Result::InvalidLayout;
        }
        const u64 resourcesSize = static_cast<u64>(m_header.resourceCount) * Resource::WireSize;
        const u64 segmentsSize = static_cast<u64>(m_header.segmentCount) * Segment::WireSize;
        const u64 dependenciesSize = static_cast<u64>(m_header.dependencyCount) * DependencyWireSize;
        if (AddOverflow(expectedSize, resourcesSize) || AddOverflow(expectedSize + resourcesSize, segmentsSize) ||
            AddOverflow(expectedSize + resourcesSize + segmentsSize, dependenciesSize) ||
            AddOverflow(expectedSize + resourcesSize + segmentsSize + dependenciesSize, m_header.debugPathBytes))
        {
            return Result::InvalidLayout;
        }
        expectedSize += resourcesSize + segmentsSize + dependenciesSize + m_header.debugPathBytes;
        if (expectedSize != m_header.indexSize)
        {
            return Result::InvalidLayout;
        }

        filesystem::MemoryFileReader indexFile(index, 0);
        vgser::BinaryReader reader(indexFile);
        u32 magic = 0;
        vgser::Version version;
        u16 headerSize = 0;
        u16 resourceSize = 0;
        u16 segmentSize = 0;
        u16 dependencySize = 0;
        u32 resourceCount = 0;
        u32 segmentCount = 0;
        u32 dependencyCount = 0;
        u32 pathBytes = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU16(version.major) || !reader.ReadU16(version.minor) || !reader.ReadU16(headerSize) ||
            !reader.ReadU16(resourceSize) || !reader.ReadU16(segmentSize) || !reader.ReadU16(dependencySize) ||
            !reader.ReadU32(resourceCount) || !reader.ReadU32(segmentCount) || !reader.ReadU32(dependencyCount) ||
            !reader.ReadU32(pathBytes) || magic != IndexMagic || version != m_header.version || headerSize != IndexHeaderWireSize ||
            resourceSize != Resource::WireSize || segmentSize != Segment::WireSize || dependencySize != DependencyWireSize ||
            resourceCount != m_header.resourceCount || segmentCount != m_header.segmentCount ||
            dependencyCount != m_header.dependencyCount || pathBytes != m_header.debugPathBytes)
        {
            return Result::InvalidLayout;
        }

        m_resources.Resize(resourceCount);
        m_segments.Resize(segmentCount);
        m_dependencies.Resize(dependencyCount);
        m_debugPaths.Resize(pathBytes);
        for (Resource& resource : m_resources)
        {
            if (!::ReadResource(reader, resource))
            {
                Close();
                return Result::InvalidLayout;
            }
        }
        for (Segment& segment : m_segments)
        {
            if (!::ReadSegment(reader, segment))
            {
                Close();
                return Result::InvalidLayout;
            }
        }
        for (Dependency& dependency : m_dependencies)
        {
            if (!::ReadDependency(reader, dependency))
            {
                Close();
                return Result::InvalidLayout;
            }
        }
        if (pathBytes != 0 && !reader.ReadBytes(m_debugPaths.Data(), pathBytes))
        {
            Close();
            return Result::InvalidLayout;
        }

        if (HasFlag(m_header.flags, PackageFlags::HasDebugPaths) != (pathBytes != 0))
        {
            Close();
            return Result::InvalidLayout;
        }

        ResourceId previousId = InvalidResourceId;
        containers::DynamicArray<u8> segmentOwners(memory::pools::Resources::GetInstance());
        containers::DynamicArray<u8> dependencyOwners(memory::pools::Resources::GetInstance());
        segmentOwners.Resize(segmentCount);
        dependencyOwners.Resize(dependencyCount);
        for (u32 ownerIndex = 0; ownerIndex < segmentOwners.Size(); ++ownerIndex)
        {
            segmentOwners[ownerIndex] = 0;
        }
        for (u32 ownerIndex = 0; ownerIndex < dependencyOwners.Size(); ++ownerIndex)
        {
            dependencyOwners[ownerIndex] = 0;
        }

        for (const Resource& resource : m_resources)
        {
            if (resource.id == InvalidResourceId || (previousId != InvalidResourceId && resource.id <= previousId) || resource.type == 0 ||
                resource.firstSegment > segmentCount || resource.segmentCount > segmentCount - resource.firstSegment ||
                resource.segmentCount == 0 || resource.firstDependency > dependencyCount ||
                resource.dependencyCount > dependencyCount - resource.firstDependency || resource.debugPathOffset > pathBytes ||
                resource.debugPathSize > pathBytes - resource.debugPathOffset)
            {
                Close();
                return Result::InvalidLayout;
            }
            previousId = resource.id;

            u64 logicalSize = 0;
            for (u32 localIndex = 0; localIndex < resource.segmentCount; ++localIndex)
            {
                const u32 segmentIndex = resource.firstSegment + localIndex;
                if (segmentOwners[segmentIndex] != 0 || AddOverflow(logicalSize, m_segments[segmentIndex].logicalSize))
                {
                    Close();
                    return Result::InvalidLayout;
                }
                segmentOwners[segmentIndex] = 1;
                logicalSize += m_segments[segmentIndex].logicalSize;
            }
            if (logicalSize != resource.logicalSize)
            {
                Close();
                return Result::InvalidLayout;
            }

            for (u32 localIndex = 0; localIndex < resource.dependencyCount; ++localIndex)
            {
                const u32 dependencyIndex = resource.firstDependency + localIndex;
                if (dependencyOwners[dependencyIndex] != 0 || m_dependencies[dependencyIndex].id == InvalidResourceId ||
                    m_dependencies[dependencyIndex].type == resources::InvalidResourceTypeId ||
                    m_dependencies[dependencyIndex].id == resource.id)
                {
                    Close();
                    return Result::InvalidLayout;
                }
                dependencyOwners[dependencyIndex] = 1;
            }

            if (pathBytes != 0)
            {
                const containers::StringView path(m_debugPaths.TypedData() + resource.debugPathOffset, resource.debugPathSize);
                char canonical[MaximumResourcePathBytes];
                usize canonicalSize = 0;
                if (CanonicalizeResourcePath(path, canonical, sizeof(canonical), canonicalSize) != Result::Success ||
                    canonicalSize != path.Size() || std::memcmp(canonical, path.Data(), canonicalSize) != 0 ||
                    HashResourcePath(path) != resource.id)
                {
                    Close();
                    return Result::InvalidLayout;
                }
            }
            else if (resource.debugPathOffset != 0 || resource.debugPathSize != 0)
            {
                Close();
                return Result::InvalidLayout;
            }
        }

        containers::DynamicArray<Segment> physicalSegments(memory::pools::Resources::GetInstance());
        containers::HashMap<u64, u32> physicalLookup(memory::pools::Resources::GetInstance());
        const u64 payloadStart = HasFlag(m_header.flags, PackageFlags::HasPackageSet)
                                     ? m_header.packageSetOffset + PackageSetRecordSize(m_packageSet.packages.Size())
                                     : PackageHeader::WireSize;
        for (const Segment& segment : m_segments)
        {
            if (segment.alignmentLog2 > limits.maximumAlignmentLog2 || segment.storedSize > limits.maximumSegmentStoredSize ||
                segment.logicalSize > limits.maximumSegmentLogicalSize || AddOverflow(segment.offset, segment.storedSize) ||
                segment.offset + segment.storedSize > m_header.indexOffset || segment.offset < payloadStart ||
                !IsPowerOfTwo(u64{1} << segment.alignmentLog2) || (segment.offset & ((u64{1} << segment.alignmentLog2) - 1u)) != 0 ||
                (segment.codec == Codec::None && segment.storedSize != segment.logicalSize))
            {
                Close();
                return Result::InvalidLayout;
            }

            u32 physicalIndex = 0;
            if (physicalLookup.Find(segment.offset, physicalIndex))
            {
                const Segment& physical = physicalSegments[physicalIndex];
                if (physical.storedSize != segment.storedSize || physical.logicalSize != segment.logicalSize ||
                    physical.storedCrc64 != segment.storedCrc64 || physical.codec != segment.codec)
                {
                    Close();
                    return Result::InvalidLayout;
                }
            }
            else
            {
                const u32 newPhysicalIndex = physicalSegments.Size();
                physicalSegments.PushBack(segment);
                if (physicalSegments.Size() != newPhysicalIndex + 1u ||
                    !physicalLookup.Insert(segment.offset, newPhysicalIndex).IsSuccessful())
                {
                    Close();
                    return Result::LimitExceeded;
                }
            }
        }
        std::sort(physicalSegments.Begin(), physicalSegments.End(),
                  [](const Segment& left, const Segment& right) { return left.offset < right.offset; });
        u64 previousSegmentEnd = payloadStart;
        for (const Segment& segment : physicalSegments)
        {
            if (segment.offset < previousSegmentEnd)
            {
                Close();
                return Result::InvalidLayout;
            }
            previousSegmentEnd = segment.offset + segment.storedSize;
        }
        for (const u8 owner : segmentOwners)
        {
            if (owner != 1)
            {
                Close();
                return Result::InvalidLayout;
            }
        }
        for (const u8 owner : dependencyOwners)
        {
            if (owner != 1)
            {
                Close();
                return Result::InvalidLayout;
            }
        }

        m_open = true;
        return Result::Success;
    }

    void PackageReader::Close() noexcept
    {
        m_header = {};
        m_resources.Clear();
        m_segments.Clear();
        m_dependencies.Clear();
        m_debugPaths.Clear();
        m_packageSet.Reset();
        m_open = false;
    }

    bool PackageReader::IsOpen() const noexcept
    {
        return m_open;
    }

    const PackageHeader& PackageReader::Header() const noexcept
    {
        return m_header;
    }

    bool PackageReader::HasPackageSet() const noexcept
    {
        return m_open && m_packageSet.IsValid();
    }

    const PackageSet* PackageReader::GetPackageSet() const noexcept
    {
        return HasPackageSet() ? &m_packageSet : nullptr;
    }

    containers::ArraySpan<const Resource> PackageReader::Resources() const noexcept
    {
        return {m_resources.TypedData(), m_resources.Size()};
    }

    containers::ArraySpan<const Segment> PackageReader::Segments(const Resource& resource) const noexcept
    {
        if (!m_open || resource.firstSegment + resource.segmentCount > m_segments.Size())
        {
            return {};
        }
        return {m_segments.TypedData() + resource.firstSegment, resource.segmentCount};
    }

    containers::ArraySpan<const Dependency> PackageReader::Dependencies(const Resource& resource) const noexcept
    {
        if (!m_open || resource.firstDependency + resource.dependencyCount > m_dependencies.Size())
        {
            return {};
        }
        return {m_dependencies.TypedData() + resource.firstDependency, resource.dependencyCount};
    }

    containers::StringView PackageReader::DebugPath(const Resource& resource) const noexcept
    {
        if (!m_open || resource.debugPathSize == 0 || resource.debugPathOffset + resource.debugPathSize > m_debugPaths.Size())
        {
            return {};
        }
        return {m_debugPaths.TypedData() + resource.debugPathOffset, resource.debugPathSize};
    }

    const Resource* PackageReader::Find(const ResourceId id) const noexcept
    {
        if (!m_open || id == InvalidResourceId)
        {
            return nullptr;
        }
        u32 first = 0;
        u32 count = m_resources.Size();
        while (count != 0)
        {
            const u32 step = count / 2;
            const u32 middle = first + step;
            if (m_resources[middle].id < id)
            {
                first = middle + 1;
                count -= step + 1;
            }
            else
            {
                count = step;
            }
        }
        return first < m_resources.Size() && m_resources[first].id == id ? &m_resources[first] : nullptr;
    }

    const Resource* PackageReader::Find(const containers::StringView path) const noexcept
    {
        return Find(HashResourcePath(path));
    }

    Result PackageReader::ReadSegment(filesystem::IFile& file, const Segment& segment, void* const destination, const usize destinationSize,
                                      void* const storedScratch, const usize storedScratchSize) const noexcept
    {
        const auto segmentAddress = reinterpret_cast<uintptr_t>(&segment);
        const auto segmentBegin = reinterpret_cast<uintptr_t>(m_segments.TypedData());
        const auto segmentEnd = segmentBegin + static_cast<uintptr_t>(m_segments.Size()) * sizeof(Segment);
        if (!m_open || segmentAddress < segmentBegin || segmentAddress >= segmentEnd ||
            ((segmentAddress - segmentBegin) % sizeof(Segment)) != 0 || !file.IsReader() || file.GetSize() != m_header.fileSize ||
            destinationSize < segment.logicalSize || (segment.logicalSize != 0 && destination == nullptr))
        {
            return destinationSize < segment.logicalSize ? Result::BufferTooSmall : Result::InvalidArgument;
        }

        if (segment.codec == Codec::None)
        {
            return ReadStoredBytes(file, segment, destination);
        }
        if (segment.codec != Codec::Lz4)
        {
            return Result::UnsupportedCodec;
        }
        if (storedScratchSize < segment.storedSize || (segment.storedSize != 0 && storedScratch == nullptr))
        {
            return Result::BufferTooSmall;
        }
        Result result = ReadStoredBytes(file, segment, storedScratch);
        if (result != Result::Success)
        {
            return result;
        }
        return backend::DecompressLz4(storedScratch, static_cast<usize>(segment.storedSize), destination,
                                      static_cast<usize>(segment.logicalSize));
    }

    Result PackageReader::DecodeSegment(const Segment& segment, const void* const storedData, const usize storedSize,
                                        void* const destination, const usize destinationSize) const noexcept
    {
        if (!m_open || storedSize != segment.storedSize || destinationSize < segment.logicalSize ||
            (storedSize != 0 && storedData == nullptr) || (segment.logicalSize != 0 && destination == nullptr))
        {
            return Result::InvalidArgument;
        }
        if (vgser::Crc64(storedData, storedSize) != segment.storedCrc64)
        {
            return Result::IntegrityFailure;
        }
        if (segment.codec == Codec::None)
        {
            if (segment.storedSize != segment.logicalSize)
            {
                return Result::InvalidLayout;
            }
            if (storedSize != 0 && storedData != destination)
            {
                std::memcpy(destination, storedData, storedSize);
            }
            return Result::Success;
        }
        if (segment.codec != Codec::Lz4)
        {
            return Result::UnsupportedCodec;
        }
        return backend::DecompressLz4(storedData, storedSize, destination, static_cast<usize>(segment.logicalSize));
    }

    Result PackageReader::ReadResource(filesystem::IFile& file, const Resource& resource, void* const destination,
                                       const usize destinationSize, void* const storedScratch, const usize storedScratchSize) const noexcept
    {
        const auto resourceAddress = reinterpret_cast<uintptr_t>(&resource);
        const auto resourceBegin = reinterpret_cast<uintptr_t>(m_resources.TypedData());
        const auto resourceEnd = resourceBegin + static_cast<uintptr_t>(m_resources.Size()) * sizeof(Resource);
        if (!m_open || resourceAddress < resourceBegin || resourceAddress >= resourceEnd ||
            ((resourceAddress - resourceBegin) % sizeof(Resource)) != 0 || destinationSize < resource.logicalSize ||
            (resource.logicalSize != 0 && destination == nullptr))
        {
            return destinationSize < resource.logicalSize ? Result::BufferTooSmall : Result::InvalidArgument;
        }

        auto* output = static_cast<u8*>(destination);
        usize outputOffset = 0;
        for (const Segment& segment : Segments(resource))
        {
            void* const segmentOutput = outputOffset == 0 ? output : output + outputOffset;
            const Result result =
                ReadSegment(file, segment, segmentOutput, destinationSize - outputOffset, storedScratch, storedScratchSize);
            if (result != Result::Success)
            {
                return result;
            }
            outputOffset += static_cast<usize>(segment.logicalSize);
        }
        return vgser::Crc64(destination, outputOffset) == resource.contentCrc64 ? Result::Success : Result::IntegrityFailure;
    }

    ResourceFileReader::ResourceFileReader() noexcept
        : filesystem::IFile(FF_Reader | FF_FileBased), m_logicalOffsets(memory::pools::Streaming::GetInstance()),
          m_decodedSegment(memory::pools::Streaming::GetInstance()), m_storedScratch(memory::pools::Streaming::GetInstance())
    {
    }

    Result ResourceFileReader::Open(const PackageReader& package, const Resource& resource,
                                    filesystem::IFile& physicalPackageFile) noexcept
    {
        Close();
        const auto resourceAddress = reinterpret_cast<uintptr_t>(&resource);
        const auto resourceBegin = reinterpret_cast<uintptr_t>(package.Resources().Data());
        const auto resourceEnd = resourceBegin + static_cast<uintptr_t>(package.Resources().Count()) * sizeof(Resource);
        const containers::ArraySpan<const Segment> segments = package.Segments(resource);
        if (!package.IsOpen() || resourceAddress < resourceBegin || resourceAddress >= resourceEnd ||
            ((resourceAddress - resourceBegin) % sizeof(Resource)) != 0 || !physicalPackageFile.IsReader() ||
            physicalPackageFile.GetSize() != package.Header().fileSize || segments.Count() != resource.segmentCount ||
            segments.Empty())
        {
            return Result::InvalidArgument;
        }

        m_logicalOffsets.Resize(segments.Count() + 1u);
        if (m_logicalOffsets.Size() != segments.Count() + 1u)
        {
            Close();
            return Result::LimitExceeded;
        }
        u64 logicalOffset = 0;
        m_logicalOffsets[0] = 0;
        for (u32 index = 0; index < segments.Count(); ++index)
        {
            if (logicalOffset > ~u64{0} - segments[index].logicalSize)
            {
                Close();
                return Result::InvalidLayout;
            }
            logicalOffset += segments[index].logicalSize;
            m_logicalOffsets[index + 1u] = logicalOffset;
        }
        if (logicalOffset != resource.logicalSize)
        {
            Close();
            return Result::InvalidLayout;
        }

        m_package = &package;
        m_resource = &resource;
        m_physicalFile = &physicalPackageFile;
        m_segments = segments;
        m_lastResult = Result::Success;
        m_open = true;
        return Result::Success;
    }

    void ResourceFileReader::Close() noexcept
    {
        m_package = nullptr;
        m_resource = nullptr;
        m_physicalFile = nullptr;
        m_segments = {};
        m_logicalOffsets.Clear();
        m_decodedSegment.Clear();
        m_storedScratch.Clear();
        m_offset = 0;
        m_storedBytesRead = 0;
        m_decodedSegmentCount = 0;
        m_cachedSegment = 0xffffffffu;
        m_lastResult = Result::InvalidState;
        m_open = false;
        m_flags &= ~FF_ErrorOccured;
    }

    bool ResourceFileReader::IsOpen() const noexcept
    {
        return m_open;
    }

    Result ResourceFileReader::LastResult() const noexcept
    {
        return m_lastResult;
    }

    u64 ResourceFileReader::StoredBytesRead() const noexcept
    {
        return m_storedBytesRead;
    }

    u32 ResourceFileReader::DecodedSegmentCount() const noexcept
    {
        return m_decodedSegmentCount;
    }

    u32 ResourceFileReader::FindSegment(const u64 logicalOffset) const noexcept
    {
        u32 first = 0;
        u32 count = m_segments.Count();
        while (count != 0)
        {
            const u32 step = count / 2;
            const u32 middle = first + step;
            if (m_logicalOffsets[middle + 1u] <= logicalOffset)
            {
                first = middle + 1u;
                count -= step + 1u;
            }
            else
            {
                count = step;
            }
        }
        return first;
    }

    bool ResourceFileReader::LoadSegment(const u32 index) noexcept
    {
        if (index == m_cachedSegment)
        {
            return true;
        }
        if (index >= m_segments.Count())
        {
            Fail(Result::InvalidLayout);
            return false;
        }

        const Segment& segment = m_segments[index];
        if (segment.logicalSize > ~u32{0} || segment.storedSize > ~u32{0})
        {
            Fail(Result::LimitExceeded);
            return false;
        }
        m_decodedSegment.Resize(static_cast<u32>(segment.logicalSize));
        const u32 scratchSize = segment.codec == Codec::None ? 0u : static_cast<u32>(segment.storedSize);
        m_storedScratch.Resize(scratchSize);
        if (m_decodedSegment.Size() != static_cast<u32>(segment.logicalSize) || m_storedScratch.Size() != scratchSize)
        {
            Fail(Result::LimitExceeded);
            return false;
        }

        const Result result = m_package->ReadSegment(
            *m_physicalFile, segment, m_decodedSegment.TypedData(), m_decodedSegment.Size(),
            m_storedScratch.TypedData(), m_storedScratch.Size());
        if (result != Result::Success)
        {
            Fail(result);
            return false;
        }
        m_storedBytesRead += segment.storedSize;
        ++m_decodedSegmentCount;
        m_cachedSegment = index;
        return true;
    }

    void ResourceFileReader::Fail(const Result result) noexcept
    {
        m_lastResult = result;
        if (!HasErrors())
        {
            HandleIOError("VPAK logical resource read failed: %s", ToString(result));
        }
    }

    void ResourceFileReader::Serialize(void* const buffer, const size_t size)
    {
        if (!m_open || (size != 0 && buffer == nullptr) || size > GetSize() - m_offset)
        {
            Fail(!m_open ? Result::InvalidState : Result::IoFailure);
            return;
        }

        auto* destination = static_cast<u8*>(buffer);
        u64 remaining = static_cast<u64>(size);
        while (remaining != 0)
        {
            const u32 segmentIndex = FindSegment(m_offset);
            if (!LoadSegment(segmentIndex))
            {
                return;
            }
            const u64 offsetInSegment = m_offset - m_logicalOffsets[segmentIndex];
            const u64 available = m_segments[segmentIndex].logicalSize - offsetInSegment;
            const usize copied = static_cast<usize>(remaining < available ? remaining : available);
            std::memcpy(destination, m_decodedSegment.TypedData() + offsetInSegment, copied);
            destination += copied;
            m_offset += copied;
            remaining -= copied;
        }
    }

    Uint64 ResourceFileReader::GetOffset() const
    {
        return m_offset;
    }

    Uint64 ResourceFileReader::GetSize() const
    {
        return m_resource != nullptr ? m_resource->logicalSize : 0;
    }

    void ResourceFileReader::Seek(const Int64 offset)
    {
        if (!m_open || offset < 0 || static_cast<u64>(offset) > GetSize())
        {
            Fail(!m_open ? Result::InvalidState : Result::InvalidArgument);
            return;
        }
        m_offset = static_cast<u64>(offset);
    }

    void ResourceFileReader::Flush()
    {
    }

    const char* ResourceFileReader::GetFileNameForDebug() const
    {
        return m_physicalFile != nullptr ? m_physicalFile->GetFileNameForDebug() : "VPAK logical resource";
    }

    MountTable::MountTable() noexcept : m_mounts(memory::pools::Resources::GetInstance()) {}

    Result MountTable::Mount(const PackageReader& package, const i32 priority) noexcept
    {
        if (!package.IsOpen())
        {
            return Result::InvalidArgument;
        }
        for (const MountedPackage& mount : m_mounts)
        {
            if (mount.package == &package)
            {
                return Result::DuplicateResource;
            }
        }

        MountedPackage mount;
        mount.package = &package;
        mount.priority = priority;
        mount.sequence = m_nextSequence++;
        m_mounts.PushBack(mount);
        std::sort(m_mounts.Begin(), m_mounts.End(), [](const MountedPackage& left, const MountedPackage& right)
                  { return left.priority != right.priority ? left.priority > right.priority : left.sequence > right.sequence; });
        return Result::Success;
    }

    bool MountTable::Unmount(const PackageReader& package) noexcept
    {
        for (u32 index = 0; index < m_mounts.Size(); ++index)
        {
            if (m_mounts[index].package == &package)
            {
                m_mounts.RemoveAt(index);
                return true;
            }
        }
        return false;
    }

    void MountTable::Clear() noexcept
    {
        m_mounts.Clear();
        m_nextSequence = 0;
    }

    ResolvedResource MountTable::Resolve(const ResourceId id) const noexcept
    {
        for (const MountedPackage& mount : m_mounts)
        {
            if (const Resource* resource = mount.package->Find(id))
            {
                return {mount.package, resource};
            }
        }
        return {};
    }

    ResolvedResource MountTable::Resolve(const containers::StringView path) const noexcept
    {
        return Resolve(HashResourcePath(path));
    }

    u32 MountTable::Count() const noexcept
    {
        return m_mounts.Size();
    }
} // namespace vanguard::packages

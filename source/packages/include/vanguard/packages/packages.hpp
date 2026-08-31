#pragma once

#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::packages
{
    using ResourceId = resources::ResourceId;
    using ResourceTypeId = resources::ResourceTypeId;

    inline constexpr ResourceId InvalidResourceId = resources::InvalidResourceId;
    inline constexpr usize MaximumResourcePathBytes = resources::MaximumResourcePathBytes;
    inline constexpr u32 PackageMagic = serialization::MakeFourCC('V', 'P', 'A', 'K');
    inline constexpr u32 PackageSetMagic = serialization::MakeFourCC('V', 'P', 'S', 'T');
    inline constexpr u32 MaximumPackageNumber = 999;

    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        InvalidPath,
        DuplicateResource,
        ResourceIdCollision,
        ResourceNotFound,
        InvalidMagic,
        UnsupportedVersion,
        InvalidLayout,
        IntegrityFailure,
        LimitExceeded,
        UnsupportedCodec,
        BufferTooSmall,
        CompressionFailure,
        IoFailure,
        MissingPackageSet
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class PackageFlags : u32
    {
        None = 0,
        Deterministic = 1u << 0u,
        HasDebugPaths = 1u << 1u,
        HasPackageSet = 1u << 2u
    };

    enum class ResourceFlags : u32
    {
        None = 0,
        Startup = 1u << 0u,
        Optional = 1u << 1u,
        Streamable = 1u << 2u,
        EditorOnly = 1u << 3u
    };

    enum class SegmentFlags : u8
    {
        None = 0,
        Inline = 1u << 0u,
        Streamable = 1u << 1u,
        MemoryResident = 1u << 2u
    };

    enum class Codec : u8
    {
        None = 0,
        Lz4 = 1
    };

    enum class PackageSetEntryFlags : u32
    {
        None = 0,
        Required = 1u << 0u,
        Optional = 1u << 1u,
        Override = 1u << 2u
    };

    [[nodiscard]] constexpr PackageFlags operator|(PackageFlags left, PackageFlags right) noexcept
    {
        return static_cast<PackageFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr ResourceFlags operator|(ResourceFlags left, ResourceFlags right) noexcept
    {
        return static_cast<ResourceFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr SegmentFlags operator|(SegmentFlags left, SegmentFlags right) noexcept
    {
        return static_cast<SegmentFlags>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr PackageSetEntryFlags operator|(PackageSetEntryFlags left, PackageSetEntryFlags right) noexcept
    {
        return static_cast<PackageSetEntryFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    struct PackageHeader
    {
        static constexpr u16 WireSize = 96;
        static constexpr u8 EncodingVersion = 1;
        static constexpr u8 LittleEndian = 1;

        serialization::Version version{1, 0};
        PackageFlags flags = PackageFlags::None;
        u64 fileSize = 0;
        u64 indexOffset = 0;
        u64 indexSize = 0;
        u64 packageId = 0;
        u64 buildId = 0;
        u32 resourceCount = 0;
        u32 segmentCount = 0;
        u32 dependencyCount = 0;
        u32 debugPathBytes = 0;
        u64 indexCrc64 = 0;
        u64 packageSetOffset = 0;
    };

    struct PackageSetEntry
    {
        static constexpr u16 WireSize = 80;

        u32 packageNumber = 0;
        PackageSetEntryFlags flags = PackageSetEntryFlags::Required;
        i32 mountPriority = 0;
        u64 packageId = 0;
        u64 buildId = 0;
        u64 fileSize = 0;
        u64 indexCrc64 = 0;
        u8 contentDigest[32]{};
    };

    struct PackageSetBuild
    {
        u64 gameId = 0;
        u32 targetPlatformId = 0;
        ResourceId startupWorld = InvalidResourceId;
        ResourceTypeId startupWorldType = resources::InvalidResourceTypeId;
        ResourceId defaultInput = InvalidResourceId;
        ResourceTypeId defaultInputType = resources::InvalidResourceTypeId;
        containers::ArraySpan<const PackageSetEntry> packages;
    };

    struct PackageSetReadLimits
    {
        u32 maximumPackages = MaximumPackageNumber;
        u64 maximumRecordSize = 1024ull * 1024ull;
    };

    enum class CatalogVerification : u8
    {
        IndexAndIdentity,
        WholeFileDigest
    };

    class PackageSet final
    {
    public:
        static constexpr u16 HeaderWireSize = 96;

        PackageSet() noexcept;

        PackageSet(const PackageSet&) = delete;
        PackageSet& operator=(const PackageSet&) = delete;

        void Reset() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;

        containers::DynamicArray<PackageSetEntry> packages;
        u64 gameId = 0;
        u64 buildId = 0;
        ResourceId startupWorld = InvalidResourceId;
        ResourceId defaultInput = InvalidResourceId;
        serialization::Version version{1, 1};
        u32 targetPlatformId = 0;
        ResourceTypeId startupWorldType = resources::InvalidResourceTypeId;
        ResourceTypeId defaultInputType = resources::InvalidResourceTypeId;

    private:
        u32 m_alignmentPadding[3]{};
    };

    struct Segment
    {
        static constexpr u16 WireSize = 40;

        u64 offset = 0;
        u64 storedSize = 0;
        u64 logicalSize = 0;
        u64 storedCrc64 = 0;
        Codec codec = Codec::None;
        u8 alignmentLog2 = 0;
        SegmentFlags flags = SegmentFlags::None;
    };

    struct Dependency
    {
        static constexpr u16 WireSize = 16;

        ResourceId id = InvalidResourceId;
        ResourceTypeId type = resources::InvalidResourceTypeId;
        resources::DependencyKind kind = resources::DependencyKind::Required;
    };

    struct Resource
    {
        static constexpr u16 WireSize = 64;

        ResourceId id = InvalidResourceId;
        ResourceTypeId type = 0;
        ResourceFlags flags = ResourceFlags::None;
        u64 logicalSize = 0;
        u32 firstSegment = 0;
        u32 segmentCount = 0;
        u32 firstDependency = 0;
        u32 dependencyCount = 0;
        u32 debugPathOffset = 0;
        u32 debugPathSize = 0;
        u64 contentCrc64 = 0;
    };

    struct ReadLimits
    {
        u64 maximumFileSize = 256ull * 1024ull * 1024ull * 1024ull;
        u64 maximumIndexSize = 512ull * 1024ull * 1024ull;
        u32 maximumResources = 4u * 1024u * 1024u;
        u32 maximumSegments = 16u * 1024u * 1024u;
        u32 maximumDependencies = 64u * 1024u * 1024u;
        u32 maximumDebugPathBytes = 512u * 1024u * 1024u;
        u64 maximumSegmentStoredSize = 2ull * 1024ull * 1024ull * 1024ull;
        u64 maximumSegmentLogicalSize = 2ull * 1024ull * 1024ull * 1024ull;
        u8 maximumAlignmentLog2 = 20;
    };

    struct BuildOptions
    {
        PackageFlags flags = PackageFlags::Deterministic | PackageFlags::HasDebugPaths;
        u64 packageId = 0;
        u64 buildId = 0;
        u8 dataAlignmentLog2 = 12;
    };

    struct BuildSegment
    {
        const void* data = nullptr;
        usize size = 0;
        Codec codec = Codec::None;
        u8 alignmentLog2 = 12;
        SegmentFlags flags = SegmentFlags::None;
    };

    struct BuildResource
    {
        containers::StringView path;
        ResourceTypeId type = 0;
        ResourceFlags flags = ResourceFlags::None;
        containers::ArraySpan<const BuildSegment> segments;
        containers::ArraySpan<const Dependency> dependencies;
    };

    [[nodiscard]] Result CanonicalizeResourcePath(containers::StringView path, char* destination, usize capacity, usize& written) noexcept;

    [[nodiscard]] ResourceId HashResourcePath(containers::StringView path) noexcept;

    [[nodiscard]] Result FormatPackageFileName(u32 packageNumber, char* destination, usize capacity, usize& written) noexcept;

    class PackageReader;

    [[nodiscard]] Result ReadPackageSet(filesystem::IFile& reader, PackageSet& packageSet, const ReadLimits& packageLimits = {},
                                        const PackageSetReadLimits& packageSetLimits = {}) noexcept;

    // Opens one external DATA package named by a validated DATA000 catalog entry. Normal startup validates exact size, package/build
    // identity, index CRC, and package layout. WholeFileDigest additionally reads and authenticates every byte and is intended for
    // installation verification or paranoid modes rather than ordinary launch-time mounting.
    [[nodiscard]] Result OpenCatalogPackage(filesystem::IFile& file, const PackageSetEntry& entry, PackageReader& package,
                                            CatalogVerification verification = CatalogVerification::IndexAndIdentity, const ReadLimits& limits = {},
                                            u32 hashBufferBytes = 1024u * 1024u) noexcept;

    class PackageWriter final
    {
    public:
        PackageWriter() noexcept;
        ~PackageWriter() = default;

        PackageWriter(const PackageWriter&) = delete;
        PackageWriter& operator=(const PackageWriter&) = delete;

        [[nodiscard]] Result Begin(filesystem::IFile& writer, const BuildOptions& options = {}) noexcept;
        [[nodiscard]] Result Begin(filesystem::IFile& writer, const BuildOptions& options, const PackageSetBuild& packageSet) noexcept;

        [[nodiscard]] Result Add(const BuildResource& resource) noexcept;
        [[nodiscard]] Result Finalize() noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool IsBuilding() const noexcept;
        [[nodiscard]] u32 GetResourceCount() const noexcept;
        [[nodiscard]] u32 GetDeduplicatedSegmentCount() const noexcept;
        [[nodiscard]] u64 GetStoredPayloadBytes() const noexcept;

    private:
        struct StoredPayload
        {
            u64 digest[4]{};
            Segment segment;
            u32 next = 0xffffffffu;
        };

        filesystem::IFile* m_writer = nullptr;
        BuildOptions m_options;
        containers::DynamicArray<Resource> m_resources;
        containers::DynamicArray<Segment> m_segments;
        containers::DynamicArray<Dependency> m_dependencies;
        containers::DynamicArray<char> m_debugPaths;
        containers::DynamicArray<u8> m_compressionScratch;
        containers::DynamicArray<StoredPayload> m_storedPayloads;
        containers::HashMap<u64, u32> m_storedPayloadLookup;
        u64 m_storedPayloadBytes = 0;
        u64 m_packageSetOffset = 0;
        u64 m_packageSetEnd = PackageHeader::WireSize;
        u32 m_deduplicatedSegments = 0;
        bool m_building = false;
    };

    class PackageReader final
    {
    public:
        PackageReader() noexcept;
        ~PackageReader() = default;

        PackageReader(const PackageReader&) = delete;
        PackageReader& operator=(const PackageReader&) = delete;

        [[nodiscard]] Result Open(filesystem::IFile& reader, const ReadLimits& limits = {}, const PackageSetReadLimits& packageSetLimits = {}) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] const PackageHeader& GetHeader() const noexcept;
        [[nodiscard]] bool HasPackageSet() const noexcept;
        [[nodiscard]] const PackageSet* GetPackageSet() const noexcept;
        [[nodiscard]] containers::ArraySpan<const Resource> GetResources() const noexcept;
        [[nodiscard]] containers::ArraySpan<const Segment> GetSegments(const Resource& resource) const noexcept;
        [[nodiscard]] containers::ArraySpan<const Dependency> GetDependencies(const Resource& resource) const noexcept;
        [[nodiscard]] containers::StringView GetDebugPath(const Resource& resource) const noexcept;

        [[nodiscard]] const Resource* Find(ResourceId id) const noexcept;
        [[nodiscard]] const Resource* Find(containers::StringView canonicalOrSourcePath) const noexcept;

        [[nodiscard]] Result ReadSegment(filesystem::IFile& reader, const Segment& segment, void* destination, usize destinationSize,
                                         void* storedScratch = nullptr, usize storedScratchSize = 0) const noexcept;

        // Decodes bytes already acquired by asynchronous I/O. Stored-byte CRC
        // is verified before copying or decompression.
        [[nodiscard]] Result DecodeSegment(const Segment& segment, const void* storedData, usize storedSize, void* destination,
                                           usize destinationSize) const noexcept;

        [[nodiscard]] Result ReadResource(filesystem::IFile& reader, const Resource& resource, void* destination, usize destinationSize,
                                          void* storedScratch = nullptr, usize storedScratchSize = 0) const noexcept;

    private:
        PackageHeader m_header;
        containers::DynamicArray<Resource> m_resources;
        containers::DynamicArray<Segment> m_segments;
        containers::DynamicArray<Dependency> m_dependencies;
        containers::DynamicArray<char> m_debugPaths;
        PackageSet m_packageSet;
        bool m_open = false;
    };

    /// Seekable logical view of one packaged resource.
    ///
    /// Reads decode and validate only the package segments touched by the requested
    /// logical range. One decoded segment is cached. The package reader, resource,
    /// and physical package file must remain valid and unchanged while this view is open.
    /// Instances are intentionally not thread-safe. Every concurrent view must receive
    /// its own independently seekable physical package reader.
    class ResourceFileReader final : public filesystem::IFile
    {
    public:
        ResourceFileReader() noexcept;
        ~ResourceFileReader() override = default;

        ResourceFileReader(const ResourceFileReader&) = delete;
        ResourceFileReader& operator=(const ResourceFileReader&) = delete;

        [[nodiscard]] Result Open(const PackageReader& package, const Resource& resource, filesystem::IFile& physicalPackageFile) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] Result GetLastResult() const noexcept;
        [[nodiscard]] u64 GetStoredBytesRead() const noexcept;
        [[nodiscard]] u32 GetDecodedSegmentCount() const noexcept;

        void Serialize(void* buffer, size_t size) override;
        [[nodiscard]] Uint64 GetOffset() const override;
        [[nodiscard]] Uint64 GetSize() const override;
        void Seek(Int64 offset) override;
        void Flush() override;
        [[nodiscard]] const char* GetFileNameForDebug() const override;

    private:
        [[nodiscard]] bool LoadSegment(u32 index) noexcept;
        [[nodiscard]] u32 FindSegment(u64 logicalOffset) const noexcept;
        void Fail(Result result) noexcept;

        const PackageReader* m_package = nullptr;
        const Resource* m_resource = nullptr;
        filesystem::IFile* m_physicalFile = nullptr;
        containers::ArraySpan<const Segment> m_segments;
        containers::DynamicArray<u64> m_logicalOffsets;
        containers::DynamicArray<u8> m_decodedSegment;
        containers::DynamicArray<u8> m_storedScratch;
        u64 m_offset = 0;
        u64 m_storedBytesRead = 0;
        u32 m_decodedSegmentCount = 0;
        u32 m_cachedSegment = 0xffffffffu;
        Result m_lastResult = Result::InvalidState;
        bool m_open = false;
    };

    struct ResolvedResource
    {
        const PackageReader* package = nullptr;
        const Resource* resource = nullptr;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return package != nullptr && resource != nullptr;
        }
    };

    class MountTable final
    {
    public:
        MountTable() noexcept;
        ~MountTable() = default;

        MountTable(const MountTable&) = delete;
        MountTable& operator=(const MountTable&) = delete;

        [[nodiscard]] Result Mount(const PackageReader& package, i32 priority) noexcept;
        [[nodiscard]] bool Unmount(const PackageReader& package) noexcept;
        void Clear() noexcept;

        [[nodiscard]] ResolvedResource Resolve(ResourceId id) const noexcept;
        [[nodiscard]] ResolvedResource Resolve(containers::StringView path) const noexcept;
        [[nodiscard]] u32 Count() const noexcept;

    private:
        struct MountedPackage
        {
            const PackageReader* package = nullptr;
            i32 priority = 0;
            u64 sequence = 0;
        };

        containers::DynamicArray<MountedPackage> m_mounts;
        u64 m_nextSequence = 0;
    };
} // namespace vanguard::packages

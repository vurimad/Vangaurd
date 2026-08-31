#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::meshes
{
    inline constexpr u32 MeshMagic = serialization::MakeFourCC('V', 'M', 'S', 'H');
    inline constexpr resources::ResourceTypeId MeshResourceType = serialization::MakeFourCC('V', 'M', 'S', 'H');
    inline constexpr u32 InvalidRecordIndex = 0xffffffffu;

    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        InvalidMagic,
        UnsupportedVersion,
        InvalidLayout,
        IntegrityFailure,
        LimitExceeded,
        DuplicateIdentifier,
        DuplicateStream,
        MissingPositionStream,
        InvalidLod,
        InvalidSubmesh,
        InvalidBuffer,
        InvalidPage,
        InvalidMaterial,
        DependencyMismatch,
        BufferTooSmall,
        Cancelled,
        IoFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class MeshKind : u8
    {
        Static,
        Skinned
    };

    enum class PrimitiveTopology : u8
    {
        PointList,
        LineList,
        TriangleList
    };

    enum class IndexFormat : u8
    {
        UInt16,
        UInt32
    };

    enum class BufferKind : u8
    {
        Vertex,
        Index,
        RayTracingPositions,
        Custom
    };

    enum class VertexSemantic : u8
    {
        Position,
        Normal,
        Tangent,
        TexCoord,
        Color,
        JointIndices,
        JointWeights,
        MorphPosition,
        Custom
    };

    enum class VertexFormat : u8
    {
        R32Float,
        R32G32Float,
        R32G32B32Float,
        R32G32B32A32Float,
        R16G16Float,
        R16G16B16A16Float,
        R16G16SNorm,
        R16G16B16A16SNorm,
        R16G16UNorm,
        R16G16B16A16UNorm,
        R8G8B8A8UNorm,
        R8G8B8A8SNorm,
        R8G8B8A8UInt,
        R16G16B16A16UInt,
        R32UInt,
        R10G10B10A2UNorm
    };

    enum class PageFlags : u16
    {
        None = 0,
        RequiredForLowestLod = 1u << 0u,
        DirectGpuUpload = 1u << 1u
    };

    [[nodiscard]] constexpr PageFlags operator|(const PageFlags left, const PageFlags right) noexcept
    {
        return static_cast<PageFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(const PageFlags value, const PageFlags flag) noexcept
    {
        return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0;
    }

    enum class SubmeshFlags : u16
    {
        None = 0,
        CastsShadow = 1u << 0u,
        RayTracing = 1u << 1u,
        TwoSided = 1u << 2u
    };

    [[nodiscard]] constexpr SubmeshFlags operator|(const SubmeshFlags left, const SubmeshFlags right) noexcept
    {
        return static_cast<SubmeshFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    struct Bounds
    {
        f32 minimum[3]{};
        f32 maximum[3]{};
        f32 sphereCenter[3]{};
        f32 sphereRadius = 0.0f;
    };

    /// Object-space decode parameters for a packed position stream.
    ///
    /// A Position stream stored as R16G16B16A16SNorm is reconstructed as:
    ///     objectPosition.xyz = normalizedInput.xyz * scale.xyz + bias.xyz
    ///
    /// The fourth component is stored as positive one and is not part of the
    /// object-space position. The parameters are mesh-wide so every submesh and
    /// LOD shares one stable decode transform.
    struct PositionQuantization
    {
        f32 scale[3]{1.0f, 1.0f, 1.0f};
        f32 bias[3]{};
    };

    struct BufferBuildRecord
    {
        u32 id = 0;
        BufferKind kind = BufferKind::Vertex;
        u32 stride = 0;
        u64 byteSize = 0;
    };

    struct PageBuildRecord
    {
        u32 bufferId = 0;
        u64 bufferOffset = 0;
        const void* data = nullptr;
        usize byteSize = 0;
        u8 alignmentLog2 = 4;
        PageFlags flags = PageFlags::None;
    };

    struct VertexStreamBuildRecord
    {
        u32 layoutId = 0;
        VertexSemantic semantic = VertexSemantic::Position;
        u8 semanticIndex = 0;
        VertexFormat format = VertexFormat::R32G32B32Float;
        u8 binding = 0;
        u32 bufferId = 0;
        u32 byteOffset = 0;
        u32 stride = 0;
    };

    struct VertexLayoutBuildRecord
    {
        u32 id = 0;
    };

    struct MaterialSlotBuildRecord
    {
        u32 id = 0;
        u64 name = 0;
        resources::ResourceReference material;
    };

    struct LodBuildRecord
    {
        u16 level = 0;
        f32 minimumScreenCoverage = 1.0f;
    };

    struct SubmeshBuildRecord
    {
        u64 stableId = 0;
        u64 name = 0;
        u16 lod = 0;
        u32 materialSlotId = 0;
        u32 vertexLayoutId = 0;
        u32 indexBufferId = 0;
        IndexFormat indexFormat = IndexFormat::UInt16;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        SubmeshFlags flags = SubmeshFlags::CastsShadow;
        u64 firstVertex = 0;
        u32 vertexCount = 0;
        u64 firstIndex = 0;
        u32 indexCount = 0;
        Bounds bounds;
    };

    struct BuildDescription
    {
        MeshKind kind = MeshKind::Static;
        u64 name = 0;
        Bounds bounds;
        PositionQuantization positionQuantization;
        crypto::Digest256 sourceFingerprint;
        resources::ResourceReference skeleton;
        containers::ArraySpan<const BufferBuildRecord> buffers;
        containers::ArraySpan<const PageBuildRecord> pages;
        containers::ArraySpan<const VertexLayoutBuildRecord> vertexLayouts;
        containers::ArraySpan<const VertexStreamBuildRecord> vertexStreams;
        containers::ArraySpan<const MaterialSlotBuildRecord> materialSlots;
        containers::ArraySpan<const LodBuildRecord> lods;
        containers::ArraySpan<const SubmeshBuildRecord> submeshes;
    };

    struct BufferRecord
    {
        BufferKind kind = BufferKind::Vertex;
        u32 stride = 0;
        u64 byteSize = 0;
        u32 firstPage = 0;
        u32 pageCount = 0;
    };

    struct PageRecord
    {
        u32 buffer = InvalidRecordIndex;
        PageFlags flags = PageFlags::None;
        u8 alignmentLog2 = 0;
        u64 bufferOffset = 0;
        u64 dataOffset = 0;
        u64 byteSize = 0;
        crypto::Digest256 digest;
    };

    struct VertexStream
    {
        VertexSemantic semantic = VertexSemantic::Position;
        u8 semanticIndex = 0;
        VertexFormat format = VertexFormat::R32G32B32Float;
        u8 binding = 0;
        u32 buffer = InvalidRecordIndex;
        u32 byteOffset = 0;
        u32 stride = 0;
    };

    struct VertexLayoutRecord
    {
        u32 firstStream = 0;
        u32 streamCount = 0;
        crypto::Digest256 fingerprint;
    };

    struct MaterialSlot
    {
        u64 name = 0;
        resources::ResourceReference material;
    };

    struct LodRecord
    {
        f32 minimumScreenCoverage = 1.0f;
        u32 firstSubmesh = 0;
        u32 submeshCount = 0;
    };

    struct SubmeshRecord
    {
        u64 stableId = 0;
        u64 name = 0;
        u32 materialSlot = InvalidRecordIndex;
        u32 vertexLayout = InvalidRecordIndex;
        u32 indexBuffer = InvalidRecordIndex;
        IndexFormat indexFormat = IndexFormat::UInt16;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        SubmeshFlags flags = SubmeshFlags::None;
        u64 firstVertex = 0;
        u32 vertexCount = 0;
        u64 firstIndex = 0;
        u32 indexCount = 0;
        Bounds bounds;
    };

    struct ReadLimits
    {
        u64 maximumFileSize = 16ull * 1024ull * 1024ull * 1024ull;
        u64 maximumMetadataBytes = 256ull * 1024ull * 1024ull;
        u64 maximumGeometryBytes = 15ull * 1024ull * 1024ull * 1024ull;
        u64 maximumPageBytes = 256ull * 1024ull * 1024ull;
        u32 maximumBuffers = 65536;
        u32 maximumPages = 65536;
        u32 maximumVertexLayouts = 65536;
        u32 maximumVertexStreams = 1048576;
        u32 maximumMaterialSlots = 65536;
        u32 maximumLods = 64;
        u32 maximumSubmeshes = 1048576;
    };

    enum class StorageSegmentFlags : u8
    {
        None = 0,
        Metadata = 1u << 0u,
        Streamable = 1u << 1u,
        RequiredForLowestLod = 1u << 2u
    };

    [[nodiscard]] constexpr StorageSegmentFlags operator|(const StorageSegmentFlags left, const StorageSegmentFlags right) noexcept
    {
        return static_cast<StorageSegmentFlags>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(const StorageSegmentFlags value, const StorageSegmentFlags flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }

    /// Byte-exact segment of a complete vmesh document. The ordered segments
    /// cover the complete document. Segment zero contains the envelope and
    /// metadata; every later segment begins at one geometry page.
    struct StorageSegment
    {
        u64 offset = 0;
        u64 byteSize = 0;
        u8 alignmentLog2 = 0;
        StorageSegmentFlags flags = StorageSegmentFlags::None;
        u32 page = InvalidRecordIndex;
    };

    class MeshFile final
    {
    public:
        MeshFile() noexcept;
        ~MeshFile() = default;

        MeshFile(const MeshFile&) = delete;
        MeshFile& operator=(const MeshFile&) = delete;
        MeshFile(MeshFile&& other) noexcept;
        MeshFile& operator=(MeshFile&& other) noexcept;

        [[nodiscard]] Result Open(filesystem::IFile& reader, const ReadLimits& limits = {}) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] MeshKind GetKind() const noexcept;
        [[nodiscard]] u64 GetName() const noexcept;
        [[nodiscard]] const Bounds& GetMeshBounds() const noexcept;
        [[nodiscard]] const PositionQuantization& GetQuantization() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetSourceFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetContentFingerprint() const noexcept;
        [[nodiscard]] resources::ResourceReference GetSkeleton() const noexcept;
        [[nodiscard]] containers::ArraySpan<const BufferRecord> GetBuffers() const noexcept;
        [[nodiscard]] containers::ArraySpan<const PageRecord> GetPages() const noexcept;
        [[nodiscard]] containers::ArraySpan<const VertexLayoutRecord> GetVertexLayouts() const noexcept;
        [[nodiscard]] containers::ArraySpan<const VertexStream> GetVertexStreams() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialSlot> GetMaterialSlots() const noexcept;
        [[nodiscard]] containers::ArraySpan<const LodRecord> GetLods() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SubmeshRecord> GetSubmeshes() const noexcept;
        [[nodiscard]] u64 GetGeometryOffset() const noexcept;
        [[nodiscard]] u64 GetGeometrySize() const noexcept;

        [[nodiscard]] Result ReadPage(filesystem::IFile& reader, u32 pageIndex, void* destination, usize capacity) const noexcept;

    private:
        bool m_open = false;
        MeshKind m_kind = MeshKind::Static;
        u64 m_name = 0;
        Bounds m_bounds;
        PositionQuantization m_quantization;
        crypto::Digest256 m_sourceFingerprint;
        crypto::Digest256 m_contentFingerprint;
        resources::ResourceReference m_skeleton;
        u64 m_geometryOffset = 0;
        u64 m_geometrySize = 0;
        containers::DynamicArray<BufferRecord> m_buffers;
        containers::DynamicArray<PageRecord> m_pages;
        containers::DynamicArray<VertexLayoutRecord> m_vertexLayouts;
        containers::DynamicArray<VertexStream> m_vertexStreams;
        containers::DynamicArray<MaterialSlot> m_materialSlots;
        containers::DynamicArray<LodRecord> m_lods;
        containers::DynamicArray<SubmeshRecord> m_submeshes;
    };

    [[nodiscard]] Result WriteMesh(filesystem::IFile& writer, const BuildDescription& description) noexcept;
    [[nodiscard]] u32 GetVertexFormatByteSize(VertexFormat format) noexcept;
    [[nodiscard]] Result BuildStorageSegments(const MeshFile& mesh, u64 documentSize, containers::DynamicArray<StorageSegment>& segments, u32 maximumSegments = 65536) noexcept;
    [[nodiscard]] Result CollectLodPages(const MeshFile& mesh, u16 lod, containers::DynamicArray<u32>& pages, u32 maximumPages = 65536) noexcept;
} // namespace vanguard::meshes

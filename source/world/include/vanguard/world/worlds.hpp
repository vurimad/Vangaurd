#pragma once

#include <vanguard/meshes/meshes.hpp>
#include <vanguard/world/cells.hpp>

namespace vanguard::world
{
    inline constexpr u32 WorldMagic = serialization::MakeFourCC('V', 'W', 'L', 'D');
    inline constexpr resources::ResourceTypeId WorldResourceType = serialization::MakeFourCC('V', 'W', 'L', 'D');
    inline constexpr u64 InvalidCellId = 0;
    inline constexpr u64 InvalidProxyId = 0;

    /// Larger values stream first. Spaced specialized values
    /// leave room for later tiers without rebuilding every cooked world.
    enum class StreamingPriority : u8
    {
        Low = 0,
        Normal = 1,
        Reserved = 2,
        StaticMesh = 20,
        StaticEntity = 21,
        OtherPrefab = 30,
        BuildingMesh = 40,
        BuildingPrefab = 50,
        FlatPath = 60,
        Critical = 254,
        Triggers = 255
    };

    enum class WorldCellFlags : u32
    {
        None = 0,
        AlwaysLoaded = 1u << 0u,
        Interior = 1u << 1u,
        Mission = 1u << 2u,
        TwoDimensionalStreaming = 1u << 3u,
        EditorOnly = 1u << 4u,
        AllowDistanceBoosting = 1u << 5u
    };

    enum class DistantProxyFlags : u32
    {
        None = 0,
        ProxyOnly = 1u << 0u,
        Building = 1u << 1u,
        Terrain = 1u << 2u,
        Road = 1u << 3u,
        Water = 1u << 4u,
        Decoration = 1u << 5u,
        Mission = 1u << 6u,
        TwoDimensionalStreaming = 1u << 7u,
        KeepResident = 1u << 8u,
        EditorOnly = 1u << 9u,
        AllowDistanceBoosting = 1u << 10u
    };

    enum class ProxyChildKind : u8
    {
        Cell,
        Proxy
    };

    enum class ProxyChildFlags : u8
    {
        None = 0,
        RequiredForReplacement = 1u << 0u
    };

    /// Double-precision global bounds used by the coarse world index. Fine placement
    /// bounds remain single-precision and relative to their vcell origin.
    struct WorldBounds
    {
        f64 minimum[3]{};
        f64 maximum[3]{};
    };

    struct WorldCellBuildRecord
    {
        u64 cellId = InvalidCellId;
        u64 parentCellId = InvalidCellId;
        u64 name = 0;
        resources::ResourceReference cell;
        i32 gridCoordinate[3]{};
        u8 hierarchyLevel = 0;
        CellCategory category = CellCategory::Generic;
        /// Larger values are scheduled first.
        StreamingPriority streamingPriority = StreamingPriority::Normal;
        f64 origin[3]{};
        WorldBounds bounds;
        f64 streamingReferencePoint[3]{};
        f32 activationDistance = 0.0f;
        f32 retentionDistance = 0.0f;
        WorldCellFlags flags = WorldCellFlags::None;
    };

    /// A far representation is a render-only vmesh. The vmesh carries its material
    /// references, while this record controls world residency and readiness-gated replacement.
    struct DistantProxyBuildRecord
    {
        u64 proxyId = InvalidProxyId;
        u64 parentProxyId = InvalidProxyId;
        u64 ownerCellId = InvalidCellId;
        u64 name = 0;
        resources::ResourceReference mesh;
        WorldBounds bounds;
        f64 streamingReferencePoint[3]{};
        f64 preboostStreamingReferencePoint[3]{};
        f64 secondaryReferencePoint[3]{};
        f32 streamingDistance = 0.0f;
        f32 secondaryReferencePointDistance = 0.0f;
        f32 nearHideDistance = 0.0f;
        StreamingPriority streamingPriority = StreamingPriority::BuildingMesh;
        DistantProxyFlags flags = DistantProxyFlags::None;
    };

    /// Identifies a cell or a finer proxy that must reach render-ready state before
    /// the owning far proxy is allowed to hide. Optional children do not gate replacement.
    struct ProxyChildBuildRecord
    {
        u64 proxyId = InvalidProxyId;
        u64 childId = 0;
        ProxyChildKind kind = ProxyChildKind::Cell;
        ProxyChildFlags flags = ProxyChildFlags::RequiredForReplacement;
    };

    struct WorldBuildDescription
    {
        u64 worldId = 0;
        f64 origin[3]{};
        WorldBounds bounds;
        containers::ArraySpan<const WorldCellBuildRecord> cells;
        containers::ArraySpan<const DistantProxyBuildRecord> distantProxies;
        containers::ArraySpan<const ProxyChildBuildRecord> proxyChildren;
        crypto::Digest256 sourceFingerprint;
        bool includeEditorData = false;
    };

    struct WorldCellRecord
    {
        u64 cellId = InvalidCellId;
        u64 parentCellId = InvalidCellId;
        u64 name = 0;
        resources::ResourceReference cell;
        i32 gridCoordinate[3]{};
        u8 hierarchyLevel = 0;
        CellCategory category = CellCategory::Generic;
        StreamingPriority streamingPriority = StreamingPriority::Normal;
        f64 origin[3]{};
        WorldBounds bounds;
        f64 streamingReferencePoint[3]{};
        f32 activationDistance = 0.0f;
        f32 retentionDistance = 0.0f;
        WorldCellFlags flags = WorldCellFlags::None;
    };

    struct DistantProxyRecord
    {
        u64 proxyId = InvalidProxyId;
        u64 parentProxyId = InvalidProxyId;
        u64 ownerCellId = InvalidCellId;
        u64 name = 0;
        resources::ResourceReference mesh;
        WorldBounds bounds;
        f64 streamingReferencePoint[3]{};
        f64 preboostStreamingReferencePoint[3]{};
        f64 secondaryReferencePoint[3]{};
        f32 streamingDistance = 0.0f;
        f32 secondaryReferencePointDistance = 0.0f;
        f32 nearHideDistance = 0.0f;
        u32 firstChild = 0;
        u32 childCount = 0;
        DistantProxyFlags flags = DistantProxyFlags::None;
        StreamingPriority streamingPriority = StreamingPriority::BuildingMesh;
    };

    struct ProxyChildRecord
    {
        u64 proxyId = InvalidProxyId;
        u64 childId = 0;
        ProxyChildKind kind = ProxyChildKind::Cell;
        ProxyChildFlags flags = ProxyChildFlags::RequiredForReplacement;
    };

    struct WorldReadLimits
    {
        u64 maximumFileSize = 256ull * 1024ull * 1024ull;
        u32 maximumCells = 1u << 20u;
        u32 maximumDistantProxies = 1u << 20u;
        u32 maximumProxyChildren = 1u << 22u;
        u32 maximumDependencies = 1u << 21u;
    };

    class WorldFile final
    {
    public:
        WorldFile() noexcept;

        WorldFile(const WorldFile&) = delete;
        WorldFile& operator=(const WorldFile&) = delete;

        [[nodiscard]] Result Open(filesystem::IFile& reader, const WorldReadLimits& limits = {}) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] u64 WorldId() const noexcept;
        [[nodiscard]] const f64* Origin() const noexcept;
        [[nodiscard]] const WorldBounds& Bounds() const noexcept;
        [[nodiscard]] const crypto::Digest256& SourceFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& ContentFingerprint() const noexcept;
        [[nodiscard]] containers::ArraySpan<const WorldCellRecord> Cells() const noexcept;
        [[nodiscard]] containers::ArraySpan<const DistantProxyRecord> DistantProxies() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ProxyChildRecord> ProxyChildren() const noexcept;
        [[nodiscard]] containers::ArraySpan<const DependencyRecord> Dependencies() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ProxyChildRecord> ChildrenOf(const DistantProxyRecord& proxy) const noexcept;
        [[nodiscard]] const WorldCellRecord* FindCell(u64 cellId) const noexcept;
        [[nodiscard]] const DistantProxyRecord* FindDistantProxy(u64 proxyId) const noexcept;

    private:
        u64 m_worldId = 0;
        f64 m_origin[3]{};
        WorldBounds m_bounds;
        crypto::Digest256 m_sourceFingerprint;
        crypto::Digest256 m_contentFingerprint;
        containers::DynamicArray<WorldCellRecord> m_cells;
        containers::DynamicArray<DistantProxyRecord> m_distantProxies;
        containers::DynamicArray<ProxyChildRecord> m_proxyChildren;
        containers::DynamicArray<DependencyRecord> m_dependencies;
        bool m_open = false;
    };

    class WorldResource final : public resources::ResourceObject
    {
    public:
        [[nodiscard]] resources::ResourceTypeId Type() const noexcept override;
        [[nodiscard]] Result Open(const void* data, usize size, const WorldReadLimits& limits = {}) noexcept;
        [[nodiscard]] const WorldFile& File() const noexcept;

    private:
        WorldFile m_file;
    };

    [[nodiscard]] Result CookWorld(const WorldBuildDescription& description, filesystem::IFile& output) noexcept;
    [[nodiscard]] Result CalculateWorldContentFingerprint(const WorldBuildDescription& description,
                                                          crypto::Digest256& fingerprint) noexcept;
} // namespace vanguard::world

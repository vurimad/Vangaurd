#pragma once

#include <vanguard/rendering/gpu_scene_types.hpp>
#include <vanguard/rhi/rhi.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumGpuScenePagesPerTable = 64;

    enum class GpuSceneTableKind : u8
    {
        Instance,
        Motion,
        Renderable,
        RenderableResidency,
        Lod,
        Primitive,
        PrimitivePlacement,
        PhaseParticipation,
        PhasePlacement,
        GeometryRange,
        VertexStream,
        PositionDecode,
        Material,
        MaterialResource,
        MaterialSet,
        MaterialIndex,
        Light,
        Decal,
        TextureResidency,
        Count
    };

    inline constexpr u32 GpuSceneTableCount = static_cast<u32>(GpuSceneTableKind::Count);
    static_assert(static_cast<u32>(GpuSceneTableKind::Instance) == 0);
    static_assert(static_cast<u32>(GpuSceneTableKind::RenderableResidency) == 3);
    static_assert(static_cast<u32>(GpuSceneTableKind::PrimitivePlacement) == 6);
    static_assert(static_cast<u32>(GpuSceneTableKind::PhasePlacement) == 8);
    static_assert(static_cast<u32>(GpuSceneTableKind::MaterialIndex) == 15);
    static_assert(static_cast<u32>(GpuSceneTableKind::Decal) == 17);
    static_assert(static_cast<u32>(GpuSceneTableKind::TextureResidency) == 18);
    static_assert(GpuSceneTableCount == 19);

    [[nodiscard]] constexpr bool IsGpuSceneParallelTable(const GpuSceneTableKind kind) noexcept
    {
        return kind == GpuSceneTableKind::RenderableResidency || kind == GpuSceneTableKind::PrimitivePlacement ||
               kind == GpuSceneTableKind::PhasePlacement;
    }

    [[nodiscard]] constexpr GpuSceneTableKind GetGpuSceneParallelTable(const GpuSceneTableKind owner) noexcept
    {
        switch (owner)
        {
        case GpuSceneTableKind::Renderable:
            return GpuSceneTableKind::RenderableResidency;
        case GpuSceneTableKind::Primitive:
            return GpuSceneTableKind::PrimitivePlacement;
        case GpuSceneTableKind::PhaseParticipation:
            return GpuSceneTableKind::PhasePlacement;
        default:
            return GpuSceneTableKind::Count;
        }
    }

    [[nodiscard]] constexpr GpuSceneTableKind GetGpuSceneParallelOwner(const GpuSceneTableKind parallel) noexcept
    {
        switch (parallel)
        {
        case GpuSceneTableKind::RenderableResidency:
            return GpuSceneTableKind::Renderable;
        case GpuSceneTableKind::PrimitivePlacement:
            return GpuSceneTableKind::Primitive;
        case GpuSceneTableKind::PhasePlacement:
            return GpuSceneTableKind::PhaseParticipation;
        default:
            return GpuSceneTableKind::Count;
        }
    }

    template <typename T> inline constexpr GpuSceneTableKind GpuSceneTableKindOf = GpuSceneTableKind::Count;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuInstance> = GpuSceneTableKind::Instance;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuMotion> = GpuSceneTableKind::Motion;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuRenderable> = GpuSceneTableKind::Renderable;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuRenderableResidency> = GpuSceneTableKind::RenderableResidency;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuLod> = GpuSceneTableKind::Lod;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuPrimitive> = GpuSceneTableKind::Primitive;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuPrimitivePlacement> = GpuSceneTableKind::PrimitivePlacement;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuPhaseParticipation> = GpuSceneTableKind::PhaseParticipation;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuPhasePlacement> = GpuSceneTableKind::PhasePlacement;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuGeometryRange> = GpuSceneTableKind::GeometryRange;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuVertexStream> = GpuSceneTableKind::VertexStream;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuPositionDecode> = GpuSceneTableKind::PositionDecode;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuMaterial> = GpuSceneTableKind::Material;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuMaterialResource> = GpuSceneTableKind::MaterialResource;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuMaterialSet> = GpuSceneTableKind::MaterialSet;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuMaterialIndex> = GpuSceneTableKind::MaterialIndex;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuLight> = GpuSceneTableKind::Light;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuDecal> = GpuSceneTableKind::Decal;
    template <> inline constexpr GpuSceneTableKind GpuSceneTableKindOf<GpuTextureResidency> = GpuSceneTableKind::TextureResidency;

    [[nodiscard]] constexpr u32 GetGpuSceneElementsPerPage(const GpuSceneTableKind kind) noexcept
    {
        switch (kind)
        {
        case GpuSceneTableKind::Instance:
            return 16'384;
        case GpuSceneTableKind::Motion:
            return 16'384;
        case GpuSceneTableKind::Renderable:
        case GpuSceneTableKind::RenderableResidency:
            return 32'768;
        case GpuSceneTableKind::Lod:
            return 65'536;
        case GpuSceneTableKind::Primitive:
        case GpuSceneTableKind::PrimitivePlacement:
            return 32'768;
        case GpuSceneTableKind::PhaseParticipation:
        case GpuSceneTableKind::PhasePlacement:
            return 65'536;
        case GpuSceneTableKind::GeometryRange:
            return 16'384;
        case GpuSceneTableKind::VertexStream:
            return 65'536;
        case GpuSceneTableKind::PositionDecode:
            return 32'768;
        case GpuSceneTableKind::Material:
            return 32'768;
        case GpuSceneTableKind::MaterialResource:
            return 65'536;
        case GpuSceneTableKind::MaterialSet:
            return 65'536;
        case GpuSceneTableKind::MaterialIndex:
            return 262'144;
        case GpuSceneTableKind::Light:
            return 8'192;
        case GpuSceneTableKind::Decal:
            return 16'384;
        case GpuSceneTableKind::TextureResidency:
            return 65'536;
        default:
            return 0;
        }
    }

    template <typename T> [[nodiscard]] constexpr u32 GetGpuSceneElementsPerPage() noexcept
    {
        static_assert(GpuSceneTableKindOf<T> != GpuSceneTableKind::Count, "type is not a GPU Scene table element");
        return GetGpuSceneElementsPerPage(GpuSceneTableKindOf<T>);
    }

    template <typename T> [[nodiscard]] constexpr u32 GpuSceneElementsPerPage() noexcept
    {
        return GetGpuSceneElementsPerPage<T>();
    }

    [[nodiscard]] constexpr u32 GetGpuScenePageShift(const GpuSceneTableKind kind) noexcept
    {
        u32 elements = GetGpuSceneElementsPerPage(kind);
        u32 shift = 0;
        while (elements > 1u)
        {
            elements >>= 1u;
            ++shift;
        }
        return shift;
    }

    struct GpuSceneLinearAddress
    {
        u32 page = 0;
        u32 element = 0;
    };

    template <typename T> [[nodiscard]] constexpr GpuSceneLinearAddress DecodeGpuSceneIndex(const u32 index) noexcept
    {
        constexpr u32 elementsPerPage = GpuSceneElementsPerPage<T>();
        static_assert((elementsPerPage & (elementsPerPage - 1u)) == 0, "GPU Scene page sizes must be powers of two");
        constexpr u32 pageShift = GetGpuScenePageShift(GpuSceneTableKindOf<T>);
        return {index >> pageShift, index & (elementsPerPage - 1u)};
    }

    struct alignas(16) GpuScenePageDirectoryEntry
    {
        u32 shaderResourceDescriptor = InvalidGpuDescriptorIndex;
        u32 unorderedAccessDescriptor = InvalidGpuDescriptorIndex;
        u32 baseElement = 0;
        u32 elementCount = 0;
    };

    struct alignas(16) GpuSceneTableDirectory
    {
        u32 firstPageEntry = 0;
        u32 pageCapacity = 0;
        u32 pageShift = 0;
        u32 pageMask = 0;

        u32 elementsPerPage = 0;
        u32 elementStride = 0;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
    };

    struct GpuSceneDirectoryBinding
    {
        u32 tableDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 pageDirectoryDescriptor = InvalidGpuDescriptorIndex;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return tableDirectoryDescriptor != InvalidGpuDescriptorIndex && pageDirectoryDescriptor != InvalidGpuDescriptorIndex;
        }
    };

    /// Non-owning page snapshot. It remains valid until the table owner is shut down.
    struct GpuSceneTablePage
    {
        rhi::BufferRef buffer;
        rhi::DescriptorHandle shaderResourceDescriptor;
        rhi::DescriptorHandle unorderedAccessDescriptor;
        u32 firstElement = 0;
        u32 elementCount = 0;

        [[nodiscard]] constexpr bool IsMaterialized() const noexcept
        {
            return buffer.IsValid() && shaderResourceDescriptor.IsValid() && unorderedAccessDescriptor.IsValid();
        }
    };

    struct GpuSceneElementAddress
    {
        rhi::BufferRef buffer;
        u32 page = 0;
        u32 element = 0;
        u64 byteOffset = 0;
        u32 shaderResourceDescriptor = InvalidGpuDescriptorIndex;
        u32 unorderedAccessDescriptor = InvalidGpuDescriptorIndex;
    };

    struct GpuSceneTableStats
    {
        u32 materializedPages = 0;
        u32 maximumPages = 0;
        u32 elementsPerPage = 0;
        u32 elementStride = 0;
        u32 elementCapacity = 0;
        u64 allocatedBytes = 0;
        u64 pageCreationFailures = 0;
    };

    struct GpuSceneTablesStats
    {
        u32 materializedPages = 0;
        u32 reservedDescriptors = 0;
        u32 populatedDescriptors = 0;
        u64 allocatedBytes = 0;
        u64 rejectedOperations = 0;
        u64 pageCreationFailures = 0;
    };

    struct GpuSceneTablesConfig
    {
        /// The table owner retains an RHI reference to this global bindless resource domain.
        rhi::DescriptorDomainRef resourceDescriptors;
        u32 maximumPagesPerTable = MaximumGpuScenePagesPerTable;
    };

    enum class GpuSceneTablesFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        RhiUnavailable,
        BindlessUnsupported,
        InvalidConfiguration,
        InvalidTable,
        CapacityExceeded,
        DescriptorCapacityExceeded,
        DescriptorFailure,
        BufferFailure
    };

    struct GpuSceneTablesFailure
    {
        GpuSceneTablesFailureCode code = GpuSceneTablesFailureCode::None;
        GpuSceneTableKind table = GpuSceneTableKind::Count;
        u32 page = 0;
        const char* message = nullptr;
        rhi::Failure rhiFailure;
    };

    /// Owns persistent GPU Scene pages and their stable bindless identities. Configuration and growth are
    /// main-thread operations; render work consumes immutable page snapshots after frame publication.
    class GpuSceneTables final
    {
    public:
        struct Impl;

        GpuSceneTables() noexcept = default;
        ~GpuSceneTables();

        GpuSceneTables(const GpuSceneTables&) = delete;
        GpuSceneTables& operator=(const GpuSceneTables&) = delete;

        [[nodiscard]] bool Initialize(const GpuSceneTablesConfig& config, GpuSceneTablesFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(const rhi::DescriptorRetirement& safeAfter, GpuSceneTablesFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        template <typename T> [[nodiscard]] bool EnsureCapacity(const u32 requiredElements, GpuSceneTablesFailure* const failure = nullptr) noexcept
        {
            static_assert(GpuSceneTableKindOf<T> != GpuSceneTableKind::Count, "type is not a GPU Scene table element");
            return EnsureCapacity(GpuSceneTableKindOf<T>, requiredElements, failure);
        }

        [[nodiscard]] bool EnsureCapacity(GpuSceneTableKind kind, u32 requiredElements, GpuSceneTablesFailure* failure = nullptr) noexcept;

        template <typename T> [[nodiscard]] bool GetPage(const u32 page, GpuSceneTablePage& output) const noexcept
        {
            static_assert(GpuSceneTableKindOf<T> != GpuSceneTableKind::Count, "type is not a GPU Scene table element");
            return GetPageRaw(GpuSceneTableKindOf<T>, sizeof(T), page, output);
        }

        [[nodiscard]] bool GetPage(GpuSceneTableKind kind, u32 page, GpuSceneTablePage& output) const noexcept;

        template <typename T> [[nodiscard]] bool Resolve(const u32 index, GpuSceneElementAddress& output) const noexcept
        {
            static_assert(GpuSceneTableKindOf<T> != GpuSceneTableKind::Count, "type is not a GPU Scene table element");
            return ResolveRaw(GpuSceneTableKindOf<T>, sizeof(T), index, output);
        }

        [[nodiscard]] bool Resolve(GpuSceneTableKind kind, u32 index, GpuSceneElementAddress& output) const noexcept;

        template <typename T> [[nodiscard]] GpuSceneTableStats GetTableStats() const noexcept
        {
            static_assert(GpuSceneTableKindOf<T> != GpuSceneTableKind::Count, "type is not a GPU Scene table element");
            return GetTableStatsRaw(GpuSceneTableKindOf<T>, sizeof(T));
        }

        [[nodiscard]] GpuSceneTableStats GetTableStats(GpuSceneTableKind kind) const noexcept;

        [[nodiscard]] GpuSceneDirectoryBinding GetDirectoryBinding() const noexcept;
        [[nodiscard]] bool GetTableDirectory(GpuSceneTableKind kind, GpuSceneTableDirectory& output) const noexcept;
        [[nodiscard]] bool GetPageDirectory(GpuSceneTableKind kind, u32 page, GpuScenePageDirectoryEntry& output) const noexcept;
        [[nodiscard]] GpuSceneTablesStats GetStats() const noexcept;

    private:
        [[nodiscard]] bool EnsureCapacityRaw(GpuSceneTableKind kind, u32 stride, u32 requiredElements, GpuSceneTablesFailure* failure) noexcept;
        [[nodiscard]] bool GetPageRaw(GpuSceneTableKind kind, u32 stride, u32 page, GpuSceneTablePage& output) const noexcept;
        [[nodiscard]] bool ResolveRaw(GpuSceneTableKind kind, u32 stride, u32 index, GpuSceneElementAddress& output) const noexcept;
        [[nodiscard]] GpuSceneTableStats GetTableStatsRaw(GpuSceneTableKind kind, u32 stride) const noexcept;

        Impl* m_impl = nullptr;
    };

    static_assert(sizeof(GpuScenePageDirectoryEntry) == 16);
    static_assert(sizeof(GpuSceneTableDirectory) == 32);
} // namespace vanguard::rendering

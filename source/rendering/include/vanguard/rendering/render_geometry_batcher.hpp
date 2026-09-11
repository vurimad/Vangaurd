#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/rendering/geometry_allocator.hpp>
#include <vanguard/rendering/gpu_scene_types.hpp>
#include <vanguard/rendering/render_phase.hpp>

namespace vanguard::rendering
{
    class MaterialResidencyRuntime;
    class MeshResidencyManager;
    class RenderGeometryBatcher;
    struct MeshDrawPreparation;
    struct RetainedGeometryBatch;

    struct GeometryBatchId
    {
        u32 index = InvalidGeometryIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return index != InvalidGeometryIndex && generation != 0; }
        [[nodiscard]] friend constexpr bool operator==(const GeometryBatchId&, const GeometryBatchId&) noexcept = default;
    };

    // Shared recording state, not per-view draw work. Pipeline identity includes
    // the native interface, vertex layout, attachments and fixed pipeline state.
    // The static-surface path uses no per-object stencil/blend/depth-bias overrides.
    struct GeometryShellKey
    {
        RenderPhaseId phase;
        rhi::PipelineRef pipeline;
        VertexArenaSetId vertexArena;
        IndexArenaId indexArena;
        rhi::IndexFormat indexFormat = rhi::IndexFormat::UInt16;
        // Derived once from the admitted immutable PSO, used to reject a view
        // whose depth convention cannot be drawn by this shell.
        bool depthTest = false;
        bool reverseDepth = false;

        [[nodiscard]] u32 CalcHash() const noexcept;
        [[nodiscard]] friend constexpr bool operator==(const GeometryShellKey&, const GeometryShellKey&) noexcept = default;
    };

    struct GeometryBinKey
    {
        GeometryBatchId shell;
        GpuGeometryHandle geometry;
        GeometryAllocationId allocation;
        u32 firstIndex = 0;
        u32 indexCount = 0;
        u32 firstVertex = 0;
        u32 vertexCount = 0;

        [[nodiscard]] u32 CalcHash() const noexcept;
        [[nodiscard]] friend constexpr bool operator==(const GeometryBinKey&, const GeometryBinKey&) noexcept = default;
    };

    struct GeometryBatchPlacement
    {
        GeometryBatchId normalShell;
        GeometryBatchId normalBin;
        GeometryBatchId mirroredShell;
        GeometryBatchId mirroredBin;
        GeometryShellKey normalState;
        GeometryShellKey mirroredState;
        GeometryBinKey normalGeometry;
        GeometryBinKey mirroredGeometry;
        GpuMaterialHandle material;
        u32 sourceSubmesh = InvalidGeometryIndex;
    };

    // Borrowed recording catalog entries. The batcher remains their only owner;
    // callers may read them after owner-thread mutation has joined.
    struct GeometryShellCatalogEntry
    {
        GeometryBatchId id;
        GeometryShellKey state;
        u32 binCount = 0;
        bool active = false;
    };

    struct GeometryBinCatalogEntry
    {
        GeometryBatchId id;
        GeometryBatchId shell;
        GpuGeometryHandle geometry;
        GeometryAllocationId allocation;
        u32 shellOrdinal = InvalidGeometryIndex;
        u32 firstIndex = 0;
        u32 indexCount = 0;
        u32 firstVertex = 0;
        u32 vertexCount = 0;
        bool active = false;
    };

    struct GeometryBatchCatalogChanges
    {
        containers::ArraySpan<const u32> shellIndices;
        containers::ArraySpan<const u32> binIndices;
        u64 revision = 0;

        [[nodiscard]] bool Empty() const noexcept { return shellIndices.Empty() && binIndices.Empty(); }
    };

    struct GeometryBatchCatalogStats
    {
        u32 activeShells = 0;
        u32 activeBins = 0;
        u32 dirtyShells = 0;
        u32 dirtyBins = 0;
        u64 revision = 0;
    };

    // Retains an exact primitive/phase preparation and its shared destinations.
    // An individual lease is not drawable mesh admission. MeshDrawableBinding
    // retains the complete accepted closure until its submitted GPU uses
    // are covered; neither a borrowed ID nor a CPU candidate seal is ownership.
    // Mutating operations and destruction are main-thread only. GetPlacement
    // may be read by joined recording jobs while the owning lease stays alive.
    class GeometryBatchLease final
    {
    public:
        GeometryBatchLease() noexcept = default;
        ~GeometryBatchLease();
        GeometryBatchLease(const GeometryBatchLease&) = delete;
        GeometryBatchLease& operator=(const GeometryBatchLease&) = delete;
        GeometryBatchLease(GeometryBatchLease&& other) noexcept;
        GeometryBatchLease& operator=(GeometryBatchLease&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const GeometryBatchPlacement* GetPlacement() const noexcept;
        [[nodiscard]] bool Retain(GeometryBatchLease& output) const noexcept;
        void Reset() noexcept;

    private:
        RetainedGeometryBatch* m_batch = nullptr;
        friend class RenderGeometryBatcher;
    };

    struct RenderGeometryBatcherConfig
    {
        u32 maximumShells = 4096;
        u32 maximumBins = 16'384;
        u32 maximumPreparations = 8192;
    };

    enum class GeometryBatchResult : u8
    {
        Success,
        NotInitialized,
        WrongThread,
        InvalidPreparation,
        Pending,
        FailedTechnique,
        CapacityExceeded,
        LiveLeasesRemain
    };

    // CPU preparation portion of the geometry batcher. Owned by mesh residency;
    // no sibling shell registry, PSO service, visible-instance collector or GPU
    // submission loop. See docs/geometry-batcher-port.md for ownership details.
    class RenderGeometryBatcher final
    {
    public:
        RenderGeometryBatcher() noexcept = default;
        ~RenderGeometryBatcher();
        RenderGeometryBatcher(const RenderGeometryBatcher&) = delete;
        RenderGeometryBatcher& operator=(const RenderGeometryBatcher&) = delete;

        [[nodiscard]] bool Initialize(const RenderGeometryBatcherConfig& config) noexcept;
        [[nodiscard]] GeometryBatchResult Shutdown() noexcept;
        /// Main-thread after recording jobs join. Invalidates outstanding CPU
        /// leases and releases their demands before the residency owners disappear.
        void AbandonDevice() noexcept;
        [[nodiscard]] bool HasLiveLeases() const noexcept;

        // Recording jobs borrow these views while the owning frame boundary
        // prevents resource-time mutation. No lock, allocation or table scan is
        // performed by these accessors.
        [[nodiscard]] containers::ArraySpan<const GeometryShellCatalogEntry> GetPhaseShells(RenderPhaseId phase) const noexcept;
        [[nodiscard]] containers::ArraySpan<const GeometryBatchId> GetShellBins(GeometryBatchId shell) const noexcept;
        [[nodiscard]] bool GetShellCatalogEntry(GeometryBatchId shell, GeometryShellCatalogEntry& output) const noexcept;
        [[nodiscard]] bool GetBinCatalogEntry(GeometryBatchId bin, GeometryBinCatalogEntry& output) const noexcept;
        // Direct-index forms expose the current active record or inactive
        // tombstone for sparse GPU publication.
        [[nodiscard]] bool GetShellCatalogEntry(u32 index, GeometryShellCatalogEntry& output) const noexcept;
        [[nodiscard]] bool GetBinCatalogEntry(u32 index, GeometryBinCatalogEntry& output) const noexcept;

        // Resource publication builds bounded sparse requests from these indices before dispatch.
        // Acknowledgement succeeds only for the exact unchanged revision, so a
        // late completion cannot discard newer catalog dirtiness.
        [[nodiscard]] GeometryBatchCatalogChanges GetCatalogChanges() const noexcept;
        [[nodiscard]] bool AcknowledgeCatalogChanges(u64 revision) noexcept;
        [[nodiscard]] GeometryBatchCatalogStats GetCatalogStats() const noexcept;

    private:
        // Manager validates provenance; success transfers preparation ownership.
        // Pending/failure leave both preparation and output untouched.
        [[nodiscard]] GeometryBatchResult Acquire(MeshDrawPreparation& preparation, MaterialResidencyRuntime& materials, GeometryBatchLease& output) noexcept;
        void Release(RetainedGeometryBatch* batch) noexcept;
        void Detach(RetainedGeometryBatch* batch) noexcept;
        struct Impl;
        Impl* m_impl = nullptr;
        friend class GeometryBatchLease;
        friend class MeshResidencyManager;
    };
} // namespace vanguard::rendering

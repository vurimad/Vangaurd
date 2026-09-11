#pragma once

#include <vanguard/rendering/material_residency_runtime.hpp>
#include <vanguard/rendering/mesh_lod_definitions.hpp>
#include <vanguard/rendering/render_phase.hpp>
#include <vanguard/rendering/render_geometry_batcher.hpp>

namespace vanguard::rendering
{
    class MaterialResidencyRuntime;
    class RenderPhaseRegistry;
    class GpuSceneRuntime;
    struct GpuSceneContributionToken;
    struct RetainedMeshDrawable;

    inline constexpr u32 InvalidMeshResidencyIndex = 0xffffffffu;

    struct MeshResidencyHandle
    {
        u32 index = InvalidMeshResidencyIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidMeshResidencyIndex && generation != 0;
        }

        [[nodiscard]] constexpr bool operator==(const MeshResidencyHandle&) const noexcept = default;
    };

    struct MeshDemandId
    {
        u32 index = InvalidMeshResidencyIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidMeshResidencyIndex && generation != 0;
        }
    };

    class MeshResidencyManager;

    /// One caller's move-only interest in the coarsest complete LOD of an exact loaded mesh generation.
    /// Destruction releases only this interest; other callers remain coalesced on the same residency record.
    class MeshDemandHandle final
    {
    public:
        MeshDemandHandle() noexcept = default;
        ~MeshDemandHandle();

        MeshDemandHandle(const MeshDemandHandle&) = delete;
        MeshDemandHandle& operator=(const MeshDemandHandle&) = delete;
        MeshDemandHandle(MeshDemandHandle&& other) noexcept;
        MeshDemandHandle& operator=(MeshDemandHandle&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] MeshResidencyHandle GetResidency() const noexcept;
        void Reset() noexcept;

    private:
        MeshDemandHandle(MeshResidencyManager& manager, MeshDemandId demand, MeshResidencyHandle residency) noexcept;

        MeshResidencyManager* m_manager = nullptr;
        MeshDemandId m_demand;
        MeshResidencyHandle m_residency;

        friend class MeshResidencyManager;
    };

    // Resource-time preparation for one shared mesh primitive/phase, never one
    // per scene instance. Requests retain their material/native dependencies;
    // the coalesced mesh demand retains the geometry. Existing technique states
    // expose Pending/Ready/Failed; both requests must be Ready before admission.
    struct MeshDrawPreparation
    {
        MeshDemandHandle mesh;
        MaterialTechniqueRequest normal;
        MaterialTechniqueRequest mirrored;
        GpuGeometryHandle geometry;
        GeometryPlacement placement;
        u32 sourceSubmesh = meshes::InvalidRecordIndex;
        RenderPhaseId phase;

    private:
        MaterialTechniqueId m_normal;
        MaterialTechniqueId m_mirrored;
        RenderPhaseId m_phase;
        u32 m_sourceSubmesh = meshes::InvalidRecordIndex;
        friend class MeshResidencyManager;
    };

    struct MeshDrawPhaseContext
    {
        RenderPhaseKey phase;
        pipelines::AttachmentSignature attachments;
    };

    enum class MeshDrawableState : u8
    {
        Invalid,
        Preparing,
        PlacementPending,
        ResidencyPending,
        Ready,
        Failed,
        Withdrawing,
        Retiring
    };

    struct MeshDrawableInfo
    {
        GpuRenderableHandle renderable;
        u32 placementRevision = 0;
        u32 anchorLod = InvalidGpuSceneIndex;
        u32 placementCount = 0;
        // Actual residency upload receipt. Cross-queue consumers must establish
        // a GPU dependency; a CPU preparation/acceptance callback is not a wait.
        rhi::GpuFence readyAfter;
    };

    // Shared exact-generation closure, including pending preparation. Main-thread
    // mutation/destruction; recording borrows require a retained binding and join.
    class MeshDrawableBinding final
    {
    public:
        MeshDrawableBinding() noexcept = default;
        ~MeshDrawableBinding();
        MeshDrawableBinding(const MeshDrawableBinding&) = delete;
        MeshDrawableBinding& operator=(const MeshDrawableBinding&) = delete;
        MeshDrawableBinding(MeshDrawableBinding&& other) noexcept;
        MeshDrawableBinding& operator=(MeshDrawableBinding&& other) noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] MeshDrawableState GetState() const noexcept;
        [[nodiscard]] GeometryBatchResult GetFailure() const noexcept;
        [[nodiscard]] const MeshDrawableInfo* GetDrawable() const noexcept;
        [[nodiscard]] const GeometryBatchPlacement* GetPlacement(u32 index) const noexcept;
        [[nodiscard]] bool Retain(MeshDrawableBinding& output) const noexcept;
        void Reset() noexcept;

    private:
        RetainedMeshDrawable* m_drawable = nullptr;
        friend class MeshResidencyManager;
    };

    enum class MeshResidencyState : u8
    {
        Invalid,
        MetadataRetained,
        AnchorLodLoading,
        AnchorLodUploadSubmitted,
        /// Physical geometry and its immutable GPU Scene geometry definitions are owned by the
        /// residency record. No primitive placement or renderable residency entry refers to them yet.
        AnchorLodDefinitionsSubmitted,
        /// Default material slots are resolving through MaterialResidencyRuntime. Geometry ownership
        /// remains private and no renderable identity has been exposed.
        MaterialsResolving,
        /// Complete immutable LOD/primitive/material/phase topology exists. This is not drawable
        /// readiness: primitive/phase placement and GpuRenderableResidency remain unpublished.
        RenderableTopologySubmitted,
        Failed,
        Cancelling,
        Retiring
    };

    struct MeshResidencyInfo
    {
        MeshResidencyHandle residency;
        resources::ResourcePath resourcePath;
        u32 resourceGeneration = 0;
        u32 demandCount = 0;
        /// Complete LOD established before drawable admission and retained while live ownership requires a
        /// guaranteed representation. V1 anchors the coarsest authored LOD; it may be desired normally or serve
        /// as a fallback, and any future re-anchoring must make the replacement ready before releasing the old one.
        u16 anchorLod = 0xffffu;
        u32 geometryCount = 0;
        u64 vertexBytes = 0;
        u64 indexBytes = 0;
        u32 materialCount = 0;
        GpuRenderableHandle renderable;
        MeshResidencyState state = MeshResidencyState::Invalid;
    };

    struct MeshResidencyConfig
    {
        GeometryAllocatorConfig geometryAllocator;
        GeometryUploadConfig geometryUpload;
        MeshLodGeometryUploaderConfig lodUpload;
        u32 maximumResidencyRecords = 4096;
        u32 maximumDemands = 16'384;
        RenderGeometryBatcherConfig geometryBatcher;
    };

    enum class MeshResidencyFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDependency,
        InvalidConfiguration,
        InvalidArgument,
        CapacityExceeded,
        StaleDemand,
        LiveResidencyRemains,
        GeometryAllocatorFailure,
        GeometryUploaderFailure,
        LodUploaderFailure,
        LodDefinitionFailure,
        MaterialFailure,
        InvalidRenderableTopology,
        RenderableDefinitionFailure,
        DrawPreparationNotReady,
        InvalidDrawLayout,
        DrawablePublicationFailure,
        IncompatibleDrawContext
    };

    struct MeshResidencyFailure
    {
        MeshResidencyFailureCode code = MeshResidencyFailureCode::None;
        const char* message = nullptr;
        GeometryAllocatorFailure allocatorFailure;
        GeometryUploadFailure uploadFailure;
        MeshLodGeometryUploadFailure lodUploadFailure;
        MeshLodDefinitionFailure lodDefinitionFailure;
        MaterialResidencyRuntimeFailure materialFailure;
        GpuSceneDefinitionFailure renderableDefinitionFailure;
    };

    struct MeshResidencyStats
    {
        GeometryAllocatorStats allocator;
        GeometryUploadStats upload;
        MeshLodGeometryUploaderStats lodUpload;
        u32 residencyRecords = 0;
        u32 liveDemands = 0;
        u64 demandsIssued = 0;
        u64 demandsCoalesced = 0;
        u64 demandsReleased = 0;
    };

    /// Renderer-global composition owner for the physical mesh residency stack. Demand creation is thread-safe;
    /// page/upload progression and physical lifetime mutation are serialized through the main-thread methods.
    class MeshResidencyManager final
    {
    public:
        MeshResidencyManager() noexcept = default;
        ~MeshResidencyManager();

        MeshResidencyManager(const MeshResidencyManager&) = delete;
        MeshResidencyManager& operator=(const MeshResidencyManager&) = delete;

        [[nodiscard]] bool Initialize(GpuSceneDefinitions& definitions, const MeshResidencyConfig& config = {}, MeshResidencyFailure* failure = nullptr) noexcept;
        /// Enables complete immutable renderable-topology construction. Technique names are durable
        /// RenderPhaseKey values; only techniques resolved by the sealed phase registry participate.
        [[nodiscard]] bool Initialize(GpuSceneDefinitions& definitions, MaterialResidencyRuntime& materials, const RenderPhaseRegistry& phases, const MeshResidencyConfig& config = {},
                                      MeshResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Initialize(GpuSceneRuntime& runtime, MaterialResidencyRuntime& materials, const RenderPhaseRegistry& phases, const MeshResidencyConfig& config = {},
                                      MeshResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(MeshResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonDevice(MeshResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Thread-safe. Retains the exact mesh resource generation and coalesces it with existing demand.
        [[nodiscard]] bool RequestMesh(const resources::ResourceHandle& resource, MeshDemandHandle& demand, MeshResidencyFailure* failure = nullptr) noexcept;
        /// Thread-safe explicit counterpart to MeshDemandHandle::Reset/destruction.
        [[nodiscard]] bool CancelDemand(MeshDemandHandle& demand, MeshResidencyFailure* failure = nullptr) noexcept;
        /// Main-thread resource preparation, not a recording operation. Indexed
        /// geometry lookup and bounded per-primitive phase lookup; no scene scan.
        /// Output remains untouched on failure. Release it before manager shutdown.
        [[nodiscard]] bool PrepareDraw(const MeshDemandHandle& demand, u32 sourceSubmesh, RenderPhaseKey phase, const pipelines::AttachmentSignature& attachments, MeshDrawPreparation& output,
                                       MeshResidencyFailure* failure = nullptr) noexcept;
        /// Main-thread resource-time admission of an unmodified PrepareDraw result.
        /// Both variants must be ready. Success transfers preparation into a retained
        /// shell/bin lease; Pending/failure leave both arguments untouched. This does
        /// not publish GPU placement/residency or make the whole mesh drawable.
        [[nodiscard]] GeometryBatchResult AcquireDrawPlacement(MeshDrawPreparation& preparation, GeometryBatchLease& output) noexcept;
        // Every anchor phase requires one explicit attachment context. Equal
        // requests share the closure; incompatible contexts cannot overwrite it.
        [[nodiscard]] bool RequestDrawable(const MeshDemandHandle& demand, containers::ArraySpan<const MeshDrawPhaseContext> phases,
                                           MeshDrawableBinding& output, MeshResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool StageGpuSceneContribution(MeshResidencyFailure* failure = nullptr) noexcept;
        /// Main-thread non-blocking geometry/topology and requested drawable closure
        /// progress. StageGpuSceneContribution feeds shared publication separately.
        /// No per-view visibility, indirect generation or drawing occurs here.
        [[nodiscard]] bool Tick(MeshResidencyFailure* failure = nullptr) noexcept;
        /// Seals geometry retired by cancelled residency work against fences supplied by the renderer's normal frame
        /// submission boundary. The manager never fabricates queue submissions merely to recycle memory.
        [[nodiscard]] bool SealRetirements(const rhi::ResidencyFenceSet& safeAfter, MeshResidencyFailure* failure = nullptr) noexcept;
        /// Collects completed geometry retirement epochs and recycles cancellation records whose ranges are gone.
        [[nodiscard]] u32 CollectRetirements(MeshResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetInfo(MeshResidencyHandle residency, MeshResidencyInfo& info, MeshResidencyFailure* failure = nullptr) const noexcept;
        [[nodiscard]] MeshResidencyStats GetStats() const noexcept;
        // Borrowed renderer inputs. Resource mutation must be joined through
        // planning and command recording; ownership stays with residency.
        [[nodiscard]] const RenderGeometryBatcher& GetGeometryBatcher() const noexcept { return m_geometryBatcher; }
        [[nodiscard]] const GeometryAllocator& GetGeometryAllocator() const noexcept { return m_geometryAllocator; }

    private:
        struct Impl;
        void ReleaseDemand(MeshDemandId demand) noexcept;
        [[nodiscard]] bool IsDemandValid(MeshDemandId demand) const noexcept;
        [[nodiscard]] bool ProgressDrawables(MeshResidencyFailure* failure) noexcept;
        void ScheduleDrawable(RetainedMeshDrawable* drawable) noexcept;
        static bool WriteDrawableContribution(void* owner, GpuSceneContributionToken token, containers::ArraySpan<const GpuSceneUploadReservation> reservations, const char*& message) noexcept;
        static void AcceptDrawableContribution(void* owner, GpuSceneContributionToken token, rhi::GpuFence completion) noexcept;
        static bool RetryDrawableContribution(void* owner, GpuSceneContributionToken token, const char*& message) noexcept;

        GpuSceneDefinitions* m_definitions = nullptr;
        GpuSceneRuntime* m_runtime = nullptr;
        MaterialResidencyRuntime* m_materials = nullptr;
        const RenderPhaseRegistry* m_phases = nullptr;
        GeometryAllocator m_geometryAllocator;
        GeometryUploader m_geometryUploader;
        MeshLodGeometryUploader m_lodUploader;
        RenderGeometryBatcher m_geometryBatcher;
        Impl* m_impl = nullptr;

        friend class MeshDemandHandle;
        friend class MeshDrawableBinding;
    };
} // namespace vanguard::rendering

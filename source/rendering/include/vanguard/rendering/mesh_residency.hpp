#pragma once

#include <vanguard/rendering/mesh_lod_definitions.hpp>

namespace vanguard::rendering
{
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

    enum class MeshResidencyState : u8
    {
        Invalid,
        MetadataRetained,
        AnchorLodLoading,
        AnchorLodUploadSubmitted,
        /// Physical geometry and its immutable GPU Scene geometry definitions are owned by the
        /// residency record. No primitive placement or renderable residency entry refers to them yet.
        AnchorLodDefinitionsSubmitted,
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
        MeshResidencyState state = MeshResidencyState::Invalid;
    };

    struct MeshResidencyConfig
    {
        GeometryAllocatorConfig geometryAllocator;
        GeometryUploadConfig geometryUpload;
        MeshLodGeometryUploaderConfig lodUpload;
        u32 maximumResidencyRecords = 4096;
        u32 maximumDemands = 16'384;
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
        LodDefinitionFailure
    };

    struct MeshResidencyFailure
    {
        MeshResidencyFailureCode code = MeshResidencyFailureCode::None;
        const char* message = nullptr;
        GeometryAllocatorFailure allocatorFailure;
        GeometryUploadFailure uploadFailure;
        MeshLodGeometryUploadFailure lodUploadFailure;
        MeshLodDefinitionFailure lodDefinitionFailure;
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

        [[nodiscard]] bool Initialize(GpuSceneDefinitions& definitions, const MeshResidencyConfig& config = {},
                                      MeshResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(MeshResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Thread-safe. Retains the exact mesh resource generation and coalesces it with existing demand.
        [[nodiscard]] bool RequestMesh(const resources::ResourceHandle& resource, MeshDemandHandle& demand,
                                       MeshResidencyFailure* failure = nullptr) noexcept;
        /// Thread-safe explicit counterpart to MeshDemandHandle::Reset/destruction.
        [[nodiscard]] bool CancelDemand(MeshDemandHandle& demand, MeshResidencyFailure* failure = nullptr) noexcept;
        /// Main-thread, non-blocking progress for anchor-LOD page reads, geometry upload, and immutable
        /// geometry-definition acquisition. Completed definitions remain owned by this manager and are not yet
        /// referenced by drawable GPU Scene records;
        /// no GPU Scene placement, renderable residency, culling, indirect-command, or drawing work is performed here.
        [[nodiscard]] bool Tick(MeshResidencyFailure* failure = nullptr) noexcept;
        /// Seals geometry retired by cancelled residency work against fences supplied by the renderer's normal frame
        /// submission boundary. The manager never fabricates queue submissions merely to recycle memory.
        [[nodiscard]] bool SealRetirements(const rhi::ResidencyFenceSet& safeAfter,
                                           MeshResidencyFailure* failure = nullptr) noexcept;
        /// Collects completed geometry retirement epochs and recycles cancellation records whose ranges are gone.
        [[nodiscard]] u32 CollectRetirements(MeshResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetInfo(MeshResidencyHandle residency, MeshResidencyInfo& info,
                                   MeshResidencyFailure* failure = nullptr) const noexcept;
        [[nodiscard]] MeshResidencyStats GetStats() const noexcept;

    private:
        struct Impl;
        void ReleaseDemand(MeshDemandId demand) noexcept;

        GpuSceneDefinitions* m_definitions = nullptr;
        GeometryAllocator m_geometryAllocator;
        GeometryUploader m_geometryUploader;
        MeshLodGeometryUploader m_lodUploader;
        Impl* m_impl = nullptr;

        friend class MeshDemandHandle;
    };
} // namespace vanguard::rendering

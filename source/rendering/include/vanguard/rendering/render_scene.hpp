#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/resources/resources.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumRenderScenes = 64;
    inline constexpr u32 MaximumRenderSceneNameBytes = 96;
    inline constexpr u32 MaximumRenderProxyNameBytes = 96;
    inline constexpr u32 MaximumRenderProxySlotsPerScene = 1u << 24u;

    struct RenderSceneHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumRenderScenes && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderSceneHandle&,
                                                       const RenderSceneHandle&) noexcept = default;
    };

    inline constexpr RenderSceneHandle InvalidRenderSceneHandle{};

    struct RenderProxyHandle
    {
        RenderSceneHandle scene;
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && index < MaximumRenderProxySlotsPerScene && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderProxyHandle&,
                                                       const RenderProxyHandle&) noexcept = default;
    };

    inline constexpr RenderProxyHandle InvalidRenderProxyHandle{};

    enum class RenderSceneMode : u8
    {
        Runtime,
        Editor,
        Preview,
        Thumbnail
    };

    enum class RenderSceneState : u8
    {
        Vacant,
        Alive,
        Destroying,
        Failed
    };

    enum class RenderSceneOwnership : u8
    {
        Service,
        External
    };

    enum class RenderSceneFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDescriptor,
        InvalidHandle,
        WrongScene,
        InvalidState,
        CapacityExceeded,
        ScenesRemainAlive,
        ProxiesRemainAlive,
        ReadersRemainAlive,
        PendingDestroy,
        VersionNotFound,
        Busy
    };

    struct RenderSceneFailure
    {
        RenderSceneFailureCode code = RenderSceneFailureCode::None;
        RenderSceneHandle scene;
        RenderProxyHandle proxy;
        const char* message = nullptr;
    };

    enum class RenderProxyState : u8
    {
        Vacant,
        Alive,
        Destroying,
        Retired,
        Failed
    };

    enum class RenderProxyMutationKind : u8
    {
        Create,
        Destroy,
        TransformAndBounds,
        Visibility,
        LayerMask,
        UserDataEpoch,
        MeshResources,
        LightProperties,
        DecalMaterial
    };

    enum class RenderProxySpatialMode : u8
    {
        None,
        Bounds
    };

    enum class RenderProxyPayloadKind : u8
    {
        None,
        Mesh,
        Light,
        Decal
    };

    enum class RenderProxyVisibilityFlags : u32
    {
        None = 0,
        Visible = 1u << 0u,
        CastsShadow = 1u << 1u,
        ReceivesDecals = 1u << 2u,
        QueryOnly = 1u << 3u,
        EditorOnly = 1u << 4u
    };

    [[nodiscard]] constexpr RenderProxyVisibilityFlags operator|(const RenderProxyVisibilityFlags left,
                                                                const RenderProxyVisibilityFlags right) noexcept
    {
        return static_cast<RenderProxyVisibilityFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr RenderProxyVisibilityFlags operator&(const RenderProxyVisibilityFlags left,
                                                                const RenderProxyVisibilityFlags right) noexcept
    {
        return static_cast<RenderProxyVisibilityFlags>(static_cast<u32>(left) & static_cast<u32>(right));
    }

    struct RenderProxyTransform
    {
        f32 row0[4]{1.0f, 0.0f, 0.0f, 0.0f};
        f32 row1[4]{0.0f, 1.0f, 0.0f, 0.0f};
        f32 row2[4]{0.0f, 0.0f, 1.0f, 0.0f};
    };

    struct RenderProxyBounds
    {
        f32 minimum[3]{};
        f32 maximum[3]{};
    };

    struct RenderProxyDesc
    {
        RenderSceneHandle scene;
        u32 typeId = 0;
        u64 producerId = 0;
        u64 producerGeneration = 0;
        RenderProxyTransform transform;
        RenderProxyBounds bounds;
        RenderProxySpatialMode spatialMode = RenderProxySpatialMode::Bounds;
        RenderProxyVisibilityFlags visibility = RenderProxyVisibilityFlags::Visible;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
        u64 userDataEpoch = 0;
        const char* debugName = nullptr;
    };

    struct RenderProxySnapshot
    {
        RenderProxyHandle handle;
        RenderProxyState state = RenderProxyState::Vacant;
        u32 typeId = 0;
        u64 producerId = 0;
        u64 producerGeneration = 0;
        RenderProxyTransform transform;
        RenderProxyBounds bounds;
        RenderProxySpatialMode spatialMode = RenderProxySpatialMode::None;
        RenderProxyPayloadKind payloadKind = RenderProxyPayloadKind::None;
        RenderProxyVisibilityFlags visibility = RenderProxyVisibilityFlags::None;
        u64 layerMask = 0;
        u32 visibilityMask = 0;
        u64 userDataEpoch = 0;
        u64 createdSerial = 0;
        u64 lifecycleRevision = 0;
        char debugName[MaximumRenderProxyNameBytes]{};
    };

    enum class RenderLightKind : u8
    {
        Directional,
        Point,
        Spot
    };

    struct MeshProxyDesc
    {
        RenderProxyDesc proxy;
        resources::ResourceReference mesh;
        resources::ResourceReference material;
        resources::ResourceHandle meshHandle;
        resources::ResourceHandle materialHandle;
        u32 submeshMask = ~0u;
        u32 renderFlags = 0;
    };

    struct MeshProxySnapshot
    {
        RenderProxyHandle proxy;
        u32 payloadGeneration = 0;
        resources::ResourceReference mesh;
        resources::ResourceReference material;
        resources::ResourceHandle meshHandle;
        resources::ResourceHandle materialHandle;
        u32 submeshMask = 0;
        u32 renderFlags = 0;
    };

    struct LightProxyDesc
    {
        RenderProxyDesc proxy;
        RenderLightKind kind = RenderLightKind::Point;
        f32 color[3]{1.0f, 1.0f, 1.0f};
        f32 intensity = 1.0f;
        f32 range = 1.0f;
        f32 innerConeRadians = 0.0f;
        f32 outerConeRadians = 0.0f;
        bool castsShadow = false;
    };

    struct LightProxySnapshot
    {
        RenderProxyHandle proxy;
        u32 payloadGeneration = 0;
        RenderLightKind kind = RenderLightKind::Point;
        f32 color[3]{};
        f32 intensity = 0.0f;
        f32 range = 0.0f;
        f32 innerConeRadians = 0.0f;
        f32 outerConeRadians = 0.0f;
        bool castsShadow = false;
    };

    struct DecalProxyDesc
    {
        RenderProxyDesc proxy;
        resources::ResourceReference material;
        resources::ResourceHandle materialHandle;
        f32 extents[3]{1.0f, 1.0f, 1.0f};
        f32 fadeDistance = 0.0f;
        u32 sortKey = 0;
    };

    struct DecalProxySnapshot
    {
        RenderProxyHandle proxy;
        u32 payloadGeneration = 0;
        resources::ResourceReference material;
        resources::ResourceHandle materialHandle;
        f32 extents[3]{};
        f32 fadeDistance = 0.0f;
        u32 sortKey = 0;
    };

    struct RenderSceneVersion
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] friend constexpr bool operator==(const RenderSceneVersion&,
                                                       const RenderSceneVersion&) noexcept = default;
    };

    inline constexpr RenderSceneVersion InvalidRenderSceneVersion{};

    struct RenderSceneCompletionToken
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    };

    struct RenderSceneFramePrepareResult
    {
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
        u32 drainedMutations = 0;
        bool completedSynchronously = true;
    };

    struct RenderSceneCommitResult
    {
        RenderSceneHandle scene;
        RenderSceneVersion version;
        RenderSceneCompletionToken completion;
        u64 mutationEpoch = 0;
        u32 proxyCount = 0;
        bool completedSynchronously = true;
    };

    struct RenderSceneVersionRetirementResult
    {
        u32 reclaimedVersions = 0;
        u32 retainedVersions = 0;
        u32 versionsBlockedByReaders = 0;
        u32 liveReadLeases = 0;
    };

    struct SceneReadLease
    {
        RenderSceneHandle scene;
        RenderSceneVersion version;
        u64 readerEpoch = 0;
        u32 proxyCount = 0;
        u32 spatialCellCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && version.IsValid() && readerEpoch != 0;
        }
    };

    enum class SpatialOutOfRangePolicy : u8
    {
        KeepUnindexed,
        RejectProxy
    };

    struct SpatialWriteIndexConfig
    {
        f32 origin[3]{};
        f32 cellSize = 64.0f;
        u32 cellsPerAxis[3]{0, 0, 0};
        SpatialOutOfRangePolicy outOfRangePolicy = SpatialOutOfRangePolicy::KeepUnindexed;

        [[nodiscard]] constexpr bool HasFiniteExtent() const noexcept
        {
            return cellsPerAxis[0] != 0 && cellsPerAxis[1] != 0 && cellsPerAxis[2] != 0;
        }
    };

    struct SpatialWriteIndexStats
    {
        u32 activeEntries = 0;
        u32 cells = 0;
        u32 occupiedCells = 0;
        u32 dirtyCells = 0;
        u64 insertedEntries = 0;
        u64 removedEntries = 0;
        u64 fastMoves = 0;
        u64 structuralMoves = 0;
        u64 repairedCells = 0;
        u64 outOfRangeProxies = 0;
        bool valid = false;
    };

    enum class VisibilityQueryPayloadFilter : u8
    {
        Any,
        Mesh,
        Light,
        Decal,
        None
    };

    inline constexpr u32 MaximumVisibilityFrustumPlanes = 8;

    struct VisibilityPlane
    {
        f32 normal[3]{};
        f32 distance = 0.0f;
    };

    struct VisibilityFrustum
    {
        VisibilityPlane planes[MaximumVisibilityFrustumPlanes];
        u32 planeCount = 0;
    };

    struct VisibilityQueryRequest
    {
        SceneReadLease lease;
        RenderProxyBounds bounds;
        VisibilityFrustum frustum;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
        RenderProxyVisibilityFlags requiredFlags = RenderProxyVisibilityFlags::Visible;
        RenderProxyVisibilityFlags excludedFlags = RenderProxyVisibilityFlags::None;
        VisibilityQueryPayloadFilter payloadFilter = VisibilityQueryPayloadFilter::Any;
        u32 maximumResults = ~0u;
        bool useBounds = true;
        bool useFrustum = false;
    };

    struct VisibilityQueryResult
    {
        RenderSceneHandle scene;
        RenderSceneVersion version;
        u32 visitedCells = 0;
        u32 candidateProxies = 0;
        u32 acceptedProxies = 0;
        u32 rejectedCellsByBounds = 0;
        u32 rejectedCellsByFrustum = 0;
        u32 rejectedByBounds = 0;
        u32 rejectedByFrustum = 0;
        u32 rejectedByLayer = 0;
        u32 rejectedByVisibility = 0;
        u32 rejectedByPayload = 0;
        u32 overflowedProxies = 0;
        bool completed = false;
    };

    struct VisibilityQueryBatch
    {
        RenderSceneHandle scene;
        RenderSceneVersion version;
        u32 firstCell = 0;
        u32 cellCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && version.IsValid() && cellCount != 0;
        }
    };

    struct VisibilityQueryPlan
    {
        RenderSceneHandle scene;
        RenderSceneVersion version;
        u32 cellCount = 0;
        u32 batchSize = 0;
        u32 batchCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && version.IsValid() && batchSize != 0;
        }
    };

    struct RenderSceneDesc
    {
        const char* name = nullptr;
        RenderSceneMode mode = RenderSceneMode::Runtime;
        RenderSceneOwnership ownership = RenderSceneOwnership::Service;
        SpatialWriteIndexConfig spatial;
        u32 maximumProxies = 1u << 20u;
        u32 maximumPendingProxyMutations = 64u * 1024u;
        u32 maximumViews = 16;
        bool allowFramePipelineParticipation = true;
    };

    struct RenderSceneSnapshot
    {
        RenderSceneHandle handle;
        RenderSceneMode mode = RenderSceneMode::Runtime;
        RenderSceneState state = RenderSceneState::Vacant;
        RenderSceneOwnership ownership = RenderSceneOwnership::Service;
        SpatialWriteIndexStats spatial;
        u32 maximumProxies = 0;
        u32 maximumPendingProxyMutations = 0;
        u32 maximumViews = 0;
        u32 activeProxies = 0;
        u32 pendingProxyMutations = 0;
        u64 createdSerial = 0;
        u64 lifecycleRevision = 0;
        bool allowFramePipelineParticipation = false;
        char name[MaximumRenderSceneNameBytes]{};
    };

    struct RenderSceneManagerConfig
    {
        u32 maximumScenes = MaximumRenderScenes;
    };

    struct RenderSceneManagerStats
    {
        u32 capacity = 0;
        u32 activeScenes = 0;
        u32 destroyingScenes = 0;
        u64 createdScenes = 0;
        u64 destroyedScenes = 0;
        u64 failedCreates = 0;
        u64 failedDestroys = 0;
        u32 activeProxies = 0;
        u32 pendingProxyMutations = 0;
        u32 liveReadLeases = 0;
        u32 retainedSceneVersions = 0;
        u32 activeMeshPayloads = 0;
        u32 activeLightPayloads = 0;
        u32 activeDecalPayloads = 0;
        u32 activeSpatialEntries = 0;
        u32 dirtySpatialCells = 0;
        u64 createdProxies = 0;
        u64 destroyedProxies = 0;
        u64 createdPayloads = 0;
        u64 destroyedPayloads = 0;
        u64 failedProxyCreates = 0;
        u64 failedProxyMutations = 0;
        u64 preparedFrames = 0;
        u64 committedVersions = 0;
        u64 failedPublishes = 0;
        u64 spatialFastMoves = 0;
        u64 spatialStructuralMoves = 0;
        u64 spatialRepairedCells = 0;
        u64 spatialOutOfRangeProxies = 0;
        u64 rejectedOperations = 0;
        bool initialized = false;
    };

    class RenderSceneManager final
    {
    public:
        struct Impl;

        RenderSceneManager() noexcept = default;
        ~RenderSceneManager();

        RenderSceneManager(const RenderSceneManager&) = delete;
        RenderSceneManager& operator=(const RenderSceneManager&) = delete;

        [[nodiscard]] bool Initialize(const RenderSceneManagerConfig& config = {},
                                      RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool CreateScene(const RenderSceneDesc& desc, RenderSceneHandle& scene,
                                       RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyScene(RenderSceneHandle scene,
                                        RenderSceneFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool CreateProxy(const RenderProxyDesc& desc, RenderProxyHandle& proxy,
                                       RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateMeshProxy(const MeshProxyDesc& desc, RenderProxyHandle& proxy,
                                           RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateLightProxy(const LightProxyDesc& desc, RenderProxyHandle& proxy,
                                            RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateDecalProxy(const DecalProxyDesc& desc, RenderProxyHandle& proxy,
                                            RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyProxy(RenderProxyHandle proxy,
                                        RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProxyTransform(RenderProxyHandle proxy, const RenderProxyTransform& transform,
                                                const RenderProxyBounds& bounds, u64 producerGeneration = 0,
                                                RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProxyVisibility(RenderProxyHandle proxy, RenderProxyVisibilityFlags visibility,
                                                 u32 visibilityMask,
                                                 RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProxyLayerMask(RenderProxyHandle proxy, u64 layerMask,
                                                RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProxyUserDataEpoch(RenderProxyHandle proxy, u64 userDataEpoch,
                                                    RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateMeshProxyResources(RenderProxyHandle proxy, resources::ResourceReference mesh,
                                                    resources::ResourceReference material,
                                                    const resources::ResourceHandle& meshHandle = {},
                                                    const resources::ResourceHandle& materialHandle = {},
                                                    RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateLightProxyProperties(RenderProxyHandle proxy, const LightProxySnapshot& properties,
                                                      RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateDecalProxyMaterial(RenderProxyHandle proxy, resources::ResourceReference material,
                                                    const resources::ResourceHandle& materialHandle = {},
                                                    RenderSceneFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool PrepareSceneFrame(RenderSceneHandle scene, RenderSceneFramePrepareResult& result,
                                             RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CommitScene(RenderSceneHandle scene, RenderSceneCommitResult& result,
                                       RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AcquireLatestReadLease(RenderSceneHandle scene, SceneReadLease& lease,
                                                  RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AcquireReadLease(RenderSceneHandle scene, RenderSceneVersion version,
                                            SceneReadLease& lease,
                                            RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReleaseReadLease(SceneReadLease& lease,
                                            RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RetirePublishedVersions(RenderSceneVersionRetirementResult& result,
                                                   RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReadProxy(const SceneReadLease& lease, RenderProxyHandle proxy,
                                     RenderProxySnapshot& snapshot,
                                     RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool ReadMeshProxy(const SceneReadLease& lease, RenderProxyHandle proxy,
                                         MeshProxySnapshot& snapshot,
                                         RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool ReadLightProxy(const SceneReadLease& lease, RenderProxyHandle proxy,
                                          LightProxySnapshot& snapshot,
                                          RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool ReadDecalProxy(const SceneReadLease& lease, RenderProxyHandle proxy,
                                          DecalProxySnapshot& snapshot,
                                          RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool CollectVisibleProxies(const VisibilityQueryRequest& request,
                                                 containers::DynamicArray<RenderProxyHandle>& proxies,
                                                 VisibilityQueryResult& result,
                                                 RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool BuildVisibilityQueryPlan(const SceneReadLease& lease, u32 targetCellsPerBatch,
                                                    containers::DynamicArray<VisibilityQueryBatch>& batches,
                                                    VisibilityQueryPlan& plan,
                                                    RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool CollectVisibleProxyBatch(const VisibilityQueryRequest& request,
                                                    const VisibilityQueryBatch& batch,
                                                    containers::DynamicArray<RenderProxyHandle>& proxies,
                                                    VisibilityQueryResult& result,
                                                    RenderSceneFailure* failure = nullptr) const noexcept;

        [[nodiscard]] bool IsAlive(RenderSceneHandle scene) const noexcept;
        [[nodiscard]] bool IsProxyAlive(RenderProxyHandle proxy) const noexcept;
        /// Returns true while any published scene version still contains this exact proxy generation.
        [[nodiscard]] bool IsProxyRetained(RenderProxyHandle proxy) const noexcept;
        [[nodiscard]] bool Snapshot(RenderSceneHandle scene,
                                    RenderSceneSnapshot& snapshot) const noexcept;
        [[nodiscard]] bool SnapshotProxy(RenderProxyHandle proxy,
                                         RenderProxySnapshot& snapshot) const noexcept;
        [[nodiscard]] bool ValidateSpatialIndex(RenderSceneHandle scene,
                                                SpatialWriteIndexStats* stats = nullptr) const noexcept;
        [[nodiscard]] bool SnapshotMeshProxy(RenderProxyHandle proxy,
                                             MeshProxySnapshot& snapshot) const noexcept;
        [[nodiscard]] bool SnapshotLightProxy(RenderProxyHandle proxy,
                                              LightProxySnapshot& snapshot) const noexcept;
        [[nodiscard]] bool SnapshotDecalProxy(RenderProxyHandle proxy,
                                              DecalProxySnapshot& snapshot) const noexcept;
        [[nodiscard]] RenderSceneManagerStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

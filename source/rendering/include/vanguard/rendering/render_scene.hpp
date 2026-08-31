#pragma once

#include <vanguard/rendering/gpu_scene_visibility.hpp>
#include <vanguard/rendering/render_view.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/resources/resources.hpp>

namespace vanguard::rendering
{
    class RenderCameraStorage;
    class RenderSceneGpuPublisher;
    class VisibilityFeedbackService;
    struct RenderSceneGpuReadView;
    inline constexpr u32 MaximumRenderScenes = 64;
    inline constexpr u32 MaximumRenderSceneNameBytes = 96;
    inline constexpr u32 MaximumRenderProxyNameBytes = 96;
    inline constexpr u32 MaximumRenderProxySlotsPerScene = 1u << 24u;
    inline constexpr u32 MaximumRenderProducerSlotsPerScene = 1u << 24u;

    struct RenderSceneHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumRenderScenes && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderSceneHandle&, const RenderSceneHandle&) noexcept = default;
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

        [[nodiscard]] friend constexpr bool operator==(const RenderProxyHandle&, const RenderProxyHandle&) noexcept = default;
    };

    inline constexpr RenderProxyHandle InvalidRenderProxyHandle{};

    /// Scene-local runtime producer identity. It is intentionally independent from ECS, serialization,
    /// networking, and editor identity types; adapters translate their runtime handles at the boundary.
    struct RenderProducerHandle
    {
        u32 index = 0;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != 0 && index < MaximumRenderProducerSlotsPerScene;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderProducerHandle&, const RenderProducerHandle&) noexcept = default;
    };

    struct RenderContributorId
    {
        u32 index = ~u32{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != ~u32{0};
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderContributorId&, const RenderContributorId&) noexcept = default;
    };

    struct RenderProducerProxy
    {
        RenderProducerHandle producer;
        RenderContributorId contributor;
        RenderProxyHandle proxy;
    };

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
        PendingDestroy,
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

    [[nodiscard]] constexpr RenderProxyVisibilityFlags operator|(const RenderProxyVisibilityFlags left, const RenderProxyVisibilityFlags right) noexcept
    {
        return static_cast<RenderProxyVisibilityFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr RenderProxyVisibilityFlags operator&(const RenderProxyVisibilityFlags left, const RenderProxyVisibilityFlags right) noexcept
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
        RenderProducerHandle producer;
        RenderContributorId contributor;
        RenderProxyTransform transform;
        RenderProxyBounds bounds;
        RenderProxySpatialMode spatialMode = RenderProxySpatialMode::Bounds;
        RenderProxyVisibilityFlags visibility = RenderProxyVisibilityFlags::Visible;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
        u64 userDataEpoch = 0;
        const char* debugName = nullptr;
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

    enum class MeshProxyUpdateFields : u8
    {
        None = 0,
        Resources = 1u << 0u,
        SubmeshSelection = 1u << 1u,
        RenderFlags = 1u << 2u,
        All = (1u << 0u) | (1u << 1u) | (1u << 2u)
    };

    [[nodiscard]] constexpr MeshProxyUpdateFields operator|(const MeshProxyUpdateFields left, const MeshProxyUpdateFields right) noexcept
    {
        return static_cast<MeshProxyUpdateFields>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr MeshProxyUpdateFields operator&(const MeshProxyUpdateFields left, const MeshProxyUpdateFields right) noexcept
    {
        return static_cast<MeshProxyUpdateFields>(static_cast<u8>(left) & static_cast<u8>(right));
    }

    struct MeshProxyUpdate
    {
        MeshProxyUpdateFields fields = MeshProxyUpdateFields::All;
        resources::ResourceReference mesh;
        resources::ResourceReference material;
        resources::ResourceHandle meshHandle;
        resources::ResourceHandle materialHandle;
        u32 submeshMask = ~0u;
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

    enum class LightProxyUpdateFields : u8
    {
        None = 0,
        Kind = 1u << 0u,
        Color = 1u << 1u,
        Photometry = 1u << 2u,
        Cones = 1u << 3u,
        Shadow = 1u << 4u,
        All = (1u << 0u) | (1u << 1u) | (1u << 2u) | (1u << 3u) | (1u << 4u)
    };

    [[nodiscard]] constexpr LightProxyUpdateFields operator|(const LightProxyUpdateFields left, const LightProxyUpdateFields right) noexcept
    {
        return static_cast<LightProxyUpdateFields>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr LightProxyUpdateFields operator&(const LightProxyUpdateFields left, const LightProxyUpdateFields right) noexcept
    {
        return static_cast<LightProxyUpdateFields>(static_cast<u8>(left) & static_cast<u8>(right));
    }

    struct LightProxyUpdate
    {
        LightProxyUpdateFields fields = LightProxyUpdateFields::All;
        RenderLightKind kind = RenderLightKind::Point;
        f32 color[3]{1.0f, 1.0f, 1.0f};
        f32 intensity = 1.0f;
        f32 range = 1.0f;
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

    enum class DecalProxyUpdateFields : u8
    {
        None = 0,
        Material = 1u << 0u,
        Extents = 1u << 1u,
        FadeDistance = 1u << 2u,
        SortKey = 1u << 3u,
        All = (1u << 0u) | (1u << 1u) | (1u << 2u) | (1u << 3u)
    };

    [[nodiscard]] constexpr DecalProxyUpdateFields operator|(const DecalProxyUpdateFields left, const DecalProxyUpdateFields right) noexcept
    {
        return static_cast<DecalProxyUpdateFields>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr DecalProxyUpdateFields operator&(const DecalProxyUpdateFields left, const DecalProxyUpdateFields right) noexcept
    {
        return static_cast<DecalProxyUpdateFields>(static_cast<u8>(left) & static_cast<u8>(right));
    }

    struct DecalProxyUpdate
    {
        DecalProxyUpdateFields fields = DecalProxyUpdateFields::All;
        resources::ResourceReference material;
        resources::ResourceHandle materialHandle;
        f32 extents[3]{};
        f32 fadeDistance = 0.0f;
        u32 sortKey = 0;
    };

    struct RenderSceneFramePrepareResult
    {
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
        u32 drainedMutations = 0;
        bool completedSynchronously = true;
    };

    /// Complete retained relink input. The newest request for a proxy wins as one unit;
    /// fields are never merged from requests produced at different simulation instants.
    struct RenderProxyRelinkRequest
    {
        RenderProxyHandle proxy;
        RenderProxyTransform transform;
        RenderProxyBounds bounds;
        u64 producerGeneration = 0;
        bool teleport = false;
    };

    struct RenderSceneUpdateResult
    {
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
        u32 submittedRelinks = 0;
        bool dispatched = false;
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

    struct VisibilityQueryRequest
    {
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
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
        u64 mutationEpoch = 0;
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
        u64 mutationEpoch = 0;
        u32 firstCell = 0;
        u32 cellCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && cellCount != 0;
        }
    };

    struct VisibilityQueryPlan
    {
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
        u32 cellCount = 0;
        u32 batchSize = 0;
        u32 batchCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && batchSize != 0;
        }
    };

    struct RenderSceneGpuCandidateBatch
    {
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
        u64 planSerial = 0;
        u32 firstTraversal = 0;
        u32 firstProxy = 0;
        u32 traversalCandidateCount = 0;
        u32 destinationOffset = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && planSerial != 0 && traversalCandidateCount != 0;
        }
    };

    struct RenderSceneGpuCandidatePlan
    {
        VisibilityQueryRequest request;
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
        u64 serial = 0;
        u32 traversalCandidateCount = 0;
        u32 requiredCandidateCapacity = 0;
        u32 requiredWorkRangeCapacity = 0;
        u32 batchCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && serial != 0;
        }
    };

    struct RenderSceneGpuCandidateBatchResult
    {
        VisibilityQueryResult visibility;
        u32 unresolvedGpuIdentities = 0;
        bool completed = false;
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
        u64 currentMutationEpoch = 0;
        u64 preparedMutationEpoch = 0;
        u64 completedMutationEpoch = 0;
        bool framePrepared = false;
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
        u32 framePipelineScenes = 0;
        u32 destroyingScenes = 0;
        u64 createdScenes = 0;
        u64 destroyedScenes = 0;
        u64 failedCreates = 0;
        u64 failedDestroys = 0;
        u32 activeProxies = 0;
        u32 pendingProxyMutations = 0;
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

        [[nodiscard]] bool Initialize(const RenderSceneManagerConfig& config = {}, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool CreateScene(const RenderSceneDesc& desc, RenderSceneHandle& scene, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyScene(RenderSceneHandle scene, RenderSceneFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool CreateProxy(const RenderProxyDesc& desc, RenderProxyHandle& proxy, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateMeshProxy(const MeshProxyDesc& desc, RenderProxyHandle& proxy, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateLightProxy(const LightProxyDesc& desc, RenderProxyHandle& proxy, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateDecalProxy(const DecalProxyDesc& desc, RenderProxyHandle& proxy, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateProducerMeshProxy(RenderProducerHandle producer, RenderContributorId contributor, const MeshProxyDesc& desc,
                                                   RenderProxyHandle& proxy, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateProducerLightProxy(RenderProducerHandle producer, RenderContributorId contributor, const LightProxyDesc& desc,
                                                    RenderProxyHandle& proxy, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateProducerDecalProxy(RenderProducerHandle producer, RenderContributorId contributor, const DecalProxyDesc& desc,
                                                    RenderProxyHandle& proxy, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyProxy(RenderProxyHandle proxy, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyProducerContribution(RenderSceneHandle scene, RenderProducerHandle producer, RenderContributorId contributor,
                                                       RenderProxyHandle* destroyed = nullptr, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyProducer(RenderSceneHandle scene, RenderProducerHandle producer,
                                           containers::DynamicArray<RenderProducerProxy>* destroyed = nullptr, RenderSceneFailure* failure = nullptr) noexcept;
        /// Cold session-reset path. Normal frame updates must address one producer or contributor directly.
        [[nodiscard]] bool DestroySceneProducers(RenderSceneHandle scene, u64 producerGeneration, containers::DynamicArray<RenderProducerProxy>& destroyed,
                                                 RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProxyTransform(RenderProxyHandle proxy, const RenderProxyTransform& transform, const RenderProxyBounds& bounds,
                                                u64 producerGeneration = 0, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProxyVisibility(RenderProxyHandle proxy, RenderProxyVisibilityFlags visibility, u32 visibilityMask,
                                                 RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProxyLayerMask(RenderProxyHandle proxy, u64 layerMask, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProxyUserDataEpoch(RenderProxyHandle proxy, u64 userDataEpoch, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateMeshProxy(RenderProxyHandle proxy, const MeshProxyUpdate& update, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateLightProxy(RenderProxyHandle proxy, const LightProxyUpdate& update, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateDecalProxy(RenderProxyHandle proxy, const DecalProxyUpdate& update, RenderSceneFailure* failure = nullptr) noexcept;

        /// Multi-producer hot ingress. This mirrors the renderer relink path: a bounded
        /// double-buffered queue retains a complete request until scene-update jobs consume it.
        [[nodiscard]] bool ScheduleRelink(const RenderProxyRelinkRequest& request, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool PrepareSceneUpdate(RenderSceneHandle scene, u64 tickCounter, RenderSceneFramePrepareResult& result,
                                              RenderSceneFailure* failure = nullptr) noexcept;
        /// Appends duplicate reduction, parallel proxy-local relinks, batched structural
        /// movement, and one spatial repair boundary to the supplied render-path builder.
        [[nodiscard]] bool ExecuteSceneUpdate(RenderSceneHandle scene, jobs::Builder& builder, RenderSceneUpdateResult& result,
                                              RenderSceneFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool CollectVisibleProxies(const VisibilityQueryRequest& request, containers::DynamicArray<RenderProxyHandle>& proxies,
                                                 VisibilityQueryResult& result, RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool BuildVisibilityQueryPlan(RenderSceneHandle scene, u64 mutationEpoch, u32 targetCellsPerBatch,
                                                    containers::DynamicArray<VisibilityQueryBatch>& batches, VisibilityQueryPlan& plan,
                                                    RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool CollectVisibleProxyBatch(const VisibilityQueryRequest& request, const VisibilityQueryBatch& batch,
                                                    containers::DynamicArray<RenderProxyHandle>& proxies, VisibilityQueryResult& result,
                                                    RenderSceneFailure* failure = nullptr) const noexcept;
        /// Seals one completed scene epoch and builds proxy-count-balanced, allocation-free candidate batches into caller storage.
        [[nodiscard]] bool PrepareGpuVisibilityCandidates(const VisibilityQueryRequest& request, u32 targetCandidatesPerBatch,
                                                          containers::ArraySpan<RenderSceneGpuCandidateBatch> batchStorage, RenderSceneGpuCandidatePlan& plan,
                                                          RenderSceneFailure* failure = nullptr) noexcept;
        /// Writes one batch directly into its preassigned prefix of the caller-owned view reservation.
        /// Parallel callers must provide distinct range, result, and failure objects for every batch.
        [[nodiscard]] bool WriteGpuVisibilityCandidateBatch(const RenderSceneGpuCandidatePlan& plan, const RenderSceneGpuCandidateBatch& batch,
                                                            const GpuVisibilityCandidateReservation& reservation, GpuVisibilityCandidateRange& range,
                                                            RenderSceneGpuCandidateBatchResult& result, RenderSceneFailure* failure = nullptr) const noexcept;
        /// Releases the scene read seal after the dependency joining every candidate batch completes.
        /// Calling this while a batch still reads the scene is a contract violation.
        [[nodiscard]] bool CompleteGpuVisibilityCandidates(const RenderSceneGpuCandidatePlan& plan, RenderSceneFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool IsAlive(RenderSceneHandle scene) const noexcept;
        [[nodiscard]] bool IsProxyAlive(RenderProxyHandle proxy) const noexcept;
        [[nodiscard]] bool FindProducerProxy(RenderSceneHandle scene, RenderProducerHandle producer, RenderContributorId contributor,
                                             RenderProxyHandle& proxy) const noexcept;
        [[nodiscard]] u32 GetProducerProxyCount(RenderSceneHandle scene, RenderProducerHandle producer) const noexcept;
        /// Copies one producer contributor chain into caller-owned storage. This never scans
        /// scene slots or allocates. A false result reports the required element count in count.
        [[nodiscard]] bool GetProducerProxies(RenderSceneHandle scene, RenderProducerHandle producer,
                                              containers::ArraySpan<RenderProducerProxy> proxies, u32& count) const noexcept;
        /// Allocation-free dense view of live scenes admitted to the engine frame pipeline.
        /// Main-thread scene creation or destruction invalidates the returned view.
        [[nodiscard]] containers::ArraySpan<const RenderSceneHandle> GetFramePipelineScenes() const noexcept;
        [[nodiscard]] bool GetSnapshot(RenderSceneHandle scene, RenderSceneSnapshot& snapshot) const noexcept;
        /// Loading-screen readiness delegated to the scene's persistent camera/custom-data storage.
        /// Call only from the serialized renderer boundary after previous CPU frame processing is complete.
        [[nodiscard]] const char* GetRenderingBlockReason(RenderSceneHandle scene) const noexcept;
        void TickWhileLoading(RenderSceneHandle scene, bool isFirstFrame) noexcept;
        [[nodiscard]] bool ValidateSpatialIndex(RenderSceneHandle scene, SpatialWriteIndexStats* stats = nullptr) const noexcept;
        [[nodiscard]] RenderSceneManagerStats GetStats() const noexcept;

    private:
        friend class RenderCameraStorage;
        friend class RenderSceneGpuPublisher;
        friend class VisibilityFeedbackService;

        [[nodiscard]] bool AttachCameraStorage(RenderCameraStorage& storage) noexcept;
        [[nodiscard]] bool DetachCameraStorage(RenderCameraStorage& storage) noexcept;
        [[nodiscard]] bool AttachGpuPublisher(RenderSceneGpuPublisher& publisher) noexcept;
        [[nodiscard]] bool DetachGpuPublisher(RenderSceneGpuPublisher& publisher) noexcept;
        [[nodiscard]] bool IsGpuPublicationReady(RenderSceneHandle scene, u64 mutationEpoch) const noexcept;
        [[nodiscard]] bool ReadGpuProxy(RenderProxyHandle proxy, RenderSceneGpuReadView& view) const noexcept;
        [[nodiscard]] bool SetGpuInstanceIndex(RenderProxyHandle proxy, GpuInstanceIndex instanceIndex) noexcept;
        void ClearGpuInstanceIndex(RenderProxyHandle proxy) noexcept;
        [[nodiscard]] bool AttachVisibilityFeedback(RenderSceneHandle scene) noexcept;
        [[nodiscard]] bool DetachVisibilityFeedback(RenderSceneHandle scene) noexcept;

        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

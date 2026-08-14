#pragma once

#include <vanguard/ecs/ecs.hpp>
#include <vanguard/rendering/render_scene.hpp>

namespace vanguard::entities
{
    enum class WorldRenderContributorKind : u8
    {
        Mesh,
        Light,
        Decal
    };

    enum class WorldRenderStateFields : u8
    {
        None = 0,
        TransformAndBounds = 1u << 0u,
        Visibility = 1u << 1u,
        LayerMask = 1u << 2u,
        UserDataEpoch = 1u << 3u
    };

    [[nodiscard]] constexpr WorldRenderStateFields operator|(const WorldRenderStateFields left,
                                                              const WorldRenderStateFields right) noexcept
    {
        return static_cast<WorldRenderStateFields>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr WorldRenderStateFields operator&(const WorldRenderStateFields left,
                                                              const WorldRenderStateFields right) noexcept
    {
        return static_cast<WorldRenderStateFields>(static_cast<u8>(left) & static_cast<u8>(right));
    }

    struct WorldRenderContributorBuild
    {
        rendering::MeshProxyDesc mesh;
        rendering::LightProxyDesc light;
        rendering::DecalProxyDesc decal;
    };

    struct WorldRenderState
    {
        WorldRenderStateFields fields = WorldRenderStateFields::None;
        rendering::RenderProxyTransform transform;
        rendering::RenderProxyBounds bounds;
        rendering::RenderProxyVisibilityFlags visibility = rendering::RenderProxyVisibilityFlags::Visible;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
        u64 userDataEpoch = 0;
    };

    using BuildWorldRenderContributor = bool (*)(const ecs::World& world, ecs::EntityId entity,
                                                  WorldRenderContributorBuild& build,
                                                  void* userData) noexcept;
    using ReadWorldRenderState = bool (*)(const ecs::World& world, ecs::EntityId entity,
                                          ecs::CommittedChangeKind change, WorldRenderState& state,
                                          void* userData) noexcept;

    struct WorldRenderContributorDescriptor
    {
        ecs::ComponentId component = ecs::InvalidComponentId;
        const ecs_world_t* componentWorld = nullptr;
        WorldRenderContributorKind kind = WorldRenderContributorKind::Mesh;
        BuildWorldRenderContributor build = nullptr;
        void* userData = nullptr;
    };

    struct WorldRenderStateDescriptor
    {
        ecs::ComponentId component = ecs::InvalidComponentId;
        const ecs_world_t* componentWorld = nullptr;
        WorldRenderStateFields fields = WorldRenderStateFields::None;
        ReadWorldRenderState read = nullptr;
        void* userData = nullptr;
    };

    struct WorldRenderBridgeConfig
    {
        u32 maximumContributorTypes = 128;
        u32 maximumStateTypes = 128;
        u32 maximumTrackedEntities = 1u << 20u;
        u32 maximumChangesPerFlush = 64u * 1024u;
    };

    enum class WorldRenderBridgeFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        InvalidArgument,
        InvalidRegistration,
        DuplicateRegistration,
        CapacityExceeded,
        LostCommittedChanges,
        TranslationFailure,
        RenderSceneFailure,
        RebuildRequired,
        LiveContributorsRemain
    };

    struct WorldRenderBridgeFailure
    {
        WorldRenderBridgeFailureCode code = WorldRenderBridgeFailureCode::None;
        ecs::EntityId entity = ecs::InvalidEntityId;
        ecs::ComponentId component = ecs::InvalidComponentId;
        rendering::RenderSceneFailure renderScene;
        const char* message = nullptr;
    };

    struct WorldRenderBridgeFlushResult
    {
        u64 bridgeGeneration = 0;
        u64 firstChangeSequence = 0;
        u64 nextChangeSequence = 0;
        u32 capturedChanges = 0;
        u32 coalescedChanges = 0;
        u32 createdProxies = 0;
        u32 replacedProxies = 0;
        u32 destroyedProxies = 0;
        u32 updatedProxies = 0;
        bool sceneCommitAllowed = false;
    };

    struct WorldRenderDetachedProxy
    {
        ecs::EntityId entity = ecs::InvalidEntityId;
        ecs::ComponentId contributor = ecs::InvalidComponentId;
        rendering::RenderProxyHandle proxy;
        u64 bridgeGeneration = 0;
    };

    struct WorldRenderBridgeStats
    {
        u32 contributorTypes = 0;
        u32 stateTypes = 0;
        u32 trackedEntities = 0;
        u32 liveContributors = 0;
        u32 pendingDetachments = 0;
        u64 capturedChanges = 0;
        u64 coalescedChanges = 0;
        u64 createdProxies = 0;
        u64 replacedProxies = 0;
        u64 destroyedProxies = 0;
        u64 updatedProxies = 0;
        u64 retiredDetachments = 0;
        u64 staleSessionChanges = 0;
        bool initialized = false;
        bool rebuildRequired = false;
    };

    struct WorldRenderBridgeValidationReport
    {
        u32 scannedWorldContributors = 0;
        u32 trackedContributors = 0;
        u32 missingProxies = 0;
        u32 staleProxies = 0;
        u32 staleEntities = 0;
        bool valid = false;
    };

    /// Per-world adapter between exact ECS commit records and renderer-owned proxies. Translation runs only
    /// after the world flush; callbacks read committed state and never execute inside Flecs observers.
    class WorldRenderBridge final
    {
    public:
        struct Impl;

        WorldRenderBridge() noexcept = default;
        ~WorldRenderBridge();

        WorldRenderBridge(const WorldRenderBridge&) = delete;
        WorldRenderBridge& operator=(const WorldRenderBridge&) = delete;

        [[nodiscard]] bool Initialize(ecs::World& world, rendering::RenderSceneManager& scenes,
                                      rendering::RenderSceneHandle scene, u64 bridgeGeneration,
                                      const WorldRenderBridgeConfig& config = {},
                                      WorldRenderBridgeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(WorldRenderBridgeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RegisterContributor(const WorldRenderContributorDescriptor& descriptor,
                                               WorldRenderBridgeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RegisterState(const WorldRenderStateDescriptor& descriptor,
                                         WorldRenderBridgeFailure* failure = nullptr) noexcept;

        /// Invalidates queued work from the previous session. Existing proxies enter the ordinary deferred
        /// detach path; registrations remain valid because the ECS world itself is unchanged.
        [[nodiscard]] bool ResetSession(u64 bridgeGeneration,
                                        WorldRenderBridgeFailure* failure = nullptr) noexcept;

        /// Captures and coalesces all retained committed changes. A false result forbids publishing the scene;
        /// the caller must rebuild the bridge/scene before any later RenderScene commit.
        [[nodiscard]] bool Flush(WorldRenderBridgeFlushResult& result,
                                 WorldRenderBridgeFailure* failure = nullptr) noexcept;

        /// Emits detach acknowledgements only after no published scene version retains the proxy generation.
        [[nodiscard]] bool RetireDetachedProxies(containers::DynamicArray<WorldRenderDetachedProxy>& detached,
                                                 WorldRenderBridgeFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool FindProxy(ecs::EntityId entity, ecs::ComponentId contributor,
                                     rendering::RenderProxyHandle& proxy) const noexcept;
        /// Cold validation/recovery aid. This performs full Flecs component scans and must not run in the
        /// normal frame path.
        [[nodiscard]] bool ValidateFullRebuild(WorldRenderBridgeValidationReport& report,
                                               WorldRenderBridgeFailure* failure = nullptr) const noexcept;
        [[nodiscard]] WorldRenderBridgeStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::entities

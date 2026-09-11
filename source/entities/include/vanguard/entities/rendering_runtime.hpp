#pragma once

#include <vanguard/entities/transform_runtime.hpp>
#include <vanguard/entities/visual_component.hpp>
#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/world/streaming_executor.hpp>

namespace vanguard::rendering
{
    class MeshResidencyManager;
    struct MeshDrawPhaseContext;
    class RenderCommandSystem;
    class RenderPhaseRegistry;
}

namespace vanguard::entities
{
    class StaticMeshComponent;
    class CameraComponent;
    inline constexpr game::RuntimeSystemId RenderingRuntimeSystemId = 37;

    struct RenderingRuntimeConfig
    {
        rendering::RenderSceneDesc scene{"world"};
        rendering::RenderSceneHandle existingScene;
        u32 maximumPendingProxyAdmissions = 64u * 1024u;
        u32 maximumProxyAdmissionsPerFrame = 1024u;
        u32 maximumPendingProxyRetirements = 64u * 1024u;
        /// Keeps a hidden proxy alive across complete frame boundaries before destruction.
        /// This is the lifetime seam that a later dissolve implementation will extend.
        u32 proxyRetirementDelayFrames = 2u;
        u32 maximumPendingMeshPreparations = 64u * 1024u;
        u32 maximumMeshPreparationsPerFrame = 128u;
        /// Startup-only source, copied during world initialization. Attachments come
        /// from renderer composition, never from serialized component properties.
        containers::ArraySpan<const rendering::MeshDrawPhaseContext> meshDrawPhases;
        u32 maximumPendingCameraTransforms = 1024u;
    };

    struct RenderingRuntimeStats
    {
        rendering::RenderSceneHandle scene;
        u32 activeProxies = 0;
        u32 pendingProxyAdmissions = 0;
        u32 pendingProxyRetirements = 0;
        u32 pendingMeshPreparations = 0;
        u32 pendingCameraTransforms = 0;
        u64 admittedProxies = 0;
        u64 cancelledProxyAdmissions = 0;
        u64 failedProxyAdmissions = 0;
        u64 retiredProxies = 0;
        u64 relinkFailures = 0;
        u32 residentDistantProxies = 0;
        u32 pendingDistantProxies = 0;
        u64 admittedDistantProxies = 0;
        u64 releasedDistantProxies = 0;
        u64 failedDistantProxies = 0;
        bool ownsScene = false;
        bool initialized = false;
    };

    /// Per-world runtime that wraps one RenderScene and provides the proxy creation
    /// boundary used by visual components. The engine RenderingService owns the
    /// renderer itself; this runtime owns only world-session rendering state.
    class RenderingRuntime final : public game::RuntimeSystem
    {
    public:
        struct Impl;

        RenderingRuntime(rendering::RenderSceneManager& scenes, const RenderingRuntimeConfig& config = {}) noexcept;
        ~RenderingRuntime() override;

        RenderingRuntime(const RenderingRuntime&) = delete;
        RenderingRuntime& operator=(const RenderingRuntime&) = delete;

        [[nodiscard]] rendering::RenderSceneHandle GetScene() const noexcept;
        [[nodiscard]] rendering::RenderSceneManager& GetScenes() noexcept;
        [[nodiscard]] const rendering::RenderSceneManager& GetScenes() const noexcept;
        [[nodiscard]] TransformRuntime* GetTransforms() noexcept;
        [[nodiscard]] const TransformRuntime* GetTransforms() const noexcept;
        /// Bind before world initialization. The engine service outlives this runtime.
        [[nodiscard]] bool BindMeshResidency(rendering::MeshResidencyManager& residency) noexcept;
        [[nodiscard]] rendering::MeshResidencyManager* GetMeshResidency() noexcept;
        [[nodiscard]] containers::ArraySpan<const rendering::MeshDrawPhaseContext> GetMeshDrawPhases() const noexcept;
        [[nodiscard]] bool BindCommands(rendering::RenderCommandSystem& commands, const rendering::RenderPhaseRegistry& phases) noexcept;
        [[nodiscard]] rendering::RenderCommandSystem* GetCommands() noexcept { return m_commands; }
        [[nodiscard]] const rendering::RenderPhaseRegistry* GetRenderPhases() const noexcept { return m_renderPhases; }
        /// Owner-thread only, after joining the complete transform CPU tail and before
        /// frame view preparation. Processes changed cameras only; no camera-table scan.
        [[nodiscard]] bool FlushCameraTransforms() noexcept;
        void ReportComponentFailure(const char* message) noexcept;

        /// Binds the immutable cooked-world directory and its streaming executor before
        /// GameWorld initialization. Distant proxies use dense world-record indices and
        /// therefore require no per-event lookup allocation.
        [[nodiscard]] bool BindDistantProxyStreaming(const world::WorldFile& world, world::WorldStreamingExecutor& executor) noexcept;
        /// Consumes one non-cell streaming event. A distant proxy becomes render-ready only
        /// after admission to RenderScene; release is acknowledged after visibility is removed
        /// and proxy lifetime has transferred to the deferred-retirement queue.
        [[nodiscard]] bool HandleStreamingEvent(const world::StreamingResourceEvent& event) noexcept;

        /// Immediate destruction is reserved for admission rollback and teardown fallback.
        /// Normal visual-component removal uses RetireProxy().
        [[nodiscard]] bool DestroyProxy(rendering::RenderProxyHandle proxy, rendering::RenderSceneFailure* failure = nullptr) noexcept;

        /// Queues proxy admission into the per-frame budget. Descriptors and callback storage
        /// must remain self-contained; debug-name pointers must outlive completion or cancellation.
        [[nodiscard]] bool QueueProxyAdmission(rendering::RenderProxyDesc desc, const ProxyAdmissionSink& sink,
                                               ProxyAdmissionHandle& admission) noexcept;
        [[nodiscard]] bool QueueProxyAdmission(rendering::MeshProxyDesc desc, const ProxyAdmissionSink& sink,
                                               ProxyAdmissionHandle& admission) noexcept;
        [[nodiscard]] bool QueueProxyAdmission(rendering::LightProxyDesc desc, const ProxyAdmissionSink& sink,
                                               ProxyAdmissionHandle& admission) noexcept;
        [[nodiscard]] bool QueueProxyAdmission(rendering::DecalProxyDesc desc, const ProxyAdmissionSink& sink,
                                               ProxyAdmissionHandle& admission) noexcept;
        /// Cancels only a still-pending admission. Completion invalidates the admission handle.
        [[nodiscard]] bool CancelProxyAdmission(ProxyAdmissionHandle admission) noexcept;

        /// Removes a proxy from visibility immediately and transfers its remaining lifetime to
        /// the runtime. Actual destruction occurs after the configured frame delay.
        [[nodiscard]] bool RetireProxy(rendering::RenderProxyHandle proxy, rendering::RenderSceneFailure* failure = nullptr) noexcept;

        /// Builds the direct transform-worker relink binding for a proxy owned by this runtime.
        /// The binding must be removed before this runtime is destroyed.
        [[nodiscard]] bool GetVisualProxyBinding(rendering::RenderProxyHandle proxy, u64 producerGeneration,
                                                 VisualProxyBinding& binding) noexcept;
        /// Called after the transform CPU tail. Returns the first worker-side relink failure.
        [[nodiscard]] bool ConsumeRelinkFailure(VisualRelinkFailure& failure) noexcept;
        /// Consumes the first main-thread admission or retirement failure so readiness can
        /// be reevaluated after the owner has handled the error.
        [[nodiscard]] bool ConsumeProxyLifecycleFailure(rendering::RenderSceneFailure& failure) noexcept;

        /// Idempotent scene release used by world teardown and rollback.
        [[nodiscard]] bool ReleaseScene(rendering::RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] RenderingRuntimeStats GetStats() const noexcept;

    protected:
        [[nodiscard]] bool OnInitialize(game::GameWorld& world) noexcept override;
        [[nodiscard]] bool OnSetup(game::GameWorld& world) noexcept override;
        void OnUninitialize(game::GameWorld& world) noexcept override;
        void OnBeginFrame(game::GameWorld& world, f32 deltaSeconds) noexcept override;
        [[nodiscard]] const char* ReadinessBlocker() const noexcept override;

    private:
        friend class StaticMeshComponent;
        friend class CameraComponent;
        void QueueCameraTransform(CameraComponent& component) noexcept;
        void CancelCameraTransform(CameraComponent& component) noexcept;
        [[nodiscard]] bool QueueMeshPreparation(StaticMeshComponent& component) noexcept;
        void CancelMeshPreparation(StaticMeshComponent& component) noexcept;
        static bool CompleteDistantProxyAdmission(ProxyAdmissionHandle admission, rendering::RenderProxyHandle proxy,
                                                   const rendering::RenderSceneFailure* failure, void* userData) noexcept;
        static void ReportRelinkFailure(const VisualRelinkFailure& failure, void* userData) noexcept;

        rendering::RenderSceneManager* m_scenes = nullptr;
        rendering::MeshResidencyManager* m_meshResidency = nullptr;
        rendering::RenderCommandSystem* m_commands = nullptr;
        const rendering::RenderPhaseRegistry* m_renderPhases = nullptr;
        const world::WorldFile* m_streamingWorld = nullptr;
        world::WorldStreamingExecutor* m_streamingExecutor = nullptr;
        RenderingRuntimeConfig m_config;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::entities

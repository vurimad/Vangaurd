#pragma once

#include <vanguard/rendering/gpu_scene_definitions.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_scene_gpu.hpp>

namespace vanguard::rendering
{
    struct GpuSceneRuntimeConfig
    {
        GpuSceneTablesConfig tables;
        GpuSceneLifetimeConfig lifetime;
        GpuSceneUploadConfig upload;
        GpuSceneDefinitionsConfig definitions;
        RenderSceneGpuConfig scenePublication;
        u32 maximumExternalContributions = 8;
    };

    struct GpuSceneContributionToken
    {
        u64 value0 = 0;
        u64 value1 = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value0 != 0 || value1 != 0;
        }
    };

    using WriteGpuSceneContribution = bool (*)(void* owner, GpuSceneContributionToken token,
                                                containers::ArraySpan<const GpuSceneUploadReservation> reservations,
                                                const char*& failureMessage) noexcept;
    using AcceptGpuSceneContribution = void (*)(void* owner, GpuSceneContributionToken token,
                                                 rhi::GpuFence sharedCompletion) noexcept;
    using RetryGpuSceneContribution = bool (*)(void* owner, GpuSceneContributionToken token,
                                                const char*& failureMessage) noexcept;

    /// One already frozen producer contribution. StageContribution copies the request span into bounded
    /// coordinator storage; owner and token must remain valid until ResolveContributions completes.
    struct GpuSceneContributionDesc
    {
        void* owner = nullptr;
        GpuSceneContributionToken token;
        containers::ArraySpan<const GpuSceneUploadRequest> requests;
        WriteGpuSceneContribution write = nullptr;
        AcceptGpuSceneContribution accept = nullptr;
        RetryGpuSceneContribution retry = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return owner != nullptr && token.IsValid() && !requests.Empty() && requests.Data() != nullptr &&
                   write != nullptr && accept != nullptr && retry != nullptr;
        }
    };

    enum class GpuSceneRuntimeFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        Busy,
        LiveScenesRemain,
        TablesFailure,
        LifetimeFailure,
        UploadFailure,
        DefinitionsFailure,
        ScenePublicationFailure,
        ContributionFailure
    };

    struct GpuSceneRuntimeFailure
    {
        GpuSceneRuntimeFailureCode code = GpuSceneRuntimeFailureCode::None;
        const char* message = nullptr;
        GpuSceneTablesFailure tablesFailure;
        GpuSceneLifetimeFailure lifetimeFailure;
        GpuSceneUploadFailure uploadFailure;
        GpuSceneDefinitionFailure definitionsFailure;
        RenderSceneGpuFailure scenePublicationFailure;
        const char* contributionFailure = nullptr;
    };

    struct GpuSceneRuntimeStats
    {
        u64 publicationTicks = 0;
        u64 submittedBatches = 0;
        u64 completedPublications = 0;
        u64 publishedObjects = 0;
        u64 deferredScenes = 0;
        u64 failedPublications = 0;
        u64 stagedContributions = 0;
        u64 acceptedContributions = 0;
        u64 retriedContributions = 0;
        bool publicationFailurePending = false;
    };

    /// Owns the renderer-wide persistent GPU Scene stack in dependency order. Initialization requires
    /// a live RHI descriptor domain and must happen before any RenderScene is created.
    class GpuSceneRuntime final
    {
    public:
        struct PublicationState;

        GpuSceneRuntime() noexcept = default;
        ~GpuSceneRuntime();

        GpuSceneRuntime(const GpuSceneRuntime&) = delete;
        GpuSceneRuntime& operator=(const GpuSceneRuntime&) = delete;

        [[nodiscard]] bool Initialize(RenderSceneManager& scenes, const GpuSceneRuntimeConfig& config, GpuSceneRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(const rhi::DescriptorRetirement& safeAfter, GpuSceneRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool Manages(const RenderSceneManager& scenes) const noexcept;

        /// Plans one bounded, sparse publication batch from the exact scene epochs carried by RenderCommandSystem.
        /// Range writers and submission are appended to the supplied RenderPath continuation without a CPU wait.
        [[nodiscard]] bool Publish(RenderFrameTickContext& context, GpuSceneRuntimeFailure* failure = nullptr) noexcept;
        /// Main-thread. Copies one producer's frozen update requests into the next shared GPU Scene batch.
        [[nodiscard]] bool StageContribution(const GpuSceneContributionDesc& contribution,
                                             GpuSceneRuntimeFailure* failure = nullptr) noexcept;
        /// Main-thread after the renderer CPU tail is flushed. Accepts submitted contributions or returns
        /// pre-submit failures to their producer queues.
        [[nodiscard]] bool ResolveContributions(GpuSceneRuntimeFailure* failure = nullptr) noexcept;
        /// Consumes the first asynchronous publication failure after the owning RenderCommandSystem CPU tail is flushed.
        [[nodiscard]] bool ConsumePublicationFailure(GpuSceneRuntimeFailure& failure) noexcept;
        [[nodiscard]] GpuSceneRuntimeStats GetStats() const noexcept;

        [[nodiscard]] GpuSceneTables& GetTables() noexcept;
        [[nodiscard]] const GpuSceneTables& GetTables() const noexcept;
        [[nodiscard]] GpuSceneLifetime& GetLifetime() noexcept;
        [[nodiscard]] const GpuSceneLifetime& GetLifetime() const noexcept;
        [[nodiscard]] GpuSceneUploader& GetUploader() noexcept;
        [[nodiscard]] const GpuSceneUploader& GetUploader() const noexcept;
        [[nodiscard]] GpuSceneDefinitions& GetDefinitions() noexcept;
        [[nodiscard]] const GpuSceneDefinitions& GetDefinitions() const noexcept;
        [[nodiscard]] RenderSceneGpuPublisher& GetScenePublisher() noexcept;
        [[nodiscard]] const RenderSceneGpuPublisher& GetScenePublisher() const noexcept;

    private:
        void RollbackInitialization() noexcept;

        RenderSceneManager* m_scenes = nullptr;
        GpuSceneTables m_tables;
        GpuSceneLifetime m_lifetime;
        GpuSceneUploader m_uploader;
        GpuSceneDefinitions m_definitions;
        RenderSceneGpuPublisher m_scenePublisher;
        PublicationState* m_publication = nullptr;
        bool m_initialized = false;
    };
} // namespace vanguard::rendering

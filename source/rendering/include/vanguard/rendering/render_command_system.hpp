#pragma once

#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/viewport.hpp>

namespace vanguard::rendering
{
    class FrameRenderer;

    enum class RenderCommandFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDescriptor,
        InvalidState,
        DuplicateScene,
        SceneFailure,
        ExecutionFailure,
        SubmissionFailure
    };

    enum class RenderCommandExecutionStage : u8
    {
        None,
        FrameTick,
        RenderFrame
    };

    struct RenderCommandFailure
    {
        RenderCommandFailureCode code = RenderCommandFailureCode::None;
        RenderCommandExecutionStage executionStage = RenderCommandExecutionStage::None;
        u64 executionSerial = 0;
        RenderSceneHandle scene;
        RenderSceneFailure sceneFailure;
        const char* message = nullptr;
    };

    struct RenderCommandFrameTickResult
    {
        u64 tick = 0;
        u32 submittedScenes = 0;
        u32 asynchronousScenes = 0;
    };

    struct RenderCommandSystemStats
    {
        u64 frameTicks = 0;
        u64 completedFrameTicks = 0;
        u64 failedFrameTicks = 0;
        u64 submittedFrames = 0;
        u64 completedFrames = 0;
        u64 failedFrames = 0;
        u64 skippedFrames = 0;
        u64 lastCompletedFrameSerial = 0;
        u64 explicitFlushes = 0;
        u64 suppressedExecutionFailures = 0;
        u64 rejectedOperations = 0;
        bool initialized = false;
        bool executionFailurePending = false;
        bool workOutstanding = false;
    };

    struct RenderSceneProcessingEpoch
    {
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
    };

    /// Renderer-global continuation created after scene update work on the shared CPU rendering chain.
    class RenderFrameTickContext final
    {
    public:
        RenderFrameTickContext(const u64 tick, const containers::ArraySpan<const RenderSceneProcessingEpoch> scenes,
                               const jobs::JobContext& continuation) noexcept
            : m_tick(tick), m_scenes(scenes), m_dispatcherThreadIndex(continuation.dispatcherThreadIndex), m_jobs(continuation)
        {
        }

        RenderFrameTickContext(const RenderFrameTickContext&) = delete;
        RenderFrameTickContext& operator=(const RenderFrameTickContext&) = delete;

        [[nodiscard]] u64 Tick() const noexcept
        {
            return m_tick;
        }

        [[nodiscard]] u32 GetDispatcherThreadIndex() const noexcept
        {
            return m_dispatcherThreadIndex;
        }

        [[nodiscard]] containers::ArraySpan<const RenderSceneProcessingEpoch> GetScenes() const noexcept
        {
            return m_scenes;
        }

        [[nodiscard]] jobs::Builder& GetJobs() noexcept
        {
            return m_jobs;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_tick != 0 && m_jobs.IsValid();
        }

    private:
        u64 m_tick = 0;
        containers::ArraySpan<const RenderSceneProcessingEpoch> m_scenes;
        u32 m_dispatcherThreadIndex = 0;
        jobs::Builder m_jobs;
    };

    /// Renderer-owned execution context created inside the retained RenderFrame job. Work dispatched through GetJobs()
    /// continues the command system's CPU rendering chain instead of creating or waiting on a second chain.
    class RenderFrameContext final
    {
    public:
        RenderFrameContext(const RenderFrameInfo& frame, const jobs::JobContext& continuation) noexcept
            : m_frame(frame), m_dispatcherThreadIndex(continuation.dispatcherThreadIndex), m_jobs(continuation)
        {
        }

        RenderFrameContext(const RenderFrameContext&) = delete;
        RenderFrameContext& operator=(const RenderFrameContext&) = delete;

        [[nodiscard]] const RenderFrameInfo& GetFrame() const noexcept
        {
            return m_frame;
        }

        [[nodiscard]] jobs::Builder& GetJobs() noexcept
        {
            return m_jobs;
        }

        [[nodiscard]] u32 GetDispatcherThreadIndex() const noexcept
        {
            return m_dispatcherThreadIndex;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_jobs.IsValid();
        }

    private:
        const RenderFrameInfo& m_frame;
        u32 m_dispatcherThreadIndex = 0;
        jobs::Builder m_jobs;
    };

    using ExecuteRenderingFrameTick = RenderFrameExecutionStatus (*)(RenderFrameTickContext&, void*) noexcept;
    using ExecuteRenderingFrame = RenderFrameExecutionStatus (*)(RenderFrameContext&, void*) noexcept;

    struct RenderCommandSystemConfig
    {
        ExecuteRenderingFrameTick executeFrameTick = nullptr;
        ExecuteRenderingFrame executeFrame = nullptr;
        void* userData = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return executeFrameTick != nullptr && executeFrame != nullptr;
        }
    };

    /// Owns the single CPU rendering chain. FrameTick appends command/scene preparation and RenderFrame appends one retained
    /// viewport frame to the same tail. GPU completion remains an independent RHI-fence contract.
    class RenderCommandSystem final
    {
    public:
        struct Impl;

        RenderCommandSystem() noexcept = default;
        ~RenderCommandSystem();

        RenderCommandSystem(const RenderCommandSystem&) = delete;
        RenderCommandSystem& operator=(const RenderCommandSystem&) = delete;

        [[nodiscard]] bool Initialize(RenderSceneManager& scenes, RenderCameraStorage& cameras, const RenderCommandSystemConfig& config,
                                      RenderCommandFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderCommandFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RegisterCamera(const RenderCameraDesc& desc, RenderCameraHandle& camera, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnregisterCamera(RenderCameraHandle camera, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateCamera(RenderCameraHandle camera, const RenderCameraState& state, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestCameraCut(RenderCameraHandle camera, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetCameraEnabled(RenderCameraHandle camera, bool enabled, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetCameraRenderPolicy(RenderCameraHandle camera, RenderCameraRenderPolicy policy, RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AddCameraDependency(RenderCameraHandle parent, RenderCameraHandle child, RenderCameraDependencyOutputs outputs,
                                               RenderCameraFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RemoveCameraDependency(RenderCameraHandle parent, RenderCameraHandle child, RenderCameraFailure* failure = nullptr) noexcept;

        /// Appends one scene-update boundary and renderer frame tick behind the current CPU rendering tail. The scene span must
        /// contain unique live handles; duplicate detection uses a preallocated direct scene-index directory.
        [[nodiscard]] bool FrameTick(containers::ArraySpan<const RenderSceneHandle> scenes, u64 tickCounter, RenderCommandFrameTickResult& result,
                                     RenderCommandFailure* failure = nullptr) noexcept;

        /// Retains and appends one viewport frame behind FrameTick and every earlier render frame.
        [[nodiscard]] bool RenderFrame(const RenderFrameInfo& frame, RenderFrameSubmission& submission, RenderCommandFailure* failure = nullptr) noexcept;

        /// Strong CPU boundary. This processes/waits for the current rendering chain, never for GPU queue completion.
        [[nodiscard]] bool FlushPreviousFrameProcessing(RenderCommandFailure* failure = nullptr) noexcept;

        /// Consumes the first asynchronous execution failure observed since the previous call. The CPU chain must first
        /// be made quiescent through FlushPreviousFrameProcessing so the returned message remains stable.
        [[nodiscard]] bool ConsumeExecutionFailure(RenderCommandFailure& failure) noexcept;
        [[nodiscard]] bool IsIdle() const noexcept;
        [[nodiscard]] RenderCommandSystemStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

#pragma once

#include <vanguard/rendering/geometry_frame_work.hpp>
#include <vanguard/rendering/render_flow_resource_execution.hpp>
#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/viewport.hpp>
#include <vanguard/rhi/rhi.hpp>

namespace vanguard::rendering
{
    class FrameRenderer;
    class RenderCommandSystem;
    class RenderNodeResourceBindings;
    class RenderNodeResourcePreparationFailures;
    struct RenderFlowResourceFailure;

    namespace detail
    {
        struct RenderCommandRetainedFrameAccess;
    }

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

    enum class ReservedFrameCommandList : u32
    {
        StorageData,
        Count
    };

    class RenderFrameCommandLists final
    {
    public:
        RenderFrameCommandLists() noexcept;
        ~RenderFrameCommandLists();

        RenderFrameCommandLists(const RenderFrameCommandLists&) = delete;
        RenderFrameCommandLists& operator=(const RenderFrameCommandLists&) = delete;

        void PrepareForFrame(u32 commandListCount) noexcept;
        void SetCommandList(u32 index, rhi::CommandListRef commandList) noexcept;
        [[nodiscard]] rhi::CommandListRef GetCommandList(u32 index) const noexcept;
        [[nodiscard]] u32 GetCount() const noexcept;
        [[nodiscard]] bool Submit(const char* scopeName, u32 upToIndex, rhi::CommandListSyncType sync, jobs::Builder& builder) noexcept;
        void Reset() noexcept;

    private:
        struct Entry
        {
            rhi::CommandListRef commandList;
            CommandScopeId scope;
            rhi::QueueType queue = rhi::QueueType::Graphics;
            rhi::FailureCode closeFailure = rhi::FailureCode::None;
            bool completionRecorded = false;
        };

        struct ExpectedQueueDependency
        {
            CommandScopeId producerScope;
            CommandScopeId consumerScope;
            rhi::CommandListSyncType sync = rhi::CommandListSyncType::None;
            u32 producerIndex = 0;
            u32 consumerIndex = 0;
            bool completionRecorded = false;
        };

        void RegisterCommandScope(u32 index, CommandScopeId scope, rhi::QueueType queue) noexcept;
        void RegisterQueueDependency(const CompiledQueueDependency& dependency) noexcept;
        void SealCommandScopes() noexcept;
        void FinalizeSubmissions(bool frameFailed) noexcept;
        [[nodiscard]] bool HasFailure() const noexcept { return m_submissionFailurePending; }
        [[nodiscard]] bool GetFirstSubmissionFailure(rhi::Failure& failure) const noexcept;
        [[nodiscard]] containers::ArraySpan<const CommandScopeExecutionReceipt> GetCommandScopeReceipts() const noexcept;
        [[nodiscard]] containers::ArraySpan<const QueueDependencyExecutionReceipt> GetQueueDependencyReceipts() const noexcept;
        [[nodiscard]] bool WasDeviceLost() const noexcept;

        containers::DynamicArray<Entry> m_commandLists;
        containers::DynamicArray<ExpectedQueueDependency> m_expectedQueueDependencies;
        containers::DynamicArray<CommandScopeExecutionReceipt> m_commandScopeReceipts;
        containers::DynamicArray<QueueDependencyExecutionReceipt> m_queueDependencyReceipts;
        rhi::Failure m_submissionFailure;
        u32 m_nextFlushStart = 0;
        // Written by ordered submission continuations and read only after their terminal join.
        bool m_submissionFailurePending = false;
        bool m_deviceLost = false;

        friend class FrameRenderer;
        friend class RenderNodeGraph;
        friend struct RenderNodeImplContext;
    };

    /// Cheap strong reference to the one stable frame allocation shared by the
    /// render-command root and every independently scheduled frame branch.
    class RetainedRenderFrameRef final
    {
    public:
        struct Impl;

        RetainedRenderFrameRef() noexcept = default;
        ~RetainedRenderFrameRef();
        RetainedRenderFrameRef(const RetainedRenderFrameRef& other) noexcept;
        RetainedRenderFrameRef& operator=(const RetainedRenderFrameRef& other) noexcept;
        RetainedRenderFrameRef(RetainedRenderFrameRef&& other) noexcept;
        RetainedRenderFrameRef& operator=(RetainedRenderFrameRef&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const RenderFrameInfo& GetFrame() const noexcept;
        [[nodiscard]] PreparedRenderViewFamily& GetPreparedViewFamily() noexcept;
        [[nodiscard]] FrameCustomData& GetFrameCustomData() noexcept;
        [[nodiscard]] GeometryFrameWork& GetGeometryFrameWork() noexcept;
        [[nodiscard]] RenderFrameCommandLists& GetFrameCommandLists() noexcept;
        [[nodiscard]] RenderNodeResourceBindings& GetResourceBindings() noexcept;
        [[nodiscard]] RenderNodeResourcePreparationFailures& GetResourcePreparationFailures() noexcept;
        [[nodiscard]] RenderFrameOutputTransaction& GetOutputTransaction() noexcept;
        void RecordFailure(const char* message) noexcept;
        void RecordFailure(const RenderFlowResourceFailure& failure) noexcept;
        [[nodiscard]] bool HasFailure() const noexcept;
        [[nodiscard]] const char* GetFailureMessage() const noexcept;
        void InstallJobsRenderFrame() noexcept;
        void ClearJobsRenderFrame() noexcept;
        void Reset() noexcept;

    private:
        explicit RetainedRenderFrameRef(Impl* impl) noexcept : m_impl(impl) {}
        void DeferTerminalCompletion() noexcept;
        void PublishTerminalCompletion(bool skipped = false) noexcept;

        Impl* m_impl = nullptr;
        friend class FrameRenderer;
        friend struct detail::RenderCommandRetainedFrameAccess;
    };

    /// Renderer-global continuation created after scene update work on the shared CPU rendering chain.
    class RenderFrameTickContext final
    {
    public:
        RenderFrameTickContext(const u64 tick, const containers::ArraySpan<const RenderSceneProcessingEpoch> scenes,
                               const jobs::JobContext& continuation) noexcept
            : m_tick(tick), m_scenes(scenes), m_dispatcherThreadIndex(continuation.dispatcherThreadIndex), m_builder(continuation)
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

        [[nodiscard]] jobs::Builder& GetBuilder() noexcept
        {
            return m_builder;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_tick != 0 && m_builder.IsValid();
        }

    private:
        u64 m_tick = 0;
        containers::ArraySpan<const RenderSceneProcessingEpoch> m_scenes;
        u32 m_dispatcherThreadIndex = 0;
        jobs::Builder m_builder;
    };

    /// Renderer-owned execution context created inside the retained RenderFrame job. Work dispatched through GetBuilder()
    /// continues the command system's CPU rendering chain instead of creating or waiting on a second chain.
    class RenderFrameContext final
    {
    public:
        RenderFrameContext(const RetainedRenderFrameRef& frame, const jobs::JobContext& continuation) noexcept
            : m_frame(frame), m_dispatcherThreadIndex(continuation.dispatcherThreadIndex), m_builder(continuation)
        {
        }

        RenderFrameContext(const RenderFrameContext&) = delete;
        RenderFrameContext& operator=(const RenderFrameContext&) = delete;

        [[nodiscard]] const RenderFrameInfo& GetFrame() const noexcept
        {
            return m_frame.GetFrame();
        }

        [[nodiscard]] RenderViewport* GetViewport() const noexcept
        {
            return m_frame.GetFrame().GetViewport();
        }

        [[nodiscard]] RetainedRenderFrameRef RetainFrame() const noexcept
        {
            return m_frame;
        }

        [[nodiscard]] PreparedRenderViewFamily& GetPreparedViewFamily() noexcept
        {
            return m_frame.GetPreparedViewFamily();
        }

        [[nodiscard]] FrameCustomData& GetFrameCustomData() noexcept
        {
            return m_frame.GetFrameCustomData();
        }

        [[nodiscard]] GeometryFrameWork& GetGeometryFrameWork() noexcept
        {
            return m_frame.GetGeometryFrameWork();
        }

        [[nodiscard]] RenderFrameCommandLists& GetFrameCommandLists() noexcept
        {
            return m_frame.GetFrameCommandLists();
        }

        [[nodiscard]] RenderNodeResourceBindings& GetResourceBindings() noexcept
        {
            return m_frame.GetResourceBindings();
        }

        [[nodiscard]] RenderNodeResourcePreparationFailures& GetResourcePreparationFailures() noexcept
        {
            return m_frame.GetResourcePreparationFailures();
        }

        [[nodiscard]] jobs::Builder& GetBuilder() noexcept
        {
            return m_builder;
        }

        [[nodiscard]] u32 GetDispatcherThreadIndex() const noexcept
        {
            return m_dispatcherThreadIndex;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_frame.IsValid() && m_builder.IsValid();
        }

    private:
        RetainedRenderFrameRef m_frame;
        u32 m_dispatcherThreadIndex = 0;
        jobs::Builder m_builder;
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
        [[nodiscard]] bool RenderFrame(const RenderFrameInfo& frame, RenderFrameOutputTransaction& output, RenderFrameSubmission& submission,
                                       RenderCommandFailure* failure = nullptr) noexcept;

        /// Strong CPU boundary. This processes/waits for the current rendering chain, never for GPU queue completion.
        [[nodiscard]] bool FlushPreviousFrameProcessing(RenderCommandFailure* failure = nullptr) noexcept;

        /// Consumes the first asynchronous execution failure observed since the previous call. The CPU chain must first
        /// be made quiescent through FlushPreviousFrameProcessing so the returned message remains stable.
        [[nodiscard]] bool ConsumeExecutionFailure(RenderCommandFailure& failure) noexcept;
        [[nodiscard]] bool IsIdle() const noexcept;
        [[nodiscard]] RenderCommandSystemStats GetStats() const noexcept;

    private:
        void PublishFrameCompletion(u64 serial, bool failed, bool skipped, const char* message) noexcept;

        Impl* m_impl = nullptr;
        friend class RetainedRenderFrameRef;
    };
} // namespace vanguard::rendering

#include <vanguard/rendering/render_command_system.hpp>

#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/render_node_job.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/system/assert.hpp>

#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u32 MaximumExecutionFailureMessageBytes = 256;

        struct FailureContext
        {
            RenderSceneHandle scene;
            const RenderSceneFailure* sceneFailure = nullptr;
        };

        void ClearFailure(RenderCommandFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        void CopyFailureMessage(char* const destination, const u32 capacity, const char* const source) noexcept
        {
            const char* const message = source != nullptr ? source : "unspecified asynchronous rendering failure";
            u32 index = 0;
            while (index + 1u < capacity && message[index] != '\0')
            {
                destination[index] = message[index];
                ++index;
            }
            destination[index] = '\0';
        }

        [[nodiscard]] bool Fail(RenderCommandFailure* const failure, RenderCommandFailureCode code, const char* message, FailureContext context = {}) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->scene = context.scene;
                failure->message = message;
                if (context.sceneFailure != nullptr)
                    failure->sceneFailure = *context.sceneFailure;
            }
            return false;
        }

        [[nodiscard]] bool CameraBoundaryFailure(RenderCameraFailure* const failure, const RenderCommandFailure& commandFailure,
                                                 const RenderSceneHandle scene = {}, const RenderCameraHandle camera = {},
                                                 const RenderCameraHandle relatedCamera = {}) noexcept
        {
            if (failure != nullptr)
            {
                failure->code =
                    commandFailure.code == RenderCommandFailureCode::WrongThread ? RenderCameraFailureCode::WrongThread : RenderCameraFailureCode::Busy;
                failure->scene = scene;
                failure->camera = camera;
                failure->relatedCamera = relatedCamera;
                failure->message = "camera command could not establish the ordered CPU rendering boundary";
            }
            return false;
        }

    } // namespace

    RenderFrameCommandLists::RenderFrameCommandLists() noexcept
        : m_commandLists(memory::pools::Rendering::GetInstance()), m_expectedQueueDependencies(memory::pools::Rendering::GetInstance()),
          m_commandScopeReceipts(memory::pools::Rendering::GetInstance()), m_queueDependencyReceipts(memory::pools::Rendering::GetInstance())
    {
    }

    RenderFrameCommandLists::~RenderFrameCommandLists()
    {
        Reset();
    }

    void RenderFrameCommandLists::PrepareForFrame(const u32 commandListCount) noexcept
    {
        for (Entry& entry : m_commandLists)
        {
            if (entry.commandList.IsValid())
                VG_FATAL("frame command list survived its frame terminal boundary");
        }
        m_commandLists.Resize(commandListCount);
        for (Entry& entry : m_commandLists)
            entry = {};
        m_expectedQueueDependencies.Clear();
        m_commandScopeReceipts.Clear();
        m_queueDependencyReceipts.Clear();
        m_submissionFailure = {};
        m_nextFlushStart = 0;
        m_submissionFailurePending = false;
        m_deviceLost = false;
    }

    void RenderFrameCommandLists::SetCommandList(const u32 index, const rhi::CommandListRef commandList) noexcept
    {
        if (index >= m_commandLists.Size())
            VG_FATAL("frame command-list index lies outside the prepared frame storage");
        if (commandList.IsValid() && m_commandLists[index].commandList.IsValid())
            VG_FATAL("frame command-list slot is already occupied");
        m_commandLists[index].commandList = commandList;
    }

    void RenderFrameCommandLists::RegisterCommandScope(const u32 index, const CommandScopeId scope, const rhi::QueueType queue) noexcept
    {
        if (index >= m_commandLists.Size() || !scope.IsValid())
            VG_FATAL("compiled command scope lies outside the prepared frame storage");
        Entry& entry = m_commandLists[index];
        if (entry.scope.IsValid())
            VG_FATAL("frame command-list slot already has a compiled command scope");
        entry.scope = scope;
        entry.queue = queue;
    }

    void RenderFrameCommandLists::RegisterQueueDependency(const CompiledQueueDependency& dependency) noexcept
    {
        if (!dependency.producerScope.IsValid() || !dependency.consumerScope.IsValid() ||
            (dependency.sync != rhi::CommandListSyncType::ForkAsyncCompute && dependency.sync != rhi::CommandListSyncType::JoinAsyncCompute))
            VG_FATAL("compiled queue dependency is invalid");
        m_expectedQueueDependencies.PushBack({dependency.producerScope, dependency.consumerScope, dependency.sync, InvalidRenderFlowResourceIndex, InvalidRenderFlowResourceIndex, false});
    }

    void RenderFrameCommandLists::SealCommandScopes() noexcept
    {
        for (ExpectedQueueDependency& dependency : m_expectedQueueDependencies)
        {
            for (u32 index = 0; index < m_commandLists.Size(); ++index)
            {
                const Entry& entry = m_commandLists[index];
                if (entry.scope == dependency.producerScope)
                    dependency.producerIndex = index;
                if (entry.scope == dependency.consumerScope)
                    dependency.consumerIndex = index;
            }
            if (dependency.producerIndex == InvalidRenderFlowResourceIndex || dependency.consumerIndex == InvalidRenderFlowResourceIndex ||
                dependency.producerIndex >= dependency.consumerIndex)
                VG_FATAL("compiled queue dependency does not match the registered command-scope order");
        }
    }

    rhi::CommandListRef RenderFrameCommandLists::GetCommandList(const u32 index) const noexcept
    {
        if (index >= m_commandLists.Size())
            VG_FATAL("frame command-list index lies outside the prepared frame storage");
        return m_commandLists[index].commandList;
    }

    u32 RenderFrameCommandLists::GetCount() const noexcept
    {
        return m_commandLists.Size();
    }

    bool RenderFrameCommandLists::Submit(const char* const scopeName, const u32 upToIndex, const rhi::CommandListSyncType sync, jobs::Builder& builder) noexcept
    {
        if (scopeName == nullptr || scopeName[0] == '\0' || upToIndex >= m_commandLists.Size())
            return false;
        if (upToIndex < m_nextFlushStart)
            VG_FATAL("render-frame submission boundaries must execute in their authored order");
        const u32 firstIndex = m_nextFlushStart;
        for (u32 index = firstIndex; index <= upToIndex; ++index)
            m_commandLists[index].closeFailure = rhi::FailureCode::None;
        jobs::ParallelTask closeTasks = jobs::ParallelTask::Create([this, firstIndex](const u32 localIndex, const jobs::JobContext&) noexcept
        {
            Entry& entry = m_commandLists[firstIndex + localIndex];
            if (entry.commandList.IsValid())
            {
                rhi::Failure closeFailure;
                if (!rhi::CloseCommandList(entry.commandList, &closeFailure))
                    entry.closeFailure = closeFailure.code == rhi::FailureCode::None ? rhi::FailureCode::BackendFailure : closeFailure.code;
            }
        });
        jobs::Task submissionTask = jobs::Task::Create([this, scopeName, firstIndex, upToIndex, sync](const jobs::JobContext&) noexcept
        {
            // Submission jobs are ordered; device-loss cleanup waits for all recording to join.
            if (HasFailure())
                return;
            if (m_nextFlushStart != firstIndex)
                VG_FATAL("render-frame submission boundaries must execute in their authored order");
            containers::DynamicArray<rhi::CommandListRef> submission{memory::pools::Rendering::GetInstance()};
            submission.Reserve(upToIndex - firstIndex + 1u);
            for (u32 index = firstIndex; index <= upToIndex; ++index)
                if (m_commandLists[index].commandList.IsValid())
                    submission.PushBack(m_commandLists[index].commandList);

            rhi::SubmissionReceipt submissionReceipt;
            rhi::Failure submissionFailure;
            bool submitted = true;
            for (u32 index = firstIndex; index <= upToIndex; ++index)
            {
                if (m_commandLists[index].closeFailure == rhi::FailureCode::None)
                    continue;
                submitted = false;
                submissionFailure.code = m_commandLists[index].closeFailure;
                CopyFailureMessage(submissionFailure.message, sizeof(submissionFailure.message), "render-frame command-list close failed");
                break;
            }
            u32 dependencyIndex = InvalidRenderFlowResourceIndex;
            bool dependencyMatches = true;
            for (u32 index = 0; index < m_expectedQueueDependencies.Size(); ++index)
            {
                const ExpectedQueueDependency& dependency = m_expectedQueueDependencies[index];
                if (dependency.producerIndex > upToIndex || upToIndex >= dependency.consumerIndex)
                    continue;
                if (dependencyIndex != InvalidRenderFlowResourceIndex || dependency.completionRecorded || dependency.sync != sync)
                    dependencyMatches = false;
                dependencyIndex = index;
            }
            if ((sync == rhi::CommandListSyncType::None) != (dependencyIndex == InvalidRenderFlowResourceIndex))
                dependencyMatches = false;

            if (submitted && !dependencyMatches)
            {
                submitted = false;
                submissionFailure.code = rhi::FailureCode::InvalidArgument;
                CopyFailureMessage(submissionFailure.message, sizeof(submissionFailure.message), "command-list synchronization does not match the compiled queue boundary");
            }
            else if (submitted && dependencyIndex != InvalidRenderFlowResourceIndex && submission.Empty())
            {
                submitted = false;
                submissionFailure.code = rhi::FailureCode::InvalidCommandList;
                CopyFailureMessage(submissionFailure.message, sizeof(submissionFailure.message), "cross-queue synchronization has no recorded producer command list");
            }
            else if (submitted && submission.Size() > rhi::MaximumCommandListsPerSubmission)
            {
                submitted = false;
                submissionFailure.code = rhi::FailureCode::CapacityExceeded;
                CopyFailureMessage(submissionFailure.message, sizeof(submissionFailure.message), "render-frame submission exceeds the RHI command-list limit");
            }
            else if (submitted && !submission.Empty())
            {
                submitted = rhi::SubmitCommandLists(scopeName, containers::ArraySpan<const rhi::CommandListRef>(submission), sync, submissionReceipt, &submissionFailure);
            }
            const bool workSubmitted = submissionReceipt.WasSubmitted();
            if (!submitted && !workSubmitted && submissionFailure.code != rhi::FailureCode::DeviceLost)
                VG_FATAL(submissionFailure.message[0] != '\0' ? submissionFailure.message : "required render-frame submission failed");
            if (!submitted && !HasFailure())
            {
                m_submissionFailure = submissionFailure;
                if (m_submissionFailure.message[0] == '\0')
                    CopyFailureMessage(m_submissionFailure.message, sizeof(m_submissionFailure.message), "render-frame command-list submission failed");
                m_submissionFailurePending = true;
            }
            m_deviceLost = m_deviceLost || (!submitted && (workSubmitted || submissionFailure.code == rhi::FailureCode::DeviceLost));

            for (u32 index = firstIndex; index <= upToIndex; ++index)
            {
                Entry& entry = m_commandLists[index];
                if (!entry.commandList.IsValid())
                    continue;
                if (!submitted && !workSubmitted)
                    rhi::DiscardCommandList(entry.commandList);
                else
                    entry.commandList = {};

                rhi::GpuFence fence;
                if (submitted)
                {
                    const u64 value = entry.queue == rhi::QueueType::Graphics ? submissionReceipt.residency.graphics :
                                      entry.queue == rhi::QueueType::Compute ? submissionReceipt.residency.compute : submissionReceipt.residency.copy;
                    fence = {entry.queue, value};
                }
                if (index == static_cast<u32>(ReservedFrameCommandList::StorageData))
                    continue;
                if (!entry.scope.IsValid())
                    continue;
                m_commandScopeReceipts.PushBack({entry.scope,
                                                 submitted ? CommandScopeCompletionKind::Submitted :
                                                             workSubmitted ? CommandScopeCompletionKind::UnknownDueToDeviceLoss :
                                                                             CommandScopeCompletionKind::DiscardedBeforeSubmission,
                                                 entry.queue, fence});
                entry.completionRecorded = true;
            }
            if (dependencyMatches && dependencyIndex != InvalidRenderFlowResourceIndex)
            {
                ExpectedQueueDependency& dependency = m_expectedQueueDependencies[dependencyIndex];
                m_queueDependencyReceipts.PushBack({dependency.producerScope, dependency.consumerScope, dependency.sync,
                                                    submitted ? QueueDependencyCompletionKind::Submitted :
                                                                workSubmitted ? QueueDependencyCompletionKind::UnknownDueToDeviceLoss :
                                                                                QueueDependencyCompletionKind::DiscardedBeforeSubmission});
                dependency.completionRecorded = true;
            }
            m_nextFlushStart = upToIndex + 1u;
        });
        if (!closeTasks || !submissionTask)
            return false;
        static jobs::JobName closeName{"RenderGraph/CloseAndSubmitCommandLists"};
        return builder.DispatchParallel(closeName, upToIndex - firstIndex + 1u, static_cast<jobs::ParallelTask&&>(closeTasks), static_cast<jobs::Task&&>(submissionTask));
    }


    void RenderFrameCommandLists::FinalizeSubmissions(const bool frameFailed) noexcept
    {
        const bool failureAlreadyKnown = frameFailed || HasFailure();
        bool foundUnsubmittedWork = false;
        for (Entry& entry : m_commandLists)
        {
            if (!entry.completionRecorded && entry.scope.IsValid())
            {
                m_commandScopeReceipts.PushBack({entry.scope, CommandScopeCompletionKind::DiscardedBeforeSubmission, entry.queue, {}});
                entry.completionRecorded = true;
                foundUnsubmittedWork = true;
            }
            if (entry.commandList.IsValid())
            {
                rhi::DiscardCommandList(entry.commandList);
                foundUnsubmittedWork = true;
            }
        }
        for (ExpectedQueueDependency& dependency : m_expectedQueueDependencies)
        {
            if (dependency.completionRecorded)
                continue;
            m_queueDependencyReceipts.PushBack({dependency.producerScope, dependency.consumerScope, dependency.sync, QueueDependencyCompletionKind::DiscardedBeforeSubmission});
            dependency.completionRecorded = true;
            foundUnsubmittedWork = true;
        }
        if (foundUnsubmittedWork && !failureAlreadyKnown)
        {
            m_submissionFailure = {};
            m_submissionFailure.code = rhi::FailureCode::InvalidCommandList;
            CopyFailureMessage(m_submissionFailure.message, sizeof(m_submissionFailure.message), "authored final synchronization node did not submit every recorded command scope");
            m_submissionFailurePending = true;
        }
    }

    bool RenderFrameCommandLists::GetFirstSubmissionFailure(rhi::Failure& failure) const noexcept
    {
        failure = m_submissionFailure;
        return HasFailure();
    }

    containers::ArraySpan<const CommandScopeExecutionReceipt> RenderFrameCommandLists::GetCommandScopeReceipts() const noexcept
    {
        return containers::ArraySpan<const CommandScopeExecutionReceipt>(m_commandScopeReceipts);
    }

    containers::ArraySpan<const QueueDependencyExecutionReceipt> RenderFrameCommandLists::GetQueueDependencyReceipts() const noexcept
    {
        return containers::ArraySpan<const QueueDependencyExecutionReceipt>(m_queueDependencyReceipts);
    }

    bool RenderFrameCommandLists::WasDeviceLost() const noexcept
    {
        return m_deviceLost;
    }

    void RenderFrameCommandLists::Reset() noexcept
    {
        for (Entry& entry : m_commandLists)
            if (entry.commandList.IsValid())
                rhi::DiscardCommandList(entry.commandList);
        m_commandLists.Clear();
        m_expectedQueueDependencies.Clear();
        m_commandScopeReceipts.Clear();
        m_queueDependencyReceipts.Clear();
        m_submissionFailure = {};
        m_nextFlushStart = 0;
        m_submissionFailurePending = false;
        m_deviceLost = false;
    }

    struct RetainedRenderFrameRef::Impl
    {
        explicit Impl(const RenderFrameInfo& source, RenderFrameOutputTransaction&& frameOutput, RenderCommandSystem& commandSystem) noexcept
            : frame(source), output(static_cast<RenderFrameOutputTransaction&&>(frameOutput)), commandSystem(&commandSystem), preparedViewFamilyRequired(source.GetViewFamily().IsValid())
        {
            if (frame.GetPayload().data != nullptr)
            {
                frame.GetPayload().retain(frame.GetPayload().data);
                payloadRetained = true;
            }
        }

        ~Impl()
        {
            if (jobsFrameInstalled.Exchange(false))
                RenderNodeJob::ClearJobsRenderFrame(&frame);
            if (payloadRetained)
                frame.GetPayload().release(frame.GetPayload().data);
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !preparedViewFamilyRequired || frame.GetViewFamily().IsValid();
        }

        concurrency::Atomic<u32> references{1};
        RenderFrameInfo frame;
        RenderFrameOutputTransaction output;
        PreparedRenderViewFamily preparedViewFamily;
        FrameCustomData frameCustomData;
        GeometryFrameWork geometryFrameWork;
        RenderFrameCommandLists frameCommandLists;
        RenderNodeResourceBindings resourceBindings;
        RenderNodeResourcePreparationFailures resourcePreparationFailures;
        RenderCommandSystem* commandSystem = nullptr;
        mutable concurrency::SpinLock failureLock;
        char failureMessage[MaximumExecutionFailureMessageBytes]{};
        concurrency::Atomic<bool> jobsFrameInstalled{false};
        concurrency::Atomic<bool> terminalCompletionPublished{false};
        bool payloadRetained = false;
        bool preparedViewFamilyRequired = false;
        concurrency::Atomic<bool> terminalCompletionDeferred{false};
        bool failurePending = false;
    };

    namespace detail
    {
        struct RenderCommandRetainedFrameAccess
        {
            [[nodiscard]] static RetainedRenderFrameRef Create(const RenderFrameInfo& source, RenderFrameOutputTransaction&& output, RenderCommandSystem& commandSystem) noexcept
            {
                memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(RetainedRenderFrameRef::Impl), alignof(RetainedRenderFrameRef::Impl));
                if (!block)
                    return {};
                return RetainedRenderFrameRef(new (block.address) RetainedRenderFrameRef::Impl(source, static_cast<RenderFrameOutputTransaction&&>(output), commandSystem));
            }

            static void TakeOutput(RetainedRenderFrameRef& frame, RenderFrameOutputTransaction& output) noexcept
            {
                if (frame.m_impl != nullptr)
                    output = static_cast<RenderFrameOutputTransaction&&>(frame.m_impl->output);
            }

            static void PublishTerminalCompletion(RetainedRenderFrameRef& frame, const bool skipped) noexcept
            {
                frame.PublishTerminalCompletion(skipped);
            }

            [[nodiscard]] static bool IsTerminalCompletionDeferred(const RetainedRenderFrameRef& frame) noexcept
            {
                return frame.m_impl != nullptr && frame.m_impl->terminalCompletionDeferred.GetValue();
            }
        };
    } // namespace detail

    RetainedRenderFrameRef::~RetainedRenderFrameRef()
    {
        Reset();
    }

    RetainedRenderFrameRef::RetainedRenderFrameRef(const RetainedRenderFrameRef& other) noexcept : m_impl(other.m_impl)
    {
        if (m_impl != nullptr)
            static_cast<void>(m_impl->references.Increment());
    }

    RetainedRenderFrameRef& RetainedRenderFrameRef::operator=(const RetainedRenderFrameRef& other) noexcept
    {
        if (this == &other)
            return *this;
        Impl* const replacement = other.m_impl;
        if (replacement != nullptr)
            static_cast<void>(replacement->references.Increment());
        Reset();
        m_impl = replacement;
        return *this;
    }

    RetainedRenderFrameRef::RetainedRenderFrameRef(RetainedRenderFrameRef&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    RetainedRenderFrameRef& RetainedRenderFrameRef::operator=(RetainedRenderFrameRef&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_impl = other.m_impl;
        other.m_impl = nullptr;
        return *this;
    }

    bool RetainedRenderFrameRef::IsValid() const noexcept
    {
        return m_impl != nullptr && m_impl->IsValid();
    }

    const RenderFrameInfo& RetainedRenderFrameRef::GetFrame() const noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        return m_impl->frame;
    }

    PreparedRenderViewFamily& RetainedRenderFrameRef::GetPreparedViewFamily() noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        return m_impl->preparedViewFamily;
    }

    FrameCustomData& RetainedRenderFrameRef::GetFrameCustomData() noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        return m_impl->frameCustomData;
    }

    GeometryFrameWork& RetainedRenderFrameRef::GetGeometryFrameWork() noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        return m_impl->geometryFrameWork;
    }

    RenderFrameCommandLists& RetainedRenderFrameRef::GetFrameCommandLists() noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        return m_impl->frameCommandLists;
    }

    RenderNodeResourceBindings& RetainedRenderFrameRef::GetResourceBindings() noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        return m_impl->resourceBindings;
    }

    RenderNodeResourcePreparationFailures& RetainedRenderFrameRef::GetResourcePreparationFailures() noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        return m_impl->resourcePreparationFailures;
    }

    RenderFrameOutputTransaction& RetainedRenderFrameRef::GetOutputTransaction() noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        return m_impl->output;
    }

    void RetainedRenderFrameRef::RecordFailure(const char* const message) noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->failureLock);
        if (m_impl->failurePending)
            return;
        CopyFailureMessage(m_impl->failureMessage, MaximumExecutionFailureMessageBytes, message);
        m_impl->failurePending = true;
    }

    void RetainedRenderFrameRef::RecordFailure(const RenderFlowResourceFailure& failure) noexcept
    {
        RecordFailure(failure.message != nullptr ? failure.message : "render-flow resource operation failed without a diagnostic");
    }

    bool RetainedRenderFrameRef::HasFailure() const noexcept
    {
        if (!IsValid())
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->failureLock);
        return m_impl->failurePending;
    }

    const char* RetainedRenderFrameRef::GetFailureMessage() const noexcept
    {
        if (!IsValid())
            return "retained render-frame reference is invalid";
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->failureLock);
        return m_impl->failurePending ? m_impl->failureMessage : nullptr;
    }

    void RetainedRenderFrameRef::InstallJobsRenderFrame() noexcept
    {
        if (!IsValid())
            VG_FATAL("retained render-frame reference is invalid");
        if (m_impl->jobsFrameInstalled.Exchange(true))
            VG_FATAL("retained render frame is already installed for render-node jobs");
        RenderNodeJob::SetJobsRenderFrame(&m_impl->frame);
    }

    void RetainedRenderFrameRef::ClearJobsRenderFrame() noexcept
    {
        if (m_impl == nullptr || !m_impl->jobsFrameInstalled.Exchange(false))
            return;
        RenderNodeJob::ClearJobsRenderFrame(&m_impl->frame);
    }

    void RetainedRenderFrameRef::PublishTerminalCompletion(const bool skipped) noexcept
    {
        if (m_impl == nullptr || m_impl->terminalCompletionPublished.Exchange(true))
            return;
        const char* const failure = GetFailureMessage();
        m_impl->commandSystem->PublishFrameCompletion(m_impl->frame.GetSerial(), failure != nullptr, skipped, failure);
    }

    void RetainedRenderFrameRef::DeferTerminalCompletion() noexcept
    {
        if (m_impl == nullptr)
            VG_FATAL("retained render-frame reference is invalid");
        m_impl->terminalCompletionDeferred.SetValue(true);
    }

    void RetainedRenderFrameRef::Reset() noexcept
    {
        Impl* const value = m_impl;
        m_impl = nullptr;
        if (value == nullptr || value->references.Decrement() != 0)
            return;
        value->~Impl();
        memory::MemoryBlock block{value, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
    }

    struct RenderCommandSystem::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        struct ExecutionFailureLatch
        {
            concurrency::SpinLock lock;
            RenderCommandExecutionStage stage = RenderCommandExecutionStage::None;
            u64 serial = 0;
            char message[MaximumExecutionFailureMessageBytes]{};
            bool pending = false;
        };

        void ReportExecutionFailure(const RenderCommandExecutionStage stage, const u64 serial, const char* const message) noexcept
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(executionFailure.lock);
            if (executionFailure.pending)
            {
                static_cast<void>(suppressedExecutionFailures.Increment());
                return;
            }
            executionFailure.stage = stage;
            executionFailure.serial = serial;
            CopyFailureMessage(executionFailure.message, MaximumExecutionFailureMessageBytes, message);
            executionFailure.pending = true;
        }

        [[nodiscard]] bool ConsumeExecutionFailure(RenderCommandFailure& failure) noexcept
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(executionFailure.lock);
            if (!executionFailure.pending)
                return false;
            failure = {};
            failure.code = RenderCommandFailureCode::ExecutionFailure;
            failure.executionStage = executionFailure.stage;
            failure.executionSerial = executionFailure.serial;
            failure.message = executionFailure.message;
            executionFailure.pending = false;
            executionFailure.stage = RenderCommandExecutionStage::None;
            executionFailure.serial = 0;
            return true;
        }

        [[nodiscard]] bool HasExecutionFailure() noexcept
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(executionFailure.lock);
            return executionFailure.pending;
        }

        struct RenderFrameDispatcher final
        {
            [[nodiscard]] bool Initialize(Impl& commandSystem, const ExecuteRenderingFrame callback, void* const callbackUserData) noexcept
            {
                if (execute != nullptr || callback == nullptr)
                    return false;
                owner = &commandSystem;
                execute = callback;
                userData = callbackUserData;
                return true;
            }

            [[nodiscard]] bool Submit(const RenderFrameInfo& frame, RenderFrameOutputTransaction& output, jobs::Counter& cpuTail, RenderFrameSubmission& submission,
                                      RenderCommandFailure* const failure) noexcept
            {
                if (execute == nullptr)
                    return Fail(failure, RenderCommandFailureCode::InvalidState, "RenderFrameDispatcher is not initialized");
                if (!concurrency::IsMainThread())
                    return Fail(failure, RenderCommandFailureCode::WrongThread, "render frames must be submitted from the main thread");
                if (frame.GetSerial() == 0 || !frame.GetEngineViewport().IsValid() || frame.GetViewport() == nullptr || !frame.GetViewport()->IsValid() ||
                    !frame.GetRenderExtent().IsValid() || !frame.GetPayload().IsValid())
                    return Fail(failure, RenderCommandFailureCode::InvalidDescriptor, "render frame packet is invalid");
                if (frame.HasViewSetup())
                {
                    const RenderFrameViewSetup setup = frame.GetViewSetup();
                    if (!setup.scene.IsValid() || setup.rootCameras.Empty() || setup.rootCameras.Size() > MaximumRenderViewsPerFamily ||
                        frame.GetViewFamily().IsValid())
                        return Fail(failure, RenderCommandFailureCode::InvalidDescriptor, "render frame view setup is invalid");
                    for (u32 index = 0; index < setup.rootCameras.Size(); ++index)
                    {
                        const RenderCameraHandle camera = setup.rootCameras[index];
                        if (!camera.IsValid() || camera.scene != setup.scene)
                            return Fail(failure, RenderCommandFailureCode::InvalidDescriptor,
                                        "render frame root camera is invalid or belongs to another scene");
                        for (u32 previous = 0; previous < index; ++previous)
                            if (setup.rootCameras[previous] == camera)
                                return Fail(failure, RenderCommandFailureCode::InvalidDescriptor, "render frame contains a duplicate root camera");
                    }
                }

                const bool requiresOutput = frame.GetOutputKind() == RenderViewportOutputKind::Texture ||
                                            (frame.GetOutputKind() == RenderViewportOutputKind::Presentation && frame.ShouldPresent());
                if (requiresOutput != output.IsValid() || (requiresOutput && output.GetKind() != frame.GetOutputKind()))
                    return Fail(failure, RenderCommandFailureCode::InvalidDescriptor, "render frame output transaction does not match the frame packet");

                RetainedRenderFrameRef retained = detail::RenderCommandRetainedFrameAccess::Create(frame, static_cast<RenderFrameOutputTransaction&&>(output), *owner->commandSystem);
                if (!retained.IsValid())
                    return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "retained render-frame allocation failed");
                jobs::Task task = jobs::Task::Create(
                    [dispatcher = this, retained](const jobs::JobContext& continuation) mutable noexcept
                    {
                        RenderFrameContext context(retained, continuation);
                        RenderFrameExecutionStatus status =
                            context.IsValid() ? dispatcher->execute(context, dispatcher->userData)
                                              : RenderFrameExecutionStatus::Failure("renderer continuation builder creation failed");
                        if (status.IsFailure())
                        {
                            retained.RecordFailure(status.message);
                            detail::RenderCommandRetainedFrameAccess::PublishTerminalCompletion(retained, false);
                        }
                        else if (status.IsSkipped())
                            detail::RenderCommandRetainedFrameAccess::PublishTerminalCompletion(retained, true);
                        else if (!detail::RenderCommandRetainedFrameAccess::IsTerminalCompletionDeferred(retained))
                            detail::RenderCommandRetainedFrameAccess::PublishTerminalCompletion(retained, false);
                    });
                if (!task)
                {
                    detail::RenderCommandRetainedFrameAccess::TakeOutput(retained, output);
                    return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "render frame job allocation failed");
                }

                jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker}, this);
                if (!builder.IsValid())
                {
                    detail::RenderCommandRetainedFrameAccess::TakeOutput(retained, output);
                    return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "render frame job builder creation failed");
                }
                if (cpuTail.IsValid())
                    builder.AddDependency(cpuTail);
                static_cast<void>(submittedFrames.Increment());
                if (!builder.Dispatch(jobName, std::move(task)))
                {
                    static_cast<void>(submittedFrames.Decrement());
                    detail::RenderCommandRetainedFrameAccess::TakeOutput(retained, output);
                    return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "render frame job dispatch failed");
                }

                jobs::Counter nextTail = builder.ExtractCounter();
                if (!nextTail.IsValid())
                {
                    if (!builder.WaitForCompletion())
                        VG_FATAL("accepted render frame could not establish its completion boundary");
                    // Dispatch accepted ownership. A synchronous join must not make this frame retryable.
                    cpuTail = {};
                    submission.serial = frame.GetSerial();
                    return true;
                }
                cpuTail = std::move(nextTail);
                submission.serial = frame.GetSerial();
                return true;
            }

            Impl* owner = nullptr;
            ExecuteRenderingFrame execute = nullptr;
            void* userData = nullptr;
            jobs::JobName jobName{"RenderCommands.RenderFrame"};
            concurrency::Atomic<u64> submittedFrames{0};
        };

        Impl() noexcept : sceneStamps(memory::pools::Rendering::GetInstance()), frameScenes(memory::pools::Rendering::GetInstance()) {}

        [[nodiscard]] bool StorageReady() const noexcept
        {
            return scenes != nullptr && sceneStamps.Size() == scenes->GetStats().capacity && frameScenes.Size() == scenes->GetStats().capacity;
        }

        void AdoptTail(jobs::Builder& builder) noexcept
        {
            cpuTail = builder.ExtractCounter();
        }

        RenderSceneManager* scenes = nullptr;
        RenderCameraStorage* cameras = nullptr;
        RenderCommandSystem* commandSystem = nullptr;
        ExecuteRenderingFrameTick executeFrameTick = nullptr;
        void* userData = nullptr;
        jobs::JobName frameTickJobName{"RenderCommands.FrameTick"};
        RenderFrameDispatcher frameDispatcher;
        jobs::Counter cpuTail;
        ExecutionFailureLatch executionFailure;
        containers::DynamicArray<u32> sceneStamps;
        containers::DynamicArray<RenderSceneProcessingEpoch> frameScenes;
        u32 frameSceneCount = 0;
        u32 sceneStamp = 0;
        concurrency::Atomic<u64> frameTicks{0};
        concurrency::Atomic<u64> completedFrameTicks{0};
        concurrency::Atomic<u64> failedFrameTicks{0};
        concurrency::Atomic<u64> completedFrames{0};
        concurrency::Atomic<u64> failedFrames{0};
        concurrency::Atomic<u64> skippedFrames{0};
        concurrency::Atomic<u64> lastCompletedFrameSerial{0};
        concurrency::Atomic<u64> suppressedExecutionFailures{0};
        u64 explicitFlushes = 0;
        u64 rejectedOperations = 0;
        bool initialized = false;
    };

    RenderCommandSystem::~RenderCommandSystem()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool RenderCommandSystem::Initialize(RenderSceneManager& scenes, RenderCameraStorage& cameras, const RenderCommandSystemConfig& config,
                                         RenderCommandFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, RenderCommandFailureCode::AlreadyInitialized, "RenderCommandSystem is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCommandFailureCode::WrongThread, "RenderCommandSystem must initialize on the main thread");
        if (!scenes.IsInitialized() || !cameras.IsInitialized() || !jobs::IsInitialized() || !config.IsValid())
            return Fail(failure, RenderCommandFailureCode::InvalidDescriptor,
                        "RenderCommandSystem requires initialized Jobs, RenderScene, RenderCamera, and execution callbacks");

        Impl* const impl = VANGUARD_NEW(Impl);
        if (impl != nullptr)
        {
            impl->scenes = &scenes;
            impl->cameras = &cameras;
            impl->commandSystem = this;
            impl->sceneStamps.Resize(scenes.GetStats().capacity);
            impl->frameScenes.Resize(scenes.GetStats().capacity);
        }
        if (impl == nullptr || !impl->StorageReady())
        {
            if (impl != nullptr)
                VANGUARD_DELETE(impl);
            return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "RenderCommandSystem direct scene directory allocation failed");
        }
        impl->executeFrameTick = config.executeFrameTick;
        impl->userData = config.userData;
        if (!impl->frameDispatcher.Initialize(*impl, config.executeFrame, config.userData))
        {
            VANGUARD_DELETE(impl);
            return Fail(failure, RenderCommandFailureCode::InvalidDescriptor, "RenderFrameDispatcher initialization failed");
        }
        impl->initialized = true;
        m_impl = impl;
        return true;
    }

    bool RenderCommandSystem::Shutdown(RenderCommandFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderCommandFailureCode::WrongThread, "RenderCommandSystem must shut down on the main thread");
        if (!FlushPreviousFrameProcessing(failure))
            return false;
        m_impl->initialized = false;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool RenderCommandSystem::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_impl->initialized;
    }

    bool RenderCommandSystem::RegisterCamera(const RenderCameraDesc& desc, RenderCameraHandle& camera, RenderCameraFailure* const failure) noexcept
    {
        RenderCommandFailure commandFailure;
        if (!FlushPreviousFrameProcessing(&commandFailure))
            return CameraBoundaryFailure(failure, commandFailure, desc.scene);
        return m_impl->cameras->RegisterCamera(desc, camera, failure);
    }

    bool RenderCommandSystem::UnregisterCamera(const RenderCameraHandle camera, RenderCameraFailure* const failure) noexcept
    {
        RenderCommandFailure commandFailure;
        if (!FlushPreviousFrameProcessing(&commandFailure))
            return CameraBoundaryFailure(failure, commandFailure, camera.scene, camera);
        return m_impl->cameras->UnregisterCamera(camera, failure);
    }

    bool RenderCommandSystem::UpdateCamera(const RenderCameraHandle camera, const RenderCameraState& state, RenderCameraFailure* const failure) noexcept
    {
        RenderCommandFailure commandFailure;
        if (!FlushPreviousFrameProcessing(&commandFailure))
            return CameraBoundaryFailure(failure, commandFailure, camera.scene, camera);
        return m_impl->cameras->UpdateCamera(camera, state, failure);
    }

    bool RenderCommandSystem::RequestCameraCut(const RenderCameraHandle camera, RenderCameraFailure* const failure) noexcept
    {
        RenderCommandFailure commandFailure;
        if (!FlushPreviousFrameProcessing(&commandFailure))
            return CameraBoundaryFailure(failure, commandFailure, camera.scene, camera);
        return m_impl->cameras->RequestCameraCut(camera, failure);
    }

    bool RenderCommandSystem::SetCameraEnabled(const RenderCameraHandle camera, const bool enabled, RenderCameraFailure* const failure) noexcept
    {
        RenderCommandFailure commandFailure;
        if (!FlushPreviousFrameProcessing(&commandFailure))
            return CameraBoundaryFailure(failure, commandFailure, camera.scene, camera);
        return m_impl->cameras->SetEnabled(camera, enabled, failure);
    }

    bool RenderCommandSystem::SetCameraRenderPolicy(const RenderCameraHandle camera, const RenderCameraRenderPolicy policy,
                                                    RenderCameraFailure* const failure) noexcept
    {
        RenderCommandFailure commandFailure;
        if (!FlushPreviousFrameProcessing(&commandFailure))
            return CameraBoundaryFailure(failure, commandFailure, camera.scene, camera);
        return m_impl->cameras->SetRenderPolicy(camera, policy, failure);
    }

    bool RenderCommandSystem::AddCameraDependency(const RenderCameraHandle parent, const RenderCameraHandle child, const RenderCameraDependencyOutputs outputs,
                                                  RenderCameraFailure* const failure) noexcept
    {
        RenderCommandFailure commandFailure;
        if (!FlushPreviousFrameProcessing(&commandFailure))
            return CameraBoundaryFailure(failure, commandFailure, parent.scene, parent, child);
        return m_impl->cameras->AddDependency(parent, child, outputs, failure);
    }

    bool RenderCommandSystem::RemoveCameraDependency(const RenderCameraHandle parent, const RenderCameraHandle child,
                                                     RenderCameraFailure* const failure) noexcept
    {
        RenderCommandFailure commandFailure;
        if (!FlushPreviousFrameProcessing(&commandFailure))
            return CameraBoundaryFailure(failure, commandFailure, parent.scene, parent, child);
        return m_impl->cameras->RemoveDependency(parent, child, failure);
    }

    bool RenderCommandSystem::FrameTick(const containers::ArraySpan<const RenderSceneHandle> scenes, const u64 tickCounter,
                                        RenderCommandFrameTickResult& result, RenderCommandFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        result.tick = tickCounter;
        if (!IsInitialized())
            return Fail(failure, RenderCommandFailureCode::NotInitialized, "RenderCommandSystem is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->rejectedOperations;
            return Fail(failure, RenderCommandFailureCode::WrongThread, "render command FrameTick must run on the main thread");
        }
        if (tickCounter == 0)
        {
            ++m_impl->rejectedOperations;
            return Fail(failure, RenderCommandFailureCode::InvalidDescriptor, "render command FrameTick requires a non-zero tick");
        }
        if (m_impl->cpuTail.IsValid())
        {
            ++m_impl->rejectedOperations;
            return Fail(failure, RenderCommandFailureCode::InvalidState, "previous CPU rendering must be flushed before render command FrameTick");
        }

        u32 stamp = ++m_impl->sceneStamp;
        if (stamp == 0)
        {
            for (u32 index = 0; index < m_impl->sceneStamps.Size(); ++index)
                m_impl->sceneStamps[index] = 0;
            stamp = ++m_impl->sceneStamp;
        }
        for (const RenderSceneHandle scene : scenes)
        {
            if (!scene.IsValid() || !m_impl->scenes->IsAlive(scene))
            {
                ++m_impl->rejectedOperations;
                return Fail(failure, RenderCommandFailureCode::InvalidDescriptor, "render command FrameTick contains an invalid or non-alive scene", {scene});
            }
            if (scene.index >= m_impl->sceneStamps.Size())
            {
                ++m_impl->rejectedOperations;
                return Fail(failure, RenderCommandFailureCode::InvalidDescriptor, "render command FrameTick scene exceeds the direct scene directory", {scene});
            }
            if (m_impl->sceneStamps[scene.index] == stamp)
            {
                ++m_impl->rejectedOperations;
                return Fail(failure, RenderCommandFailureCode::DuplicateScene, "render command FrameTick contains a duplicate scene", {scene});
            }
            m_impl->sceneStamps[scene.index] = stamp;
        }

        jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker}, this);
        if (!builder.IsValid())
            return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "render command FrameTick builder creation failed");
        m_impl->frameSceneCount = 0;
        for (const RenderSceneHandle scene : scenes)
        {
            RenderSceneFramePrepareResult prepare;
            RenderSceneFailure sceneFailure;
            if (!m_impl->scenes->PrepareSceneUpdate(scene, tickCounter, prepare, &sceneFailure))
            {
                if (result.asynchronousScenes != 0)
                    m_impl->AdoptTail(builder);
                return Fail(failure, RenderCommandFailureCode::SceneFailure, "RenderScene update preparation failed", {scene, &sceneFailure});
            }
            RenderSceneUpdateResult update;
            if (!m_impl->scenes->ExecuteSceneUpdate(scene, builder, update, &sceneFailure))
            {
                if (result.asynchronousScenes != 0)
                    m_impl->AdoptTail(builder);
                return Fail(failure, RenderCommandFailureCode::SceneFailure, "RenderScene update dispatch failed", {scene, &sceneFailure});
            }
            ++result.submittedScenes;
            result.asynchronousScenes += static_cast<u32>(update.dispatched);
            m_impl->frameScenes[m_impl->frameSceneCount++] = {scene, update.mutationEpoch};
        }

        jobs::Task frameTickTask = jobs::Task::Create(
            [impl = m_impl, tickCounter](const jobs::JobContext& context) noexcept
            {
                RenderFrameTickContext tickContext(tickCounter, {impl->frameScenes.TypedData(), impl->frameSceneCount}, context);
                const RenderFrameExecutionStatus status = tickContext.IsValid()
                                                              ? impl->executeFrameTick(tickContext, impl->userData)
                                                              : RenderFrameExecutionStatus::Failure("renderer FrameTick continuation builder creation failed");
                if (!status)
                {
                    impl->ReportExecutionFailure(RenderCommandExecutionStage::FrameTick, tickCounter, status.message);
                    static_cast<void>(impl->failedFrameTicks.Increment());
                }
                static_cast<void>(impl->completedFrameTicks.Increment());
            });
        if (!frameTickTask || !builder.Dispatch(m_impl->frameTickJobName, std::move(frameTickTask)))
        {
            if (result.asynchronousScenes != 0)
                m_impl->AdoptTail(builder);
            return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "renderer FrameTick dispatch failed");
        }

        m_impl->AdoptTail(builder);
        if (!m_impl->cpuTail.IsValid())
            return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "renderer FrameTick completion extraction failed");
        static_cast<void>(m_impl->frameTicks.Increment());
        return true;
    }

    bool RenderCommandSystem::RenderFrame(const RenderFrameInfo& frame, RenderFrameSubmission& submission, RenderCommandFailure* const failure) noexcept
    {
        RenderFrameOutputTransaction output;
        return RenderFrame(frame, output, submission, failure);
    }

    bool RenderCommandSystem::RenderFrame(const RenderFrameInfo& frame, RenderFrameOutputTransaction& output, RenderFrameSubmission& submission,
                                          RenderCommandFailure* const failure) noexcept
    {
        ClearFailure(failure);
        submission = {};
        if (!IsInitialized())
            return Fail(failure, RenderCommandFailureCode::NotInitialized, "RenderCommandSystem is not initialized");
        const bool submitted = m_impl->frameDispatcher.Submit(frame, output, m_impl->cpuTail, submission, failure);
        if (!submitted)
            ++m_impl->rejectedOperations;
        return submitted;
    }

    bool RenderCommandSystem::FlushPreviousFrameProcessing(RenderCommandFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, RenderCommandFailureCode::NotInitialized, "RenderCommandSystem is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->rejectedOperations;
            return Fail(failure, RenderCommandFailureCode::WrongThread, "previous render frame processing must be flushed on the main thread");
        }
        if (!m_impl->cpuTail.IsValid())
            return true;
        if (!m_impl->cpuTail.WaitOnProcessFrame())
            return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "previous CPU rendering chain flush failed");
        m_impl->cpuTail = {};
        ++m_impl->explicitFlushes;
        return true;
    }

    bool RenderCommandSystem::ConsumeExecutionFailure(RenderCommandFailure& failure) noexcept
    {
        failure = {};
        if (!IsInitialized() || !concurrency::IsMainThread() || m_impl->cpuTail.IsValid())
            return false;
        return m_impl->ConsumeExecutionFailure(failure);
    }

    bool RenderCommandSystem::IsIdle() const noexcept
    {
        return IsInitialized() && (!m_impl->cpuTail.IsValid() || m_impl->cpuTail.IsReady());
    }

    void RenderCommandSystem::PublishFrameCompletion(const u64 serial, const bool failed, const bool skipped, const char* const message) noexcept
    {
        if (m_impl == nullptr)
            return;
        if (failed)
        {
            m_impl->ReportExecutionFailure(RenderCommandExecutionStage::RenderFrame, serial, message);
            static_cast<void>(m_impl->failedFrames.Increment());
        }
        else if (skipped)
            static_cast<void>(m_impl->skippedFrames.Increment());
        static_cast<void>(m_impl->completedFrames.Increment());
        m_impl->lastCompletedFrameSerial.SetValue(serial);
    }

    RenderCommandSystemStats RenderCommandSystem::GetStats() const noexcept
    {
        RenderCommandSystemStats stats;
        if (!IsInitialized())
            return stats;
        stats.frameTicks = m_impl->frameTicks.GetValue();
        stats.completedFrameTicks = m_impl->completedFrameTicks.GetValue();
        stats.failedFrameTicks = m_impl->failedFrameTicks.GetValue();
        stats.submittedFrames = m_impl->frameDispatcher.submittedFrames.GetValue();
        stats.completedFrames = m_impl->completedFrames.GetValue();
        stats.failedFrames = m_impl->failedFrames.GetValue();
        stats.skippedFrames = m_impl->skippedFrames.GetValue();
        stats.lastCompletedFrameSerial = m_impl->lastCompletedFrameSerial.GetValue();
        stats.explicitFlushes = m_impl->explicitFlushes;
        stats.suppressedExecutionFailures = m_impl->suppressedExecutionFailures.GetValue();
        stats.rejectedOperations = m_impl->rejectedOperations;
        stats.initialized = true;
        stats.executionFailurePending = m_impl->HasExecutionFailure();
        stats.workOutstanding = m_impl->cpuTail.IsValid() && !m_impl->cpuTail.IsReady();
        return stats;
    }
} // namespace vanguard::rendering

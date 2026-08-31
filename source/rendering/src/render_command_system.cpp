#include <vanguard/rendering/render_command_system.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/pool.hpp>

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

        class RetainedFrame final
        {
        public:
            explicit RetainedFrame(const RenderFrameInfo& source) noexcept : m_frame(source), m_preparedViewFamilyRequired(source.GetViewFamily().IsValid())
            {
                if (m_frame.GetPayload().data != nullptr)
                {
                    m_frame.GetPayload().retain(m_frame.GetPayload().data);
                    m_payloadRetained = true;
                }
            }

            RetainedFrame(RetainedFrame&& other) noexcept
                : m_frame(std::move(other.m_frame)), m_payloadRetained(other.m_payloadRetained),
                  m_preparedViewFamilyRequired(other.m_preparedViewFamilyRequired)
            {
                other.m_payloadRetained = false;
                other.m_preparedViewFamilyRequired = false;
            }

            RetainedFrame(const RetainedFrame&) = delete;
            RetainedFrame& operator=(const RetainedFrame&) = delete;
            RetainedFrame& operator=(RetainedFrame&&) = delete;

            ~RetainedFrame()
            {
                Release();
            }

            [[nodiscard]] RenderFrameInfo& GetFrame() noexcept
            {
                return m_frame;
            }

            [[nodiscard]] bool IsValid() const noexcept
            {
                return !m_preparedViewFamilyRequired || m_frame.GetViewFamily().IsValid();
            }

        private:
            void Release() noexcept
            {
                if (m_payloadRetained)
                {
                    m_frame.GetPayload().release(m_frame.GetPayload().data);
                    m_payloadRetained = false;
                }
            }

            RenderFrameInfo m_frame;
            bool m_payloadRetained = false;
            bool m_preparedViewFamilyRequired = false;
        };
    } // namespace

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

            [[nodiscard]] bool Submit(const RenderFrameInfo& frame, jobs::Counter& cpuTail, RenderFrameSubmission& submission,
                                      RenderCommandFailure* const failure) noexcept
            {
                if (execute == nullptr)
                    return Fail(failure, RenderCommandFailureCode::InvalidState, "RenderFrameDispatcher is not initialized");
                if (!concurrency::IsMainThread())
                    return Fail(failure, RenderCommandFailureCode::WrongThread, "render frames must be submitted from the main thread");
                if (frame.GetSerial() == 0 || !frame.GetEngineViewport().IsValid() || !frame.GetOutputViewport().IsValid() || !frame.GetRenderExtent().IsValid() ||
                    !frame.GetPayload().IsValid())
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

                RetainedFrame retained(frame);
                if (!retained.IsValid())
                    return Fail(failure, RenderCommandFailureCode::InvalidDescriptor, "prepared RenderViewFamily could not be retained");
                jobs::Task task = jobs::Task::Create(
                    [dispatcher = this, retained = std::move(retained)](const jobs::JobContext& continuation) mutable noexcept
                    {
                        const RenderFrameInfo& retainedFrame = retained.GetFrame();
                        RenderFrameContext context(retainedFrame, continuation);
                        RenderFrameExecutionStatus status =
                            context.IsValid() ? dispatcher->execute(context, dispatcher->userData)
                                              : RenderFrameExecutionStatus::Failure("renderer continuation builder creation failed");
                        if (status.IsFailure())
                        {
                            dispatcher->owner->ReportExecutionFailure(RenderCommandExecutionStage::RenderFrame, retainedFrame.GetSerial(), status.message);
                            static_cast<void>(dispatcher->failedFrames.Increment());
                        }
                        else if (status.IsSkipped())
                            static_cast<void>(dispatcher->skippedFrames.Increment());
                        static_cast<void>(dispatcher->completedFrames.Increment());
                        dispatcher->lastCompletedSerial.SetValue(retainedFrame.GetSerial());
                    });
                if (!task)
                    return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "render frame job allocation failed");

                jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker}, this);
                if (!builder.IsValid())
                    return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "render frame job builder creation failed");
                if (cpuTail.IsValid())
                    builder.AddDependency(cpuTail);
                if (!builder.Dispatch(jobName, std::move(task)))
                    return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "render frame job dispatch failed");

                jobs::Counter nextTail = builder.ExtractCounter();
                if (!nextTail.IsValid())
                    return Fail(failure, RenderCommandFailureCode::SubmissionFailure, "render frame completion extraction failed");
                cpuTail = std::move(nextTail);
                static_cast<void>(submittedFrames.Increment());
                submission.serial = frame.GetSerial();
                return true;
            }

            Impl* owner = nullptr;
            ExecuteRenderingFrame execute = nullptr;
            void* userData = nullptr;
            jobs::JobName jobName{"RenderCommands.RenderFrame"};
            concurrency::Atomic<u64> submittedFrames{0};
            concurrency::Atomic<u64> completedFrames{0};
            concurrency::Atomic<u64> failedFrames{0};
            concurrency::Atomic<u64> skippedFrames{0};
            concurrency::Atomic<u64> lastCompletedSerial{0};
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
        ClearFailure(failure);
        submission = {};
        if (!IsInitialized())
            return Fail(failure, RenderCommandFailureCode::NotInitialized, "RenderCommandSystem is not initialized");
        const bool submitted = m_impl->frameDispatcher.Submit(frame, m_impl->cpuTail, submission, failure);
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

    RenderCommandSystemStats RenderCommandSystem::GetStats() const noexcept
    {
        RenderCommandSystemStats stats;
        if (!IsInitialized())
            return stats;
        stats.frameTicks = m_impl->frameTicks.GetValue();
        stats.completedFrameTicks = m_impl->completedFrameTicks.GetValue();
        stats.failedFrameTicks = m_impl->failedFrameTicks.GetValue();
        stats.submittedFrames = m_impl->frameDispatcher.submittedFrames.GetValue();
        stats.completedFrames = m_impl->frameDispatcher.completedFrames.GetValue();
        stats.failedFrames = m_impl->frameDispatcher.failedFrames.GetValue();
        stats.skippedFrames = m_impl->frameDispatcher.skippedFrames.GetValue();
        stats.lastCompletedFrameSerial = m_impl->frameDispatcher.lastCompletedSerial.GetValue();
        stats.explicitFlushes = m_impl->explicitFlushes;
        stats.suppressedExecutionFailures = m_impl->suppressedExecutionFailures.GetValue();
        stats.rejectedOperations = m_impl->rejectedOperations;
        stats.initialized = true;
        stats.executionFailurePending = m_impl->HasExecutionFailure();
        stats.workOutstanding = m_impl->cpuTail.IsValid() && !m_impl->cpuTail.IsReady();
        return stats;
    }
} // namespace vanguard::rendering

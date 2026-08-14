#include <vanguard/jobs/jobs.hpp>
#include <vanguard/system/assert.hpp>

#include <vanguard/jobs/jobs_backend.hpp>

namespace vanguard::jobs
{
    Config RuntimeConfig() noexcept
    {
        return backend::RuntimeConfig();
    }

    Config EditorConfig() noexcept
    {
        return backend::EditorConfig();
    }

    Config ToolConfig() noexcept
    {
        return backend::ToolConfig();
    }

    bool Initialize(const Config& config) noexcept
    {
        if (!memory::IsInitialized() && !memory::Initialize())
        {
            return false;
        }
        return backend::Initialize(config);
    }

    bool Shutdown() noexcept
    {
        return backend::Shutdown();
    }

    bool IsInitialized() noexcept
    {
        return backend::IsInitialized();
    }

    u32 WorkerCount() noexcept
    {
        return backend::WorkerCount();
    }

    u32 OutstandingJobCount() noexcept
    {
        return backend::OutstandingJobCount();
    }

    SchedulerStats GetSchedulerStats() noexcept
    {
        return backend::GetSchedulerStats();
    }

    u32 DispatcherThreadIndex() noexcept
    {
        return backend::DispatcherThreadIndex();
    }

    bool IsWorkerThread() noexcept
    {
        return backend::IsWorkerThread();
    }

    void RegisterCurrentThread(const char* name) noexcept
    {
        backend::RegisterCurrentThread(name);
    }

    CompletionDeferral::CompletionDeferral(void* backendPointer) noexcept : m_backend(backendPointer) {}

    CompletionDeferral::~CompletionDeferral()
    {
        backend::DestroyDeferral(m_backend);
    }

    CompletionDeferral::CompletionDeferral(CompletionDeferral&& other) noexcept : m_backend(other.m_backend)
    {
        other.m_backend = nullptr;
    }

    CompletionDeferral& CompletionDeferral::operator=(CompletionDeferral&& other) noexcept
    {
        if (this != &other)
        {
            backend::DestroyDeferral(m_backend);
            m_backend = other.m_backend;
            other.m_backend = nullptr;
        }
        return *this;
    }

    bool CompletionDeferral::IsValid() const noexcept
    {
        return m_backend != nullptr;
    }

    bool CompletionDeferral::IsFinished() const noexcept
    {
        return m_backend != nullptr && backend::DeferralIsFinished(m_backend);
    }

    void CompletionDeferral::Finish() noexcept
    {
        VG_ASSERT_MSG(m_backend != nullptr, "Cannot finish an invalid jobs::CompletionDeferral.");
        if (m_backend != nullptr)
        {
            backend::FinishDeferral(m_backend);
        }
    }

    JobName::JobName(const char* staticName) noexcept
    {
        backend::ConstructJobName(m_storage, staticName);
    }

    JobName::~JobName()
    {
        backend::DestroyJobName(m_storage);
    }

    const char* JobName::Get() const noexcept
    {
        return backend::GetJobName(m_storage);
    }

    Counter::Counter(void* backendPointer) noexcept : m_backend(backendPointer) {}

    Counter::~Counter()
    {
        backend::DestroyCounter(m_backend);
    }

    Counter::Counter(Counter&& other) noexcept : m_backend(other.m_backend)
    {
        other.m_backend = nullptr;
    }

    Counter& Counter::operator=(Counter&& other) noexcept
    {
        if (this != &other)
        {
            backend::DestroyCounter(m_backend);
            m_backend = other.m_backend;
            other.m_backend = nullptr;
        }
        return *this;
    }

    bool Counter::IsValid() const noexcept
    {
        return m_backend != nullptr;
    }

    bool Counter::IsReady() const noexcept
    {
        return m_backend != nullptr && backend::CounterIsReady(m_backend);
    }

    bool Counter::Wait(const bool processLatent, const i32 timeoutMilliseconds) const noexcept
    {
        return m_backend != nullptr && backend::WaitCounter(m_backend, processLatent, timeoutMilliseconds);
    }

    CompletionDeferral Counter::CreateDeferral(const char* staticDebugName, const void* debugUserData) noexcept
    {
        return CompletionDeferral{m_backend != nullptr ? backend::CreateDeferral(m_backend, staticDebugName, debugUserData) : nullptr};
    }

    void Counter::Analyze() const noexcept
    {
        if (m_backend != nullptr)
        {
            backend::AnalyzeCounter(m_backend);
        }
    }

    Builder::Builder(const Schedule schedule, const void* debugUserData) noexcept
        : m_backend(backend::CreateBuilder(schedule, debugUserData))
    {
    }

    Builder::Builder(const JobContext& continuationContext) noexcept : m_backend(backend::CreateContinuationBuilder(continuationContext)) {}

    Builder::~Builder()
    {
        VG_ASSERT_MSG(!m_hasOpenFenceGroup, "A jobs::Builder with Fence::None work must call "
                                            "DispatchFence() before destruction.");
        backend::DestroyBuilder(m_backend);
    }

    bool Builder::IsValid() const noexcept
    {
        return m_backend != nullptr;
    }

    bool Builder::Dispatch(JobName& name, Task&& task, const Fence fence) noexcept
    {
        if (m_backend == nullptr || !task)
        {
            return false;
        }
        VG_ASSERT_MSG(fence != Fence::Full || !m_hasOpenFenceGroup, "DispatchFence() is required before a Fence::Full dispatch.");

        const bool dispatched = backend::Dispatch(m_backend, name.m_storage, task.Release(), fence);
        if (dispatched)
        {
            m_hasOpenFenceGroup = fence == Fence::None;
        }
        return dispatched;
    }

    bool Builder::DispatchAfter(const Counter& dependency, JobName& name, Task&& task, const Fence fence) noexcept
    {
        if (m_backend == nullptr || !dependency.IsValid() || !task)
        {
            return false;
        }
        VG_ASSERT_MSG(fence != Fence::Full || !m_hasOpenFenceGroup, "DispatchFence() is required before a Fence::Full dispatch.");

        const bool dispatched = backend::DispatchAfter(m_backend, dependency.m_backend, name.m_storage, task.Release(), fence);
        if (dispatched)
        {
            m_hasOpenFenceGroup = fence == Fence::None;
        }
        return dispatched;
    }

    bool Builder::DispatchParallel(JobName& name, const u32 elementCount, ParallelTask&& task, Task&& epilogue, const u32 maximumBatchSize,
                                   const Fence fence) noexcept
    {
        if (m_backend == nullptr || !task)
        {
            return false;
        }
        VG_ASSERT_MSG(fence != Fence::Full || !m_hasOpenFenceGroup, "DispatchFence() is required before a Fence::Full dispatch.");

        const bool dispatched =
            backend::DispatchParallel(m_backend, name.m_storage, elementCount, task.Release(), epilogue.Release(), maximumBatchSize, fence);
        if (dispatched)
        {
            m_hasOpenFenceGroup = fence == Fence::None;
        }
        return dispatched;
    }

    void Builder::AddDependency(const Counter& dependency) noexcept
    {
        if (m_backend != nullptr && dependency.IsValid())
        {
            VG_ASSERT_MSG(!m_hasOpenFenceGroup, "DispatchFence() is required before AddDependency().");
            backend::AddDependency(m_backend, dependency.m_backend);
        }
    }

    void Builder::DispatchFence() noexcept
    {
        if (m_backend != nullptr)
        {
            backend::DispatchFence(m_backend);
            m_hasOpenFenceGroup = false;
        }
    }

    Counter Builder::ExtractCounter() noexcept
    {
        VG_ASSERT_MSG(!m_hasOpenFenceGroup, "DispatchFence() is required before ExtractCounter().");
        return Counter{m_backend != nullptr ? backend::ExtractCounter(m_backend) : nullptr};
    }
} // namespace vanguard::jobs

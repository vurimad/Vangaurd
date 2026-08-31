#include <vanguard/jobs/jobs_backend.hpp>

#include "../../imported/common/redJobs2/include/redJobs2Public.h"
#include "../../imported/common/redCore/include/corePool.h"
#include "../../imported/common/redCore/include/instrumentationObject.h"
#include "../../imported/common/redCore/include/profilerManager.h"
#include "../../imported/common/redJobs2/include/jobBuilder.h"
#include "../../imported/common/redJobs2/include/jobCounterFunctions.h"
#include "../../imported/common/redJobs2/include/jobSystem.h"
#include "../../imported/common/redMemory/include/utils.h"

#include <vanguard/memory/memory.hpp>

#include <cstddef>
#include <new>
#include <utility>

namespace vanguard::jobs
{
    struct BackendContextAccess
    {
        static void Set(JobContext& context, const void* backendContext) noexcept
        {
            context.m_backendContext = backendContext;
        }

        [[nodiscard]] static const void* Get(const JobContext& context) noexcept
        {
            return context.m_backendContext;
        }
    };
} // namespace vanguard::jobs

namespace
{
    template <typename T> struct EngineObjectEnvelope
    {
        vanguard::memory::MemoryBlock allocation;
        alignas(T) vanguard::byte object[sizeof(T)];
    };

    template <typename T, typename... Args> T* AllocateEngineObject(Args&&... args) noexcept
    {
        using Envelope = EngineObjectEnvelope<T>;

        vanguard::memory::MemoryBlock allocation = vanguard::memory::Allocate(vanguard::memory::PoolId::Jobs, sizeof(Envelope), alignof(Envelope));
        if (!allocation)
        {
            return nullptr;
        }

        auto* envelope = ::new (allocation.address) Envelope{};
        envelope->allocation = allocation;
        return ::new (envelope->object) T(std::forward<Args>(args)...);
    }

    template <typename T> void FreeEngineObject(T* const object) noexcept
    {
        if (object == nullptr)
        {
            return;
        }

        using Envelope = EngineObjectEnvelope<T>;
        auto* const envelope = reinterpret_cast<Envelope*>(reinterpret_cast<vanguard::byte*>(object) - offsetof(Envelope, object));

        object->~T();
        vanguard::memory::MemoryBlock allocation = envelope->allocation;
        envelope->~Envelope();
        vanguard::memory::Free(allocation);
    }

    enum class Lifecycle
    {
        NeverInitialized,
        Running,
        Stopped
    };

    Lifecycle g_lifecycle = Lifecycle::NeverInitialized;
    bool g_profilerInitialized = false;
    red::Atomic<red::Uint32> g_liveBuilders{0};
    red::Atomic<red::Uint32> g_liveCounters{0};
    red::Atomic<red::Uint32> g_activeDeferrals{0};
    red::Atomic<red::Uint32> g_outstandingJobs{0};
    red::Atomic<red::Uint64> g_submittedJobs{0};
    red::Atomic<red::Uint64> g_completedJobs{0};
    red::Atomic<red::Uint64> g_waitCalls{0};
    red::Atomic<red::Uint64> g_timedOutWaits{0};

    struct JobNameBackend
    {
        explicit JobNameBackend(const char* staticName) noexcept : instrumentation(staticName != nullptr ? staticName : "") {}

        red::InstrumentationObject instrumentation;
        red::Atomic<red::Uint32> inFlight{0};
    };

    [[nodiscard]] job::Priority ToRedPriority(const vanguard::jobs::Priority priority) noexcept
    {
        using vanguard::jobs::Priority;

        switch (priority)
        {
        case Priority::Latent:
            return job::Priority::Latent;
        case Priority::RenderPath:
            return job::Priority::RenderPath;
        case Priority::Immediate:
            return job::Priority::Immediate;
        case Priority::CriticalPath:
        default:
            return job::Priority::CriticalPath;
        }
    }

    [[nodiscard]] job::ScheduleParam ToRedSchedule(const vanguard::jobs::Schedule schedule) noexcept
    {
        return {ToRedPriority(schedule.priority), job::Affinity::All};
    }

    [[nodiscard]] vanguard::jobs::JobContext MakeContext(const job::RunContext& redContext) noexcept
    {
        vanguard::jobs::JobContext context;
        context.debugName = redContext.debugName;
        context.parallelForTeamIndex = redContext.parallelForTeamIndex;
        context.dispatcherThreadIndex = redContext.dispatcherThreadIndex;
        vanguard::jobs::BackendContextAccess::Set(context, &redContext);
        return context;
    }

    using TaskPacket = vanguard::jobs::detail::TaskPacket;
    using ParallelTaskPacket = vanguard::jobs::detail::ParallelTaskPacket;

    void RunTask(const TaskPacket task, JobNameBackend& name, const job::RunContext& redContext) noexcept
    {
        const vanguard::jobs::JobContext context = MakeContext(redContext);
        task.execute(task.state, context);
        task.destroy(task.state);
        name.inFlight.Decrement();
        g_outstandingJobs.Decrement();
        g_completedJobs.Increment();
    }

    void RunParallelTask(const ParallelTaskPacket task, const vanguard::u32 index, const job::RunContext& redContext) noexcept
    {
        const vanguard::jobs::JobContext context = MakeContext(redContext);
        task.execute(task.state, index, context);
    }

    void RunParallelEpilogue(const ParallelTaskPacket parallelTask, const TaskPacket epilogue, JobNameBackend& name, const job::RunContext& redContext) noexcept
    {
        parallelTask.destroy(parallelTask.state);

        if (epilogue.state != nullptr)
        {
            const vanguard::jobs::JobContext context = MakeContext(redContext);
            epilogue.execute(epilogue.state, context);
            epilogue.destroy(epilogue.state);
        }
        name.inFlight.Decrement();
        g_outstandingJobs.Decrement();
        g_completedJobs.Increment();
    }
} // namespace

namespace vanguard::jobs::backend
{
    Config RuntimeConfig() noexcept
    {
        const job::InitParam imported;

        Config config;
        config.maxLatentJobs = imported.maxLatentJobs;
        config.maxCriticalPathJobs = imported.maxCriticalPathJobs;
        config.maxImmediateJobs = imported.maxImmediateJobs;
        config.workerStackSizeKiB = imported.workerThreadStackSizeKB;
        config.maxWorkers = 0;
        config.allJobsCriticalPath = imported.allJobsCriticalPath;
        config.enableDebugger = imported.useJobDebugger;
        return config;
    }

    Config GetEditorConfig() noexcept
    {
        const job::InitParam imported = job::DefaultEditorInitParam();

        Config config;
        config.maxLatentJobs = imported.maxLatentJobs;
        config.maxCriticalPathJobs = imported.maxCriticalPathJobs;
        config.maxImmediateJobs = imported.maxImmediateJobs;
        config.workerStackSizeKiB = imported.workerThreadStackSizeKB;
        config.maxWorkers = 0;
        config.allJobsCriticalPath = imported.allJobsCriticalPath;
        config.enableDebugger = imported.useJobDebugger;
        return config;
    }

    Config ToolConfig() noexcept
    {
        const job::InitParam imported = job::DefaultToolInitParam();

        Config config;
        config.maxLatentJobs = imported.maxLatentJobs;
        config.maxCriticalPathJobs = imported.maxCriticalPathJobs;
        config.maxImmediateJobs = imported.maxImmediateJobs;
        config.workerStackSizeKiB = imported.workerThreadStackSizeKB;
        config.maxWorkers = 0;
        config.allJobsCriticalPath = imported.allJobsCriticalPath;
        config.enableDebugger = imported.useJobDebugger;
        return config;
    }

    bool Initialize(const Config& config) noexcept
    {
        if (g_lifecycle != Lifecycle::NeverInitialized)
        {
            return false;
        }

        if (config.maxLatentJobs == 0 || config.maxCriticalPathJobs == 0 || config.maxImmediateJobs == 0 || config.workerStackSizeKiB == 0 ||
            config.maxWorkers > MaximumWorkerCount)
        {
            return false;
        }

        red::memory::RegisterCurrentThread("VanguardMain");
        red::InitializeCoreMemoryPools();

#ifdef USE_PROFILER
        if (!gProfilers.Init(64u * 1024u * 1024u))
        {
            g_lifecycle = Lifecycle::Stopped;
            return false;
        }
        g_profilerInitialized = true;
        red::profiler::InitInGameProfiler();
#endif

        job::InitParam imported;
        imported.maxLatentJobs = config.maxLatentJobs;
        imported.maxCriticalPathJobs = config.maxCriticalPathJobs;
        imported.maxImmediateJobs = config.maxImmediateJobs;
        imported.workerThreadStackSizeKB = config.workerStackSizeKiB;
        if (config.maxWorkers != 0)
        {
            imported.maxThreads = config.maxWorkers;
        }
        imported.allJobsCriticalPath = config.allJobsCriticalPath;
        imported.useJobDebugger = config.enableDebugger;

        job::Initialize(imported);
        g_submittedJobs.SetValue(0);
        g_completedJobs.SetValue(0);
        g_waitCalls.SetValue(0);
        g_timedOutWaits.SetValue(0);
        g_activeDeferrals.SetValue(0);
        g_lifecycle = Lifecycle::Running;
        return true;
    }

    bool Shutdown() noexcept
    {
        if (g_lifecycle != Lifecycle::Running)
        {
            return false;
        }

        if (g_liveBuilders.GetValue() != 0 || g_liveCounters.GetValue() != 0 || g_activeDeferrals.GetValue() != 0 || g_outstandingJobs.GetValue() != 0)
        {
            return false;
        }

        job::Shutdown();
        if (g_profilerInitialized)
        {
#ifdef USE_PROFILER
            gProfilers.Shutdown();
#endif
            g_profilerInitialized = false;
        }
        g_lifecycle = Lifecycle::Stopped;
        return true;
    }

    bool IsInitialized() noexcept
    {
        return g_lifecycle == Lifecycle::Running;
    }

    u32 GetWorkerCount() noexcept
    {
        return IsInitialized() ? static_cast<u32>(job::GetNumDispatcherThreads()) : 0;
    }

    u32 GetOutstandingJobCount() noexcept
    {
        return static_cast<u32>(g_outstandingJobs.GetValue());
    }

    SchedulerStats GetSchedulerStats() noexcept
    {
        SchedulerStats stats;
        stats.initialized = IsInitialized();
        stats.workerCount = stats.initialized ? GetWorkerCount() : 0;
        stats.outstandingJobs = static_cast<u32>(g_outstandingJobs.GetValue());
        stats.liveBuilders = static_cast<u32>(g_liveBuilders.GetValue());
        stats.liveCounters = static_cast<u32>(g_liveCounters.GetValue());
        stats.activeDeferrals = static_cast<u32>(g_activeDeferrals.GetValue());
        stats.submittedJobs = static_cast<u64>(g_submittedJobs.GetValue());
        stats.completedJobs = static_cast<u64>(g_completedJobs.GetValue());
        stats.waitCalls = static_cast<u64>(g_waitCalls.GetValue());
        stats.timedOutWaits = static_cast<u64>(g_timedOutWaits.GetValue());

        if (stats.initialized)
        {
            stats.queued.latent = static_cast<u32>(job::GetApproximateQueueDepth(job::Priority::Latent));
            stats.queued.renderPath = static_cast<u32>(job::GetApproximateQueueDepth(job::Priority::RenderPath));
            stats.queued.criticalPath = static_cast<u32>(job::GetApproximateQueueDepth(job::Priority::CriticalPath));
            stats.queued.immediate = static_cast<u32>(job::GetApproximateQueueDepth(job::Priority::Immediate));
        }
        return stats;
    }

    u32 GetDispatcherThreadIndex() noexcept
    {
        return static_cast<u32>(job::GetDispatcherThreadIndex());
    }

    bool IsWorkerThread() noexcept
    {
        return IsInitialized() && GetDispatcherThreadIndex() != 0 && GetDispatcherThreadIndex() != UINT32_MAX;
    }

    void RegisterCurrentThread(const char* name) noexcept
    {
        if (IsInitialized())
        {
            red::memory::RegisterCurrentThread(name != nullptr ? name : "VanguardJobProducer");
        }
    }

    void ConstructJobName(void* storage, const char* staticName) noexcept
    {
        static_assert(sizeof(JobNameBackend) <= 64, "Vanguard JobName storage is too small for RED instrumentation.");
        static_assert(alignof(JobNameBackend) <= 16, "Vanguard JobName alignment is too small for RED instrumentation.");

        ::new (storage) JobNameBackend(staticName);
    }

    void DestroyJobName(void* storage) noexcept
    {
        auto* name = static_cast<JobNameBackend*>(storage);
        RED_FATAL_ASSERT(name->inFlight.GetValue() == 0, "A Vanguard JobName was destroyed while jobs still referenced it.");
        name->~JobNameBackend();
    }

    const char* GetJobName(const void* storage) noexcept
    {
        return static_cast<const JobNameBackend*>(storage)->instrumentation.m_name;
    }

    void* CreateBuilder(const Schedule schedule, const void* debugUserData) noexcept
    {
        if (!IsInitialized())
        {
            return nullptr;
        }

        auto* builder = AllocateEngineObject<job::Builder>(ToRedSchedule(schedule), debugUserData);
        if (builder == nullptr)
        {
            return nullptr;
        }

        g_liveBuilders.Increment();
        return builder;
    }

    void* CreateContinuationBuilder(const JobContext& context) noexcept
    {
        if (!IsInitialized())
        {
            return nullptr;
        }

        const auto* redContext = static_cast<const job::RunContext*>(BackendContextAccess::Get(context));
        if (redContext == nullptr)
        {
            return nullptr;
        }

        auto* builder = AllocateEngineObject<job::Builder>(*redContext);
        if (builder == nullptr)
        {
            return nullptr;
        }

        g_liveBuilders.Increment();
        return builder;
    }

    void DestroyBuilder(void* builder) noexcept
    {
        if (builder != nullptr)
        {
            FreeEngineObject(static_cast<job::Builder*>(builder));
            g_liveBuilders.Decrement();
        }
    }

    bool Dispatch(void* builder, void* jobName, const detail::TaskPacket task, const Fence fence) noexcept
    {
        if (builder == nullptr || jobName == nullptr || task.state == nullptr)
        {
            return false;
        }

        auto& importedBuilder = *static_cast<job::Builder*>(builder);
        auto* name = static_cast<JobNameBackend*>(jobName);

        const auto callable = [task, name](const job::RunContext& context) noexcept { RunTask(task, *name, context); };

        name->inFlight.Increment();
        g_outstandingJobs.Increment();
        g_submittedJobs.Increment();
        if (fence == Fence::Full)
        {
            importedBuilder.DispatchJob<job::Fence::Full>(name->instrumentation, callable);
        }
        else
        {
            importedBuilder.DispatchJob<job::Fence::None>(name->instrumentation, callable);
        }
        return true;
    }

    bool DispatchAfter(void* builder, const void* dependency, void* jobName, const detail::TaskPacket task, const Fence fence) noexcept
    {
        if (builder == nullptr || dependency == nullptr || jobName == nullptr || task.state == nullptr)
        {
            return false;
        }

        auto& importedBuilder = *static_cast<job::Builder*>(builder);
        const auto& importedDependency = *static_cast<const job::Counter*>(dependency);
        auto* name = static_cast<JobNameBackend*>(jobName);

        name->inFlight.Increment();
        g_outstandingJobs.Increment();
        g_submittedJobs.Increment();
        importedBuilder.DispatchJobAfterWait_NoFence(importedDependency, name->instrumentation,
                                                     [task, name](const job::RunContext& context) noexcept { RunTask(task, *name, context); });

        if (fence == Fence::Full)
        {
            importedBuilder.DispatchFenceExplicitly();
        }
        return true;
    }

    bool DispatchParallel(void* builder, void* jobName, const u32 elementCount, const detail::ParallelTaskPacket task, const detail::TaskPacket epilogue,
                          const u32 maximumBatchSize, const Fence fence) noexcept
    {
        if (builder == nullptr || jobName == nullptr || task.state == nullptr)
        {
            return false;
        }

        auto& importedBuilder = *static_cast<job::Builder*>(builder);
        auto* name = static_cast<JobNameBackend*>(jobName);

        const auto parallelCallable = [task](const u32 index, const job::RunContext& context) noexcept { RunParallelTask(task, index, context); };
        const auto epilogueCallable = [task, epilogue, name](const job::RunContext& context) noexcept { RunParallelEpilogue(task, epilogue, *name, context); };

        name->inFlight.Increment();
        g_outstandingJobs.Increment();
        g_submittedJobs.Increment();
        if (maximumBatchSize != 0)
        {
            if (fence == Fence::Full)
            {
                importedBuilder.DispatchParallelForJobWithEpilogueWithBatchSize<job::Fence::Full>(name->instrumentation, job::ImmediateValue{elementCount},
                                                                                                  parallelCallable, epilogueCallable, maximumBatchSize);
            }
            else
            {
                importedBuilder.DispatchParallelForJobWithEpilogueWithBatchSize<job::Fence::None>(name->instrumentation, job::ImmediateValue{elementCount},
                                                                                                  parallelCallable, epilogueCallable, maximumBatchSize);
            }
        }
        else if (fence == Fence::Full)
        {
            importedBuilder.DispatchParallelForJobWithEpilogue<job::Fence::Full>(name->instrumentation, job::ImmediateValue{elementCount}, parallelCallable,
                                                                                 epilogueCallable);
        }
        else
        {
            importedBuilder.DispatchParallelForJobWithEpilogue<job::Fence::None>(name->instrumentation, job::ImmediateValue{elementCount}, parallelCallable,
                                                                                 epilogueCallable);
        }
        return true;
    }

    void AddDependency(void* builder, const void* dependency) noexcept
    {
        static_cast<job::Builder*>(builder)->DispatchWait(*static_cast<const job::Counter*>(dependency));
    }

    void DispatchFence(void* builder) noexcept
    {
        static_cast<job::Builder*>(builder)->DispatchFenceExplicitly();
    }

    void* ExtractCounter(void* builder) noexcept
    {
        auto* counter = AllocateEngineObject<job::Counter>(static_cast<job::Builder*>(builder)->ExtractWaitCounter());
        if (counter == nullptr)
        {
            return nullptr;
        }

        g_liveCounters.Increment();
        return counter;
    }

    void DestroyCounter(void* counter) noexcept
    {
        if (counter != nullptr)
        {
            FreeEngineObject(static_cast<job::Counter*>(counter));
            g_liveCounters.Decrement();
        }
    }

    bool CounterIsReady(const void* counter) noexcept
    {
        return static_cast<const job::Counter*>(counter)->Internal_IsZeroSnapshot();
    }

    bool WaitCounter(const void* counter, const bool processLatent, const i32 timeoutMilliseconds) noexcept
    {
        g_waitCalls.Increment();
        const bool completed = job::FlushCounter(*static_cast<const job::Counter*>(counter), processLatent, timeoutMilliseconds);
        if (!completed)
        {
            g_timedOutWaits.Increment();
        }
        return completed;
    }

    bool WaitCounterOnProcessFrame(const void* counter) noexcept
    {
        g_waitCalls.Increment();
        const bool completed = job::FlushCounterOnProcessFrame(*static_cast<const job::Counter*>(counter));
        if (!completed)
        {
            g_timedOutWaits.Increment();
        }
        return completed;
    }

    void* CreateDeferral(void* counter, const char* staticDebugName, const void* debugUserData) noexcept
    {
        if (!IsInitialized() || counter == nullptr)
        {
            return nullptr;
        }

        auto& importedCounter = *static_cast<job::Counter*>(counter);
        auto* deferral =
            AllocateEngineObject<job::CompletionDeferral>(importedCounter.CreateDeferral(debugUserData, staticDebugName != nullptr ? staticDebugName : ""));
        if (deferral == nullptr)
        {
            return nullptr;
        }

        g_activeDeferrals.Increment();
        return deferral;
    }

    void DestroyDeferral(void* deferral) noexcept
    {
        if (deferral == nullptr)
        {
            return;
        }

        auto* imported = static_cast<job::CompletionDeferral*>(deferral);
        const bool wasActive = !imported->GetDebugIsFinished();
        FreeEngineObject(imported);
        if (wasActive)
        {
            g_activeDeferrals.Decrement();
        }
    }

    bool DeferralIsFinished(const void* deferral) noexcept
    {
        return deferral != nullptr && static_cast<const job::CompletionDeferral*>(deferral)->GetDebugIsFinished();
    }

    void FinishDeferral(void* deferral) noexcept
    {
        auto* imported = static_cast<job::CompletionDeferral*>(deferral);
        const bool wasFinished = imported->GetDebugIsFinished();
        imported->FinishDeferral();
        if (!wasFinished)
        {
            g_activeDeferrals.Decrement();
        }
    }

    void AnalyzeCounter(const void* counter) noexcept
    {
        if (IsInitialized() && counter != nullptr)
        {
            job::AnalyzeCounter(*static_cast<const job::Counter*>(counter));
        }
    }
} // namespace vanguard::jobs::backend

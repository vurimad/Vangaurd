#pragma once

#include <vanguard/jobs/task.hpp>

#include <cstddef>

namespace vanguard::jobs
{
    // RED Jobs 2's Windows ceiling when RED memory extended thread
    // registration is enabled by the imported WinPC configuration.
    inline constexpr u32 MaximumWorkerCount = 27;

    enum class Priority : u8
    {
        Latent,
        RenderPath,
        CriticalPath,
        Immediate
    };

    enum class Affinity : u8
    {
        AnyWorker
    };

    enum class Fence : u8
    {
        None,
        Full
    };

    struct Schedule
    {
        Priority priority = Priority::CriticalPath;
        Affinity affinity = Affinity::AnyWorker;
    };

    struct Config
    {
        u32 maxLatentJobs = 128 * 1024;
        u32 maxCriticalPathJobs = 64 * 1024;
        u32 maxImmediateJobs = 2 * 1024;
        u32 workerStackSizeKiB = 1024;
        u32 maxWorkers = 0;
        bool allJobsCriticalPath = false;
        bool enableDebugger = false;
    };

    struct QueueDepths
    {
        u32 latent = 0;
        u32 renderPath = 0;
        u32 criticalPath = 0;
        u32 immediate = 0;
    };

    // A lock-free diagnostic snapshot for editor panels, captures, and health
    // checks. Individual fields may advance while the snapshot is collected.
    struct SchedulerStats
    {
        QueueDepths queued;
        u32 workerCount = 0;
        u32 outstandingJobs = 0;
        u32 liveBuilders = 0;
        u32 liveCounters = 0;
        u32 activeDeferrals = 0;
        u64 submittedJobs = 0;
        u64 completedJobs = 0;
        u64 waitCalls = 0;
        u64 timedOutWaits = 0;
        bool initialized = false;
    };

    [[nodiscard]] Config RuntimeConfig() noexcept;
    [[nodiscard]] Config EditorConfig() noexcept;
    [[nodiscard]] Config ToolConfig() noexcept;

    // Initialization and shutdown are composition-root operations and must be
    // serialized. Every outstanding Counter must be completed before shutdown.
    [[nodiscard]] bool Initialize(const Config& config = RuntimeConfig()) noexcept;
    [[nodiscard]] bool Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;

    [[nodiscard]] u32 WorkerCount() noexcept;
    [[nodiscard]] u32 OutstandingJobCount() noexcept;
    [[nodiscard]] SchedulerStats GetSchedulerStats() noexcept;
    [[nodiscard]] u32 DispatcherThreadIndex() noexcept;
    [[nodiscard]] bool IsWorkerThread() noexcept;
    void RegisterCurrentThread(const char* name) noexcept;

    class JobName
    {
    public:
        explicit JobName(const char* staticName) noexcept;
        ~JobName();

        JobName(const JobName&) = delete;
        JobName& operator=(const JobName&) = delete;

        [[nodiscard]] const char* Get() const noexcept;

    private:
        friend class Builder;

        alignas(16) std::byte m_storage[64]{};
    };

    class CompletionDeferral
    {
    public:
        CompletionDeferral() noexcept = default;
        ~CompletionDeferral();

        CompletionDeferral(CompletionDeferral&& other) noexcept;
        CompletionDeferral& operator=(CompletionDeferral&& other) noexcept;

        CompletionDeferral(const CompletionDeferral&) = delete;
        CompletionDeferral& operator=(const CompletionDeferral&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsFinished() const noexcept;

        // Finishes the RED completion deferral. Calling Finish twice is a
        // contract violation, matching RED Jobs 2.
        void Finish() noexcept;

    private:
        friend class Counter;
        explicit CompletionDeferral(void* backend) noexcept;

        void* m_backend = nullptr;
    };

    class Counter
    {
    public:
        Counter() noexcept = default;
        ~Counter();

        Counter(Counter&& other) noexcept;
        Counter& operator=(Counter&& other) noexcept;

        Counter(const Counter&) = delete;
        Counter& operator=(const Counter&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsReady() const noexcept;

        [[nodiscard]] bool Wait(bool processLatent = false, i32 timeoutMilliseconds = -1) const noexcept;

        // Adds one RED completion deferral to this counter. The counter cannot
        // become ready until the returned object is finished or destroyed.
        [[nodiscard]] CompletionDeferral CreateDeferral(const char* staticDebugName = nullptr,
                                                        const void* debugUserData = nullptr) noexcept;

        // Emits RED's blocker/dependency/deferral analysis through its logger.
        // Config::enableDebugger must be enabled for the complete report.
        void Analyze() const noexcept;

    private:
        friend class Builder;
        explicit Counter(void* backend) noexcept;

        void* m_backend = nullptr;
    };

    class Builder
    {
    public:
        explicit Builder(Schedule schedule = {}, const void* debugUserData = nullptr) noexcept;
        explicit Builder(const JobContext& continuationContext) noexcept;
        ~Builder();

        Builder(const Builder&) = delete;
        Builder& operator=(const Builder&) = delete;
        Builder(Builder&&) = delete;
        Builder& operator=(Builder&&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] bool Dispatch(JobName& name, Task&& task, Fence fence = Fence::Full) noexcept;

        [[nodiscard]] bool DispatchAfter(const Counter& dependency, JobName& name, Task&& task, Fence fence = Fence::None) noexcept;

        [[nodiscard]] bool DispatchParallel(JobName& name, u32 elementCount, ParallelTask&& task, Task&& epilogue = {},
                                            u32 maximumBatchSize = 0, Fence fence = Fence::Full) noexcept;

        void AddDependency(const Counter& dependency) noexcept;

        // Required after one or more Fence::None dispatches and before a
        // Fence::Full dispatch, AddDependency, ExtractCounter, or destruction.
        void DispatchFence() noexcept;
        [[nodiscard]] Counter ExtractCounter() noexcept;

    private:
        void* m_backend = nullptr;
        bool m_hasOpenFenceGroup = false;
    };
} // namespace vanguard::jobs

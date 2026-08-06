#pragma once

#include <vanguard/jobs/jobs.hpp>

namespace vanguard::jobs::backend
{
    [[nodiscard]] Config RuntimeConfig() noexcept;
    [[nodiscard]] Config EditorConfig() noexcept;
    [[nodiscard]] Config ToolConfig() noexcept;

    [[nodiscard]] bool Initialize(const Config& config) noexcept;
    [[nodiscard]] bool Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] u32 WorkerCount() noexcept;
    [[nodiscard]] u32 OutstandingJobCount() noexcept;
    [[nodiscard]] SchedulerStats GetSchedulerStats() noexcept;
    [[nodiscard]] u32 DispatcherThreadIndex() noexcept;
    [[nodiscard]] bool IsWorkerThread() noexcept;
    void RegisterCurrentThread(const char* name) noexcept;

    void ConstructJobName(void* storage, const char* staticName) noexcept;
    void DestroyJobName(void* storage) noexcept;
    [[nodiscard]] const char* GetJobName(const void* storage) noexcept;

    [[nodiscard]] void* CreateBuilder(Schedule schedule, const void* debugUserData) noexcept;
    [[nodiscard]] void* CreateContinuationBuilder(const JobContext& context) noexcept;
    void DestroyBuilder(void* builder) noexcept;

    [[nodiscard]] bool Dispatch(void* builder, void* jobName, detail::TaskPacket task, Fence fence) noexcept;
    [[nodiscard]] bool DispatchAfter(void* builder, const void* dependency, void* jobName, detail::TaskPacket task, Fence fence) noexcept;
    [[nodiscard]] bool DispatchParallel(void* builder, void* jobName, u32 elementCount, detail::ParallelTaskPacket task,
                                        detail::TaskPacket epilogue, u32 maximumBatchSize, Fence fence) noexcept;

    void AddDependency(void* builder, const void* dependency) noexcept;
    void DispatchFence(void* builder) noexcept;
    [[nodiscard]] void* ExtractCounter(void* builder) noexcept;

    void DestroyCounter(void* counter) noexcept;
    [[nodiscard]] bool CounterIsReady(const void* counter) noexcept;
    [[nodiscard]] bool WaitCounter(const void* counter, bool processLatent, i32 timeoutMilliseconds) noexcept;
    [[nodiscard]] void* CreateDeferral(void* counter, const char* staticDebugName, const void* debugUserData) noexcept;
    void DestroyDeferral(void* deferral) noexcept;
    [[nodiscard]] bool DeferralIsFinished(const void* deferral) noexcept;
    void FinishDeferral(void* deferral) noexcept;
    void AnalyzeCounter(const void* counter) noexcept;
} // namespace vanguard::jobs::backend

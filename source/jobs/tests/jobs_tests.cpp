#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/jobs/jobs.hpp>

#include <cstdio>

namespace
{
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::concurrency::Atomic;
    using vanguard::concurrency::ManualResetEvent;
    using namespace vanguard::jobs;

    [[nodiscard]] bool Check(const bool condition, const char* message) noexcept
    {
        if (!condition)
        {
            std::fprintf(stderr, "[jobsTests] %s\n", message);
        }
        return condition;
    }
} // namespace

int main()
{
    std::puts("[jobsTests] querying runtime config");
    std::fflush(stdout);
    Config config = RuntimeConfig();
    config.maxWorkers = 4;
    config.enableDebugger = true;

    std::puts("[jobsTests] initializing");
    std::fflush(stdout);
    if (!Check(Initialize(config), "Initialize failed") || !Check(IsInitialized(), "Jobs did not enter running state") ||
        !Check(WorkerCount() == 4, "Unexpected worker count") ||
        !Check(GetSchedulerStats().initialized, "Telemetry did not enter running state") ||
        !Check(DispatcherThreadIndex() == 0, "Main thread index is not zero") ||
        !Check(!IsWorkerThread(), "Main thread reported as a worker"))
    {
        return 1;
    }
    std::puts("[jobsTests] initialized");
    std::fflush(stdout);

    JobName bulkName{"Vanguard.Jobs.Bulk"};
    JobName explicitFenceName{"Vanguard.Jobs.ExplicitFence"};
    JobName dependencyName{"Vanguard.Jobs.Dependency"};
    JobName parallelName{"Vanguard.Jobs.Parallel"};
    JobName continuationName{"Vanguard.Jobs.Continuation"};
    JobName deferralName{"Vanguard.Jobs.Deferral"};

    constexpr u32 bulkCount = 4096;
    Atomic<u32> completed{0};
    Atomic<u32> completedOnWorkers{0};
    {
        std::puts("[jobsTests] bulk");
        std::fflush(stdout);
        Builder builder;
        if (!Check(builder.IsValid(), "Bulk builder is invalid"))
        {
            return 2;
        }

        for (u32 index = 0; index < bulkCount; ++index)
        {
            Task task = Task::Create(
                [&completed, &completedOnWorkers](const JobContext& context) noexcept
                {
                    (void)completed.Increment();
                    if (context.dispatcherThreadIndex != 0 && IsWorkerThread())
                    {
                        (void)completedOnWorkers.Increment();
                    }
                });

            if (!Check(builder.Dispatch(bulkName, std::move(task), Fence::None), "Bulk dispatch failed"))
            {
                return 3;
            }
        }

        builder.DispatchFence();
        Counter counter = builder.ExtractCounter();
        if (!Check(counter.IsValid(), "Bulk counter is invalid") || !Check(counter.Wait(), "Bulk wait failed"))
        {
            return 4;
        }
    }

    if (!Check(completed.GetValue() == bulkCount, "Not all bulk jobs completed") ||
        !Check(completedOnWorkers.GetValue() != 0, "No bulk job executed on a worker"))
    {
        return 5;
    }

    {
        constexpr u32 firstPhaseCount = 128;
        Atomic<u32> firstPhaseCompleted{0};
        Atomic<u32> orderingFailures{0};
        Builder builder;
        for (u32 index = 0; index < firstPhaseCount; ++index)
        {
            Task task = Task::Create([&firstPhaseCompleted](const JobContext&) noexcept { (void)firstPhaseCompleted.Increment(); });
            if (!builder.Dispatch(explicitFenceName, std::move(task), Fence::None))
            {
                return 22;
            }
        }
        builder.DispatchFence();

        Task secondPhase = Task::Create(
            [&firstPhaseCompleted, &orderingFailures](const JobContext&) noexcept
            {
                if (firstPhaseCompleted.GetValue() != firstPhaseCount)
                {
                    (void)orderingFailures.Increment();
                }
            });
        if (!builder.Dispatch(explicitFenceName, std::move(secondPhase), Fence::Full))
        {
            return 23;
        }

        Counter counter = builder.ExtractCounter();
        if (!Check(counter.Wait() && firstPhaseCompleted.GetValue() == firstPhaseCount && orderingFailures.GetValue() == 0,
                   "Explicit fence did not separate dispatch phases"))
        {
            return 24;
        }
    }

    Atomic<u32> dependencyStage{0};
    {
        Counter prerequisite;
        {
            std::puts("[jobsTests] dependencies");
            std::fflush(stdout);
            Builder first;
            Task task = Task::Create([&dependencyStage](const JobContext&) noexcept { dependencyStage.SetValue(1); });
            if (!first.Dispatch(dependencyName, std::move(task)))
            {
                return 6;
            }
            prerequisite = first.ExtractCounter();
        }

        {
            Builder second;
            Task task = Task::Create(
                [&dependencyStage](const JobContext&) noexcept
                {
                    if (dependencyStage.GetValue() == 1)
                    {
                        dependencyStage.SetValue(2);
                    }
                });
            if (!second.DispatchAfter(prerequisite, dependencyName, std::move(task), Fence::Full))
            {
                return 7;
            }

            Counter dependent = second.ExtractCounter();
            if (!Check(dependent.Wait(), "Dependent job wait failed") ||
                !Check(dependencyStage.GetValue() == 2, "Dependency ordering was violated"))
            {
                return 8;
            }
        }
    }

    constexpr u32 elementCount = 65536;
    Atomic<u64> sum{0};
    Atomic<u32> epilogueCount{0};
    {
        std::puts("[jobsTests] parallel");
        std::fflush(stdout);
        Builder builder;
        ParallelTask task = ParallelTask::Create(
            [&sum](const u32 index, const JobContext& context) noexcept
            {
                if (context.parallelForTeamIndex >= 0)
                {
                    (void)sum.ExchangeAdd(static_cast<u64>(index) + 1);
                }
            });
        Task epilogue = Task::Create([&epilogueCount](const JobContext&) noexcept { (void)epilogueCount.Increment(); });

        if (!Check(builder.DispatchParallel(parallelName, elementCount, std::move(task), std::move(epilogue), 128),
                   "Parallel dispatch failed"))
        {
            return 9;
        }

        Counter counter = builder.ExtractCounter();
        if (!Check(counter.Wait(), "Parallel wait failed"))
        {
            return 10;
        }
    }

    const u64 expectedSum = static_cast<u64>(elementCount) * static_cast<u64>(elementCount + 1) / 2;
    if (!Check(sum.GetValue() == expectedSum, "Parallel result is incorrect") ||
        !Check(epilogueCount.GetValue() == 1, "Parallel epilogue count is wrong"))
    {
        return 11;
    }

    Atomic<u32> continuationCount{0};
    {
        std::puts("[jobsTests] continuation");
        std::fflush(stdout);
        Builder parent;
        Task task = Task::Create(
            [&continuationCount, &continuationName](const JobContext& context) noexcept
            {
                (void)continuationCount.Increment();

                Builder continuation{context};
                Task child = Task::Create([&continuationCount](const JobContext&) noexcept { (void)continuationCount.Increment(); });
                (void)continuation.Dispatch(continuationName, std::move(child));
            });

        if (!parent.Dispatch(continuationName, std::move(task)))
        {
            return 12;
        }

        Counter counter = parent.ExtractCounter();
        if (!Check(counter.Wait(), "Continuation wait failed") ||
            !Check(continuationCount.GetValue() == 2, "Continuation did not extend parent completion"))
        {
            return 13;
        }
    }

    Atomic<u32> deferralJobCompleted{0};
    {
        Builder builder;
        Task task = Task::Create([&deferralJobCompleted](const JobContext&) noexcept { deferralJobCompleted.SetValue(1); });
        if (!builder.Dispatch(deferralName, std::move(task)))
        {
            return 25;
        }

        Counter counter = builder.ExtractCounter();
        CompletionDeferral deferral = counter.CreateDeferral("Vanguard.Jobs.TestDeferral");
        if (!Check(deferral.IsValid(), "Completion deferral is invalid") ||
            !Check(GetSchedulerStats().activeDeferrals == 1, "Active deferral telemetry is incorrect"))
        {
            return 26;
        }

        while (deferralJobCompleted.GetValue() == 0)
        {
            vanguard::concurrency::YieldCurrentThread();
        }
        if (!Check(!counter.IsReady(), "Counter completed while its RED deferral was active"))
        {
            return 27;
        }

        counter.Analyze();
        deferral.Finish();
        if (!Check(deferral.IsFinished(), "Deferral did not finish") || !Check(counter.Wait(), "Deferred counter did not complete") ||
            !Check(GetSchedulerStats().activeDeferrals == 0, "Finished deferral remained active in telemetry"))
        {
            return 28;
        }

        {
            CompletionDeferral scopedDeferral = counter.CreateDeferral("Vanguard.Jobs.ScopedDeferral");
            if (!Check(scopedDeferral.IsValid() && !counter.IsReady(), "Scoped RED deferral did not hold the counter"))
            {
                return 32;
            }
        }
        if (!Check(counter.Wait() && GetSchedulerStats().activeDeferrals == 0, "Deferral destruction did not release the counter"))
        {
            return 33;
        }
    }

    ManualResetEvent releaseWorker{false};
    {
        std::puts("[jobsTests] timed wait");
        std::fflush(stdout);
        Builder builder;
        Task task = Task::Create([&releaseWorker](const JobContext&) noexcept { releaseWorker.Wait(); });
        if (!builder.Dispatch(dependencyName, std::move(task)))
        {
            return 14;
        }

        Counter counter = builder.ExtractCounter();
        if (!Check(!counter.Wait(false, 10), "Timed wait completed before the job was released"))
        {
            return 15;
        }

        releaseWorker.Signal();
        if (!Check(counter.Wait(), "Wait after signal failed"))
        {
            return 16;
        }
    }

    Counter shutdownGuard;
    {
        Builder builder;
        Task task = Task::Create([](const JobContext&) noexcept {});
        if (!builder.Dispatch(dependencyName, std::move(task)))
        {
            return 17;
        }
        shutdownGuard = builder.ExtractCounter();
    }
    if (!shutdownGuard.Wait() || !Check(!Shutdown(), "Shutdown accepted a live counter handle") ||
        !Check(IsInitialized(), "Rejected shutdown changed scheduler state"))
    {
        return 18;
    }
    shutdownGuard = {};

    CompletionDeferral shutdownDeferral;
    {
        Builder builder;
        Task task = Task::Create([](const JobContext&) noexcept {});
        if (!builder.Dispatch(deferralName, std::move(task)))
        {
            return 29;
        }
        Counter counter = builder.ExtractCounter();
        if (!counter.Wait())
        {
            return 30;
        }
        shutdownDeferral = counter.CreateDeferral("Vanguard.Jobs.ShutdownDeferral");
    }
    if (!Check(!Shutdown() && IsInitialized(), "Shutdown accepted an active RED completion deferral"))
    {
        return 31;
    }
    shutdownDeferral.Finish();
    shutdownDeferral = {};

    std::puts("[jobsTests] shutdown");
    std::fflush(stdout);
    const SchedulerStats finalStats = GetSchedulerStats();
    if (!Check(OutstandingJobCount() == 0, "Logical jobs remained outstanding before shutdown") ||
        !Check(finalStats.submittedJobs == finalStats.completedJobs, "Telemetry submission/completion totals diverged") ||
        !Check(finalStats.activeDeferrals == 0, "An active completion deferral remained before shutdown") ||
        !Check(finalStats.timedOutWaits == 1, "Telemetry did not record the timed-out wait") ||
        !Check(Shutdown(), "Shutdown rejected a clean jobs state"))
    {
        return 19;
    }
    if (!Check(!IsInitialized(), "Jobs remained initialized after shutdown"))
    {
        return 20;
    }
    if (!Check(!GetSchedulerStats().initialized, "Telemetry remained initialized after shutdown"))
    {
        return 21;
    }

    std::printf("[jobsTests] Passed: %u workers, %u jobs, %u parallel elements.\n", config.maxWorkers, bulkCount, elementCount);
    return 0;
}

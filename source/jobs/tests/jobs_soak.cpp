#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/jobs/jobs.hpp>

#include <cstdio>
#include <utility>

namespace
{
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::concurrency::Atomic;
    using vanguard::concurrency::ManualResetEvent;
    using namespace vanguard::jobs;

    constexpr u32 GraphNodeCount = 2048;
    constexpr u32 GraphRounds = 8;
    constexpr u32 MaximumDependencies = 4;
    constexpr u32 QueuedJobsPerPriority = 64;

    class Random final
    {
    public:
        explicit Random(const u64 seed) noexcept : m_state(seed != 0 ? seed : 0x9E3779B97F4A7C15ull) {}

        [[nodiscard]] u32 Next() noexcept
        {
            u64 value = m_state;
            value ^= value >> 12;
            value ^= value << 25;
            value ^= value >> 27;
            m_state = value;
            return static_cast<u32>((value * 0x2545F4914F6CDD1Dull) >> 32);
        }

        [[nodiscard]] u32 Below(const u32 limit) noexcept
        {
            return limit != 0 ? Next() % limit : 0;
        }

    private:
        u64 m_state;
    };

    struct NodeSpec
    {
        u32 dependencies[MaximumDependencies]{};
        u32 dependencyCount = 0;
    };

    struct NodeState
    {
        Atomic<u32> value{0};
    };

    [[nodiscard]] bool Check(const bool condition, const char* message) noexcept
    {
        if (!condition)
        {
            std::fprintf(stderr, "[jobsSoak] %s\n", message);
        }
        return condition;
    }

    [[nodiscard]] bool DispatchQueuedPriority(const Priority priority, JobName& name, Atomic<u32>& executed, Counter& output) noexcept
    {
        Builder builder{Schedule{priority, Affinity::AnyWorker}};
        for (u32 index = 0; index < QueuedJobsPerPriority; ++index)
        {
            Task task = Task::Create([&executed](const JobContext&) noexcept { (void)executed.Increment(); });
            if (!builder.Dispatch(name, std::move(task), Fence::None))
            {
                return false;
            }
        }
        builder.DispatchFence();
        output = builder.ExtractCounter();
        return output.IsValid();
    }

    [[nodiscard]] bool VerifyQueueGauges() noexcept
    {
        std::puts("[jobsSoak] queue gauges: pinning workers");
        std::fflush(stdout);
        const u32 workerCount = vanguard::jobs::WorkerCount();
        ManualResetEvent releaseWorkers{false};
        Atomic<u32> enteredWorkers{0};
        Atomic<u32> executed{0};
        JobName blockerName{"Vanguard.Jobs.Soak.QueueBlocker"};
        JobName queuedName{"Vanguard.Jobs.Soak.Queued"};

        Counter blockerCounter;
        {
            Builder blockers{Schedule{Priority::Immediate, Affinity::AnyWorker}};
            for (u32 index = 0; index < workerCount; ++index)
            {
                Task blocker = Task::Create(
                    [&enteredWorkers, &releaseWorkers](const JobContext&) noexcept
                    {
                        (void)enteredWorkers.Increment();
                        releaseWorkers.Wait();
                    });
                if (!blockers.Dispatch(blockerName, std::move(blocker), Fence::None))
                {
                    return false;
                }
            }
            blockers.DispatchFence();
            blockerCounter = blockers.ExtractCounter();
        }

        u32 waitMilliseconds = 0;
        while (enteredWorkers.GetValue() != workerCount && waitMilliseconds < 10000)
        {
            vanguard::concurrency::SleepOnCurrentThread(1);
            ++waitMilliseconds;
        }
        if (!Check(enteredWorkers.GetValue() == workerCount, "Workers did not enter the queue-gauge barrier"))
        {
            releaseWorkers.Signal();
            return false;
        }

        Counter queuedCounters[4];
        std::puts("[jobsSoak] queue gauges: filling priorities");
        std::fflush(stdout);
        const bool dispatched = DispatchQueuedPriority(Priority::Latent, queuedName, executed, queuedCounters[0]) &&
                                DispatchQueuedPriority(Priority::RenderPath, queuedName, executed, queuedCounters[1]) &&
                                DispatchQueuedPriority(Priority::CriticalPath, queuedName, executed, queuedCounters[2]) &&
                                DispatchQueuedPriority(Priority::Immediate, queuedName, executed, queuedCounters[3]);

        const SchedulerStats blockedStats = GetSchedulerStats();
        const bool gaugesValid =
            blockedStats.queued.latent >= QueuedJobsPerPriority && blockedStats.queued.renderPath >= QueuedJobsPerPriority &&
            blockedStats.queued.criticalPath >= QueuedJobsPerPriority && blockedStats.queued.immediate >= QueuedJobsPerPriority;

        std::printf("[jobsSoak] queue depths: %u/%u/%u/%u\n", blockedStats.queued.latent, blockedStats.queued.renderPath,
                    blockedStats.queued.criticalPath, blockedStats.queued.immediate);
        std::fflush(stdout);
        releaseWorkers.Signal();
        std::puts("[jobsSoak] queue gauges: draining");
        std::fflush(stdout);
        if (!dispatched || !gaugesValid || !blockerCounter.Wait())
        {
            return Check(false, "Physical queue gauge validation failed");
        }
        for (Counter& counter : queuedCounters)
        {
            if (!counter.Wait())
            {
                return Check(false, "Queued priority work did not complete");
            }
        }
        return Check(executed.GetValue() == QueuedJobsPerPriority * 4, "Queued priority execution count mismatched");
    }

    [[nodiscard]] bool RunGraphRound(const u64 seed, JobName& graphName) noexcept
    {
        Random random{seed};
        NodeSpec specs[GraphNodeCount]{};
        NodeState states[GraphNodeCount]{};
        Counter counters[GraphNodeCount];
        Atomic<u32> orderingFailures{0};

        for (u32 node = 1; node < GraphNodeCount; ++node)
        {
            NodeSpec& spec = specs[node];
            const u32 maximum = node < MaximumDependencies ? node : MaximumDependencies;
            spec.dependencyCount = 1 + random.Below(maximum);

            for (u32 slot = 0; slot < spec.dependencyCount; ++slot)
            {
                u32 candidate = 0;
                bool duplicate = false;
                do
                {
                    candidate = random.Below(node);
                    duplicate = false;
                    for (u32 prior = 0; prior < slot; ++prior)
                    {
                        duplicate |= spec.dependencies[prior] == candidate;
                    }
                } while (duplicate);
                spec.dependencies[slot] = candidate;
            }
        }

        for (u32 node = 0; node < GraphNodeCount; ++node)
        {
            Builder builder;
            const NodeSpec& spec = specs[node];
            for (u32 slot = 0; slot < spec.dependencyCount; ++slot)
            {
                builder.AddDependency(counters[spec.dependencies[slot]]);
            }

            Task task = Task::Create(
                [&states, &orderingFailures, &spec, node](const JobContext&) noexcept
                {
                    for (u32 slot = 0; slot < spec.dependencyCount; ++slot)
                    {
                        if (states[spec.dependencies[slot]].value.GetValue() != 1)
                        {
                            (void)orderingFailures.Increment();
                        }
                    }
                    states[node].value.SetValue(1);
                });

            const Fence fence = (random.Next() & 7u) == 0 ? Fence::Full : Fence::None;
            if (!builder.Dispatch(graphName, std::move(task), fence))
            {
                return Check(false, "Graph node dispatch failed");
            }
            if (fence == Fence::None)
            {
                builder.DispatchFence();
            }
            counters[node] = builder.ExtractCounter();
        }

        for (u32 node = 0; node < GraphNodeCount; ++node)
        {
            if (!counters[node].Wait())
            {
                return Check(false, "Graph counter wait failed");
            }
        }

        if (orderingFailures.GetValue() != 0)
        {
            return Check(false, "A DAG dependency executed out of order");
        }
        for (const NodeState& state : states)
        {
            if (state.value.GetValue() != 1)
            {
                return Check(false, "A DAG node did not execute exactly once");
            }
        }
        return true;
    }

    [[nodiscard]] bool VerifyShutdownRefusal() noexcept
    {
        ManualResetEvent entered{false};
        ManualResetEvent release{false};
        JobName name{"Vanguard.Jobs.Soak.Shutdown"};

        {
            Builder builder;
            Task task = Task::Create(
                [&entered, &release](const JobContext&) noexcept
                {
                    entered.Signal();
                    release.Wait();
                });
            if (!builder.Dispatch(name, std::move(task)))
            {
                return false;
            }
            Counter counter = builder.ExtractCounter();
            if (!entered.TryWait(10000))
            {
                release.Signal();
                return Check(false, "Shutdown blocker did not start");
            }
            if (!Check(!Shutdown() && IsInitialized(), "Shutdown accepted active scheduler state"))
            {
                release.Signal();
                return false;
            }
            release.Signal();
            if (!counter.Wait() || !Check(!Shutdown() && IsInitialized(), "Shutdown accepted live handles"))
            {
                return false;
            }
        }
        return true;
    }
} // namespace

int main()
{
    Config config = RuntimeConfig();
    config.maxWorkers = 4;
    config.enableDebugger = true;
    if (!Check(Initialize(config), "Initialization failed") || !VerifyQueueGauges())
    {
        return 1;
    }

    JobName graphName{"Vanguard.Jobs.Soak.DeterministicDAG"};
    constexpr u64 baseSeed = 0xD1B54A32D192ED03ull;
    for (u32 round = 0; round < GraphRounds; ++round)
    {
        const u64 seed = baseSeed ^ (0x9E3779B97F4A7C15ull * (round + 1));
        std::printf("[jobsSoak] DAG round %u/%u, seed 0x%016llX\n", round + 1, GraphRounds, static_cast<unsigned long long>(seed));
        std::fflush(stdout);
        if (!RunGraphRound(seed, graphName))
        {
            std::fprintf(stderr, "[jobsSoak] failing seed: 0x%016llX\n", static_cast<unsigned long long>(seed));
            return 2;
        }
    }

    if (!VerifyShutdownRefusal())
    {
        return 3;
    }

    const SchedulerStats stats = GetSchedulerStats();
    if (!Check(stats.outstandingJobs == 0, "Jobs remained outstanding") ||
        !Check(stats.submittedJobs == stats.completedJobs, "Submission/completion totals diverged") ||
        !Check(Shutdown(), "Clean shutdown failed"))
    {
        return 4;
    }

    std::printf("[jobsSoak] Passed: %u seeds, %u DAG nodes/seed, "
                "%llu logical jobs completed.\n",
                GraphRounds, GraphNodeCount, static_cast<unsigned long long>(stats.completedJobs));
    return 0;
}

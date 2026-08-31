#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/jobs/jobs.hpp>

#include <cstdio>
#include <utility>

namespace
{
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::concurrency::Atomic;
    using namespace vanguard::jobs;

    constexpr u32 ProducerCount = 3;
    constexpr u32 JobsPerProducer = 8192;

    class Producer final : public vanguard::concurrency::Thread
    {
    public:
        Producer(const u32 producerIndex, JobName& name, Atomic<u64>& completed, Counter& output) noexcept
            : Thread("JobsStressProducer"), m_producerIndex(producerIndex), m_name(name), m_completed(completed), m_output(output)
        {
        }

        void ThreadFunction() noexcept override
        {
            RegisterCurrentThread("JobsStressProducer");

            Builder builder;
            for (u32 index = 0; index < JobsPerProducer; ++index)
            {
                Task task = Task::Create([this](const JobContext&) noexcept { (void)m_completed.Increment(); });

                if (!builder.Dispatch(m_name, std::move(task), Fence::None))
                {
                    m_failed.SetValue(true);
                    return;
                }
            }

            builder.DispatchFence();
            m_output = builder.ExtractCounter();
        }

        [[nodiscard]] bool Failed() const noexcept
        {
            return m_failed.GetValue();
        }

    private:
        u32 m_producerIndex;
        JobName& m_name;
        Atomic<u64>& m_completed;
        Counter& m_output;
        Atomic<bool> m_failed{false};
    };
} // namespace

int main()
{
    Config config = RuntimeConfig();
    config.maxWorkers = MaximumWorkerCount;
    config.enableDebugger = true;

    if (!Initialize(config))
    {
        std::fputs("[jobsStress] initialization failed\n", stderr);
        return 1;
    }

    const u32 hardwareThreads = vanguard::concurrency::GetMaxHardwareConcurrency();
    const u32 availableWorkers = hardwareThreads > 1 ? hardwareThreads - 1 : 1;
    const u32 expectedWorkers = availableWorkers < MaximumWorkerCount ? availableWorkers : MaximumWorkerCount;
    if (GetWorkerCount() != expectedWorkers)
    {
        std::fputs("[jobsStress] RED worker ceiling mismatch\n", stderr);
        return 12;
    }

    JobName bulkName{"Vanguard.Jobs.Stress.MultiProducer"};
    JobName chainName{"Vanguard.Jobs.Stress.Chain"};
    JobName parallelName{"Vanguard.Jobs.Stress.Parallel"};

    Atomic<u64> completed{0};
    Counter producerCounters[ProducerCount];
    Producer producers[ProducerCount]{
        {0, bulkName, completed, producerCounters[0]}, {1, bulkName, completed, producerCounters[1]}, {2, bulkName, completed, producerCounters[2]}};

    for (Producer& producer : producers)
    {
        producer.InitThread();
    }
    for (Producer& producer : producers)
    {
        producer.JoinThread();
        if (producer.Failed())
        {
            return 2;
        }
    }
    for (Counter& counter : producerCounters)
    {
        if (!counter.IsValid() || !counter.Wait())
        {
            return 3;
        }
    }

    const u64 expectedProduced = static_cast<u64>(ProducerCount) * JobsPerProducer;
    if (completed.GetValue() != expectedProduced)
    {
        std::fputs("[jobsStress] multi-producer count mismatch\n", stderr);
        return 4;
    }

    Atomic<u32> chainValue{0};
    {
        Counter previous;
        {
            Builder root;
            Task rootTask = Task::Create([&chainValue](const JobContext&) noexcept { chainValue.SetValue(1); });
            if (!root.Dispatch(chainName, std::move(rootTask)))
            {
                return 5;
            }
            previous = root.ExtractCounter();
        }

        constexpr u32 chainLength = 1024;
        for (u32 expected = 2; expected <= chainLength; ++expected)
        {
            Builder nextBuilder;
            Task nextTask = Task::Create(
                [&chainValue, expected](const JobContext&) noexcept
                {
                    if (chainValue.GetValue() == expected - 1)
                    {
                        chainValue.SetValue(expected);
                    }
                });
            if (!nextBuilder.DispatchAfter(previous, chainName, std::move(nextTask), Fence::Full))
            {
                return 6;
            }

            Counter next = nextBuilder.ExtractCounter();
            previous = std::move(next);
        }

        if (!previous.Wait() || chainValue.GetValue() != chainLength)
        {
            std::fputs("[jobsStress] dependency chain failed\n", stderr);
            return 7;
        }
    }

    Atomic<u64> parallelVisits{0};
    constexpr u32 parallelRounds = 32;
    constexpr u32 elementsPerRound = 32768;
    for (u32 round = 0; round < parallelRounds; ++round)
    {
        Builder builder;
        ParallelTask task = ParallelTask::Create([&parallelVisits](u32, const JobContext&) noexcept { (void)parallelVisits.Increment(); });
        if (!builder.DispatchParallel(parallelName, elementsPerRound, std::move(task), {}, 64))
        {
            return 8;
        }

        Counter counter = builder.ExtractCounter();
        if (!counter.Wait())
        {
            return 9;
        }
    }

    const u64 expectedParallelVisits = static_cast<u64>(parallelRounds) * elementsPerRound;
    if (parallelVisits.GetValue() != expectedParallelVisits)
    {
        std::fputs("[jobsStress] parallel visit count mismatch\n", stderr);
        return 10;
    }

    for (Counter& counter : producerCounters)
    {
        counter = {};
    }

    if (GetOutstandingJobCount() != 0 || !Shutdown())
    {
        std::fputs("[jobsStress] shutdown contract failed\n", stderr);
        return 11;
    }

    std::printf("[jobsStress] Passed: %llu producer jobs, %u dependency links, "
                "%llu parallel visits.\n",
                static_cast<unsigned long long>(expectedProduced), 1024u, static_cast<unsigned long long>(expectedParallelVisits));
    return 0;
}

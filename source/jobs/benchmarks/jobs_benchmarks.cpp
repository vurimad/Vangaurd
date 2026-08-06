#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>

#include <Windows.h>

#include <cstdio>
#include <utility>

namespace
{
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::concurrency::Atomic;
    using vanguard::concurrency::ManualResetEvent;
    using namespace vanguard::jobs;

    constexpr u32 SampleCount = 9;
    constexpr u32 LogicalJobsPerSample = 32768;
    constexpr u32 ParallelElements = 1024 * 1024;
    constexpr u32 WakeSamples = 101;

    struct Clock
    {
        Clock() noexcept
        {
            LARGE_INTEGER value{};
            QueryPerformanceFrequency(&value);
            frequency = static_cast<u64>(value.QuadPart);
        }

        [[nodiscard]] u64 Now() const noexcept
        {
            LARGE_INTEGER value{};
            QueryPerformanceCounter(&value);
            return static_cast<u64>(value.QuadPart);
        }

        [[nodiscard]] double Nanoseconds(const u64 begin, const u64 end) const noexcept
        {
            return static_cast<double>(end - begin) * 1000000000.0 / static_cast<double>(frequency);
        }

        u64 frequency = 0;
    };

    template <u32 Count> void Sort(double (&values)[Count]) noexcept
    {
        for (u32 index = 1; index < Count; ++index)
        {
            const double value = values[index];
            u32 position = index;
            while (position != 0 && values[position - 1] > value)
            {
                values[position] = values[position - 1];
                --position;
            }
            values[position] = value;
        }
    }

    [[nodiscard]] bool RunLogicalBatch(const Clock& clock, JobName& name, const u32 count, double& enqueueNanosecondsPerJob,
                                       double& completionJobsPerSecond) noexcept
    {
        Builder builder;
        const u64 begin = clock.Now();
        for (u32 index = 0; index < count; ++index)
        {
            Task task = Task::Create([](const JobContext&) noexcept {});
            if (!builder.Dispatch(name, std::move(task), Fence::None))
            {
                return false;
            }
        }
        builder.DispatchFence();
        const u64 enqueued = clock.Now();
        Counter counter = builder.ExtractCounter();
        if (!counter.Wait())
        {
            return false;
        }
        const u64 completed = clock.Now();

        enqueueNanosecondsPerJob = clock.Nanoseconds(begin, enqueued) / static_cast<double>(count);
        completionJobsPerSecond = static_cast<double>(count) * 1000000000.0 / clock.Nanoseconds(begin, completed);
        return true;
    }

    [[nodiscard]] bool MeasureWakeLatency(const Clock& clock, JobName& name, double (&samples)[WakeSamples]) noexcept
    {
        for (u32 sample = 0; sample < WakeSamples; ++sample)
        {
            ManualResetEvent startedEvent{false};
            Atomic<u64> startedAt{0};
            vanguard::concurrency::SleepOnCurrentThread(2);

            Builder builder{Schedule{Priority::Immediate, Affinity::AnyWorker}};
            const u64 dispatchedAt = clock.Now();
            Task task = Task::Create(
                [&clock, &startedAt, &startedEvent](const JobContext&) noexcept
                {
                    startedAt.SetValue(clock.Now());
                    startedEvent.Signal();
                });
            if (!builder.Dispatch(name, std::move(task)))
            {
                return false;
            }
            Counter counter = builder.ExtractCounter();
            if (!startedEvent.TryWait(10000) || !counter.Wait())
            {
                return false;
            }
            samples[sample] = clock.Nanoseconds(dispatchedAt, startedAt.GetValue());
        }
        return true;
    }

    [[nodiscard]] bool MeasureParallel(const Clock& clock, JobName& name, double (&samples)[SampleCount]) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(static_cast<vanguard::usize>(ParallelElements) * sizeof(u32), alignof(u32));
        if (!block)
        {
            return false;
        }
        auto* values = static_cast<u32*>(block.address);

        bool succeeded = true;
        for (u32 sample = 0; sample < SampleCount; ++sample)
        {
            Builder builder;
            const u64 begin = clock.Now();
            ParallelTask task =
                ParallelTask::Create([values](const u32 index, const JobContext&) noexcept { values[index] = index ^ 0xA5A5A5A5u; });
            if (!builder.DispatchParallel(name, ParallelElements, std::move(task), {}, 256))
            {
                succeeded = false;
                break;
            }
            Counter counter = builder.ExtractCounter();
            if (!counter.Wait())
            {
                succeeded = false;
                break;
            }
            const u64 completed = clock.Now();
            samples[sample] = static_cast<double>(ParallelElements) * 1000000000.0 / clock.Nanoseconds(begin, completed);
        }

        if (succeeded && values[ParallelElements - 1] != ((ParallelElements - 1) ^ 0xA5A5A5A5u))
        {
            succeeded = false;
        }
        vanguard::memory::Free(block);
        return succeeded;
    }
} // namespace

int main()
{
    Config config = RuntimeConfig();
    config.maxWorkers = 4;
    config.enableDebugger = false;
    if (!Initialize(config))
    {
        std::fputs("[jobsBenchmarks] initialization failed\n", stderr);
        return 1;
    }

    Clock clock;
    JobName logicalName{"Vanguard.Jobs.Benchmark.Logical"};
    JobName wakeName{"Vanguard.Jobs.Benchmark.Wake"};
    JobName parallelName{"Vanguard.Jobs.Benchmark.Parallel"};

    double discardedEnqueue = 0.0;
    double discardedCompletion = 0.0;
    if (!RunLogicalBatch(clock, logicalName, 4096, discardedEnqueue, discardedCompletion))
    {
        return 2;
    }

    double enqueueSamples[SampleCount]{};
    double completionSamples[SampleCount]{};
    for (u32 sample = 0; sample < SampleCount; ++sample)
    {
        if (!RunLogicalBatch(clock, logicalName, LogicalJobsPerSample, enqueueSamples[sample], completionSamples[sample]))
        {
            return 3;
        }
    }

    double wakeSamples[WakeSamples]{};
    double parallelSamples[SampleCount]{};
    if (!MeasureWakeLatency(clock, wakeName, wakeSamples) || !MeasureParallel(clock, parallelName, parallelSamples))
    {
        return 4;
    }

    Sort(enqueueSamples);
    Sort(completionSamples);
    Sort(wakeSamples);
    Sort(parallelSamples);

    const SchedulerStats stats = GetSchedulerStats();
    if (stats.outstandingJobs != 0 || stats.submittedJobs != stats.completedJobs || !Shutdown())
    {
        std::fputs("[jobsBenchmarks] shutdown contract failed\n", stderr);
        return 5;
    }

    std::printf("[jobsBenchmarks] workers: %u\n"
                "  enqueue p50:        %.1f ns/logical-job\n"
                "  enqueue p95:        %.1f ns/logical-job\n"
                "  completion p50:     %.3f M logical-jobs/s\n"
                "  completion p95:     %.3f M logical-jobs/s\n"
                "  idle wake p50:      %.1f us dispatch-to-start\n"
                "  idle wake p95:      %.1f us dispatch-to-start\n"
                "  parallel p50:       %.3f M elements/s\n"
                "  parallel p95:       %.3f M elements/s\n",
                config.maxWorkers, enqueueSamples[SampleCount / 2], enqueueSamples[SampleCount - 1],
                completionSamples[SampleCount / 2] / 1000000.0, completionSamples[SampleCount - 1] / 1000000.0,
                wakeSamples[WakeSamples / 2] / 1000.0, wakeSamples[WakeSamples - 1] / 1000.0, parallelSamples[SampleCount / 2] / 1000000.0,
                parallelSamples[SampleCount - 1] / 1000000.0);
    return 0;
}

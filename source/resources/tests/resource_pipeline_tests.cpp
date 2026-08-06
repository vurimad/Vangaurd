#include <vanguard/resources/resource_pipeline.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/pool.hpp>

#include <array>
#include <cstdio>
#include <thread>

namespace
{
    using namespace vanguard;
    using namespace vanguard::resources;

    u32 g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            ++g_failures;
            std::fprintf(stderr, "[resourcePipelineTests] FAILED: %s\n", message);
        }
    }

    class GraphResource final : public ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(memory::pools::Resources);

        GraphResource(const ResourceTypeId type, const ResourceId identity) noexcept : m_type(type), m_identity(identity) {}

        [[nodiscard]] ResourceTypeId Type() const noexcept override
        {
            return m_type;
        }

        [[nodiscard]] ResourceId Identity() const noexcept
        {
            return m_identity;
        }

    private:
        ResourceTypeId m_type;
        ResourceId m_identity;
    };

    struct GraphHarness
    {
        ResourceTypeId type = InvalidResourceTypeId;
        ResourceReference root;
        ResourceReference left;
        ResourceReference right;
        ResourceReference shared;
        ResourceReference optionalRoot;
        ResourceReference optionalFailure;
        ResourceReference requiredRoot;
        ResourceReference requiredFailure;
        ResourceReference cycleA;
        ResourceReference cycleB;
        ResourceReference cancelA;
        ResourceReference cancelB;
        ResourceReference slowShared;
        ResourceReference soloSlow;
        concurrency::ManualResetEvent slowEntered{false};
        concurrency::ManualResetEvent releaseSlow{false};
        concurrency::Atomic<u32> discoveries{0};
        concurrency::Atomic<u32> constructions{0};
        concurrency::Atomic<u32> destructions{0};
        concurrency::Atomic<u32> sharedConstructions{0};
        concurrency::Atomic<u32> slowConstructions{0};
        concurrency::Atomic<u32> optionalObserved{0};
        concurrency::Atomic<u32> highestSlowPriority{static_cast<u32>(LoadPriority::Background)};
    };

    [[nodiscard]] ResourceReference MakeReference(const char* const path, const ResourceTypeId type) noexcept
    {
        return ResourceReference(ResourcePath::FromString(path), type);
    }

    Failure DiscoverGraphDependencies(const ResourceReference reference, DependencyBuilder& dependencies, void* const userData) noexcept
    {
        auto& graph = *static_cast<GraphHarness*>(userData);
        static_cast<void>(graph.discoveries.Increment());

        if (reference == graph.root)
        {
            if (!dependencies.Add(graph.left) || !dependencies.Add(graph.right))
            {
                return Failure::OutOfMemory;
            }
        }
        else if (reference == graph.left || reference == graph.right)
        {
            if (!dependencies.Add(graph.shared))
            {
                return Failure::OutOfMemory;
            }
        }
        else if (reference == graph.optionalRoot)
        {
            if (!dependencies.Add(graph.optionalFailure, DependencyRequirement::Optional))
            {
                return Failure::OutOfMemory;
            }
        }
        else if (reference == graph.requiredRoot)
        {
            if (!dependencies.Add(graph.requiredFailure))
            {
                return Failure::OutOfMemory;
            }
        }
        else if (reference == graph.cycleA)
        {
            if (!dependencies.Add(graph.cycleB))
            {
                return Failure::OutOfMemory;
            }
        }
        else if (reference == graph.cycleB)
        {
            if (!dependencies.Add(graph.cycleA))
            {
                return Failure::OutOfMemory;
            }
        }
        else if (reference == graph.cancelA || reference == graph.cancelB)
        {
            if (!dependencies.Add(graph.slowShared))
            {
                return Failure::OutOfMemory;
            }
        }
        return Failure::None;
    }

    ResourceObject* ConstructGraphResource(const LoadContext& context, Failure& failure, void* const userData) noexcept
    {
        auto& graph = *static_cast<GraphHarness*>(userData);
        const ResourceReference reference = context.Reference();

        if (reference == graph.optionalFailure || reference == graph.requiredFailure)
        {
            failure = Failure::IoFailure;
            return nullptr;
        }

        if (reference == graph.slowShared || reference == graph.soloSlow)
        {
            static_cast<void>(graph.slowConstructions.Increment());
            graph.highestSlowPriority.SetValue(static_cast<u32>(context.Priority()));
            graph.slowEntered.Signal();
            graph.releaseSlow.Wait();
            if (context.IsCancellationRequested())
            {
                failure = Failure::Cancelled;
                return nullptr;
            }
        }

        for (u32 index = 0; index < context.DependencyCount(); ++index)
        {
            if (context.DependencyRequirementAt(index) == DependencyRequirement::Required && !context.Dependency(index))
            {
                failure = Failure::DependencyFailure;
                return nullptr;
            }
        }

        if (reference == graph.optionalRoot)
        {
            if (context.DependencyCount() == 1 && !context.Dependency(0) && context.DependencyError(0) == Failure::IoFailure)
            {
                graph.optionalObserved.SetValue(1);
            }
        }
        if (reference == graph.shared)
        {
            static_cast<void>(graph.sharedConstructions.Increment());
        }
        static_cast<void>(graph.constructions.Increment());
        return VANGUARD_NEW(GraphResource)(graph.type, reference.Path().Id());
    }

    void DestroyGraphResource(ResourceObject* const resource, void* const userData) noexcept
    {
        auto& graph = *static_cast<GraphHarness*>(userData);
        static_cast<void>(graph.destructions.Increment());
        VANGUARD_DELETE(static_cast<GraphResource*>(resource));
    }
} // namespace

int main()
{
    using namespace vanguard;
    using namespace vanguard::resources;

    ResourceRegistry registry;
    Check(registry.Initialize(), "registry initializes");

    ResourcePipeline pipeline;
    Check(pipeline.Initialize(registry, PipelineConfig{16, 16}), "pipeline initializes with Vanguard Jobs");

    GraphHarness graph;
    graph.type = HashTypeName("vanguard.graph-test");
    graph.root = MakeReference("graph/root.vgraph", graph.type);
    graph.left = MakeReference("graph/left.vgraph", graph.type);
    graph.right = MakeReference("graph/right.vgraph", graph.type);
    graph.shared = MakeReference("graph/shared.vgraph", graph.type);
    graph.optionalRoot = MakeReference("graph/optional-root.vgraph", graph.type);
    graph.optionalFailure = MakeReference("graph/optional-failure.vgraph", graph.type);
    graph.requiredRoot = MakeReference("graph/required-root.vgraph", graph.type);
    graph.requiredFailure = MakeReference("graph/required-failure.vgraph", graph.type);
    graph.cycleA = MakeReference("graph/cycle-a.vgraph", graph.type);
    graph.cycleB = MakeReference("graph/cycle-b.vgraph", graph.type);
    graph.cancelA = MakeReference("graph/cancel-a.vgraph", graph.type);
    graph.cancelB = MakeReference("graph/cancel-b.vgraph", graph.type);
    graph.slowShared = MakeReference("graph/slow-shared.vgraph", graph.type);
    graph.soloSlow = MakeReference("graph/solo-slow.vgraph", graph.type);

    const AsyncLoaderDescriptor loader{
        graph.type, "graph test loader", &DiscoverGraphDependencies, &ConstructGraphResource, &DestroyGraphResource, &graph};
    Check(pipeline.RegisterLoader(loader), "asynchronous graph loader registers");
    Check(!pipeline.RegisterLoader(loader), "duplicate asynchronous loader is rejected");

    PipelineRequest first = pipeline.Request(graph.root, LoadPriority::Background);
    PipelineRequest second = pipeline.Request(graph.root, LoadPriority::Critical);
    Check(first.IsSameOperation(second), "root requests coalesce into one graph operation");
    Check(first.Priority() == LoadPriority::Critical && second.Priority() == LoadPriority::Critical,
          "higher caller priority promotes the shared operation");
    Check(first.TryWait(5000), "dependency fan-in completes without blocking a worker");
    second.Wait();
    Check(first.HasLoaded() && second.HasLoaded(), "all root observers see successful graph completion");
    Check(graph.sharedConstructions.GetValue() == 1, "diamond graph constructs its shared dependency once");
    ResourceHandle rootHandle = first.Acquire();
    Check(rootHandle.IsValid() && static_cast<GraphResource*>(rootHandle.Get())->Identity() == graph.root.Path().Id(),
          "completed graph publishes the requested root");

    constexpr usize simultaneousRequestCount = 16;
    std::array<PipelineRequest, simultaneousRequestCount> simultaneousRequests;
    std::array<std::thread, simultaneousRequestCount> simultaneousThreads;
    for (usize index = 0; index < simultaneousRequestCount; ++index)
    {
        simultaneousThreads[index] = std::thread([index, &pipeline, &graph, &simultaneousRequests]()
                                                 { simultaneousRequests[index] = pipeline.Request(graph.root, LoadPriority::Normal); });
    }
    for (std::thread& thread : simultaneousThreads)
    {
        thread.join();
    }
    for (PipelineRequest& request : simultaneousRequests)
    {
        Check(request.IsSameOperation(first) && request.HasLoaded(), "simultaneous callers share the loaded graph operation");
    }

    PipelineRequest optional = pipeline.Request(graph.optionalRoot);
    Check(optional.TryWait(5000) && optional.HasLoaded(), "optional dependency failure does not fail its parent");
    Check(graph.optionalObserved.GetValue() == 1, "constructor can inspect an optional dependency failure");
    ResourceHandle optionalHandle = optional.Acquire();

    PipelineRequest required = pipeline.Request(graph.requiredRoot);
    Check(required.TryWait(5000) && required.HasFailed() && required.Error() == Failure::DependencyFailure,
          "required dependency failure propagates to its parent");
    FailureTrace requiredTrace;
    Check(required.GetFailureTrace(requiredTrace) && requiredTrace.count == 2 && requiredTrace.entries[0].resource == graph.requiredRoot &&
              requiredTrace.entries[0].failure == Failure::DependencyFailure &&
              requiredTrace.entries[1].resource == graph.requiredFailure && requiredTrace.entries[1].failure == Failure::IoFailure,
          "failure trace preserves the causal dependency chain");

    PipelineRequest cycle = pipeline.Request(graph.cycleA);
    Check(cycle.TryWait(5000) && cycle.HasFailed(), "dependency cycle terminates instead of deadlocking");
    FailureTrace cycleTrace;
    Check(cycle.GetFailureTrace(cycleTrace) && cycleTrace.count >= 1, "cycle failure remains diagnosable");

    PipelineRequest cancelFirst = pipeline.Request(graph.cancelA, LoadPriority::Low);
    PipelineRequest cancelSecond = pipeline.Request(graph.cancelB, LoadPriority::High);
    Check(graph.slowEntered.TryWait(5000), "shared slow dependency begins construction");
    Check(cancelFirst.Cancel() && cancelFirst.Status() == State::Cancelled, "one parent can explicitly cancel its interest");
    Check(!cancelSecond.HasFinished(), "shared dependency continues for its remaining parent");
    graph.releaseSlow.Signal();
    Check(cancelSecond.TryWait(5000) && cancelSecond.HasLoaded(), "remaining parent completes after shared cancellation");
    Check(graph.slowConstructions.GetValue() == 1, "shared dependency is not restarted after one cancellation");
    ResourceHandle cancelHandle = cancelSecond.Acquire();

    graph.slowEntered.Reset();
    graph.releaseSlow.Reset();
    PipelineRequest soloCancellation = pipeline.Request(graph.soloSlow, LoadPriority::Normal);
    Check(graph.slowEntered.TryWait(5000), "single-interest construction begins");
    Check(soloCancellation.Cancel(), "last caller explicitly cancels running construction");
    graph.releaseSlow.Signal();
    Check(soloCancellation.HasFailed() && soloCancellation.Error() == Failure::Cancelled,
          "running loader observes last-interest cancellation");

    const ResourceTypeId unknownType = HashTypeName("vanguard.pipeline-unknown");
    PipelineRequest unknown = pipeline.Request(MakeReference("graph/unknown.vgraph", unknownType));
    Check(unknown.TryWait(5000) && unknown.HasFailed() && unknown.Error() == Failure::UnknownType,
          "unregistered dependency type fails asynchronously");

    const PipelineStats stats = pipeline.GetStats();
    Check(stats.registeredLoaders == 1 && stats.coalescedRequests >= 1 && stats.priorityPromotions >= 1 && stats.dependencyEdges >= 9,
          "pipeline exposes editor-ready graph and priority statistics");

    first.Reset();
    second.Reset();
    for (PipelineRequest& request : simultaneousRequests)
    {
        request.Reset();
    }
    optional.Reset();
    required.Reset();
    cycle.Reset();
    cancelFirst.Reset();
    cancelSecond.Reset();
    soloCancellation.Reset();
    unknown.Reset();
    rootHandle.Reset();
    optionalHandle.Reset();
    cancelHandle.Reset();

    for (u32 attempt = 0; attempt < 500 && pipeline.GetStats().activeJobs != 0; ++attempt)
    {
        concurrency::SleepOnCurrentThread(1);
    }

    Check(pipeline.GetStats().activeJobs == 0, "all asynchronous pipeline stages drain");
    Check(pipeline.Shutdown(), "pipeline shuts down after requests and handles release");
    Check(registry.Shutdown(), "registry shuts down after pipeline removal");
    Check(jobs::Shutdown(), "Vanguard Jobs shuts down after resource work drains");

    if (g_failures == 0)
    {
        std::puts("[resourcePipelineTests] Vanguard asynchronous resource "
                  "pipeline checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}

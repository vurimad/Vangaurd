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

        [[nodiscard]] ResourceTypeId GetType() const noexcept override
        {
            return m_type;
        }

        [[nodiscard]] ResourceId GetIdentity() const noexcept
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
        ResourceReference replacementRoot;
        ResourceReference replacementParent;
        concurrency::ManualResetEvent slowEntered{false};
        concurrency::ManualResetEvent releaseSlow{false};
        concurrency::ManualResetEvent replacementEntered{false};
        concurrency::ManualResetEvent releaseReplacement{false};
        concurrency::Atomic<u32> discoveries{0};
        concurrency::Atomic<u32> constructions{0};
        concurrency::Atomic<u32> destructions{0};
        concurrency::Atomic<u32> sharedConstructions{0};
        concurrency::Atomic<u32> slowConstructions{0};
        concurrency::Atomic<u32> optionalObserved{0};
        concurrency::Atomic<u32> replacementConstructions{0};
        concurrency::Atomic<u32> observedReplacementDependencyGeneration{0};
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
        else if (reference == graph.replacementParent)
        {
            if (!dependencies.Add(graph.replacementRoot))
                return Failure::OutOfMemory;
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
        if (reference == graph.replacementRoot)
        {
            static_cast<void>(graph.replacementConstructions.Increment());
            if (graph.replacementConstructions.GetValue() > 1)
            {
                graph.replacementEntered.Signal();
                graph.releaseReplacement.Wait();
                if (context.IsCancellationRequested())
                {
                    failure = Failure::Cancelled;
                    return nullptr;
                }
            }
        }

        for (u32 index = 0; index < context.GetDependencyCount(); ++index)
        {
            if (context.GetDependencyRequirementAt(index) == DependencyRequirement::Required && !context.GetDependency(index))
            {
                failure = Failure::DependencyFailure;
                return nullptr;
            }
        }

        if (reference == graph.optionalRoot)
        {
            if (context.GetDependencyCount() == 1 && !context.GetDependency(0) && context.GetDependencyError(0) == Failure::IoFailure)
            {
                graph.optionalObserved.SetValue(1);
            }
        }
        if (reference == graph.replacementParent && context.GetDependencyCount() == 1)
        {
            const ResourceHandle dependency = context.GetDependency(0);
            if (dependency)
                graph.observedReplacementDependencyGeneration.SetValue(dependency.GetGeneration());
        }
        if (reference == graph.shared)
        {
            static_cast<void>(graph.sharedConstructions.Increment());
        }
        static_cast<void>(graph.constructions.Increment());
        return VANGUARD_NEW(GraphResource)(graph.type, reference.GetPath().Id());
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
    graph.replacementRoot = MakeReference("graph/replacement-root.vgraph", graph.type);
    graph.replacementParent = MakeReference("graph/replacement-parent.vgraph", graph.type);

    const AsyncLoaderDescriptor loader{graph.type, "graph test loader", &DiscoverGraphDependencies, &ConstructGraphResource, &DestroyGraphResource, &graph};
    Check(pipeline.RegisterLoader(loader), "asynchronous graph loader registers");
    Check(!pipeline.RegisterLoader(loader), "duplicate asynchronous loader is rejected");

    PipelineRequest first = pipeline.Request(graph.root, LoadPriority::Background);
    PipelineRequest second = pipeline.Request(graph.root, LoadPriority::Critical);
    Check(first.IsSameOperation(second), "root requests coalesce into one graph operation");
    Check(first.Priority() == LoadPriority::Critical && second.Priority() == LoadPriority::Critical, "higher caller priority promotes the shared operation");
    Check(first.TryWait(5000), "dependency fan-in completes without blocking a worker");
    second.Wait();
    Check(first.HasLoaded() && second.HasLoaded(), "all root observers see successful graph completion");
    Check(graph.sharedConstructions.GetValue() == 1, "diamond graph constructs its shared dependency once");
    ResourceHandle rootHandle = first.Acquire();
    Check(rootHandle.IsValid() && static_cast<GraphResource*>(rootHandle.Get())->GetIdentity() == graph.root.GetPath().Id(),
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
    Check(required.TryWait(5000) && required.HasFailed() && required.GetError() == Failure::DependencyFailure,
          "required dependency failure propagates to its parent");
    FailureTrace requiredTrace;
    Check(required.GetFailureTrace(requiredTrace) && requiredTrace.count == 2 && requiredTrace.entries[0].resource == graph.requiredRoot &&
              requiredTrace.entries[0].failure == Failure::DependencyFailure && requiredTrace.entries[1].resource == graph.requiredFailure &&
              requiredTrace.entries[1].failure == Failure::IoFailure,
          "failure trace preserves the causal dependency chain");

    PipelineRequest cycle = pipeline.Request(graph.cycleA);
    Check(cycle.TryWait(5000) && cycle.HasFailed(), "dependency cycle terminates instead of deadlocking");
    FailureTrace cycleTrace;
    Check(cycle.GetFailureTrace(cycleTrace) && cycleTrace.count >= 1, "cycle failure remains diagnosable");

    PipelineRequest cancelFirst = pipeline.Request(graph.cancelA, LoadPriority::Low);
    PipelineRequest cancelSecond = pipeline.Request(graph.cancelB, LoadPriority::High);
    Check(graph.slowEntered.TryWait(5000), "shared slow dependency begins construction");
    Check(cancelFirst.Cancel() && cancelFirst.GetStatus() == State::Cancelled, "one parent can explicitly cancel its interest");
    Check(!cancelSecond.HasFinished(), "shared dependency continues for its remaining parent");
    graph.releaseSlow.Signal();
    Check(cancelSecond.TryWait(5000) && cancelSecond.HasLoaded(), "remaining parent completes after shared cancellation");
    Check(graph.slowConstructions.GetValue() == 1, "shared dependency is not restarted after one cancellation");
    ResourceHandle cancelHandle = cancelSecond.Acquire();

    // P3: a runtime resident normally outlives the PipelineRequest that loaded it.
    // Releasing that request exact-evicts the pipeline operation while the strong
    // handle deliberately keeps the registry generation alive. Replacement must
    // still load a distinct candidate, cut the path over atomically, and leave the
    // old handle/weak generation valid until their own references drain.
    PipelineRequest replacementInitial = pipeline.Request(graph.replacementRoot);
    Check(replacementInitial.TryWait(5000) && replacementInitial.HasLoaded(), "replacement fixture initial generation loads");
    ResourceHandle replacementOld = replacementInitial.Acquire();
    WeakResourceHandle replacementOldWeak = replacementOld.ToWeak();
    const u32 replacementOldGeneration = replacementOld.GetGeneration();
    replacementInitial.Reset();
    Check(registry.GetState(graph.replacementRoot.GetPath()) == State::Evicting && replacementOld.IsValid(),
          "strong resident keeps old generation alive after its pipeline request releases");

    PipelineRequest replacement = pipeline.Replace(graph.replacementRoot, LoadPriority::High);
    Check(replacement.IsValid() && graph.replacementEntered.TryWait(5000),
          "replacement can start from a strongly retained current generation without an old pipeline request");

    PipelineRequest replacementDuring = pipeline.Request(graph.replacementRoot);
    Check(replacementDuring.IsValid() && replacementDuring.HasLoaded(),
          "ordinary request during replacement resolves to the anchored old-current operation");
    ResourceHandle replacementDuringHandle = replacementDuring.Acquire();
    Check(replacementDuringHandle.IsValid() && replacementDuringHandle.GetGeneration() == replacementOldGeneration,
          "pre-cutover ordinary request cannot create a duplicate wrapper for the replacement candidate");

    PipelineRequest replacementParent = pipeline.Request(graph.replacementParent);
    Check(replacementParent.TryWait(5000) && replacementParent.HasLoaded() &&
              graph.observedReplacementDependencyGeneration.GetValue() == replacementOldGeneration,
          "dependency attachment during replacement resolves through the same anchored old-current operation");

    graph.releaseReplacement.Signal();
    Check(replacement.TryWait(5000) && replacement.HasLoaded(), "replacement candidate completes after the cutover barrier releases");
    ResourceHandle replacementNew = replacement.Acquire();
    Check(replacementNew.IsValid() && replacementNew.GetGeneration() != replacementOldGeneration && replacementOld.IsValid(),
          "replacement publishes a distinct current generation while the old strong handle remains live");
    ResourceHandle replacementOldWeakLock = replacementOldWeak.Lock();
    Check(replacementOldWeakLock.IsValid() && replacementOldWeakLock.GetGeneration() == replacementOldGeneration,
          "old weak handle remains generation-safe across replacement cutover");
    PipelineRequest replacementCurrent = pipeline.Request(graph.replacementRoot);
    ResourceHandle replacementCurrentHandle = replacementCurrent.Acquire();
    Check(replacementCurrent.IsSameOperation(replacement) && replacementCurrentHandle.IsValid() &&
              replacementCurrentHandle.GetGeneration() == replacementNew.GetGeneration(),
          "ordinary requests route to the replacement operation after cutover");

    replacementOldWeakLock.Reset();
    replacementDuringHandle.Reset();
    replacementDuring.Reset();
    replacementParent.Reset();
    replacementOld.Reset();
    Check(replacementOldWeak.IsStale() && !replacementOldWeak.Lock(),
          "retired old weak generation becomes stale after its last strong handle drains");
    replacementOldWeak.Reset();

    graph.slowEntered.Reset();
    graph.releaseSlow.Reset();
    PipelineRequest soloCancellation = pipeline.Request(graph.soloSlow, LoadPriority::Normal);
    Check(graph.slowEntered.TryWait(5000), "single-interest construction begins");
    Failure cancellationFailure = Failure::None;
    Check(soloCancellation.CancelWithStatus(&cancellationFailure) == PipelineCancelStatus::Cancelled &&
              cancellationFailure == Failure::None,
          "P4 atomic cancellation reports unfinished caller-interest withdrawal");
    graph.releaseSlow.Signal();
    Check(soloCancellation.HasFailed() && soloCancellation.GetError() == Failure::Cancelled, "running loader observes last-interest cancellation");

    const ResourceTypeId unknownType = HashTypeName("vanguard.pipeline-unknown");
    PipelineRequest unknown = pipeline.Request(MakeReference("graph/unknown.vgraph", unknownType));
    Check(unknown.TryWait(5000) && unknown.HasFailed() && unknown.GetError() == Failure::UnknownType, "unregistered dependency type fails asynchronously");
    Failure terminalCancellationFailure = Failure::None;
    Check(unknown.CancelWithStatus(&terminalCancellationFailure) == PipelineCancelStatus::Failed &&
              terminalCancellationFailure == Failure::UnknownType,
          "P4 atomic cancellation preserves an already-terminal load failure instead of rewriting it as Cancelled");
    PipelineRequest completedCancellation = pipeline.Request(graph.shared);
    Check(completedCancellation.TryWait(5000) && completedCancellation.HasLoaded() &&
              completedCancellation.CancelWithStatus(&terminalCancellationFailure) == PipelineCancelStatus::Completed &&
              terminalCancellationFailure == Failure::None,
          "P4 atomic cancellation distinguishes already-completed success from unfinished cancellation");

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
    completedCancellation.Reset();
    replacement.Reset();
    replacementCurrent.Reset();
    rootHandle.Reset();
    optionalHandle.Reset();
    cancelHandle.Reset();
    replacementNew.Reset();
    replacementCurrentHandle.Reset();

    for (u32 attempt = 0; attempt < 500 && pipeline.GetStats().activeJobs != 0; ++attempt)
    {
        concurrency::SleepOnCurrentThread(1);
    }

    Check(pipeline.GetStats().activeJobs == 0 && pipeline.GetStats().externalRequests == 0,
          "P4 atomic cancellation and terminal release return external request ownership to baseline");
    Check(pipeline.Shutdown(), "pipeline shuts down after requests and handles release");

    // P4: terminal operation metadata is reclaimed at runtime and the configured
    // bound is a deterministic admission guard rather than permanent history
    // growth. A graph needing more than one simultaneous operation fails at a
    // one-operation cap; after that failed root retires, a leaf can use the same
    // bounded slot without reinitializing the pipeline or registry.
    PipelineConfig boundedPipelineConfig;
    boundedPipelineConfig.maximumRetainedOperations = 1u;
    ResourcePipeline boundedPipeline;
    Check(boundedPipeline.Initialize(registry, boundedPipelineConfig) && boundedPipeline.RegisterLoader(loader),
          "P4 bounded resource pipeline initializes and registers its loader");
    PipelineRequest boundedOverflow = boundedPipeline.Request(graph.root);
    Check(boundedOverflow.TryWait(5000) && boundedOverflow.HasFailed(),
          "P4 operation-history cap rejects graph expansion without unbounded metadata growth");
    boundedOverflow.Reset();
    for (u32 attempt = 0; attempt < 500 && boundedPipeline.GetStats().activeJobs != 0; ++attempt)
        concurrency::SleepOnCurrentThread(1);
    PipelineRequest boundedRecovered = boundedPipeline.Request(graph.shared);
    Check(boundedRecovered.TryWait(5000) && boundedRecovered.HasLoaded() && boundedPipeline.GetStats().knownOperations <= 1u,
          "P4 terminal no-interest operation is reclaimed so bounded pipeline admission recovers");
    boundedRecovered.Reset();
    for (u32 attempt = 0; attempt < 500 && boundedPipeline.GetStats().activeJobs != 0; ++attempt)
        concurrency::SleepOnCurrentThread(1);

    // A cancellation can publish terminal completion before the scheduled
    // construction job returns from user code. The retained-operation collector
    // must keep that captured PipelineOperation storage alive until the worker is
    // actually gone; at a one-operation cap, a different request therefore stays
    // inadmissible while the cancelled worker is deliberately held.
    graph.slowEntered.Reset();
    graph.releaseSlow.Reset();
    PipelineRequest boundedCancelledWorker = boundedPipeline.Request(graph.soloSlow);
    Check(boundedCancelledWorker.IsValid() && graph.slowEntered.TryWait(5000),
          "P4 bounded reclamation fixture holds a scheduled construction job live");
    Check(boundedCancelledWorker.Cancel(),
          "P4 bounded reclamation fixture publishes cancellation while worker remains captured");
    concurrency::SleepOnCurrentThread(20);
    PipelineRequest boundedWhileWorkerAlive =
        boundedPipeline.Request(MakeReference("graph/bounded-worker-owner.vgraph", unknownType));
    Check(!boundedWhileWorkerAlive.IsValid() && boundedPipeline.GetStats().knownOperations == 1u,
          "P4 retained-operation cap cannot recycle terminal storage still captured by a scheduled worker");
    graph.releaseSlow.Signal();
    for (u32 attempt = 0; attempt < 500 && boundedPipeline.GetStats().activeJobs != 0; ++attempt)
        concurrency::SleepOnCurrentThread(1);
    boundedCancelledWorker.Reset();
    PipelineRequest boundedAfterWorkerDrain =
        boundedPipeline.Request(MakeReference("graph/bounded-worker-owner.vgraph", unknownType));
    Check(boundedAfterWorkerDrain.TryWait(5000) && boundedAfterWorkerDrain.HasFailed() &&
              boundedAfterWorkerDrain.GetError() == Failure::UnknownType,
          "P4 retained-operation admission recovers after the captured worker returns");
    boundedAfterWorkerDrain.Reset();
    for (u32 attempt = 0; attempt < 500 && boundedPipeline.GetStats().activeJobs != 0; ++attempt)
        concurrency::SleepOnCurrentThread(1);
    Check(boundedPipeline.Shutdown(), "P4 bounded pipeline shuts down after reclaimed operation churn");
    Check(registry.Shutdown(), "registry shuts down after pipeline removal");
    Check(jobs::Shutdown(), "Vanguard Jobs shuts down after resource work drains");

    if (g_failures == 0)
    {
        std::puts("[resourcePipelineTests] Vanguard asynchronous resource "
                  "pipeline checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}

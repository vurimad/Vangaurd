#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/rendering/render_graph_cache.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/render_node_job.hpp>
#include <vanguard/rendering/viewport.hpp>

#include <cstring>

namespace
{
    namespace jobs = vanguard::jobs;
    namespace rendering = vanguard::rendering;
    namespace concurrency = vanguard::concurrency;
    using vanguard::u32;
    using vanguard::u64;

    using CheckFunction = void (*)(bool condition, const char* message) noexcept;

    class InvalidImportNode final : public rendering::RenderNodeImpl
    {
    public:
        bool DeclareResources(const rendering::RenderNodeImplContext& context, rendering::RenderFlowResourceFailure*) const noexcept override
        {
            static_cast<void>(context.RTInject("ImportedSource", {}));
            return true;
        }
        void Execute(const rendering::RenderNodeImplContext&, jobs::Builder*) const override {}
        rendering::RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return rendering::RenderNodeCommandListUsage::Require; }
    };

    class TestNode final : public rendering::RenderNodeImpl
    {
    public:
        explicit TestNode(concurrency::Atomic<u32>* const declarations = nullptr) noexcept : m_declarations(declarations) {}

        [[nodiscard]] bool DeclareResources(const rendering::RenderNodeImplContext&, rendering::RenderFlowResourceFailure*) const noexcept override
        {
            if (m_declarations != nullptr)
                static_cast<void>(m_declarations->Increment());
            return true;
        }

        void Execute(const rendering::RenderNodeImplContext&, jobs::Builder*) const override {}
        [[nodiscard]] rendering::RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return rendering::RenderNodeCommandListUsage::None; }

    private:
        concurrency::Atomic<u32>* m_declarations = nullptr;
    };

    class UnbalancedOccurrenceNode final : public rendering::RenderNodeImpl
    {
    public:
        [[nodiscard]] bool DeclareResources(const rendering::RenderNodeImplContext& context, rendering::RenderFlowResourceFailure*) const noexcept override
        {
            context.BeginNewNode();
            return true;
        }

        void Execute(const rendering::RenderNodeImplContext&, jobs::Builder*) const override {}
        [[nodiscard]] rendering::RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return rendering::RenderNodeCommandListUsage::None; }
    };

    [[nodiscard]] rendering::RenderNodeGraph::ItemId AddTestNode(rendering::NodeGraphFactory& factory, const rendering::NodeGroupId group = rendering::NodeGroupId::None,
                                                                 const rendering::RenderNodeType type = rendering::RenderNodeType::Stage, const u32 subtype = 0,
                                                                 concurrency::Atomic<u32>* const declarations = nullptr)
    {
        return factory.Create<TestNode>(group, type, rendering::RenderNodeSubtype(subtype), declarations);
    }

    [[nodiscard]] rendering::RenderGraphKey MakeGraphKey(const u64 featureMask) noexcept
    {
        rendering::RenderGraphKey key;
        key.outputKind = rendering::RenderViewportOutputKind::Headless;
        key.renderExtent = {1280, 720};
        key.outputExtent = {1280, 720};
        key.featureMask = featureMask;
        key.rendererRevision = 7;
        key.RebuildHash();
        return key;
    }

    [[nodiscard]] rendering::RenderViewGraphKey MakeViewKey(const u64 featureMask) noexcept
    {
        rendering::RenderViewGraphKey key;
        key.width = 1280;
        key.height = 720;
        key.featureMask = featureMask;
        key.RebuildHash();
        return key;
    }

    void CheckGraphAndFactory(CheckFunction check)
    {
        rendering::NodesContainer nodes;
        rendering::RenderNodeGraph graph;
        rendering::RenderNodeGraph::ItemId first = rendering::RenderNodeGraph::InvalidItemId;
        rendering::RenderNodeGraph::ItemId second = rendering::RenderNodeGraph::InvalidItemId;
        rendering::RenderNodeGraph::ItemId third = rendering::RenderNodeGraph::InvalidItemId;
        {
            rendering::NodeGraphFactory factory(&graph, &nodes);
            first = AddTestNode(factory);
            second = AddTestNode(factory);
            third = AddTestNode(factory);
            factory.Link(first, second, rendering::RenderNodeDependencyType::Cpu);
            factory.Link(first, third, rendering::RenderNodeDependencyType::Gpu);
        }

        check(first != second && second != third && graph.IsAllocated(first) && graph.IsAllocated(second) && graph.IsAllocated(third),
              "render graph assigns distinct allocated node identities");
        check(graph.GetNumNodes() == 3 && graph.GetNumDependencies() == 2 && graph.HasDependency(first, second, rendering::RenderNodeDependencyType::Cpu) &&
                  graph.HasDependency(first, third, rendering::RenderNodeDependencyType::Gpu),
              "render graph preserves independent CPU and GPU dependencies");
        graph.BuildRenderFlowGroups();
        check(graph.GetNodeContext(first).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)] <
                      graph.GetNodeContext(second).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)] &&
                  graph.GetNodeContext(first).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Gpu)] <
                      graph.GetNodeContext(third).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Gpu)],
              "flow-group construction respects both dependency domains");

        rendering::NodesContainer groupedNodes;
        rendering::RenderNodeGraph groupedGraph;
        const rendering::NodeGroupId group = static_cast<rendering::NodeGroupId>(1);
        rendering::RenderNodeGraph::ItemId groupMemberA = rendering::RenderNodeGraph::InvalidItemId;
        rendering::RenderNodeGraph::ItemId groupMemberB = rendering::RenderNodeGraph::InvalidItemId;
        rendering::RenderNodeGraph::ItemId beforeGroup = rendering::RenderNodeGraph::InvalidItemId;
        rendering::RenderNodeGraph::ItemId afterGroup = rendering::RenderNodeGraph::InvalidItemId;
        {
            rendering::NodeGraphFactory factory(&groupedGraph, &groupedNodes);
            groupMemberA = AddTestNode(factory, group);
            beforeGroup = AddTestNode(factory);
            factory.Link(beforeGroup, group, rendering::RenderNodeDependencyType::Cpu);
            groupMemberB = AddTestNode(factory, group);
            afterGroup = AddTestNode(factory);
            factory.Link(group, afterGroup, rendering::RenderNodeDependencyType::Cpu);
        }
        groupedGraph.BuildRenderFlowGroups();
        check(groupedGraph.GetNumNodes() == 6 &&
                  groupedGraph.GetNodeContext(beforeGroup).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)] <
                      groupedGraph.GetNodeContext(groupMemberA).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)] &&
                  groupedGraph.GetNodeContext(beforeGroup).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)] <
                      groupedGraph.GetNodeContext(groupMemberB).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)] &&
                  groupedGraph.GetNodeContext(groupMemberA).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)] <
                      groupedGraph.GetNodeContext(afterGroup).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)] &&
                  groupedGraph.GetNodeContext(groupMemberB).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)] <
                      groupedGraph.GetNodeContext(afterGroup).m_renderFlowGroups[static_cast<u32>(rendering::RenderNodeDependencyType::Cpu)],
              "factory group boundaries order existing and subsequently registered members");
    }

    void CheckGraphComposition(CheckFunction check)
    {
        rendering::NodesContainer baseNodes;
        rendering::RenderNodeGraph base;
        rendering::RenderNodeGraph::ItemId baseUnique = rendering::RenderNodeGraph::InvalidItemId;
        rendering::RenderNodeGraph::ItemId baseSequenceBegin = rendering::RenderNodeGraph::InvalidItemId;
        rendering::RenderNodeGraph::ItemId baseSequenceEnd = rendering::RenderNodeGraph::InvalidItemId;
        {
            rendering::NodeGraphFactory factory(&base, &baseNodes);
            baseUnique = AddTestNode(factory, rendering::NodeGroupId::None, rendering::RenderNodeType::Unique, 1);
            baseSequenceBegin = AddTestNode(factory, rendering::NodeGroupId::None, rendering::RenderNodeType::SequenceBegin, 2);
            baseSequenceEnd = AddTestNode(factory, rendering::NodeGroupId::None, rendering::RenderNodeType::SequenceEnd, 2);
            factory.Link(baseUnique, baseSequenceBegin, rendering::RenderNodeDependencyType::Cpu);
            factory.Link(baseSequenceBegin, baseSequenceEnd, rendering::RenderNodeDependencyType::Cpu);
        }

        rendering::NodesContainer viewNodes;
        rendering::RenderNodeGraph view;
        {
            rendering::NodeGraphFactory factory(&view, &viewNodes);
            const rendering::RenderNodeGraph::ItemId unique = AddTestNode(factory, rendering::NodeGroupId::None, rendering::RenderNodeType::Unique, 1);
            const rendering::RenderNodeGraph::ItemId sequenceBegin = AddTestNode(factory, rendering::NodeGroupId::None, rendering::RenderNodeType::SequenceBegin, 2);
            const rendering::RenderNodeGraph::ItemId stage = AddTestNode(factory);
            const rendering::RenderNodeGraph::ItemId sequenceEnd = AddTestNode(factory, rendering::NodeGroupId::None, rendering::RenderNodeType::SequenceEnd, 2);
            factory.Link(unique, sequenceBegin, rendering::RenderNodeDependencyType::Cpu);
            factory.Link(sequenceBegin, stage, rendering::RenderNodeDependencyType::Cpu);
            factory.Link(stage, sequenceEnd, rendering::RenderNodeDependencyType::Cpu);
        }

        rendering::RenderNodeGraph composed;
        composed.AddGraph(base, false, 0, false);
        composed.AddGraph(view, true, 5, true);
        check(composed.GetNumNodes() == 4, "graph composition merges matching unique and sequence boundaries without dropping the sequence body");
        u32 forcedViewNodes = 0;
        for (u32 index = 0; index < composed.GetNumNodes(); ++index)
            forcedViewNodes += composed.GetNodeContext(composed.GetNode(index)).m_cameraIndex == 5 ? 1u : 0u;
        check(forcedViewNodes == 2, "graph composition applies the requested view index to surviving imported occurrences");
        composed.RemoveHelperNodes();
        check(composed.GetNumNodes() == 2, "sequence helper removal retains the unique node and composed sequence body");
        composed.BuildRenderFlowGroups();
    }

    void CheckGraphCache(CheckFunction check)
    {
        rendering::RenderGraphCache cache;
        const rendering::RenderGraphKey firstKey = MakeGraphKey(1);
        bool needsRebuild = false;
        rendering::RenderGraphCache::CacheEntry* const first = cache.GetGraph(1, firstKey, 1, needsRebuild);
        check(first != nullptr && needsRebuild && first->cameraBuildData.Size() == 1, "graph cache miss allocates the requested per-view storage");
        if (first == nullptr)
            return;
        first->cameraBuildData[0]->cameraSetupKey = MakeViewKey(10);
        first->PostBuildClear();

        rendering::RenderGraphCache::CacheEntry* const hit = cache.GetGraph(2, firstKey, 1, needsRebuild);
        check(hit == first && !needsRebuild && hit->lastUsedFrame == 2, "graph cache hit preserves the completed entry and updates recency");

        rendering::RenderGraphKey collisionKey = firstKey;
        collisionKey.featureMask = 2;
        rendering::RenderGraphCache::CacheEntry* const collision = cache.GetGraph(3, collisionKey, 1, needsRebuild);
        check(collision != nullptr && collision != first && needsRebuild, "graph cache compares complete keys when hashes collide");
        if (collision != nullptr)
            collision->PostBuildClear();

        rendering::RenderGraphCache::CacheEntry* const incomplete = cache.GetGraph(4, MakeGraphKey(3), 0, needsRebuild);
        check(incomplete != nullptr && needsRebuild, "graph cache allocates a private entry for a build in progress");
        rendering::RenderGraphCache::CacheEntry* const retry = cache.GetGraph(5, MakeGraphKey(3), 0, needsRebuild);
        check(retry != nullptr && needsRebuild, "an unpublished graph build is never treated as a cache hit");
        if (retry != nullptr)
            retry->PostBuildClear();

        rendering::RenderGraphCache::CacheEntry* entries[rendering::RenderGraphCache::MaximumCacheEntries]{};
        cache.Clear();
        for (u32 index = 0; index < rendering::RenderGraphCache::MaximumCacheEntries; ++index)
        {
            entries[index] = cache.GetGraph(index + 1, MakeGraphKey(index + 10), 0, needsRebuild);
            if (entries[index] != nullptr)
                entries[index]->PostBuildClear();
        }
        static_cast<void>(cache.GetGraph(10, MakeGraphKey(10), 0, needsRebuild));
        concurrency::Atomic<bool> evictionStarted{false};
        rendering::RenderGraphCache::CacheEntry* replacement = nullptr;
        bool replacementNeedsRebuild = false;
        entries[1]->graph.AcquireExclusiveUpdateFlag();
        jobs::Builder evictionBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        static jobs::JobName evictionName{"RenderingTests.RenderGraphCacheEviction"};
        jobs::Task evictionTask = jobs::Task::Create([&](const jobs::JobContext&) noexcept
        {
            evictionStarted.SetValue(true);
            replacement = cache.GetGraph(11, MakeGraphKey(20), 0, replacementNeedsRebuild);
        });
        const bool evictionDispatched = evictionBuilder.IsValid() && evictionTask && evictionBuilder.Dispatch(evictionName, static_cast<jobs::Task&&>(evictionTask));
        jobs::Counter evictionComplete = evictionBuilder.ExtractCounter();
        check(evictionDispatched && evictionComplete.IsValid(), "graph cache retained-eviction task dispatch");
        u32 evictionStartWait = 0;
        while (evictionDispatched && !evictionStarted.GetValue() && evictionStartWait < 10000)
        {
            concurrency::SleepOnCurrentThread(1);
            ++evictionStartWait;
        }
        check(evictionStarted.GetValue() && !evictionComplete.IsReady(), "graph cache cannot evict an entry retained by active execution");
        entries[1]->graph.ReleaseExclusiveUpdateFlag();
        const bool evictionJoined = evictionComplete.IsValid() && evictionComplete.Wait();
        check(evictionJoined && replacement == entries[1] && replacementNeedsRebuild, "graph cache evicts the least-recently-used entry after execution releases it");
        cache.Clear();
        check(cache.GetGraph(12, firstKey, 1, needsRebuild) != nullptr && needsRebuild, "graph cache invalidation forces reconstruction");
    }

    void CheckDeclarationPermutation(const u32 nodeCount, CheckFunction check)
    {
        rendering::NodesContainer nodes;
        rendering::RenderNodeGraph graph;
        concurrency::Atomic<u32> declarations{0};
        {
            rendering::NodeGraphFactory factory(&graph, &nodes);
            for (u32 index = 0; index < nodeCount; ++index)
                static_cast<void>(AddTestNode(factory, rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, 0, &declarations));
        }
        graph.BuildRenderFlowGroups();

        rendering::RenderFlowResourceAllocatorConfig config;
        config.allowLogicalOnlyValidation = true;
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::RenderNodeResourceBindings bindings;
        rendering::RenderNodeResourcePreparationFailures failures;
        const bool initialized = allocator.Initialize(config, &failure) && allocator.BeginFrame(nodeCount, {}, &failure) && graph.PrepareResourceBindings(bindings, failures);
        check(initialized, nodeCount == 1021 ? "1021-node declaration traversal setup" : "2042-node declaration traversal setup");
        if (!initialized)
            return;

        rendering::RenderFrameInfo frame;
        rendering::RenderNodeImplContext context;
        rendering::RenderNodeImplContext::InitData initData(0);
        initData.frame = &frame;
        context.Init(initData);
        jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        graph.PrepareResourcesParallel(context, allocator, bindings, failures, builder);
        jobs::Counter completion = builder.ExtractCounter();
        const bool joined = completion.IsValid() && completion.Wait();
        check(joined && !failures.HasFailure() && declarations.GetValue() == nodeCount,
              nodeCount == 1021 ? "1021-node permutation declares every occurrence exactly once" : "2042-node permutation declares every occurrence exactly once");
        allocator.CancelBeforePublication();
        static_cast<void>(allocator.Shutdown());
    }

    void CheckInvalidImportDeclaration(CheckFunction check)
    {
        rendering::NodesContainer nodes;
        rendering::RenderNodeGraph graph;
        {
            rendering::NodeGraphFactory factory(&graph, &nodes);
            static_cast<void>(factory.Create<InvalidImportNode>(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0)));
        }
        graph.BuildRenderFlowGroups();
        rendering::RenderFlowResourceAllocatorConfig config;
        config.allowLogicalOnlyValidation = true;
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::RenderNodeResourceBindings bindings;
        rendering::RenderNodeResourcePreparationFailures failures;
        const bool initialized = allocator.Initialize(config, &failure) && allocator.BeginFrame(1, {}, &failure) && graph.PrepareResourceBindings(bindings, failures);
        check(initialized, "import declaration setup");
        if (!initialized)
            return;
        rendering::RenderFrameInfo frame;
        rendering::RenderNodeImplContext context;
        rendering::RenderNodeImplContext::InitData initData(0);
        initData.frame = &frame;
        context.Init(initData);
        jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        graph.PrepareResourcesParallel(context, allocator, bindings, failures, builder);
        check(builder.WaitForCompletion() && failures.HasFailure(), "graph declaration propagates an unregistered texture import");
        allocator.CancelBeforePublication();
        check(allocator.Shutdown(), "invalid import declaration cleanup");
    }

    void CheckPreSchedulingCapacityFailure(CheckFunction check)
    {
        rendering::NodesContainer nodes;
        rendering::RenderNodeGraph graph;
        concurrency::Atomic<u32> declarations{0};
        {
            rendering::NodeGraphFactory factory(&graph, &nodes);
            static_cast<void>(AddTestNode(factory, rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, 0, &declarations));
            static_cast<void>(AddTestNode(factory, rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, 0, &declarations));
        }
        graph.BuildRenderFlowGroups();

        rendering::RenderFlowResourceAllocatorConfig config;
        config.maximumPlanningWriters = 1;
        config.allowLogicalOnlyValidation = true;
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::RenderNodeResourceBindings bindings;
        rendering::RenderNodeResourcePreparationFailures failures;
        const bool initialized = allocator.Initialize(config, &failure) && allocator.BeginFrame(3, {}, &failure) && graph.PrepareResourceBindings(bindings, failures);
        check(initialized, "pre-scheduling capacity-failure setup");
        if (!initialized)
            return;

        rendering::RenderFrameInfo frame;
        rendering::RenderNodeImplContext context;
        rendering::RenderNodeImplContext::InitData initData(0);
        initData.frame = &frame;
        context.Init(initData);
        jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        graph.PrepareResourcesParallel(context, allocator, bindings, failures, builder);
        const rendering::RenderFlowResourceFailure preparationFailure = failures.GetFailure();
        check(preparationFailure.code == rendering::RenderFlowResourceFailureCode::CapacityExceeded && declarations.GetValue() == 0 &&
                  allocator.GetState() == rendering::RenderFlowResourceSessionState::Planning,
              "planning-capacity exhaustion fails before dispatching any node declaration");
        allocator.CancelBeforePublication();
        check(allocator.GetState() == rendering::RenderFlowResourceSessionState::Idle && allocator.Shutdown(&failure),
              "pre-scheduling capacity failure cancels without publishing execution state");
    }

} // namespace

int RunRenderGraphFatalCase(const char* name) noexcept
{
    if (name == nullptr)
        return 100;

    if (std::strcmp(name, "cpu-cycle") == 0 || std::strcmp(name, "gpu-cycle") == 0)
    {
        rendering::NodesContainer nodes;
        rendering::RenderNodeGraph graph;
        {
            rendering::NodeGraphFactory factory(&graph, &nodes);
            const rendering::RenderNodeGraph::ItemId first = AddTestNode(factory);
            const rendering::RenderNodeGraph::ItemId second = AddTestNode(factory);
            const rendering::RenderNodeDependencyType dependencyType = std::strcmp(name, "cpu-cycle") == 0 ? rendering::RenderNodeDependencyType::Cpu : rendering::RenderNodeDependencyType::Gpu;
            factory.Link(first, second, dependencyType);
            factory.Link(second, first, dependencyType);
        }
        graph.BuildRenderFlowGroups();
        return 101;
    }

    if (std::strcmp(name, "invalid-group") == 0)
    {
        rendering::NodesContainer nodes;
        rendering::RenderNodeGraph graph;
        rendering::NodeGraphFactory factory(&graph, &nodes);
        static_cast<void>(AddTestNode(factory, static_cast<rendering::NodeGroupId>(rendering::NodeGraphFactory::MaximumGroupCount)));
        return 102;
    }

    if (std::strcmp(name, "unbalanced-occurrence") == 0)
    {
        rendering::NodesContainer nodes;
        rendering::RenderNodeGraph graph;
        {
            rendering::NodeGraphFactory factory(&graph, &nodes);
            static_cast<void>(factory.Create<UnbalancedOccurrenceNode>(rendering::NodeGroupId::None, rendering::RenderNodeType::Stage, rendering::RenderNodeSubtype(0)));
        }
        graph.BuildRenderFlowGroups();

        rendering::RenderFlowResourceAllocatorConfig config;
        config.allowLogicalOnlyValidation = true;
        rendering::RenderFlowResourceAllocator allocator;
        rendering::RenderFlowResourceFailure failure;
        rendering::RenderNodeResourceBindings bindings;
        rendering::RenderNodeResourcePreparationFailures preparationFailures;
        if (!allocator.Initialize(config, &failure) || !allocator.BeginFrame(1, {}, &failure) || !graph.PrepareResourceBindings(bindings, preparationFailures))
            return 110;

        rendering::RenderFrameInfo frame;
        rendering::RenderNodeImplContext baseContext;
        rendering::RenderNodeImplContext::InitData initData(0);
        initData.frame = &frame;
        baseContext.Init(initData);

        jobs::Builder declarationBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        graph.PrepareResourcesParallel(baseContext, allocator, bindings, preparationFailures, declarationBuilder);
        if (!declarationBuilder.WaitForCompletion() || preparationFailures.HasFailure())
            return 111;
        if (!allocator.SealPlanning(rendering::PlanningJoinToken::CompletedSynchronously(), &failure) || !allocator.Resolve(nullptr, &failure) || !allocator.BeginExecution(&failure))
            return 112;

        rendering::RenderFrameCommandLists frameCommandLists;
        frameCommandLists.PrepareForFrame(static_cast<u32>(rendering::ReservedFrameCommandList::Count));
        if (!graph.PrepareExecutionPackets(allocator, frameCommandLists, bindings, &failure))
            return 113;

        jobs::Builder kickoffBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        static jobs::JobName kickoffName{"RenderingTests.MalformedNodeKickoff"};
        jobs::Task kickoffTask = jobs::Task::Create([](const jobs::JobContext&) noexcept {});
        if (!kickoffTask || !kickoffBuilder.Dispatch(kickoffName, static_cast<jobs::Task&&>(kickoffTask)))
            return 114;
        jobs::Counter kickoff = kickoffBuilder.ExtractCounter();
        if (!kickoff.IsValid() || !kickoff.Wait())
            return 115;

        concurrency::Atomic<bool> completed{false};
        concurrency::Atomic<bool> schedulingFailed{false};
        jobs::Builder setupBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        static jobs::JobName setupName{"RenderingTests.MalformedNodeSetup"};
        jobs::Task setupTask = jobs::Task::Create([&](const jobs::JobContext& jobContext) noexcept
        {
            jobs::Task completion = jobs::Task::Create([&completed](const jobs::JobContext&) noexcept { completed.SetValue(true); });
            rendering::RenderFlowResourceFailure schedulingFailure;
            if (!completion || !rendering::RenderNodeJob::RunRenderNodeJobs(graph, baseContext, bindings, kickoff, jobContext, static_cast<jobs::Task&&>(completion), &schedulingFailure))
                schedulingFailed.SetValue(true);
        });
        if (!setupTask || !setupBuilder.Dispatch(setupName, static_cast<jobs::Task&&>(setupTask)) || !setupBuilder.WaitForCompletion())
            return 116;
        for (u32 wait = 0; wait < 10000 && !completed.GetValue() && !schedulingFailed.GetValue(); ++wait)
            concurrency::SleepOnCurrentThread(1);
        return completed.GetValue() ? 117 : 118;
    }

    if (std::strcmp(name, "invalid-submission-sync") == 0)
    {
        rendering::RenderFrameCommandLists frameCommandLists;
        frameCommandLists.PrepareForFrame(1);
        jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        if (!frameCommandLists.Submit("RenderingTests.InvalidSubmissionSync", 0, vanguard::rhi::CommandListSyncType::ForkAsyncCompute, builder))
            return 120;
        return builder.WaitForCompletion() ? 121 : 122;
    }

    return 103;
}

void RunRenderGraphTests(void (*check)(bool condition, const char* message) noexcept) noexcept
{
    CheckGraphAndFactory(check);
    CheckGraphComposition(check);
    CheckGraphCache(check);
    CheckDeclarationPermutation(1021, check);
    CheckDeclarationPermutation(2042, check);
    CheckPreSchedulingCapacityFailure(check);
    CheckInvalidImportDeclaration(check);
}

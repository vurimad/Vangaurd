#include <vanguard/rendering/render_node_job.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        concurrency::Atomic<const RenderFrameInfo*> g_jobsRenderFrame;

        class RenderNodeBuilderStorage final
        {
        public:
            ~RenderNodeBuilderStorage()
            {
                Reset();
            }

            [[nodiscard]] bool Initialize(const u32 count) noexcept
            {
                if (count == 0)
                    return true;
                m_block = memory::Allocate(memory::PoolId::Rendering, static_cast<usize>(count) * sizeof(jobs::Builder), alignof(jobs::Builder));
                if (!m_block)
                    return false;
                m_builders = static_cast<jobs::Builder*>(m_block.address);
                for (; m_count < count; ++m_count)
                {
                    jobs::Builder* const builder = new (m_builders + m_count) jobs::Builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
                    if (!builder->IsValid())
                    {
                        ++m_count;
                        Reset();
                        return false;
                    }
                }
                return true;
            }

            [[nodiscard]] jobs::Builder& operator[](const u32 index) noexcept
            {
                VG_ASSERT(index < m_count);
                return m_builders[index];
            }

        private:
            void Reset() noexcept
            {
                while (m_count != 0)
                    m_builders[--m_count].~Builder();
                m_builders = nullptr;
                if (m_block)
                    memory::Free(m_block);
                m_block = {};
            }

            memory::MemoryBlock m_block;
            jobs::Builder* m_builders = nullptr;
            u32 m_count = 0;
        };

    }

    void RenderNodeJob::SetJobsRenderFrame(const RenderFrameInfo* const frame) noexcept
    {
        if (frame == nullptr)
        {
            g_jobsRenderFrame.SetValue(nullptr);
            return;
        }

        const RenderFrameInfo* const previous = g_jobsRenderFrame.CompareExchange(frame, nullptr);
        if (previous != nullptr)
            VG_FATAL("a render frame is already installed for render-node jobs");
    }

    void RenderNodeJob::ClearJobsRenderFrame(const RenderFrameInfo* const expectedFrame) noexcept
    {
        if (expectedFrame == nullptr)
            VG_FATAL("render-node job frame cleanup requires the expected frame");
        const RenderFrameInfo* const previous = g_jobsRenderFrame.CompareExchange(nullptr, expectedFrame);
        if (previous != expectedFrame)
            VG_FATAL("render-node job frame cleanup does not match the installed frame");
    }

    const RenderFrameInfo* RenderNodeJob::GetJobsRenderFrame() noexcept
    {
        return g_jobsRenderFrame.GetValue();
    }

    bool RenderNodeJob::RunRenderNodeJobs(const RenderNodeGraph& graph, const RenderNodeImplContext& baseContext,
                                          RenderNodeResourceBindings& resourceBindings, const jobs::Counter& kickoff,
                                          const jobs::JobContext& setupContext, jobs::Task&& completion,
                                          RenderFlowResourceFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        const u32 nodeCount = graph.m_nodes.GetNumItems();
        if (!graph.m_isBuilt || !kickoff.IsValid() || !completion || resourceBindings.m_entries.Size() != nodeCount)
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::IncompleteExecution;
                failure->phase = RenderFlowResourceSessionState::Executing;
                failure->message = "render-node job setup is incomplete";
            }
            return false;
        }

        containers::DynamicArray<RenderNodeGraph::ItemId> orderedNodes{memory::pools::Rendering::GetInstance()};
        graph.BuildFlattenedNodesArray(orderedNodes, RenderNodeDependencyType::Cpu);
        if (orderedNodes.Size() != nodeCount)
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::BackendContractViolation;
                failure->phase = RenderFlowResourceSessionState::Executing;
                failure->message = "render-node CPU graph is not a complete acyclic ordering";
            }
            return false;
        }
        containers::DynamicArray<jobs::Task> nodeTasks{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<jobs::Counter> nodeCounters{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<u32> incomingCounts{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<u32> outgoingCounts{memory::pools::Rendering::GetInstance()};
        nodeTasks.Resize(nodeCount);
        nodeCounters.Resize(nodeCount);
        incomingCounts.Resize(nodeCount);
        outgoingCounts.Resize(nodeCount);
        for (u32 index = 0; index < nodeCount; ++index)
        {
            incomingCounts[index] = 0;
            outgoingCounts[index] = 0;
        }

        for (u32 dependencyIndex = 0; dependencyIndex < graph.m_dependencies.GetNumItems(); ++dependencyIndex)
        {
            const RenderNodeGraph::DependencyData& dependency = graph.m_dependencies[graph.m_dependencies.GetItemIdByUsageIndex(dependencyIndex)];
            if (dependency.m_type != RenderNodeDependencyType::Cpu)
                continue;
            ++outgoingCounts[graph.GetNodeIndex(dependency.m_nodes[static_cast<u32>(RenderNodeGraph::RelationType::Parent)])];
            ++incomingCounts[graph.GetNodeIndex(dependency.m_nodes[static_cast<u32>(RenderNodeGraph::RelationType::Child)])];
        }

        u32 rootCount = 0;
        u32 leafCount = 0;
        for (u32 index = 0; index < nodeCount; ++index)
        {
            rootCount += incomingCounts[index] == 0 ? 1u : 0u;
            leafCount += outgoingCounts[index] == 0 ? 1u : 0u;
        }
        if (nodeCount != 0 && (rootCount == 0 || leafCount != 1))
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::BackendContractViolation;
                failure->phase = RenderFlowResourceSessionState::Executing;
                failure->message = rootCount == 0 ? "render-node CPU graph has no root" : "render-node CPU graph does not have exactly one terminal leaf";
            }
            return false;
        }

        static jobs::JobName occurrenceName{"RenderGraph/Node"};
        for (u32 orderIndex = 0; orderIndex < nodeCount; ++orderIndex)
        {
            const RenderNodeGraph::ItemId nodeId = orderedNodes[orderIndex];
            const u32 usageIndex = graph.GetNodeIndex(nodeId);
            RenderNodeResourceBindings::Entry& entry = resourceBindings.m_entries[usageIndex];
            if (entry.node != RenderFlowNodeId{nodeId} || entry.executionContext == nullptr)
            {
                if (failure != nullptr)
                {
                    failure->code = RenderFlowResourceFailureCode::IncompleteExecution;
                    failure->phase = RenderFlowResourceSessionState::Executing;
                    failure->node = RenderFlowNodeId{nodeId};
                    failure->message = "render-node occurrence has no execution context";
                }
                return false;
            }

            entry.executionContext->~RenderNodeImplContext();
            static_cast<void>(new (entry.executionContext) RenderNodeImplContext(baseContext, 0));
            entry.executionContext->SetupNodeData(graph.m_nodes[nodeId].m_renderNodeContext, graph.m_nodes[nodeId].m_renderNodeParameters);
            nodeTasks[usageIndex] = jobs::Task::Create([node = &graph.m_nodes[nodeId], entryPointer = &entry](const jobs::JobContext& jobContext) noexcept
            {
                RenderNodeImpl* const impl = node->m_renderNodeParameters.m_impl;
                if (impl == nullptr)
                    return;
                // Setup failures are published before kickoff; no recording has begun for this occurrence.
                if (entryPointer->executionFailure.code != RenderFlowResourceFailureCode::None)
                    return;
                RenderNodeImplContext& context = *entryPointer->executionContext;
                context.m_dispatcherThreadIndex = jobContext.dispatcherThreadIndex;
                context.BeginResourceExecution(entryPointer->packet.IsValid() ? &entryPointer->packet : nullptr, entryPointer->cursor, entryPointer->resources,
                                               entryPointer->executionFailure);
                if (!impl->GetJobBuilderUsage())
                {
                    impl->Process(context, nullptr);
                    return;
                }
                jobs::Builder occurrenceBuilder(jobContext);
                if (!occurrenceBuilder.IsValid())
                    VG_FATAL("render-node continuation builder creation failed");
                impl->Process(context, &occurrenceBuilder);
            });
            if (!nodeTasks[usageIndex])
            {
                if (failure != nullptr)
                {
                    failure->code = RenderFlowResourceFailureCode::CapacityExceeded;
                    failure->phase = RenderFlowResourceSessionState::Executing;
                    failure->node = RenderFlowNodeId{nodeId};
                    failure->message = "render-node task allocation failed";
                }
                return false;
            }
        }

        RenderNodeBuilderStorage nodeBuilders;
        jobs::Builder finishBuilder(setupContext);
        if (!nodeBuilders.Initialize(nodeCount) || !finishBuilder.IsValid())
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::CapacityExceeded;
                failure->phase = RenderFlowResourceSessionState::Executing;
                failure->message = "render-node branch builder allocation failed";
            }
            return false;
        }

        for (u32 orderIndex = 0; orderIndex < nodeCount; ++orderIndex)
        {
            const RenderNodeGraph::ItemId nodeId = orderedNodes[orderIndex];
            const u32 usageIndex = graph.GetNodeIndex(nodeId);
            jobs::Builder& nodeBuilder = nodeBuilders[usageIndex];
            if (incomingCounts[usageIndex] == 0)
                nodeBuilder.AddDependency(kickoff);
            for (RenderNodeGraph::ItemId dependency = graph.m_nodes[nodeId].m_dependencies[static_cast<u32>(RenderNodeGraph::RelationType::Parent)][static_cast<u32>(RenderNodeDependencyType::Cpu)];
                 dependency != RenderNodeGraph::InvalidItemId;
                 dependency = graph.m_dependencies[dependency].m_nextDependencies[static_cast<u32>(RenderNodeGraph::RelationType::Child)])
            {
                const RenderNodeGraph::ItemId parentNode = graph.m_dependencies[dependency].m_nodes[static_cast<u32>(RenderNodeGraph::RelationType::Parent)];
                nodeBuilder.AddDependency(nodeCounters[graph.GetNodeIndex(parentNode)]);
            }
            if (!nodeBuilder.Dispatch(occurrenceName, static_cast<jobs::Task&&>(nodeTasks[usageIndex])))
                VG_FATAL("render-node task dispatch failed after scheduling began");
            nodeCounters[usageIndex] = nodeBuilder.ExtractCounter();
            if (!nodeCounters[usageIndex].IsValid())
                VG_FATAL("render-node completion counter extraction failed after scheduling began");
        }

        if (nodeCount == 0)
            finishBuilder.AddDependency(kickoff);
        else
            for (u32 index = 0; index < nodeCount; ++index)
                if (outgoingCounts[index] == 0)
                    finishBuilder.AddDependency(nodeCounters[index]);
        static jobs::JobName completionName{"RenderGraph/NodesComplete"};
        if (!finishBuilder.Dispatch(completionName, static_cast<jobs::Task&&>(completion)))
            VG_FATAL("render-node terminal continuation dispatch failed after scheduling began");
        return true;
    }
} // namespace vanguard::rendering

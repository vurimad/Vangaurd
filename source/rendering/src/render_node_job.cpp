#include <vanguard/rendering/render_node_job.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/profiling/profiling.hpp>
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

            template <typename DebugUserDataProvider>
            [[nodiscard]] bool Initialize(const u32 count, DebugUserDataProvider&& debugUserDataProvider) noexcept
            {
                if (count == 0)
                    return true;
                m_block = memory::Allocate(memory::PoolId::Rendering, static_cast<usize>(count) * sizeof(jobs::Builder), alignof(jobs::Builder));
                if (!m_block)
                    return false;
                m_builders = static_cast<jobs::Builder*>(m_block.address);
                for (; m_count < count; ++m_count)
                {
                    jobs::Builder* const builder = new (m_builders + m_count)
                        jobs::Builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker}, debugUserDataProvider(m_count));
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
        VG_PROFILE_ZONE_NAMED("RenderGraph/ScheduleNodes");
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

        if (graph.m_cpuOrderedNodes.Size() != nodeCount || graph.m_cpuParentOffsets.Size() != nodeCount + 1u ||
            graph.m_cpuParentOffsets[nodeCount] != graph.m_cpuParentUsageIndices.Size())
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::BackendContractViolation;
                failure->phase = RenderFlowResourceSessionState::Executing;
                failure->message = "render-node CPU graph has no complete compiled execution topology";
            }
            return false;
        }
        containers::DynamicArray<jobs::Counter> nodeCounters{memory::pools::Rendering::GetInstance()};
        nodeCounters.Resize(nodeCount);
        if (nodeCount != 0 && (graph.m_cpuRootCount == 0 || graph.m_cpuLeafCount != 1 || graph.m_cpuTerminalLeafUsageIndex >= nodeCount))
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::BackendContractViolation;
                failure->phase = RenderFlowResourceSessionState::Executing;
                failure->message = graph.m_cpuRootCount == 0 ? "render-node CPU graph has no root" : "render-node CPU graph does not have exactly one terminal leaf";
            }
            return false;
        }

        static jobs::JobName occurrenceName{"RenderGraph/Node"};
        for (u32 orderIndex = 0; orderIndex < nodeCount; ++orderIndex)
        {
            const RenderNodeGraph::ItemId nodeId = graph.m_cpuOrderedNodes[orderIndex];
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
            entry.executionImpl = graph.m_nodes[nodeId].m_renderNodeParameters.m_impl;
        }

        RenderNodeBuilderStorage nodeBuilders;
        jobs::Builder finishBuilder(setupContext);
        if (!nodeBuilders.Initialize(nodeCount, [&](const u32 index) noexcept -> const void*
            {
                return &resourceBindings.m_entries[index];
            }) ||
            !finishBuilder.IsValid())
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::CapacityExceeded;
                failure->phase = RenderFlowResourceSessionState::Executing;
                failure->message = "render-node branch builder allocation failed";
            }
            return false;
        }

        const auto executeNode = [](void* const state, const jobs::JobContext& jobContext) noexcept
        {
            auto* const entryPointer = static_cast<RenderNodeResourceBindings::Entry*>(state);
            RenderNodeImpl* const impl = entryPointer->executionImpl;
            if (impl == nullptr)
                return;
            VG_PROFILE_ZONE_DYNAMIC_VALUE(impl->GetName(), reinterpret_cast<std::uintptr_t>(entryPointer));
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
        };

        for (u32 orderIndex = 0; orderIndex < nodeCount; ++orderIndex)
        {
            const RenderNodeGraph::ItemId nodeId = graph.m_cpuOrderedNodes[orderIndex];
            const u32 usageIndex = graph.GetNodeIndex(nodeId);
            jobs::Builder& nodeBuilder = nodeBuilders[usageIndex];
            const u32 parentBegin = graph.m_cpuParentOffsets[usageIndex];
            const u32 parentEnd = graph.m_cpuParentOffsets[usageIndex + 1u];
            if (parentBegin == parentEnd)
                nodeBuilder.AddDependency(kickoff);
            for (u32 parentIndex = parentBegin; parentIndex < parentEnd; ++parentIndex)
                nodeBuilder.AddDependency(nodeCounters[graph.m_cpuParentUsageIndices[parentIndex]]);
            jobs::Task nodeTask = jobs::Task::CreateBorrowed(&resourceBindings.m_entries[usageIndex], executeNode);
            if (!nodeBuilder.Dispatch(occurrenceName, static_cast<jobs::Task&&>(nodeTask)))
                VG_FATAL("render-node task dispatch failed after scheduling began");
            nodeCounters[usageIndex] = nodeBuilder.ExtractCounter();
            if (!nodeCounters[usageIndex].IsValid())
                VG_FATAL("render-node completion counter extraction failed after scheduling began");
        }

        if (nodeCount == 0)
            finishBuilder.AddDependency(kickoff);
        else
            finishBuilder.AddDependency(nodeCounters[graph.m_cpuTerminalLeafUsageIndex]);
        static jobs::JobName completionName{"RenderGraph/NodesComplete"};
        if (!finishBuilder.Dispatch(completionName, static_cast<jobs::Task&&>(completion)))
            VG_FATAL("render-node terminal continuation dispatch failed after scheduling began");
        return true;
    }
} // namespace vanguard::rendering

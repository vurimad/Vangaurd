#include <vanguard/rendering/render_node_graph.hpp>

#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_flow_resource_internal.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/jobs/jobs.hpp>

#include <vanguard/system/assert.hpp>

#include <numeric>

namespace vanguard::rendering
{
    namespace
    {
        template <typename Array>
        [[nodiscard]] RenderNodeGraph::ItemId GetReindexedItemId(const Array& target, const Array& source, const RenderNodeGraph::ItemId sourceId)
        {
            return sourceId != RenderNodeGraph::InvalidItemId ? target.GetReindexedItemId(source, sourceId) : RenderNodeGraph::InvalidItemId;
        }

        [[nodiscard]] constexpr bool IsHelperNodeType(const RenderNodeType type) noexcept
        {
            return type >= RenderNodeType::SequenceBegin && type <= RenderNodeType::Temporary;
        }

        void SwapItemIds(RenderNodeGraph::ItemId& first, RenderNodeGraph::ItemId& second) noexcept
        {
            const RenderNodeGraph::ItemId value = first;
            first = second;
            second = value;
        }

    } // namespace

    void RenderNodeResourcePreparationFailures::Record(const RenderFlowResourceFailure& failure) noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        if (m_failure.code == RenderFlowResourceFailureCode::None)
            m_failure = failure;
    }

    bool RenderNodeResourcePreparationFailures::HasFailure() const noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        return m_failure.code != RenderFlowResourceFailureCode::None;
    }

    RenderFlowResourceFailure RenderNodeResourcePreparationFailures::GetFailure() const noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        return m_failure;
    }

    rhi::CommandListRef RenderNodeImpl::CreateCommandList() const
    {
        VG_FATAL("render node does not support command-list creation");
    }

    void RenderNodeImpl::Process(const RenderNodeImplContext& context, jobs::Builder* const builder) const
    {
        if (context.GetDispatcherThreadIndex() == ~u32{0})
            VG_FATAL("render-node processing requires a dispatcher thread index");

        if (context.IsDeclaringResources())
        {
            context.BeginNewNode();
            RenderFlowResourceFailure& failure = context.GetResourceFailure();
            if (!DeclareResources(context, &failure) && failure.code == RenderFlowResourceFailureCode::None)
            {
                failure.code = RenderFlowResourceFailureCode::IncompletePlanning;
                failure.phase = RenderFlowResourceSessionState::Planning;
                failure.message = "render node resource declaration failed without failure evidence";
            }
            return;
        }

        RenderNodeImplContext& executionContext = const_cast<RenderNodeImplContext&>(context);
        if (executionContext.m_operation != RenderNodeImplContext::Operation::Execute)
            VG_FATAL("render-node execution processing requires initialized occurrence state");
        const RenderNodeCommandListUsage commandListUsage = GetCommandListUsage();
        const bool usesCommandList = HasCommandList(commandListUsage);
        jobs::Task epilogue;
        if (GetJobBuilderUsage())
        {
            if (builder == nullptr || !builder->IsValid())
                VG_FATAL("render-node epilogue requires a valid continuation builder");
            // Allocate cleanup before Execute can issue children that borrow this occurrence.
            epilogue = jobs::Task::Create([this, contextPointer = &context](const jobs::JobContext&) noexcept { ProcessEpilogue(*contextPointer); });
            if (!epilogue)
                VG_FATAL("render-node epilogue allocation failed");
        }

        if (commandListUsage == RenderNodeCommandListUsage::Own)
        {
            const rhi::CommandListRef commandList = CreateCommandList();
            if (!commandList.IsValid())
                VG_FATAL("render node failed to create its command list");
            context.SetCommandList(commandList);
        }

        if (usesCommandList || executionContext.m_executionPacket != nullptr)
        {
            if (!context.HasCommandList())
                VG_FATAL("render node has no command list for its flow group");
            rhi::Failure rhiFailure;
            if (!rhi::BindCommandList(context.GetCommandList(), &rhiFailure))
                VG_FATAL(rhiFailure.message[0] != '\0' ? rhiFailure.message : "render node failed to bind its command list");
            if (executionContext.m_executionPacket == nullptr)
                VG_FATAL("command-list render node has no compiled execution packet");
            if (!executionContext.OpenResourceExecutionPacket())
                VG_FATAL(executionContext.m_resourceFailure->message != nullptr ? executionContext.m_resourceFailure->message : "render-node packet opening failed");
            if (usesCommandList)
                BeginProfilerBlock();
        }

        context.BeginNewNode();
        if (!usesCommandList && executionContext.m_executionPacket != nullptr)
            rhi::UnbindCommandList();
        Execute(context, builder);

        if (usesCommandList)
            rhi::UnbindCommandList();
        if (epilogue)
        {
            static jobs::JobName epilogueName{"RenderGraph/NodeEpilogue"};
            if (!builder->Dispatch(epilogueName, static_cast<jobs::Task&&>(epilogue)))
                VG_FATAL("preallocated render-node epilogue could not be dispatched by its valid continuation builder");
            return;
        }
        ProcessEpilogue(context);
    }

    void RenderNodeImpl::ProcessEpilogue(const RenderNodeImplContext& context) const
    {
        RenderNodeImplContext& executionContext = const_cast<RenderNodeImplContext&>(context);
        // Packetless occurrences own their context too, but must keep it alive
        // through any child continuations before completing it.
        if (executionContext.m_executionPacket == nullptr)
        {
            executionContext.EndResourceExecution();
            return;
        }
        if (!context.HasCommandList())
        {
            executionContext.FailResourceOperation(RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "render-node epilogue has no command list");
        }
        else
        {
            rhi::Failure rhiFailure;
            if (!rhi::BindCommandList(context.GetCommandList(), &rhiFailure))
                executionContext.FailResourceOperation(detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                                       "render-node epilogue failed to bind its command list");
        }

        executionContext.EndNodeResourceUses();
        if (HasCommandList(GetCommandListUsage()))
            EndProfilerBlock();
        if (GetCommandListUsage() == RenderNodeCommandListUsage::Own)
            executionContext.EndResourceExecution();
        rhi::UnbindCommandList();
    }

    void RenderNodeImpl::BeginProfilerBlock() const
    {
    }

    void RenderNodeImpl::EndProfilerBlock() const
    {
    }

    RenderNodeGraph::RenderNodeData::RenderNodeData() noexcept
    {
        for (u32 relation = 0; relation < static_cast<u32>(RelationType::Count); ++relation)
            for (u32 dependencyType = 0; dependencyType < RenderNodeDependencyTypeCount; ++dependencyType)
                m_dependencies[relation][dependencyType] = InvalidItemId;
    }

    bool RenderNodeGraph::RenderNodeData::HasAnyDependency() const noexcept
    {
        for (u32 relation = 0; relation < static_cast<u32>(RelationType::Count); ++relation)
            for (u32 dependencyType = 0; dependencyType < RenderNodeDependencyTypeCount; ++dependencyType)
                if (m_dependencies[relation][dependencyType] != InvalidItemId)
                    return true;
        return false;
    }

    bool RenderNodeGraph::RenderNodeData::HasAnyDependency(const RelationType relation, const RenderNodeDependencyType dependencyType) const noexcept
    {
        return m_dependencies[static_cast<u32>(relation)][static_cast<u32>(dependencyType)] != InvalidItemId;
    }

    RenderNodeGraph::RenderNodeGraph() : m_nodes(0, 250), m_dependencies(0, 500) {}

    bool RenderNodeGraph::IsAllocated(const ItemId id) const noexcept
    {
        VG_ASSERT(NodesArray::IsValidId(id) != DependenciesArray::IsValidId(id));
        return NodesArray::IsValidId(id) ? m_nodes.IsAllocated(id) : m_dependencies.IsAllocated(id);
    }

    RenderNodeGraph::ItemId RenderNodeGraph::AddNode(const RenderNodeParameters& nodeParameters, const i32 cameraIndex)
    {
        if (m_isBuilt || m_nodes.GetNumItems() >= 0xfffeu)
            VG_FATAL("render-node insertion requires an unbuilt graph within the flow-group capacity");
        const ItemId node = m_nodes.Allocate();
        VG_ASSERT(!m_nodes[node].HasAnyDependency());
        m_nodes[node].m_renderNodeParameters = nodeParameters;
        m_nodes[node].m_renderNodeContext.Set(cameraIndex, 0, 0);
        return node;
    }

    RenderNodeGraph::ItemId RenderNodeGraph::AddDependency(ItemId parentNodeId, ItemId childNodeId, const RenderNodeDependencyType dependencyType, const bool swapParentChild)
    {
        if (m_isBuilt || static_cast<u32>(dependencyType) >= RenderNodeDependencyTypeCount)
            VG_FATAL("render-node dependency requires an unbuilt graph and a valid dependency type");
        if (swapParentChild)
            SwapItemIds(parentNodeId, childNodeId);
        if (!NodesArray::IsValidId(parentNodeId) || !m_nodes.IsAllocated(parentNodeId) || !NodesArray::IsValidId(childNodeId) || !m_nodes.IsAllocated(childNodeId) || parentNodeId == childNodeId)
            VG_FATAL("render-node dependency endpoints must be distinct allocated nodes");
        if (HasDependency(parentNodeId, childNodeId, dependencyType))
            VG_FATAL("render-node dependency is already present");

        RenderNodeData& parent = m_nodes[parentNodeId];
        RenderNodeData& child = m_nodes[childNodeId];
        const ItemId dependency = m_dependencies.Allocate();
        DependencyData& data = m_dependencies[dependency];
        data.m_nodes[static_cast<u32>(RelationType::Parent)] = parentNodeId;
        data.m_nodes[static_cast<u32>(RelationType::Child)] = childNodeId;
        data.m_nextDependencies[static_cast<u32>(RelationType::Parent)] = parent.m_dependencies[static_cast<u32>(RelationType::Child)][static_cast<u32>(dependencyType)];
        data.m_nextDependencies[static_cast<u32>(RelationType::Child)] = child.m_dependencies[static_cast<u32>(RelationType::Parent)][static_cast<u32>(dependencyType)];
        data.m_type = dependencyType;
        parent.m_dependencies[static_cast<u32>(RelationType::Child)][static_cast<u32>(dependencyType)] = dependency;
        child.m_dependencies[static_cast<u32>(RelationType::Parent)][static_cast<u32>(dependencyType)] = dependency;
        VG_ASSERT(dependency == FindDependency(parentNodeId, childNodeId, dependencyType));
        return dependency;
    }

    bool RenderNodeGraph::HasDependency(const ItemId parentNodeId, const ItemId childNodeId, const RenderNodeDependencyType dependencyType, const bool swapParentChild) const
    {
        return FindDependency(parentNodeId, childNodeId, dependencyType, swapParentChild) != InvalidItemId;
    }

    RenderNodeGraph::ItemId RenderNodeGraph::FindDependency(ItemId parentNodeId, ItemId childNodeId, const RenderNodeDependencyType dependencyType, const bool swapParentChild) const
    {
        if (swapParentChild)
            SwapItemIds(parentNodeId, childNodeId);
        VG_ASSERT(IsAllocated(parentNodeId) && NodesArray::IsValidId(parentNodeId));
        VG_ASSERT(IsAllocated(childNodeId) && NodesArray::IsValidId(childNodeId));

        const RenderNodeData& parent = m_nodes[parentNodeId];
        for (ItemId dependency = parent.m_dependencies[static_cast<u32>(RelationType::Child)][static_cast<u32>(dependencyType)]; dependency != InvalidItemId;
             dependency = m_dependencies[dependency].m_nextDependencies[static_cast<u32>(RelationType::Parent)])
        {
            const DependencyData& data = m_dependencies[dependency];
            VG_ASSERT(data.m_nodes[static_cast<u32>(RelationType::Parent)] == parentNodeId);
            VG_ASSERT(data.m_type == dependencyType);
            if (data.m_nodes[static_cast<u32>(RelationType::Child)] == childNodeId)
                return dependency;
        }
        return InvalidItemId;
    }

    void RenderNodeGraph::Remove(const ItemId id)
    {
        VG_ASSERT(!m_isBuilt);
        VG_ASSERT(NodesArray::IsValidId(id) != DependenciesArray::IsValidId(id));
        VG_ASSERT(IsAllocated(id));
        if (NodesArray::IsValidId(id))
            RemoveNode(id, false);
        else
            RemoveDependency(id);
    }

    void RenderNodeGraph::RemoveDependency(const ItemId id)
    {
        VG_ASSERT(!m_isBuilt);
        VG_ASSERT(IsAllocated(id) && DependenciesArray::IsValidId(id));
        const DependencyData dependency = m_dependencies[id];
        const u32 parent = static_cast<u32>(RelationType::Parent);
        const u32 child = static_cast<u32>(RelationType::Child);
        const u32 type = static_cast<u32>(dependency.m_type);
        VG_ASSERT(HasDependency(dependency.m_nodes[parent], dependency.m_nodes[child], dependency.m_type));

        ItemId* link = &m_nodes[dependency.m_nodes[parent]].m_dependencies[child][type];
        while (*link != id)
        {
            link = &m_dependencies[*link].m_nextDependencies[parent];
            VG_ASSERT(*link != InvalidItemId);
        }
        *link = dependency.m_nextDependencies[parent];

        link = &m_nodes[dependency.m_nodes[child]].m_dependencies[parent][type];
        while (*link != id)
        {
            link = &m_dependencies[*link].m_nextDependencies[child];
            VG_ASSERT(*link != InvalidItemId);
        }
        *link = dependency.m_nextDependencies[child];

        VG_ASSERT(!HasDependency(dependency.m_nodes[parent], dependency.m_nodes[child], dependency.m_type));
        m_dependencies.Free(id);
        VG_ASSERT(!IsAllocated(id));
    }

    void RenderNodeGraph::RemoveNode(const ItemId id, const bool mergeDependencies)
    {
        VG_ASSERT(!m_isBuilt);
        if (mergeDependencies)
        {
            RenderNodeData& node = m_nodes[id];
            for (u32 typeIndex = 0; typeIndex < RenderNodeDependencyTypeCount; ++typeIndex)
            {
                const auto type = static_cast<RenderNodeDependencyType>(typeIndex);
                for (ItemId parentDependency = node.m_dependencies[static_cast<u32>(RelationType::Parent)][typeIndex]; parentDependency != InvalidItemId;
                     parentDependency = m_dependencies[parentDependency].m_nextDependencies[static_cast<u32>(RelationType::Child)])
                {
                    const ItemId parentNode = m_dependencies[parentDependency].m_nodes[static_cast<u32>(RelationType::Parent)];
                    for (ItemId childDependency = node.m_dependencies[static_cast<u32>(RelationType::Child)][typeIndex]; childDependency != InvalidItemId;
                         childDependency = m_dependencies[childDependency].m_nextDependencies[static_cast<u32>(RelationType::Parent)])
                    {
                        const ItemId childNode = m_dependencies[childDependency].m_nodes[static_cast<u32>(RelationType::Child)];
                        if (!HasDependency(parentNode, childNode, type))
                            static_cast<void>(AddDependency(parentNode, childNode, type));
                    }
                }
            }
        }

        VG_ASSERT(IsAllocated(id) && NodesArray::IsValidId(id));
        RemoveNodeDependencies(id);
        m_nodes.Free(id);
        VG_ASSERT(!IsAllocated(id));
    }

    void RenderNodeGraph::CopyDependencies(const ItemId sourceNodeId, const ItemId targetNodeId, const RelationType relation, const RenderNodeDependencyType dependencyType)
    {
        VG_ASSERT(!m_isBuilt);
        if (sourceNodeId == targetNodeId)
            return;
        const RenderNodeData& source = m_nodes[sourceNodeId];
        const bool swapParentChild = relation == RelationType::Child;
        const RelationType invertedRelation = GetInvertedRelation(relation);
        for (ItemId sourceDependency = source.m_dependencies[static_cast<u32>(relation)][static_cast<u32>(dependencyType)]; sourceDependency != InvalidItemId;
             sourceDependency = m_dependencies[sourceDependency].m_nextDependencies[static_cast<u32>(invertedRelation)])
        {
            const DependencyData& dependency = m_dependencies[sourceDependency];
            if (!HasDependency(dependency.m_nodes[static_cast<u32>(relation)], targetNodeId, dependencyType, swapParentChild))
                static_cast<void>(AddDependency(dependency.m_nodes[static_cast<u32>(relation)], targetNodeId, dependencyType, swapParentChild));
        }
    }

    void RenderNodeGraph::CopyDependencies(const ItemId sourceNodeId, const ItemId targetNodeId, const RelationType relation)
    {
        for (u32 type = 0; type < RenderNodeDependencyTypeCount; ++type)
            CopyDependencies(sourceNodeId, targetNodeId, relation, static_cast<RenderNodeDependencyType>(type));
    }

    void RenderNodeGraph::CopyDependencies(const ItemId sourceNodeId, const ItemId targetNodeId)
    {
        for (u32 relation = 0; relation < static_cast<u32>(RelationType::Count); ++relation)
            CopyDependencies(sourceNodeId, targetNodeId, static_cast<RelationType>(relation));
    }

    void RenderNodeGraph::RemoveNodeDependencies(const ItemId nodeId, const RelationType relation, const RenderNodeDependencyType dependencyType)
    {
        VG_ASSERT(!m_isBuilt);
        RenderNodeData& node = m_nodes[nodeId];
        ItemId& dependency = node.m_dependencies[static_cast<u32>(relation)][static_cast<u32>(dependencyType)];
        while (dependency != InvalidItemId)
            RemoveDependency(dependency);
        VG_ASSERT(!node.HasAnyDependency(relation, dependencyType));
    }

    void RenderNodeGraph::RemoveNodeDependencies(const ItemId nodeId, const RelationType relation)
    {
        for (u32 type = 0; type < RenderNodeDependencyTypeCount; ++type)
            RemoveNodeDependencies(nodeId, relation, static_cast<RenderNodeDependencyType>(type));
    }

    void RenderNodeGraph::RemoveNodeDependencies(const ItemId nodeId)
    {
        for (u32 relation = 0; relation < static_cast<u32>(RelationType::Count); ++relation)
            RemoveNodeDependencies(nodeId, static_cast<RelationType>(relation));
    }

    void RenderNodeGraph::AddGraph(const RenderNodeGraph& graph, const bool enableForceCameraIndex, const RenderFlowSpace forcedCameraIndex, const bool performMerge)
    {
        VG_ASSERT(!m_isBuilt);
        VG_ASSERT(this != &graph);
        const u32 nodesOffset = m_nodes.GetNumItems();
        const u32 dependenciesOffset = m_dependencies.GetNumItems();
        m_nodes.Allocate(graph.m_nodes.GetNumItems());
        m_dependencies.Allocate(graph.m_dependencies.GetNumItems());
        VG_ASSERT(m_nodes.GetNumItems() == nodesOffset + graph.m_nodes.GetNumItems());
        VG_ASSERT(m_dependencies.GetNumItems() == dependenciesOffset + graph.m_dependencies.GetNumItems());

        for (u32 usageIndex = 0; usageIndex < graph.m_nodes.GetNumItems(); ++usageIndex)
        {
            const RenderNodeData& source = graph.m_nodes[graph.m_nodes.GetItemIdByUsageIndex(usageIndex)];
            RenderNodeData& target = m_nodes[m_nodes.GetItemIdByUsageIndex(nodesOffset + usageIndex)];
            target.m_renderNodeParameters = source.m_renderNodeParameters;
            target.m_renderNodeContext = source.m_renderNodeContext;
            if (enableForceCameraIndex)
                target.m_renderNodeContext.m_cameraIndex = static_cast<i32>(forcedCameraIndex);
            for (u32 relation = 0; relation < static_cast<u32>(RelationType::Count); ++relation)
                for (u32 type = 0; type < RenderNodeDependencyTypeCount; ++type)
                    target.m_dependencies[relation][type] = GetReindexedItemId(m_dependencies, graph.m_dependencies, source.m_dependencies[relation][type]);
        }

        for (u32 usageIndex = 0; usageIndex < graph.m_dependencies.GetNumItems(); ++usageIndex)
        {
            const DependencyData& source = graph.m_dependencies[graph.m_dependencies.GetItemIdByUsageIndex(usageIndex)];
            DependencyData& target = m_dependencies[m_dependencies.GetItemIdByUsageIndex(dependenciesOffset + usageIndex)];
            target.m_type = source.m_type;
            for (u32 relation = 0; relation < static_cast<u32>(RelationType::Count); ++relation)
            {
                target.m_nodes[relation] = GetReindexedItemId(m_nodes, graph.m_nodes, source.m_nodes[relation]);
                target.m_nextDependencies[relation] = GetReindexedItemId(m_dependencies, graph.m_dependencies, source.m_nextDependencies[relation]);
            }
        }

        if (performMerge)
            MergeNodes(nodesOffset);
    }

    void RenderNodeGraph::MergeNodes(const u32 numOriginalUsedNodeIndices)
    {
        VG_ASSERT(!m_isBuilt);
        constexpr u32 specialTypeCount = 3;
        constexpr u32 subtypeCapacity = 64;
        ItemId specialNodes[2][specialTypeCount][subtypeCapacity];
        for (u32 graphIndex = 0; graphIndex < 2; ++graphIndex)
            for (u32 typeIndex = 0; typeIndex < specialTypeCount; ++typeIndex)
                for (u32 subtypeIndex = 0; subtypeIndex < subtypeCapacity; ++subtypeIndex)
                    specialNodes[graphIndex][typeIndex][subtypeIndex] = InvalidItemId;

        const auto specialIndex = [](const RenderNodeType type) -> u32
        {
            VG_ASSERT(type == RenderNodeType::Unique || type == RenderNodeType::SequenceBegin || type == RenderNodeType::SequenceEnd);
            return static_cast<u32>(type) - static_cast<u32>(RenderNodeType::Unique);
        };

        if (numOriginalUsedNodeIndices > 0 && numOriginalUsedNodeIndices < m_nodes.GetNumItems())
        {
            for (u32 usageIndex = 0; usageIndex < m_nodes.GetNumItems(); ++usageIndex)
            {
                const ItemId nodeId = m_nodes.GetItemIdByUsageIndex(usageIndex);
                const RenderNodeParameters& parameters = m_nodes[nodeId].m_renderNodeParameters;
                if (parameters.m_type != RenderNodeType::Unique && parameters.m_type != RenderNodeType::SequenceBegin && parameters.m_type != RenderNodeType::SequenceEnd)
                    continue;
                const u32 graphIndex = usageIndex < numOriginalUsedNodeIndices ? 0 : 1;
                const u32 subtypeIndex = parameters.m_subtype.GetValue();
                if (subtypeIndex >= subtypeCapacity)
                    VG_FATAL("render-node subtype exceeds merge-key capacity");
                ItemId& specialNode = specialNodes[graphIndex][specialIndex(parameters.m_type)][subtypeIndex];
                if (specialNode != InvalidItemId)
                    VG_FATAL("special render nodes cannot be duplicated within one graph");
                specialNode = nodeId;
            }
        }

        for (u32 graphIndex = 0; graphIndex < 2; ++graphIndex)
            for (u32 subtypeIndex = 0; subtypeIndex < subtypeCapacity; ++subtypeIndex)
                if ((specialNodes[graphIndex][specialIndex(RenderNodeType::SequenceBegin)][subtypeIndex] != InvalidItemId) !=
                    (specialNodes[graphIndex][specialIndex(RenderNodeType::SequenceEnd)][subtypeIndex] != InvalidItemId))
                    VG_FATAL("render-node sequence has only one boundary");

        for (u32 subtypeIndex = 0; subtypeIndex < subtypeCapacity; ++subtypeIndex)
        {
            const ItemId original = specialNodes[0][specialIndex(RenderNodeType::Unique)][subtypeIndex];
            const ItemId imported = specialNodes[1][specialIndex(RenderNodeType::Unique)][subtypeIndex];
            if (original != InvalidItemId && imported != InvalidItemId)
            {
                CopyDependencies(imported, original);
                RemoveNode(imported, false);
            }
        }

        for (u32 subtypeIndex = 0; subtypeIndex < subtypeCapacity; ++subtypeIndex)
        {
            const ItemId begin0 = specialNodes[0][specialIndex(RenderNodeType::SequenceBegin)][subtypeIndex];
            const ItemId end0 = specialNodes[0][specialIndex(RenderNodeType::SequenceEnd)][subtypeIndex];
            const ItemId begin1 = specialNodes[1][specialIndex(RenderNodeType::SequenceBegin)][subtypeIndex];
            const ItemId end1 = specialNodes[1][specialIndex(RenderNodeType::SequenceEnd)][subtypeIndex];
            if (begin0 == InvalidItemId || begin1 == InvalidItemId)
                continue;
            CopyDependencies(begin1, begin0, RelationType::Parent);
            RemoveNodeDependencies(begin1, RelationType::Parent);
            CopyDependencies(end0, begin1, RelationType::Parent);
            CopyDependencies(end0, end1, RelationType::Child);
            RemoveNode(end0, false);
            RemoveNode(begin1, true);
        }
    }

    void RenderNodeGraph::RemoveHelperNodes()
    {
        VG_ASSERT(!m_isBuilt);
        for (u32 usageIndex = 0; usageIndex < m_nodes.GetNumItems(); ++usageIndex)
        {
            const ItemId nodeId = m_nodes.GetItemIdByUsageIndex(usageIndex);
            if (IsHelperNodeType(m_nodes[nodeId].m_renderNodeParameters.m_type))
            {
                RemoveNode(nodeId, true);
                --usageIndex;
            }
        }
    }

    void RenderNodeGraph::CalculateNodeLevelInRenderFlowGroupRecursive(const RenderNodeDependencyType dependencyType, const ItemId nodeItemId,
                                                                       containers::DynamicArray<u32>& levelNodeCounts)
    {
        VG_ASSERT(!m_isBuilt);
        RenderNodeData& node = m_nodes[nodeItemId];
        const u32 type = static_cast<u32>(dependencyType);
        const RenderFlowGroup level = node.m_renderNodeContext.m_renderFlowGroups[type];
        for (ItemId dependency = node.m_dependencies[static_cast<u32>(RelationType::Child)][type]; dependency != InvalidItemId;
             dependency = m_dependencies[dependency].m_nextDependencies[static_cast<u32>(RelationType::Parent)])
        {
            const ItemId childNodeId = m_dependencies[dependency].m_nodes[static_cast<u32>(RelationType::Child)];
            RenderFlowGroup& childLevel = m_nodes[childNodeId].m_renderNodeContext.m_renderFlowGroups[type];
            if (childLevel <= level)
            {
                if (childLevel > 0)
                    --levelNodeCounts[childLevel];
                childLevel = level + 1;
                ++levelNodeCounts[childLevel];
                CalculateNodeLevelInRenderFlowGroupRecursive(dependencyType, childNodeId, levelNodeCounts);
            }
        }
    }

    void RenderNodeGraph::BuildRenderFlowGroups()
    {
        if (m_isBuilt || m_nodes.GetNumItems() >= 0xffffu)
            VG_FATAL("flow-group construction requires an unbuilt graph within the flow-group capacity");
        for (u32 usageIndex = 0; usageIndex < m_nodes.GetNumItems(); ++usageIndex)
            for (u32 type = 0; type < RenderNodeDependencyTypeCount; ++type)
                m_nodes[m_nodes.GetItemIdByUsageIndex(usageIndex)].m_renderNodeContext.m_renderFlowGroups[type] = 0;

        containers::DynamicArray<u32> levelNodeCounts{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<ItemId> readyNodes{memory::pools::Rendering::GetInstance()};
        levelNodeCounts.Resize(m_nodes.GetNumItems());
        readyNodes.Reserve(m_nodes.GetNumItems());
        for (u32 type = 0; type < RenderNodeDependencyTypeCount; ++type)
        {
            // Validate both dependency domains before recursive level assignment can follow a cycle.
            readyNodes.Clear();
            for (u32 index = 0; index < levelNodeCounts.Size(); ++index)
                levelNodeCounts[index] = 0;
            for (u32 index = 0; index < m_dependencies.GetNumItems(); ++index)
            {
                const DependencyData& dependency = m_dependencies[m_dependencies.GetItemIdByUsageIndex(index)];
                if (static_cast<u32>(dependency.m_type) == type)
                    ++levelNodeCounts[GetNodeIndex(dependency.m_nodes[static_cast<u32>(RelationType::Child)])];
            }
            for (u32 index = 0; index < levelNodeCounts.Size(); ++index)
                if (levelNodeCounts[index] == 0)
                    readyNodes.PushBack(m_nodes.GetItemIdByUsageIndex(index));
            for (u32 index = 0; index < readyNodes.Size(); ++index)
                for (ItemId dependencyId = m_nodes[readyNodes[index]].m_dependencies[static_cast<u32>(RelationType::Child)][type]; dependencyId != InvalidItemId;
                     dependencyId = m_dependencies[dependencyId].m_nextDependencies[static_cast<u32>(RelationType::Parent)])
                {
                    const ItemId child = m_dependencies[dependencyId].m_nodes[static_cast<u32>(RelationType::Child)];
                    if (--levelNodeCounts[GetNodeIndex(child)] == 0)
                        readyNodes.PushBack(child);
                }
            if (readyNodes.Size() != m_nodes.GetNumItems())
                VG_FATAL("render-node graph contains a cycle in its CPU or GPU dependencies");

            for (u32 index = 0; index < levelNodeCounts.Size(); ++index)
                levelNodeCounts[index] = 0;
            for (u32 usageIndex = 0; usageIndex < m_nodes.GetNumItems(); ++usageIndex)
            {
                const ItemId nodeId = m_nodes.GetItemIdByUsageIndex(usageIndex);
                RenderNodeData& node = m_nodes[nodeId];
                if (node.m_dependencies[static_cast<u32>(RelationType::Parent)][type] == InvalidItemId)
                {
                    ++levelNodeCounts[0];
                    CalculateNodeLevelInRenderFlowGroupRecursive(static_cast<RenderNodeDependencyType>(type), nodeId, levelNodeCounts);
                }
            }
            for (u32 level = 1; level < m_nodes.GetNumItems() && levelNodeCounts[level] != 0; ++level)
                levelNodeCounts[level] += levelNodeCounts[level - 1];
            for (i32 usageIndex = static_cast<i32>(m_nodes.GetNumItems()) - 1; usageIndex >= 0; --usageIndex)
            {
                RenderNodeData& node = m_nodes[m_nodes.GetItemIdByUsageIndex(static_cast<u32>(usageIndex))];
                const u32 level = node.m_renderNodeContext.m_renderFlowGroups[type];
                node.m_renderNodeContext.m_renderFlowGroups[type] = levelNodeCounts[level] - 1;
                --levelNodeCounts[level];
            }
        }
        m_isBuilt = true;
    }

    void RenderNodeGraph::BuildFlattenedNodesArray(containers::DynamicArray<ItemId>& nodes, const RenderNodeDependencyType dependencyType) const
    {
        VG_ASSERT(m_isBuilt);
        nodes.Resize(m_nodes.GetNumItems());
        for (ItemId& node : nodes)
            node = InvalidItemId;
        for (u32 usageIndex = 0; usageIndex < m_nodes.GetNumItems(); ++usageIndex)
        {
            const ItemId nodeId = m_nodes.GetItemIdByUsageIndex(usageIndex);
            const RenderFlowGroup flowGroup = m_nodes[nodeId].m_renderNodeContext.m_renderFlowGroups[static_cast<u32>(dependencyType)];
            if (flowGroup >= nodes.Size())
                VG_FATAL("render-node flow group lies outside the flattened graph");
            if (nodes[flowGroup] != InvalidItemId)
                VG_FATAL("multiple render nodes occupy the same flattened flow group");
            nodes[flowGroup] = nodeId;
        }
        for (const ItemId node : nodes)
            if (node == InvalidItemId)
                VG_FATAL("flattened render-node graph contains an unfilled flow group");
    }

    bool RenderNodeGraph::PrepareResourceBindings(RenderNodeResourceBindings& resourceBindings, RenderNodeResourcePreparationFailures& failures) const
    {
        VG_ASSERT(m_isBuilt);
        resourceBindings.Reset();
        if (m_nodes.GetNumItems() == 0)
            return true;

        resourceBindings.m_entries.Resize(m_nodes.GetNumItems());
        const usize executionContextBytes = static_cast<usize>(m_nodes.GetNumItems()) * sizeof(RenderNodeImplContext);
        memory::MemoryBlock executionContextBlock = memory::Allocate(memory::PoolId::Rendering, executionContextBytes, alignof(RenderNodeImplContext));
        if (!executionContextBlock)
        {
            RenderFlowResourceFailure failure;
            failure.code = RenderFlowResourceFailureCode::NativeOutOfMemory;
            failure.phase = RenderFlowResourceSessionState::Planning;
            failure.message = "render-node execution context storage allocation failed";
            failures.Record(failure);
            resourceBindings.m_entries.Clear();
            return false;
        }
        resourceBindings.m_executionContextStorage = executionContextBlock.address;
        resourceBindings.m_executionContextCount = m_nodes.GetNumItems();
        auto* const executionContexts = static_cast<RenderNodeImplContext*>(executionContextBlock.address);
        for (u32 usageIndex = 0; usageIndex < m_nodes.GetNumItems(); ++usageIndex)
        {
            RenderNodeResourceBindings::Entry& entry = resourceBindings.m_entries[usageIndex];
            entry.node = RenderFlowNodeId{m_nodes.GetItemIdByUsageIndex(usageIndex)};
            entry.resources.Reset();
            entry.executionContext = new (executionContexts + usageIndex) RenderNodeImplContext();
        }
        return true;
    }

    void RenderNodeGraph::PrepareResourcesParallel(const RenderNodeImplContext& context, RenderFlowResourceAllocator& allocator, RenderNodeResourceBindings& resourceBindings,
                                                   RenderNodeResourcePreparationFailures& failures, jobs::Builder& builder) const
    {
        VG_ASSERT(m_isBuilt);
        VG_ASSERT(resourceBindings.m_entries.Size() == m_nodes.GetNumItems());
        if (m_nodes.GetNumItems() == 0)
            return;

        RenderFlowResourceFailure groupFailure;
        if (!allocator.PreparePlanningGroups(m_nodes.GetNumItems(), &groupFailure))
        {
            failures.Record(groupFailure);
            return;
        }

        containers::DynamicArray<ItemId> orderedNodes{memory::pools::Rendering::GetInstance()};
        orderedNodes.Resize(m_nodes.GetNumItems());
        for (ItemId& node : orderedNodes)
            node = InvalidItemId;

        u32 stride = 1021;
        while (std::gcd(stride, m_nodes.GetNumItems()) != 1u)
            ++stride;
        u32 quasiRandomIndex = stride % m_nodes.GetNumItems();
        for (u32 index = 0; index < m_nodes.GetNumItems(); ++index)
        {
            orderedNodes[quasiRandomIndex] = m_nodes.GetItemIdByUsageIndex(index);
            quasiRandomIndex = (quasiRandomIndex + stride) % m_nodes.GetNumItems();
        }

        // Allocate group writers before dispatch; workers only own their indexed writer.
        containers::DynamicArray<ResourcePlanningWriter> writers{memory::pools::Rendering::GetInstance()};
        writers.Resize(orderedNodes.Size());
        for (u32 index = 0; index < orderedNodes.Size(); ++index)
        {
            const ItemId nodeId = orderedNodes[index];
            const RenderNodeData& node = m_nodes[nodeId];
            RenderNodeImpl* const impl = node.m_renderNodeParameters.m_impl;
            if (impl == nullptr || !impl->HasCommandList(impl->GetCommandListUsage()))
                continue;
            const RenderFlowGroup flowGroup = node.m_renderNodeContext.m_renderFlowGroups[static_cast<u32>(RenderNodeDependencyType::Gpu)];
            RenderFlowResourceFailure failure;
            if (!allocator.CreatePlanningWriter(RenderFlowNodeId{nodeId}, GpuFlowGroupId{flowGroup}, CommandScopeId{nodeId}, writers[index], &failure))
            {
                failures.Record(failure);
                return;
            }
        }

        constexpr u32 nodeBucketSize = 8;
        const u32 nodeBucketCount = (orderedNodes.Size() + nodeBucketSize - 1) / nodeBucketSize;
        static jobs::JobName prepareResourcesName{"RenderGraph/PrepareResources"};
        jobs::ParallelTask task = jobs::ParallelTask::Create([this, context, &allocator, &resourceBindings, &failures, writers = static_cast<containers::DynamicArray<ResourcePlanningWriter>&&>(writers), orderedNodes = static_cast<containers::DynamicArray<ItemId>&&>(orderedNodes)](const u32 bucketIndex, const jobs::JobContext& jobContext) mutable noexcept
        {
            RenderNodeImplContext nodeContext(context, jobContext.dispatcherThreadIndex);
            nodeContext.ResetNodeData();
            const u32 firstNode = bucketIndex * nodeBucketSize;
            const u32 endNode = firstNode + nodeBucketSize;
            for (u32 index = firstNode; index < endNode && index < orderedNodes.Size(); ++index)
            {
                const ItemId nodeId = orderedNodes[index];
                const RenderNodeData& node = m_nodes[nodeId];
                RenderNodeImpl* const impl = node.m_renderNodeParameters.m_impl;
                if (impl == nullptr)
                    continue;

                const RenderNodeCommandListUsage commandListUsage = impl->GetCommandListUsage();
                RenderNodeResourceBindings::Entry& binding = resourceBindings.m_entries[GetNodeIndex(nodeId)];
                VG_ASSERT(binding.node == RenderFlowNodeId{nodeId});
                nodeContext.SetupNodeData(node.m_renderNodeContext, node.m_renderNodeParameters);
                RenderFlowResourceFailure failure;
                if (!impl->HasCommandList(commandListUsage))
                {
                    nodeContext.BeginResourceDeclaration(allocator, nullptr, binding.resources, failure);
                    impl->Process(nodeContext, nullptr);
                    nodeContext.EndResourceDeclaration();
                    if (failure.code != RenderFlowResourceFailureCode::None)
                        failures.Record(failure);
                    continue;
                }

                ResourcePlanningWriter& writer = writers[index];
                nodeContext.BeginResourceDeclaration(allocator, &writer, binding.resources, failure);
                impl->Process(nodeContext, nullptr);
                nodeContext.EndResourceDeclaration();
                if (failure.code == RenderFlowResourceFailureCode::None && !writer.Close(&failure))
                    writer.Abandon();
                if (failure.code != RenderFlowResourceFailureCode::None)
                {
                    writer.Abandon();
                    failures.Record(failure);
                }
            }
        });

        if (!task || !builder.DispatchParallel(prepareResourcesName, nodeBucketCount, static_cast<jobs::ParallelTask&&>(task), {}, 0, jobs::Fence::None))
        {
            RenderFlowResourceFailure failure;
            failure.code = RenderFlowResourceFailureCode::CapacityExceeded;
            failure.phase = RenderFlowResourceSessionState::Planning;
            failure.message = "render-node resource-preparation dispatch failed";
            failures.Record(failure);
            return;
        }
        builder.DispatchFence();
    }

    bool RenderNodeGraph::PrepareExecutionPackets(RenderFlowResourceAllocator& allocator, RenderFrameCommandLists& frameCommandLists, RenderNodeResourceBindings& resourceBindings,
                                                  RenderFlowResourceFailure* const failure) const noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (!m_isBuilt || resourceBindings.m_entries.Size() != m_nodes.GetNumItems())
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::IncompleteExecution;
                failure->phase = RenderFlowResourceSessionState::Executing;
                failure->message = "render-node execution bindings do not match the built graph";
            }
            return false;
        }

        for (const CompiledCommandScope& scope : allocator.GetExecutionCommandScopes())
            frameCommandLists.RegisterCommandScope(scope.stableOrder + static_cast<u32>(ReservedFrameCommandList::Count), scope.scope, scope.queue);
        for (const CompiledQueueDependency& dependency : allocator.GetExecutionQueueDependencies())
            frameCommandLists.RegisterQueueDependency(dependency);
        frameCommandLists.SealCommandScopes();

        for (u32 usageIndex = 0; usageIndex < m_nodes.GetNumItems(); ++usageIndex)
        {
            const ItemId nodeId = m_nodes.GetItemIdByUsageIndex(usageIndex);
            const RenderNodeData& node = m_nodes[nodeId];
            RenderNodeResourceBindings::Entry& entry = resourceBindings.m_entries[usageIndex];
            if (entry.cursor.IsValid())
            {
                if (failure != nullptr)
                {
                    failure->code = RenderFlowResourceFailureCode::InvalidPhase;
                    failure->phase = RenderFlowResourceSessionState::Executing;
                    failure->node = RenderFlowNodeId{nodeId};
                    failure->message = "render-node occurrence packet is already open";
                }
                return false;
            }
            entry.executionFailure = {};
            entry.packet = {};
            if (entry.node != RenderFlowNodeId{nodeId} || entry.executionContext == nullptr)
            {
                if (failure != nullptr)
                {
                    failure->code = RenderFlowResourceFailureCode::IncompleteExecution;
                    failure->phase = RenderFlowResourceSessionState::Executing;
                    failure->node = RenderFlowNodeId{nodeId};
                    failure->message = "render-node occurrence has no stable execution state";
                }
                return false;
            }

            const RenderNodeImpl* const impl = node.m_renderNodeParameters.m_impl;
            if (impl == nullptr || !impl->HasCommandList(impl->GetCommandListUsage()))
                continue;
            if (!allocator.PacketFor(entry.node, entry.packet, &entry.executionFailure))
            {
                if (failure != nullptr)
                    *failure = entry.executionFailure;
                return false;
            }
        }
        return true;
    }

    void RenderNodeGraph::Reset()
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_updateFlag);
        m_isBuilt = false;
        m_nodes.Reset();
        m_dependencies.Reset();
    }

    void RenderNodeGraph::AcquireExclusiveUpdateFlag() { m_updateFlag.Acquire(); }
    void RenderNodeGraph::ReleaseExclusiveUpdateFlag() { m_updateFlag.Release(); }

    // Scheduling and execution-side RenderNodeImpl processing remain separate from packet preparation.
} // namespace vanguard::rendering

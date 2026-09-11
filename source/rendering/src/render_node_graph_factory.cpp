#include <vanguard/rendering/render_node_graph_factory.hpp>

#include <vanguard/rendering/render_node_impl_context.hpp>

#include <vanguard/system/assert.hpp>

namespace vanguard::rendering
{
    class RenderNodeCommandListGroup final : public RenderNodeImpl
    {
    public:
        RenderNodeCommandListGroup(const char* const name, const rhi::CommandListType commandListType)
            : m_name(memory::pools::Rendering::GetInstance()), m_commandListType(commandListType), m_subnodes(memory::pools::Rendering::GetInstance())
        {
            if (name != nullptr)
                m_name = name;
        }

        ~RenderNodeCommandListGroup() override
        {
            for (RenderNodeImpl* const subnode : m_subnodes)
                VANGUARD_DELETE(subnode);
        }

        void AddSubnode(RenderNodeImpl* const node)
        {
            m_subnodes.PushBack(node);
        }

        [[nodiscard]] const char* GetName() const noexcept override { return m_name.AsChar(); }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Own; }
        [[nodiscard]] rhi::CommandListRef CreateCommandList() const override;
        [[nodiscard]] bool GetJobBuilderUsage() const noexcept override
        {
            for (const RenderNodeImpl* const subnode : m_subnodes)
                if (subnode->GetJobBuilderUsage())
                    return true;
            return false;
        }

        [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* const failure) const noexcept override
        {
            if (!context.BeginResourceQueue(rhi::GetQueueType(m_commandListType)))
                return false;
            for (const RenderNodeImpl* const subnode : m_subnodes)
            {
                subnode->Process(context, nullptr);
                if (failure != nullptr && failure->code != RenderFlowResourceFailureCode::None)
                    return false;
            }
            return context.EndResourceQueue();
        }

        void Execute(const RenderNodeImplContext& context, jobs::Builder* const builder) const override
        {
            if (m_subnodes.Empty())
                return;

            rhi::UnbindCommandList();
            bool childJobDispatched = false;
            u32 firstDeferredNodeIndex = 0;
            for (u32 nodeIndex = 0; nodeIndex < m_subnodes.Size(); ++nodeIndex)
            {
                RenderNodeImpl* const subnode = m_subnodes[nodeIndex];
                const bool isLastSubnode = nodeIndex + 1 == m_subnodes.Size();
                const bool nodeRequiresBuilder = subnode->GetJobBuilderUsage();

                if (!childJobDispatched && !nodeRequiresBuilder)
                {
                    subnode->Process(context, nullptr);
                    firstDeferredNodeIndex = nodeIndex + 1;
                    continue;
                }

                if (!isLastSubnode && !nodeRequiresBuilder)
                    continue;
                if (firstDeferredNodeIndex > nodeIndex)
                    continue;
                if (builder == nullptr)
                    VG_FATAL("command-list group requires a Jobs builder for deferred child processing");

                static jobs::JobName childBatchName{"RenderGraph/CommandListGroup"};
                jobs::Task childBatch = jobs::Task::Create([this, contextPointer = &context, firstNodeIndex = firstDeferredNodeIndex, lastNodeIndex = nodeIndex](const jobs::JobContext& jobContext) noexcept
                {
                    RenderNodeImplContext& childContext = const_cast<RenderNodeImplContext&>(*contextPointer);
                    childContext.m_dispatcherThreadIndex = jobContext.dispatcherThreadIndex;
                    for (u32 childIndex = firstNodeIndex; childIndex <= lastNodeIndex; ++childIndex)
                    {
                        RenderNodeImpl* const child = m_subnodes[childIndex];
                        if (child->GetJobBuilderUsage())
                        {
                            if (childIndex != lastNodeIndex)
                                VG_FATAL("a command-list child using a Jobs builder must terminate its batch");
                            jobs::Builder childBuilder(jobContext);
                            child->Process(childContext, &childBuilder);
                        }
                        else
                        {
                            child->Process(childContext, nullptr);
                        }
                    }
                });
                if (!builder->Dispatch(childBatchName, static_cast<jobs::Task&&>(childBatch)))
                {
                    VG_FATAL("command-list child batch dispatch failed");
                }
                childJobDispatched = true;
                firstDeferredNodeIndex = nodeIndex + 1;
            }
        }

    private:
        containers::String m_name;
        rhi::CommandListType m_commandListType = rhi::CommandListType::Default;
        containers::DynamicArray<RenderNodeImpl*> m_subnodes;
    };

    rhi::CommandListRef RenderNodeCommandListGroup::CreateCommandList() const
    {
        rhi::CommandListRef commandList = rhi::CreateCommandList(m_commandListType);
        if (commandList.IsValid())
            rhi::SetResourceDebugName(commandList, m_name.AsChar());
        return commandList;
    }

    NodesContainer::NodesContainer() : m_nodes(memory::pools::Rendering::GetInstance()) {}
    NodesContainer::~NodesContainer() { Reset(); }

    void NodesContainer::Reset() noexcept
    {
        for (RenderNodeImpl* const node : m_nodes)
            VANGUARD_DELETE(node);
        m_nodes.Clear();
    }

    NodeGraphFactory::NodeGraphFactory(RenderNodeGraph* const graph, NodesContainer* const nodes) noexcept
        : m_graph(graph), m_nodes(nodes), m_nodeIds(memory::pools::Rendering::GetInstance()), m_groupTags(memory::pools::Rendering::GetInstance())
    {
        if (m_graph == nullptr || m_nodes == nullptr)
            VG_FATAL("graph construction requires graph storage and an external node container");
        m_graph->Reset();
        m_nodes->Reset();
    }

    NodeGraphFactory::~NodeGraphFactory()
    {
        if (m_currentGroup != nullptr)
            VG_FATAL("command-list group is still open");
    }

    u32 NodeGraphFactory::GetGroupIndex(const NodeGroupId groupId) noexcept
    {
        const u32 index = static_cast<u32>(groupId);
        if (index >= MaximumGroupCount)
            VG_FATAL("render-node group identifier exceeds factory capacity");
        return index;
    }

    i32 NodeGraphFactory::FindGroupFirstTagIndex(const NodeGroupId groupId) const noexcept
    {
        return m_groupData[GetGroupIndex(groupId)].firstGroupTagIndex;
    }

    RenderNodeGraph::ItemId NodeGraphFactory::Register(const NodeGroupId groupId, const RenderNodeParameters& nodeParameters)
    {
        if (nodeParameters.m_impl == nullptr)
            VG_FATAL("render-node implementation is missing");
        VG_ASSERT(m_graph != nullptr);
        VG_ASSERT(m_nodes != nullptr);

        if (m_currentGroup != nullptr)
        {
            m_currentGroup->AddSubnode(nodeParameters.m_impl);
            return RenderNodeGraph::InvalidItemId;
        }

        m_nodes->m_nodes.PushBack(nodeParameters.m_impl);
        const RenderNodeGraph::ItemId nodeId = m_graph->AddNode(nodeParameters, 0);
        GroupData& group = m_groupData[GetGroupIndex(groupId)];
        m_groupTags.Grow(1);
        m_groupTags.Back().nodeId = nodeId;
        m_groupTags.Back().nextIndex = group.firstGroupTagIndex;
        group.firstGroupTagIndex = static_cast<i32>(m_groupTags.Size()) - 1;
        if (group.dummyInputNode != RenderNodeGraph::InvalidItemId)
            LinkImpl(group.dummyInputNode, nodeId, RenderNodeDependencyType::Cpu);
        if (group.dummyOutputNode != RenderNodeGraph::InvalidItemId)
            LinkImpl(nodeId, group.dummyOutputNode, RenderNodeDependencyType::Cpu);
        m_nodeIds.PushBack(nodeId);
        VG_ASSERT(m_nodes->m_nodes.Size() == m_nodeIds.Size());
        return nodeId;
    }

    RenderNodeGraph::ItemId NodeGraphFactory::CreateCustom(const NodeGroupId groupId, const RenderNodeParameters& nodeParameters)
    {
        return Register(groupId, nodeParameters);
    }

    RenderNodeGraph::ItemId NodeGraphFactory::BeginCommandListGroup(const NodeGroupId groupId, const RenderNodeType type, const RenderNodeSubtype subtype,
                                                                          const char* const name, const rhi::CommandListType commandListType)
    {
        if (m_currentGroup != nullptr)
            VG_FATAL("command-list group nesting is not supported");
        RenderNodeCommandListGroup* const group = VANGUARD_NEW(RenderNodeCommandListGroup)(name, commandListType);
        if (group == nullptr)
            VG_FATAL("command-list group allocation failed during graph construction");
        const RenderNodeGraph::ItemId itemId = Register(groupId, RenderNodeParameters().Set(type, subtype, group));
        m_currentGroup = group;
        return itemId;
    }

    void NodeGraphFactory::EndCommandListGroup()
    {
        if (m_currentGroup == nullptr)
            VG_FATAL("no command-list group is open");
        m_currentGroup = nullptr;
    }

    void NodeGraphFactory::LinkImpl(const RenderNodeGraph::ItemId parent, const RenderNodeGraph::ItemId child, const RenderNodeDependencyType dependencyType)
    {
        if (parent == RenderNodeGraph::InvalidItemId || child == RenderNodeGraph::InvalidItemId)
            VG_FATAL("render-node dependency endpoints must be valid nodes");
        if (!m_graph->HasDependency(parent, child, dependencyType))
            static_cast<void>(m_graph->AddDependency(parent, child, dependencyType));
    }

    void NodeGraphFactory::Link(const RenderNodeGraph::ItemId parent, const RenderNodeGraph::ItemId child, const RenderNodeDependencyType dependencyType)
    {
        LinkImpl(parent, child, dependencyType);
    }

    void NodeGraphFactory::Link(const RenderNodeGraph::ItemId parent, const NodeGroupId child, const RenderNodeDependencyType dependencyType)
    {
        if (dependencyType != RenderNodeDependencyType::Cpu)
            VG_FATAL("dependency groups support only CPU dependencies");
        LinkImpl(parent, GetDummyInputNodeForGroup(child), dependencyType);
    }

    void NodeGraphFactory::Link(const NodeGroupId parent, const RenderNodeGraph::ItemId child, const RenderNodeDependencyType dependencyType)
    {
        if (dependencyType != RenderNodeDependencyType::Cpu)
            VG_FATAL("dependency groups support only CPU dependencies");
        LinkImpl(GetDummyOutputNodeForGroup(parent), child, dependencyType);
    }

    void NodeGraphFactory::Link(const NodeGroupId parent, const NodeGroupId child, const RenderNodeDependencyType dependencyType)
    {
        if (dependencyType != RenderNodeDependencyType::Cpu)
            VG_FATAL("dependency groups support only CPU dependencies");
        LinkImpl(GetDummyOutputNodeForGroup(parent), GetDummyInputNodeForGroup(child), dependencyType);
    }

    RenderNodeGraph::ItemId NodeGraphFactory::GetDummyInputNodeForGroup(const NodeGroupId groupId)
    {
        GroupData& group = m_groupData[GetGroupIndex(groupId)];
        if (group.dummyInputNode == RenderNodeGraph::InvalidItemId)
        {
            group.dummyInputNode = m_graph->AddNode(RenderNodeParameters(), 0);
            m_nodes->m_nodes.PushBack(nullptr);
            m_nodeIds.PushBack(group.dummyInputNode);
            VG_ASSERT(m_nodes->m_nodes.Size() == m_nodeIds.Size());
            for (i32 tagIndex = FindGroupFirstTagIndex(groupId); tagIndex != -1; tagIndex = m_groupTags[tagIndex].nextIndex)
                LinkImpl(group.dummyInputNode, m_groupTags[tagIndex].nodeId, RenderNodeDependencyType::Cpu);
            if (group.dummyOutputNode != RenderNodeGraph::InvalidItemId)
                LinkImpl(group.dummyInputNode, group.dummyOutputNode, RenderNodeDependencyType::Cpu);
        }
        return group.dummyInputNode;
    }

    RenderNodeGraph::ItemId NodeGraphFactory::GetDummyOutputNodeForGroup(const NodeGroupId groupId)
    {
        GroupData& group = m_groupData[GetGroupIndex(groupId)];
        if (group.dummyOutputNode == RenderNodeGraph::InvalidItemId)
        {
            group.dummyOutputNode = m_graph->AddNode(RenderNodeParameters(), 0);
            m_nodes->m_nodes.PushBack(nullptr);
            m_nodeIds.PushBack(group.dummyOutputNode);
            VG_ASSERT(m_nodes->m_nodes.Size() == m_nodeIds.Size());
            for (i32 tagIndex = FindGroupFirstTagIndex(groupId); tagIndex != -1; tagIndex = m_groupTags[tagIndex].nextIndex)
                LinkImpl(m_groupTags[tagIndex].nodeId, group.dummyOutputNode, RenderNodeDependencyType::Cpu);
            if (group.dummyInputNode != RenderNodeGraph::InvalidItemId)
                LinkImpl(group.dummyInputNode, group.dummyOutputNode, RenderNodeDependencyType::Cpu);
        }
        return group.dummyOutputNode;
    }

    void NodeGraphFactory::LinkGpu()
    {
        for (u32 index = 1; index < m_nodeIds.Size(); ++index)
            if (!m_graph->HasDependency(m_nodeIds[index - 1], m_nodeIds[index], RenderNodeDependencyType::Gpu))
                static_cast<void>(m_graph->AddDependency(m_nodeIds[index - 1], m_nodeIds[index], RenderNodeDependencyType::Gpu));
    }

    void NodeGraphFactory::LinkCpu()
    {
        for (u32 index = 1; index < m_nodeIds.Size(); ++index)
            if (!m_graph->HasDependency(m_nodeIds[index - 1], m_nodeIds[index], RenderNodeDependencyType::Cpu))
                static_cast<void>(m_graph->AddDependency(m_nodeIds[index - 1], m_nodeIds[index], RenderNodeDependencyType::Cpu));
    }

    void NodeGraphFactory::LinkCpuToNextGpu(const RenderNodeGraph::ItemId parent)
    {
        bool afterParent = false;
        for (u32 index = 0; index < m_nodeIds.Size(); ++index)
        {
            if (afterParent)
            {
                RenderNodeImpl* const nextNode = m_nodes->m_nodes[index];
                if (nextNode != nullptr && nextNode->GetCommandListUsage() != RenderNodeCommandListUsage::None &&
                    !m_graph->HasDependency(parent, m_nodeIds[index], RenderNodeDependencyType::Cpu))
                    static_cast<void>(m_graph->AddDependency(parent, m_nodeIds[index], RenderNodeDependencyType::Cpu));
            }
            else if (m_nodeIds[index] == parent)
            {
                afterParent = true;
            }
        }
    }

    void NodeGraphFactory::LinkCpuToPreviousGpu(const RenderNodeGraph::ItemId child)
    {
        for (u32 index = 0; index < m_nodeIds.Size() && m_nodeIds[index] != child; ++index)
        {
            RenderNodeImpl* const previousNode = m_nodes->m_nodes[index];
            if (previousNode != nullptr && previousNode->GetCommandListUsage() != RenderNodeCommandListUsage::None &&
                !m_graph->HasDependency(m_nodeIds[index], child, RenderNodeDependencyType::Cpu))
                static_cast<void>(m_graph->AddDependency(m_nodeIds[index], child, RenderNodeDependencyType::Cpu));
        }
    }
} // namespace vanguard::rendering

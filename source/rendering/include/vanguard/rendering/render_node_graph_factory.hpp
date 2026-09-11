#pragma once

#include <vanguard/rendering/render_node_graph.hpp>
#include <vanguard/system/assert.hpp>

#include <utility>

namespace vanguard::rendering
{
    class RenderNodeCommandListGroup;

    struct NodesContainer
    {
        NodesContainer();
        ~NodesContainer();
        NodesContainer(const NodesContainer&) = delete;
        NodesContainer& operator=(const NodesContainer&) = delete;

        void Reset() noexcept;

        containers::DynamicArray<RenderNodeImpl*> m_nodes;
    };

    enum class NodeGroupId : u8 { None = 0 };

    class NodeGraphFactory
    {
    public:
        static constexpr u32 MaximumGroupCount = 16;

        NodeGraphFactory(RenderNodeGraph* graph, NodesContainer* nodes) noexcept;
        ~NodeGraphFactory();

        RenderNodeGraph::ItemId Register(NodeGroupId groupId, const RenderNodeParameters& nodeParameters);
        RenderNodeGraph::ItemId CreateCustom(NodeGroupId groupId, const RenderNodeParameters& nodeParameters);

        template <typename Node, typename... Args>
        RenderNodeGraph::ItemId Create(NodeGroupId groupId, RenderNodeType type, RenderNodeSubtype subtype, Args&&... args)
        {
            Node* const node = VANGUARD_NEW(Node)(static_cast<Args&&>(args)...);
            if (node == nullptr)
                VG_FATAL("render-node allocation failed during graph construction");
            return Register(groupId, RenderNodeParameters().Set(type, subtype, node));
        }

        // While a command-list group is open, Create adds implementations to
        // that group rather than adding graph occurrences. Only one group may
        // be open at a time; nesting is rejected.
        RenderNodeGraph::ItemId BeginCommandListGroup(NodeGroupId groupId, RenderNodeType type, RenderNodeSubtype subtype, const char* name,
                                                       rhi::CommandListType commandListType = rhi::CommandListType::Default);
        void EndCommandListGroup();

        void Link(RenderNodeGraph::ItemId parent, RenderNodeGraph::ItemId child, RenderNodeDependencyType dependencyType);
        void Link(RenderNodeGraph::ItemId parent, NodeGroupId child, RenderNodeDependencyType dependencyType);
        void Link(NodeGroupId parent, RenderNodeGraph::ItemId child, RenderNodeDependencyType dependencyType);
        void Link(NodeGroupId parent, NodeGroupId child, RenderNodeDependencyType dependencyType);

        template <RenderNodeDependencyType DependencyType, typename Function>
        void LinkIf(const RenderNodeGraph::ItemId parent, const Function& function)
        {
            for (u32 index = 0; index < m_graph->GetNumNodes(); ++index)
            {
                const RenderNodeGraph::ItemId node = m_graph->GetNode(index);
                if (function(m_graph->GetNodeParameters(node).m_impl))
                    Link(parent, node, DependencyType);
            }
        }

        void LinkGpu();
        void LinkCpu();
        void LinkCpuToNextGpu(RenderNodeGraph::ItemId parent);
        void LinkCpuToPreviousGpu(RenderNodeGraph::ItemId child);

    private:
        struct GroupData
        {
            i32 firstGroupTagIndex = -1;
            RenderNodeGraph::ItemId dummyInputNode = RenderNodeGraph::InvalidItemId;
            RenderNodeGraph::ItemId dummyOutputNode = RenderNodeGraph::InvalidItemId;
        };

        struct GroupTag
        {
            RenderNodeGraph::ItemId nodeId = RenderNodeGraph::InvalidItemId;
            i32 nextIndex = -1;
        };

        void LinkImpl(RenderNodeGraph::ItemId parent, RenderNodeGraph::ItemId child, RenderNodeDependencyType dependencyType);
        [[nodiscard]] static u32 GetGroupIndex(NodeGroupId groupId) noexcept;
        [[nodiscard]] RenderNodeGraph::ItemId GetDummyInputNodeForGroup(NodeGroupId groupId);
        [[nodiscard]] RenderNodeGraph::ItemId GetDummyOutputNodeForGroup(NodeGroupId groupId);
        [[nodiscard]] i32 FindGroupFirstTagIndex(NodeGroupId groupId) const noexcept;

        RenderNodeGraph* m_graph = nullptr;
        NodesContainer* m_nodes = nullptr;
        containers::DynamicArray<RenderNodeGraph::ItemId> m_nodeIds;
        containers::FixedArray<GroupData, MaximumGroupCount> m_groupData;
        containers::DynamicArray<GroupTag> m_groupTags;
        RenderNodeCommandListGroup* m_currentGroup = nullptr;
    };

    class ScopedCloseCommandList
    {
    public:
        explicit ScopedCloseCommandList(NodeGraphFactory& graphFactory) noexcept : factory(graphFactory) {}
        ~ScopedCloseCommandList() { factory.EndCommandListGroup(); }
        ScopedCloseCommandList(const ScopedCloseCommandList&) = delete;
        ScopedCloseCommandList& operator=(const ScopedCloseCommandList&) = delete;
        explicit operator bool() const noexcept { return true; }

        NodeGraphFactory& factory;
    };
} // namespace vanguard::rendering

#define ADD_NODE(groupId, name, impl, ...) factory.Create<impl>(groupId, ::vanguard::rendering::RenderNodeType::Stage, ::vanguard::rendering::RenderNodeSubtype(0), ##__VA_ARGS__)
#define ADD_UNIQUE(groupId, name, subtype, impl, ...) factory.Create<impl>(groupId, ::vanguard::rendering::RenderNodeType::Unique, ::vanguard::rendering::RenderNodeSubtype(subtype), ##__VA_ARGS__)
#define ADD_SEQ_BEGIN(groupId, name, subtype, ...) factory.Create<::vanguard::rendering::RenderNodeDummy>(groupId, ::vanguard::rendering::RenderNodeType::SequenceBegin, ::vanguard::rendering::RenderNodeSubtype(subtype), ##__VA_ARGS__)
#define ADD_SEQ_END(groupId, name, subtype, ...) factory.Create<::vanguard::rendering::RenderNodeDummy>(groupId, ::vanguard::rendering::RenderNodeType::SequenceEnd, ::vanguard::rendering::RenderNodeSubtype(subtype), ##__VA_ARGS__)

#define RENDER_COMMAND_LIST(groupId, name) factory.BeginCommandListGroup(groupId, ::vanguard::rendering::RenderNodeType::Stage, ::vanguard::rendering::RenderNodeSubtype(0), name); if (auto scopedCommandList = ::vanguard::rendering::ScopedCloseCommandList(factory))
#define COMPUTE_COMMAND_LIST(groupId, name) factory.BeginCommandListGroup(groupId, ::vanguard::rendering::RenderNodeType::Stage, ::vanguard::rendering::RenderNodeSubtype(0), name, ::vanguard::rhi::CommandListType::Compute); if (auto scopedCommandList = ::vanguard::rendering::ScopedCloseCommandList(factory))
#define RENDER_UNIQUE_COMMAND_LIST(groupId, subtype, name) factory.BeginCommandListGroup(groupId, ::vanguard::rendering::RenderNodeType::Unique, ::vanguard::rendering::RenderNodeSubtype(subtype), name); if (auto scopedCommandList = ::vanguard::rendering::ScopedCloseCommandList(factory))
#define ADD_SUBNODE(name, impl, ...) scopedCommandList.factory.Create<impl>(::vanguard::rendering::NodeGroupId::None, ::vanguard::rendering::RenderNodeType::Stage, ::vanguard::rendering::RenderNodeSubtype(0), ##__VA_ARGS__)

#define RENDER_SIMPLE_COMMAND_LIST(groupId, name, impl, ...) RENDER_COMMAND_LIST(groupId, name) { ADD_SUBNODE(name, impl, ##__VA_ARGS__); }
#define RENDER_UNIQUE_SIMPLE_COMMAND_LIST(groupId, name, subtype, impl, ...) RENDER_UNIQUE_COMMAND_LIST(groupId, subtype, name) { ADD_SUBNODE(name, impl, ##__VA_ARGS__); }
#define SYNC_SUBMIT(groupId, name, waitType) { const auto synchronizeNode = ADD_NODE(groupId, name "_Flush", RenderNodeSynchronize, waitType, "Submit_" name); factory.LinkCpuToPreviousGpu(synchronizeNode); }

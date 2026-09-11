#pragma once

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/render_flow_resource_allocator.hpp>
#include <vanguard/rendering/render_node_graph_array.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/system/types.hpp>

namespace vanguard::jobs
{
    class Builder;
}

namespace vanguard::rendering
{
    class NodeGraphFactory;
    class RenderFrameCommandLists;
    class RenderFlowResourceAllocator;
    class RenderNodeResourceBindings;
    struct RenderNodeImplContext;

    class RenderNodeResourcePreparationFailures final
    {
    public:
        void Record(const RenderFlowResourceFailure& failure) noexcept;
        [[nodiscard]] bool HasFailure() const noexcept;
        [[nodiscard]] RenderFlowResourceFailure GetFailure() const noexcept;

    private:
        mutable concurrency::SpinLock m_lock;
        RenderFlowResourceFailure m_failure;
    };

    enum class RenderNodeType : u8
    {
        Unique,
        SequenceBegin,
        SequenceEnd,
        Temporary,
        Stage,
        Count
    };

    enum class RenderNodeDependencyType : u8
    {
        Cpu,
        Gpu,
        Count
    };

    inline constexpr u32 RenderNodeDependencyTypeCount = static_cast<u32>(RenderNodeDependencyType::Count);

    namespace node
    {
        struct GlobalBindingsModification
        {
            u64 slots = 0;

            template <typename... Args>
            explicit GlobalBindingsModification(Args&&... args)
            {
                Setup(static_cast<Args&&>(args)...);
            }

            void Setup() noexcept {}

            template <typename... Args>
            void Setup(const u64 slot, Args&&... args) noexcept
            {
                slots |= u64{1} << slot;
                Setup(static_cast<Args&&>(args)...);
            }
        };

        struct RenderTargetBinder {};
        struct GpuSubmit { static constexpr bool gpuSubmit = true; static constexpr bool gpuScope = false; };
        struct SimpleNode { static constexpr bool gpuSubmit = false; static constexpr bool gpuScope = true; };
        struct SimpleNoGpuScope { static constexpr bool gpuSubmit = false; static constexpr bool gpuScope = false; };
    } // namespace node

    enum class RenderNodeCommandListUsage : u8
    {
        None,
        Require,
        Own,
        Sync
    };

    class RenderNodeImpl
    {
        friend class NodeGraphFactory;

    public:
        VANGUARD_USE_POLYMORPHIC_MEMORY_POOL(memory::pools::Rendering);

        template <typename... Args>
        explicit RenderNodeImpl(Args&&... args)
        {
            Setup(static_cast<Args&&>(args)...);
        }

        virtual ~RenderNodeImpl() = default;
        [[nodiscard]] virtual bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept
        {
            return true;
        }
        virtual void Execute(const RenderNodeImplContext& context, jobs::Builder* builder) const = 0;
        [[nodiscard]] virtual const char* GetName() const noexcept { return ""; }
        [[nodiscard]] virtual rhi::CommandListRef CreateCommandList() const;
        [[nodiscard]] virtual bool GetJobBuilderUsage() const noexcept { return false; }
        [[nodiscard]] virtual RenderNodeCommandListUsage GetCommandListUsage() const noexcept = 0;

        [[nodiscard]] u64 GetGlobalBindingModification() const noexcept { return m_globalBindingModification; }
        [[nodiscard]] bool HasCommandList(const RenderNodeCommandListUsage usage) const noexcept { return usage != RenderNodeCommandListUsage::None && usage != RenderNodeCommandListUsage::Sync; }

        void Process(const RenderNodeImplContext& context, jobs::Builder* builder) const;
        void ProcessEpilogue(const RenderNodeImplContext& context) const;
        void BeginProfilerBlock() const;
        void EndProfilerBlock() const;

    private:
        void Setup() noexcept {}

        template <typename... Args>
        void Setup(const node::RenderTargetBinder&, Args&&... args)
        {
            m_renderTargetBinder = true;
            Setup(static_cast<Args&&>(args)...);
        }

        template <typename... Args>
        void Setup(const node::GlobalBindingsModification& modification, Args&&... args)
        {
            m_globalBindingModification = modification.slots;
            Setup(static_cast<Args&&>(args)...);
        }

        u64 m_globalBindingModification = 0;
        bool m_renderTargetBinder = false;
    };

    class RenderNodeDummy final : public RenderNodeImpl
    {
    public:
        using RenderNodeImpl::RenderNodeImpl;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override {}
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
    };

    struct RenderNodeSubtype
    {
        using ValueType = u32;

        explicit constexpr RenderNodeSubtype(const ValueType value = 0) noexcept : m_value(value) {}
        void SetValue(const ValueType value) noexcept { m_value = value; }
        [[nodiscard]] constexpr ValueType GetValue() const noexcept { return m_value; }
        [[nodiscard]] friend constexpr bool operator==(RenderNodeSubtype, RenderNodeSubtype) noexcept = default;

    private:
        ValueType m_value = 0;
    };

    struct RenderNodeParameters
    {
        RenderNodeParameters() noexcept = default;

        RenderNodeParameters& Set(const RenderNodeType type, const RenderNodeSubtype subtype, RenderNodeImpl* const impl) noexcept
        {
            m_type = type;
            m_subtype = subtype;
            m_impl = impl;
            return *this;
        }

        RenderNodeType m_type = RenderNodeType::Stage;
        RenderNodeSubtype m_subtype;
        RenderNodeImpl* m_impl = nullptr;
    };

    using RenderFlowGroup = u32;
    using RenderFlowSpace = u32;

    struct RenderNodeContext
    {
        RenderNodeContext() noexcept = default;

        RenderNodeContext& Set(const i32 cameraIndex, const RenderFlowGroup gpuRenderFlowGroup, const RenderFlowGroup cpuRenderFlowGroup) noexcept
        {
            m_cameraIndex = cameraIndex;
            m_renderFlowGroups[static_cast<u32>(RenderNodeDependencyType::Gpu)] = gpuRenderFlowGroup;
            m_renderFlowGroups[static_cast<u32>(RenderNodeDependencyType::Cpu)] = cpuRenderFlowGroup;
            return *this;
        }

        i32 m_cameraIndex = -1;
        RenderFlowGroup m_renderFlowGroups[RenderNodeDependencyTypeCount]{};
    };

    class RenderNodeGraph
    {
    public:
        using ItemId = u32;
        static constexpr ItemId InvalidItemId = u32{1} << 31;

        enum class RelationType : u8
        {
            Parent,
            Child,
            Count
        };

        static constexpr RelationType GetInvertedRelation(const RelationType relation) noexcept { return relation == RelationType::Parent ? RelationType::Child : RelationType::Parent; }

    private:
        struct RenderNodeData
        {
            RenderNodeData() noexcept;
            [[nodiscard]] bool HasAnyDependency() const noexcept;
            [[nodiscard]] bool HasAnyDependency(RelationType relation, RenderNodeDependencyType dependencyType) const noexcept;

            RenderNodeParameters m_renderNodeParameters;
            RenderNodeContext m_renderNodeContext;
            ItemId m_dependencies[static_cast<u32>(RelationType::Count)][RenderNodeDependencyTypeCount];
        };

    public:
        struct DependencyData
        {
            RenderNodeDependencyType m_type = RenderNodeDependencyType::Cpu;
            ItemId m_nodes[static_cast<u32>(RelationType::Count)]{InvalidItemId, InvalidItemId};
            ItemId m_nextDependencies[static_cast<u32>(RelationType::Count)]{InvalidItemId, InvalidItemId};
        };

    private:
        using NodesArray = RenderNodeGraphArray<RenderNodeData, u32{1} << 29>;
        using DependenciesArray = RenderNodeGraphArray<DependencyData, u32{1} << 30>;

    public:
        RenderNodeGraph();
        [[nodiscard]] bool IsAllocated(ItemId id) const noexcept;
        [[nodiscard]] ItemId AddNode(const RenderNodeParameters& nodeParameters, i32 cameraIndex);
        [[nodiscard]] ItemId AddDependency(ItemId parentNodeId, ItemId childNodeId, RenderNodeDependencyType dependencyType, bool swapParentChild = false);
        [[nodiscard]] ItemId FindDependency(ItemId parentNodeId, ItemId childNodeId, RenderNodeDependencyType dependencyType, bool swapParentChild = false) const;
        [[nodiscard]] bool HasDependency(ItemId parentNodeId, ItemId childNodeId, RenderNodeDependencyType dependencyType, bool swapParentChild = false) const;
        void RemoveNode(ItemId id, bool mergeDependencies);
        void RemoveDependency(ItemId id);
        void Remove(ItemId id);
        void AddGraph(const RenderNodeGraph& graph, bool enableForceCameraIndex, RenderFlowSpace forcedCameraIndex, bool performMerge);
        void RemoveHelperNodes();
        void BuildRenderFlowGroups();
        [[nodiscard]] bool PrepareResourceBindings(RenderNodeResourceBindings& resourceBindings, RenderNodeResourcePreparationFailures& failures) const;
        void PrepareResourcesParallel(const RenderNodeImplContext& context, RenderFlowResourceAllocator& allocator, RenderNodeResourceBindings& resourceBindings,
                                      RenderNodeResourcePreparationFailures& failures, jobs::Builder& builder) const;
        [[nodiscard]] bool PrepareExecutionPackets(RenderFlowResourceAllocator& allocator, RenderFrameCommandLists& frameCommandLists, RenderNodeResourceBindings& resourceBindings,
                                                   RenderFlowResourceFailure* failure = nullptr) const noexcept;
        void Reset();

        void AcquireExclusiveUpdateFlag();
        void ReleaseExclusiveUpdateFlag();

        void CopyDependencies(ItemId sourceNodeId, ItemId targetNodeId, RelationType relation, RenderNodeDependencyType dependencyType);
        void CopyDependencies(ItemId sourceNodeId, ItemId targetNodeId, RelationType relation);
        void CopyDependencies(ItemId sourceNodeId, ItemId targetNodeId);
        void RemoveNodeDependencies(ItemId nodeId, RelationType relation, RenderNodeDependencyType dependencyType);
        void RemoveNodeDependencies(ItemId nodeId, RelationType relation);
        void RemoveNodeDependencies(ItemId nodeId);

        [[nodiscard]] u32 GetNumNodes() const noexcept { return m_nodes.GetNumItems(); }
        [[nodiscard]] ItemId GetNode(const u32 nodeIndex) const noexcept { return m_nodes.GetItemIdByUsageIndex(nodeIndex); }
        [[nodiscard]] u32 GetNumDependencies() const noexcept { return m_dependencies.GetNumItems(); }
        [[nodiscard]] const DependencyData& GetDependency(const u32 dependencyIndex) const noexcept { return m_dependencies[m_dependencies.GetItemIdByUsageIndex(dependencyIndex)]; }
        [[nodiscard]] u32 GetNodeIndex(const ItemId nodeId) const noexcept { return m_nodes.GetItemUsageIndex(nodeId); }
        [[nodiscard]] const RenderNodeParameters& GetNodeParameters(const ItemId nodeId) const noexcept { return m_nodes[nodeId].m_renderNodeParameters; }
        [[nodiscard]] RenderNodeParameters& RefNodeParameters(const ItemId nodeId) noexcept { return m_nodes[nodeId].m_renderNodeParameters; }
        [[nodiscard]] const RenderNodeContext& GetNodeContext(const ItemId nodeId) const noexcept { return m_nodes[nodeId].m_renderNodeContext; }
        [[nodiscard]] RenderNodeContext& RefNodeContext(const ItemId nodeId) noexcept { return m_nodes[nodeId].m_renderNodeContext; }

    private:
        void MergeNodes(u32 numOriginalUsedNodeIndices);
        void CalculateNodeLevelInRenderFlowGroupRecursive(RenderNodeDependencyType dependencyType, ItemId nodeItemId, containers::DynamicArray<u32>& levelNodeCounts);
        void BuildFlattenedNodesArray(containers::DynamicArray<ItemId>& nodes, RenderNodeDependencyType dependencyType) const;

        concurrency::SpinLock m_updateFlag;
        NodesArray m_nodes;
        DependenciesArray m_dependencies;
        bool m_isBuilt = false;

        friend class RenderNodeJob;
    };
} // namespace vanguard::rendering

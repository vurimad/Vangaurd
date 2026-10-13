#include <vanguard/rendering/render_node_impl_context.hpp>

#include <vanguard/rendering/render_camera.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_flow_resource_internal.hpp>
#include <vanguard/rendering/viewport.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        [[nodiscard]] constexpr u32 ResourceLookupHash(const FlowSpaceId flowSpace, const containers::StringView name, const FrameResourceKind kind) noexcept
        {
            const LogicalResourceKey key(flowSpace, LogicalResourceName(name));
            u32 result = key.CalcHash() ^ ((static_cast<u32>(kind) + 1u) * 0x9e3779b9u);
            result ^= result >> 16u;
            result *= 0x7feb352du;
            result ^= result >> 15u;
            result *= 0x846ca68bu;
            result ^= result >> 16u;
            return result;
        }
    } // namespace

    RenderNodeResources::RenderNodeResources() noexcept
        : m_uses(memory::pools::Rendering::GetInstance()), m_decisions(memory::pools::Rendering::GetInstance()), m_ranges(memory::pools::Rendering::GetInstance()),
          m_lookupBuckets(memory::pools::Rendering::GetInstance()), m_lookupGroups(memory::pools::Rendering::GetInstance()),
          m_resolvedTextures(memory::pools::Rendering::GetInstance()), m_resolvedBuffers(memory::pools::Rendering::GetInstance()),
          m_warmRangeExecutions(memory::pools::Rendering::GetInstance()), m_warmLookupRuntimeNextUses(memory::pools::Rendering::GetInstance())
    {
    }

    void RenderNodeResources::Reset() noexcept
    {
        m_resolvedTextures.Clear();
        m_resolvedBuffers.Clear();
        ReleaseWarmContractView();
        m_lookupBuckets.Clear();
        m_lookupGroups.Clear();
        m_uses.Clear();
        m_decisions.Clear();
        m_ranges.Clear();
    }

    void RenderNodeResources::BindWarmContract(const detail::ResourceContractNodeTemplate& source, const u32 generation) noexcept
    {
        Reset();
        m_warmTemplate = &source;
        m_warmGeneration = generation;
        m_warmRangeExecutions.Resize(source.ranges.Size());
        for (CompiledExecutionRange& execution : m_warmRangeExecutions)
            execution = {};
        m_warmLookupRuntimeNextUses.Resize(source.lookupGroups.Size());
        for (u32 index = 0; index < source.lookupGroups.Size(); ++index)
            m_warmLookupRuntimeNextUses[index] = source.lookupGroups[index].firstUse;
        m_resolvedTextures.Resize(source.textureBindingCount);
        m_resolvedBuffers.Resize(source.bufferBindingCount);
    }

    void RenderNodeResources::ReleaseWarmContractView() noexcept
    {
        m_warmTemplate = nullptr;
        m_warmGeneration = 0;
        m_warmRangeExecutions.Clear();
        m_warmLookupRuntimeNextUses.Clear();
    }

    u32 RenderNodeResources::UseCount() const noexcept
    {
        return IsWarmContract() ? m_warmTemplate->uses.Size() : m_uses.Size();
    }

    u32 RenderNodeResources::DecisionCount() const noexcept
    {
        return IsWarmContract() ? m_warmTemplate->decisions.Size() : m_decisions.Size();
    }

    u32 RenderNodeResources::RangeCount() const noexcept
    {
        return IsWarmContract() ? m_warmTemplate->ranges.Size() : m_ranges.Size();
    }

    u32 RenderNodeResources::LookupBucketCount() const noexcept
    {
        return IsWarmContract() ? m_warmTemplate->lookupBuckets.Size() : m_lookupBuckets.Size();
    }

    u32 RenderNodeResources::LookupGroupCount() const noexcept
    {
        return IsWarmContract() ? m_warmTemplate->lookupGroups.Size() : m_lookupGroups.Size();
    }

    u32 RenderNodeResources::TextureBindingCount() const noexcept
    {
        return IsWarmContract() ? m_warmTemplate->textureBindingCount : m_resolvedTextures.Size();
    }

    u32 RenderNodeResources::BufferBindingCount() const noexcept
    {
        return IsWarmContract() ? m_warmTemplate->bufferBindingCount : m_resolvedBuffers.Size();
    }

    RenderNodeResourceUseView RenderNodeResources::GetUse(const u32 index) const noexcept
    {
        if (IsWarmContract())
        {
            VG_ASSERT(index < m_warmTemplate->uses.Size());
            const detail::ResourceContractUseTemplate& source = m_warmTemplate->uses[index];
            return {source.flowSpace, containers::StringView(source.name), source.lookupHash,
                    {source.flowGroup, source.ordinal, m_warmGeneration}, source.kind, source.nextDuplicate, source.resolvedIndex};
        }
        VG_ASSERT(index < m_uses.Size());
        const RenderNodeResourceUseBinding& source = m_uses[index];
        return {source.flowSpace, containers::StringView(source.name), source.lookupHash, source.use, source.kind, source.nextDuplicate, source.resolvedIndex};
    }

    DecisionId RenderNodeResources::GetDecision(const u32 index) const noexcept
    {
        if (IsWarmContract())
        {
            VG_ASSERT(index < m_warmTemplate->decisions.Size());
            const detail::ResourceContractDecisionTemplate& source = m_warmTemplate->decisions[index];
            return {source.flowGroup, source.ordinal, m_warmGeneration};
        }
        VG_ASSERT(index < m_decisions.Size());
        return m_decisions[index].decision;
    }

    RenderNodeResourceRange RenderNodeResources::GetRange(const u32 index) const noexcept
    {
        if (IsWarmContract())
        {
            VG_ASSERT(index < m_warmTemplate->ranges.Size() && index < m_warmRangeExecutions.Size());
            const detail::ResourceContractRangeTemplate& source = m_warmTemplate->ranges[index];
            return {source.firstUse, source.useCount, source.firstDecision, source.decisionCount,
                    source.lookupBucketOffset, source.lookupBucketCount, source.lookupGroupOffset, source.lookupGroupCount,
                    m_warmRangeExecutions[index]};
        }
        VG_ASSERT(index < m_ranges.Size());
        return m_ranges[index];
    }

    u32 RenderNodeResources::GetLookupBucket(const u32 index) const noexcept
    {
        if (IsWarmContract())
        {
            VG_ASSERT(index < m_warmTemplate->lookupBuckets.Size());
            return m_warmTemplate->lookupBuckets[index];
        }
        VG_ASSERT(index < m_lookupBuckets.Size());
        return m_lookupBuckets[index];
    }

    RenderNodeResourceLookupGroup RenderNodeResources::GetLookupGroup(const u32 index) const noexcept
    {
        if (IsWarmContract())
        {
            VG_ASSERT(index < m_warmTemplate->lookupGroups.Size() && index < m_warmLookupRuntimeNextUses.Size());
            const detail::ResourceContractLookupGroupTemplate& source = m_warmTemplate->lookupGroups[index];
            return {source.lookupHash, source.firstUse, source.lastUse, m_warmLookupRuntimeNextUses[index], source.nextBucketGroup};
        }
        VG_ASSERT(index < m_lookupGroups.Size());
        return m_lookupGroups[index];
    }

    void RenderNodeResources::SetLookupGroupRuntimeNextUse(const u32 index, const u32 nextUse) noexcept
    {
        if (IsWarmContract())
        {
            VG_ASSERT(index < m_warmLookupRuntimeNextUses.Size());
            m_warmLookupRuntimeNextUses[index] = nextUse;
            return;
        }
        VG_ASSERT(index < m_lookupGroups.Size());
        m_lookupGroups[index].runtimeNextUse = nextUse;
    }

    void RenderNodeResources::ClearRangeExecutions() noexcept
    {
        if (IsWarmContract())
        {
            for (CompiledExecutionRange& execution : m_warmRangeExecutions)
                execution = {};
            return;
        }
        for (RenderNodeResourceRange& range : m_ranges)
            range.execution = {};
    }

    void RenderNodeResources::SetRangeExecution(const u32 index, const CompiledExecutionRange& execution) noexcept
    {
        if (IsWarmContract())
        {
            VG_ASSERT(index < m_warmRangeExecutions.Size());
            m_warmRangeExecutions[index] = execution;
            return;
        }
        VG_ASSERT(index < m_ranges.Size());
        m_ranges[index].execution = execution;
    }

    RenderNodeResourceBindings::RenderNodeResourceBindings() noexcept : m_entries(memory::pools::Rendering::GetInstance()) {}

    RenderNodeResourceBindings::~RenderNodeResourceBindings()
    {
        Reset();
    }

    void RenderNodeResourceBindings::Reset() noexcept
    {
        for (Entry& entry : m_entries)
        {
            if (entry.cursor.IsValid())
                entry.cursor.CancelRemaining();
            if (entry.executionContext != nullptr)
            {
                entry.executionContext->~RenderNodeImplContext();
                entry.executionContext = nullptr;
            }
        }
        m_entries.Clear();
        if (m_executionContextStorage != nullptr)
        {
            memory::MemoryBlock block{m_executionContextStorage, static_cast<usize>(m_executionContextCount) * sizeof(RenderNodeImplContext), memory::PoolId::Rendering};
            memory::Free(block);
            m_executionContextStorage = nullptr;
            m_executionContextCount = 0;
        }
    }

    void RenderNodeResourceBindings::ReleaseWarmContractViews() noexcept
    {
        for (Entry& entry : m_entries)
            entry.resources.ReleaseWarmContractView();
    }

    void RenderNodeResourceBindings::BlockExecution(const RenderFlowResourceFailureCode code, const char* const message) noexcept
    {
        for (Entry& entry : m_entries)
        {
            entry.executionFailure = {};
            entry.executionFailure.code = code;
            entry.executionFailure.phase = RenderFlowResourceSessionState::Executing;
            entry.executionFailure.node = entry.node;
            entry.executionFailure.message = message;
        }
    }

    bool RenderNodeResourceBindings::GetFirstExecutionFailure(RenderFlowResourceFailure& failure) const noexcept
    {
        failure = {};
        for (const Entry& entry : m_entries)
        {
            if (entry.executionFailure.code == RenderFlowResourceFailureCode::None)
                continue;
            failure = entry.executionFailure;
            return true;
        }
        return false;
    }

    RenderNodeImplContext::RenderNodeImplContext() noexcept = default;

    RenderNodeImplContext::RenderNodeImplContext(const RenderNodeImplContext& other) noexcept
        : RenderNodeImplContext(other, other.m_dispatcherThreadIndex)
    {
    }

    RenderNodeImplContext::RenderNodeImplContext(const RenderNodeImplContext& other, const u32 dispatcherThreadIndex) noexcept
        : m_frame(other.m_frame), m_viewFamily(other.m_viewFamily), m_cameraStorage(other.m_cameraStorage), m_geometryFrameWork(other.m_geometryFrameWork),
          m_frameCommandLists(other.m_frameCommandLists),
          m_outputTransaction(other.m_outputTransaction), m_view(other.m_view), m_cameraIndex(other.m_cameraIndex), m_dispatcherThreadIndex(dispatcherThreadIndex), m_renderFlowSpace(other.m_renderFlowSpace),
          m_renderFlowGroup(other.m_renderFlowGroup)
    {
    }

    void RenderNodeImplContext::Init(const InitData& initData) noexcept
    {
        if (initData.frame == nullptr)
            VG_FATAL("render-node context requires retained frame information");
        if (initData.dispatcherThreadIndex == ~u32{0})
            VG_FATAL("render-node context requires a dispatcher thread index");

        m_frame = initData.frame;
        m_viewFamily = initData.skipCameraData ? nullptr : initData.viewFamily;
        m_cameraStorage = initData.skipCameraData ? nullptr : initData.cameraStorage;
        m_geometryFrameWork = initData.geometryFrameWork;
        m_frameCommandLists = initData.frameCommandLists;
        m_outputTransaction = initData.outputTransaction;
        m_dispatcherThreadIndex = initData.dispatcherThreadIndex;
        ResetNodeData();
    }

    void RenderNodeImplContext::MarkPresentNodeReached() const noexcept
    {
        if (m_outputTransaction != nullptr && m_outputTransaction->IsValid())
            m_outputTransaction->MarkPresentNodeReached();
    }

    bool RenderNodeImplContext::HasPresentationOutput() const noexcept
    {
        return m_outputTransaction != nullptr && m_outputTransaction->IsValid() &&
               m_outputTransaction->GetKind() == RenderViewportOutputKind::Presentation &&
               m_outputTransaction->m_resourceImportIndex != InvalidRenderFlowResourceIndex && m_outputTransaction->m_resourceImportGeneration != 0;
    }

    void RenderNodeImplContext::SetupNodeData(const RenderNodeContext& nodeContext, const RenderNodeParameters& nodeParameters) noexcept
    {
        m_cameraIndex = nodeContext.m_cameraIndex;
        m_renderFlowGroup = nodeContext.m_renderFlowGroups[static_cast<u32>(RenderNodeDependencyType::Gpu)];
        const bool isNodeUnique = nodeParameters.m_type == RenderNodeType::Unique;
        m_view = nullptr;

        if (isNodeUnique)
        {
            m_renderFlowSpace = 0;
        }
        else if (m_viewFamily != nullptr && m_cameraIndex >= 0)
        {
            const containers::ArraySpan<const RenderView> views = m_viewFamily->GetViews();
            const u32 viewIndex = static_cast<u32>(m_cameraIndex);
            if (viewIndex >= views.Size())
                VG_FATAL("render-node camera index lies outside the prepared view family");
            m_view = &views[viewIndex];
            m_renderFlowSpace = viewIndex + 1;
        }
        else
        {
            // Preserve the no-camera sentinel. Zero remains reserved for unique
            // frame-global resource names.
            m_renderFlowSpace = 0xffu;
        }

    }

    void RenderNodeImplContext::ResetNodeData() noexcept
    {
        if (m_operation == Operation::DeclareResources)
            EndResourceDeclaration();
        else if (m_operation == Operation::Execute)
            EndResourceExecution();
        m_view = nullptr;
        m_cameraIndex = -1;
        m_renderFlowSpace = 0xffu;
        m_renderFlowGroup = ~RenderFlowGroup{0};
        BeginNewNode();
    }

    void RenderNodeImplContext::BeginResourceDeclaration(RenderFlowResourceAllocator& allocator, ResourcePlanningWriter* const writer, RenderNodeResources& resources,
                                                         RenderFlowResourceFailure& failure) noexcept
    {
        if (m_operation != Operation::None || (writer != nullptr && !writer->IsValid()))
            VG_FATAL("render-node resource declaration requires an unbound context and an optional valid planning writer");
        resources.Reset();
        m_resourceWriter = writer;
        m_resourceAllocator = &allocator;
        m_resourceFailure = &failure;
        m_nodeResources = &resources;
        m_resourceRange = InvalidRenderFlowResourceIndex;
        m_nextResourceRange = 0;
        m_operation = Operation::DeclareResources;
    }

    void RenderNodeImplContext::EndResourceDeclaration() noexcept
    {
        EndNodeResourceUses();
        m_resourceWriter = nullptr;
        m_resourceAllocator = nullptr;
        m_resourceFailure = nullptr;
        m_nodeResources = nullptr;
        m_resourceRange = InvalidRenderFlowResourceIndex;
        m_operation = Operation::None;
    }

    void RenderNodeImplContext::BeginResourceContractPatch(RenderFlowResourceAllocator& allocator, RenderNodeResources& resources,
                                                           RenderFlowResourceFailure& failure) noexcept
    {
        if (m_operation != Operation::None)
            VG_FATAL("render-node resource-contract patch requires an unbound context");
        m_resourceWriter = nullptr;
        m_resourceAllocator = &allocator;
        m_resourceFailure = &failure;
        m_nodeResources = &resources;
        m_resourceRange = InvalidRenderFlowResourceIndex;
        m_nextResourceRange = 0;
        m_nextResourceDecision = 0;
        m_operation = Operation::PatchResourceContract;
    }

    void RenderNodeImplContext::EndResourceContractPatch() noexcept
    {
        EndNodeResourceUses();
        if (m_resourceFailure != nullptr && m_resourceFailure->code == RenderFlowResourceFailureCode::None && m_nodeResources != nullptr &&
            m_nextResourceRange != m_nodeResources->RangeCount())
            FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning,
                                  "resource-contract patch did not visit every compiled child range");
        m_resourceWriter = nullptr;
        m_resourceAllocator = nullptr;
        m_resourceFailure = nullptr;
        m_nodeResources = nullptr;
        m_resourceRange = InvalidRenderFlowResourceIndex;
        m_operation = Operation::None;
    }

    void RenderNodeImplContext::BeginResourceExecution(CompiledExecutionPacketView* const packet, ExecutionPacketCursor& cursor, RenderNodeResources& resources,
                                                       RenderFlowResourceFailure& failure) noexcept
    {
        if (m_operation != Operation::None || cursor.IsValid() || (packet != nullptr && !packet->IsValid()))
            VG_FATAL("render-node resource execution requires an unbound context, a closed cursor, and an optional valid packet");
        m_resourceFailure = &failure;
        m_nodeResources = &resources;
        m_executionPacket = packet;
        m_resourceCursor = &cursor;
        m_resourceRange = InvalidRenderFlowResourceIndex;
        m_nextResourceRange = 0;
        m_nextResourceDecision = 0;
        if (packet != nullptr && resources.TextureBindingCount() + resources.BufferBindingCount() != resources.UseCount())
            VG_FATAL("render-node typed resource slots do not match their declarations");
        m_operation = Operation::Execute;
    }

    bool RenderNodeImplContext::OpenResourceExecutionPacket() noexcept
    {
        if (m_operation != Operation::Execute || m_resourceFailure == nullptr || m_resourceCursor == nullptr)
            VG_FATAL("render-node packet opening is available only during execution");
        if (m_executionPacket == nullptr)
            return true;
        if (m_resourceCursor->IsValid())
            return true;
        const u32 expectedRangeCount = m_nodeResources != nullptr ? m_nodeResources->RangeCount() : 0u;
        return m_executionPacket->OpenCompiledCursor(m_executionPacket->GetCommandScope(), m_executionPacket->GetQueue(), expectedRangeCount,
                                                     *m_resourceCursor, m_resourceFailure);
    }

    void RenderNodeImplContext::EndResourceExecution() noexcept
    {
        if (m_resourceFailure != nullptr && m_resourceFailure->code != RenderFlowResourceFailureCode::None)
            VG_FATAL(m_resourceFailure->message != nullptr ? m_resourceFailure->message : "render-node recording failed");
        EndNodeResourceUses();
        if (m_resourceFailure != nullptr && m_resourceFailure->code == RenderFlowResourceFailureCode::None && m_nodeResources != nullptr &&
            m_nextResourceRange != m_nodeResources->RangeCount())
            FailResourceOperation(RenderFlowResourceFailureCode::IncompleteExecution, "render-node occurrence did not consume every declared child resource range");
        if (m_resourceCursor != nullptr && m_resourceCursor->IsValid())
        {
            if (!m_resourceCursor->FinalizePacket(m_resourceFailure))
                VG_FATAL(m_resourceFailure != nullptr && m_resourceFailure->message != nullptr ? m_resourceFailure->message : "render-node packet finalization failed");
        }
        m_resourceFailure = nullptr;
        m_nodeResources = nullptr;
        m_executionPacket = nullptr;
        m_resourceCursor = nullptr;
        m_resourceRange = InvalidRenderFlowResourceIndex;
        m_operation = Operation::None;
    }

    void RenderNodeImplContext::BeginNewNode() const noexcept
    {
        if (m_nodeResources == nullptr)
            return;
        EndNodeResourceUses();
        if (IsDeclaringResources())
        {
            RenderNodeResourceRange range;
            range.firstUse = m_nodeResources->m_uses.Size();
            range.firstDecision = m_nodeResources->m_decisions.Size();
            m_nodeResources->m_ranges.PushBack(range);
            m_resourceRange = m_nodeResources->m_ranges.Size() - 1;
            return;
        }
        if (m_operation == Operation::Execute)
        {
            if (m_nextResourceRange >= m_nodeResources->RangeCount())
            {
                FailResourceOperation(RenderFlowResourceFailureCode::IncompleteExecution, "render-node execution has no matching declared resource range");
                return;
            }
            m_resourceRange = m_nextResourceRange++;
            const RenderNodeResourceRange range = m_nodeResources->GetRange(m_resourceRange);
            m_nextResourceDecision = range.firstDecision;
            if (m_executionPacket != nullptr && m_resourceFailure->code == RenderFlowResourceFailureCode::None &&
                !m_resourceCursor->BeginRange(range.execution, m_resourceRange + 1u, m_resourceFailure))
                VG_FATAL(m_resourceFailure->message != nullptr ? m_resourceFailure->message : "render-node compiled resource range begin failed");
        }
        else if (IsPatchingResourceContract())
        {
            if (m_nextResourceRange >= m_nodeResources->RangeCount())
            {
                FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning,
                                      "resource-contract patch has no matching compiled child range");
                return;
            }
            m_resourceRange = m_nextResourceRange++;
            const RenderNodeResourceRange range = m_nodeResources->GetRange(m_resourceRange);
            m_nextResourceDecision = range.firstDecision;
        }
    }

    void RenderNodeImplContext::SetCustomDataView(const RenderView* const view, const i32 cameraIndex) noexcept
    {
        if (m_operation != Operation::None)
            VG_FATAL("custom-data view selection cannot change during node resource processing");
        m_view = view;
        m_cameraIndex = cameraIndex;
        m_renderFlowSpace = view != nullptr && cameraIndex >= 0 ? static_cast<RenderFlowSpace>(cameraIndex) + 1u : 0xffu;
    }

    void RenderNodeImplContext::EndNodeResourceUses() const noexcept
    {
        if (m_nodeResources == nullptr || m_resourceRange >= m_nodeResources->RangeCount())
            return;
        if (IsDeclaringResources())
        {
            RenderNodeResourceRange& range = m_nodeResources->m_ranges[m_resourceRange];
            range.useCount = m_nodeResources->m_uses.Size() - range.firstUse;
            range.decisionCount = m_nodeResources->m_decisions.Size() - range.firstDecision;
            range.lookupBucketOffset = m_nodeResources->m_lookupBuckets.Size();
            range.lookupBucketCount = 0;
            range.lookupGroupOffset = m_nodeResources->m_lookupGroups.Size();
            range.lookupGroupCount = 0;
            if (m_resourceFailure->code == RenderFlowResourceFailureCode::None && range.useCount != 0)
            {
                if (range.useCount > 0x40000000u)
                {
                    FailResourceOperation(RenderFlowResourceFailureCode::CapacityExceeded, "render-node resource lookup image exceeds addressable capacity");
                }
                else
                {
                    const u32 desiredBuckets = range.useCount * 2u;
                    u32 bucketCount = 1u;
                    while (bucketCount < desiredBuckets)
                        bucketCount <<= 1u;
                    if (bucketCount > InvalidRenderFlowResourceIndex - m_nodeResources->m_lookupBuckets.Size() ||
                        range.useCount > InvalidRenderFlowResourceIndex - m_nodeResources->m_lookupGroups.Size())
                    {
                        FailResourceOperation(RenderFlowResourceFailureCode::CapacityExceeded, "render-node resource lookup storage exceeds addressable capacity");
                    }
                    else
                    {
                        range.lookupBucketCount = bucketCount;
                        m_nodeResources->m_lookupBuckets.Resize(range.lookupBucketOffset + bucketCount, InvalidRenderFlowResourceIndex);
                        m_nodeResources->m_lookupGroups.Reserve(m_nodeResources->m_lookupGroups.Size() + range.useCount);
                    }
                }
            }
            if (m_resourceWriter != nullptr && m_resourceFailure->code == RenderFlowResourceFailureCode::None)
            {
                for (u32 index = range.useCount; index > 0; --index)
                {
                    const u32 localIndex = index - 1u;
                    RenderNodeResourceUseBinding& binding = m_nodeResources->m_uses[range.firstUse + localIndex];
                    if (!binding.use.IsValid() || binding.use.ordinal != range.firstUse + localIndex)
                    {
                        FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning, "render-node declaration does not match the dense resource-use order");
                        break;
                    }

                    const u32 bucket = range.lookupBucketOffset + (binding.lookupHash & (range.lookupBucketCount - 1u));
                    u32 groupIndex = m_nodeResources->m_lookupBuckets[bucket];
                    u32 matchingGroup = InvalidRenderFlowResourceIndex;
                    while (groupIndex != InvalidRenderFlowResourceIndex)
                    {
                        if (groupIndex < range.lookupGroupOffset || groupIndex >= m_nodeResources->m_lookupGroups.Size())
                        {
                            FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning, "render-node declaration lookup bucket is malformed");
                            break;
                        }
                        const RenderNodeResourceLookupGroup& candidate = m_nodeResources->m_lookupGroups[groupIndex];
                        if (candidate.firstUse >= range.useCount || range.firstUse + candidate.firstUse >= m_nodeResources->m_uses.Size())
                        {
                            FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning, "render-node declaration lookup group is malformed");
                            break;
                        }
                        const RenderNodeResourceUseBinding& firstBinding = m_nodeResources->m_uses[range.firstUse + candidate.firstUse];
                        if (candidate.lookupHash == binding.lookupHash && firstBinding.kind == binding.kind &&
                            firstBinding.flowSpace == binding.flowSpace && containers::StringView(firstBinding.name) == containers::StringView(binding.name))
                        {
                            matchingGroup = groupIndex;
                            break;
                        }
                        groupIndex = candidate.nextBucketGroup;
                    }
                    if (m_resourceFailure->code != RenderFlowResourceFailureCode::None)
                        break;

                    binding.nextDuplicate = InvalidRenderFlowResourceIndex;
                    if (matchingGroup == InvalidRenderFlowResourceIndex)
                    {
                        RenderNodeResourceLookupGroup group;
                        group.lookupHash = binding.lookupHash;
                        group.firstUse = localIndex;
                        group.lastUse = localIndex;
                        group.runtimeNextUse = localIndex;
                        group.nextBucketGroup = m_nodeResources->m_lookupBuckets[bucket];
                        m_nodeResources->m_lookupGroups.PushBack(group);
                        m_nodeResources->m_lookupBuckets[bucket] = m_nodeResources->m_lookupGroups.Size() - 1u;
                    }
                    else
                    {
                        RenderNodeResourceLookupGroup& group = m_nodeResources->m_lookupGroups[matchingGroup];
                        if (group.firstUse <= localIndex || group.lastUse < group.firstUse)
                        {
                            FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning, "render-node duplicate resource lookup group is malformed");
                            break;
                        }
                        binding.nextDuplicate = group.firstUse;
                        group.firstUse = localIndex;
                        group.runtimeNextUse = localIndex;
                    }

                    if (!m_resourceWriter->EndUse(binding.use, m_resourceFailure))
                        break;
                }
            }
            range.lookupGroupCount = m_nodeResources->m_lookupGroups.Size() - range.lookupGroupOffset;
        }
        else if (m_operation == Operation::Execute)
        {
            const RenderNodeResourceRange range = m_nodeResources->GetRange(m_resourceRange);
            if (m_resourceFailure->code != RenderFlowResourceFailureCode::None)
                VG_FATAL(m_resourceFailure->message != nullptr ? m_resourceFailure->message : "render-node recording failed");
            if (m_nextResourceDecision != range.firstDecision + range.decisionCount)
                FailResourceOperation(RenderFlowResourceFailureCode::IncompleteExecution, "render node did not consume its declared resource decisions");
            if (m_executionPacket != nullptr && !m_resourceCursor->EndRange(range.execution, m_resourceRange + 1u, m_resourceFailure))
                VG_FATAL(m_resourceFailure->message != nullptr ? m_resourceFailure->message : "render-node compiled resource range end failed");
        }
        else if (IsPatchingResourceContract())
        {
            const RenderNodeResourceRange range = m_nodeResources->GetRange(m_resourceRange);
            if (m_resourceFailure->code == RenderFlowResourceFailureCode::None &&
                m_nextResourceDecision != range.firstDecision + range.decisionCount)
                FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning,
                                      "resource-contract patch did not provide every dynamic decision in the compiled child range");
        }
        m_resourceRange = InvalidRenderFlowResourceIndex;
    }

    CommandScopeId RenderNodeImplContext::GetCommandScope() const noexcept
    {
        return m_executionPacket != nullptr ? m_executionPacket->GetCommandScope() : CommandScopeId{};
    }

    RenderFlowNameTag RenderNodeImplContext::RTNameTag(const LogicalResourceName name) const noexcept
    {
        const LogicalResourceKey key{{m_renderFlowSpace}, name};
        return {key, {}, FrameResourceKind::Invalid};
    }

    RenderFlowNameTag RenderNodeImplContext::RTSharedNameTag(const LogicalResourceName name) noexcept
    {
        const LogicalResourceKey key{{0}, name};
        return {key, {}, FrameResourceKind::Invalid};
    }

    RenderFlowNameTag RenderNodeImplContext::RTCameraNameTag(const u32 viewIndex, const LogicalResourceName name) noexcept
    {
        VG_ASSERT(viewIndex < MaximumRenderViewsPerFamily);
        const LogicalResourceKey key{{viewIndex + 1u}, name};
        return {key, {}, FrameResourceKind::Invalid};
    }

    bool RenderNodeImplContext::CanRecordResourceOperation() const noexcept
    {
        if (IsPatchingResourceContract())
        {
            // RF12 warm patching is deliberately decision-only.  A node that
            // accidentally replays a declaration/resource mutation here must
            // reject the warm bind and fall back to a cold compile; it must not
            // turn an otherwise recoverable contract miss into a process fatal.
            if (m_resourceFailure == nullptr || m_nodeResources == nullptr)
                VG_FATAL("render-node resource-contract patch has no active resource state");
            FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning,
                                  "resource-contract patch attempted to replay a static resource operation");
            return false;
        }
        if (!IsDeclaringResources() || m_resourceFailure == nullptr || m_nodeResources == nullptr)
            VG_FATAL("render-node resource operation is available only during resource declaration");
        if (m_resourceFailure->code != RenderFlowResourceFailureCode::None)
            return false;
        if (m_resourceWriter != nullptr)
            return true;
        m_resourceFailure->code = RenderFlowResourceFailureCode::IncompletePlanning;
        m_resourceFailure->phase = RenderFlowResourceSessionState::Planning;
        m_resourceFailure->message = "a command-list-free render node attempted to declare a GPU resource operation";
        return false;
    }

    bool RenderNodeImplContext::CanResolveResourceOperation() const noexcept
    {
        if (m_operation != Operation::Execute || m_resourceFailure == nullptr || m_nodeResources == nullptr || m_resourceCursor == nullptr)
            VG_FATAL("render-node resource resolution is available only during execution");
        if (m_resourceFailure->code != RenderFlowResourceFailureCode::None || m_resourceRange >= m_nodeResources->RangeCount())
            VG_FATAL("render-node resource lookup requires a valid active resource range");
        return true;
    }

    void RenderNodeImplContext::FailResourceOperation(const RenderFlowResourceFailureCode code, const char* const message) const noexcept
    {
        if (m_operation == Operation::Execute)
            VG_FATAL(message != nullptr ? message : "required render-node operation failed");
        if (m_resourceFailure == nullptr || m_resourceFailure->code != RenderFlowResourceFailureCode::None)
            return;
        m_resourceFailure->code = code;
        m_resourceFailure->phase = RenderFlowResourceSessionState::Planning;
        m_resourceFailure->message = message;
    }

    RenderFlowNameTag RenderNodeImplContext::ResolveNamedTag(const RenderFlowNameTag& tag) const noexcept
    {
        if (!tag.IsValid() || tag.resource.IsValid() || !CanRecordResourceOperation())
            return tag;
        RenderFlowNameTag resolved = tag;
        if (!GetResourceWriter().ReferenceResource(tag.key, resolved.resource, m_resourceFailure))
            resolved.resource = {};
        return resolved;
    }

    RenderFlowNameTag RenderNodeImplContext::MakeTemporaryTag(const containers::StringView name, const LogicalResourceId resource, const FrameResourceKind kind) const noexcept
    {
        // Temporary identities never participate in the named-resource index.
        // Avoid paying the named hash cost merely to retain the diagnostic name.
        const LogicalResourceKey key = LogicalResourceKey::Temporary({m_renderFlowSpace}, name);
        return {key, resource, kind};
    }

    RenderFlowNameTag RenderNodeImplContext::RTAlloc(const LogicalResourceName name, const FrameTextureDesc& desc) const noexcept
    {
        RenderFlowNameTag tag = RTNameTag(name);
        tag.kind = FrameResourceKind::Texture;
        if (CanRecordResourceOperation() && !GetResourceWriter().DeclareTexture(tag.key, desc, tag.resource, m_resourceFailure))
            tag.resource = {};
        return tag;
    }

    RenderFlowNameTag RenderNodeImplContext::RTAlloc(const LogicalResourceName name, const FrameBufferDesc& desc) const noexcept
    {
        RenderFlowNameTag tag = RTNameTag(name);
        tag.kind = FrameResourceKind::Buffer;
        if (CanRecordResourceOperation() && !GetResourceWriter().DeclareBuffer(tag.key, desc, tag.resource, m_resourceFailure))
            tag.resource = {};
        return tag;
    }

    RenderFlowNameTag RenderNodeImplContext::RTAlloc(const LogicalResourceName name, const RenderFlowNameTag& desc) const noexcept
    {
        const RenderFlowNameTag source = ResolveNamedTag(desc);
        RenderFlowNameTag tag = RTNameTag(name);
        tag.kind = source.kind;
        if (CanRecordResourceOperation() && !GetResourceWriter().DeclareLike(tag.key, source.resource, tag.resource, m_resourceFailure))
            tag.resource = {};
        return tag;
    }

    RenderFlowNameTag RenderNodeImplContext::RTSharedAlloc(const LogicalResourceName name, const FrameTextureDesc& desc) const noexcept
    {
        RenderFlowNameTag tag = RTSharedNameTag(name);
        tag.kind = FrameResourceKind::Texture;
        if (CanRecordResourceOperation() && !GetResourceWriter().DeclareTexture(tag.key, desc, tag.resource, m_resourceFailure))
            tag.resource = {};
        return tag;
    }

    RenderFlowNameTag RenderNodeImplContext::RTSharedAlloc(const LogicalResourceName name, const FrameBufferDesc& desc) const noexcept
    {
        RenderFlowNameTag tag = RTSharedNameTag(name);
        tag.kind = FrameResourceKind::Buffer;
        if (CanRecordResourceOperation() && !GetResourceWriter().DeclareBuffer(tag.key, desc, tag.resource, m_resourceFailure))
            tag.resource = {};
        return tag;
    }

    RenderFlowNameTag RenderNodeImplContext::RTTempAlloc(const containers::StringView name, const FrameTextureDesc& desc) const noexcept
    {
        LogicalResourceId resource;
        if (CanRecordResourceOperation() && !GetResourceWriter().DeclareTemporaryTexture(name, desc, resource, m_resourceFailure))
            resource = {};
        return MakeTemporaryTag(name, resource, FrameResourceKind::Texture);
    }

    RenderFlowNameTag RenderNodeImplContext::RTTempAlloc(const containers::StringView name, const FrameBufferDesc& desc) const noexcept
    {
        LogicalResourceId resource;
        if (CanRecordResourceOperation() && !GetResourceWriter().DeclareTemporaryBuffer(name, desc, resource, m_resourceFailure))
            resource = {};
        return MakeTemporaryTag(name, resource, FrameResourceKind::Buffer);
    }

    RenderFlowNameTag RenderNodeImplContext::RTTempAlloc(const containers::StringView name, const RenderFlowNameTag& desc) const noexcept
    {
        const RenderFlowNameTag source = ResolveNamedTag(desc);
        LogicalResourceId resource;
        if (CanRecordResourceOperation() && !GetResourceWriter().DeclareTemporaryLike(name, source.resource, resource, m_resourceFailure))
            resource = {};
        return MakeTemporaryTag(name, resource, source.kind);
    }

    RenderFlowNameTag RenderNodeImplContext::RTInject(const LogicalResourceName name, const ImportedResourceId imported) const noexcept
    {
        RenderFlowNameTag tag = RTNameTag(name);
        tag.kind = FrameResourceKind::Texture;
        if (CanRecordResourceOperation() && !GetResourceWriter().ImportTexture(tag.key, imported, tag.resource, m_resourceFailure))
            tag.resource = {};
        return tag;
    }

    RenderFlowNameTag RenderNodeImplContext::RTSharedInjectBuffer(const LogicalResourceName name, const ImportedResourceId imported) const noexcept
    {
        RenderFlowNameTag tag = RTSharedNameTag(name);
        tag.kind = FrameResourceKind::Buffer;
        if (CanRecordResourceOperation() && !GetResourceWriter().ImportBuffer(tag.key, imported, tag.resource, m_resourceFailure))
            tag.resource = {};
        return tag;
    }

    bool RenderNodeImplContext::HasFrameOutput() const noexcept
    {
        return m_outputTransaction != nullptr && m_outputTransaction->IsValid();
    }

    rhi::TextureRef RenderNodeImplContext::GetFrameOutputTexture() const noexcept
    {
        return HasFrameOutput() ? m_outputTransaction->GetTexture() : rhi::TextureRef{};
    }

    void RenderNodeImplContext::RTImportFrameOutput() const noexcept
    {
        if (!HasFrameOutput())
            return;
        RenderFlowNameTag tag = RTSharedNameTag(VG_RENDER_FLOW_NAME("FrameOutput"));
        tag.kind = FrameResourceKind::Texture;
        const ImportedResourceId imported{m_outputTransaction->m_resourceImportIndex, m_outputTransaction->m_resourceImportGeneration};
        if (CanRecordResourceOperation() && !GetResourceWriter().ImportTexture(tag.key, imported, tag.resource, m_resourceFailure))
            tag.resource = {};
    }

    void RenderNodeImplContext::RTExportFrameOutput() const noexcept
    {
        if (!HasFrameOutput() || m_outputTransaction->GetKind() != RenderViewportOutputKind::Texture)
            return;
        const ExportSlotId slot{m_outputTransaction->m_resourceExportIndex, m_outputTransaction->m_resourceExportGeneration};
        if (!slot.IsValid())
        {
            FailResourceOperation(RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
                                  "texture frame output has no reserved terminal export slot");
            return;
        }
        RenderFlowNameTag tag = ResolveNamedTag(RTSharedNameTag(VG_RENDER_FLOW_NAME("FrameOutput")));
        if (!tag.resource.IsValid())
            return;
        const RenderTextureOutputContract& contract = m_outputTransaction->m_viewport->GetTextureOutputContract();
        // EndRender is the frame-output terminal owner. Model the final producer-side
        // endpoint as a preserving render-target write, then let RequestExport author
        // the transition from that executable endpoint into the declared resting state.
        // This keeps Common and other compatible resting states truthful instead of
        // pretending that the resting state itself is necessarily readable.
        TextureUseDesc terminalUse;
        terminalUse.requiredState = rhi::ResourceState::RenderTarget;
        terminalUse.access = LogicalAccessIntent::Write;
        terminalUse.content = ResourceContentIntent::Preserve;
        RTUseBegin(tag, terminalUse);
        RTUseEnd(tag);
        TerminalResourceExportDesc desc;
        desc.terminalState = contract.restingState;
        desc.terminalQueue = contract.restingQueue;
        desc.readiness = contract.publicationReadiness == RenderOutputPublicationReadinessKind::SameQueueContinuation
                             ? ExportReadinessKind::SameQueueContinuation
                             : ExportReadinessKind::ExplicitFenceSignal;
        if (CanRecordResourceOperation() && !GetResourceWriter().RequestExport(tag.resource, slot, desc, m_resourceFailure))
            return;
    }

    void RenderNodeImplContext::RTImportPresentationOutput() const noexcept
    {
        if (!HasPresentationOutput())
            return;
        RenderFlowNameTag tag = RTSharedNameTag(VG_RENDER_FLOW_NAME("FrameOutput"));
        tag.kind = FrameResourceKind::Texture;
        const ImportedResourceId imported{m_outputTransaction->m_resourceImportIndex, m_outputTransaction->m_resourceImportGeneration};
        if (CanRecordResourceOperation() && !GetResourceWriter().ImportTexture(tag.key, imported, tag.resource, m_resourceFailure))
            tag.resource = {};
    }

    void RenderNodeImplContext::RTUsePresentationOutput() const noexcept
    {
        if (!HasPresentationOutput())
            return;
        TextureUseDesc use;
        use.requiredState = rhi::ResourceState::RenderTarget;
        use.access = LogicalAccessIntent::Write;
        use.content = ResourceContentIntent::Preserve;
        const RenderFlowNameTag tag = RTSharedNameTag(VG_RENDER_FLOW_NAME("FrameOutput"));
        RTUseBegin(tag, use);
        RTUseEnd(tag);
    }

    const ResolvedTextureUse& RenderNodeImplContext::RTPresentationOutput() const noexcept
    {
        static const ResolvedTextureUse invalid;
        return HasPresentationOutput() ? RTTexture(RTSharedNameTag(VG_RENDER_FLOW_NAME("FrameOutput"))) : invalid;
    }

    void RenderNodeImplContext::RTUseBegin(const RenderFlowNameTag& input, const TextureUseDesc& desc) const noexcept
    {
        const RenderFlowNameTag tag = ResolveNamedTag(input);
        if (!CanRecordResourceOperation())
            return;
        ResourceUseId use;
        if (!GetResourceWriter().OpenResourceScope(tag.resource, m_resourceFailure) || !GetResourceWriter().BeginTextureUse(tag.resource, desc, use, m_resourceFailure))
            return;
        RenderNodeResourceUseBinding& binding = m_nodeResources->m_uses.EmplaceBack();
        binding.flowSpace = tag.key.GetFlowSpace();
        binding.name.Set(tag.key.GetName());
        binding.lookupHash = ResourceLookupHash(tag.key.GetFlowSpace(), tag.key.GetName(), FrameResourceKind::Texture);
        binding.use = use;
        binding.kind = FrameResourceKind::Texture;
        binding.resolvedIndex = m_nodeResources->m_resolvedTextures.Size();
        m_nodeResources->m_resolvedTextures.EmplaceBack();
    }

    void RenderNodeImplContext::RTUseBegin(const RenderFlowNameTag& input, const BufferUseDesc& desc) const noexcept
    {
        const RenderFlowNameTag tag = ResolveNamedTag(input);
        if (!CanRecordResourceOperation())
            return;
        ResourceUseId use;
        if (!GetResourceWriter().OpenResourceScope(tag.resource, m_resourceFailure) || !GetResourceWriter().BeginBufferUse(tag.resource, desc, use, m_resourceFailure))
            return;
        RenderNodeResourceUseBinding& binding = m_nodeResources->m_uses.EmplaceBack();
        binding.flowSpace = tag.key.GetFlowSpace();
        binding.name.Set(tag.key.GetName());
        binding.lookupHash = ResourceLookupHash(tag.key.GetFlowSpace(), tag.key.GetName(), FrameResourceKind::Buffer);
        binding.use = use;
        binding.kind = FrameResourceKind::Buffer;
        binding.resolvedIndex = m_nodeResources->m_resolvedBuffers.Size();
        m_nodeResources->m_resolvedBuffers.EmplaceBack();
    }

    void RenderNodeImplContext::RTUseEnd(const RenderFlowNameTag& tag) const noexcept
    {
        const RenderFlowNameTag resolved = ResolveNamedTag(tag);
        if (CanRecordResourceOperation())
            static_cast<void>(GetResourceWriter().CloseResourceScope(resolved.resource, m_resourceFailure));
    }

    void RenderNodeImplContext::RTSwap(const RenderFlowNameTag& leftInput, const RenderFlowNameTag& rightInput) const noexcept
    {
        const RenderFlowNameTag left = ResolveNamedTag(leftInput);
        const RenderFlowNameTag right = ResolveNamedTag(rightInput);
        if (CanRecordResourceOperation())
            static_cast<void>(GetResourceWriter().SwapLogicalMappings(left.resource, right.resource, m_resourceFailure));
    }

    bool RenderNodeImplContext::RTDecision(const bool value) const noexcept
    {
        if (IsDeclaringResources())
        {
            if (!CanRecordResourceOperation())
                return value;
            DecisionId decision;
            if (!GetResourceWriter().CaptureDecision(value, decision, m_resourceFailure))
                return value;
            m_nodeResources->m_decisions.PushBack({decision});
            return value;
        }
        if (IsPatchingResourceContract())
        {
            if (m_resourceFailure == nullptr || m_resourceAllocator == nullptr || m_nodeResources == nullptr ||
                m_resourceRange >= m_nodeResources->RangeCount())
            {
                FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning,
                                      "resource-contract decision patch has no active compiled range");
                return value;
            }
            const RenderNodeResourceRange range = m_nodeResources->GetRange(m_resourceRange);
            if (m_nextResourceDecision >= range.firstDecision + range.decisionCount ||
                m_nextResourceDecision >= m_nodeResources->DecisionCount())
            {
                FailResourceOperation(RenderFlowResourceFailureCode::IncompletePlanning,
                                      "resource-contract patch produced an undeclared dynamic decision");
                return value;
            }
            const DecisionId decision = m_nodeResources->GetDecision(m_nextResourceDecision++);
            if (!m_resourceAllocator->PatchResourceContractDecision(decision, value, m_resourceFailure))
                return value;
            return value;
        }
        if (!CanResolveResourceOperation())
            return value;
        const RenderNodeResourceRange range = m_nodeResources->GetRange(m_resourceRange);
        if (m_nextResourceDecision >= range.firstDecision + range.decisionCount)
        {
            FailResourceOperation(RenderFlowResourceFailureCode::IncompleteExecution, "render node requested an undeclared resource decision");
            return value;
        }
        bool captured = value;
        const DecisionId decision = m_nodeResources->GetDecision(m_nextResourceDecision++);
        if (!m_resourceCursor->CapturedDecision(decision, captured, m_resourceFailure))
            VG_FATAL(m_resourceFailure->message != nullptr ? m_resourceFailure->message : "render-node decision lookup failed");
        return captured;
    }

    const ResolvedTextureUse& RenderNodeImplContext::RTTexture(const RenderFlowNameTag& tag) const noexcept
    {
        static const ResolvedTextureUse invalid;
        if (!CanResolveResourceOperation())
            return invalid;
        const RenderNodeResourceRange range = m_nodeResources->GetRange(m_resourceRange);
        if (range.lookupBucketCount != 0 && range.lookupBucketOffset <= m_nodeResources->LookupBucketCount() &&
            range.lookupBucketCount <= m_nodeResources->LookupBucketCount() - range.lookupBucketOffset &&
            range.lookupGroupOffset <= m_nodeResources->LookupGroupCount() &&
            range.lookupGroupCount <= m_nodeResources->LookupGroupCount() - range.lookupGroupOffset)
        {
            const u32 lookupHash = ResourceLookupHash(tag.key.GetFlowSpace(), tag.key.GetName(), FrameResourceKind::Texture);
            const u32 bucket = range.lookupBucketOffset + (lookupHash & (range.lookupBucketCount - 1u));
            u32 groupIndex = m_nodeResources->GetLookupBucket(bucket);
            while (groupIndex != InvalidRenderFlowResourceIndex)
            {
                if (groupIndex < range.lookupGroupOffset || groupIndex >= range.lookupGroupOffset + range.lookupGroupCount)
                    break;
                const RenderNodeResourceLookupGroup group = m_nodeResources->GetLookupGroup(groupIndex);
                if (group.firstUse >= range.useCount || range.firstUse + group.firstUse >= m_nodeResources->UseCount())
                    break;
                const RenderNodeResourceUseView binding = m_nodeResources->GetUse(range.firstUse + group.firstUse);
                if (group.lookupHash == lookupHash && binding.kind == FrameResourceKind::Texture && binding.flowSpace == tag.key.GetFlowSpace() &&
                    binding.name == tag.key.GetName())
                {
                    const u32 localIndex = group.runtimeNextUse;
                    const u32 selected = localIndex != InvalidRenderFlowResourceIndex ? localIndex : group.firstUse;
                    if (selected >= range.useCount || range.firstUse + selected >= m_nodeResources->UseCount())
                        break;
                    const RenderNodeResourceUseView selectedBinding = m_nodeResources->GetUse(range.firstUse + selected);
                    if (selectedBinding.kind == FrameResourceKind::Texture && selectedBinding.resolvedIndex < m_nodeResources->m_resolvedTextures.Size())
                    {
                        ResolvedTextureUse& resolved = m_nodeResources->m_resolvedTextures[selectedBinding.resolvedIndex];
                        if (!resolved.IsValid() &&
                            (m_executionPacket == nullptr || !m_executionPacket->ResolveTextureUseBinding(selectedBinding.use, m_resourceRange + 1u, resolved, m_resourceFailure)))
                            VG_FATAL(m_resourceFailure->message != nullptr ? m_resourceFailure->message : "render-node texture binding resolution failed");
                        if (localIndex != InvalidRenderFlowResourceIndex)
                            m_nodeResources->SetLookupGroupRuntimeNextUse(groupIndex, selectedBinding.nextDuplicate);
                        return resolved;
                    }
                    break;
                }
                groupIndex = group.nextBucketGroup;
            }
        }
        FailResourceOperation(RenderFlowResourceFailureCode::IncompleteExecution, "render node requested a texture that was not declared for this occurrence");
        return invalid;
    }

    const ResolvedBufferUse& RenderNodeImplContext::RTBuffer(const RenderFlowNameTag& tag) const noexcept
    {
        static const ResolvedBufferUse invalid;
        if (!CanResolveResourceOperation())
            return invalid;
        const RenderNodeResourceRange range = m_nodeResources->GetRange(m_resourceRange);
        if (range.lookupBucketCount != 0 && range.lookupBucketOffset <= m_nodeResources->LookupBucketCount() &&
            range.lookupBucketCount <= m_nodeResources->LookupBucketCount() - range.lookupBucketOffset &&
            range.lookupGroupOffset <= m_nodeResources->LookupGroupCount() &&
            range.lookupGroupCount <= m_nodeResources->LookupGroupCount() - range.lookupGroupOffset)
        {
            const u32 lookupHash = ResourceLookupHash(tag.key.GetFlowSpace(), tag.key.GetName(), FrameResourceKind::Buffer);
            const u32 bucket = range.lookupBucketOffset + (lookupHash & (range.lookupBucketCount - 1u));
            u32 groupIndex = m_nodeResources->GetLookupBucket(bucket);
            while (groupIndex != InvalidRenderFlowResourceIndex)
            {
                if (groupIndex < range.lookupGroupOffset || groupIndex >= range.lookupGroupOffset + range.lookupGroupCount)
                    break;
                const RenderNodeResourceLookupGroup group = m_nodeResources->GetLookupGroup(groupIndex);
                if (group.firstUse >= range.useCount || range.firstUse + group.firstUse >= m_nodeResources->UseCount())
                    break;
                const RenderNodeResourceUseView binding = m_nodeResources->GetUse(range.firstUse + group.firstUse);
                if (group.lookupHash == lookupHash && binding.kind == FrameResourceKind::Buffer && binding.flowSpace == tag.key.GetFlowSpace() &&
                    binding.name == tag.key.GetName())
                {
                    const u32 localIndex = group.runtimeNextUse;
                    const u32 selected = localIndex != InvalidRenderFlowResourceIndex ? localIndex : group.firstUse;
                    if (selected >= range.useCount || range.firstUse + selected >= m_nodeResources->UseCount())
                        break;
                    const RenderNodeResourceUseView selectedBinding = m_nodeResources->GetUse(range.firstUse + selected);
                    if (selectedBinding.kind == FrameResourceKind::Buffer && selectedBinding.resolvedIndex < m_nodeResources->m_resolvedBuffers.Size())
                    {
                        ResolvedBufferUse& resolved = m_nodeResources->m_resolvedBuffers[selectedBinding.resolvedIndex];
                        if (!resolved.IsValid() &&
                            (m_executionPacket == nullptr || !m_executionPacket->ResolveBufferUseBinding(selectedBinding.use, m_resourceRange + 1u, resolved, m_resourceFailure)))
                            VG_FATAL(m_resourceFailure->message != nullptr ? m_resourceFailure->message : "render-node buffer binding resolution failed");
                        if (localIndex != InvalidRenderFlowResourceIndex)
                            m_nodeResources->SetLookupGroupRuntimeNextUse(groupIndex, selectedBinding.nextDuplicate);
                        return resolved;
                    }
                    break;
                }
                groupIndex = group.nextBucketGroup;
            }
        }
        FailResourceOperation(RenderFlowResourceFailureCode::IncompleteExecution, "render node requested a buffer that was not declared for this occurrence");
        return invalid;
    }

    const RenderFrameInfo& RenderNodeImplContext::GetFrameInfo() const noexcept
    {
        if (m_frame == nullptr)
            VG_FATAL("render-node context has no retained frame information");
        return *m_frame;
    }

    RenderViewport* RenderNodeImplContext::GetViewport() const noexcept
    {
        return GetFrameInfo().GetViewport();
    }

    void RenderNodeImplContext::SetCommandList(const rhi::CommandListRef commandList) const noexcept
    {
        if (m_frameCommandLists == nullptr || m_renderFlowGroup == ~RenderFlowGroup{0})
            VG_FATAL("render-node context has no frame command-list storage or flow group");
        m_frameCommandLists->SetCommandList(m_renderFlowGroup + static_cast<u32>(ReservedFrameCommandList::Count), commandList);
    }

    rhi::CommandListRef RenderNodeImplContext::GetCommandList() const noexcept
    {
        if (m_frameCommandLists == nullptr || m_renderFlowGroup == ~RenderFlowGroup{0})
            VG_FATAL("render-node context has no frame command-list storage or flow group");
        return m_frameCommandLists->GetCommandList(m_renderFlowGroup + static_cast<u32>(ReservedFrameCommandList::Count));
    }

    bool RenderNodeImplContext::HasCommandList() const noexcept
    {
        return GetCommandList().IsValid();
    }

    bool RenderNodeImplContext::SubmitCommandLists(const char* const scopeName, const rhi::CommandListSyncType sync, jobs::Builder& builder) const noexcept
    {
        if (m_frameCommandLists == nullptr || m_renderFlowGroup == ~RenderFlowGroup{0})
            VG_FATAL("render-node synchronization has no frame command-list storage or flow group");
        return m_frameCommandLists->Submit(scopeName, m_renderFlowGroup, sync, builder);
    }

    bool RenderNodeImplContext::BeginResourceQueue(const rhi::QueueType queue) const noexcept
    {
        if (m_resourceAllocator == nullptr || m_resourceFailure == nullptr)
            return false;
        return m_resourceAllocator->RequestBeginQueue(queue, GpuFlowGroupId{m_renderFlowGroup}, m_resourceFailure);
    }

    bool RenderNodeImplContext::EndResourceQueue() const noexcept
    {
        if (m_resourceAllocator == nullptr || m_resourceFailure == nullptr)
            return false;
        return m_resourceAllocator->RequestEndQueue(GpuFlowGroupId{m_renderFlowGroup}, m_resourceFailure);
    }

    bool RenderNodeImplContext::SyncResourceQueue(const rhi::CommandListSyncType sync) const noexcept
    {
        if (m_resourceAllocator == nullptr || m_resourceFailure == nullptr)
            return false;
        return m_resourceAllocator->RequestQueueSync(GpuFlowGroupId{m_renderFlowGroup}, sync, m_resourceFailure);
    }

    ResourcePlanningWriter& RenderNodeImplContext::GetResourceWriter() const noexcept
    {
        if (!IsDeclaringResources() || m_resourceWriter == nullptr)
            VG_FATAL("render-node resource writer is available only during resource declaration");
        return *m_resourceWriter;
    }

    RenderFlowResourceFailure& RenderNodeImplContext::GetResourceFailure() const noexcept
    {
        if ((!IsDeclaringResources() && !IsPatchingResourceContract()) || m_resourceFailure == nullptr)
            VG_FATAL("render-node resource failure is available only during resource declaration or contract patching");
        return *m_resourceFailure;
    }
} // namespace vanguard::rendering

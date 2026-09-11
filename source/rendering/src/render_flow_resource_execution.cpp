#include <vanguard/rendering/render_flow_resource_internal.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace vanguard::rendering
{
    ExecutionGenerationRef::Impl::~Impl()
    {
        if (!resourceDescriptors)
            return;
        for (const detail::PhysicalBindingRecord& binding : physicalBindings)
            for (const rhi::DescriptorHandle descriptor : {binding.shaderResource, binding.unorderedAccess})
                if (descriptor.IsValid() && !rhi::RetireDescriptor(resourceDescriptors.GetRef(), descriptor, descriptorRetirement) && !descriptorDeviceLost)
                    VG_FATAL("render-flow descriptor retirement failed");
    }

    namespace
    {
        [[nodiscard]] bool PacketFailure(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message, const detail::CompiledPacket* const packet = nullptr,
                                         const ResourceUseId use = {}) noexcept
        {
            return detail::Fail(failure, code, RenderFlowResourceSessionState::Executing, message, packet != nullptr ? packet->node : RenderFlowNodeId{}, {}, use);
        }

        [[nodiscard]] detail::CompiledPacket* GetPacket(ExecutionGenerationRef::Impl* const generation, const u32 packetIndex) noexcept
        {
            return generation != nullptr && packetIndex < generation->packets.Size() ? &generation->packets[packetIndex] : nullptr;
        }

        [[nodiscard]] const detail::CompiledPacket* GetPacket(const ExecutionGenerationRef::Impl* const generation, const u32 packetIndex) noexcept
        {
            return generation != nullptr && packetIndex < generation->packets.Size() ? &generation->packets[packetIndex] : nullptr;
        }

        [[nodiscard]] bool IsActiveUse(const detail::CompiledPacket& packet, const u32 runtimeUseSlot) noexcept
        {
            return packet.runtime.liveness != nullptr && runtimeUseSlot < packet.runtime.liveness->useActive.Size() && packet.runtime.liveness->useActive[runtimeUseSlot] != 0;
        }

        [[nodiscard]] constexpr bool ValidScopeCompletion(const CommandScopeCompletionKind completion) noexcept
        {
            return completion == CommandScopeCompletionKind::Submitted || completion == CommandScopeCompletionKind::DiscardedBeforeSubmission || completion == CommandScopeCompletionKind::UnknownDueToDeviceLoss;
        }

        [[nodiscard]] constexpr bool ValidTerminalCompletion(const TerminalExecutionCompletionKind completion) noexcept
        {
            return completion == TerminalExecutionCompletionKind::Completed || completion == TerminalExecutionCompletionKind::Aborted || completion == TerminalExecutionCompletionKind::DeviceLost;
        }

        [[nodiscard]] constexpr bool ValidQueueDependencyCompletion(const QueueDependencyCompletionKind completion) noexcept
        {
            return completion == QueueDependencyCompletionKind::Submitted || completion == QueueDependencyCompletionKind::DiscardedBeforeSubmission ||
                   completion == QueueDependencyCompletionKind::UnknownDueToDeviceLoss;
        }

        [[nodiscard]] u32 ScopeOrder(const ExecutionGenerationRef::Impl& generation, const CommandScopeId scope) noexcept
        {
            for (const CompiledCommandScope& candidate : generation.commandScopes)
                if (candidate.scope == scope)
                    return candidate.stableOrder;
            return InvalidRenderFlowResourceIndex;
        }

        [[nodiscard]] bool SubmittedQueueOrderProved(const ExecutionGenerationRef::Impl& generation, const TerminalExecutionReceipt& receipt, const detail::CompiledPacket& producer,
                                                     const detail::CompiledPacket& consumer) noexcept
        {
            const rhi::CommandListSyncType required = detail::RequiredQueueSync(producer.queue, consumer.queue);
            if (required == rhi::CommandListSyncType::None)
                return false;
            const u32 producerOrder = ScopeOrder(generation, producer.commandScope);
            const u32 consumerOrder = ScopeOrder(generation, consumer.commandScope);
            if (producerOrder == InvalidRenderFlowResourceIndex || consumerOrder == InvalidRenderFlowResourceIndex)
                return false;
            for (const CompiledQueueDependency& dependency : generation.queueDependencies)
            {
                if (dependency.sync != required)
                    continue;
                const u32 boundaryProducerOrder = ScopeOrder(generation, dependency.producerScope);
                const u32 boundaryConsumerOrder = ScopeOrder(generation, dependency.consumerScope);
                if (boundaryProducerOrder == InvalidRenderFlowResourceIndex || boundaryConsumerOrder == InvalidRenderFlowResourceIndex || producerOrder > boundaryProducerOrder ||
                    boundaryConsumerOrder > consumerOrder)
                    continue;
                for (const QueueDependencyExecutionReceipt& submitted : receipt.queueDependencies)
                    if (submitted.producerScope == dependency.producerScope && submitted.consumerScope == dependency.consumerScope && submitted.sync == dependency.sync &&
                        submitted.completion == QueueDependencyCompletionKind::Submitted)
                        return true;
            }
            return false;
        }

        void FailPacketExecution(detail::CompiledPacket& packet) noexcept
        {
            packet.runtime.state = detail::PacketRuntimeState::Failed;
            packet.runtime.activeUseCount = 0;
            if (packet.runtime.liveness != nullptr)
                for (u8& active : packet.runtime.liveness->useActive)
                    active = 0;
        }

        [[nodiscard]] bool RecorderMatches(const ExecutionGenerationRef::Impl& generation, const rhi::CommandListRef commandList) noexcept
        {
            return !generation.hasNativeResourceBindings || (commandList.IsValid() && rhi::GetBoundCommandList() == commandList);
        }

        [[nodiscard]] bool ExecuteActions(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet, const u32 offset, const u32 count, const ResourceUseId use,
                                          RenderFlowResourceFailure* const failure, const bool scopeExit = false) noexcept
        {
            const auto& actions = scopeExit ? packet.exitActions : packet.actions;
            if (offset > actions.Size() || count > actions.Size() - offset)
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled resource-action range is invalid", &packet, use);
            if (!generation.hasNativeResourceBindings)
                return true;
            for (u32 index = offset; index < offset + count; ++index)
            {
                const detail::CompiledResourceAction& action = actions[index];
                if (!action.physical.IsValid() || action.physical.generation != generation.id.generation || action.physical.index >= generation.physicalBindings.Size())
                    return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled resource action references a stale physical assignment", &packet, use);
                const detail::PhysicalBindingRecord& binding = generation.physicalBindings[action.physical.index];
                rhi::Failure rhiFailure;
                bool succeeded = false;
                if (action.kind == detail::CompiledResourceActionKind::TextureTransition && binding.texture.IsValid() && !binding.buffer.IsValid())
                    succeeded = rhi::TransitionTexture(binding.texture, action.before, action.after, action.textureSubresources, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::BufferTransition && binding.buffer.IsValid() && !binding.texture.IsValid())
                    succeeded = rhi::TransitionBuffer(binding.buffer, action.before, action.after, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureUavBarrier && binding.texture.IsValid() && !binding.buffer.IsValid())
                    succeeded = rhi::BarrierTextureUav(binding.texture, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::BufferUavBarrier && binding.buffer.IsValid() && !binding.texture.IsValid())
                    succeeded = rhi::BarrierBufferUav(binding.buffer, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureColorTargetClear && binding.texture.IsValid() && !binding.buffer.IsValid())
                    succeeded = rhi::ClearColorTarget(binding.texture, action.textureClearValue.color, action.textureSubresources, nullptr, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureDepthClear && binding.texture.IsValid() && !binding.buffer.IsValid())
                    succeeded = rhi::ClearDepthTarget(binding.texture, action.textureClearValue.depth, action.textureSubresources, nullptr, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureStencilClear && binding.texture.IsValid() && !binding.buffer.IsValid())
                    succeeded = rhi::ClearStencilTarget(binding.texture, action.textureClearValue.stencil, action.textureSubresources, nullptr, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureDepthStencilClear && binding.texture.IsValid() && !binding.buffer.IsValid())
                    succeeded = rhi::ClearDepthStencilTarget(binding.texture, action.textureClearValue.depth, action.textureClearValue.stencil, action.textureSubresources, nullptr, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureUavFloatClear && binding.texture.IsValid() && !binding.buffer.IsValid())
                    succeeded = rhi::ClearTextureUav(binding.texture, action.textureClearValue.color, action.textureSubresources, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::TextureUavUintClear && binding.texture.IsValid() && !binding.buffer.IsValid())
                    succeeded = rhi::ClearTextureUav(binding.texture, action.textureClearValue.uintValue, action.textureSubresources, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::BufferUavUintClear && binding.buffer.IsValid() && !binding.texture.IsValid())
                    succeeded = rhi::ClearBufferUav(binding.buffer, action.bufferClearValue.value, &rhiFailure);
                else if (action.kind == detail::CompiledResourceActionKind::SwapChainPresentTransition &&
                         binding.kind == detail::PhysicalBindingKind::RetainedImport && binding.texture.IsValid() && !binding.buffer.IsValid() &&
                         binding.retainedImport < generation.retainedImports.Size())
                {
                    const detail::RetainedImportRecord& imported = generation.retainedImports[binding.retainedImport];
                    if (!imported.presentationAcquisition.IsValid() || imported.presentationAcquisition.texture != binding.texture ||
                        imported.presentationAcquisition.swapChain != generation.presentationAcquisition.swapChain ||
                        imported.presentationAcquisition.serial != generation.presentationAcquisition.serial)
                        return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                             "compiled presentation action disagrees with its retained acquisition", &packet, use);
                    succeeded = rhi::TransitionSwapChainPresent(imported.presentationAcquisition, &rhiFailure);
                }
                else
                    return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation, "compiled resource action kind disagrees with its physical assignment", &packet, use);
                if (!succeeded)
                    return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                         "compiled resource action failed", &packet, use);
            }
            return true;
        }

        [[nodiscard]] bool ExecuteAliasActivation(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet, const detail::CompiledStep& step, const ResourceUseId use,
                                                  RenderFlowResourceFailure* const failure) noexcept
        {
            if (step.aliasPredecessorCount == 0)
                return true;
            if (step.aliasPredecessorOffset > packet.aliasPredecessors.Size() || step.aliasPredecessorCount > packet.aliasPredecessors.Size() - step.aliasPredecessorOffset || !step.physical.IsValid() ||
                step.physical.generation != generation.id.generation || step.physical.index >= generation.physicalBindings.Size())
                return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "compiled alias-activation range is invalid", &packet, use);
            const detail::PhysicalBindingRecord& destinationBinding = generation.physicalBindings[step.physical.index];
            if (destinationBinding.kind != detail::PhysicalBindingKind::PlacedPool || destinationBinding.texture.IsValid() == destinationBinding.buffer.IsValid())
                return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation, "compiled alias activation does not target a placed resource", &packet, use);
            for (u32 index = 0; index < step.aliasPredecessorCount; ++index)
                if (!packet.aliasPredecessors[step.aliasPredecessorOffset + index].IsValid())
                    return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled alias activation contains an invalid predecessor", &packet, use);
            const rhi::ResourceRef destination = destinationBinding.texture.IsValid() ? rhi::ResourceRef(destinationBinding.texture) : rhi::ResourceRef(destinationBinding.buffer);
            rhi::Failure rhiFailure;
            if (!rhi::ActivateAliasedResource(destination, containers::ArraySpan<const rhi::ResourceRef>(&packet.aliasPredecessors[step.aliasPredecessorOffset], step.aliasPredecessorCount), &rhiFailure))
                return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                     "placed alias activation failed", &packet, use);
            return true;
        }

        [[nodiscard]] bool FinalizeAliasPredecessor(ExecutionGenerationRef::Impl& generation, detail::CompiledPacket& packet, const detail::CompiledStep& step, const ResourceUseId use,
                                                    RenderFlowResourceFailure* const failure) noexcept
        {
            if (!step.finalizeForAlias)
                return true;
            if (!step.physical.IsValid() || step.physical.generation != generation.id.generation || step.physical.index >= generation.physicalBindings.Size())
                return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled alias predecessor has no physical assignment", &packet, use);
            const detail::PhysicalBindingRecord& binding = generation.physicalBindings[step.physical.index];
            if (binding.kind != detail::PhysicalBindingKind::PlacedPool || binding.texture.IsValid() == binding.buffer.IsValid())
                return PacketFailure(failure, RenderFlowResourceFailureCode::BackendContractViolation, "compiled alias predecessor is not a placed resource", &packet, use);
            rhi::Failure rhiFailure;
            const bool finalized = binding.texture.IsValid() ? rhi::MakeStateSafeToRetire(binding.texture, &rhiFailure) : rhi::MakeStateSafeToRetire(binding.buffer, &rhiFailure);
            if (!finalized)
                return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction),
                                     "placed alias predecessor finalization failed", &packet, use);
            return true;
        }

    } // namespace

    namespace detail
    {
        PacketUseLiveness* AllocatePacketUseLiveness() noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(PacketUseLiveness), alignof(PacketUseLiveness));
            return block ? new (block.address) PacketUseLiveness() : nullptr;
        }

        void RetainPacketUseLiveness(PacketUseLiveness* const liveness) noexcept
        {
            if (liveness != nullptr)
                static_cast<void>(liveness->references.Increment());
        }

        void ReleasePacketUseLiveness(PacketUseLiveness* const liveness) noexcept
        {
            if (liveness == nullptr || liveness->references.Decrement() != 0)
                return;
            liveness->~PacketUseLiveness();
            memory::MemoryBlock block{liveness, sizeof(PacketUseLiveness), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void RetainGeneration(ExecutionGenerationRef::Impl* const generation) noexcept
        {
            if (generation != nullptr)
                static_cast<void>(generation->references.Increment());
        }

        void ReleaseGeneration(ExecutionGenerationRef::Impl* const generation) noexcept
        {
            if (generation == nullptr || generation->references.Decrement() != 0)
                return;
            generation->~Impl();
            memory::MemoryBlock block{generation, sizeof(ExecutionGenerationRef::Impl), memory::PoolId::Rendering};
            memory::Free(block);
        }
    } // namespace detail

    bool PublishedResourceExport::IsValid() const noexcept
    {
        return m_slot.IsValid() && (m_texture.IsValid() != m_buffer.IsValid());
    }

    FrameResourceKind PublishedResourceExport::GetKind() const noexcept
    {
        return m_kind;
    }

    ExportSlotId PublishedResourceExport::GetSlot() const noexcept
    {
        return m_slot;
    }

    rhi::TextureRef PublishedResourceExport::GetTexture() const noexcept
    {
        return IsValid() && m_kind == FrameResourceKind::Texture ? m_texture.GetRef() : rhi::TextureRef{};
    }

    rhi::BufferRef PublishedResourceExport::GetBuffer() const noexcept
    {
        return IsValid() && m_kind == FrameResourceKind::Buffer ? m_buffer.GetRef() : rhi::BufferRef{};
    }

    const rhi::TextureDesc& PublishedResourceExport::GetTextureDesc() const noexcept
    {
        return m_textureDesc;
    }

    const rhi::BufferDesc& PublishedResourceExport::GetBufferDesc() const noexcept
    {
        return m_bufferDesc;
    }

    rhi::ResourceState PublishedResourceExport::GetTerminalState() const noexcept
    {
        return m_terminalState;
    }

    rhi::QueueType PublishedResourceExport::GetTerminalQueue() const noexcept
    {
        return m_terminalQueue;
    }

    ExportReadinessKind PublishedResourceExport::GetReadiness() const noexcept
    {
        return m_readiness;
    }

    rhi::GpuFence PublishedResourceExport::GetReadyFence() const noexcept
    {
        return m_readyFence;
    }

    void PublishedResourceExport::Reset() noexcept
    {
        m_slot = {};
        m_texture.Reset();
        m_buffer.Reset();
        m_textureDesc = {};
        m_bufferDesc = {};
        m_terminalState = rhi::ResourceState::Unknown;
        m_terminalQueue = rhi::QueueType::Graphics;
        m_readiness = ExportReadinessKind::SameQueueContinuation;
        m_readyFence = {};
    }

    bool TerminalJoinToken::FromReadyCounter(const jobs::Counter& counter, const ExecutionGenerationId generation, TerminalJoinToken& token) noexcept
    {
        token = TerminalJoinToken({}, false);
        if (!generation.IsValid() || !counter.IsValid() || !counter.IsReady())
            return false;
        token = TerminalJoinToken(generation, true);
        return true;
    }

    ExecutionGenerationRef::ExecutionGenerationRef(Impl* const impl) noexcept : m_impl(impl)
    {
        detail::RetainGeneration(m_impl);
    }

    ExecutionGenerationRef::~ExecutionGenerationRef()
    {
        Reset();
    }

    ExecutionGenerationRef::ExecutionGenerationRef(const ExecutionGenerationRef& other) noexcept : m_impl(other.m_impl)
    {
        detail::RetainGeneration(m_impl);
    }

    ExecutionGenerationRef& ExecutionGenerationRef::operator=(const ExecutionGenerationRef& other) noexcept
    {
        if (this == &other)
            return *this;
        Impl* const replacement = other.m_impl;
        detail::RetainGeneration(replacement);
        detail::ReleaseGeneration(m_impl);
        m_impl = replacement;
        return *this;
    }

    ExecutionGenerationRef::ExecutionGenerationRef(ExecutionGenerationRef&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    ExecutionGenerationRef& ExecutionGenerationRef::operator=(ExecutionGenerationRef&& other) noexcept
    {
        if (this == &other)
            return *this;
        detail::ReleaseGeneration(m_impl);
        m_impl = other.m_impl;
        other.m_impl = nullptr;
        return *this;
    }

    bool ExecutionGenerationRef::IsValid() const noexcept
    {
        return m_impl != nullptr && m_impl->id.IsValid();
    }

    ExecutionGenerationId ExecutionGenerationRef::GetId() const noexcept
    {
        return m_impl != nullptr ? m_impl->id : ExecutionGenerationId{};
    }

    void ExecutionGenerationRef::Reset() noexcept
    {
        Impl* const value = m_impl;
        m_impl = nullptr;
        detail::ReleaseGeneration(value);
    }

    bool CompiledExecutionPacketView::IsValid() const noexcept
    {
        return m_generation.IsValid() && m_packetIndex < m_generation.m_impl->packets.Size();
    }

    ExecutionGenerationId CompiledExecutionPacketView::GetGeneration() const noexcept
    {
        return m_generation.GetId();
    }

    RenderFlowNodeId CompiledExecutionPacketView::GetNode() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->node : RenderFlowNodeId{};
    }

    GpuFlowGroupId CompiledExecutionPacketView::GetFlowGroup() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->flowGroup : GpuFlowGroupId{};
    }

    CommandScopeId CompiledExecutionPacketView::GetCommandScope() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->commandScope : CommandScopeId{};
    }

    rhi::QueueType CompiledExecutionPacketView::GetQueue() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->queue : rhi::QueueType::Graphics;
    }

    u32 CompiledExecutionPacketView::GetStepCount() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->steps.Size() : 0;
    }

    u32 CompiledExecutionPacketView::GetDecisionCount() const noexcept
    {
        const detail::CompiledPacket* const packet = GetPacket(m_generation.m_impl, m_packetIndex);
        return packet != nullptr ? packet->decisions.Size() : 0;
    }

    bool CompiledExecutionPacketView::OpenCursor(const CommandScopeId activeScope, const rhi::QueueType activeQueue, ExecutionPacketCursor& cursor, RenderFlowResourceFailure* const failure) const noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid() || cursor.IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "execution packet or output cursor is invalid");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        // The scheduled occurrence owns this packet exclusively. Teardown follows
        // the complete recording join, so command-list seeding needs no lifecycle lock.
        if (packet.commandScope != activeScope || packet.queue != activeQueue)
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active recorder queue or command scope does not match the compiled packet", &packet);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Unclaimed)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution packet has already been claimed or generation is terminal", &packet);
        rhi::CommandListRef commandList;
        if (generation->hasNativeResourceBindings)
        {
            commandList = rhi::GetBoundCommandList();
            if (!commandList.IsValid() || rhi::GetQueueType(rhi::GetBoundCommandListType()) != activeQueue)
                return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "physical execution requires a bound command list on the packet's compiled queue", &packet);
            rhi::Failure rhiFailure;
            for (u32 producer = 0; producer < 3; ++producer)
            {
                if (packet.incomingWaits[producer] == 0)
                    continue;
                const bool waitRecorded = rhi::AddCommandListWait({static_cast<rhi::QueueType>(producer), packet.incomingWaits[producer]}, &rhiFailure);
                if (!waitRecorded)
                {
                    FailPacketExecution(packet);
                    return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction), "incoming queue wait recording failed; discard the command list", &packet);
                }
            }
            const bool statesSeeded = rhi::SeedCommandListStates(containers::ArraySpan<const rhi::CommandListEntryState>(packet.entryStates), &rhiFailure);
            if (!statesSeeded)
            {
                FailPacketExecution(packet);
                return PacketFailure(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ExecutionAction), "command-scope entry-state seeding failed; discard the command list", &packet);
            }
        }
        packet.runtime.state = detail::PacketRuntimeState::Executing;
        packet.runtime.nextStep = 0;
        packet.runtime.activeUseCount = 0;
        if (packet.runtime.liveness != nullptr)
        {
            for (u8& active : packet.runtime.liveness->useActive)
                active = 0;
        }
        cursor.m_generation = m_generation;
        cursor.m_packetIndex = m_packetIndex;
        cursor.m_commandList = commandList;
        cursor.m_open = true;
        return true;
    }

    ResolvedTextureUse::~ResolvedTextureUse()
    {
        detail::ReleasePacketUseLiveness(m_liveness);
    }

    ResolvedTextureUse::ResolvedTextureUse(ResolvedTextureUse&& other) noexcept
        : m_liveness(other.m_liveness), m_use(other.m_use), m_physical(other.m_physical), m_texture(other.m_texture), m_desc(other.m_desc), m_viewDesc(other.m_viewDesc),
          m_runtimeUseSlot(other.m_runtimeUseSlot), m_hasExplicitView(other.m_hasExplicitView), m_shaderResource(other.m_shaderResource), m_unorderedAccess(other.m_unorderedAccess)
    {
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
        other.m_texture = {};
        other.m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        other.m_hasExplicitView = false;
    }

    ResolvedTextureUse& ResolvedTextureUse::operator=(ResolvedTextureUse&& other) noexcept
    {
        if (this == &other)
            return *this;
        detail::ReleasePacketUseLiveness(m_liveness);
        m_liveness = other.m_liveness;
        m_use = other.m_use;
        m_physical = other.m_physical;
        m_texture = other.m_texture;
        m_desc = other.m_desc;
        m_viewDesc = other.m_viewDesc;
        m_runtimeUseSlot = other.m_runtimeUseSlot;
        m_hasExplicitView = other.m_hasExplicitView;
        m_shaderResource = other.m_shaderResource;
        m_unorderedAccess = other.m_unorderedAccess;
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
        other.m_texture = {};
        other.m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        other.m_hasExplicitView = false;
        return *this;
    }

    bool ResolvedTextureUse::IsValid() const noexcept
    {
        return m_liveness != nullptr && m_runtimeUseSlot < m_liveness->useActive.Size() && m_liveness->useActive[m_runtimeUseSlot] != 0 && m_physical.IsValid();
    }

    ResourceUseId ResolvedTextureUse::GetUse() const noexcept
    {
        return m_use;
    }
    PhysicalResourceId ResolvedTextureUse::GetPhysicalResource() const noexcept
    {
        return m_physical;
    }
    rhi::TextureRef ResolvedTextureUse::GetTexture() const noexcept
    {
        return IsValid() ? m_texture : rhi::TextureRef{};
    }
    const TextureUseDesc& ResolvedTextureUse::GetDesc() const noexcept
    {
        return m_desc;
    }

    rhi::DescriptorHandle ResolvedTextureUse::GetShaderResourceDescriptor() const noexcept
    {
        return IsValid() && !m_hasExplicitView ? m_shaderResource : rhi::DescriptorHandle{};
    }
    rhi::DescriptorHandle ResolvedTextureUse::GetUnorderedAccessDescriptor() const noexcept
    {
        return IsValid() && !m_hasExplicitView ? m_unorderedAccess : rhi::DescriptorHandle{};
    }

    bool ResolvedTextureUse::HasExplicitView() const noexcept
    {
        return m_hasExplicitView;
    }

    const rhi::TextureViewDesc& ResolvedTextureUse::GetViewDesc() const noexcept
    {
        return m_viewDesc;
    }

    ResolvedBufferUse::~ResolvedBufferUse()
    {
        detail::ReleasePacketUseLiveness(m_liveness);
    }

    ResolvedBufferUse::ResolvedBufferUse(ResolvedBufferUse&& other) noexcept
        : m_liveness(other.m_liveness), m_use(other.m_use), m_physical(other.m_physical), m_buffer(other.m_buffer), m_desc(other.m_desc), m_viewDesc(other.m_viewDesc), m_runtimeUseSlot(other.m_runtimeUseSlot),
          m_hasExplicitView(other.m_hasExplicitView), m_shaderResource(other.m_shaderResource), m_unorderedAccess(other.m_unorderedAccess)
    {
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
        other.m_buffer = {};
        other.m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        other.m_hasExplicitView = false;
    }

    ResolvedBufferUse& ResolvedBufferUse::operator=(ResolvedBufferUse&& other) noexcept
    {
        if (this == &other)
            return *this;
        detail::ReleasePacketUseLiveness(m_liveness);
        m_liveness = other.m_liveness;
        m_use = other.m_use;
        m_physical = other.m_physical;
        m_buffer = other.m_buffer;
        m_desc = other.m_desc;
        m_viewDesc = other.m_viewDesc;
        m_runtimeUseSlot = other.m_runtimeUseSlot;
        m_hasExplicitView = other.m_hasExplicitView;
        m_shaderResource = other.m_shaderResource;
        m_unorderedAccess = other.m_unorderedAccess;
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
        other.m_buffer = {};
        other.m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        other.m_hasExplicitView = false;
        return *this;
    }

    bool ResolvedBufferUse::IsValid() const noexcept
    {
        return m_liveness != nullptr && m_runtimeUseSlot < m_liveness->useActive.Size() && m_liveness->useActive[m_runtimeUseSlot] != 0 && m_physical.IsValid();
    }

    ResourceUseId ResolvedBufferUse::GetUse() const noexcept
    {
        return m_use;
    }
    PhysicalResourceId ResolvedBufferUse::GetPhysicalResource() const noexcept
    {
        return m_physical;
    }
    rhi::BufferRef ResolvedBufferUse::GetBuffer() const noexcept
    {
        return IsValid() ? m_buffer : rhi::BufferRef{};
    }
    const BufferUseDesc& ResolvedBufferUse::GetDesc() const noexcept
    {
        return m_desc;
    }

    rhi::DescriptorHandle ResolvedBufferUse::GetShaderResourceDescriptor() const noexcept
    {
        return IsValid() && !m_hasExplicitView ? m_shaderResource : rhi::DescriptorHandle{};
    }
    rhi::DescriptorHandle ResolvedBufferUse::GetUnorderedAccessDescriptor() const noexcept
    {
        return IsValid() && !m_hasExplicitView ? m_unorderedAccess : rhi::DescriptorHandle{};
    }

    bool ResolvedBufferUse::HasExplicitView() const noexcept
    {
        return m_hasExplicitView;
    }

    const rhi::BufferViewDesc& ResolvedBufferUse::GetViewDesc() const noexcept
    {
        return m_viewDesc;
    }

    ExecutionPacketCursor::~ExecutionPacketCursor()
    {
        if (!m_open || m_generation.m_impl == nullptr)
            return;
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket* const packet = GetPacket(generation, m_packetIndex);
        if (packet != nullptr)
        {
            if (packet->runtime.state == detail::PacketRuntimeState::Executing)
            {
                packet->runtime.state = detail::PacketRuntimeState::Failed;
                packet->runtime.activeUseCount = 0;
                if (packet->runtime.liveness != nullptr)
                {
                    for (u8& active : packet->runtime.liveness->useActive)
                        active = 0;
                }
            }
        }
        m_open = false;
        m_commandList = {};
    }

    ExecutionPacketCursor::ExecutionPacketCursor(ExecutionPacketCursor&& other) noexcept
        : m_generation(static_cast<ExecutionGenerationRef&&>(other.m_generation)), m_packetIndex(other.m_packetIndex), m_commandList(other.m_commandList), m_open(other.m_open)
    {
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
        other.m_commandList = {};
        other.m_open = false;
    }

    ExecutionPacketCursor& ExecutionPacketCursor::operator=(ExecutionPacketCursor&& other) noexcept
    {
        if (this == &other)
            return *this;
        if (m_open && m_generation.m_impl != nullptr)
        {
            ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
            detail::CompiledPacket* const packet = GetPacket(generation, m_packetIndex);
            if (packet != nullptr)
            {
                if (packet->runtime.state == detail::PacketRuntimeState::Executing)
                {
                    packet->runtime.state = detail::PacketRuntimeState::Failed;
                    packet->runtime.activeUseCount = 0;
                    if (packet->runtime.liveness != nullptr)
                    {
                        for (u8& active : packet->runtime.liveness->useActive)
                            active = 0;
                    }
                }
            }
            m_open = false;
        }
        m_generation = static_cast<ExecutionGenerationRef&&>(other.m_generation);
        m_packetIndex = other.m_packetIndex;
        m_commandList = other.m_commandList;
        m_open = other.m_open;
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
        other.m_commandList = {};
        other.m_open = false;
        return *this;
    }

    bool ExecutionPacketCursor::IsValid() const noexcept
    {
        return m_open && m_generation.IsValid() && m_packetIndex < m_generation.m_impl->packets.Size();
    }

    bool ExecutionPacketCursor::BeginTextureUse(const ResourceUseId use, ResolvedTextureUse& resolved, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open", nullptr, use);
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed during texture use", &packet, use);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture use cannot begin at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::TextureUseBegin || step.use != use || !step.physical.IsValid() || packet.runtime.liveness == nullptr ||
            step.runtimeUseSlot >= packet.runtime.liveness->useActive.Size() || IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture use does not match the next compiled packet step", &packet, use);
        if (!ExecuteAliasActivation(*generation, packet, step, use, failure) || !ExecuteActions(*generation, packet, step.beforeActionOffset, step.beforeActionCount, use, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 1;
        ++packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        ResolvedTextureUse value;
        detail::RetainPacketUseLiveness(packet.runtime.liveness);
        value.m_liveness = packet.runtime.liveness;
        value.m_runtimeUseSlot = step.runtimeUseSlot;
        value.m_use = use;
        value.m_physical = step.physical;
        if (generation->hasNativeResourceBindings && step.physical.index < generation->physicalBindings.Size())
        {
            value.m_texture = generation->physicalBindings[step.physical.index].texture;
            value.m_shaderResource = generation->physicalBindings[step.physical.index].shaderResource;
            value.m_unorderedAccess = generation->physicalBindings[step.physical.index].unorderedAccess;
        }
        value.m_desc = step.texture;
        value.m_viewDesc = step.textureView;
        value.m_hasExplicitView = step.hasExplicitView;
        resolved = static_cast<ResolvedTextureUse&&>(value);
        return true;
    }

    bool ExecutionPacketCursor::BeginBufferUse(const ResourceUseId use, ResolvedBufferUse& resolved, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open", nullptr, use);
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed during buffer use", &packet, use);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer use cannot begin at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::BufferUseBegin || step.use != use || !step.physical.IsValid() || packet.runtime.liveness == nullptr ||
            step.runtimeUseSlot >= packet.runtime.liveness->useActive.Size() || IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer use does not match the next compiled packet step", &packet, use);
        if (!ExecuteAliasActivation(*generation, packet, step, use, failure) || !ExecuteActions(*generation, packet, step.beforeActionOffset, step.beforeActionCount, use, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 1;
        ++packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        ResolvedBufferUse value;
        detail::RetainPacketUseLiveness(packet.runtime.liveness);
        value.m_liveness = packet.runtime.liveness;
        value.m_runtimeUseSlot = step.runtimeUseSlot;
        value.m_use = use;
        value.m_physical = step.physical;
        if (generation->hasNativeResourceBindings && step.physical.index < generation->physicalBindings.Size())
        {
            value.m_buffer = generation->physicalBindings[step.physical.index].buffer;
            value.m_shaderResource = generation->physicalBindings[step.physical.index].shaderResource;
            value.m_unorderedAccess = generation->physicalBindings[step.physical.index].unorderedAccess;
        }
        value.m_desc = step.buffer;
        value.m_viewDesc = step.bufferView;
        value.m_hasExplicitView = step.hasExplicitView;
        resolved = static_cast<ResolvedBufferUse&&>(value);
        return true;
    }

    bool ExecutionPacketCursor::EndUse(const ResourceUseId use, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open", nullptr, use);
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed before resource-use end", &packet, use);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "resource use cannot end at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::UseEnd || step.use != use || !IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "resource use end does not match the next compiled packet step", &packet, use);
        if (!ExecuteActions(*generation, packet, step.afterActionOffset, step.afterActionCount, use, failure) || !FinalizeAliasPredecessor(*generation, packet, step, use, failure))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 0;
        --packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        return true;
    }

    bool ExecutionPacketCursor::CapturedDecision(const DecisionId decision, bool& value, RenderFlowResourceFailure* const failure) const noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        const detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution packet is not active", &packet);
        for (const detail::CapturedDecisionRecord& captured : packet.decisions)
        {
            if (captured.id != decision)
                continue;
            value = captured.value;
            return true;
        }
        return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "decision does not belong to this execution packet", &packet);
    }

    bool ExecutionPacketCursor::FinalizePacket(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution cursor is not open");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (!RecorderMatches(*generation, m_commandList))
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active command recorder changed before packet finalization", &packet);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.activeUseCount != 0 || packet.runtime.nextStep != packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "execution packet still has an active or unconsumed step", &packet);
        if (!ExecuteActions(*generation, packet, 0, packet.exitActions.Size(), {}, failure, true))
        {
            FailPacketExecution(packet);
            m_open = false;
            m_commandList = {};
            return false;
        }
        packet.runtime.state = detail::PacketRuntimeState::Complete;
        m_open = false;
        m_commandList = {};
        return true;
    }

    void ExecutionPacketCursor::CancelRemaining() noexcept
    {
        if (!IsValid())
            return;
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        if (packet.runtime.state == detail::PacketRuntimeState::Executing)
        {
            packet.runtime.state = detail::PacketRuntimeState::Canceled;
            packet.runtime.activeUseCount = 0;
            if (packet.runtime.liveness != nullptr)
            {
                for (u8& active : packet.runtime.liveness->useActive)
                    active = 0;
            }
        }
        m_open = false;
        m_commandList = {};
    }

    bool RenderFlowResourceAllocator::TakeExport(const ExportSlotId slot, PublishedResourceExport& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (m_impl->state != RenderFlowResourceSessionState::Idle)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, m_impl->state, "terminal exports may be taken only between frame sessions");
        if (!slot.IsValid() || resource.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, m_impl->state, "terminal export slot or output owner is invalid");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->writerLock);
        for (u32 index = 0; index < m_impl->publishedExports.Size(); ++index)
        {
            detail::PublishedExportRecord& published = m_impl->publishedExports[index];
            if (published.slot != slot)
                continue;
            resource.m_slot = published.slot;
            resource.m_kind = published.kind;
            resource.m_texture = static_cast<rhi::Texture&&>(published.texture);
            resource.m_buffer = static_cast<rhi::Buffer&&>(published.buffer);
            resource.m_textureDesc = published.textureDesc;
            resource.m_bufferDesc = published.bufferDesc;
            resource.m_terminalState = published.terminalState;
            resource.m_terminalQueue = published.terminalQueue;
            resource.m_readiness = published.readiness;
            resource.m_readyFence = published.readyFence;
            static_cast<void>(m_impl->publishedExports.RemoveAt(index));
            return true;
        }
        return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, m_impl->state, "terminal export slot has not been published or was already taken");
    }

    bool RenderFlowResourceAllocator::BeginExecution(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->publishedGeneration == nullptr || owner->state != RenderFlowResourceSessionState::Ready)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator has no ready execution generation");
        owner->state = RenderFlowResourceSessionState::Executing;
        owner->stats.state = owner->state;
        return true;
    }

    bool RenderFlowResourceAllocator::PacketFor(const RenderFlowNodeId node, CompiledExecutionPacketView& packet, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        packet = {};
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized", node);
        if (owner->publishedGeneration == nullptr || owner->state != RenderFlowResourceSessionState::Executing)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator has no published execution generation", node);
        u32 packetIndex = InvalidRenderFlowResourceIndex;
        if (!node.IsValid() || !owner->publishedGeneration->packetByNode.Find(node.value, packetIndex) || packetIndex >= owner->publishedGeneration->packets.Size() ||
            owner->publishedGeneration->packets[packetIndex].node != node)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "render node has no compiled execution packet", node);
        packet.m_generation = ExecutionGenerationRef(owner->publishedGeneration);
        packet.m_packetIndex = packetIndex;
        return true;
    }

    containers::ArraySpan<const CompiledCommandScope> RenderFlowResourceAllocator::GetExecutionCommandScopes() const noexcept
    {
        return m_impl != nullptr && m_impl->publishedGeneration != nullptr ? containers::ArraySpan<const CompiledCommandScope>(m_impl->publishedGeneration->commandScopes) :
                                                                            containers::ArraySpan<const CompiledCommandScope>{};
    }

    containers::ArraySpan<const CompiledQueueDependency> RenderFlowResourceAllocator::GetExecutionQueueDependencies() const noexcept
    {
        return m_impl != nullptr && m_impl->publishedGeneration != nullptr ? containers::ArraySpan<const CompiledQueueDependency>(m_impl->publishedGeneration->queueDependencies) :
                                                                            containers::ArraySpan<const CompiledQueueDependency>{};
    }

    bool RenderFlowResourceAllocator::Finish(const TerminalExecutionReceipt& receipt, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->publishedGeneration == nullptr || (owner->state != RenderFlowResourceSessionState::Ready && owner->state != RenderFlowResourceSessionState::Executing))
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not ready for terminal completion");
        ExecutionGenerationRef::Impl* const generation = owner->publishedGeneration;
        if (!ValidTerminalCompletion(receipt.completion) || !receipt.join.IsReadyFor(generation->id) || receipt.generation != generation->id ||
            receipt.commandScopes.Size() != generation->commandScopes.Size() || receipt.queueDependencies.Size() != generation->queueDependencies.Size())
            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "terminal receipt does not prove the complete execution generation joined");
        if (receipt.completion == TerminalExecutionCompletionKind::Completed && owner->state != RenderFlowResourceSessionState::Executing)
            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful terminal completion requires an execution generation that was explicitly started");

        for (const CompiledCommandScope& expected : generation->commandScopes)
        {
            const CommandScopeExecutionReceipt* found = nullptr;
            for (const CommandScopeExecutionReceipt& candidate : receipt.commandScopes)
            {
                if (candidate.scope != expected.scope)
                    continue;
                if (found != nullptr)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "terminal receipt duplicates a command scope");
                found = &candidate;
            }
            if (found == nullptr || !detail::ValidQueue(found->queue) || found->queue != expected.queue || !ValidScopeCompletion(found->completion))
                return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "terminal receipt is missing a command scope or reports the wrong queue");
            if (generation->hasNativeResourceBindings && receipt.completion == TerminalExecutionCompletionKind::Completed && found->completion != CommandScopeCompletionKind::Submitted)
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful terminal completion requires every command scope to be submitted");
            if (found->completion == CommandScopeCompletionKind::Submitted && (!found->fence.IsValid() || found->fence.queue != found->queue))
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "submitted command scope requires a matching real queue fence");
            if (found->completion == CommandScopeCompletionKind::DiscardedBeforeSubmission && found->fence.IsValid())
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "discarded command scope cannot report a submission fence");
            if (found->completion == CommandScopeCompletionKind::UnknownDueToDeviceLoss && (receipt.completion != TerminalExecutionCompletionKind::DeviceLost || found->fence.IsValid()))
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "unknown command completion is legal only after device loss");
        }

        if (generation->presentationAcquisition.IsValid())
        {
            if (!generation->presentationCommandScope.IsValid())
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state,
                                    "presentation acquisition has no compiled terminal Graphics scope");
            const CommandScopeExecutionReceipt* presentationReceipt = nullptr;
            for (const CommandScopeExecutionReceipt& candidate : receipt.commandScopes)
                if (candidate.scope == generation->presentationCommandScope)
                    presentationReceipt = &candidate;
            if (receipt.completion == TerminalExecutionCompletionKind::Completed &&
                (presentationReceipt == nullptr || presentationReceipt->completion != CommandScopeCompletionKind::Submitted ||
                 presentationReceipt->queue != rhi::QueueType::Graphics || !presentationReceipt->fence.IsValid() ||
                 presentationReceipt->fence.queue != rhi::QueueType::Graphics))
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state,
                                    "successful presentation requires its terminal Graphics scope submission receipt");
        }

        for (const CompiledQueueDependency& expected : generation->queueDependencies)
        {
            const QueueDependencyExecutionReceipt* found = nullptr;
            for (const QueueDependencyExecutionReceipt& candidate : receipt.queueDependencies)
            {
                if (candidate.producerScope != expected.producerScope || candidate.consumerScope != expected.consumerScope)
                    continue;
                if (found != nullptr)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "terminal receipt duplicates a compiled queue dependency");
                found = &candidate;
            }
            if (found == nullptr || found->sync != expected.sync || !ValidQueueDependencyCompletion(found->completion))
                return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "terminal receipt is missing or disagrees with a compiled queue dependency");
            if (generation->hasNativeResourceBindings && receipt.completion == TerminalExecutionCompletionKind::Completed && found->completion != QueueDependencyCompletionKind::Submitted)
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful terminal completion requires every queue dependency to be submitted");
            if (found->completion == QueueDependencyCompletionKind::UnknownDueToDeviceLoss && receipt.completion != TerminalExecutionCompletionKind::DeviceLost)
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "unknown queue-dependency completion is legal only after device loss");
            const CommandScopeExecutionReceipt* loweringReceipt = nullptr;
            for (const CommandScopeExecutionReceipt& candidate : receipt.commandScopes)
            {
                if (candidate.scope == expected.producerScope)
                    loweringReceipt = &candidate;
            }
            const CommandScopeCompletionKind requiredLowering = found->completion == QueueDependencyCompletionKind::Submitted                   ? CommandScopeCompletionKind::Submitted
                                                                : found->completion == QueueDependencyCompletionKind::DiscardedBeforeSubmission ? CommandScopeCompletionKind::DiscardedBeforeSubmission
                                                                                                                                                : CommandScopeCompletionKind::UnknownDueToDeviceLoss;
            if (loweringReceipt == nullptr || loweringReceipt->completion != requiredLowering)
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "queue dependency completion disagrees with its lowering command scope");
        }

        {
            if (generation->terminal.GetValue())
                return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "execution generation is already terminal");

            // Recording and its continuations have joined before this coordinator access.
            // Validate first. An invalid terminal receipt must not partially cancel packets.
            for (detail::CompiledPacket& compiled : generation->packets)
            {
                if (receipt.completion == TerminalExecutionCompletionKind::Completed && compiled.runtime.state != detail::PacketRuntimeState::Complete)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful frame completion requires every packet to be exhausted", compiled.node);
                if (receipt.completion != TerminalExecutionCompletionKind::Completed && compiled.runtime.state == detail::PacketRuntimeState::Executing)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "terminal abort cannot join while a packet cursor is still executing", compiled.node);
            }

            if (receipt.completion != TerminalExecutionCompletionKind::DeviceLost)
                for (const detail::CompiledPacket& compiled : generation->packets)
                {
                    const CommandScopeExecutionReceipt* consumer = nullptr;
                    for (const CommandScopeExecutionReceipt& scope : receipt.commandScopes)
                        if (scope.scope == compiled.commandScope)
                            consumer = &scope;
                    if (consumer == nullptr || consumer->completion != CommandScopeCompletionKind::Submitted)
                        continue;
                    for (const u32 predecessorIndex : compiled.statePredecessorPackets)
                    {
                        if (predecessorIndex >= generation->packets.Size())
                            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "explicit state predecessor is stale", compiled.node);
                        const detail::CompiledPacket& predecessor = generation->packets[predecessorIndex];
                        const CommandScopeExecutionReceipt* producer = nullptr;
                        for (const CommandScopeExecutionReceipt& scope : receipt.commandScopes)
                            if (scope.scope == predecessor.commandScope)
                                producer = &scope;
                        const bool ordered = producer != nullptr && producer->completion == CommandScopeCompletionKind::Submitted &&
                                             (producer->queue == consumer->queue ? producer->fence.value <= consumer->fence.value
                                                                                : SubmittedQueueOrderProved(*generation, receipt, predecessor, compiled));
                        if (predecessor.runtime.state != detail::PacketRuntimeState::Complete || !ordered)
                            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "submitted explicit-state consumer requires its complete, ordered submitted producer", compiled.node);
                    }
                }

            if (receipt.completion == TerminalExecutionCompletionKind::Completed)
            {
                containers::DynamicArray<detail::PublishedExportRecord> publications{memory::pools::Rendering::GetInstance()};
                containers::DynamicArray<u32> poolTransfers{memory::pools::Rendering::GetInstance()};
                publications.Reserve(generation->pendingExports.Size());
                poolTransfers.Reserve(generation->pendingExports.Size());
                for (const detail::PendingExportRecord& pending : generation->pendingExports)
                {
                    if (!pending.slot.IsValid() || !pending.physical.IsValid() || pending.physical.generation != generation->id.generation || pending.physical.index >= generation->physicalBindings.Size() ||
                        !pending.terminalCommandScope.IsValid())
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "compiled terminal export contains an invalid physical or command-scope assignment");
                    const CommandScopeExecutionReceipt* terminalReceipt = nullptr;
                    for (const CommandScopeExecutionReceipt& candidate : receipt.commandScopes)
                        if (candidate.scope == pending.terminalCommandScope)
                            terminalReceipt = &candidate;
                    if (terminalReceipt == nullptr || terminalReceipt->completion != CommandScopeCompletionKind::Submitted || terminalReceipt->queue != pending.terminalQueue ||
                        !terminalReceipt->fence.IsValid())
                        return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful terminal export requires the submitted receipt of its final command scope");

                    const detail::PhysicalBindingRecord& binding = generation->physicalBindings[pending.physical.index];
                    if (binding.kind == detail::PhysicalBindingKind::PlacedPool)
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "terminal exports cannot transfer allocator-owned placed resources");
                    detail::PublishedExportRecord publication;
                    publication.slot = pending.slot;
                    publication.kind = pending.kind;
                    publication.textureDesc = pending.textureDesc;
                    publication.bufferDesc = pending.bufferDesc;
                    publication.terminalState = pending.terminalState;
                    publication.terminalQueue = pending.terminalQueue;
                    publication.readiness = pending.readiness;
                    publication.readyFence = terminalReceipt->fence;
                    if (pending.kind == FrameResourceKind::Texture && binding.texture.IsValid() && !binding.buffer.IsValid())
                        publication.texture = rhi::Texture(binding.texture);
                    else if (pending.kind == FrameResourceKind::Buffer && binding.buffer.IsValid() && !binding.texture.IsValid())
                        publication.buffer = rhi::Buffer(binding.buffer);
                    else
                        return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "terminal export kind disagrees with its physical resource");
                    if (binding.kind == detail::PhysicalBindingKind::DedicatedPool)
                        poolTransfers.PushBack(binding.poolEntry);
                    publications.PushBack(static_cast<detail::PublishedExportRecord&&>(publication));
                }
                if (!owner->dedicatedPool.TransferOwnership(containers::ArraySpan<const u32>(poolTransfers), failure))
                    return false;
                {
                    concurrency::ScopedLock<concurrency::SpinLock> publicationGuard(owner->writerLock);
                    for (detail::PublishedExportRecord& publication : publications)
                        owner->publishedExports.PushBack(static_cast<detail::PublishedExportRecord&&>(publication));
                }
            }
            else
            {
                for (detail::CompiledPacket& compiled : generation->packets)
                {
                    if (compiled.runtime.state == detail::PacketRuntimeState::Unclaimed)
                        compiled.runtime.state = detail::PacketRuntimeState::Canceled;
                }
            }
            generation->terminal.SetValue(true);
        }

        generation->descriptorDeviceLost = receipt.completion == TerminalExecutionCompletionKind::DeviceLost;
        for (const CommandScopeExecutionReceipt& commandScope : receipt.commandScopes)
            if (commandScope.completion == CommandScopeCompletionKind::Submitted)
                generation->descriptorRetirement.Include(commandScope.fence);
        if (generation->hasNativeResourceBindings)
        {
            if (receipt.completion == TerminalExecutionCompletionKind::DeviceLost)
            {
                owner->dedicatedPool.DeviceLost();
                owner->placedPool.DeviceLost();
                concurrency::ScopedLock<concurrency::SpinLock> publicationGuard(owner->writerLock);
                owner->publishedExports.Clear();
            }
            else
            {
                rhi::ResidencyFenceSet safeAfter;
                for (const CommandScopeExecutionReceipt& commandScope : receipt.commandScopes)
                    if (commandScope.completion == CommandScopeCompletionKind::Submitted)
                        safeAfter.Include(commandScope.fence);
                for (u32 bindingIndex = 0; bindingIndex < generation->physicalBindings.Size(); ++bindingIndex)
                {
                    const detail::PhysicalBindingRecord& binding = generation->physicalBindings[bindingIndex];
                    bool exported = false;
                    if (receipt.completion == TerminalExecutionCompletionKind::Completed)
                        for (const detail::PendingExportRecord& pending : generation->pendingExports)
                            exported = exported || pending.physical.index == bindingIndex;
                    if (binding.kind == detail::PhysicalBindingKind::DedicatedPool && !exported)
                        owner->dedicatedPool.Retire(binding.poolEntry, safeAfter, !binding.explicitState || receipt.completion == TerminalExecutionCompletionKind::Completed);
                }
                bool placedReusable = true;
                // A placed batch is one heap-ownership unit. Any explicit object with uncertain abort state makes the complete batch unsafe to cache.
                if (receipt.completion != TerminalExecutionCompletionKind::Completed)
                    for (const detail::PhysicalBindingRecord& binding : generation->physicalBindings)
                        if (binding.kind == detail::PhysicalBindingKind::PlacedPool && binding.explicitState)
                        {
                            placedReusable = false;
                            break;
                        }
                owner->placedPool.Retire(generation->placedBatch, safeAfter, placedReusable);
            }
        }

        owner->state = receipt.completion == TerminalExecutionCompletionKind::DeviceLost ? RenderFlowResourceSessionState::DeviceUnavailable : RenderFlowResourceSessionState::TerminalJoined;
        owner->stats.state = owner->state;
        if (receipt.completion == TerminalExecutionCompletionKind::Completed)
            ++owner->stats.completedFrames;
        else
            ++owner->stats.abortedFrames;

        owner->publishedGeneration = nullptr;
        owner->activePolicy = {};
        detail::ReleaseGeneration(generation);
        if (receipt.completion != TerminalExecutionCompletionKind::DeviceLost)
        {
            owner->state = RenderFlowResourceSessionState::Idle;
            owner->stats.state = owner->state;
        }
        return true;
    }
} // namespace vanguard::rendering

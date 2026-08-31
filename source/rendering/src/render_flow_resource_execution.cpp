#include <vanguard/rendering/render_flow_resource_internal.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        [[nodiscard]] bool PacketFailure(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message,
                                         const detail::CompiledPacket* const packet = nullptr, const ResourceUseId use = {}) noexcept
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

        [[nodiscard]] constexpr bool ValidQueue(const rhi::QueueType queue) noexcept
        {
            return queue == rhi::QueueType::Graphics || queue == rhi::QueueType::Compute || queue == rhi::QueueType::Copy;
        }

        [[nodiscard]] constexpr bool ValidScopeCompletion(const CommandScopeCompletionKind completion) noexcept
        {
            return completion == CommandScopeCompletionKind::Submitted || completion == CommandScopeCompletionKind::DiscardedBeforeSubmission ||
                   completion == CommandScopeCompletionKind::UnknownDueToDeviceLoss;
        }

        [[nodiscard]] constexpr bool ValidTerminalCompletion(const TerminalExecutionCompletionKind completion) noexcept
        {
            return completion == TerminalExecutionCompletionKind::Completed || completion == TerminalExecutionCompletionKind::Aborted ||
                   completion == TerminalExecutionCompletionKind::DeviceLost;
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

    bool CompiledExecutionPacketView::OpenCursor(const CommandScopeId activeScope, const rhi::QueueType activeQueue, ExecutionPacketCursor& cursor,
                                                 RenderFlowResourceFailure* const failure) const noexcept
    {
        detail::ClearFailure(failure);
        if (!IsValid() || cursor.IsValid())
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "execution packet or output cursor is invalid");
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        concurrency::ScopedLock<concurrency::SpinLock> terminalGuard(generation->terminalLock);
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        concurrency::ScopedLock<concurrency::SpinLock> packetGuard(packet.runtime.lock);
        if (packet.commandScope != activeScope || packet.queue != activeQueue)
            return PacketFailure(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "active recorder queue or command scope does not match the compiled packet", &packet);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Unclaimed)
            return PacketFailure(failure, RenderFlowResourceFailureCode::InvalidPhase, "execution packet has already been claimed or generation is terminal", &packet);
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
        cursor.m_open = true;
        return true;
    }

    ResolvedTextureUse::~ResolvedTextureUse()
    {
        detail::ReleasePacketUseLiveness(m_liveness);
    }

    ResolvedTextureUse::ResolvedTextureUse(ResolvedTextureUse&& other) noexcept
        : m_liveness(other.m_liveness), m_use(other.m_use), m_physical(other.m_physical), m_desc(other.m_desc), m_viewDesc(other.m_viewDesc), m_runtimeUseSlot(other.m_runtimeUseSlot),
          m_hasExplicitView(other.m_hasExplicitView)
    {
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
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
        m_desc = other.m_desc;
        m_viewDesc = other.m_viewDesc;
        m_runtimeUseSlot = other.m_runtimeUseSlot;
        m_hasExplicitView = other.m_hasExplicitView;
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
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
    const TextureUseDesc& ResolvedTextureUse::GetDesc() const noexcept
    {
        return m_desc;
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
        : m_liveness(other.m_liveness), m_use(other.m_use), m_physical(other.m_physical), m_desc(other.m_desc), m_viewDesc(other.m_viewDesc), m_runtimeUseSlot(other.m_runtimeUseSlot),
          m_hasExplicitView(other.m_hasExplicitView)
    {
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
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
        m_desc = other.m_desc;
        m_viewDesc = other.m_viewDesc;
        m_runtimeUseSlot = other.m_runtimeUseSlot;
        m_hasExplicitView = other.m_hasExplicitView;
        other.m_liveness = nullptr;
        other.m_use = {};
        other.m_physical = {};
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
    const BufferUseDesc& ResolvedBufferUse::GetDesc() const noexcept
    {
        return m_desc;
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
            concurrency::ScopedLock<concurrency::SpinLock> guard(packet->runtime.lock);
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

    ExecutionPacketCursor::ExecutionPacketCursor(ExecutionPacketCursor&& other) noexcept
        : m_generation(static_cast<ExecutionGenerationRef&&>(other.m_generation)), m_packetIndex(other.m_packetIndex), m_open(other.m_open)
    {
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
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
                concurrency::ScopedLock<concurrency::SpinLock> guard(packet->runtime.lock);
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
        m_open = other.m_open;
        other.m_packetIndex = InvalidRenderFlowResourceIndex;
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
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture use cannot begin at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::TextureUseBegin || step.use != use || !step.physical.IsValid() || packet.runtime.liveness == nullptr ||
            step.runtimeUseSlot >= packet.runtime.liveness->useActive.Size() || IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "texture use does not match the next compiled packet step", &packet, use);
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 1;
        ++packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        ResolvedTextureUse value;
        detail::RetainPacketUseLiveness(packet.runtime.liveness);
        value.m_liveness = packet.runtime.liveness;
        value.m_runtimeUseSlot = step.runtimeUseSlot;
        value.m_use = use;
        value.m_physical = step.physical;
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
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer use cannot begin at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::BufferUseBegin || step.use != use || !step.physical.IsValid() || packet.runtime.liveness == nullptr ||
            step.runtimeUseSlot >= packet.runtime.liveness->useActive.Size() || IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "buffer use does not match the next compiled packet step", &packet, use);
        packet.runtime.liveness->useActive[step.runtimeUseSlot] = 1;
        ++packet.runtime.activeUseCount;
        ++packet.runtime.nextStep;
        ResolvedBufferUse value;
        detail::RetainPacketUseLiveness(packet.runtime.liveness);
        value.m_liveness = packet.runtime.liveness;
        value.m_runtimeUseSlot = step.runtimeUseSlot;
        value.m_use = use;
        value.m_physical = step.physical;
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
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.nextStep >= packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "resource use cannot end at the current packet step", &packet, use);
        const detail::CompiledStep& step = packet.steps[packet.runtime.nextStep];
        if (step.kind != CompiledResourceStepKind::UseEnd || step.use != use || !IsActiveUse(packet, step.runtimeUseSlot))
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "resource use end does not match the next compiled packet step", &packet, use);
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
        concurrency::ScopedLock<concurrency::SpinLock> guard(packet.runtime.lock);
        if (generation->terminal.GetValue() || packet.runtime.state != detail::PacketRuntimeState::Executing || packet.runtime.activeUseCount != 0 ||
            packet.runtime.nextStep != packet.steps.Size())
            return PacketFailure(failure, RenderFlowResourceFailureCode::IncompleteExecution, "execution packet still has an active or unconsumed step", &packet);
        packet.runtime.state = detail::PacketRuntimeState::Complete;
        m_open = false;
        return true;
    }

    void ExecutionPacketCursor::CancelRemaining() noexcept
    {
        if (!IsValid())
            return;
        ExecutionGenerationRef::Impl* const generation = m_generation.m_impl;
        detail::CompiledPacket& packet = generation->packets[m_packetIndex];
        concurrency::ScopedLock<concurrency::SpinLock> guard(packet.runtime.lock);
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
    }

    bool FrameResourceSession::BeginExecution(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        auto* const owner = static_cast<RenderFlowResourceAllocator::Impl*>(m_owner);
        if (owner == nullptr || owner->sessionGeneration != m_generation || owner->publishedGeneration == nullptr || owner->state != RenderFlowResourceSessionState::Ready)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, GetState(), "frame resource session has no ready execution generation");
        owner->state = RenderFlowResourceSessionState::Executing;
        owner->stats.state = owner->state;
        return true;
    }

    bool FrameResourceSession::PacketFor(const RenderFlowNodeId node, CompiledExecutionPacketView& packet, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        packet = {};
        auto* const owner = static_cast<RenderFlowResourceAllocator::Impl*>(m_owner);
        if (owner == nullptr || owner->sessionGeneration != m_generation || owner->publishedGeneration == nullptr || owner->state != RenderFlowResourceSessionState::Executing)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, GetState(), "frame resource session has no published execution generation", node);
        u32 packetIndex = InvalidRenderFlowResourceIndex;
        if (!node.IsValid() || !owner->publishedGeneration->packetByNode.Find(node.value, packetIndex) || packetIndex >= owner->publishedGeneration->packets.Size() ||
            owner->publishedGeneration->packets[packetIndex].node != node)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "surviving node has no compiled execution packet", node);
        packet.m_generation = ExecutionGenerationRef(owner->publishedGeneration);
        packet.m_packetIndex = packetIndex;
        return true;
    }

    bool FrameResourceSession::Finish(const TerminalExecutionReceipt& receipt, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        auto* const owner = static_cast<RenderFlowResourceAllocator::Impl*>(m_owner);
        if (owner == nullptr || owner->sessionGeneration != m_generation || owner->publishedGeneration == nullptr ||
            (owner->state != RenderFlowResourceSessionState::Ready && owner->state != RenderFlowResourceSessionState::Executing))
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, GetState(), "frame resource session is not ready for terminal completion");
        ExecutionGenerationRef::Impl* const generation = owner->publishedGeneration;
        if (!ValidTerminalCompletion(receipt.completion) || !receipt.join.IsReadyFor(generation->id) || receipt.generation != generation->id ||
            receipt.commandScopes.Size() != generation->commandScopes.Size())
            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "terminal receipt does not prove the complete execution generation joined");
        if (receipt.completion == TerminalExecutionCompletionKind::Completed && owner->state != RenderFlowResourceSessionState::Executing)
            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state,
                                "successful terminal completion requires an execution generation that was explicitly started");

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
            if (found == nullptr || !ValidQueue(found->queue) || found->queue != expected.queue || !ValidScopeCompletion(found->completion))
                return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state,
                                    "terminal receipt is missing a command scope or reports the wrong queue");
            if (found->completion == CommandScopeCompletionKind::Submitted && (!found->fence.IsValid() || found->fence.queue != found->queue))
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "submitted command scope requires a matching real queue fence");
            if (found->completion == CommandScopeCompletionKind::DiscardedBeforeSubmission && found->fence.IsValid())
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "discarded command scope cannot report a submission fence");
            if (found->completion == CommandScopeCompletionKind::UnknownDueToDeviceLoss && (receipt.completion != TerminalExecutionCompletionKind::DeviceLost || found->fence.IsValid()))
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "unknown command completion is legal only after device loss");
        }

        {
            concurrency::ScopedLock<concurrency::SpinLock> terminalGuard(generation->terminalLock);
            if (generation->terminal.GetValue())
                return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "execution generation is already terminal");

            // Validate first. An invalid terminal receipt must not partially cancel packets.
            for (detail::CompiledPacket& compiled : generation->packets)
            {
                concurrency::ScopedLock<concurrency::SpinLock> packetGuard(compiled.runtime.lock);
                if (receipt.completion == TerminalExecutionCompletionKind::Completed && compiled.runtime.state != detail::PacketRuntimeState::Complete)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "successful frame completion requires every packet to be exhausted",
                                        compiled.node);
                if (receipt.completion != TerminalExecutionCompletionKind::Completed && compiled.runtime.state == detail::PacketRuntimeState::Executing)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::IncompleteExecution, owner->state, "terminal abort cannot join while a packet cursor is still executing",
                                        compiled.node);
            }

            if (receipt.completion != TerminalExecutionCompletionKind::Completed)
            {
                for (detail::CompiledPacket& compiled : generation->packets)
                {
                    concurrency::ScopedLock<concurrency::SpinLock> packetGuard(compiled.runtime.lock);
                    if (compiled.runtime.state == detail::PacketRuntimeState::Unclaimed)
                        compiled.runtime.state = detail::PacketRuntimeState::Canceled;
                }
            }
            generation->terminal.SetValue(true);
        }

        owner->state = receipt.completion == TerminalExecutionCompletionKind::DeviceLost ? RenderFlowResourceSessionState::DeviceUnavailable : RenderFlowResourceSessionState::TerminalJoined;
        owner->stats.state = owner->state;
        if (receipt.completion == TerminalExecutionCompletionKind::Completed)
            ++owner->stats.completedFrames;
        else
            ++owner->stats.abortedFrames;

        owner->publishedGeneration = nullptr;
        detail::ReleaseGeneration(generation);
        if (receipt.completion != TerminalExecutionCompletionKind::DeviceLost)
        {
            owner->state = RenderFlowResourceSessionState::Idle;
            owner->stats.state = owner->state;
        }
        m_owner = nullptr;
        m_generation = 0;
        return true;
    }
} // namespace vanguard::rendering

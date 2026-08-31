#pragma once

#include <vanguard/rendering/render_flow_resource_allocator.hpp>

namespace vanguard::rendering
{
    namespace detail
    {
        struct PacketUseLiveness;
    }

    enum class CompiledResourceStepKind : u8
    {
        TextureUseBegin,
        BufferUseBegin,
        UseEnd
    };

    enum class CommandScopeCompletionKind : u8
    {
        Submitted,
        DiscardedBeforeSubmission,
        UnknownDueToDeviceLoss
    };

    enum class TerminalExecutionCompletionKind : u8
    {
        Completed,
        Aborted,
        DeviceLost
    };

    struct CommandScopeExecutionReceipt
    {
        CommandScopeId scope;
        CommandScopeCompletionKind completion = CommandScopeCompletionKind::DiscardedBeforeSubmission;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        rhi::GpuFence fence;
    };

    // CPU completion evidence for the complete execution generation. Per-scope
    // receipts separately report whether GPU work was submitted and its fence.
    class TerminalJoinToken final
    {
    public:
        [[nodiscard]] static constexpr TerminalJoinToken CompletedSynchronously(const ExecutionGenerationId generation) noexcept
        {
            return TerminalJoinToken(generation, generation.IsValid());
        }

        [[nodiscard]] static bool FromReadyCounter(const jobs::Counter& counter, ExecutionGenerationId generation, TerminalJoinToken& token) noexcept;

        [[nodiscard]] constexpr bool IsReadyFor(const ExecutionGenerationId generation) const noexcept
        {
            return m_ready && m_generation == generation;
        }

    private:
        explicit constexpr TerminalJoinToken(const ExecutionGenerationId generation, const bool ready) noexcept : m_generation(generation), m_ready(ready) {}

        ExecutionGenerationId m_generation;
        bool m_ready = false;
    };

    struct TerminalExecutionReceipt
    {
        ExecutionGenerationId generation;
        TerminalExecutionCompletionKind completion = TerminalExecutionCompletionKind::Completed;
        containers::ArraySpan<const CommandScopeExecutionReceipt> commandScopes;
        TerminalJoinToken join = TerminalJoinToken::CompletedSynchronously({});
    };

    class ExecutionPacketCursor;

    class ExecutionGenerationRef final
    {
    public:
        struct Impl;

        ExecutionGenerationRef() noexcept = default;
        ~ExecutionGenerationRef();
        ExecutionGenerationRef(const ExecutionGenerationRef& other) noexcept;
        ExecutionGenerationRef& operator=(const ExecutionGenerationRef& other) noexcept;
        ExecutionGenerationRef(ExecutionGenerationRef&& other) noexcept;
        ExecutionGenerationRef& operator=(ExecutionGenerationRef&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] ExecutionGenerationId GetId() const noexcept;
        void Reset() noexcept;

    private:
        Impl* m_impl = nullptr;
        explicit ExecutionGenerationRef(Impl* impl) noexcept;
        friend class FrameResourceSession;
        friend class CompiledExecutionPacketView;
        friend class ExecutionPacketCursor;
        friend class ResolvedTextureUse;
        friend class ResolvedBufferUse;
    };

    class CompiledExecutionPacketView final
    {
    public:
        CompiledExecutionPacketView() noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] ExecutionGenerationId GetGeneration() const noexcept;
        [[nodiscard]] RenderFlowNodeId GetNode() const noexcept;
        [[nodiscard]] GpuFlowGroupId GetFlowGroup() const noexcept;
        [[nodiscard]] CommandScopeId GetCommandScope() const noexcept;
        [[nodiscard]] rhi::QueueType GetQueue() const noexcept;
        [[nodiscard]] u32 GetStepCount() const noexcept;
        [[nodiscard]] u32 GetDecisionCount() const noexcept;
        [[nodiscard]] bool OpenCursor(CommandScopeId activeScope, rhi::QueueType activeQueue, ExecutionPacketCursor& cursor, RenderFlowResourceFailure* failure = nullptr) const noexcept;

    private:
        ExecutionGenerationRef m_generation;
        u32 m_packetIndex = InvalidRenderFlowResourceIndex;
        friend class FrameResourceSession;
    };

    // A resolved use retains only a packet-local liveness witness so stale
    // IsValid checks remain memory-safe. It owns neither the execution
    // generation nor an independent physical allocation and is usable only
    // between its matching Begin/End on the packet execution thread.
    class ResolvedTextureUse final
    {
    public:
        ResolvedTextureUse() noexcept = default;
        ~ResolvedTextureUse();
        ResolvedTextureUse(ResolvedTextureUse&& other) noexcept;
        ResolvedTextureUse& operator=(ResolvedTextureUse&& other) noexcept;

        ResolvedTextureUse(const ResolvedTextureUse&) = delete;
        ResolvedTextureUse& operator=(const ResolvedTextureUse&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] ResourceUseId GetUse() const noexcept;
        [[nodiscard]] PhysicalResourceId GetPhysicalResource() const noexcept;
        [[nodiscard]] const TextureUseDesc& GetDesc() const noexcept;
        [[nodiscard]] bool HasExplicitView() const noexcept;
        [[nodiscard]] const rhi::TextureViewDesc& GetViewDesc() const noexcept;

    private:
        detail::PacketUseLiveness* m_liveness = nullptr;
        ResourceUseId m_use;
        PhysicalResourceId m_physical;
        TextureUseDesc m_desc;
        rhi::TextureViewDesc m_viewDesc;
        u32 m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        bool m_hasExplicitView = false;
        friend class ExecutionPacketCursor;
    };

    class ResolvedBufferUse final
    {
    public:
        ResolvedBufferUse() noexcept = default;
        ~ResolvedBufferUse();
        ResolvedBufferUse(ResolvedBufferUse&& other) noexcept;
        ResolvedBufferUse& operator=(ResolvedBufferUse&& other) noexcept;

        ResolvedBufferUse(const ResolvedBufferUse&) = delete;
        ResolvedBufferUse& operator=(const ResolvedBufferUse&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] ResourceUseId GetUse() const noexcept;
        [[nodiscard]] PhysicalResourceId GetPhysicalResource() const noexcept;
        [[nodiscard]] const BufferUseDesc& GetDesc() const noexcept;
        [[nodiscard]] bool HasExplicitView() const noexcept;
        [[nodiscard]] const rhi::BufferViewDesc& GetViewDesc() const noexcept;

    private:
        detail::PacketUseLiveness* m_liveness = nullptr;
        ResourceUseId m_use;
        PhysicalResourceId m_physical;
        BufferUseDesc m_desc;
        rhi::BufferViewDesc m_viewDesc;
        u32 m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        bool m_hasExplicitView = false;
        friend class ExecutionPacketCursor;
    };

    class ExecutionPacketCursor final
    {
    public:
        ExecutionPacketCursor() noexcept = default;
        ~ExecutionPacketCursor();
        ExecutionPacketCursor(ExecutionPacketCursor&& other) noexcept;
        ExecutionPacketCursor& operator=(ExecutionPacketCursor&& other) noexcept;

        ExecutionPacketCursor(const ExecutionPacketCursor&) = delete;
        ExecutionPacketCursor& operator=(const ExecutionPacketCursor&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool BeginTextureUse(ResourceUseId use, ResolvedTextureUse& resolved, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginBufferUse(ResourceUseId use, ResolvedBufferUse& resolved, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool EndUse(ResourceUseId use, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CapturedDecision(DecisionId decision, bool& value, RenderFlowResourceFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool FinalizePacket(RenderFlowResourceFailure* failure = nullptr) noexcept;
        void CancelRemaining() noexcept;

    private:
        ExecutionGenerationRef m_generation;
        u32 m_packetIndex = InvalidRenderFlowResourceIndex;
        bool m_open = false;
        friend class CompiledExecutionPacketView;
    };
} // namespace vanguard::rendering

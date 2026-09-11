#pragma once

#include <vanguard/rendering/render_flow_resource_allocator.hpp>
#include <vanguard/rhi/rhi.hpp>

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
        // CloseAndSubmit returned false after SubmissionReceipt::WasSubmitted.
        // The executor has lost the fence but must not classify issued work as discarded.
        UnknownDueToDeviceLoss
    };

    enum class TerminalExecutionCompletionKind : u8
    {
        Completed,
        Aborted,
        DeviceLost
    };

    enum class QueueDependencyCompletionKind : u8
    {
        Submitted,
        DiscardedBeforeSubmission,
        // The lowering submission issued native work, then lost completion evidence.
        UnknownDueToDeviceLoss
    };

    struct CommandScopeExecutionReceipt
    {
        CommandScopeId scope;
        CommandScopeCompletionKind completion = CommandScopeCompletionKind::DiscardedBeforeSubmission;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        rhi::GpuFence fence;
    };

    // Executor acknowledgement that the matching compiled queue dependency
    // was lowered through CloseAndSubmitCommandLists with this sync mode.
    struct QueueDependencyExecutionReceipt
    {
        CommandScopeId producerScope;
        CommandScopeId consumerScope;
        rhi::CommandListSyncType sync = rhi::CommandListSyncType::None;
        QueueDependencyCompletionKind completion = QueueDependencyCompletionKind::DiscardedBeforeSubmission;
    };

    // CPU completion evidence for the complete execution generation. Per-scope
    // receipts separately report whether GPU work was submitted and its fence.
    // Neither factory joins jobs. The caller supplies an already established
    // complete-generation join, including queued work and child continuations.
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
        containers::ArraySpan<const QueueDependencyExecutionReceipt> queueDependencies;
    };

    // Owning result published only by a successful terminal frame completion.
    // The ready fence is the actual submission receipt for the resource's last
    // command scope; same-queue consumers may instead continue in submission
    // order on terminalQueue.
    class PublishedResourceExport final
    {
    public:
        PublishedResourceExport() noexcept = default;
        ~PublishedResourceExport() = default;
        PublishedResourceExport(PublishedResourceExport&&) noexcept = default;
        PublishedResourceExport& operator=(PublishedResourceExport&&) noexcept = default;

        PublishedResourceExport(const PublishedResourceExport&) = delete;
        PublishedResourceExport& operator=(const PublishedResourceExport&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] FrameResourceKind GetKind() const noexcept;
        [[nodiscard]] ExportSlotId GetSlot() const noexcept;
        [[nodiscard]] rhi::TextureRef GetTexture() const noexcept;
        [[nodiscard]] rhi::BufferRef GetBuffer() const noexcept;
        [[nodiscard]] const rhi::TextureDesc& GetTextureDesc() const noexcept;
        [[nodiscard]] const rhi::BufferDesc& GetBufferDesc() const noexcept;
        [[nodiscard]] rhi::ResourceState GetTerminalState() const noexcept;
        [[nodiscard]] rhi::QueueType GetTerminalQueue() const noexcept;
        [[nodiscard]] ExportReadinessKind GetReadiness() const noexcept;
        [[nodiscard]] rhi::GpuFence GetReadyFence() const noexcept;
        void Reset() noexcept;

    private:
        ExportSlotId m_slot;
        FrameResourceKind m_kind = FrameResourceKind::Texture;
        rhi::Texture m_texture;
        rhi::Buffer m_buffer;
        rhi::TextureDesc m_textureDesc;
        rhi::BufferDesc m_bufferDesc;
        rhi::ResourceState m_terminalState = rhi::ResourceState::Unknown;
        rhi::QueueType m_terminalQueue = rhi::QueueType::Graphics;
        ExportReadinessKind m_readiness = ExportReadinessKind::SameQueueContinuation;
        rhi::GpuFence m_readyFence;
        friend class RenderFlowResourceAllocator;
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
        friend class RenderFlowResourceAllocator;
        friend class CompiledExecutionPacketView;
        friend class ExecutionPacketCursor;
        friend class ResolvedTextureUse;
        friend class ResolvedBufferUse;
    };

    // Views may be retained/copied, but each packet has exactly one recording
    // owner. Opening the same packet concurrently is a caller contract violation.
    // Owner handoff between continuation jobs must be ordered by job dependencies.
    // Finish and allocator teardown cannot overlap opening or cursor operations.
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
        friend class RenderFlowResourceAllocator;
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
        // Non-owning and valid only while this scoped use is valid.
        [[nodiscard]] rhi::TextureRef GetTexture() const noexcept;
        [[nodiscard]] const TextureUseDesc& GetDesc() const noexcept;
        // Whole-resource descriptors, valid only during this use. Explicit views require their own descriptor support.
        [[nodiscard]] rhi::DescriptorHandle GetShaderResourceDescriptor() const noexcept;
        [[nodiscard]] rhi::DescriptorHandle GetUnorderedAccessDescriptor() const noexcept;
        [[nodiscard]] bool HasExplicitView() const noexcept;
        [[nodiscard]] const rhi::TextureViewDesc& GetViewDesc() const noexcept;

    private:
        detail::PacketUseLiveness* m_liveness = nullptr;
        ResourceUseId m_use;
        PhysicalResourceId m_physical;
        rhi::TextureRef m_texture;
        TextureUseDesc m_desc;
        rhi::TextureViewDesc m_viewDesc;
        u32 m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        bool m_hasExplicitView = false;
        rhi::DescriptorHandle m_shaderResource;
        rhi::DescriptorHandle m_unorderedAccess;
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
        // Non-owning and valid only while this scoped use is valid.
        [[nodiscard]] rhi::BufferRef GetBuffer() const noexcept;
        [[nodiscard]] const BufferUseDesc& GetDesc() const noexcept;
        // Whole-resource descriptors, valid only during this use. Explicit views require their own descriptor support.
        [[nodiscard]] rhi::DescriptorHandle GetShaderResourceDescriptor() const noexcept;
        [[nodiscard]] rhi::DescriptorHandle GetUnorderedAccessDescriptor() const noexcept;
        [[nodiscard]] bool HasExplicitView() const noexcept;
        [[nodiscard]] const rhi::BufferViewDesc& GetViewDesc() const noexcept;

    private:
        detail::PacketUseLiveness* m_liveness = nullptr;
        ResourceUseId m_use;
        PhysicalResourceId m_physical;
        rhi::BufferRef m_buffer;
        BufferUseDesc m_desc;
        rhi::BufferViewDesc m_viewDesc;
        u32 m_runtimeUseSlot = InvalidRenderFlowResourceIndex;
        bool m_hasExplicitView = false;
        rhi::DescriptorHandle m_shaderResource;
        rhi::DescriptorHandle m_unorderedAccess;
        friend class ExecutionPacketCursor;
    };

    // Single-owner execution state. Moves transfer ownership only after the old
    // owner's access has ended; destruction and cancellation are owner operations,
    // not asynchronous requests to stop another recording job.
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
        rhi::CommandListRef m_commandList;
        bool m_open = false;
        friend class CompiledExecutionPacketView;
    };
} // namespace vanguard::rendering

#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rendering
{
    class FrameRenderer;

    namespace detail
    {
        struct DedicatedResourceProviderTestAccess;
    }

    inline constexpr u32 InvalidRenderFlowResourceIndex = 0xffffffffu;

    template <typename Tag> struct RenderFlowStableId
    {
        u32 value = InvalidRenderFlowResourceIndex;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != InvalidRenderFlowResourceIndex;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderFlowStableId&, const RenderFlowStableId&) noexcept = default;
    };

    template <typename Tag> struct RenderFlowGenerationId
    {
        u32 index = InvalidRenderFlowResourceIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidRenderFlowResourceIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderFlowGenerationId&, const RenderFlowGenerationId&) noexcept = default;
    };

    struct FlowSpaceTag;
    struct GpuFlowGroupTag;
    struct RenderFlowNodeTag;
    struct CommandScopeTag;
    struct LogicalResourceTag;
    struct LogicalTextureViewTag;
    struct LogicalBufferViewTag;
    struct ImportedResourceTag;
    struct ExportSlotTag;
    struct PhysicalResourceTag;
    struct ExecutionGenerationTag;

    using FlowSpaceId = RenderFlowStableId<FlowSpaceTag>;
    using GpuFlowGroupId = RenderFlowStableId<GpuFlowGroupTag>;
    using RenderFlowNodeId = RenderFlowStableId<RenderFlowNodeTag>;
    using CommandScopeId = RenderFlowStableId<CommandScopeTag>;

    template <typename Tag> struct RenderFlowLocalId
    {
        GpuFlowGroupId flowGroup;
        u32 index = InvalidRenderFlowResourceIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return flowGroup.IsValid() && index != InvalidRenderFlowResourceIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderFlowLocalId&, const RenderFlowLocalId&) noexcept = default;
    };

    using LogicalResourceId = RenderFlowLocalId<LogicalResourceTag>;
    using LogicalTextureViewId = RenderFlowLocalId<LogicalTextureViewTag>;
    using LogicalBufferViewId = RenderFlowLocalId<LogicalBufferViewTag>;
    using ImportedResourceId = RenderFlowGenerationId<ImportedResourceTag>;
    using ExportSlotId = RenderFlowGenerationId<ExportSlotTag>;
    using PhysicalResourceId = RenderFlowGenerationId<PhysicalResourceTag>;
    using ExecutionGenerationId = RenderFlowGenerationId<ExecutionGenerationTag>;

    struct ResourceUseId
    {
        GpuFlowGroupId flowGroup;
        u32 ordinal = InvalidRenderFlowResourceIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return flowGroup.IsValid() && ordinal != InvalidRenderFlowResourceIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const ResourceUseId&, const ResourceUseId&) noexcept = default;
    };

    struct DecisionId
    {
        GpuFlowGroupId flowGroup;
        u32 ordinal = InvalidRenderFlowResourceIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return flowGroup.IsValid() && ordinal != InvalidRenderFlowResourceIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const DecisionId&, const DecisionId&) noexcept = default;
    };

    struct ResourceScopeId
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const ResourceScopeId&, const ResourceScopeId&) noexcept = default;
    };

    struct ExternalResourceToken
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const ExternalResourceToken&, const ExternalResourceToken&) noexcept = default;
    };

    struct PlanPosition
    {
        GpuFlowGroupId flowGroup;
        u32 ordinal = InvalidRenderFlowResourceIndex;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return flowGroup.IsValid() && ordinal != InvalidRenderFlowResourceIndex;
        }

        [[nodiscard]] friend constexpr bool operator==(const PlanPosition&, const PlanPosition&) noexcept = default;
    };

    enum class FrameResourceKind : u8
    {
        Invalid,
        Texture,
        Buffer
    };

    enum class FrameResourceInitialization : u8
    {
        Undefined,
        Clear,
        ImportedContents
    };

    enum class LogicalAccessIntent : u8
    {
        Read,
        Write,
        ReadWrite
    };

    enum class ResourceContentIntent : u8
    {
        Discard,
        Preserve,
        Clear
    };

    enum class TextureClearValueKind : u8
    {
        None,
        ColorFloat,
        ColorUint,
        Depth,
        Stencil,
        DepthStencil
    };

    struct TextureClearValue
    {
        TextureClearValueKind kind = TextureClearValueKind::None;
        rhi::ColorValue color;
        u32 uintValue = 0;
        f32 depth = 1.0f;
        u8 stencil = 0;

        [[nodiscard]] static constexpr TextureClearValue Color(const rhi::ColorValue value) noexcept
        {
            TextureClearValue result;
            result.kind = TextureClearValueKind::ColorFloat;
            result.color = value;
            return result;
        }

        [[nodiscard]] static constexpr TextureClearValue Uint(const u32 value) noexcept
        {
            TextureClearValue result;
            result.kind = TextureClearValueKind::ColorUint;
            result.uintValue = value;
            return result;
        }

        [[nodiscard]] static constexpr TextureClearValue Depth(const f32 value) noexcept
        {
            TextureClearValue result;
            result.kind = TextureClearValueKind::Depth;
            result.depth = value;
            return result;
        }

        [[nodiscard]] static constexpr TextureClearValue Stencil(const u8 value) noexcept
        {
            TextureClearValue result;
            result.kind = TextureClearValueKind::Stencil;
            result.stencil = value;
            return result;
        }

        [[nodiscard]] static constexpr TextureClearValue DepthStencil(const f32 depthValue, const u8 stencilValue) noexcept
        {
            TextureClearValue result;
            result.kind = TextureClearValueKind::DepthStencil;
            result.depth = depthValue;
            result.stencil = stencilValue;
            return result;
        }
    };

    enum class BufferClearValueKind : u8
    {
        None,
        Uint
    };

    struct BufferClearValue
    {
        BufferClearValueKind kind = BufferClearValueKind::None;
        u32 value = 0;

        [[nodiscard]] static constexpr BufferClearValue Uint(const u32 clearValue) noexcept
        {
            BufferClearValue result;
            result.kind = BufferClearValueKind::Uint;
            result.value = clearValue;
            return result;
        }
    };

    enum class ImportReadinessKind : u8
    {
        SameQueueContinuation,
        // incomingWait is an already submitted fence from initialQueue. The
        // consumer may use another queue; initialState must be legal there.
        // Copy-to-consumer handoffs must publish Common state.
        ExplicitFenceWait
    };

    enum class ExportReadinessKind : u8
    {
        SameQueueContinuation,
        ExplicitFenceSignal
    };

    struct TerminalResourceExportDesc
    {
        rhi::ResourceState terminalState = rhi::ResourceState::Common;
        rhi::QueueType terminalQueue = rhi::QueueType::Graphics;
        ExportReadinessKind readiness = ExportReadinessKind::SameQueueContinuation;
    };

    struct RetainedTextureImportDesc
    {
        ExternalResourceToken token;
        rhi::TextureRef texture;
        rhi::TextureDesc expected;
        rhi::ResourceState initialState = rhi::ResourceState::Common;
        rhi::ResourceState terminalState = rhi::ResourceState::Common;
        rhi::QueueType initialQueue = rhi::QueueType::Graphics;
        rhi::QueueType terminalQueue = rhi::QueueType::Graphics;
        ImportReadinessKind readiness = ImportReadinessKind::SameQueueContinuation;
        rhi::GpuFence incomingWait;
    };

    struct RetainedBufferImportDesc
    {
        ExternalResourceToken token;
        rhi::BufferRef buffer;
        rhi::BufferDesc expected;
        rhi::ResourceState initialState = rhi::ResourceState::Common;
        rhi::ResourceState terminalState = rhi::ResourceState::Common;
        rhi::QueueType initialQueue = rhi::QueueType::Graphics;
        rhi::QueueType terminalQueue = rhi::QueueType::Graphics;
        ImportReadinessKind readiness = ImportReadinessKind::SameQueueContinuation;
        rhi::GpuFence incomingWait;
    };

    struct FrameTextureDesc
    {
        rhi::TextureDesc active;
        rhi::Extent3D maximumExtent;
        u16 maximumMipCount = 0;
        FrameResourceInitialization initialization = FrameResourceInitialization::Undefined;
        TextureClearValue clearValue;
    };

    struct FrameBufferDesc
    {
        rhi::BufferDesc active;
        u64 maximumSize = 0;
        FrameResourceInitialization initialization = FrameResourceInitialization::Undefined;
        BufferClearValue clearValue;
    };

    struct FrameResourceDesc
    {
        FrameResourceKind kind = FrameResourceKind::Invalid;
        FrameTextureDesc texture;
        FrameBufferDesc buffer;

        [[nodiscard]] static constexpr FrameResourceDesc Texture(const FrameTextureDesc& desc) noexcept
        {
            FrameResourceDesc result;
            result.kind = FrameResourceKind::Texture;
            result.texture = desc;
            return result;
        }

        [[nodiscard]] static constexpr FrameResourceDesc Buffer(const FrameBufferDesc& desc) noexcept
        {
            FrameResourceDesc result;
            result.kind = FrameResourceKind::Buffer;
            result.buffer = desc;
            return result;
        }
    };

    struct TextureUseDesc
    {
        rhi::ResourceState requiredState = rhi::ResourceState::ShaderResourceGraphics;
        rhi::SubresourceRange subresources;
        LogicalAccessIntent access = LogicalAccessIntent::Read;
        ResourceContentIntent content = ResourceContentIntent::Preserve;
        TextureClearValue clearValue;
    };

    struct BufferUseDesc
    {
        rhi::ResourceState requiredState = rhi::ResourceState::ShaderResourceGraphics;
        LogicalAccessIntent access = LogicalAccessIntent::Read;
        ResourceContentIntent content = ResourceContentIntent::Preserve;
        BufferClearValue clearValue;
    };

    struct LogicalResourceKey
    {
        FlowSpaceId flowSpace;
        containers::StringView name;
    };

    enum class RenderFlowResourceSessionState : u8
    {
        Idle,
        Planning,
        CandidatesSealed,
        Resolving,
        Ready,
        Executing,
        TerminalJoined,
        DeviceUnavailable
    };

    enum class RenderFlowResourceFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        InvalidConfiguration,
        InvalidPhase,
        InvalidOrStaleIdentity,
        DescriptorConflict,
        InvalidUseOrScope,
        QueueOrCommandScopeMismatch,
        UnsupportedCapability,
        ArithmeticOverflow,
        CapacityExceeded,
        IncompletePlanning,
        IncompleteExecution,
        BudgetExceeded,
        NativeOutOfMemory,
        BackendContractViolation,
        DeviceLostOrBackendFailure
    };

    struct RenderFlowResourceFailure
    {
        RenderFlowResourceFailureCode code = RenderFlowResourceFailureCode::None;
        RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Idle;
        RenderFlowNodeId node;
        PlanPosition position;
        ResourceUseId use;
        const char* message = nullptr;
    };

    struct RenderFlowResourceAllocatorConfig
    {
        u32 maximumPlanningWriters = 4096;
        u32 maximumOperations = 262'144;
        u32 maximumLogicalResources = 65'536;
        u32 maximumViews = 65'536;
        u32 maximumTrackedTextureSubresources = 1'048'576;
        u32 maximumExecutionPackets = 4096;
        u32 maximumCompiledResourceActions = 1'048'576;
        u32 maximumCommandScopeEntryStates = 1'048'576;
        u32 maximumRetainedImports = 4096;
        u32 maximumTerminalExports = 4096;
        u32 maximumOutstandingPublishedExports = 4096;
        // One hard ledger for every allocator-owned dedicated resource and
        // placed heap. Pending GPU retirement and native destruction remain charged.
        u64 hardNativeByteLimit = ~u64{0};
        // Allocator-wide projected targets shared by dedicated resources and
        // placed heaps. Pending native destruction remains physically charged.
        u64 textureSoftTargetBytes = ~u64{0};
        u64 bufferSoftTargetBytes = ~u64{0};
        // Explicit planning/execution validation seam for allocator tests that
        // run without an RHI device. Production must leave this disabled: a
        // resolved use then requires a real dedicated-resource assignment.
        bool allowLogicalOnlyValidation = false;
        // Optional shared bindless domain. Resolve creates whole-resource
        // descriptors; the execution generation retires them with its receipts.
        rhi::DescriptorDomainRef resourceDescriptors;
    };

    struct FrameResourcePolicy
    {
        bool enablePlacedResources = false;
        bool processEviction = true;
    };

    struct RenderFlowResourceAllocatorStats
    {
        u64 begunFrames = 0;
        u64 resolvedFrames = 0;
        u64 completedFrames = 0;
        u64 abortedFrames = 0;
        u64 rejectedOperations = 0;
        u64 compiledOperations = 0;
        u64 compiledResourceActions = 0;
        u32 planningWriters = 0;
        u32 logicalAllocations = 0;
        u32 compiledPackets = 0;
        u32 retainedImports = 0;
        u32 outstandingPublishedExports = 0;
        u64 chargedNativeBytes = 0;
        u64 chargedTextureBytes = 0;
        u64 chargedBufferBytes = 0;
        u64 dedicatedResourcePoolHits = 0;
        u64 dedicatedResourcePoolMisses = 0;
        u64 placedHeapPoolHits = 0;
        u64 placedHeapPoolMisses = 0;
        u64 placedObjectPoolHits = 0;
        u64 placedObjectPoolMisses = 0;
        u64 pendingRetirementResources = 0;
        u64 pendingNativeDestructionResources = 0;
        u64 pendingRetirementPlacedHeaps = 0;
        u64 pendingNativeDestructionPlacedHeaps = 0;
        RenderFlowResourceSessionState state = RenderFlowResourceSessionState::Idle;
    };

    class PlanningJoinToken final
    {
    public:
        [[nodiscard]] static constexpr PlanningJoinToken CompletedSynchronously() noexcept
        {
            return PlanningJoinToken(true);
        }

        [[nodiscard]] static bool FromReadyCounter(const jobs::Counter& counter, PlanningJoinToken& token) noexcept;

        [[nodiscard]] constexpr bool IsReady() const noexcept
        {
            return m_ready;
        }

    private:
        explicit constexpr PlanningJoinToken(const bool ready) noexcept : m_ready(ready) {}
        bool m_ready = false;
    };

    struct CompiledCommandScope
    {
        CommandScopeId scope;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        u32 stableOrder = 0;
    };

    // Executable graphics/async-compute dependency compiled from the allocator's
    // explicit queue request stream. The authored synchronization submission
    // follows the producer and orders the subsequent consumer queue work.
    struct CompiledQueueDependency
    {
        CommandScopeId producerScope;
        CommandScopeId consumerScope;
        rhi::CommandListSyncType sync = rhi::CommandListSyncType::None;
    };

    class ExecutionGenerationRef;
    class CompiledExecutionPacketView;
    class PublishedResourceExport;
    struct TerminalExecutionReceipt;

    class ResourcePlanningWriter final
    {
    public:
        struct Impl;

        ResourcePlanningWriter() noexcept = default;
        ~ResourcePlanningWriter();
        ResourcePlanningWriter(ResourcePlanningWriter&& other) noexcept;
        ResourcePlanningWriter& operator=(ResourcePlanningWriter&& other) noexcept;

        ResourcePlanningWriter(const ResourcePlanningWriter&) = delete;
        ResourcePlanningWriter& operator=(const ResourcePlanningWriter&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] RenderFlowNodeId GetNode() const noexcept;
        [[nodiscard]] GpuFlowGroupId GetFlowGroup() const noexcept;

        [[nodiscard]] bool ReferenceResource(LogicalResourceKey key, LogicalResourceId& resource, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DeclareTexture(LogicalResourceKey key, const FrameTextureDesc& desc, LogicalResourceId& resource, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DeclareBuffer(LogicalResourceKey key, const FrameBufferDesc& desc, LogicalResourceId& resource, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DeclareTemporaryTexture(containers::StringView displayName, const FrameTextureDesc& desc, LogicalResourceId& resource,
                                                   RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DeclareTemporaryBuffer(containers::StringView displayName, const FrameBufferDesc& desc, LogicalResourceId& resource,
                                                  RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DeclareLike(LogicalResourceKey key, LogicalResourceId source, LogicalResourceId& resource, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ImportTexture(LogicalResourceKey key, ImportedResourceId imported, LogicalResourceId& resource,
                                         RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ImportBuffer(LogicalResourceKey key, ImportedResourceId imported, LogicalResourceId& resource,
                                        RenderFlowResourceFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool CreateTextureView(LogicalResourceId resource, const rhi::TextureViewDesc& desc, LogicalTextureViewId& view, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateBufferView(LogicalResourceId resource, const rhi::BufferViewDesc& desc, LogicalBufferViewId& view, RenderFlowResourceFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool BeginTextureUse(LogicalResourceId resource, const TextureUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginTextureViewUse(LogicalTextureViewId view, const TextureUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginBufferUse(LogicalResourceId resource, const BufferUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginBufferViewUse(LogicalBufferViewId view, const BufferUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool EndUse(ResourceUseId use, RenderFlowResourceFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool OpenResourceScope(LogicalResourceId resource, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CloseResourceScope(LogicalResourceId resource, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool OpenResourceScope(LogicalResourceId resource, ResourceScopeId scope, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CloseResourceScope(ResourceScopeId scope, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DeclareTemporaryLike(containers::StringView displayName, LogicalResourceId source, LogicalResourceId& resource,
                                                RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SwapLogicalMappings(LogicalResourceId left, LogicalResourceId right, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CaptureDecision(bool value, DecisionId& decision, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestExport(LogicalResourceId resource, ExportSlotId slot, const TerminalResourceExportDesc& desc,
                                         RenderFlowResourceFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool Close(RenderFlowResourceFailure* failure = nullptr) noexcept;
        void Abandon() noexcept;

    private:
        Impl* m_impl = nullptr;
        friend class RenderFlowResourceAllocator;
    };

    // Coordinator lifetime operations are externally serialized. Recording jobs
    // and their continuations must be joined before Finish, destruction, or
    // abandonment; teardown never cancels concurrently running packet owners.
    // Parallel declaration writers retain their separate writer/join contract.
    class RenderFlowResourceAllocator final
    {
    public:
        struct Impl;

        RenderFlowResourceAllocator() noexcept = default;
        ~RenderFlowResourceAllocator();
        RenderFlowResourceAllocator(const RenderFlowResourceAllocator&) = delete;
        RenderFlowResourceAllocator& operator=(const RenderFlowResourceAllocator&) = delete;

        [[nodiscard]] bool Initialize(const RenderFlowResourceAllocatorConfig& config = {}, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] RenderFlowResourceSessionState GetState() const noexcept;
        [[nodiscard]] ExecutionGenerationId GetExecutionGeneration() const noexcept;
        // Non-blocking cache release. Only allocator-owned persistent pool
        // entries are cleared; published exports remain caller-visible owners.
        // Native bytes stay charged internally until destruction is observed.
        [[nodiscard]] bool ClearPersistentCaches(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginFrame(u64 frameSerial, const FrameResourcePolicy& policy, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RegisterPresentationImport(const rhi::AcquiredBackBuffer& acquisition, ImportedResourceId& imported,
                                                      RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RegisterImport(const RetainedTextureImportDesc& desc, ImportedResourceId& imported, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RegisterImport(const RetainedBufferImportDesc& desc, ImportedResourceId& imported, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReserveExportSlot(ExportSlotId& slot, RenderFlowResourceFailure* failure = nullptr) noexcept;
        // Each writer owns a stable publication slot. The renderer creates writers before dispatch;
        // declaration, Close and Abandon remain exclusive to that writer's job chain.
        [[nodiscard]] bool CreatePlanningWriter(RenderFlowNodeId node, GpuFlowGroupId flowGroup, CommandScopeId commandScope, ResourcePlanningWriter& writer,
                                                RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestBeginQueue(rhi::QueueType queue, GpuFlowGroupId flowGroup, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestEndQueue(GpuFlowGroupId flowGroup, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestQueueSync(GpuFlowGroupId flowGroup, rhi::CommandListSyncType sync, RenderFlowResourceFailure* failure = nullptr) noexcept;
        // Requires the actual declaration join before reading writer-owned completion slots.
        [[nodiscard]] bool SealPlanning(PlanningJoinToken join, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Resolve(jobs::Builder* creationJobs = nullptr, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginExecution(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool PacketFor(RenderFlowNodeId node, CompiledExecutionPacketView& packet, RenderFlowResourceFailure* failure = nullptr) noexcept;
        // Caller has joined all recording work, closed/canceled every cursor,
        // and guarantees no queued job can claim a packet. GPU completion is
        // separate and remains described by the submission/fence receipts.
        [[nodiscard]] bool Finish(const TerminalExecutionReceipt& receipt, RenderFlowResourceFailure* failure = nullptr) noexcept;
        // Idempotent only while planning or resolving. A published generation
        // must leave through Finish with terminal join evidence.
        void CancelBeforePublication() noexcept;
        [[nodiscard]] RenderFlowResourceAllocatorStats GetStats() const noexcept;
        [[nodiscard]] bool TakeExport(ExportSlotId slot, PublishedResourceExport& resource, RenderFlowResourceFailure* failure = nullptr) noexcept;

    private:
        [[nodiscard]] bool PreparePlanningGroups(u32 groupCount, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] containers::ArraySpan<const CompiledCommandScope> GetExecutionCommandScopes() const noexcept;
        [[nodiscard]] containers::ArraySpan<const CompiledQueueDependency> GetExecutionQueueDependencies() const noexcept;
        // Terminal fallback only, with the same CPU quiescence requirement as Finish.
        void AbandonPublishedExecution() noexcept;

        Impl* m_impl = nullptr;
        friend class FrameRenderer;
        friend class ResourcePlanningWriter;
        friend class RenderNodeGraph;
        friend struct detail::DedicatedResourceProviderTestAccess;
    };
} // namespace vanguard::rendering

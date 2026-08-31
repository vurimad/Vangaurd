#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rendering
{
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

    struct FrameTextureDesc
    {
        rhi::TextureDesc active;
        rhi::Extent3D maximumExtent;
        u16 maximumMipCount = 0;
        FrameResourceInitialization initialization = FrameResourceInitialization::Undefined;
    };

    struct FrameBufferDesc
    {
        rhi::BufferDesc active;
        u64 maximumSize = 0;
        FrameResourceInitialization initialization = FrameResourceInitialization::Undefined;
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
    };

    struct BufferUseDesc
    {
        rhi::ResourceState requiredState = rhi::ResourceState::ShaderResourceGraphics;
        LogicalAccessIntent access = LogicalAccessIntent::Read;
        ResourceContentIntent content = ResourceContentIntent::Preserve;
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
    };

    struct FrameResourcePolicy
    {
        bool enablePlacedResources = false;
    };

    struct RenderFlowResourceAllocatorStats
    {
        u64 begunFrames = 0;
        u64 resolvedFrames = 0;
        u64 completedFrames = 0;
        u64 abortedFrames = 0;
        u64 rejectedOperations = 0;
        u64 compiledOperations = 0;
        u32 planningWriters = 0;
        u32 logicalAllocations = 0;
        u32 compiledPackets = 0;
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

    struct SurvivingGraphOverlay
    {
        containers::ArraySpan<const RenderFlowNodeId> nodes;
        bool allNodesSurvive = false;

        [[nodiscard]] bool Contains(RenderFlowNodeId node) const noexcept;
    };

    struct CompiledCommandScope
    {
        CommandScopeId scope;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        u32 stableOrder = 0;
    };

    struct CompiledQueueSchedule
    {
        containers::ArraySpan<const CompiledCommandScope> scopes;
    };

    class ExecutionGenerationRef;
    class CompiledExecutionPacketView;
    struct TerminalExecutionReceipt;
    class FrameResourceSession;

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

        [[nodiscard]] bool CreateTextureView(LogicalResourceId resource, const rhi::TextureViewDesc& desc, LogicalTextureViewId& view, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CreateBufferView(LogicalResourceId resource, const rhi::BufferViewDesc& desc, LogicalBufferViewId& view, RenderFlowResourceFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool BeginTextureUse(LogicalResourceId resource, const TextureUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginTextureViewUse(LogicalTextureViewId view, const TextureUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginBufferUse(LogicalResourceId resource, const BufferUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginBufferViewUse(LogicalBufferViewId view, const BufferUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool EndUse(ResourceUseId use, RenderFlowResourceFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool OpenResourceScope(LogicalResourceId resource, ResourceScopeId scope, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CloseResourceScope(ResourceScopeId scope, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SwapLogicalMappings(LogicalResourceId left, LogicalResourceId right, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CaptureDecision(bool value, DecisionId& decision, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestExport(LogicalResourceId resource, ExportSlotId slot, RenderFlowResourceFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool Close(RenderFlowResourceFailure* failure = nullptr) noexcept;
        void Abandon() noexcept;

    private:
        Impl* m_impl = nullptr;
        friend class FrameResourceSession;
    };

    class FrameResourceSession final
    {
    public:
        FrameResourceSession() noexcept = default;
        ~FrameResourceSession();
        FrameResourceSession(FrameResourceSession&& other) noexcept;
        FrameResourceSession& operator=(FrameResourceSession&& other) noexcept;

        FrameResourceSession(const FrameResourceSession&) = delete;
        FrameResourceSession& operator=(const FrameResourceSession&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] RenderFlowResourceSessionState GetState() const noexcept;
        [[nodiscard]] bool CreatePlanningWriter(RenderFlowNodeId node, GpuFlowGroupId flowGroup, CommandScopeId commandScope, ResourcePlanningWriter& writer,
                                                RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SealCandidates(PlanningJoinToken join, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Resolve(const SurvivingGraphOverlay& surviving, const CompiledQueueSchedule& schedule, ExecutionGenerationRef& generation, jobs::Builder* creationJobs = nullptr,
                                   RenderFlowResourceFailure* failure = nullptr) noexcept;
        // Coordinator-only transition. Packet lookup and execution are illegal
        // until this succeeds exactly once for the published generation.
        [[nodiscard]] bool BeginExecution(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool PacketFor(RenderFlowNodeId node, CompiledExecutionPacketView& packet, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Finish(const TerminalExecutionReceipt& receipt, RenderFlowResourceFailure* failure = nullptr) noexcept;
        // Idempotent only while planning or resolving. A published generation
        // must leave through Finish with terminal join evidence.
        void CancelBeforePublication() noexcept;

    private:
        void* m_owner = nullptr;
        u32 m_generation = 0;
        friend class RenderFlowResourceAllocator;
    };

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
        [[nodiscard]] bool BeginFrame(u64 frameSerial, const FrameResourcePolicy& policy, FrameResourceSession& session, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] RenderFlowResourceAllocatorStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
        friend class FrameResourceSession;
    };
} // namespace vanguard::rendering

#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/rendering/render_flow_resource_execution.hpp>

#include <utility>

namespace vanguard::rendering::detail
{
    enum class CandidateIdentityKind : u8
    {
        Named,
        Temporary
    };

    enum class CandidateOperationKind : u8
    {
        DeclareTexture,
        DeclareBuffer,
        DeclareLike,
        CreateTextureView,
        CreateBufferView,
        TextureUseBegin,
        BufferUseBegin,
        UseEnd,
        ScopeOpen,
        ScopeClose,
        SwapMappings,
        Decision,
        Export
    };

    struct CandidateResource
    {
        CandidateResource() noexcept : name(memory::pools::Rendering::GetInstance()) {}

        CandidateIdentityKind identity = CandidateIdentityKind::Named;
        FlowSpaceId flowSpace;
        containers::String name;
        PlanPosition declarationPosition;
    };

    struct CandidateTextureView
    {
        LogicalResourceId resource;
        rhi::TextureViewDesc desc;
    };

    struct CandidateBufferView
    {
        LogicalResourceId resource;
        rhi::BufferViewDesc desc;
    };

    struct CandidateOperation
    {
        CandidateOperationKind kind = CandidateOperationKind::Decision;
        u32 ordinal = 0;
        LogicalResourceId resource;
        LogicalResourceId otherResource;
        LogicalTextureViewId textureView;
        LogicalBufferViewId bufferView;
        ResourceUseId use;
        DecisionId decision;
        ResourceScopeId scope;
        ExportSlotId exportSlot;
        FrameResourceDesc resourceDesc;
        TextureUseDesc textureUse;
        BufferUseDesc bufferUse;
        bool decisionValue = false;
        bool useEnded = false;
    };

    struct CandidateWriterBatch
    {
        CandidateWriterBatch() noexcept
            : resources(memory::pools::Rendering::GetInstance()), textureViews(memory::pools::Rendering::GetInstance()), bufferViews(memory::pools::Rendering::GetInstance()),
              operations(memory::pools::Rendering::GetInstance())
        {
        }

        CandidateWriterBatch(CandidateWriterBatch&&) noexcept = default;
        CandidateWriterBatch& operator=(CandidateWriterBatch&&) noexcept = default;
        CandidateWriterBatch(const CandidateWriterBatch&) = delete;
        CandidateWriterBatch& operator=(const CandidateWriterBatch&) = delete;

        RenderFlowNodeId node;
        GpuFlowGroupId flowGroup;
        CommandScopeId commandScope;
        u32 sessionGeneration = 0;
        containers::DynamicArray<CandidateResource> resources;
        containers::DynamicArray<CandidateTextureView> textureViews;
        containers::DynamicArray<CandidateBufferView> bufferViews;
        containers::DynamicArray<CandidateOperation> operations;
    };

    struct WriterReservation
    {
        RenderFlowNodeId node;
        GpuFlowGroupId flowGroup;
        bool open = false;
        bool failed = false;
    };

    struct LogicalSlot
    {
        LogicalSlot() noexcept : name(memory::pools::Rendering::GetInstance()) {}

        CandidateIdentityKind identity = CandidateIdentityKind::Named;
        FlowSpaceId flowSpace;
        containers::String name;
        PlanPosition temporaryPosition;
        u32 currentAllocation = InvalidRenderFlowResourceIndex;
    };

    struct LogicalAllocationRecord
    {
        FrameResourceDesc desc;
        PlanPosition declarationPosition;
        PlanPosition firstUsePosition;
        PlanPosition lastUsePosition;
        CommandScopeId firstUseCommandScope;
        CommandScopeId lastUseCommandScope;
        PhysicalResourceId physical;
        u8 queueMask = 0;
        bool used = false;
        bool exported = false;
        bool imported = false;
    };

    struct ResolvedTextureViewRecord
    {
        u32 slot = InvalidRenderFlowResourceIndex;
        rhi::TextureViewDesc desc;
    };

    struct ResolvedBufferViewRecord
    {
        u32 slot = InvalidRenderFlowResourceIndex;
        rhi::BufferViewDesc desc;
    };

    struct CompiledStep
    {
        CompiledResourceStepKind kind = CompiledResourceStepKind::UseEnd;
        ResourceUseId use;
        PhysicalResourceId physical;
        TextureUseDesc texture;
        BufferUseDesc buffer;
        rhi::TextureViewDesc textureView;
        rhi::BufferViewDesc bufferView;
        u32 logicalAllocation = InvalidRenderFlowResourceIndex;
        u32 runtimeUseSlot = InvalidRenderFlowResourceIndex;
        bool hasExplicitView = false;
    };

    struct CapturedDecisionRecord
    {
        DecisionId id;
        bool value = false;
    };

    enum class PacketRuntimeState : u8
    {
        Unclaimed,
        Executing,
        Complete,
        Failed,
        Canceled
    };

    struct PacketUseLiveness
    {
        PacketUseLiveness() noexcept : useActive(memory::pools::Rendering::GetInstance()) {}

        concurrency::Atomic<u32> references{1};
        containers::DynamicArray<u8> useActive;
    };

    [[nodiscard]] PacketUseLiveness* AllocatePacketUseLiveness() noexcept;
    void RetainPacketUseLiveness(PacketUseLiveness* liveness) noexcept;
    void ReleasePacketUseLiveness(PacketUseLiveness* liveness) noexcept;

    struct PacketExecutionRuntime
    {
        PacketExecutionRuntime() noexcept = default;
        ~PacketExecutionRuntime()
        {
            if (liveness != nullptr)
            {
                for (u8& active : liveness->useActive)
                    active = 0;
            }
            ReleasePacketUseLiveness(liveness);
        }

        PacketExecutionRuntime(PacketExecutionRuntime&& other) noexcept : state(other.state), nextStep(other.nextStep), liveness(other.liveness), activeUseCount(other.activeUseCount)
        {
            other.liveness = nullptr;
        }

        PacketExecutionRuntime& operator=(PacketExecutionRuntime&& other) noexcept
        {
            if (this == &other)
                return *this;
            if (liveness != nullptr)
            {
                for (u8& active : liveness->useActive)
                    active = 0;
            }
            ReleasePacketUseLiveness(liveness);
            state = other.state;
            nextStep = other.nextStep;
            liveness = other.liveness;
            activeUseCount = other.activeUseCount;
            other.liveness = nullptr;
            return *this;
        }
        PacketExecutionRuntime(const PacketExecutionRuntime&) = delete;
        PacketExecutionRuntime& operator=(const PacketExecutionRuntime&) = delete;

        mutable concurrency::SpinLock lock;
        PacketRuntimeState state = PacketRuntimeState::Unclaimed;
        u32 nextStep = 0;
        PacketUseLiveness* liveness = nullptr;
        u32 activeUseCount = 0;
    };

    struct CompiledPacket
    {
        CompiledPacket() noexcept : steps(memory::pools::Rendering::GetInstance()), decisions(memory::pools::Rendering::GetInstance()) {}

        CompiledPacket(CompiledPacket&&) noexcept = default;
        CompiledPacket& operator=(CompiledPacket&&) noexcept = default;
        CompiledPacket(const CompiledPacket&) = delete;
        CompiledPacket& operator=(const CompiledPacket&) = delete;

        RenderFlowNodeId node;
        GpuFlowGroupId flowGroup;
        CommandScopeId commandScope;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        containers::DynamicArray<CompiledStep> steps;
        containers::DynamicArray<CapturedDecisionRecord> decisions;
        PacketExecutionRuntime runtime;
    };

    struct ActiveUseRecord
    {
        ResourceUseId id;
        u32 allocation = InvalidRenderFlowResourceIndex;
        u32 packet = InvalidRenderFlowResourceIndex;
        LogicalAccessIntent access = LogicalAccessIntent::Read;
        ResourceContentIntent content = ResourceContentIntent::Preserve;
        rhi::ResourceState requiredState = rhi::ResourceState::Unknown;
        rhi::SubresourceRange textureSubresources;
        u32 runtimeUseSlot = InvalidRenderFlowResourceIndex;
        u32 previousActive = InvalidRenderFlowResourceIndex;
        u32 nextActive = InvalidRenderFlowResourceIndex;
        bool texture = false;
        bool ended = false;
    };

    struct OpenScopeRecord
    {
        ResourceScopeId id;
        u32 allocation = InvalidRenderFlowResourceIndex;
        PlanPosition begin;
        bool closed = false;
    };

    struct PendingExportRecord
    {
        ExportSlotId slot;
        u32 logicalSlot = InvalidRenderFlowResourceIndex;
        u32 allocation = InvalidRenderFlowResourceIndex;
    };

    struct BatchResolveMap
    {
        BatchResolveMap() noexcept
            : resourceSlots(memory::pools::Rendering::GetInstance()), textureViews(memory::pools::Rendering::GetInstance()), bufferViews(memory::pools::Rendering::GetInstance()),
              useRecords(memory::pools::Rendering::GetInstance())
        {
        }

        containers::DynamicArray<u32> resourceSlots;
        containers::DynamicArray<ResolvedTextureViewRecord> textureViews;
        containers::DynamicArray<ResolvedBufferViewRecord> bufferViews;
        containers::DynamicArray<u32> useRecords;
    };

    [[nodiscard]] inline constexpr u32 NextGeneration(const u32 generation) noexcept
    {
        const u32 next = generation + 1u;
        return next != 0 ? next : 1u;
    }

    inline void ClearFailure(RenderFlowResourceFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
    }

    [[nodiscard]] inline bool Fail(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const RenderFlowResourceSessionState phase, const char* const message,
                                   const RenderFlowNodeId node = {}, const PlanPosition position = {}, const ResourceUseId use = {}) noexcept
    {
        if (failure != nullptr)
            *failure = {code, phase, node, position, use, message};
        return false;
    }

    [[nodiscard]] bool TextureDescEqual(const FrameTextureDesc& left, const FrameTextureDesc& right) noexcept;
    [[nodiscard]] bool BufferDescEqual(const FrameBufferDesc& left, const FrameBufferDesc& right) noexcept;
    [[nodiscard]] bool ResourceDescEqual(const FrameResourceDesc& left, const FrameResourceDesc& right) noexcept;
    [[nodiscard]] bool TextureViewDescEqual(const rhi::TextureViewDesc& left, const rhi::TextureViewDesc& right) noexcept;
    [[nodiscard]] bool BufferViewDescEqual(const rhi::BufferViewDesc& left, const rhi::BufferViewDesc& right) noexcept;
    [[nodiscard]] bool ValidTextureDesc(const FrameTextureDesc& desc) noexcept;
    [[nodiscard]] bool ValidBufferDesc(const FrameBufferDesc& desc) noexcept;

    void RetainGeneration(ExecutionGenerationRef::Impl* generation) noexcept;
    void ReleaseGeneration(ExecutionGenerationRef::Impl* generation) noexcept;
    [[nodiscard]] bool ResolveFrame(RenderFlowResourceAllocator::Impl& allocator, const SurvivingGraphOverlay& surviving, const CompiledQueueSchedule& schedule,
                                    ExecutionGenerationRef::Impl*& output, RenderFlowResourceFailure* failure) noexcept;
    void CancelSession(RenderFlowResourceAllocator::Impl& allocator, u32 generation) noexcept;
} // namespace vanguard::rendering::detail

namespace vanguard::rendering
{
    struct ExecutionGenerationRef::Impl
    {
        Impl() noexcept
            : packets(memory::pools::Rendering::GetInstance()), packetByNode(memory::pools::Rendering::GetInstance()), allocations(memory::pools::Rendering::GetInstance()),
              commandScopes(memory::pools::Rendering::GetInstance()), pendingExports(memory::pools::Rendering::GetInstance())
        {
        }

        concurrency::Atomic<u32> references{1};
        mutable concurrency::SpinLock terminalLock;
        concurrency::Atomic<bool> terminal{false};
        ExecutionGenerationId id;
        containers::DynamicArray<detail::CompiledPacket> packets;
        containers::HashMap<u32, u32> packetByNode;
        containers::DynamicArray<detail::LogicalAllocationRecord> allocations;
        containers::DynamicArray<CompiledCommandScope> commandScopes;
        containers::DynamicArray<detail::PendingExportRecord> pendingExports;
    };

    struct RenderFlowResourceAllocator::Impl
    {
        explicit Impl(const RenderFlowResourceAllocatorConfig& allocatorConfig) noexcept
            : writerReservations(memory::pools::Rendering::GetInstance()), writerBatches(memory::pools::Rendering::GetInstance()), config(allocatorConfig)
        {
        }

        mutable concurrency::SpinLock writerLock;
        containers::DynamicArray<detail::WriterReservation> writerReservations;
        containers::DynamicArray<detail::CandidateWriterBatch> writerBatches;
        RenderFlowResourceAllocatorConfig config;
        FrameResourcePolicy policy;
        RenderFlowResourceAllocatorStats stats;
        ExecutionGenerationRef::Impl* publishedGeneration = nullptr;
        u64 frameSerial = 0;
        u32 sessionGeneration = 0;
        u32 executionGeneration = 0;
        u32 openWriters = 0;
        u32 failedWriters = 0;
        u32 operationCount = 0;
        RenderFlowResourceSessionState state = RenderFlowResourceSessionState::Idle;
    };

    struct ResourcePlanningWriter::Impl
    {
        explicit Impl(RenderFlowResourceAllocator::Impl& allocator) noexcept : owner(&allocator) {}

        RenderFlowResourceAllocator::Impl* owner = nullptr;
        detail::CandidateWriterBatch batch;
        u64 rejectedOperations = 0;
        RenderFlowResourceFailureCode firstFailureCode = RenderFlowResourceFailureCode::None;
        const char* firstFailureMessage = nullptr;
        bool failed = false;
        bool closed = false;
    };
} // namespace vanguard::rendering

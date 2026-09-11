#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/rendering/render_flow_resource_execution.hpp>
#include <vanguard/rendering/render_flow_resource_placed.hpp>
#include <vanguard/rendering/render_flow_resource_pool.hpp>

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
        ImportTexture,
        ImportBuffer,
        CreateTextureView,
        CreateBufferView,
        TextureUseBegin,
        BufferUseBegin,
        UseEnd,
        ScopeOpen,
        ScopeClose,
        NamedScopeOpen,
        NamedScopeClose,
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
        ImportedResourceId importedResource;
        LogicalTextureViewId textureView;
        LogicalBufferViewId bufferView;
        ResourceUseId use;
        DecisionId decision;
        ResourceScopeId scope;
        ExportSlotId exportSlot;
        FrameResourceDesc resourceDesc;
        TextureUseDesc textureUse;
        BufferUseDesc bufferUse;
        TerminalResourceExportDesc exportDesc;
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
        CandidateWriterBatch batch;
    };

    enum class QueueRequestKind : u8
    {
        Begin,
        End,
        Sync
    };

    struct QueueRequestRecord
    {
        GpuFlowGroupId flowGroup;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        rhi::CommandListSyncType sync = rhi::CommandListSyncType::None;
        QueueRequestKind kind = QueueRequestKind::Begin;
    };

    struct QueueRequestGroup
    {
        QueueRequestRecord requests[2];
        u32 count = 0;
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
        u32 importedResource = InvalidRenderFlowResourceIndex;
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
        u32 beforeActionOffset = 0;
        u32 beforeActionCount = 0;
        u32 afterActionOffset = 0;
        u32 afterActionCount = 0;
        u32 aliasPredecessorOffset = 0;
        u32 aliasPredecessorCount = 0;
        bool hasExplicitView = false;
        bool finalizeForAlias = false;
    };

    enum class CompiledResourceActionKind : u8
    {
        TextureTransition,
        BufferTransition,
        TextureUavBarrier,
        BufferUavBarrier,
        TextureColorTargetClear,
        TextureDepthClear,
        TextureStencilClear,
        TextureDepthStencilClear,
        TextureUavFloatClear,
        TextureUavUintClear,
        BufferUavUintClear,
        SwapChainPresentTransition
    };

    struct CompiledResourceAction
    {
        CompiledResourceActionKind kind = CompiledResourceActionKind::BufferTransition;
        u32 logicalAllocation = InvalidRenderFlowResourceIndex;
        PhysicalResourceId physical;
        rhi::ResourceState before = rhi::ResourceState::Unknown;
        rhi::ResourceState after = rhi::ResourceState::Unknown;
        rhi::SubresourceRange textureSubresources;
        TextureClearValue textureClearValue;
        BufferClearValue bufferClearValue;
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

        // One recording owner; continuation handoffs and terminal access are job-ordered.
        PacketRuntimeState state = PacketRuntimeState::Unclaimed;
        u32 nextStep = 0;
        PacketUseLiveness* liveness = nullptr;
        u32 activeUseCount = 0;
    };

    struct CompiledPacket
    {
        CompiledPacket() noexcept
            : steps(memory::pools::Rendering::GetInstance()), actions(memory::pools::Rendering::GetInstance()), aliasPredecessors(memory::pools::Rendering::GetInstance()),
              decisions(memory::pools::Rendering::GetInstance()), entryStates(memory::pools::Rendering::GetInstance()), exitActions(memory::pools::Rendering::GetInstance()), statePredecessorPackets(memory::pools::Rendering::GetInstance())
        {
        }

        CompiledPacket(CompiledPacket&&) noexcept = default;
        CompiledPacket& operator=(CompiledPacket&&) noexcept = default;
        CompiledPacket(const CompiledPacket&) = delete;
        CompiledPacket& operator=(const CompiledPacket&) = delete;

        RenderFlowNodeId node;
        GpuFlowGroupId flowGroup;
        CommandScopeId commandScope;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        containers::DynamicArray<CompiledStep> steps;
        containers::DynamicArray<CompiledResourceAction> actions;
        containers::DynamicArray<rhi::ResourceRef> aliasPredecessors;
        containers::DynamicArray<CapturedDecisionRecord> decisions;
        containers::DynamicArray<rhi::CommandListEntryState> entryStates;
        containers::DynamicArray<CompiledResourceAction> exitActions;
        containers::DynamicArray<u32> statePredecessorPackets;
        // Maximum imported producer fence per queue, lowered into this packet's recorder.
        u64 incomingWaits[3]{};
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
        u32 endStep = InvalidRenderFlowResourceIndex;
        PlanPosition beginPosition;
        PlanPosition endPosition;
        bool texture = false;
        bool ended = false;
    };

    struct OpenScopeRecord
    {
        ResourceScopeId id;
        u32 allocation = InvalidRenderFlowResourceIndex;
        u32 previous = InvalidRenderFlowResourceIndex;
        PlanPosition begin;
        bool named = false;
        bool closed = false;
    };

    struct PendingExportRecord
    {
        ExportSlotId slot;
        u32 logicalSlot = InvalidRenderFlowResourceIndex;
        u32 allocation = InvalidRenderFlowResourceIndex;
        PhysicalResourceId physical;
        CommandScopeId terminalCommandScope;
        rhi::ResourceState terminalState = rhi::ResourceState::Common;
        rhi::QueueType terminalQueue = rhi::QueueType::Graphics;
        ExportReadinessKind readiness = ExportReadinessKind::SameQueueContinuation;
        FrameResourceKind kind = FrameResourceKind::Texture;
        rhi::TextureDesc textureDesc;
        rhi::BufferDesc bufferDesc;
    };

    struct PublishedExportRecord
    {
        ExportSlotId slot;
        FrameResourceKind kind = FrameResourceKind::Texture;
        rhi::Texture texture;
        rhi::Buffer buffer;
        rhi::TextureDesc textureDesc;
        rhi::BufferDesc bufferDesc;
        rhi::ResourceState terminalState = rhi::ResourceState::Common;
        rhi::QueueType terminalQueue = rhi::QueueType::Graphics;
        ExportReadinessKind readiness = ExportReadinessKind::SameQueueContinuation;
        rhi::GpuFence readyFence;
    };

    struct RetainedImportRecord
    {
        ExternalResourceToken token;
        FrameResourceDesc desc;
        rhi::Texture texture;
        rhi::Buffer buffer;
        rhi::ResourceState initialState = rhi::ResourceState::Common;
        rhi::ResourceState terminalState = rhi::ResourceState::Common;
        rhi::QueueType initialQueue = rhi::QueueType::Graphics;
        rhi::QueueType terminalQueue = rhi::QueueType::Graphics;
        ImportReadinessKind readiness = ImportReadinessKind::SameQueueContinuation;
        rhi::GpuFence incomingWait;
        rhi::AcquiredBackBuffer presentationAcquisition;
    };

    enum class PhysicalBindingKind : u8
    {
        DedicatedPool,
        PlacedPool,
        RetainedImport
    };

    struct PhysicalBindingRecord
    {
        PhysicalBindingKind kind = PhysicalBindingKind::DedicatedPool;
        u32 poolEntry = InvalidDedicatedResourceEntry;
        rhi::TextureRef texture;
        rhi::BufferRef buffer;
        u32 retainedImport = InvalidRenderFlowResourceIndex;
        bool explicitState = false;
        rhi::DescriptorHandle shaderResource;
        rhi::DescriptorHandle unorderedAccess;
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

    [[nodiscard]] inline constexpr bool ValidQueue(const rhi::QueueType queue) noexcept
    {
        return queue == rhi::QueueType::Graphics || queue == rhi::QueueType::Compute || queue == rhi::QueueType::Copy;
    }

    [[nodiscard]] inline constexpr rhi::CommandListSyncType RequiredQueueSync(const rhi::QueueType producer, const rhi::QueueType consumer) noexcept
    {
        return producer == rhi::QueueType::Graphics && consumer == rhi::QueueType::Compute   ? rhi::CommandListSyncType::ForkAsyncCompute
               : producer == rhi::QueueType::Compute && consumer == rhi::QueueType::Graphics ? rhi::CommandListSyncType::JoinAsyncCompute
                                                                                             : rhi::CommandListSyncType::None;
    }

    enum class RhiFailureContext : u8
    {
        BackendContract,
        ImportedIdentity,
        ExecutionAction
    };

    [[nodiscard]] inline RenderFlowResourceFailureCode MapRhiFailure(const rhi::Failure& failure, const RhiFailureContext context) noexcept
    {
        if (failure.code == rhi::FailureCode::NotInitialized)
            return RenderFlowResourceFailureCode::NotInitialized;
        if (failure.code == rhi::FailureCode::Unsupported)
            return RenderFlowResourceFailureCode::UnsupportedCapability;
        if (failure.code == rhi::FailureCode::CapacityExceeded)
            return RenderFlowResourceFailureCode::CapacityExceeded;
        if (failure.code == rhi::FailureCode::OutOfMemory)
            return RenderFlowResourceFailureCode::NativeOutOfMemory;
        if (failure.code == rhi::FailureCode::DeviceLost)
            return RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
        if (context == RhiFailureContext::ImportedIdentity && (failure.code == rhi::FailureCode::InvalidArgument || failure.code == rhi::FailureCode::InvalidReference))
            return RenderFlowResourceFailureCode::InvalidOrStaleIdentity;
        if (context == RhiFailureContext::ExecutionAction && (failure.code == rhi::FailureCode::InvalidCommandList || failure.code == rhi::FailureCode::NoBoundCommandList))
            return RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch;
        return failure.code == rhi::FailureCode::BackendFailure || failure.code == rhi::FailureCode::Busy || failure.code == rhi::FailureCode::Timeout ? RenderFlowResourceFailureCode::DeviceLostOrBackendFailure
                                                                                                                                                       : RenderFlowResourceFailureCode::BackendContractViolation;
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
    [[nodiscard]] bool PhysicalTextureDescEqual(const rhi::TextureDesc& left, const rhi::TextureDesc& right) noexcept;
    [[nodiscard]] bool PhysicalBufferDescEqual(const rhi::BufferDesc& left, const rhi::BufferDesc& right) noexcept;
    [[nodiscard]] bool TextureStateAllowed(const rhi::TextureDesc& desc, rhi::ResourceState state) noexcept;
    [[nodiscard]] bool BufferStateAllowed(const rhi::BufferDesc& desc, rhi::ResourceState state) noexcept;
    [[nodiscard]] bool QueueStateAllowed(rhi::QueueType queue, FrameResourceKind kind, rhi::ResourceState state) noexcept;

    void RetainGeneration(ExecutionGenerationRef::Impl* generation) noexcept;
    void ReleaseGeneration(ExecutionGenerationRef::Impl* generation) noexcept;
    void RetainAllocator(RenderFlowResourceAllocator::Impl* allocator) noexcept;
    void ReleaseAllocator(RenderFlowResourceAllocator::Impl* allocator) noexcept;
    void AbandonAllocatorSession(RenderFlowResourceAllocator::Impl& allocator, u32 generation) noexcept;
    [[nodiscard]] bool ResolveFrame(RenderFlowResourceAllocator::Impl& allocator, RenderFlowResourceFailure* failure) noexcept;
    void CancelSession(RenderFlowResourceAllocator::Impl& allocator, u32 generation) noexcept;
} // namespace vanguard::rendering::detail

namespace vanguard::rendering
{
    struct ExecutionGenerationRef::Impl
    {
        ~Impl();
        Impl() noexcept
            : packets(memory::pools::Rendering::GetInstance()), packetByNode(memory::pools::Rendering::GetInstance()), allocations(memory::pools::Rendering::GetInstance()),
              commandScopes(memory::pools::Rendering::GetInstance()), queueDependencies(memory::pools::Rendering::GetInstance()), pendingExports(memory::pools::Rendering::GetInstance()),
              physicalBindings(memory::pools::Rendering::GetInstance()), retainedImports(memory::pools::Rendering::GetInstance())
        {
        }

        concurrency::Atomic<u32> references{1};
        concurrency::Atomic<bool> terminal{false};
        ExecutionGenerationId id;
        containers::DynamicArray<detail::CompiledPacket> packets;
        containers::HashMap<u32, u32> packetByNode;
        containers::DynamicArray<detail::LogicalAllocationRecord> allocations;
        containers::DynamicArray<CompiledCommandScope> commandScopes;
        containers::DynamicArray<CompiledQueueDependency> queueDependencies;
        containers::DynamicArray<detail::PendingExportRecord> pendingExports;
        containers::DynamicArray<detail::PhysicalBindingRecord> physicalBindings;
        containers::DynamicArray<detail::RetainedImportRecord> retainedImports;
        detail::PlacedResourceBatch placedBatch;
        rhi::AcquiredBackBuffer presentationAcquisition;
        CommandScopeId presentationCommandScope;
        bool hasNativeResourceBindings = false;
        rhi::DescriptorDomain resourceDescriptors;
        rhi::DescriptorRetirement descriptorRetirement;
        bool descriptorDeviceLost = false;
    };

    struct RenderFlowResourceAllocator::Impl
    {
        explicit Impl(const RenderFlowResourceAllocatorConfig& allocatorConfig) noexcept
            : writerReservations(memory::pools::Rendering::GetInstance()), writerBatches(memory::pools::Rendering::GetInstance()), queueRequestGroups(memory::pools::Rendering::GetInstance()),
              retainedImports(memory::pools::Rendering::GetInstance()), publishedExports(memory::pools::Rendering::GetInstance()), config(allocatorConfig),
              nativeByteLedger(allocatorConfig.hardNativeByteLimit), dedicatedPool(allocatorConfig, nativeByteLedger), placedPool(allocatorConfig, nativeByteLedger)
        {
        }

        concurrency::Atomic<u32> references{1};
        mutable concurrency::SpinLock writerLock;
        concurrency::Atomic<u64> pendingRejectedOperations{0};
        containers::DynamicArray<detail::WriterReservation> writerReservations;
        containers::DynamicArray<detail::CandidateWriterBatch> writerBatches;
        containers::DynamicArray<detail::QueueRequestGroup> queueRequestGroups;
        containers::DynamicArray<detail::RetainedImportRecord> retainedImports;
        containers::DynamicArray<detail::PublishedExportRecord> publishedExports;
        RenderFlowResourceAllocatorConfig config;
        FrameResourcePolicy activePolicy;
        RenderFlowResourceAllocatorStats stats;
        detail::AllocatorNativeByteLedger nativeByteLedger;
        detail::DedicatedResourcePool dedicatedPool;
        detail::PlacedResourcePool placedPool;
        ExecutionGenerationRef::Impl* publishedGeneration = nullptr;
        u64 frameSerial = 0;
        u32 sessionGeneration = 0;
        u32 executionGeneration = 0;
        u32 reservedExportSlots = 0;
        RenderFlowResourceSessionState state = RenderFlowResourceSessionState::Idle;
    };

    struct ResourcePlanningWriter::Impl
    {
        explicit Impl(RenderFlowResourceAllocator::Impl& allocator) noexcept : owner(&allocator) {}

        RenderFlowResourceAllocator::Impl* owner = nullptr;
        // Stable for the planning phase; only this writer publishes to this slot.
        detail::WriterReservation* reservation = nullptr;
        detail::CandidateWriterBatch batch;
        u64 rejectedOperations = 0;
        RenderFlowResourceFailureCode firstFailureCode = RenderFlowResourceFailureCode::None;
        const char* firstFailureMessage = nullptr;
        bool failed = false;
        bool closed = false;
    };
} // namespace vanguard::rendering

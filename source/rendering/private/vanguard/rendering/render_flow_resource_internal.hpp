#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/rendering/render_flow_resource_execution.hpp>
#include <vanguard/rendering/render_flow_resource_placed.hpp>
#include <vanguard/rendering/render_flow_resource_pool.hpp>
#include <vanguard/system/assert.hpp>

#include <new>
#include <type_traits>
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
        ScopeOpen,
        ScopeClose,
        NamedScopeOpen,
        NamedScopeClose,
        SwapMappings,
        Decision,
        Export
    };

    enum class CandidateUseKind : u8
    {
        Texture,
        Buffer
    };

    struct CandidateResource
    {
        enum Flag : u8
        {
            FastPathExceptional = 1u << 0,
        };

        CandidateResource() noexcept : name(memory::pools::Rendering::GetInstance()) {}

        void Reset() noexcept
        {
            identity = CandidateIdentityKind::Named;
            flowSpace = {};
            logicalNameHash = 0;
            name.Clear();
            planningFlags = 0;
            namedScopeDepth = 0;
        }

        void SetNameBounded(const containers::StringView value) noexcept
        {
            nameDormantFrames = 0;
            const u32 length = value.Data() != nullptr ? value.Length() : 0u;
            constexpr u32 retainedFloor = 64;
            const u64 retained = length > retainedFloor ? length : retainedFloor;
            if (name.Capacity() > retainedFloor && static_cast<u64>(name.Capacity()) > retained * 4u)
            {
                containers::String compact{memory::pools::Rendering::GetInstance()};
                if (length != 0)
                    compact.Set(value);
                name.Swap(compact);
                return;
            }
            if (length != 0)
                name.Set(value);
        }

        CandidateIdentityKind identity = CandidateIdentityKind::Named;
        FlowSpaceId flowSpace;
        // Hash accelerator only. Named identity remains flow-space + full string.
        u32 logicalNameHash = 0;
        containers::String name;
        u8 nameDormantFrames = 0;
        bool nameRetentionTracked = false;
        u8 planningFlags = 0;
        u8 namedScopeDepth = 0;

        void MarkFastPathExceptional() noexcept { planningFlags |= FastPathExceptional; }
        [[nodiscard]] bool FastPathEligible() const noexcept { return (planningFlags & FastPathExceptional) == 0 && namedScopeDepth == 0; }
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

    union CandidateUsePayload
    {
        CandidateUsePayload() noexcept {}

        TextureUseDesc texture;
        BufferUseDesc buffer;
    };

    struct CandidateUse
    {
        [[nodiscard]] static CandidateUse Texture(const u32 resourceIndex, const u32 viewIndex, const TextureUseDesc& desc, const u32 begin) noexcept
        {
            CandidateUse result;
            result.kind = CandidateUseKind::Texture;
            result.resource = resourceIndex;
            result.view = viewIndex;
            result.beginSequence = begin;
            ::new (static_cast<void*>(&result.payload.texture)) TextureUseDesc(desc);
            return result;
        }

        [[nodiscard]] static CandidateUse Buffer(const u32 resourceIndex, const u32 viewIndex, const BufferUseDesc& desc, const u32 begin) noexcept
        {
            CandidateUse result;
            result.kind = CandidateUseKind::Buffer;
            result.resource = resourceIndex;
            result.view = viewIndex;
            result.beginSequence = begin;
            ::new (static_cast<void*>(&result.payload.buffer)) BufferUseDesc(desc);
            return result;
        }

        CandidateUseKind kind = CandidateUseKind::Buffer;
        u32 resource = InvalidRenderFlowResourceIndex;
        u32 view = InvalidRenderFlowResourceIndex;
        u32 beginSequence = InvalidRenderFlowResourceIndex;
        u32 endSequence = InvalidRenderFlowResourceIndex;
        CandidateUsePayload payload;
    };

    struct CandidateSemanticEvent
    {
        static constexpr u32 UseEndBit = 0x80000000u;
        static constexpr u32 IndexMask = ~UseEndBit;

        u32 value = InvalidRenderFlowResourceIndex;

        [[nodiscard]] static CandidateSemanticEvent Operation(const u32 operationIndex) noexcept
        {
            CandidateSemanticEvent result;
            result.value = operationIndex;
            return result;
        }

        [[nodiscard]] static CandidateSemanticEvent UseEnd(const u32 useIndex) noexcept
        {
            CandidateSemanticEvent result;
            result.value = UseEndBit | useIndex;
            return result;
        }

        [[nodiscard]] bool IsValid() const noexcept { return value != InvalidRenderFlowResourceIndex; }
        [[nodiscard]] bool IsUseEnd() const noexcept { return IsValid() && (value & UseEndBit) != 0; }
        [[nodiscard]] bool IsOperation() const noexcept { return IsValid() && (value & UseEndBit) == 0; }
        [[nodiscard]] u32 Index() const noexcept { return value & IndexMask; }

        [[nodiscard]] friend constexpr bool operator==(const CandidateSemanticEvent&, const CandidateSemanticEvent&) noexcept = default;
    };

    static_assert(sizeof(CandidateSemanticEvent) == 4);

    union CandidateOperationPayload
    {
        CandidateOperationPayload() noexcept {}

        ResourceScopeId scope;
        TerminalResourceExportDesc exportDesc;
    };

    struct CandidateOperation
    {
        enum Flag : u8
        {
            FlagValue = 1u << 0,
        };

        CandidateOperationKind kind = CandidateOperationKind::Decision;
        u8 flags = 0;
        u16 reserved16 = 0;
        u32 sequence = InvalidRenderFlowResourceIndex;
        u32 a = InvalidRenderFlowResourceIndex;
        u32 b = InvalidRenderFlowResourceIndex;
        CandidateOperationPayload payload;

        [[nodiscard]] static CandidateOperation DeclareResource(const CandidateOperationKind operationKind, const u32 resourceIndex, const u32 descIndex) noexcept
        {
            CandidateOperation result;
            result.kind = operationKind;
            result.a = resourceIndex;
            result.b = descIndex;
            return result;
        }

        [[nodiscard]] static CandidateOperation DeclareLike(const u32 resourceIndex, const u32 sourceIndex) noexcept
        {
            CandidateOperation result;
            result.kind = CandidateOperationKind::DeclareLike;
            result.a = resourceIndex;
            result.b = sourceIndex;
            return result;
        }

        [[nodiscard]] static CandidateOperation ImportResource(const CandidateOperationKind operationKind, const u32 resourceIndex, const u32 importIndex) noexcept
        {
            CandidateOperation result;
            result.kind = operationKind;
            result.a = resourceIndex;
            result.b = importIndex;
            return result;
        }

        [[nodiscard]] static CandidateOperation CreateTextureView(const u32 resourceIndex, const u32 viewIndex) noexcept
        {
            CandidateOperation result;
            result.kind = CandidateOperationKind::CreateTextureView;
            result.a = resourceIndex;
            result.b = viewIndex;
            return result;
        }

        [[nodiscard]] static CandidateOperation CreateBufferView(const u32 resourceIndex, const u32 viewIndex) noexcept
        {
            CandidateOperation result;
            result.kind = CandidateOperationKind::CreateBufferView;
            result.a = resourceIndex;
            result.b = viewIndex;
            return result;
        }

        [[nodiscard]] static CandidateOperation BeginUse(const CandidateOperationKind operationKind, const u32 useIndex) noexcept
        {
            CandidateOperation result;
            result.kind = operationKind;
            result.a = useIndex;
            return result;
        }

        [[nodiscard]] static CandidateOperation OpenScope(const u32 resourceIndex, const ResourceScopeId scope) noexcept
        {
            CandidateOperation result;
            result.kind = CandidateOperationKind::ScopeOpen;
            result.a = resourceIndex;
            ::new (static_cast<void*>(&result.payload.scope)) ResourceScopeId(scope);
            return result;
        }

        [[nodiscard]] static CandidateOperation CloseScope(const ResourceScopeId scope) noexcept
        {
            CandidateOperation result;
            result.kind = CandidateOperationKind::ScopeClose;
            ::new (static_cast<void*>(&result.payload.scope)) ResourceScopeId(scope);
            return result;
        }

        [[nodiscard]] static CandidateOperation NamedScope(const CandidateOperationKind operationKind, const u32 resourceIndex) noexcept
        {
            CandidateOperation result;
            result.kind = operationKind;
            result.a = resourceIndex;
            return result;
        }

        [[nodiscard]] static CandidateOperation SwapMappings(const u32 leftIndex, const u32 rightIndex) noexcept
        {
            CandidateOperation result;
            result.kind = CandidateOperationKind::SwapMappings;
            result.a = leftIndex;
            result.b = rightIndex;
            return result;
        }

        [[nodiscard]] static CandidateOperation Decision(const bool value) noexcept
        {
            CandidateOperation result;
            result.kind = CandidateOperationKind::Decision;
            if (value)
                result.flags |= FlagValue;
            return result;
        }

        [[nodiscard]] static CandidateOperation Export(const u32 resourceIndex, const u32 slotIndex, const TerminalResourceExportDesc& desc) noexcept
        {
            CandidateOperation result;
            result.kind = CandidateOperationKind::Export;
            result.a = resourceIndex;
            result.b = slotIndex;
            ::new (static_cast<void*>(&result.payload.exportDesc)) TerminalResourceExportDesc(desc);
            return result;
        }

        [[nodiscard]] bool FlagSet() const noexcept { return (flags & FlagValue) != 0; }
    };

    static_assert(std::is_trivially_copyable_v<CandidateUsePayload>);
    static_assert(std::is_trivially_copyable_v<CandidateUse>);
    static_assert(std::is_trivially_copyable_v<CandidateOperationPayload>);
    static_assert(std::is_trivially_copyable_v<CandidateOperation>);
    static_assert(sizeof(CandidateOperationPayload) == 8);
    static_assert(sizeof(CandidateOperation) == 24);

    union CandidateColdPayload
    {
        CandidateColdPayload() noexcept {}

        [[nodiscard]] static CandidateColdPayload TextureDeclaration(const FrameTextureDesc& desc) noexcept
        {
            CandidateColdPayload result;
            ::new (static_cast<void*>(&result.textureDeclaration)) FrameTextureDesc(desc);
            return result;
        }

        [[nodiscard]] static CandidateColdPayload BufferDeclaration(const FrameBufferDesc& desc) noexcept
        {
            CandidateColdPayload result;
            ::new (static_cast<void*>(&result.bufferDeclaration)) FrameBufferDesc(desc);
            return result;
        }

        FrameTextureDesc textureDeclaration;
        FrameBufferDesc bufferDeclaration;
    };

    static_assert(std::is_trivially_copyable_v<CandidateColdPayload>);
    static_assert(sizeof(CandidateColdPayload) == 88);

    struct CandidateWriterBatch
    {
        CandidateWriterBatch() noexcept
            : resources(memory::pools::Rendering::GetInstance()), textureViews(memory::pools::Rendering::GetInstance()), bufferViews(memory::pools::Rendering::GetInstance()),
              coldPayloads(memory::pools::Rendering::GetInstance()), uses(memory::pools::Rendering::GetInstance()), semanticEvents(memory::pools::Rendering::GetInstance()),
              operations(memory::pools::Rendering::GetInstance()),
              namedResourceSlots(memory::pools::Rendering::GetInstance()), namedResourceStamps(memory::pools::Rendering::GetInstance()),
              oversizedNameSlots(memory::pools::Rendering::GetInstance())
        {
        }

        CandidateWriterBatch(CandidateWriterBatch&&) noexcept = default;
        CandidateWriterBatch& operator=(CandidateWriterBatch&&) noexcept = default;
        CandidateWriterBatch(const CandidateWriterBatch&) = delete;
        CandidateWriterBatch& operator=(const CandidateWriterBatch&) = delete;

        void Reset(const RenderFlowNodeId batchNode = {}, const GpuFlowGroupId batchFlowGroup = {}, const CommandScopeId batchCommandScope = {}, const u32 generation = 0) noexcept
        {
            // Resource slots are reset lazily when reclaimed. Keeping the high-water
            // objects alive preserves nested String backing and makes frame reset O(1).
            resourceCount = 0;
            textureViews.Clear();
            bufferViews.Clear();
            coldPayloads.Clear();
            uses.Clear();
            semanticEvents.Clear();
            operations.Clear();
            ResetNamedResourceIndex();
            node = batchNode;
            flowGroup = batchFlowGroup;
            commandScope = batchCommandScope;
            sessionGeneration = generation;
            declarationCount = 0;
            semanticSequenceCount = 0;
            openUseCount = 0;
            stepCount = 0;
            explicitScopeOpenCount = 0;
            decisionCount = 0;
            exportCount = 0;
        }

        [[nodiscard]] u32 FindNamedResource(const FlowSpaceId flowSpace, const containers::StringView name, const u32 hash) const noexcept
        {
            if (namedResourceSlots.Empty())
                return InvalidRenderFlowResourceIndex;
            const u32 mask = namedResourceSlots.Size() - 1u;
            u32 slot = hash & mask;
            for (u32 probe = 0; probe < namedResourceSlots.Size(); ++probe)
            {
                if (namedResourceStamps[slot] != namedResourceGeneration)
                    return InvalidRenderFlowResourceIndex;
                const u32 resourceIndex = namedResourceSlots[slot];
                if (resourceIndex < resourceCount)
                {
                    const CandidateResource& candidate = resources[resourceIndex];
                    if (candidate.identity == CandidateIdentityKind::Named && candidate.logicalNameHash == hash && candidate.flowSpace == flowSpace &&
                        containers::StringView(candidate.name) == name)
                        return resourceIndex;
                }
                slot = (slot + 1u) & mask;
            }
            return InvalidRenderFlowResourceIndex;
        }

        void IndexNamedResource(const u32 resourceIndex) noexcept
        {
            constexpr u32 activationThreshold = 24;
            if (resourceCount < activationThreshold && namedResourceSlots.Empty())
                return;
            if (namedResourceSlots.Empty() || static_cast<u64>(namedResourceIndexSize + 1u) * 2u > namedResourceSlots.Size())
            {
                RebuildNamedResourceIndex();
                return;
            }
            InsertNamedResource(resourceIndex);
        }

        void RebuildNamedResourceIndex() noexcept
        {
            constexpr u32 activationThreshold = 24;
            if (resourceCount < activationThreshold)
            {
                ResetNamedResourceIndex();
                return;
            }
            u32 capacity = 64;
            while (static_cast<u64>(capacity) < static_cast<u64>(resourceCount) * 2u)
                capacity <<= 1u;
            if (namedResourceSlots.Size() != capacity)
            {
                namedResourceSlots.Resize(capacity, InvalidRenderFlowResourceIndex);
                namedResourceStamps.Resize(capacity, 0u);
                // Resize preserves the old prefix. Those stamps belonged to the
                // previous table generation and must not become live tombstones
                // when the rebuilt table restarts at generation one.
                for (u32& stamp : namedResourceStamps)
                    stamp = 0;
                namedResourceGeneration = 1;
            }
            else
                ResetNamedResourceIndex();
            namedResourceIndexSize = 0;
            for (u32 index = 0; index < resourceCount; ++index)
                if (resources[index].identity == CandidateIdentityKind::Named)
                    InsertNamedResource(index);
        }

        void ReleaseNamedResourceIndex() noexcept
        {
            namedResourceSlots.Clear();
            namedResourceStamps.Clear();
            namedResourceSlots.Shrink();
            namedResourceStamps.Shrink();
            namedResourceGeneration = 1;
            namedResourceIndexSize = 0;
        }

        void TrackOversizedName(const u32 resourceIndex) noexcept
        {
            if (resourceIndex >= resourceCount || resourceIndex >= resources.Size())
                return;
            CandidateResource& resource = resources[resourceIndex];
            constexpr u32 retainedNameThreshold = 256;
            if (resource.name.Capacity() <= retainedNameThreshold || resource.nameRetentionTracked)
                return;
            resource.nameRetentionTracked = true;
            resource.nameDormantFrames = 0;
            oversizedNameSlots.PushBack(resourceIndex);
        }

        void TrimDormantNameBacking() noexcept
        {
            constexpr u32 retainedNameThreshold = 256;
            constexpr u8 dormantFramesBeforeRelease = 2;
            u32 write = 0;
            for (const u32 resourceIndex : oversizedNameSlots)
            {
                if (resourceIndex >= resources.Size())
                    continue;
                CandidateResource& resource = resources[resourceIndex];
                if (resource.name.Capacity() <= retainedNameThreshold)
                {
                    resource.nameRetentionTracked = false;
                    resource.nameDormantFrames = 0;
                    continue;
                }
                if (resourceIndex < resourceCount)
                {
                    resource.nameDormantFrames = 0;
                    oversizedNameSlots[write++] = resourceIndex;
                    continue;
                }
                if (++resource.nameDormantFrames < dormantFramesBeforeRelease)
                {
                    oversizedNameSlots[write++] = resourceIndex;
                    continue;
                }
                containers::String compact{memory::pools::Rendering::GetInstance()};
                resource.name.Swap(compact);
                resource.nameRetentionTracked = false;
                resource.nameDormantFrames = 0;
            }
            oversizedNameSlots.Resize(write);
        }

        void PruneNameRetentionTracking() noexcept
        {
            u32 write = 0;
            for (const u32 resourceIndex : oversizedNameSlots)
            {
                if (resourceIndex >= resources.Size())
                    continue;
                oversizedNameSlots[write++] = resourceIndex;
            }
            oversizedNameSlots.Resize(write);
        }

        void ReleaseNameRetentionTracking() noexcept
        {
            oversizedNameSlots.Clear();
            oversizedNameSlots.Shrink();
        }

        void TrimAuxiliaryRetentionBacking() noexcept
        {
            constexpr u32 activationThreshold = 24;
            constexpr u32 minimumNamedIndexCapacity = 64;
            if (!namedResourceSlots.Empty())
            {
                if (resourceCount < activationThreshold)
                {
                    if (namedResourceSlots.Capacity() > minimumNamedIndexCapacity * 4u)
                        ReleaseNamedResourceIndex();
                }
                else
                {
                    u32 targetCapacity = minimumNamedIndexCapacity;
                    while (static_cast<u64>(targetCapacity) < static_cast<u64>(resourceCount) * 2u)
                        targetCapacity <<= 1u;
                    if (static_cast<u64>(namedResourceSlots.Capacity()) > static_cast<u64>(targetCapacity) * 4u)
                    {
                        RebuildNamedResourceIndex();
                        namedResourceSlots.Shrink();
                        namedResourceStamps.Shrink();
                    }
                }
            }

            constexpr u32 minimumTrackingCapacity = 64;
            const u32 trackingFloor = oversizedNameSlots.Size() > minimumTrackingCapacity ? oversizedNameSlots.Size() : minimumTrackingCapacity;
            if (static_cast<u64>(oversizedNameSlots.Capacity()) > static_cast<u64>(trackingFloor) * 4u)
                oversizedNameSlots.Shrink();
        }

        RenderFlowNodeId node;
        GpuFlowGroupId flowGroup;
        CommandScopeId commandScope;
        u32 sessionGeneration = 0;
        containers::DynamicArray<CandidateResource> resources;
        containers::DynamicArray<CandidateTextureView> textureViews;
        containers::DynamicArray<CandidateBufferView> bufferViews;
        containers::DynamicArray<CandidateColdPayload> coldPayloads;
        containers::DynamicArray<CandidateUse> uses;
        // Authoritative dense semantic order. Recording publishes one compact
        // entry for every operation and UseEnd, so Resolve never has to merge
        // two independently ordered tapes back together.
        containers::DynamicArray<CandidateSemanticEvent> semanticEvents;
        containers::DynamicArray<CandidateOperation> operations;
        containers::DynamicArray<u32> namedResourceSlots;
        containers::DynamicArray<u32> namedResourceStamps;
        containers::DynamicArray<u32> oversizedNameSlots;
        u32 resourceCount = 0;
        u32 declarationCount = 0;
        u32 semanticSequenceCount = 0;
        u32 openUseCount = 0;
        u32 stepCount = 0;
        u32 explicitScopeOpenCount = 0;
        u32 decisionCount = 0;
        u32 exportCount = 0;

    private:
        void ResetNamedResourceIndex() noexcept
        {
            namedResourceIndexSize = 0;
            ++namedResourceGeneration;
            if (namedResourceGeneration != 0)
                return;
            for (u32& stamp : namedResourceStamps)
                stamp = 0;
            namedResourceGeneration = 1;
        }

        void InsertNamedResource(const u32 resourceIndex) noexcept
        {
            if (resourceIndex >= resourceCount || namedResourceSlots.Empty())
                return;
            const CandidateResource& candidate = resources[resourceIndex];
            if (candidate.identity != CandidateIdentityKind::Named)
                return;
            const u32 mask = namedResourceSlots.Size() - 1u;
            u32 slot = candidate.logicalNameHash & mask;
            while (namedResourceStamps[slot] == namedResourceGeneration)
                slot = (slot + 1u) & mask;
            namedResourceSlots[slot] = resourceIndex;
            namedResourceStamps[slot] = namedResourceGeneration;
            ++namedResourceIndexSize;
        }

        u32 namedResourceGeneration = 1;
        u32 namedResourceIndexSize = 0;
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
        void Reset() noexcept
        {
            currentAllocation = InvalidRenderFlowResourceIndex;
        }

        u32 currentAllocation = InvalidRenderFlowResourceIndex;
    };

    struct LogicalAllocationRecord
    {
        FrameResourceDesc desc;
        PlanPosition declarationPosition;
        PlanPosition firstUsePosition;
        PlanPosition lastUsePosition;
        // Dense frame-semantic ordinals preserve the exact PlanPosition ordering
        // without requiring ordered structures to compare two-field keys.
        u32 firstUseSemanticOrdinal = InvalidRenderFlowResourceIndex;
        u32 lastUseSemanticOrdinal = InvalidRenderFlowResourceIndex;
        u32 firstUseCommandScopeIndex = InvalidRenderFlowResourceIndex;
        u32 lastUseCommandScopeIndex = InvalidRenderFlowResourceIndex;
        PhysicalResourceId physical;
        u8 queueMask = 0;
        bool used = false;
        bool exported = false;
        bool imported = false;
        bool requiresCanonicalDescriptors = false;
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

    enum class CompiledResourceActionKind : u8;

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
        u32 actionTapeOffset = 0;
        u32 actionTapeCount = 0;
        u32 resourceBinding = InvalidRenderFlowResourceIndex;
        // RF17 packet-local exact-state lowering owns ordinary use-state barriers.
        // This span addresses CompiledPacket::compiledBarriers and executes after
        // alias activation, before this step's exceptional before-action slice.
        u32 compiledBarrierOffset = 0;
        u32 compiledBarrierCount = 0;
        rhi::ResourceState terminalState = rhi::ResourceState::Unknown;
        TextureClearValue nativeTextureClearValue;
        BufferClearValue nativeBufferClearValue;
        CompiledResourceActionKind nativeClearKind{};
        bool hasExplicitView = false;
        bool finalizeForAlias = false;
        bool nativeHasClear = false;
        bool terminalPresent = false;
        // Resolve-time intent consumed by RF17's packet-local exact-state compiler.
        // Runtime execution never lowers Unknown -> requiredState from this flag.
        bool localStateRequired = false;
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
        u32 resourceBinding = InvalidRenderFlowResourceIndex;
        // Barrier actions retain their semantic action record for generic fallback
        // and validation, but their exact native request lives in this packet-local
        // compiled barrier span. Non-barrier actions keep a zero count.
        u32 compiledBarrierOffset = 0;
        u32 compiledBarrierCount = 0;
        rhi::ResourceState before = rhi::ResourceState::Unknown;
        rhi::ResourceState after = rhi::ResourceState::Unknown;
        rhi::SubresourceRange textureSubresources;
        TextureClearValue textureClearValue;
        BufferClearValue bufferClearValue;
    };

    enum class CompiledActionTapePhase : u8
    {
        Begin,
        End,
        Exit
    };

    // One entry per compiled step that has exceptional semantic work. Ordinary
    // local state requests remain intrinsic to begin steps. Begin exceptional
    // order is alias activation then action slice; End order is action slice then
    // alias finalization. Exit entries address packet.exitActions.
    struct CompiledActionTapeEntry
    {
        CompiledActionTapePhase phase = CompiledActionTapePhase::Begin;
        u32 stepIndex = InvalidRenderFlowResourceIndex;
        u32 actionOffset = 0;
        u32 actionCount = 0;
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

    enum class PacketExecutionMode : u8
    {
        Undecided,
        LegacyUseReplay,
        CompiledRanges
    };

    struct PacketUseLiveness
    {
        PacketUseLiveness() noexcept : useActive(memory::pools::Rendering::GetInstance()) {}

        void PublishCompiledRange(const u32 firstUseOrdinal, const u32 endUseOrdinal, const u32 epoch) noexcept
        {
            activeRangeFirstUseOrdinal.SetValue(firstUseOrdinal);
            activeRangeEndUseOrdinal.SetValue(endUseOrdinal);
            activeRangeEpoch.SetValue(epoch);
        }

        void ResetCompiledRange() noexcept
        {
            activeRangeEpoch.SetValue(0);
            activeRangeFirstUseOrdinal.SetValue(InvalidRenderFlowResourceIndex);
            activeRangeEndUseOrdinal.SetValue(InvalidRenderFlowResourceIndex);
        }

        [[nodiscard]] bool ContainsCompiledUse(const u32 epoch, const u32 useOrdinal) const noexcept
        {
            const u32 activeEpoch = activeRangeEpoch.GetValue();
            if (activeEpoch != epoch)
                return false;
            const u32 firstUseOrdinal = activeRangeFirstUseOrdinal.GetValue();
            const u32 endUseOrdinal = activeRangeEndUseOrdinal.GetValue();
            return useOrdinal >= firstUseOrdinal && useOrdinal < endUseOrdinal && activeRangeEpoch.GetValue() == activeEpoch;
        }

        concurrency::Atomic<u32> references{1};
        concurrency::Atomic<u32> activeRangeEpoch{0};
        concurrency::Atomic<u32> activeRangeFirstUseOrdinal{InvalidRenderFlowResourceIndex};
        concurrency::Atomic<u32> activeRangeEndUseOrdinal{InvalidRenderFlowResourceIndex};
        ExecutionGenerationId generation;
        u32 packetIndex = InvalidRenderFlowResourceIndex;
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
                liveness->ResetCompiledRange();
                for (u8& active : liveness->useActive)
                    active = 0;
            }
            ReleasePacketUseLiveness(liveness);
        }

        PacketExecutionRuntime(PacketExecutionRuntime&& other) noexcept
            : state(other.state), mode(other.mode), nextStep(other.nextStep), liveness(other.liveness), activeUseCount(other.activeUseCount),
              activeRangeEpoch(other.activeRangeEpoch), activeRangeBeginStep(other.activeRangeBeginStep), activeRangeEndStep(other.activeRangeEndStep),
              activeRangeEndCount(other.activeRangeEndCount), lastCompletedRangeEpoch(other.lastCompletedRangeEpoch), expectedRangeCount(other.expectedRangeCount)
        {
            other.liveness = nullptr;
            other.activeRangeEpoch = 0;
            other.activeRangeBeginStep = InvalidRenderFlowResourceIndex;
            other.activeRangeEndStep = InvalidRenderFlowResourceIndex;
            other.activeRangeEndCount = 0;
            other.lastCompletedRangeEpoch = 0;
            other.expectedRangeCount = InvalidRenderFlowResourceIndex;
        }

        PacketExecutionRuntime& operator=(PacketExecutionRuntime&& other) noexcept
        {
            if (this == &other)
                return *this;
            if (liveness != nullptr)
            {
                liveness->ResetCompiledRange();
                for (u8& active : liveness->useActive)
                    active = 0;
            }
            ReleasePacketUseLiveness(liveness);
            state = other.state;
            mode = other.mode;
            nextStep = other.nextStep;
            liveness = other.liveness;
            activeUseCount = other.activeUseCount;
            activeRangeEpoch = other.activeRangeEpoch;
            activeRangeBeginStep = other.activeRangeBeginStep;
            activeRangeEndStep = other.activeRangeEndStep;
            activeRangeEndCount = other.activeRangeEndCount;
            lastCompletedRangeEpoch = other.lastCompletedRangeEpoch;
            expectedRangeCount = other.expectedRangeCount;
            other.liveness = nullptr;
            other.activeRangeEpoch = 0;
            other.activeRangeBeginStep = InvalidRenderFlowResourceIndex;
            other.activeRangeEndStep = InvalidRenderFlowResourceIndex;
            other.activeRangeEndCount = 0;
            other.lastCompletedRangeEpoch = 0;
            other.expectedRangeCount = InvalidRenderFlowResourceIndex;
            return *this;
        }
        PacketExecutionRuntime(const PacketExecutionRuntime&) = delete;
        PacketExecutionRuntime& operator=(const PacketExecutionRuntime&) = delete;

        // One recording owner; continuation handoffs and terminal access are job-ordered.
        PacketRuntimeState state = PacketRuntimeState::Unclaimed;
        PacketExecutionMode mode = PacketExecutionMode::Undecided;
        u32 nextStep = 0;
        PacketUseLiveness* liveness = nullptr;
        u32 activeUseCount = 0;
        // Compiled-range execution advances whole contiguous child spans at once.
        // Keep an explicit runtime witness even when the child has zero uses and
        // therefore no PacketUseLiveness allocation.
        u32 activeRangeEpoch = 0;
        u32 activeRangeBeginStep = InvalidRenderFlowResourceIndex;
        u32 activeRangeEndStep = InvalidRenderFlowResourceIndex;
        u32 activeRangeEndCount = 0;
        u32 lastCompletedRangeEpoch = 0;
        u32 expectedRangeCount = InvalidRenderFlowResourceIndex;
    };

    struct CompiledPacket
    {
        CompiledPacket() noexcept
            : steps(memory::pools::Rendering::GetInstance()), beginStepByUseOrdinal(memory::pools::Rendering::GetInstance()), endStepByUseOrdinal(memory::pools::Rendering::GetInstance()),
              actions(memory::pools::Rendering::GetInstance()), actionTape(memory::pools::Rendering::GetInstance()), aliasPredecessors(memory::pools::Rendering::GetInstance()),
              aliasPredecessorPhysicalIndices(memory::pools::Rendering::GetInstance()), aliasPredecessorBindingSlots(memory::pools::Rendering::GetInstance()),
              decisions(memory::pools::Rendering::GetInstance()), resourceBindings(memory::pools::Rendering::GetInstance()), resourceBindingPhysicals(memory::pools::Rendering::GetInstance()),
              resourceBindingShaderResources(memory::pools::Rendering::GetInstance()), resourceBindingUnorderedAccess(memory::pools::Rendering::GetInstance()),
              entryStates(memory::pools::Rendering::GetInstance()), compiledBarriers(memory::pools::Rendering::GetInstance()), compiledBarrierBatches(memory::pools::Rendering::GetInstance()),
              exitActions(memory::pools::Rendering::GetInstance()), statePredecessorPackets(memory::pools::Rendering::GetInstance())
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
        // Expected certified structural step count even when the warm
        // execution-program path defers copying this array until validation.
        u32 structuralStepCount = 0;
        // Dense ResourceUseId.ordinal -> TextureUseBegin/BufferUseBegin step.
        // Sealed while steps are emitted/copied so later execution-range publication
        // never needs to rescan the packet's use stream.
        containers::DynamicArray<u32> beginStepByUseOrdinal;
        // Dense ResourceUseId.ordinal -> matching UseEnd step.
        containers::DynamicArray<u32> endStepByUseOrdinal;
        containers::DynamicArray<CompiledResourceAction> actions;
        containers::DynamicArray<CompiledActionTapeEntry> actionTape;
        containers::DynamicArray<rhi::ResourceRef> aliasPredecessors;
        containers::DynamicArray<u32> aliasPredecessorPhysicalIndices;
        containers::DynamicArray<u32> aliasPredecessorBindingSlots;
        containers::DynamicArray<CapturedDecisionRecord> decisions;
        // Sorted unique native resource table for this packet's compiled resource
        // execution. OpenCursor admits this table to the command list once; generic
        // node recording may still add resources outside the table normally.
        containers::DynamicArray<rhi::ResourceRef> resourceBindings;
        // Packet-local binding slot -> generation physical identity. Kept parallel
        // with resourceBindings so execution validates the packet-local slot once
        // instead of rediscovering the generation-wide physical record per action.
        containers::DynamicArray<PhysicalResourceId> resourceBindingPhysicals;
        // Canonical whole-resource descriptor witnesses are packet-local too so
        // resolved-use publication does not return to the generation-wide physical
        // table after the binding slot has already been proved.
        containers::DynamicArray<rhi::DescriptorHandle> resourceBindingShaderResources;
        containers::DynamicArray<rhi::DescriptorHandle> resourceBindingUnorderedAccess;
        containers::DynamicArray<rhi::CommandListEntryState> entryStates;
        // RF17 exact barrier program. Entries are appended in semantic execution
        // order. Parallel batch ids identify dependency-free native runs; Invalid
        // marks a generic-only transition (currently Present/fixed-heap cases).
        containers::DynamicArray<rhi::CompiledBarrier> compiledBarriers;
        containers::DynamicArray<u32> compiledBarrierBatches;
        containers::DynamicArray<CompiledResourceAction> exitActions;
        u32 exitActionTapeOffset = 0;
        u32 exitActionTapeCount = 0;
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
        u32 nextAllocationUse = InvalidRenderFlowResourceIndex;
        u32 beginStep = InvalidRenderFlowResourceIndex;
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
        // Dense packet/command-scope index of the final executable use. Item #8
        // guarantees packet index == dense command-scope index, so downstream
        // terminal validation never needs to rediscover this relation by ID.
        u32 terminalPacket = InvalidRenderFlowResourceIndex;
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
        ResourceContractPatchId contractPatch;
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

    // Allocator-persistent canonical descriptor owner for externally retained
    // resources. The ResourceRef carries the native resource generation; the
    // allocator cache itself is scoped to one RHI/device incarnation and one
    // descriptor-domain generation. Frame generations borrow handles from these
    // owners and never retire them directly.
    struct RetainedImportDescriptorOwner
    {
        rhi::ResourceRef resource;
        CanonicalResourceDescriptors descriptors;
        rhi::ResidencyFenceSet safeAfter;
        u32 activeBorrows = 0;
        u32 reusablePosition = InvalidRenderFlowResourceIndex;
        FrameResourceKind kind = FrameResourceKind::Invalid;
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
        FrameResourceKind stateDescriptorKind = FrameResourceKind::Invalid;
        rhi::ResourceState initialState = rhi::ResourceState::Unknown;
        union
        {
            rhi::TextureUsage textureUsage = rhi::TextureUsage::None;
            rhi::BufferUsage bufferUsage;
        };
        u16 textureMipCount = 1;
        u16 textureArraySize = 1;
        rhi::MemoryType bufferMemoryType = rhi::MemoryType::DeviceLocal;
        u32 retainedImport = InvalidRenderFlowResourceIndex;
        u32 retainedDescriptorOwner = InvalidRenderFlowResourceIndex;
        bool explicitState = false;
        rhi::DescriptorHandle shaderResource;
        rhi::DescriptorHandle unorderedAccess;
    };

    // Every canonical handle is borrowed from a persistent physical/import
    // owner. Generation teardown releases the retained-import borrow but does
    // not rewrite or retire the descriptor bundle on an ordinary frame.
    static_assert(sizeof(PhysicalBindingRecord) == 72);

    struct SealedWriterRecord
    {
        u32 groupIndex = InvalidRenderFlowResourceIndex;
        u32 resourceSlotOffset = 0;
        u32 textureViewOffset = 0;
        u32 bufferViewOffset = 0;
        u32 useRecordOffset = 0;
        u32 semanticEventOffset = 0;
    };

    static_assert(sizeof(SealedWriterRecord) == 24);

    struct SealedPlanningSummary
    {
        void Reset() noexcept
        {
            declarationCount = 0;
            resourceSlotCount = 0;
            textureViewCount = 0;
            bufferViewCount = 0;
            useBeginCount = 0;
            unterminatedUseCount = 0;
            explicitScopeOpenCount = 0;
            exportCount = 0;
            semanticEventCount = 0;
        }

        u32 declarationCount = 0;
        u32 resourceSlotCount = 0;
        u32 textureViewCount = 0;
        u32 bufferViewCount = 0;
        u32 useBeginCount = 0;
        u32 unterminatedUseCount = 0;
        u32 explicitScopeOpenCount = 0;
        u32 exportCount = 0;
        u32 semanticEventCount = 0;
    };

    struct NamedScopeState
    {
        u32 depth = 0;
        PlanPosition firstOpen;
    };

    struct TextureSubresourceLogicalState
    {
        rhi::ResourceState state = rhi::ResourceState::Common;
        u32 activeUseCount = 0;
        bool contentsDefined = false;
        CommandScopeId lastCommandScope;
        LogicalAccessIntent lastAccess = LogicalAccessIntent::Read;
        bool hasPriorUse = false;
        bool authoritativeBefore = false;
    };

    struct AllocationLogicalState
    {
        u32 textureOffset = InvalidRenderFlowResourceIndex;
        u32 textureCount = 0;
        u32 firstActiveUse = InvalidRenderFlowResourceIndex;
        // Stable allocation-local use timeline. Unlike firstActiveUse, these
        // links survive UseEnd and remain valid through the whole Resolve.
        u32 firstUseRecord = InvalidRenderFlowResourceIndex;
        u32 lastUseBeginRecord = InvalidRenderFlowResourceIndex;
        u32 terminalUseRecord = InvalidRenderFlowResourceIndex;
        u8 useQueueMask = 0;
        rhi::ResourceState bufferState = rhi::ResourceState::Common;
        bool bufferContentsDefined = false;
        CommandScopeId bufferLastCommandScope;
        LogicalAccessIntent bufferLastAccess = LogicalAccessIntent::Read;
        bool bufferHasPriorUse = false;
        bool bufferAuthoritativeBefore = false;
        bool initializationPending = false;
    };

    struct LogicalNameKey
    {
        FlowSpaceId flowSpace;
        containers::StringView name;

        [[nodiscard]] friend bool operator==(const LogicalNameKey& left, const LogicalNameKey& right) noexcept
        {
            return left.flowSpace == right.flowSpace && left.name == right.name;
        }
    };

    struct WholeFrameLane
    {
        u64 capacity = 0;
        u32 lastUseSemanticOrdinal = InvalidRenderFlowResourceIndex;
        u32 lastUseCommandScopeIndex = InvalidRenderFlowResourceIndex;
        u32 physicalIndex = InvalidRenderFlowResourceIndex;
        u32 reuseClass = InvalidRenderFlowResourceIndex;
        u32 epoch = 1;
    };

    struct PersistentDedicatedPlanAllocation
    {
        FrameResourceDesc desc;
        // Certified physical request is immutable for the lifetime of this
        // exact dedicated contract. Rebuild only when the contract changes;
        // warm assignment must still acquire/check the live owner each frame.
        DedicatedResourceRequest dedicatedRequest;
        PlanPosition firstUsePosition;
        PlanPosition lastUsePosition;
        u32 firstUseCommandScopeIndex = InvalidRenderFlowResourceIndex;
        u32 lastUseCommandScopeIndex = InvalidRenderFlowResourceIndex;
        u32 lane = InvalidRenderFlowResourceIndex;
        u8 queueMask = 0;
        bool used = false;
        bool exported = false;
        bool imported = false;
        bool requiresCanonicalDescriptors = false;
    };

    struct PersistentDedicatedPlanLane
    {
        DedicatedResourceClassKey compatibility;
        u64 requiredCapacity = 0;
    };

    struct PersistentDedicatedPlanLaneRuntime
    {
        PersistentDedicatedPlanLaneRuntime() noexcept : leases(memory::pools::Rendering::GetInstance()) {}
        PersistentDedicatedPlanLaneRuntime(PersistentDedicatedPlanLaneRuntime&&) noexcept = default;
        PersistentDedicatedPlanLaneRuntime& operator=(PersistentDedicatedPlanLaneRuntime&&) noexcept = default;
        PersistentDedicatedPlanLaneRuntime(const PersistentDedicatedPlanLaneRuntime&) = delete;
        PersistentDedicatedPlanLaneRuntime& operator=(const PersistentDedicatedPlanLaneRuntime&) = delete;

        containers::DynamicArray<DedicatedResourceLease> leases;
    };

    // Immutable native-state program captured after the certified dedicated
    // assignment topology has been lowered. It owns no RHI handles, descriptor
    // borrows, packet liveness, or frame fences: every execution generation
    // receives its own patched copy before publication.
    struct PersistentStateDescriptorWitness
    {
        PhysicalBindingKind kind = PhysicalBindingKind::DedicatedPool;
        FrameResourceKind resourceKind = FrameResourceKind::Invalid;
        rhi::ResourceState initialState = rhi::ResourceState::Unknown;
        u32 usage = 0;
        u16 mipCount = 0;
        u16 arraySize = 0;
        rhi::MemoryType memoryType = rhi::MemoryType::DeviceLocal;
        bool explicitState = false;
    };

    struct PersistentStateEntryCell
    {
        rhi::ResourceState state = rhi::ResourceState::Unknown;
        rhi::TextureSubresource subresource;
    };

    struct PersistentStateEntryGroup
    {
        u32 offset = 0;
        u32 count = 0;
    };

    struct PersistentStateBindingSort
    {
        rhi::ResourceRef resource;
        u32 oldSlot = InvalidRenderFlowResourceIndex;
    };

    struct PersistentStatePacket
    {
        PersistentStatePacket() noexcept
            : steps(memory::pools::Rendering::GetInstance()), actions(memory::pools::Rendering::GetInstance()),
              exitActions(memory::pools::Rendering::GetInstance()), actionTape(memory::pools::Rendering::GetInstance()),
              statePredecessors(memory::pools::Rendering::GetInstance()), bindingPhysicalIndices(memory::pools::Rendering::GetInstance()),
              entryGroups(memory::pools::Rendering::GetInstance()), entryCells(memory::pools::Rendering::GetInstance()),
              barriers(memory::pools::Rendering::GetInstance()), barrierBatches(memory::pools::Rendering::GetInstance()),
              decisions(memory::pools::Rendering::GetInstance()) {}

        PersistentStatePacket(PersistentStatePacket&&) noexcept = default;
        PersistentStatePacket& operator=(PersistentStatePacket&&) noexcept = default;
        PersistentStatePacket(const PersistentStatePacket&) = delete;
        PersistentStatePacket& operator=(const PersistentStatePacket&) = delete;

        RenderFlowNodeId node;
        GpuFlowGroupId flowGroup;
        CommandScopeId commandScope;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        containers::DynamicArray<CompiledStep> steps;
        containers::DynamicArray<CompiledResourceAction> actions;
        containers::DynamicArray<CompiledResourceAction> exitActions;
        containers::DynamicArray<CompiledActionTapeEntry> actionTape;
        containers::DynamicArray<u32> statePredecessors;
        containers::DynamicArray<u32> bindingPhysicalIndices;
        containers::DynamicArray<PersistentStateEntryGroup> entryGroups;
        containers::DynamicArray<PersistentStateEntryCell> entryCells;
        containers::DynamicArray<rhi::CompiledBarrier> barriers;
        containers::DynamicArray<u32> barrierBatches;
        containers::DynamicArray<u8> decisions;
        u32 exitTapeOffset = 0;
        u32 exitTapeCount = 0;
    };

    struct PersistentStateProgram
    {
        PersistentStateProgram() noexcept
            : physicalByAllocation(memory::pools::Rendering::GetInstance()),
              physicalWitnesses(memory::pools::Rendering::GetInstance()),
              packets(memory::pools::Rendering::GetInstance()) {}
        PersistentStateProgram(PersistentStateProgram&&) noexcept = default;
        PersistentStateProgram& operator=(PersistentStateProgram&&) noexcept = default;
        PersistentStateProgram(const PersistentStateProgram&) = delete;
        PersistentStateProgram& operator=(const PersistentStateProgram&) = delete;

        void Clear() noexcept
        {
            physicalByAllocation.Clear();
            physicalWitnesses.Clear();
            packets.Clear();
            valid = false;
        }

        containers::DynamicArray<u32> physicalByAllocation;
        containers::DynamicArray<PersistentStateDescriptorWitness> physicalWitnesses;
        containers::DynamicArray<PersistentStatePacket> packets;
        bool valid = false;
    };

    struct PersistentDedicatedPhysicalPlan
    {
        PersistentDedicatedPhysicalPlan() noexcept
            : allocations(memory::pools::Rendering::GetInstance()), scopeQueues(memory::pools::Rendering::GetInstance()),
              crossQueueProducerScopes(memory::pools::Rendering::GetInstance()), lanes(memory::pools::Rendering::GetInstance()),
              laneRuntime(memory::pools::Rendering::GetInstance())
        {
        }

        PersistentDedicatedPhysicalPlan(PersistentDedicatedPhysicalPlan&&) noexcept = default;
        PersistentDedicatedPhysicalPlan& operator=(PersistentDedicatedPhysicalPlan&&) noexcept = default;
        PersistentDedicatedPhysicalPlan(const PersistentDedicatedPhysicalPlan&) = delete;
        PersistentDedicatedPhysicalPlan& operator=(const PersistentDedicatedPhysicalPlan&) = delete;

        void Clear() noexcept
        {
            allocations.Clear();
            scopeQueues.Clear();
            crossQueueProducerScopes.Clear();
            lanes.Clear();
            laneRuntime.Clear();
            stateProgram.Clear();
            valid = false;
        }

        containers::DynamicArray<PersistentDedicatedPlanAllocation> allocations;
        containers::DynamicArray<rhi::QueueType> scopeQueues;
        containers::DynamicArray<u32> crossQueueProducerScopes;
        containers::DynamicArray<PersistentDedicatedPlanLane> lanes;
        containers::DynamicArray<PersistentDedicatedPlanLaneRuntime> laneRuntime;
        PersistentStateProgram stateProgram;
        bool valid = false;
    };

    inline constexpr u32 MaximumPersistentDedicatedPhysicalPrograms = 4;

    // RF13 cache-variant physical program. The immutable resource contract owns
    // semantic topology; this allocator-local companion owns only dedicated-pool
    // lane topology and strong reusable leases. Exact contract publication and
    // backend epochs prevent graph-cache reuse, storage ABA, device recreation,
    // or descriptor-domain replacement from reviving a stale native chain.
    struct PersistentDedicatedPhysicalProgram
    {
        PersistentDedicatedPhysicalPlan plan;
        u64 contractIdentity = 0;
        u64 contractPublicationSerial = 0;
        u64 rhiDeviceIncarnation = 0;
        u64 descriptorDomainIdentity = 0;
        u64 lastUsedFrame = 0;

        [[nodiscard]] bool Matches(const u64 identity, const u64 publication, const u64 device, const u64 domain) const noexcept
        {
            return plan.valid && contractIdentity != 0 && contractIdentity == identity && contractPublicationSerial == publication &&
                   rhiDeviceIncarnation == device && descriptorDomainIdentity == domain;
        }

        void Clear() noexcept
        {
            plan.Clear();
            contractIdentity = 0;
            contractPublicationSerial = 0;
            rhiDeviceIncarnation = 0;
            descriptorDomainIdentity = 0;
            lastUsedFrame = 0;
        }
    };

    struct StructuralResourceKey
    {
        StructuralResourceKey() noexcept : name(memory::pools::Rendering::GetInstance()) {}
        StructuralResourceKey(StructuralResourceKey&&) noexcept = default;
        StructuralResourceKey& operator=(StructuralResourceKey&&) noexcept = default;
        StructuralResourceKey(const StructuralResourceKey&) = delete;
        StructuralResourceKey& operator=(const StructuralResourceKey&) = delete;

        CandidateIdentityKind identity = CandidateIdentityKind::Named;
        FlowSpaceId flowSpace;
        containers::String name;
    };

    struct StructuralImportContract
    {
        ResourceContractPatchId contractPatch;
        FrameResourceDesc desc;
        rhi::ResourceState initialState = rhi::ResourceState::Common;
        rhi::ResourceState terminalState = rhi::ResourceState::Common;
        rhi::QueueType initialQueue = rhi::QueueType::Graphics;
        rhi::QueueType terminalQueue = rhi::QueueType::Graphics;
        rhi::QueueType incomingWaitQueue = rhi::QueueType::Graphics;
        ImportReadinessKind readiness = ImportReadinessKind::SameQueueContinuation;
        bool presentation = false;
    };

    struct StructuralBatchKey
    {
        StructuralBatchKey() noexcept
            : resources(memory::pools::Rendering::GetInstance()), textureViews(memory::pools::Rendering::GetInstance()),
              bufferViews(memory::pools::Rendering::GetInstance()), uses(memory::pools::Rendering::GetInstance()),
              semanticEvents(memory::pools::Rendering::GetInstance()), operations(memory::pools::Rendering::GetInstance())
        {
        }

        StructuralBatchKey(StructuralBatchKey&&) noexcept = default;
        StructuralBatchKey& operator=(StructuralBatchKey&&) noexcept = default;
        StructuralBatchKey(const StructuralBatchKey&) = delete;
        StructuralBatchKey& operator=(const StructuralBatchKey&) = delete;

        RenderFlowNodeId node;
        GpuFlowGroupId flowGroup;
        CommandScopeId commandScope;
        u32 groupIndex = InvalidRenderFlowResourceIndex;
        u32 declarationCount = 0;
        u32 semanticSequenceCount = 0;
        u32 stepCount = 0;
        u32 explicitScopeOpenCount = 0;
        u32 decisionCount = 0;
        u32 exportCount = 0;
        containers::DynamicArray<StructuralResourceKey> resources;
        containers::DynamicArray<CandidateTextureView> textureViews;
        containers::DynamicArray<CandidateBufferView> bufferViews;
        containers::DynamicArray<CandidateUse> uses;
        containers::DynamicArray<CandidateSemanticEvent> semanticEvents;
        containers::DynamicArray<CandidateOperation> operations;
    };

    struct StructuralQueueGroupKey
    {
        u32 groupIndex = InvalidRenderFlowResourceIndex;
        QueueRequestRecord requests[2];
        u32 count = 0;
    };

    struct StructuralPacketTemplate
    {
        struct ImportEvent
        {
            u32 logicalAllocation = InvalidRenderFlowResourceIndex;
            bool beginsUse = false;
            bool terminalPresent = false;
        };
        StructuralPacketTemplate() noexcept
            : steps(memory::pools::Rendering::GetInstance()), beginStepByUseOrdinal(memory::pools::Rendering::GetInstance()),
              endStepByUseOrdinal(memory::pools::Rendering::GetInstance()), importEvents(memory::pools::Rendering::GetInstance()) {}
        StructuralPacketTemplate(StructuralPacketTemplate&&) noexcept = default;
        StructuralPacketTemplate& operator=(StructuralPacketTemplate&&) noexcept = default;
        StructuralPacketTemplate(const StructuralPacketTemplate&) = delete;
        StructuralPacketTemplate& operator=(const StructuralPacketTemplate&) = delete;

        RenderFlowNodeId node;
        GpuFlowGroupId flowGroup;
        CommandScopeId commandScope;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        containers::DynamicArray<CompiledStep> steps;
        // Immutable cold-compiler dense step maps and dynamic import sites.
        containers::DynamicArray<u32> beginStepByUseOrdinal;
        containers::DynamicArray<u32> endStepByUseOrdinal;
        containers::DynamicArray<ImportEvent> importEvents;
        u32 decisionCount = 0;
        u32 useSlotCount = 0;
    };

    struct PersistentStructuralPlan
    {
        PersistentStructuralPlan() noexcept
            : batches(memory::pools::Rendering::GetInstance()), declarationDescs(memory::pools::Rendering::GetInstance()),
              importContracts(memory::pools::Rendering::GetInstance()), queueGroups(memory::pools::Rendering::GetInstance()),
              allocations(memory::pools::Rendering::GetInstance()), packets(memory::pools::Rendering::GetInstance()),
              commandScopes(memory::pools::Rendering::GetInstance()), queueDependencies(memory::pools::Rendering::GetInstance()),
              crossQueueProducerScopes(memory::pools::Rendering::GetInstance()), firstTouchAllocationOrder(memory::pools::Rendering::GetInstance()),
              pendingExports(memory::pools::Rendering::GetInstance()), exportPatches(memory::pools::Rendering::GetInstance())
        {
        }

        PersistentStructuralPlan(PersistentStructuralPlan&&) noexcept = default;
        PersistentStructuralPlan& operator=(PersistentStructuralPlan&&) noexcept = default;
        PersistentStructuralPlan(const PersistentStructuralPlan&) = delete;
        PersistentStructuralPlan& operator=(const PersistentStructuralPlan&) = delete;

        void Clear() noexcept
        {
            batches.Clear();
            declarationDescs.Clear();
            importContracts.Clear();
            queueGroups.Clear();
            allocations.Clear();
            packets.Clear();
            commandScopes.Clear();
            queueDependencies.Clear();
            crossQueueProducerScopes.Clear();
            firstTouchAllocationOrder.Clear();
            pendingExports.Clear();
            exportPatches.Clear();
            semanticEventCount = 0;
            valid = false;
        }

        containers::DynamicArray<StructuralBatchKey> batches;
        containers::DynamicArray<FrameResourceDesc> declarationDescs;
        containers::DynamicArray<StructuralImportContract> importContracts;
        containers::DynamicArray<StructuralQueueGroupKey> queueGroups;
        containers::DynamicArray<LogicalAllocationRecord> allocations;
        containers::DynamicArray<StructuralPacketTemplate> packets;
        containers::DynamicArray<CompiledCommandScope> commandScopes;
        containers::DynamicArray<CompiledQueueDependency> queueDependencies;
        containers::DynamicArray<u32> crossQueueProducerScopes;
        containers::DynamicArray<u32> firstTouchAllocationOrder;
        containers::DynamicArray<PendingExportRecord> pendingExports;
        containers::DynamicArray<ResourceContractPatchId> exportPatches;
        // Global dense semantic extent authored by the cold compiler. Warm
        // frames deliberately have no SealedPlanningSummary/writers, but the
        // cached allocation lifetimes and reuse buckets still use these
        // ordinals and therefore require the original exact bound.
        u32 semanticEventCount = 0;
        bool valid = false;
    };

    struct ResourceContractUseTemplate
    {
        ResourceContractUseTemplate() noexcept : name(memory::pools::Rendering::GetInstance()) {}
        ResourceContractUseTemplate(ResourceContractUseTemplate&&) noexcept = default;
        ResourceContractUseTemplate& operator=(ResourceContractUseTemplate&&) noexcept = default;
        ResourceContractUseTemplate(const ResourceContractUseTemplate&) = delete;
        ResourceContractUseTemplate& operator=(const ResourceContractUseTemplate&) = delete;

        FlowSpaceId flowSpace;
        containers::String name;
        u32 lookupHash = 0;
        GpuFlowGroupId flowGroup;
        u32 ordinal = InvalidRenderFlowResourceIndex;
        FrameResourceKind kind = FrameResourceKind::Invalid;
        u32 nextDuplicate = InvalidRenderFlowResourceIndex;
        u32 resolvedIndex = InvalidRenderFlowResourceIndex;
    };

    struct ResourceContractDecisionTemplate
    {
        GpuFlowGroupId flowGroup;
        u32 ordinal = InvalidRenderFlowResourceIndex;
    };

    struct ResourceContractRangeTemplate
    {
        u32 firstUse = 0;
        u32 useCount = 0;
        u32 firstDecision = 0;
        u32 decisionCount = 0;
        u32 lookupBucketOffset = 0;
        u32 lookupBucketCount = 0;
        u32 lookupGroupOffset = 0;
        u32 lookupGroupCount = 0;
    };

    struct ResourceContractLookupGroupTemplate
    {
        u32 lookupHash = 0;
        u32 firstUse = InvalidRenderFlowResourceIndex;
        u32 lastUse = InvalidRenderFlowResourceIndex;
        u32 nextBucketGroup = InvalidRenderFlowResourceIndex;
    };

    struct ResourceContractNodeTemplate
    {
        ResourceContractNodeTemplate() noexcept
            : uses(memory::pools::Rendering::GetInstance()), decisions(memory::pools::Rendering::GetInstance()),
              ranges(memory::pools::Rendering::GetInstance()), lookupBuckets(memory::pools::Rendering::GetInstance()),
              lookupGroups(memory::pools::Rendering::GetInstance())
        {
        }

        ResourceContractNodeTemplate(ResourceContractNodeTemplate&&) noexcept = default;
        ResourceContractNodeTemplate& operator=(ResourceContractNodeTemplate&&) noexcept = default;
        ResourceContractNodeTemplate(const ResourceContractNodeTemplate&) = delete;
        ResourceContractNodeTemplate& operator=(const ResourceContractNodeTemplate&) = delete;

        RenderFlowNodeId node;
        containers::DynamicArray<ResourceContractUseTemplate> uses;
        containers::DynamicArray<ResourceContractDecisionTemplate> decisions;
        containers::DynamicArray<ResourceContractRangeTemplate> ranges;
        containers::DynamicArray<u32> lookupBuckets;
        containers::DynamicArray<ResourceContractLookupGroupTemplate> lookupGroups;
        u32 textureBindingCount = 0;
        u32 bufferBindingCount = 0;
    };

    struct ResourceContractStorage
    {
        ResourceContractStorage() noexcept
            : nodes(memory::pools::Rendering::GetInstance()), candidateNodes(memory::pools::Rendering::GetInstance())
        {
        }

        PersistentStructuralPlan plan;
        containers::DynamicArray<ResourceContractNodeTemplate> nodes;
        containers::DynamicArray<ResourceContractNodeTemplate> candidateNodes;
        u64 contractIdentity = 0;
        u64 rhiDeviceIncarnation = 0;
        u64 descriptorDomainIdentity = 0;
        u64 publicationSerial = 0;
        bool candidateNodesReady = false;
        bool valid = false;
    };

    struct WholeFrameReuseCandidate
    {
        u64 capacity = 0;
        u32 lane = InvalidRenderFlowResourceIndex;
        u32 epoch = 0;
        u32 producerScope = InvalidRenderFlowResourceIndex;
    };

    struct WholeFrameReadyIndex
    {
        struct Node
        {
            WholeFrameReuseCandidate candidate;
            u32 left = InvalidRenderFlowResourceIndex;
            u32 right = InvalidRenderFlowResourceIndex;
            u32 parent = InvalidRenderFlowResourceIndex;
            u16 height = 1;
        };

        WholeFrameReadyIndex() noexcept : nodes(memory::pools::Rendering::GetInstance()) {}

        WholeFrameReadyIndex(WholeFrameReadyIndex&&) noexcept = default;
        WholeFrameReadyIndex& operator=(WholeFrameReadyIndex&&) noexcept = default;
        WholeFrameReadyIndex(const WholeFrameReadyIndex&) = delete;
        WholeFrameReadyIndex& operator=(const WholeFrameReadyIndex&) = delete;

        void Reset() noexcept
        {
            root = InvalidRenderFlowResourceIndex;
            nodeCount = 0;
        }

        void TrimAfterUse() noexcept
        {
            constexpr u32 minimumRetainedCount = 32;
            const u32 retainedFloor = nodeCount > minimumRetainedCount ? nodeCount : minimumRetainedCount;
            if (static_cast<u64>(nodes.Capacity()) > static_cast<u64>(retainedFloor) * 4u)
            {
                nodes.Resize(nodeCount);
                nodes.Shrink();
            }
        }

        void ReleaseDormantBacking() noexcept
        {
            Reset();
            nodes.Clear();
            nodes.Shrink();
        }

        [[nodiscard]] bool Empty() const noexcept { return root == InvalidRenderFlowResourceIndex; }

        [[nodiscard]] static bool CandidateLess(const WholeFrameReuseCandidate& left, const WholeFrameReuseCandidate& right) noexcept
        {
            if (left.capacity != right.capacity)
                return left.capacity < right.capacity;
            if (left.lane != right.lane)
                return left.lane < right.lane;
            return left.epoch < right.epoch;
        }

        [[nodiscard]] u16 Height(const u32 index) const noexcept
        {
            return index == InvalidRenderFlowResourceIndex ? 0u : nodes[index].height;
        }

        void UpdateHeight(const u32 index) noexcept
        {
            if (index == InvalidRenderFlowResourceIndex)
                return;
            Node& node = nodes[index];
            const u16 left = Height(node.left);
            const u16 right = Height(node.right);
            node.height = static_cast<u16>((left > right ? left : right) + 1u);
        }

        void ReplaceParentLink(const u32 parent, const u32 oldChild, const u32 newChild) noexcept
        {
            if (parent == InvalidRenderFlowResourceIndex)
                root = newChild;
            else if (nodes[parent].left == oldChild)
                nodes[parent].left = newChild;
            else
            {
                VG_ASSERT(nodes[parent].right == oldChild);
                nodes[parent].right = newChild;
            }
            if (newChild != InvalidRenderFlowResourceIndex)
                nodes[newChild].parent = parent;
        }

        u32 RotateLeft(const u32 subtreeRoot) noexcept
        {
            Node& rootNode = nodes[subtreeRoot];
            const u32 pivot = rootNode.right;
            VG_ASSERT(pivot != InvalidRenderFlowResourceIndex);
            Node& pivotNode = nodes[pivot];
            const u32 parent = rootNode.parent;
            const u32 middle = pivotNode.left;
            ReplaceParentLink(parent, subtreeRoot, pivot);
            pivotNode.left = subtreeRoot;
            rootNode.parent = pivot;
            rootNode.right = middle;
            if (middle != InvalidRenderFlowResourceIndex)
                nodes[middle].parent = subtreeRoot;
            UpdateHeight(subtreeRoot);
            UpdateHeight(pivot);
            return pivot;
        }

        u32 RotateRight(const u32 subtreeRoot) noexcept
        {
            Node& rootNode = nodes[subtreeRoot];
            const u32 pivot = rootNode.left;
            VG_ASSERT(pivot != InvalidRenderFlowResourceIndex);
            Node& pivotNode = nodes[pivot];
            const u32 parent = rootNode.parent;
            const u32 middle = pivotNode.right;
            ReplaceParentLink(parent, subtreeRoot, pivot);
            pivotNode.right = subtreeRoot;
            rootNode.parent = pivot;
            rootNode.left = middle;
            if (middle != InvalidRenderFlowResourceIndex)
                nodes[middle].parent = subtreeRoot;
            UpdateHeight(subtreeRoot);
            UpdateHeight(pivot);
            return pivot;
        }

        void RebalanceFrom(u32 index) noexcept
        {
            while (index != InvalidRenderFlowResourceIndex)
            {
                UpdateHeight(index);
                const i32 balance = static_cast<i32>(Height(nodes[index].left)) - static_cast<i32>(Height(nodes[index].right));
                u32 balancedRoot = index;
                if (balance > 1)
                {
                    const u32 left = nodes[index].left;
                    if (Height(nodes[left].left) < Height(nodes[left].right))
                        static_cast<void>(RotateLeft(left));
                    balancedRoot = RotateRight(index);
                }
                else if (balance < -1)
                {
                    const u32 right = nodes[index].right;
                    if (Height(nodes[right].right) < Height(nodes[right].left))
                        static_cast<void>(RotateRight(right));
                    balancedRoot = RotateLeft(index);
                }
                index = nodes[balancedRoot].parent;
            }
        }

        u32 Insert(const WholeFrameReuseCandidate& candidate) noexcept
        {
            const u32 index = nodeCount++;
            if (index == nodes.Size())
                nodes.EmplaceBack();
            else
                nodes[index] = {};
            Node& inserted = nodes[index];
            inserted.candidate = candidate;
            if (root == InvalidRenderFlowResourceIndex)
            {
                root = index;
                return index;
            }

            u32 current = root;
            for (;;)
            {
                if (CandidateLess(candidate, nodes[current].candidate))
                {
                    if (nodes[current].left == InvalidRenderFlowResourceIndex)
                    {
                        nodes[current].left = index;
                        inserted.parent = current;
                        RebalanceFrom(current);
                        return index;
                    }
                    current = nodes[current].left;
                }
                else
                {
                    if (nodes[current].right == InvalidRenderFlowResourceIndex)
                    {
                        nodes[current].right = index;
                        inserted.parent = current;
                        RebalanceFrom(current);
                        return index;
                    }
                    current = nodes[current].right;
                }
            }
        }

        void Remove(const u32 index) noexcept
        {
            if (index == InvalidRenderFlowResourceIndex || index >= nodeCount)
                return;
            Node& removed = nodes[index];
            while (removed.left != InvalidRenderFlowResourceIndex && removed.right != InvalidRenderFlowResourceIndex)
            {
                if (Height(removed.left) > Height(removed.right))
                    static_cast<void>(RotateRight(index));
                else
                    static_cast<void>(RotateLeft(index));
            }
            const u32 parent = removed.parent;
            const u32 child = removed.left != InvalidRenderFlowResourceIndex ? removed.left : removed.right;
            ReplaceParentLink(parent, index, child);
            removed.left = removed.right = removed.parent = InvalidRenderFlowResourceIndex;
            removed.height = 1;
            RebalanceFrom(parent);
        }

        [[nodiscard]] u32 LowerBoundCapacity(const u64 capacity) const noexcept
        {
            u32 current = root;
            u32 best = InvalidRenderFlowResourceIndex;
            while (current != InvalidRenderFlowResourceIndex)
            {
                if (nodes[current].candidate.capacity >= capacity)
                {
                    best = current;
                    current = nodes[current].left;
                }
                else
                    current = nodes[current].right;
            }
            return best;
        }

        [[nodiscard]] const WholeFrameReuseCandidate& Candidate(const u32 index) const noexcept { return nodes[index].candidate; }

        containers::DynamicArray<Node> nodes;
        u32 root = InvalidRenderFlowResourceIndex;
        u32 nodeCount = 0;
    };

    struct WholeFrameReleaseEvent
    {
        u32 next = InvalidRenderFlowResourceIndex;
        u32 lane = InvalidRenderFlowResourceIndex;
        u32 epoch = 0;
    };

    struct WholeFrameReuseClass
    {
        WholeFrameReuseClass() noexcept
            : graphicsToComputePending(memory::pools::Rendering::GetInstance()), computeToGraphicsPending(memory::pools::Rendering::GetInstance())
        {
        }

        WholeFrameReuseClass(WholeFrameReuseClass&&) noexcept = default;
        WholeFrameReuseClass& operator=(WholeFrameReuseClass&&) noexcept = default;
        WholeFrameReuseClass(const WholeFrameReuseClass&) = delete;
        WholeFrameReuseClass& operator=(const WholeFrameReuseClass&) = delete;

        void Reset() noexcept
        {
            compatibility = {};
            nextCollision = InvalidRenderFlowResourceIndex;
            graphicsReady.Reset();
            computeReady.Reset();
            copyReady.Reset();
            graphicsToComputePending.Clear();
            computeToGraphicsPending.Clear();
            graphicsToComputePendingCursor = 0;
            computeToGraphicsPendingCursor = 0;
            graphicsToComputeReady.Reset();
            computeToGraphicsReady.Reset();
        }

        void TrimAfterUse() noexcept
        {
            graphicsReady.TrimAfterUse();
            computeReady.TrimAfterUse();
            copyReady.TrimAfterUse();
            graphicsToComputeReady.TrimAfterUse();
            computeToGraphicsReady.TrimAfterUse();
            constexpr u32 minimumRetainedCount = 32;
            const u32 graphicsPendingFloor = graphicsToComputePending.Size() > minimumRetainedCount ? graphicsToComputePending.Size() : minimumRetainedCount;
            if (static_cast<u64>(graphicsToComputePending.Capacity()) > static_cast<u64>(graphicsPendingFloor) * 4u)
                graphicsToComputePending.Shrink();
            const u32 computePendingFloor = computeToGraphicsPending.Size() > minimumRetainedCount ? computeToGraphicsPending.Size() : minimumRetainedCount;
            if (static_cast<u64>(computeToGraphicsPending.Capacity()) > static_cast<u64>(computePendingFloor) * 4u)
                computeToGraphicsPending.Shrink();
        }

        void ReleaseDormantBacking() noexcept
        {
            Reset();
            graphicsReady.ReleaseDormantBacking();
            computeReady.ReleaseDormantBacking();
            copyReady.ReleaseDormantBacking();
            graphicsToComputeReady.ReleaseDormantBacking();
            computeToGraphicsReady.ReleaseDormantBacking();
            graphicsToComputePending.Shrink();
            computeToGraphicsPending.Shrink();
        }

        DedicatedResourceClassKey compatibility;
        u32 nextCollision = InvalidRenderFlowResourceIndex;
        WholeFrameReadyIndex graphicsReady;
        WholeFrameReadyIndex computeReady;
        WholeFrameReadyIndex copyReady;
        containers::DynamicArray<WholeFrameReuseCandidate> graphicsToComputePending;
        u32 graphicsToComputePendingCursor = 0;
        WholeFrameReadyIndex graphicsToComputeReady;
        containers::DynamicArray<WholeFrameReuseCandidate> computeToGraphicsPending;
        u32 computeToGraphicsPendingCursor = 0;
        WholeFrameReadyIndex computeToGraphicsReady;
    };

    struct PhysicalScopeState
    {
        void Reset() noexcept
        {
            cellOffset = 0;
            cellCount = 0;
            initial = rhi::ResourceState::Unknown;
            terminal = rhi::ResourceState::Unknown;
            lastPacket = InvalidRenderFlowResourceIndex;
            trackedPacket = InvalidRenderFlowResourceIndex;
        }

        u32 cellOffset = 0;
        u32 cellCount = 0;
        rhi::ResourceState initial = rhi::ResourceState::Unknown;
        rhi::ResourceState terminal = rhi::ResourceState::Unknown;
        u32 lastPacket = InvalidRenderFlowResourceIndex;
        u32 trackedPacket = InvalidRenderFlowResourceIndex;
    };

    [[nodiscard]] inline u32 ResolveScratchHash(const u32 value) noexcept
    {
        u32 hash = value;
        hash ^= hash >> 16;
        hash *= 0x7feb352du;
        hash ^= hash >> 15;
        hash *= 0x846ca68bu;
        hash ^= hash >> 16;
        return hash;
    }

    [[nodiscard]] inline u32 ResolveScratchHash(const u64 value) noexcept
    {
        return ResolveScratchHash(static_cast<u32>(value) ^ static_cast<u32>(value >> 32));
    }

    struct PresentationImportKey
    {
        u64 swapChain = 0;
        u64 serial = 0;

        [[nodiscard]] friend constexpr bool operator==(const PresentationImportKey&, const PresentationImportKey&) noexcept = default;
    };

    [[nodiscard]] inline u32 ResolveScratchHash(const PresentationImportKey& value) noexcept
    {
        const u32 first = ResolveScratchHash(value.swapChain);
        const u32 second = ResolveScratchHash(value.serial);
        return ResolveScratchHash(first ^ ((second << 13u) | (second >> 19u)));
    }

    struct TextureTransitionRunKey
    {
        u32 logicalAllocation = InvalidRenderFlowResourceIndex;
        u32 physicalIndex = InvalidRenderFlowResourceIndex;
        u32 physicalGeneration = 0;
        u32 before = 0;
        u32 after = 0;
        u16 firstMip = 0;
        u16 mipCount = 0;

        [[nodiscard]] friend constexpr bool operator==(const TextureTransitionRunKey&, const TextureTransitionRunKey&) noexcept = default;
    };

    [[nodiscard]] inline u32 ResolveScratchHash(const TextureTransitionRunKey& value) noexcept
    {
        u32 hash = ResolveScratchHash(value.logicalAllocation ^ (value.physicalIndex * 0x9e3779b9u));
        hash = ResolveScratchHash(hash ^ value.physicalGeneration);
        hash = ResolveScratchHash(hash ^ value.before ^ (value.after * 0x85ebca6bu));
        return ResolveScratchHash(hash ^ static_cast<u32>(value.firstMip) ^ (static_cast<u32>(value.mipCount) << 16u));
    }

    template <typename Key, typename Value> struct ResolveScratchHashMap
    {
        struct Entry
        {
            Key key{};
            Value value{};
            u32 hash = 0;
        };

        ResolveScratchHashMap() noexcept : entries(memory::pools::Rendering::GetInstance()), stamps(memory::pools::Rendering::GetInstance()) {}

        void Reset() noexcept
        {
            size = 0;
            ++generation;
            if (generation != 0)
                return;
            for (u32& stamp : stamps)
                stamp = 0;
            generation = 1;
        }

        [[nodiscard]] u32 Size() const noexcept { return size; }
        [[nodiscard]] u32 Capacity() const noexcept { return entries.Size(); }

        void Reserve(const u32 expectedSize) noexcept
        {
            if (expectedSize == 0)
                return;
            u64 required = static_cast<u64>(expectedSize) * 2u;
            u32 capacity = 8;
            while (capacity < required && capacity <= 0x40000000u)
                capacity <<= 1u;
            if (capacity < required)
                capacity = 0x80000000u;
            if (capacity > entries.Size())
                Rehash(capacity);
        }

        [[nodiscard]] bool Find(const Key& key, Value& value) const noexcept
        {
            return FindPrehashed(key, ResolveScratchHash(key), value);
        }

        [[nodiscard]] bool FindPrehashed(const Key& key, const u32 hash, Value& value) const noexcept
        {
            const u32 capacity = entries.Size();
            if (capacity == 0)
                return false;
            u32 index = hash & (capacity - 1u);
            for (u32 probe = 0; probe < capacity; ++probe)
            {
                if (stamps[index] != generation)
                    return false;
                const Entry& entry = entries[index];
                if (entry.hash == hash && entry.key == key)
                {
                    value = entry.value;
                    return true;
                }
                index = (index + 1u) & (capacity - 1u);
            }
            return false;
        }

        [[nodiscard]] bool Insert(const Key& key, const Value& value) noexcept
        {
            return InsertPrehashed(key, ResolveScratchHash(key), value);
        }

        [[nodiscard]] bool InsertPrehashed(const Key& key, const u32 hash, const Value& value) noexcept
        {
            if (entries.Empty() || static_cast<u64>(size + 1u) * 2u > entries.Size())
                Reserve(size + 1u);
            const u32 capacity = entries.Size();
            u32 index = hash & (capacity - 1u);
            for (u32 probe = 0; probe < capacity; ++probe)
            {
                if (stamps[index] != generation)
                {
                    entries[index] = {key, value, hash};
                    stamps[index] = generation;
                    ++size;
                    return true;
                }
                const Entry& entry = entries[index];
                if (entry.hash == hash && entry.key == key)
                    return false;
                index = (index + 1u) & (capacity - 1u);
            }
            return false;
        }

        [[nodiscard]] bool Set(const Key& key, const Value& value) noexcept
        {
            const u32 hash = ResolveScratchHash(key);
            if (entries.Empty() || static_cast<u64>(size + 1u) * 2u > entries.Size())
                Reserve(size + 1u);
            const u32 capacity = entries.Size();
            u32 index = hash & (capacity - 1u);
            for (u32 probe = 0; probe < capacity; ++probe)
            {
                if (stamps[index] != generation)
                {
                    entries[index] = {key, value, hash};
                    stamps[index] = generation;
                    ++size;
                    return true;
                }
                Entry& entry = entries[index];
                if (entry.hash == hash && entry.key == key)
                {
                    entry.value = value;
                    return true;
                }
                index = (index + 1u) & (capacity - 1u);
            }
            return false;
        }

        [[nodiscard]] bool Erase(const Key& key) noexcept
        {
            const u32 capacity = entries.Size();
            if (capacity == 0)
                return false;
            const u32 hash = ResolveScratchHash(key);
            u32 index = hash & (capacity - 1u);
            for (u32 probe = 0; probe < capacity; ++probe)
            {
                if (stamps[index] != generation)
                    return false;
                const Entry& entry = entries[index];
                if (entry.hash == hash && entry.key == key)
                {
                    stamps[index] = 0;
                    --size;
                    // Reinsert the remaining probe cluster so removing this
                    // bucket cannot make a later key unreachable.
                    u32 next = (index + 1u) & (capacity - 1u);
                    while (stamps[next] == generation)
                    {
                        const Entry moved = entries[next];
                        stamps[next] = 0;
                        --size;
                        const bool inserted = InsertPrehashed(moved.key, moved.hash, moved.value);
                        VG_ASSERT(inserted);
                        next = (next + 1u) & (capacity - 1u);
                    }
                    return true;
                }
                index = (index + 1u) & (capacity - 1u);
            }
            return false;
        }

    private:
        void Rehash(const u32 capacity) noexcept
        {
            containers::DynamicArray<Entry> replacementEntries{memory::pools::Rendering::GetInstance()};
            containers::DynamicArray<u32> replacementStamps{memory::pools::Rendering::GetInstance()};
            replacementEntries.Resize(capacity);
            replacementStamps.Resize(capacity, 0u);
            const u32 oldGeneration = generation;
            for (u32 oldIndex = 0; oldIndex < entries.Size(); ++oldIndex)
            {
                if (stamps[oldIndex] != oldGeneration)
                    continue;
                const Entry& source = entries[oldIndex];
                u32 index = source.hash & (capacity - 1u);
                while (replacementStamps[index] == oldGeneration)
                    index = (index + 1u) & (capacity - 1u);
                replacementEntries[index] = source;
                replacementStamps[index] = oldGeneration;
            }
            entries = static_cast<containers::DynamicArray<Entry>&&>(replacementEntries);
            stamps = static_cast<containers::DynamicArray<u32>&&>(replacementStamps);
        }

        containers::DynamicArray<Entry> entries;
        containers::DynamicArray<u32> stamps;
        u32 generation = 1;
        u32 size = 0;
    };

    struct ResolveScratchHashSet
    {
        ResolveScratchHashSet() noexcept : keys(memory::pools::Rendering::GetInstance()), stamps(memory::pools::Rendering::GetInstance()) {}

        void Reset() noexcept
        {
            size = 0;
            ++generation;
            if (generation != 0)
                return;
            for (u32& stamp : stamps)
                stamp = 0;
            generation = 1;
        }

        void Reserve(const u32 expectedSize) noexcept
        {
            if (expectedSize == 0)
                return;
            u64 required = static_cast<u64>(expectedSize) * 2u;
            u32 capacity = 8;
            while (capacity < required && capacity <= 0x40000000u)
                capacity <<= 1u;
            if (capacity < required)
                capacity = 0x80000000u;
            if (capacity > keys.Size())
                Rehash(capacity);
        }

        [[nodiscard]] bool Contains(const u32 value) const noexcept
        {
            const u32 capacity = keys.Size();
            if (capacity == 0)
                return false;
            u32 index = ResolveScratchHash(value) & (capacity - 1u);
            for (u32 probe = 0; probe < capacity; ++probe)
            {
                if (stamps[index] != generation)
                    return false;
                if (keys[index] == value)
                    return true;
                index = (index + 1u) & (capacity - 1u);
            }
            return false;
        }

        [[nodiscard]] bool Insert(const u32 value) noexcept
        {
            if (keys.Empty() || static_cast<u64>(size + 1u) * 2u > keys.Size())
                Reserve(size + 1u);
            const u32 capacity = keys.Size();
            u32 index = ResolveScratchHash(value) & (capacity - 1u);
            for (u32 probe = 0; probe < capacity; ++probe)
            {
                if (stamps[index] != generation)
                {
                    keys[index] = value;
                    stamps[index] = generation;
                    ++size;
                    return true;
                }
                if (keys[index] == value)
                    return false;
                index = (index + 1u) & (capacity - 1u);
            }
            return false;
        }

    private:
        void Rehash(const u32 capacity) noexcept
        {
            containers::DynamicArray<u32> replacementKeys{memory::pools::Rendering::GetInstance()};
            containers::DynamicArray<u32> replacementStamps{memory::pools::Rendering::GetInstance()};
            replacementKeys.Resize(capacity);
            replacementStamps.Resize(capacity, 0u);
            const u32 oldGeneration = generation;
            for (u32 oldIndex = 0; oldIndex < keys.Size(); ++oldIndex)
            {
                if (stamps[oldIndex] != oldGeneration)
                    continue;
                const u32 value = keys[oldIndex];
                u32 index = ResolveScratchHash(value) & (capacity - 1u);
                while (replacementStamps[index] == oldGeneration)
                    index = (index + 1u) & (capacity - 1u);
                replacementKeys[index] = value;
                replacementStamps[index] = oldGeneration;
            }
            keys = static_cast<containers::DynamicArray<u32>&&>(replacementKeys);
            stamps = static_cast<containers::DynamicArray<u32>&&>(replacementStamps);
        }

        containers::DynamicArray<u32> keys;
        containers::DynamicArray<u32> stamps;
        u32 generation = 1;
        u32 size = 0;
    };

    struct ResolveScratch
    {
        // Single-resolver allocator-owned working memory. No published
        // execution generation may retain pointers or views into this storage.
        ResolveScratch() noexcept
            : batchResourceSlots(memory::pools::Rendering::GetInstance()), batchTextureViews(memory::pools::Rendering::GetInstance()), batchBufferViews(memory::pools::Rendering::GetInstance()),
              terminalDependencyByConsumer(memory::pools::Rendering::GetInstance()),
              slots(memory::pools::Rendering::GetInstance()), slotFastPath(memory::pools::Rendering::GetInstance()),
              uses(memory::pools::Rendering::GetInstance()), scopes(memory::pools::Rendering::GetInstance()), namedScopes(memory::pools::Rendering::GetInstance()),
              allocationStates(memory::pools::Rendering::GetInstance()), textureSubresourceStates(memory::pools::Rendering::GetInstance()), crossQueueProducerScopes(memory::pools::Rendering::GetInstance()),
              terminalDependencyProducerScopes(memory::pools::Rendering::GetInstance()),
              importedInstalled(memory::pools::Rendering::GetInstance()), physicalAssignmentOrder(memory::pools::Rendering::GetInstance()),
              persistentAllocationLanes(memory::pools::Rendering::GetInstance()), persistentLanePhysical(memory::pools::Rendering::GetInstance()),
              persistentLaneAdoptionEntries(memory::pools::Rendering::GetInstance()), releasedPersistentLeases(memory::pools::Rendering::GetInstance()),
              structuralScopeClosed(memory::pools::Rendering::GetInstance()), structuralImportIndices(memory::pools::Rendering::GetInstance()),
              structuralExportSlots(memory::pools::Rendering::GetInstance()), structuralDecisionValues(memory::pools::Rendering::GetInstance()),
              wholeFrameLanes(memory::pools::Rendering::GetInstance()), wholeFrameReleaseEvents(memory::pools::Rendering::GetInstance()), wholeFrameReleaseHeads(memory::pools::Rendering::GetInstance()),
              wholeFrameReuseClasses(memory::pools::Rendering::GetInstance()), placedRequests(memory::pools::Rendering::GetInstance()), placedObjects(memory::pools::Rendering::GetInstance()),
              physicalHistory(memory::pools::Rendering::GetInstance()), physicalStateCells(memory::pools::Rendering::GetInstance()), physicalGlobalState(memory::pools::Rendering::GetInstance()),
              physicalAccessCells(memory::pools::Rendering::GetInstance()),
              packetPhysicalIndices(memory::pools::Rendering::GetInstance()),
              cachedOldToCurrentPhysical(memory::pools::Rendering::GetInstance()),
              cachedCurrentToOldPhysical(memory::pools::Rendering::GetInstance()),
              cachedBindingSort(memory::pools::Rendering::GetInstance()),
              cachedBindingSlotRemap(memory::pools::Rendering::GetInstance())
        {
        }

        void ResetFrame() noexcept
        {
            batchResourceSlots.Clear();
            batchTextureViews.Clear();
            batchBufferViews.Clear();
            terminalDependencyByConsumer.Clear();
            slotCount = 0;
            slotFastPath.Clear();
            uses.Clear();
            scopes.Clear();
            namedScopes.Clear();
            allocationStates.Clear();
            textureSubresourceStates.Clear();
            namedSlots.Reset();
            identityIds.Reset();
            retainedImportTokens.Reset();
            retainedImportResources.Reset();
            retainedImportContractPatches.Reset();
            presentationImports.Reset();
            textureTransitionRuns.Reset();
            crossQueueProducerScopes.Clear();
            terminalDependencyProducerScopes.Clear();
            openScopeIndices.Reset();
            importedInstalled.Clear();
            physicalAssignmentOrder.Clear();
            persistentAllocationLanes.Clear();
            persistentLanePhysical.Clear();
            persistentLaneAdoptionEntries.Clear();
            releasedPersistentLeases.Clear();
            structuralScopeOrdinals.Reset();
            structuralScopeClosed.Clear();
            structuralImportIndices.Clear();
            structuralExportSlots.Clear();
            structuralDecisionValues.Clear();
            candidateStructuralPlan.Clear();
            candidateDedicatedPlan.Clear();
            wholeFrameLanes.Clear();
            wholeFrameReleaseEvents.Clear();
            wholeFrameReleaseHeads.Clear();
            wholeFrameReuseHeads.Reset();
            const u32 activeReuseClassCount = wholeFrameReuseClassCount;
            for (u32 index = 0; index < activeReuseClassCount; ++index)
            {
                wholeFrameReuseClasses[index].TrimAfterUse();
                wholeFrameReuseClasses[index].Reset();
            }
            constexpr u32 dormantReuseClassTrimBudget = 2;
            for (u32 pass = 0; pass < dormantReuseClassTrimBudget && activeReuseClassCount < wholeFrameReuseClasses.Size(); ++pass)
            {
                if (wholeFrameReuseDormantTrimCursor < activeReuseClassCount || wholeFrameReuseDormantTrimCursor >= wholeFrameReuseClasses.Size())
                    wholeFrameReuseDormantTrimCursor = activeReuseClassCount;
                wholeFrameReuseClasses[wholeFrameReuseDormantTrimCursor].ReleaseDormantBacking();
                if (++wholeFrameReuseDormantTrimCursor >= wholeFrameReuseClasses.Size())
                    wholeFrameReuseDormantTrimCursor = activeReuseClassCount;
            }
            constexpr u32 minimumRetainedReuseClasses = 16;
            const u32 retainedReuseClassFloor = activeReuseClassCount > minimumRetainedReuseClasses ? activeReuseClassCount : minimumRetainedReuseClasses;
            if (static_cast<u64>(wholeFrameReuseClasses.Size()) > static_cast<u64>(retainedReuseClassFloor) * 4u)
            {
                wholeFrameReuseClasses.Resize(activeReuseClassCount);
                wholeFrameReuseClasses.Shrink();
                wholeFrameReuseDormantTrimCursor = activeReuseClassCount;
            }
            wholeFrameReuseClassCount = 0;
            placedRequests.Clear();
            placedObjects.Clear();
            placedPlan.Clear();
            placedPlannerScratch.Reset();
            physicalHistoryCount = 0;
            physicalStateCells.Clear();
            physicalGlobalState.Clear();
            physicalAccessCells.Clear();
            packetPhysicalIndices.Clear();
            cachedOldToCurrentPhysical.Clear();
            cachedCurrentToOldPhysical.Clear();
            cachedBindingSort.Clear();
            cachedBindingSlotRemap.Clear();
        }

        void ResetPhysicalHistory(const u32 count) noexcept
        {
            if (physicalHistory.Size() < count)
                physicalHistory.Resize(count);
            for (u32 index = 0; index < count; ++index)
                physicalHistory[index].Reset();
            physicalHistoryCount = count;
        }

        containers::DynamicArray<u32> batchResourceSlots;
        containers::DynamicArray<ResolvedTextureViewRecord> batchTextureViews;
        containers::DynamicArray<ResolvedBufferViewRecord> batchBufferViews;
        // Finish-only dense command-scope -> dependency scratch. This is not
        // ResourceUseId metadata and is never sized from the planning tape.
        containers::DynamicArray<u32> terminalDependencyByConsumer;
        containers::DynamicArray<LogicalSlot> slots;
        // Global logical-slot classification after named-resource coalescing.
        // 1 means the slot has a stable direct mapping and canonical/non-nested
        // named scopes; 0 routes every mapping-sensitive semantic through the
        // existing general interpreter.
        containers::DynamicArray<u8> slotFastPath;
        containers::DynamicArray<ActiveUseRecord> uses;
        containers::DynamicArray<OpenScopeRecord> scopes;
        containers::DynamicArray<NamedScopeState> namedScopes;
        containers::DynamicArray<AllocationLogicalState> allocationStates;
        containers::DynamicArray<TextureSubresourceLogicalState> textureSubresourceStates;
        ResolveScratchHashMap<LogicalNameKey, u32> namedSlots;
        // Reused serially: SealPlanning uses this set for node uniqueness, then
        // ResetFrame() clears it before Resolve optionally materializes the
        // noncanonical command-scope uniqueness fallback.
        ResolveScratchHashSet identityIds;
        // Session-only sparse import identities. Registration owns these before
        // planning writers exist; Resolve resets them once their job is done.
        ResolveScratchHashMap<u64, u32> retainedImportTokens;
        ResolveScratchHashMap<u64, u32> retainedImportResources;
        ResolveScratchHashMap<u64, u32> retainedImportContractPatches;
        ResolveScratchHashMap<PresentationImportKey, u32> presentationImports;
        ResolveScratchHashMap<TextureTransitionRunKey, u32> textureTransitionRuns;
        // Lazily materialized only after the first real Fork/Join. Once active,
        // each dense consumer scope stores the greatest dense producer scope
        // admitted by the boundary type that consumer queue can require:
        // Graphics carries Join, Compute carries Fork. Earlier prefix entries
        // are Invalid because no cross-queue boundary existed yet.
        containers::DynamicArray<u32> crossQueueProducerScopes;
        // Finish-only dense dependency metadata. Each compiled dependency's
        // producer scope index is resolved once from the public stable ID and
        // reused for lowering validation and submitted-order watermarks.
        containers::DynamicArray<u32> terminalDependencyProducerScopes;
        ResolveScratchHashMap<u64, u32> openScopeIndices;
        containers::DynamicArray<u8> importedInstalled;
        // Resolve scratch reused serially: reserved export-slot map prefix +
        // first-touch physical-assignment suffix, dense physical->export for
        // terminal lowering, then physical->packet-binding slot compilation.
        containers::DynamicArray<u32> physicalAssignmentOrder;
        containers::DynamicArray<u32> persistentAllocationLanes;
        containers::DynamicArray<u32> persistentLanePhysical;
        containers::DynamicArray<u32> persistentLaneAdoptionEntries;
        containers::DynamicArray<DedicatedResourceLease> releasedPersistentLeases;
        ResolveScratchHashMap<u64, u32> structuralScopeOrdinals;
        containers::DynamicArray<u8> structuralScopeClosed;
        containers::DynamicArray<u32> structuralImportIndices;
        containers::DynamicArray<u32> structuralExportSlots;
        containers::DynamicArray<u8> structuralDecisionValues;
        PersistentStructuralPlan candidateStructuralPlan;
        PersistentDedicatedPhysicalPlan candidateDedicatedPlan;
        containers::DynamicArray<WholeFrameLane> wholeFrameLanes;
        containers::DynamicArray<WholeFrameReleaseEvent> wholeFrameReleaseEvents;
        // semantic ordinal -> intrusive release-event list. The assignment walk
        // advances this array monotonically, replacing the old O(log R) heap.
        containers::DynamicArray<u32> wholeFrameReleaseHeads;
        ResolveScratchHashMap<u64, u32> wholeFrameReuseHeads;
        containers::DynamicArray<WholeFrameReuseClass> wholeFrameReuseClasses;
        containers::DynamicArray<PlacedRangeRequest> placedRequests;
        containers::DynamicArray<PlacedObjectRequest> placedObjects;
        PlacedRangePlan placedPlan;
        PlacedRangePlannerScratch placedPlannerScratch;
        containers::DynamicArray<PhysicalScopeState> physicalHistory;
        containers::DynamicArray<rhi::ResourceState> physicalStateCells;
        // Sparse global-proof classifier. Ordinary same-queue resources stay 0
        // and use only command-list-local native state tracking.
        containers::DynamicArray<u8> physicalGlobalState;
        containers::DynamicArray<u8> physicalAccessCells;
        containers::DynamicArray<u32> packetPhysicalIndices;
        containers::DynamicArray<u32> cachedOldToCurrentPhysical;
        containers::DynamicArray<u32> cachedCurrentToOldPhysical;
        containers::DynamicArray<PersistentStateBindingSort> cachedBindingSort;
        containers::DynamicArray<u32> cachedBindingSlotRemap;
        u32 slotCount = 0;
        u32 physicalHistoryCount = 0;
        u32 wholeFrameReuseClassCount = 0;
        u32 wholeFrameReuseDormantTrimCursor = 0;
    };

    [[nodiscard]] inline u32 NextRenderFlowSessionGeneration() noexcept
    {
        // Value-only session identities (imports/exports/uses/decisions) can
        // outlive one allocator implementation.  A process-wide sequence keeps
        // them stale across allocator shutdown/reinitialize and across distinct
        // allocators instead of restarting the ABA space at generation 1.
        static concurrency::Atomic<u32> generation{0};
        u32 next = generation.Increment();
        if (next == 0)
            next = generation.Increment();
        return next;
    }

    [[nodiscard]] inline u32 NextRenderFlowExecutionGeneration() noexcept
    {
        // Compiled execution ranges are plain value witnesses, so they require
        // the same cross-incarnation property independently from planning IDs.
        static concurrency::Atomic<u32> generation{0};
        u32 next = generation.Increment();
        if (next == 0)
            next = generation.Increment();
        return next;
    }

    [[nodiscard]] inline u64 NextRenderFlowResourceContractIdentity() noexcept
    {
        // Contract storage can be destroyed and later reused at the same address.
        // A process-wide identity keeps allocator-local RF13 physical programs
        // from accepting pointer ABA even when publication serials restart at 1.
        static concurrency::Atomic<u64> identity{0};
        u64 next = identity.Increment();
        if (next == 0)
            next = identity.Increment();
        return next;
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
    void ReleaseGenerationRuntimeOwners(ExecutionGenerationRef::Impl* generation) noexcept;
    [[nodiscard]] bool BorrowRetainedImportCanonicalDescriptors(RenderFlowResourceAllocator::Impl& allocator,
                                                                PhysicalBindingRecord& binding,
                                                                const FrameResourceDesc& desc,
                                                                CanonicalDescriptorPressure* pressure,
                                                                bool recordStats,
                                                                RenderFlowResourceFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool ReleaseRetainedImportDescriptorBorrows(RenderFlowResourceAllocator::Impl& allocator,
                                                              ExecutionGenerationRef::Impl& generation,
                                                              const rhi::ResidencyFenceSet& fences,
                                                              bool deviceLost,
                                                              RenderFlowResourceFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool PrepareRetainedImportDescriptorCacheShutdown(RenderFlowResourceAllocator::Impl& allocator,
                                                                   bool& deviceLost,
                                                                   RenderFlowResourceFailure* failure = nullptr) noexcept;
    void CommitRetainedImportDescriptorCacheShutdown(RenderFlowResourceAllocator::Impl& allocator) noexcept;
    void RetainedImportDescriptorCacheDeviceLost(RenderFlowResourceAllocator::Impl& allocator,
                                                 const rhi::ResidencyFenceSet& submittedFences = {}) noexcept;
    void RetainAllocator(RenderFlowResourceAllocator::Impl* allocator) noexcept;
    void ReleaseAllocator(RenderFlowResourceAllocator::Impl* allocator) noexcept;
    void AbandonAllocatorSession(RenderFlowResourceAllocator::Impl& allocator, u32 generation) noexcept;
    void ResetPlanningStorage(RenderFlowResourceAllocator::Impl& allocator, bool retainBoundedRecording = false) noexcept;
    void ReleasePersistentDedicatedPlan(RenderFlowResourceAllocator::Impl& allocator, bool releaseLeases = true) noexcept;
    [[nodiscard]] bool ResolveFrame(RenderFlowResourceAllocator::Impl& allocator, RenderFlowResourceFailure* failure) noexcept;
    [[nodiscard]] bool RelieveCanonicalDescriptorPressure(RenderFlowResourceAllocator::Impl& allocator, const CanonicalDescriptorPressure& pressure,
                                                          RenderFlowResourceFailure* failure) noexcept;
    [[nodiscard]] bool TrimCanonicalDescriptorCacheToTarget(RenderFlowResourceAllocator::Impl& allocator, RenderFlowResourceFailure* failure) noexcept;
    void CancelSession(RenderFlowResourceAllocator::Impl& allocator, u32 generation) noexcept;
} // namespace vanguard::rendering::detail

namespace vanguard::rendering
{
    struct ExecutionGenerationRef::Impl
    {
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
    };

    struct ResourcePlanningWriter::Impl
    {
        RenderFlowResourceAllocator::Impl* owner = nullptr;
        // Allocator-owned reservation storage is stable while this lease's
        // generation is current. Between sessions the reservation array may
        // contract or move; stale-handle paths must reject generation/phase
        // before dereferencing this pointer.
        detail::WriterReservation* reservation = nullptr;
        RenderFlowNodeId node;
        GpuFlowGroupId flowGroup;
        u32 sessionGeneration = 0;
        // The normal writer control block is permanently associated with one
        // flow-group slot. A stale old-generation handle keeps that slot leased,
        // forcing a rare heap fallback without contending with other groups.
        u32 primaryIndex = InvalidRenderFlowResourceIndex;
        u64 rejectedOperations = 0;
        RenderFlowResourceFailureCode firstFailureCode = RenderFlowResourceFailureCode::None;
        const char* firstFailureMessage = nullptr;
        bool failed = false;
        bool closed = true;
        // Accessed only through atomic_ref after this primary slot can escape.
        // Kept with the control block so parallel groups do not converge on a
        // densely packed global lease array during Close/Abandon.
        u32 primaryLease = 0;
    };

    struct RenderFlowResourceAllocator::Impl
    {
        explicit Impl(const RenderFlowResourceAllocatorConfig& allocatorConfig) noexcept
            : writerReservations(memory::pools::Rendering::GetInstance()), sealedWriters(memory::pools::Rendering::GetInstance()), sealedQueueGroups(memory::pools::Rendering::GetInstance()),
              queueRequestGroups(memory::pools::Rendering::GetInstance()),
              writerImpls(memory::pools::Rendering::GetInstance()),
              retainedImports(memory::pools::Rendering::GetInstance()), publishedExports(memory::pools::Rendering::GetInstance()),
              reservedExportContractPatches(memory::pools::Rendering::GetInstance()),
              persistentDedicatedPrograms(memory::pools::Rendering::GetInstance()),
              retainedImportDescriptorOwners(memory::pools::Rendering::GetInstance()), retainedImportDescriptorReusable(memory::pools::Rendering::GetInstance()),
              freeRetainedImportDescriptorOwners(memory::pools::Rendering::GetInstance()), config(allocatorConfig),
              resourceDescriptors(allocatorConfig.resourceDescriptors), descriptorLedger(allocatorConfig.descriptorCacheTarget, allocatorConfig.descriptorCacheMaximum),
              nativeByteLedger(allocatorConfig.hardNativeByteLimit), dedicatedPool(allocatorConfig, nativeByteLedger, descriptorLedger, resourceDescriptors.GetRef()),
              placedPool(allocatorConfig, nativeByteLedger, descriptorLedger, resourceDescriptors.GetRef())
        {
        }

        concurrency::Atomic<u32> references{1};
        mutable concurrency::SpinLock writerLock;
        concurrency::Atomic<u64> pendingRejectedOperations{0};
        containers::DynamicArray<detail::WriterReservation> writerReservations;
        containers::DynamicArray<detail::SealedWriterRecord> sealedWriters;
        containers::DynamicArray<u32> sealedQueueGroups;
        containers::DynamicArray<detail::QueueRequestGroup> queueRequestGroups;
        containers::DynamicArray<ResourcePlanningWriter::Impl> writerImpls;
        detail::SealedPlanningSummary sealedPlanning;
        containers::DynamicArray<detail::RetainedImportRecord> retainedImports;
        containers::DynamicArray<detail::PublishedExportRecord> publishedExports;
        detail::ResolveScratchHashMap<u64, u32> publishedExportIndices;
        containers::DynamicArray<ResourceContractPatchId> reservedExportContractPatches;
        detail::ResolveScratchHashMap<u64, u32> reservedExportContractPatchIndices;
        detail::ResolveScratchHashMap<u64, u32> contractDecisionPatches;
        detail::ResolveScratch resolveScratch;
        detail::PersistentDedicatedPhysicalPlan persistentDedicatedPlan;
        containers::DynamicArray<detail::PersistentDedicatedPhysicalProgram> persistentDedicatedPrograms;
        containers::DynamicArray<detail::RetainedImportDescriptorOwner> retainedImportDescriptorOwners;
        detail::ResolveScratchHashMap<u64, u32> retainedImportDescriptorOwnerByResource;
        containers::DynamicArray<u32> retainedImportDescriptorReusable;
        containers::DynamicArray<u32> freeRetainedImportDescriptorOwners;
        RenderFlowResourceAllocatorConfig config;
        FrameResourcePolicy activePolicy;
        RenderFlowResourceAllocatorStats stats;
        rhi::DescriptorDomain resourceDescriptors;
        detail::AllocatorDescriptorLedger descriptorLedger;
        detail::AllocatorNativeByteLedger nativeByteLedger;
        detail::DedicatedResourcePool dedicatedPool;
        detail::PlacedResourcePool placedPool;
        u64 rhiDeviceIncarnation = 0;
        u64 descriptorDomainIdentity = 0;
        detail::RenderFlowResourceShutdownFailureInjection shutdownFailureInjection;
        ExecutionGenerationRef::Impl* publishedGeneration = nullptr;
        u64 frameSerial = 0;
        u32 sessionGeneration = 0;
        u32 executionGeneration = 0;
        u32 preparedPlanningGroupCount = 0;
        concurrency::Atomic<u64> recordedQueueRequestCount{0};
        concurrency::Atomic<u32> recordedQueueGroupCount{0};
        u32 reservedExportSlots = 0;
        detail::ResourceContractStorage* contractCompileTarget = nullptr;
        const detail::ResourceContractStorage* boundContract = nullptr;
        RenderFlowResourceSessionState state = RenderFlowResourceSessionState::Idle;
    };
} // namespace vanguard::rendering

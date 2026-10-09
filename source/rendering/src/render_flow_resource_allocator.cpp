#include <vanguard/rendering/render_flow_resource_allocator.hpp>

#include <vanguard/rendering/render_flow_resource_internal.hpp>
#include <vanguard/system/assert.hpp>

#include <atomic>
#include <cstring>
#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        template <typename Enum> [[nodiscard]] constexpr bool HasAny(const Enum value, const Enum flags) noexcept
        {
            return (static_cast<u32>(value) & static_cast<u32>(flags)) != 0;
        }

        [[nodiscard]] bool ActualDeviceLoss() noexcept
        {
            if (!rhi::IsInitialized())
                return false;
            const rhi::DeviceState state = rhi::TestDeviceState();
            return state == rhi::DeviceState::Removed || state == rhi::DeviceState::ResetRequired;
        }

        [[nodiscard]] bool ValidImportReadiness(ImportReadinessKind readiness, rhi::GpuFence fence, rhi::QueueType initialQueue) noexcept
        {
            if (readiness == ImportReadinessKind::SameQueueContinuation)
                return !fence.IsValid() && initialQueue != rhi::QueueType::Copy;
            return readiness == ImportReadinessKind::ExplicitFenceWait && fence.IsValid() &&
                   detail::ValidQueue(fence.queue) && fence.queue == initialQueue;
        }
    } // namespace

    namespace
    {
        [[nodiscard]] detail::CandidateWriterBatch& WriterBatch(ResourcePlanningWriter::Impl& writer) noexcept
        {
            return writer.reservation->batch;
        }

        [[nodiscard]] const detail::CandidateWriterBatch& WriterBatch(const ResourcePlanningWriter::Impl& writer) noexcept
        {
            return writer.reservation->batch;
        }

        [[nodiscard]] bool ValidWriter(const ResourcePlanningWriter::Impl* const writer) noexcept
        {
            if (writer == nullptr || writer->closed || writer->owner == nullptr || writer->reservation == nullptr ||
                writer->owner->sessionGeneration != writer->sessionGeneration || writer->owner->state != RenderFlowResourceSessionState::Planning)
                return false;
            const detail::WriterReservation& reservation = *writer->reservation;
            const detail::CandidateWriterBatch& batch = reservation.batch;
            return reservation.open && reservation.node == writer->node && reservation.flowGroup == writer->flowGroup &&
                   batch.node == writer->node && batch.flowGroup == writer->flowGroup && batch.sessionGeneration == writer->sessionGeneration;
        }

        [[nodiscard]] bool FailWriter(ResourcePlanningWriter::Impl* const writer, RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message,
                                      const ResourceUseId use = {}) noexcept
        {
            const RenderFlowResourceSessionState phase = writer != nullptr && writer->owner != nullptr ? writer->owner->state : RenderFlowResourceSessionState::Idle;
            const RenderFlowNodeId node = writer != nullptr ? writer->node : RenderFlowNodeId{};
            if (writer != nullptr && !writer->closed)
            {
                ++writer->rejectedOperations;
                writer->failed = true;
                if (writer->firstFailureCode == RenderFlowResourceFailureCode::None)
                {
                    writer->firstFailureCode = code;
                    writer->firstFailureMessage = message;
                }
            }
            return detail::Fail(failure, code, phase, message, node, {}, use);
        }

        template <typename T> void PrepareBoundedPlanningAppend(containers::DynamicArray<T>& array, const u32 hardLimit) noexcept
        {
            if (array.Capacity() > hardLimit)
                array.Shrink();
            if (array.Size() < array.Capacity())
                return;

            const u32 capacity = array.Capacity();
            u32 nextCapacity = capacity > 1u ? capacity + capacity / 2u : capacity + 1u;
            if (nextCapacity < capacity || nextCapacity > hardLimit)
                nextCapacity = hardLimit;
            array.Reserve(nextCapacity);
        }

        void ReleaseWriterTapeCapacity(detail::WriterReservation& reservation) noexcept
        {
            detail::CandidateWriterBatch& batch = reservation.batch;
            batch.resources.Clear();
            batch.textureViews.Clear();
            batch.bufferViews.Clear();
            batch.coldPayloads.Clear();
            batch.uses.Clear();
            batch.useEndOrder.Clear();
            batch.operations.Clear();
            batch.resources.Shrink();
            batch.textureViews.Shrink();
            batch.bufferViews.Shrink();
            batch.coldPayloads.Shrink();
            batch.uses.Shrink();
            batch.useEndOrder.Shrink();
            batch.operations.Shrink();
            batch.ReleaseNamedResourceIndex();
            batch.ReleaseNameRetentionTracking();
        }

        void TrimWriterRecordingCapacity(detail::CandidateWriterBatch& batch) noexcept
        {
            constexpr u32 resourceFloor = 64;
            constexpr u32 viewFloor = 64;
            batch.TrimDormantNameBacking();
            const u32 liveResources = batch.resourceCount;
            if (batch.resources.Size() > resourceFloor && batch.resources.Size() > (liveResources > resourceFloor ? liveResources : resourceFloor) * 4u)
            {
                batch.resources.Resize(liveResources);
                batch.resources.Shrink();
                batch.PruneNameRetentionTracking();
                batch.RebuildNamedResourceIndex();
            }
            if (batch.textureViews.Capacity() > viewFloor && batch.textureViews.Capacity() > (batch.textureViews.Size() > viewFloor ? batch.textureViews.Size() : viewFloor) * 4u)
                batch.textureViews.Shrink();
            if (batch.bufferViews.Capacity() > viewFloor && batch.bufferViews.Capacity() > (batch.bufferViews.Size() > viewFloor ? batch.bufferViews.Size() : viewFloor) * 4u)
                batch.bufferViews.Shrink();
            if (batch.uses.Capacity() > viewFloor && batch.uses.Capacity() > (batch.uses.Size() > viewFloor ? batch.uses.Size() : viewFloor) * 4u)
                batch.uses.Shrink();
            if (batch.useEndOrder.Capacity() > viewFloor && batch.useEndOrder.Capacity() > (batch.useEndOrder.Size() > viewFloor ? batch.useEndOrder.Size() : viewFloor) * 4u)
                batch.useEndOrder.Shrink();
            batch.TrimAuxiliaryRetentionBacking();
        }

        void ResetWriterReservation(detail::WriterReservation& reservation, const bool retainBoundedRecording = true) noexcept
        {
            // RED's fixed per-group request array keeps its bounded backing and
            // clears only logical size. Do the same for active Vanguard groups;
            // the explicit per-writer cap prevents historical unbounded growth.
            // Rejected/canceled planning never reached the aggregate admission
            // gate, so drop that unvalidated footprint instead of retaining it.
            if (!retainBoundedRecording)
                ReleaseWriterTapeCapacity(reservation);
            else
                TrimWriterRecordingCapacity(reservation.batch);
            reservation.batch.Reset();
            reservation.node = {};
            reservation.flowGroup = {};
            reservation.open = false;
            reservation.failed = false;
        }

        void ResetWriterImpl(ResourcePlanningWriter::Impl& writer) noexcept
        {
            writer.owner = nullptr;
            writer.reservation = nullptr;
            writer.node = {};
            writer.flowGroup = {};
            writer.sessionGeneration = 0;
            writer.rejectedOperations = 0;
            writer.firstFailureCode = RenderFlowResourceFailureCode::None;
            writer.firstFailureMessage = nullptr;
            writer.failed = false;
            writer.closed = true;
        }

        void RollbackLastCandidateResource(detail::CandidateWriterBatch& batch) noexcept
        {
            VG_ASSERT(batch.resourceCount != 0);
            --batch.resourceCount;
            batch.resources[batch.resourceCount].Reset();
            if (!batch.namedResourceSlots.Empty())
                batch.RebuildNamedResourceIndex();
        }

        [[nodiscard]] ResourcePlanningWriter::Impl* AcquireWriterImpl(RenderFlowResourceAllocator::Impl& owner, const GpuFlowGroupId flowGroup) noexcept
        {
            if (flowGroup.value < owner.writerImpls.Size())
            {
                ResourcePlanningWriter::Impl& writer = owner.writerImpls[flowGroup.value];
                std::atomic_ref<u32> lease(writer.primaryLease);
                u32 expected = 0;
                if (lease.compare_exchange_strong(expected, 1u, std::memory_order_acq_rel, std::memory_order_acquire))
                    return &writer;
            }

            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(ResourcePlanningWriter::Impl), alignof(ResourcePlanningWriter::Impl));
            if (!block)
                return nullptr;
            return new (block.address) ResourcePlanningWriter::Impl();
        }

        void DestroyWriter(ResourcePlanningWriter::Impl*& writer) noexcept
        {
            if (writer == nullptr)
                return;
            ResourcePlanningWriter::Impl* const value = writer;
            writer = nullptr;
            RenderFlowResourceAllocator::Impl* const owner = value->owner;
            const u32 primaryIndex = value->primaryIndex;
            if (primaryIndex != InvalidRenderFlowResourceIndex)
            {
                ResetWriterImpl(*value);
                std::atomic_ref<u32> lease(value->primaryLease);
                lease.store(0u, std::memory_order_release);
            }
            else
            {
                value->~Impl();
                memory::MemoryBlock block{value, sizeof(ResourcePlanningWriter::Impl), memory::PoolId::Rendering};
                memory::Free(block);
            }
            detail::ReleaseAllocator(owner);
        }

        [[nodiscard]] bool ValidResource(const ResourcePlanningWriter::Impl& writer, const LogicalResourceId resource) noexcept
        {
            if (!ValidWriter(&writer))
                return false;
            const detail::CandidateWriterBatch& batch = WriterBatch(writer);
            return resource.IsValid() && resource.flowGroup == writer.flowGroup && resource.generation == writer.sessionGeneration && resource.index < batch.resourceCount;
        }

        [[nodiscard]] bool ValidTextureView(const ResourcePlanningWriter::Impl& writer, const LogicalTextureViewId view) noexcept
        {
            if (!ValidWriter(&writer))
                return false;
            const detail::CandidateWriterBatch& batch = WriterBatch(writer);
            return view.IsValid() && view.flowGroup == writer.flowGroup && view.generation == writer.sessionGeneration && view.index < batch.textureViews.Size();
        }

        [[nodiscard]] bool ValidBufferView(const ResourcePlanningWriter::Impl& writer, const LogicalBufferViewId view) noexcept
        {
            if (!ValidWriter(&writer))
                return false;
            const detail::CandidateWriterBatch& batch = WriterBatch(writer);
            return view.IsValid() && view.flowGroup == writer.flowGroup && view.generation == writer.sessionGeneration && view.index < batch.bufferViews.Size();
        }

        [[nodiscard]] bool PushOperation(ResourcePlanningWriter::Impl& writer, detail::CandidateOperation operation, RenderFlowResourceFailure* const failure) noexcept
        {
            if (!ValidWriter(&writer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
            if (writer.failed)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::IncompletePlanning, "planning writer is poisoned by an earlier rejected operation");
            detail::CandidateWriterBatch& batch = WriterBatch(writer);
            const u32 writerOperationLimit = writer.owner->config.maximumOperationsPerPlanningWriter;
            if (batch.semanticSequenceCount >= writerOperationLimit)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer operation capacity is exhausted");
            PrepareBoundedPlanningAppend(batch.operations, writerOperationLimit);
            const detail::CandidateOperationKind kind = operation.kind;
            operation.sequence = batch.semanticSequenceCount++;
            batch.operations.PushBack(std::move(operation));
            switch (kind)
            {
            case detail::CandidateOperationKind::DeclareTexture:
            case detail::CandidateOperationKind::DeclareBuffer:
            case detail::CandidateOperationKind::DeclareLike:
            case detail::CandidateOperationKind::ImportTexture:
            case detail::CandidateOperationKind::ImportBuffer:
                ++batch.declarationCount;
                break;
            case detail::CandidateOperationKind::TextureUseBegin:
            case detail::CandidateOperationKind::BufferUseBegin:
                ++batch.stepCount;
                break;
            case detail::CandidateOperationKind::ScopeOpen:
                ++batch.explicitScopeOpenCount;
                break;
            case detail::CandidateOperationKind::Decision:
                ++batch.decisionCount;
                break;
            case detail::CandidateOperationKind::Export:
                ++batch.exportCount;
                break;
            default:
                break;
            }
            return true;
        }

        [[nodiscard]] bool FindOrCreateNamedResource(ResourcePlanningWriter::Impl& writer, const LogicalResourceKey key, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
        {
            resource = {};
            if (!ValidWriter(&writer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
            if (writer.failed)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::IncompletePlanning, "planning writer is poisoned by an earlier rejected operation");
            const FlowSpaceId flowSpace = key.GetFlowSpace();
            const containers::StringView name = key.GetName();
            if (!flowSpace.IsValid() || name.Data() == nullptr || name.Length() == 0)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "logical resource key is invalid");

            const u32 logicalNameHash = key.CalcHash();
            detail::CandidateWriterBatch& batch = WriterBatch(writer);
            if (!batch.namedResourceSlots.Empty())
            {
                const u32 index = batch.FindNamedResource(flowSpace, name, logicalNameHash);
                if (index != InvalidRenderFlowResourceIndex)
                {
                    resource = {writer.flowGroup, index, writer.sessionGeneration};
                    return true;
                }
            }
            else
                for (u32 index = 0; index < batch.resourceCount; ++index)
                {
                    const detail::CandidateResource& candidate = batch.resources[index];
                    if (candidate.identity == detail::CandidateIdentityKind::Named && candidate.logicalNameHash == logicalNameHash && candidate.flowSpace == flowSpace &&
                        containers::StringView(candidate.name) == name)
                    {
                        resource = {writer.flowGroup, index, writer.sessionGeneration};
                        return true;
                    }
                }
            if (batch.resourceCount >= writer.owner->config.maximumLogicalResources)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer logical-resource capacity is exhausted");

            const u32 resourceIndex = batch.resourceCount++;
            if (resourceIndex == batch.resources.Size())
                batch.resources.EmplaceBack();
            detail::CandidateResource& candidate = batch.resources[resourceIndex];
            candidate.Reset();
            candidate.identity = detail::CandidateIdentityKind::Named;
            candidate.flowSpace = flowSpace;
            candidate.logicalNameHash = logicalNameHash;
            candidate.SetNameBounded(name);
            batch.TrackOversizedName(resourceIndex);
            batch.IndexNamedResource(resourceIndex);
            resource = {writer.flowGroup, resourceIndex, writer.sessionGeneration};
            return true;
        }

        [[nodiscard]] bool DeclareNamed(ResourcePlanningWriter::Impl& writer, const LogicalResourceKey key, const FrameResourceDesc& desc, const detail::CandidateOperationKind kind, LogicalResourceId& resource,
                                        RenderFlowResourceFailure* const failure) noexcept
        {
            if (!ValidWriter(&writer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
            detail::CandidateWriterBatch& batch = WriterBatch(writer);
            const u32 resourceCount = batch.resourceCount;
            if (!FindOrCreateNamedResource(writer, key, resource, failure))
                return false;
            const u32 descIndex = batch.coldPayloads.Size();
            const u32 writerOperationLimit = writer.owner->config.maximumOperationsPerPlanningWriter;
            if (descIndex >= writerOperationLimit)
            {
                if (batch.resourceCount > resourceCount)
                {
                    RollbackLastCandidateResource(batch);
                }
                resource = {};
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer cold declaration capacity is exhausted");
            }
            PrepareBoundedPlanningAppend(batch.coldPayloads, writerOperationLimit);
            if (kind == detail::CandidateOperationKind::DeclareTexture)
                batch.coldPayloads.PushBack(detail::CandidateColdPayload::TextureDeclaration(desc.texture));
            else
                batch.coldPayloads.PushBack(detail::CandidateColdPayload::BufferDeclaration(desc.buffer));
            detail::CandidateOperation operation = detail::CandidateOperation::DeclareResource(kind, resource.index, descIndex);
            if (PushOperation(writer, std::move(operation), failure))
                return true;
            batch.coldPayloads.PopBack();
            if (batch.resourceCount > resourceCount)
            {
                RollbackLastCandidateResource(batch);
            }
            resource = {};
            return false;
        }

        [[nodiscard]] bool DeclareTemporary(ResourcePlanningWriter::Impl& writer, const containers::StringView displayName, const FrameResourceDesc& desc, const detail::CandidateOperationKind kind,
                                            LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
        {
            resource = {};
            if (!ValidWriter(&writer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
            detail::CandidateWriterBatch& batch = WriterBatch(writer);
            if (batch.resourceCount >= writer.owner->config.maximumLogicalResources)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer logical-resource capacity is exhausted");

            const u32 resourceIndex = batch.resourceCount++;
            if (resourceIndex == batch.resources.Size())
                batch.resources.EmplaceBack();
            detail::CandidateResource& candidate = batch.resources[resourceIndex];
            candidate.Reset();
            candidate.identity = detail::CandidateIdentityKind::Temporary;
            candidate.flowSpace = {};
            candidate.SetNameBounded(displayName);
            batch.TrackOversizedName(resourceIndex);
            resource = {writer.flowGroup, resourceIndex, writer.sessionGeneration};

            const u32 descIndex = batch.coldPayloads.Size();
            const u32 writerOperationLimit = writer.owner->config.maximumOperationsPerPlanningWriter;
            if (descIndex >= writerOperationLimit)
            {
                RollbackLastCandidateResource(batch);
                resource = {};
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer cold declaration capacity is exhausted");
            }
            PrepareBoundedPlanningAppend(batch.coldPayloads, writerOperationLimit);
            if (kind == detail::CandidateOperationKind::DeclareTexture)
                batch.coldPayloads.PushBack(detail::CandidateColdPayload::TextureDeclaration(desc.texture));
            else
                batch.coldPayloads.PushBack(detail::CandidateColdPayload::BufferDeclaration(desc.buffer));
            detail::CandidateOperation operation = detail::CandidateOperation::DeclareResource(kind, resource.index, descIndex);
            if (!PushOperation(writer, std::move(operation), failure))
            {
                batch.coldPayloads.PopBack();
                RollbackLastCandidateResource(batch);
                resource = {};
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ImportNamed(ResourcePlanningWriter::Impl& writer, const LogicalResourceKey key, const ImportedResourceId imported, const detail::CandidateOperationKind kind,
                                       LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
        {
            resource = {};
            if (!ValidWriter(&writer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
            detail::CandidateWriterBatch& batch = WriterBatch(writer);
            if (!imported.IsValid() || imported.generation != writer.sessionGeneration || imported.index >= writer.owner->retainedImports.Size())
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "import operation references an invalid session import");
            const detail::RetainedImportRecord& retained = writer.owner->retainedImports[imported.index];
            if ((kind == detail::CandidateOperationKind::ImportTexture && retained.desc.kind != FrameResourceKind::Texture) ||
                (kind == detail::CandidateOperationKind::ImportBuffer && retained.desc.kind != FrameResourceKind::Buffer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::DescriptorConflict, "import operation kind disagrees with the registered physical resource");
            const u32 resourceCount = batch.resourceCount;
            if (!FindOrCreateNamedResource(writer, key, resource, failure))
                return false;
            detail::CandidateOperation operation = detail::CandidateOperation::ImportResource(kind, resource.index, imported.index);
            if (PushOperation(writer, std::move(operation), failure))
                return true;
            if (batch.resourceCount > resourceCount)
            {
                RollbackLastCandidateResource(batch);
            }
            resource = {};
            return false;
        }

        [[nodiscard]] bool BeginTextureUseImpl(ResourcePlanningWriter::Impl& writer, const LogicalResourceId resource, const LogicalTextureViewId view, const TextureUseDesc& desc, ResourceUseId& use,
                                               RenderFlowResourceFailure* const failure) noexcept
        {
            use = {};
            if ((!resource.IsValid() || !ValidResource(writer, resource)) && (!view.IsValid() || !ValidTextureView(writer, view)))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture use references an invalid resource or view");
            detail::CandidateWriterBatch& batch = WriterBatch(writer);
            const u32 writerOperationLimit = writer.owner->config.maximumOperationsPerPlanningWriter;
            if (batch.uses.Size() >= writerOperationLimit)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer resource-use capacity is exhausted");
            const u32 useIndex = batch.uses.Size();
            const ResourceUseId result{writer.flowGroup, useIndex, writer.sessionGeneration};
            PrepareBoundedPlanningAppend(batch.uses, writerOperationLimit);
            batch.uses.PushBack(detail::CandidateUse::Texture(resource.IsValid() ? resource.index : InvalidRenderFlowResourceIndex,
                                                              view.IsValid() ? view.index : InvalidRenderFlowResourceIndex, desc, batch.semanticSequenceCount));
            detail::CandidateOperation operation = detail::CandidateOperation::BeginUse(detail::CandidateOperationKind::TextureUseBegin, useIndex);
            if (!PushOperation(writer, std::move(operation), failure))
            {
                batch.uses.PopBack();
                return false;
            }
            use = result;
            return true;
        }

        [[nodiscard]] bool BeginBufferUseImpl(ResourcePlanningWriter::Impl& writer, const LogicalResourceId resource, const LogicalBufferViewId view, const BufferUseDesc& desc, ResourceUseId& use,
                                              RenderFlowResourceFailure* const failure) noexcept
        {
            use = {};
            if ((!resource.IsValid() || !ValidResource(writer, resource)) && (!view.IsValid() || !ValidBufferView(writer, view)))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer use references an invalid resource or view");
            detail::CandidateWriterBatch& batch = WriterBatch(writer);
            const u32 writerOperationLimit = writer.owner->config.maximumOperationsPerPlanningWriter;
            if (batch.uses.Size() >= writerOperationLimit)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer resource-use capacity is exhausted");
            const u32 useIndex = batch.uses.Size();
            const ResourceUseId result{writer.flowGroup, useIndex, writer.sessionGeneration};
            PrepareBoundedPlanningAppend(batch.uses, writerOperationLimit);
            batch.uses.PushBack(detail::CandidateUse::Buffer(resource.IsValid() ? resource.index : InvalidRenderFlowResourceIndex,
                                                             view.IsValid() ? view.index : InvalidRenderFlowResourceIndex, desc, batch.semanticSequenceCount));
            detail::CandidateOperation operation = detail::CandidateOperation::BeginUse(detail::CandidateOperationKind::BufferUseBegin, useIndex);
            if (!PushOperation(writer, std::move(operation), failure))
            {
                batch.uses.PopBack();
                return false;
            }
            use = result;
            return true;
        }
    } // namespace

    bool PlanningJoinToken::FromReadyCounter(const jobs::Counter& counter, PlanningJoinToken& token) noexcept
    {
        token = PlanningJoinToken(false);
        if (!counter.IsValid() || !counter.IsReady())
            return false;
        token = PlanningJoinToken(true);
        return true;
    }

    ResourcePlanningWriter::~ResourcePlanningWriter()
    {
        Abandon();
    }

    ResourcePlanningWriter::ResourcePlanningWriter(ResourcePlanningWriter&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    ResourcePlanningWriter& ResourcePlanningWriter::operator=(ResourcePlanningWriter&& other) noexcept
    {
        if (this == &other)
            return *this;
        Abandon();
        m_impl = other.m_impl;
        other.m_impl = nullptr;
        return *this;
    }

    bool ResourcePlanningWriter::IsValid() const noexcept
    {
        return ValidWriter(m_impl);
    }

    RenderFlowNodeId ResourcePlanningWriter::GetNode() const noexcept
    {
        return m_impl != nullptr ? m_impl->node : RenderFlowNodeId{};
    }

    GpuFlowGroupId ResourcePlanningWriter::GetFlowGroup() const noexcept
    {
        return m_impl != nullptr ? m_impl->flowGroup : GpuFlowGroupId{};
    }

    bool ResourcePlanningWriter::ReferenceResource(const LogicalResourceKey key, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
        return FindOrCreateNamedResource(*m_impl, key, resource, failure);
    }

    bool ResourcePlanningWriter::DeclareTexture(const LogicalResourceKey key, const FrameTextureDesc& desc, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
        if (!detail::ValidTextureDesc(desc))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::DescriptorConflict, "texture declaration descriptor is invalid");
        return DeclareNamed(*m_impl, key, FrameResourceDesc::Texture(desc), detail::CandidateOperationKind::DeclareTexture, resource, failure);
    }

    bool ResourcePlanningWriter::DeclareBuffer(const LogicalResourceKey key, const FrameBufferDesc& desc, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
        if (!detail::ValidBufferDesc(desc))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::DescriptorConflict, "buffer declaration descriptor is invalid");
        return DeclareNamed(*m_impl, key, FrameResourceDesc::Buffer(desc), detail::CandidateOperationKind::DeclareBuffer, resource, failure);
    }

    bool ResourcePlanningWriter::DeclareTemporaryTexture(const containers::StringView displayName, const FrameTextureDesc& desc, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
        if (!detail::ValidTextureDesc(desc))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::DescriptorConflict, "temporary texture descriptor is invalid");
        return DeclareTemporary(*m_impl, displayName, FrameResourceDesc::Texture(desc), detail::CandidateOperationKind::DeclareTexture, resource, failure);
    }

    bool ResourcePlanningWriter::DeclareTemporaryBuffer(const containers::StringView displayName, const FrameBufferDesc& desc, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
        if (!detail::ValidBufferDesc(desc))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::DescriptorConflict, "temporary buffer descriptor is invalid");
        return DeclareTemporary(*m_impl, displayName, FrameResourceDesc::Buffer(desc), detail::CandidateOperationKind::DeclareBuffer, resource, failure);
    }

    bool ResourcePlanningWriter::DeclareLike(const LogicalResourceKey key, const LogicalResourceId source, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        resource = {};
        if (m_impl == nullptr || !ValidResource(*m_impl, source))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "declare-like source is not owned by this planning writer");
        detail::CandidateWriterBatch& batch = WriterBatch(*m_impl);
        const u32 resourceCount = batch.resourceCount;
        if (!FindOrCreateNamedResource(*m_impl, key, resource, failure))
            return false;
        if (resource == source)
        {
            resource = {};
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "declare-like destination must differ from its source");
        }
        detail::CandidateOperation operation = detail::CandidateOperation::DeclareLike(resource.index, source.index);
        if (!PushOperation(*m_impl, std::move(operation), failure))
        {
            if (batch.resourceCount > resourceCount)
            {
                RollbackLastCandidateResource(batch);
            }
            resource = {};
            return false;
        }
        return true;
    }

    bool ResourcePlanningWriter::DeclareTemporaryLike(const containers::StringView displayName, const LogicalResourceId source, LogicalResourceId& resource,
                                                      RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        resource = {};
        if (m_impl == nullptr || !ValidResource(*m_impl, source))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "temporary declare-like source is not owned by this planning writer");
        detail::CandidateWriterBatch& batch = WriterBatch(*m_impl);
        if (batch.resourceCount >= m_impl->owner->config.maximumLogicalResources)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer logical-resource capacity is exhausted");

        const u32 resourceIndex = batch.resourceCount++;
        if (resourceIndex == batch.resources.Size())
            batch.resources.EmplaceBack();
        detail::CandidateResource& candidate = batch.resources[resourceIndex];
        candidate.Reset();
        candidate.identity = detail::CandidateIdentityKind::Temporary;
        candidate.flowSpace = {};
        candidate.SetNameBounded(displayName);
        batch.TrackOversizedName(resourceIndex);
        resource = {m_impl->flowGroup, resourceIndex, m_impl->sessionGeneration};

        detail::CandidateOperation operation = detail::CandidateOperation::DeclareLike(resource.index, source.index);
        if (!PushOperation(*m_impl, std::move(operation), failure))
        {
            RollbackLastCandidateResource(batch);
            resource = {};
            return false;
        }
        return true;
    }

    bool ResourcePlanningWriter::ImportTexture(const LogicalResourceKey key, const ImportedResourceId imported, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
        return ImportNamed(*m_impl, key, imported, detail::CandidateOperationKind::ImportTexture, resource, failure);
    }

    bool ResourcePlanningWriter::ImportBuffer(const LogicalResourceKey key, const ImportedResourceId imported, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
        return ImportNamed(*m_impl, key, imported, detail::CandidateOperationKind::ImportBuffer, resource, failure);
    }

    bool ResourcePlanningWriter::CreateTextureView(const LogicalResourceId resource, const rhi::TextureViewDesc& desc, LogicalTextureViewId& view, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        view = {};
        if (m_impl == nullptr || !ValidResource(*m_impl, resource))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture view references an invalid logical resource");
        detail::CandidateWriterBatch& batch = WriterBatch(*m_impl);
        if (batch.textureViews.Size() >= m_impl->owner->config.maximumViews)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::CapacityExceeded, "texture-view capacity is exhausted");
        view = {m_impl->flowGroup, batch.textureViews.Size(), m_impl->sessionGeneration};
        batch.textureViews.PushBack({resource, desc});
        detail::CandidateOperation operation = detail::CandidateOperation::CreateTextureView(resource.index, view.index);
        if (!PushOperation(*m_impl, std::move(operation), failure))
        {
            batch.textureViews.PopBack();
            view = {};
            return false;
        }
        return true;
    }

    bool ResourcePlanningWriter::CreateBufferView(const LogicalResourceId resource, const rhi::BufferViewDesc& desc, LogicalBufferViewId& view, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        view = {};
        if (m_impl == nullptr || !ValidResource(*m_impl, resource))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer view references an invalid logical resource");
        detail::CandidateWriterBatch& batch = WriterBatch(*m_impl);
        if (batch.bufferViews.Size() >= m_impl->owner->config.maximumViews)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::CapacityExceeded, "buffer-view capacity is exhausted");
        view = {m_impl->flowGroup, batch.bufferViews.Size(), m_impl->sessionGeneration};
        batch.bufferViews.PushBack({resource, desc});
        detail::CandidateOperation operation = detail::CandidateOperation::CreateBufferView(resource.index, view.index);
        if (!PushOperation(*m_impl, std::move(operation), failure))
        {
            batch.bufferViews.PopBack();
            view = {};
            return false;
        }
        return true;
    }

    bool ResourcePlanningWriter::BeginTextureUse(const LogicalResourceId resource, const TextureUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        return m_impl != nullptr ? BeginTextureUseImpl(*m_impl, resource, {}, desc, use, failure)
                                 : detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
    }

    bool ResourcePlanningWriter::BeginTextureViewUse(const LogicalTextureViewId view, const TextureUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        return m_impl != nullptr ? BeginTextureUseImpl(*m_impl, {}, view, desc, use, failure)
                                 : detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
    }

    bool ResourcePlanningWriter::BeginBufferUse(const LogicalResourceId resource, const BufferUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        return m_impl != nullptr ? BeginBufferUseImpl(*m_impl, resource, {}, desc, use, failure)
                                 : detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
    }

    bool ResourcePlanningWriter::BeginBufferViewUse(const LogicalBufferViewId view, const BufferUseDesc& desc, ResourceUseId& use, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        return m_impl != nullptr ? BeginBufferUseImpl(*m_impl, {}, view, desc, use, failure)
                                 : detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
    }

    bool ResourcePlanningWriter::EndUse(const ResourceUseId use, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidWriter(m_impl))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open", use);
        detail::CandidateWriterBatch& batch = WriterBatch(*m_impl);
        if (!use.IsValid() || use.flowGroup != m_impl->flowGroup || use.generation != m_impl->sessionGeneration || use.ordinal >= batch.uses.Size())
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use identity is invalid", use);
        detail::CandidateUse& candidate = batch.uses[use.ordinal];
        if (candidate.endSequence != InvalidRenderFlowResourceIndex)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use was already ended", use);
        if (m_impl->failed)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::IncompletePlanning, "planning writer is poisoned by an earlier rejected operation", use);
        const u32 writerOperationLimit = m_impl->owner->config.maximumOperationsPerPlanningWriter;
        if (batch.semanticSequenceCount >= writerOperationLimit)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer operation capacity is exhausted", use);
        PrepareBoundedPlanningAppend(batch.useEndOrder, writerOperationLimit);
        candidate.endSequence = batch.semanticSequenceCount++;
        batch.useEndOrder.PushBack(use.ordinal);
        ++batch.stepCount;
        return true;
    }

    bool ResourcePlanningWriter::OpenResourceScope(const LogicalResourceId resource, const ResourceScopeId scope, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, resource) || !scope.IsValid())
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope begin is invalid");
        detail::CandidateOperation operation = detail::CandidateOperation::OpenScope(resource.index, scope);
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::OpenResourceScope(const LogicalResourceId resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, resource))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope begin is invalid");
        detail::CandidateOperation operation = detail::CandidateOperation::NamedScope(detail::CandidateOperationKind::NamedScopeOpen, resource.index);
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::CloseResourceScope(const ResourceScopeId scope, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !scope.IsValid())
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope end is invalid");
        detail::CandidateOperation operation = detail::CandidateOperation::CloseScope(scope);
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::CloseResourceScope(const LogicalResourceId resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, resource))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope end is invalid");
        detail::CandidateOperation operation = detail::CandidateOperation::NamedScope(detail::CandidateOperationKind::NamedScopeClose, resource.index);
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::SwapLogicalMappings(const LogicalResourceId left, const LogicalResourceId right, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, left) || !ValidResource(*m_impl, right) || left == right)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "logical swap identities are invalid");
        detail::CandidateOperation operation = detail::CandidateOperation::SwapMappings(left.index, right.index);
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::CaptureDecision(const bool value, DecisionId& decision, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        decision = {};
        if (m_impl == nullptr || !ValidWriter(m_impl))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
        const detail::CandidateWriterBatch& batch = WriterBatch(*m_impl);
        // Decision identity is decision-local, not an operation-tape address.
        // Carry the dense ordinal now so execution can index the captured value
        // directly instead of searching the packet's decision list.
        const DecisionId result{m_impl->flowGroup, batch.decisionCount, m_impl->sessionGeneration};
        detail::CandidateOperation operation = detail::CandidateOperation::Decision(value);
        if (!PushOperation(*m_impl, std::move(operation), failure))
            return false;
        decision = result;
        return true;
    }

    bool ResourcePlanningWriter::RequestExport(const LogicalResourceId resource, const ExportSlotId slot, const TerminalResourceExportDesc& desc, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, resource) || !slot.IsValid() || slot.generation != m_impl->sessionGeneration || slot.index >= m_impl->owner->reservedExportSlots)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export request is invalid");
        if (desc.readiness != ExportReadinessKind::SameQueueContinuation && desc.readiness != ExportReadinessKind::ExplicitFenceSignal)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "export readiness kind is unsupported");
        if (!detail::ValidQueue(desc.terminalQueue))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "export terminal queue is invalid");
        detail::CandidateOperation operation = detail::CandidateOperation::Export(resource.index, slot.index, desc);
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::Close(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (!ValidWriter(m_impl))
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, m_impl != nullptr && m_impl->owner != nullptr ? m_impl->owner->state : RenderFlowResourceSessionState::Idle,
                                "planning writer is not open", m_impl != nullptr ? m_impl->node : RenderFlowNodeId{});

        RenderFlowResourceAllocator::Impl& owner = *m_impl->owner;
        {
            detail::WriterReservation* const reservation = m_impl->reservation;
            if (reservation == nullptr || !reservation->open || reservation->node != m_impl->node || reservation->flowGroup != m_impl->flowGroup)
                return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "planning writer reservation is not open");

            if (m_impl->rejectedOperations != 0)
                static_cast<void>(owner.pendingRejectedOperations.ExchangeAdd(m_impl->rejectedOperations));
            if (m_impl->failed)
            {
                reservation->open = false;
                reservation->failed = true;
                m_impl->closed = true;
                const RenderFlowResourceFailureCode code = m_impl->firstFailureCode != RenderFlowResourceFailureCode::None ? m_impl->firstFailureCode : RenderFlowResourceFailureCode::IncompletePlanning;
                const char* const message = m_impl->firstFailureMessage != nullptr ? m_impl->firstFailureMessage : "planning writer was poisoned by a rejected operation";
                const bool result = detail::Fail(failure, code, owner.state, message, m_impl->node);
                DestroyWriter(m_impl);
                return result;
            }

            reservation->open = false;
            m_impl->closed = true;
        }
        DestroyWriter(m_impl);
        return true;
    }

    void ResourcePlanningWriter::Abandon() noexcept
    {
        if (m_impl == nullptr)
            return;
        RenderFlowResourceAllocator::Impl* const owner = m_impl->owner;
        if (!m_impl->closed && owner != nullptr)
        {
            if (owner->sessionGeneration == m_impl->sessionGeneration && owner->state == RenderFlowResourceSessionState::Planning && m_impl->reservation != nullptr)
            {
                detail::WriterReservation& reservation = *m_impl->reservation;
                if (reservation.open && reservation.node == m_impl->node && reservation.flowGroup == m_impl->flowGroup &&
                    reservation.batch.sessionGeneration == m_impl->sessionGeneration)
                {
                    if (m_impl->rejectedOperations != 0)
                        static_cast<void>(owner->pendingRejectedOperations.ExchangeAdd(m_impl->rejectedOperations));
                    reservation.open = false;
                    reservation.failed = true;
                }
            }
            m_impl->closed = true;
        }
        DestroyWriter(m_impl);
    }

    bool RenderFlowResourceAllocator::RegisterPresentationImport(const rhi::AcquiredBackBuffer& acquisition, ImportedResourceId& imported,
                                                                 RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        imported = {};
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not accepting presentation imports");
        concurrency::ScopedLock<concurrency::SpinLock> guard(owner->writerLock);
        if (owner->preparedPlanningGroupCount != 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "presentation import must be registered before creating planning writers");
        if (owner->config.allowLogicalOnlyValidation || !acquisition.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::UnsupportedCapability, owner->state, "presentation import requires a real acquired swap-chain back buffer");

        rhi::TextureDesc actual;
        rhi::Failure rhiFailure;
        if (!rhi::GetTextureDesc(acquisition.texture, actual, &rhiFailure))
            return detail::Fail(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ImportedIdentity), owner->state,
                                rhiFailure.message[0] != '\0' ? rhiFailure.message : "presentation texture descriptor query failed");
        if (actual.virtualResource || !HasAny(actual.usage, rhi::TextureUsage::Present) || actual.extent.width != acquisition.width ||
            actual.extent.height != acquisition.height || actual.extent.depth != 1 || actual.mipCount != 1 || actual.arraySize != 1 ||
            actual.initialState != rhi::ResourceState::Present)
            return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "acquired back buffer does not satisfy the presentation import contract");
        detail::ResolveScratch& registrationScratch = owner->resolveScratch;
        const detail::PresentationImportKey presentationKey{rhi::ResourceRef(acquisition.swapChain).value, acquisition.serial};
        const u64 resourceKey = rhi::ResourceRef(acquisition.texture).value;
        u32 existingIndex = InvalidRenderFlowResourceIndex;
        if (registrationScratch.presentationImports.Find(presentationKey, existingIndex))
        {
            if (existingIndex >= owner->retainedImports.Size())
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "presentation import index is stale");
            imported = {existingIndex, owner->sessionGeneration};
            return true;
        }
        if (registrationScratch.retainedImportResources.Find(resourceKey, existingIndex))
            return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state,
                                "one acquired back buffer cannot be registered as multiple retained imports");
        if (owner->retainedImports.Size() >= owner->config.maximumRetainedImports)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "retained import capacity is exhausted");

        registrationScratch.presentationImports.Reserve(owner->retainedImports.Size() + 1u);
        registrationScratch.retainedImportResources.Reserve(owner->retainedImports.Size() + 1u);
        FrameTextureDesc frameDesc;
        frameDesc.active = actual;
        frameDesc.maximumExtent = actual.extent;
        frameDesc.maximumMipCount = actual.mipCount;
        frameDesc.initialization = FrameResourceInitialization::ImportedContents;
        detail::RetainedImportRecord& record = owner->retainedImports.EmplaceBack();
        record.desc = FrameResourceDesc::Texture(frameDesc);
        record.texture = rhi::Texture(acquisition.texture);
        record.initialState = rhi::ResourceState::Present;
        record.terminalState = rhi::ResourceState::Present;
        record.initialQueue = rhi::QueueType::Graphics;
        record.terminalQueue = rhi::QueueType::Graphics;
        record.presentationAcquisition = acquisition;
        const u32 importIndex = owner->retainedImports.Size() - 1u;
        if (!registrationScratch.presentationImports.Insert(presentationKey, importIndex))
        {
            static_cast<void>(owner->retainedImports.PopBack());
            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "presentation import index rejected a prevalidated identity");
        }
        if (!registrationScratch.retainedImportResources.Insert(resourceKey, importIndex))
        {
            static_cast<void>(registrationScratch.presentationImports.Erase(presentationKey));
            static_cast<void>(owner->retainedImports.PopBack());
            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "presentation resource index rejected a prevalidated identity");
        }
        imported = {importIndex, owner->sessionGeneration};
        return true;
    }

    bool RenderFlowResourceAllocator::RegisterImport(const RetainedTextureImportDesc& desc, ImportedResourceId& imported, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        imported = {};
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not accepting imports");
        concurrency::ScopedLock<concurrency::SpinLock> guard(owner->writerLock);
        if (owner->preparedPlanningGroupCount != 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "retained imports must be registered before creating planning writers");
        if (!desc.token.IsValid() || !desc.texture.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "retained texture import identity is invalid");
        const bool readinessValid = ValidImportReadiness(desc.readiness, desc.incomingWait, desc.initialQueue);
        if (!readinessValid)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "import readiness requires either same-queue continuation or a fence from its initial queue");
        if (!detail::ValidQueue(desc.initialQueue) || !detail::ValidQueue(desc.terminalQueue) || desc.terminalQueue == rhi::QueueType::Copy)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "retained import must terminate on Graphics or Compute");
        rhi::TextureDesc actual;
        rhi::Failure rhiFailure;
        const bool descriptorAvailable = rhi::GetTextureDesc(desc.texture, actual, &rhiFailure);
        if (!descriptorAvailable)
            return detail::Fail(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ImportedIdentity), owner->state,
                                rhiFailure.message[0] != '\0' ? rhiFailure.message : "retained texture descriptor query failed");
        if (actual.virtualResource || !detail::PhysicalTextureDescEqual(actual, desc.expected))
            return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "retained texture's authoritative descriptor does not match the expected contract");
        if (HasAny(actual.usage, rhi::TextureUsage::Present))
            return detail::Fail(failure, RenderFlowResourceFailureCode::UnsupportedCapability, owner->state,
                                "present-capable textures require the acquisition-aware presentation import contract");
        if (!detail::TextureStateAllowed(actual, desc.initialState) || !detail::TextureStateAllowed(actual, desc.terminalState) ||
            !detail::QueueStateAllowed(desc.initialQueue, FrameResourceKind::Texture, desc.initialState) || !detail::QueueStateAllowed(desc.terminalQueue, FrameResourceKind::Texture, desc.terminalState))
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidUseOrScope, owner->state, "retained texture initial or terminal state is incompatible with its descriptor or queue");

        FrameTextureDesc frameDesc;
        frameDesc.active = actual;
        frameDesc.maximumExtent = actual.extent;
        frameDesc.maximumMipCount = actual.mipCount;
        frameDesc.initialization = FrameResourceInitialization::ImportedContents;
        const FrameResourceDesc resourceDesc = FrameResourceDesc::Texture(frameDesc);
        detail::ResolveScratch& registrationScratch = owner->resolveScratch;
        u32 existingIndex = InvalidRenderFlowResourceIndex;
        if (registrationScratch.retainedImportTokens.Find(desc.token.value, existingIndex))
        {
            if (existingIndex >= owner->retainedImports.Size())
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "retained texture token index is stale");
            const detail::RetainedImportRecord& existing = owner->retainedImports[existingIndex];
            if (existing.texture.GetRef() != desc.texture || !detail::ResourceDescEqual(existing.desc, resourceDesc) || existing.initialState != desc.initialState ||
                existing.terminalState != desc.terminalState || existing.initialQueue != desc.initialQueue || existing.terminalQueue != desc.terminalQueue ||
                existing.readiness != desc.readiness || existing.incomingWait != desc.incomingWait)
                return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "repeated external texture token disagrees with its registered contract");
            imported = {existingIndex, owner->sessionGeneration};
            return true;
        }
        const u64 resourceKey = rhi::ResourceRef(desc.texture).value;
        if (registrationScratch.retainedImportResources.Find(resourceKey, existingIndex))
            return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "one external texture cannot be registered under multiple tokens");
        if (owner->retainedImports.Size() >= owner->config.maximumRetainedImports)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "retained import capacity is exhausted");
        registrationScratch.retainedImportTokens.Reserve(owner->retainedImports.Size() + 1u);
        registrationScratch.retainedImportResources.Reserve(owner->retainedImports.Size() + 1u);
        detail::RetainedImportRecord& record = owner->retainedImports.EmplaceBack();
        record.token = desc.token;
        record.desc = resourceDesc;
        record.texture = rhi::Texture(desc.texture);
        record.readiness = desc.readiness;
        record.incomingWait = desc.incomingWait;
        record.initialState = desc.initialState;
        record.terminalState = desc.terminalState;
        record.initialQueue = desc.initialQueue;
        record.terminalQueue = desc.terminalQueue;
        const u32 importIndex = owner->retainedImports.Size() - 1u;
        if (!registrationScratch.retainedImportTokens.Insert(desc.token.value, importIndex))
        {
            static_cast<void>(owner->retainedImports.PopBack());
            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "retained texture token index rejected a prevalidated identity");
        }
        if (!registrationScratch.retainedImportResources.Insert(resourceKey, importIndex))
        {
            static_cast<void>(registrationScratch.retainedImportTokens.Erase(desc.token.value));
            static_cast<void>(owner->retainedImports.PopBack());
            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "retained texture resource index rejected a prevalidated identity");
        }
        imported = {importIndex, owner->sessionGeneration};
        return true;
    }

    bool RenderFlowResourceAllocator::RegisterImport(const RetainedBufferImportDesc& desc, ImportedResourceId& imported, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        imported = {};
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not accepting imports");
        concurrency::ScopedLock<concurrency::SpinLock> guard(owner->writerLock);
        if (owner->preparedPlanningGroupCount != 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "retained imports must be registered before creating planning writers");
        if (!desc.token.IsValid() || !desc.buffer.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "retained buffer import identity is invalid");
        const bool readinessValid = ValidImportReadiness(desc.readiness, desc.incomingWait, desc.initialQueue);
        if (!readinessValid)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "import readiness requires either same-queue continuation or a fence from its initial queue");
        if (!detail::ValidQueue(desc.initialQueue) || !detail::ValidQueue(desc.terminalQueue) || desc.terminalQueue == rhi::QueueType::Copy)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "retained import must terminate on Graphics or Compute");
        rhi::BufferDesc actual;
        rhi::Failure rhiFailure;
        const bool descriptorAvailable = rhi::GetBufferDesc(desc.buffer, actual, &rhiFailure);
        if (!descriptorAvailable)
            return detail::Fail(failure, detail::MapRhiFailure(rhiFailure, detail::RhiFailureContext::ImportedIdentity), owner->state,
                                rhiFailure.message[0] != '\0' ? rhiFailure.message : "retained buffer descriptor query failed");
        if (actual.virtualResource || !detail::PhysicalBufferDescEqual(actual, desc.expected))
            return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "retained buffer's authoritative descriptor does not match the expected contract");
        if (!detail::BufferStateAllowed(actual, desc.initialState) || !detail::BufferStateAllowed(actual, desc.terminalState) ||
            !detail::QueueStateAllowed(desc.initialQueue, FrameResourceKind::Buffer, desc.initialState) || !detail::QueueStateAllowed(desc.terminalQueue, FrameResourceKind::Buffer, desc.terminalState))
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidUseOrScope, owner->state, "retained buffer initial or terminal state is incompatible with its descriptor or queue");

        FrameBufferDesc frameDesc;
        frameDesc.active = actual;
        frameDesc.maximumSize = actual.size;
        frameDesc.initialization = FrameResourceInitialization::ImportedContents;
        const FrameResourceDesc resourceDesc = FrameResourceDesc::Buffer(frameDesc);
        detail::ResolveScratch& registrationScratch = owner->resolveScratch;
        u32 existingIndex = InvalidRenderFlowResourceIndex;
        if (registrationScratch.retainedImportTokens.Find(desc.token.value, existingIndex))
        {
            if (existingIndex >= owner->retainedImports.Size())
                return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "retained buffer token index is stale");
            const detail::RetainedImportRecord& existing = owner->retainedImports[existingIndex];
            if (existing.buffer.GetRef() != desc.buffer || !detail::ResourceDescEqual(existing.desc, resourceDesc) || existing.initialState != desc.initialState ||
                existing.terminalState != desc.terminalState || existing.initialQueue != desc.initialQueue || existing.terminalQueue != desc.terminalQueue ||
                existing.readiness != desc.readiness || existing.incomingWait != desc.incomingWait)
                return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "repeated external buffer token disagrees with its registered contract");
            imported = {existingIndex, owner->sessionGeneration};
            return true;
        }
        const u64 resourceKey = rhi::ResourceRef(desc.buffer).value;
        if (registrationScratch.retainedImportResources.Find(resourceKey, existingIndex))
            return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "one external buffer cannot be registered under multiple tokens");
        if (owner->retainedImports.Size() >= owner->config.maximumRetainedImports)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "retained import capacity is exhausted");
        registrationScratch.retainedImportTokens.Reserve(owner->retainedImports.Size() + 1u);
        registrationScratch.retainedImportResources.Reserve(owner->retainedImports.Size() + 1u);
        detail::RetainedImportRecord& record = owner->retainedImports.EmplaceBack();
        record.token = desc.token;
        record.desc = resourceDesc;
        record.buffer = rhi::Buffer(desc.buffer);
        record.readiness = desc.readiness;
        record.incomingWait = desc.incomingWait;
        record.initialState = desc.initialState;
        record.terminalState = desc.terminalState;
        record.initialQueue = desc.initialQueue;
        record.terminalQueue = desc.terminalQueue;
        const u32 importIndex = owner->retainedImports.Size() - 1u;
        if (!registrationScratch.retainedImportTokens.Insert(desc.token.value, importIndex))
        {
            static_cast<void>(owner->retainedImports.PopBack());
            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "retained buffer token index rejected a prevalidated identity");
        }
        if (!registrationScratch.retainedImportResources.Insert(resourceKey, importIndex))
        {
            static_cast<void>(registrationScratch.retainedImportTokens.Erase(desc.token.value));
            static_cast<void>(owner->retainedImports.PopBack());
            return detail::Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, owner->state, "retained buffer resource index rejected a prevalidated identity");
        }
        imported = {importIndex, owner->sessionGeneration};
        return true;
    }

    bool RenderFlowResourceAllocator::ReserveExportSlot(ExportSlotId& slot, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        slot = {};
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not accepting export slots");
        concurrency::ScopedLock<concurrency::SpinLock> guard(owner->writerLock);
        if (owner->preparedPlanningGroupCount != 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "terminal export slots must be reserved before creating planning writers");
        if (owner->reservedExportSlots >= owner->config.maximumTerminalExports)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "terminal export slot capacity is exhausted");
        slot = {owner->reservedExportSlots++, owner->sessionGeneration};
        return true;
    }

    bool RenderFlowResourceAllocator::PreparePlanningGroups(const u32 groupCount, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not planning");
        if (groupCount == 0 || groupCount > owner->config.maximumPlanningWriters)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "render-flow group count exceeds the configured planning capacity");
        if (owner->preparedPlanningGroupCount != 0)
        {
            if (owner->preparedPlanningGroupCount == groupCount)
                return true;
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render-flow group storage was already prepared with a different count");
        }

        // Writer control blocks are pointer-stable for the allocator lifetime.
        // A stale old-generation handle may outlive cancellation, so this array
        // must never move after the first writer can be published.
        if (owner->writerImpls.Empty())
        {
            owner->writerImpls.Resize(owner->config.maximumPlanningWriters);
            if (owner->writerImpls.Size() != owner->config.maximumPlanningWriters)
                return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "planning writer control-block storage allocation failed");
            for (u32 index = 0; index < owner->writerImpls.Size(); ++index)
                owner->writerImpls[index].primaryIndex = index;
        }

        // Keep the reservation/request arrays at the current graph's logical
        // breadth. DynArray downsizing destroys the inactive tail (releasing its
        // nested recording backing) without downsizing the outer buffer, so a
        // one-frame graph spike is paid down once instead of rescanned on every
        // later small frame. Growing back within the retained outer capacity is
        // allocation-free. writerImpls stays allocator-lifetime fixed because
        // stale planning-writer handles require those control blocks to remain
        // pointer-stable; stale generations never dereference an old reservation.
        owner->writerReservations.Resize(groupCount);
        owner->queueRequestGroups.Resize(groupCount);
        if (owner->writerReservations.Size() < groupCount || owner->queueRequestGroups.Size() < groupCount)
        {
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "render-flow group storage allocation failed");
        }
        owner->preparedPlanningGroupCount = groupCount;
        return true;
    }

    bool RenderFlowResourceAllocator::CreatePlanningWriter(const RenderFlowNodeId node, const GpuFlowGroupId flowGroup, const CommandScopeId commandScope,
                                                           ResourcePlanningWriter& writer, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (writer.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, GetState(), "output planning writer is already active", node);
        if (writer.m_impl != nullptr)
            writer.Abandon();
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized", node);
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not planning", node);
        if (!node.IsValid() || !flowGroup.IsValid() || !commandScope.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "planning writer identity is invalid", node);
        if (owner->preparedPlanningGroupCount == 0 && !PreparePlanningGroups(owner->config.maximumPlanningWriters, failure))
            return false;
        if (flowGroup.value >= owner->preparedPlanningGroupCount)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "planning writer flow group exceeds the prepared group storage", node);
        const u32 generation = owner->sessionGeneration;

        detail::WriterReservation& reservation = owner->writerReservations[flowGroup.value];
        if (reservation.node.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "GPU flow group already owns a planning writer", node);

        ResourcePlanningWriter::Impl* const impl = AcquireWriterImpl(*owner, flowGroup);
        if (impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "planning writer metadata allocation failed", node);
        impl->owner = owner;
        impl->reservation = &reservation;
        impl->node = node;
        impl->flowGroup = flowGroup;
        impl->sessionGeneration = generation;
        impl->closed = false;

        reservation.batch.Reset(node, flowGroup, commandScope, generation);
        reservation.node = node;
        reservation.flowGroup = flowGroup;
        reservation.open = true;
        reservation.failed = false;
        ++owner->stats.planningWriters;
        writer.m_impl = impl;
        detail::RetainAllocator(owner);
        return true;
    }

    bool RenderFlowResourceAllocator::RequestBeginQueue(const rhi::QueueType queue, const GpuFlowGroupId flowGroup, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not accepting queue requests");
        if (!flowGroup.IsValid() || !detail::ValidQueue(queue))
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "queue request contains an invalid queue or GPU flow group");
        if (owner->preparedPlanningGroupCount == 0 && !PreparePlanningGroups(owner->config.maximumPlanningWriters, failure))
            return false;
        if (flowGroup.value >= owner->preparedPlanningGroupCount)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "queue request flow group exceeds the prepared group storage");
        detail::QueueRequestGroup& group = owner->queueRequestGroups[flowGroup.value];
        if (group.count != 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "GPU flow group already owns a queue or synchronization request");
        group.requests[group.count++] = {flowGroup, queue, rhi::CommandListSyncType::None, detail::QueueRequestKind::Begin};
        static_cast<void>(owner->recordedQueueRequestCount.Increment());
        static_cast<void>(owner->recordedQueueGroupCount.Increment());
        return true;
    }

    bool RenderFlowResourceAllocator::RequestEndQueue(const GpuFlowGroupId flowGroup, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not accepting queue requests");
        if (!flowGroup.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "queue-end request contains an invalid GPU flow group");
        if (owner->preparedPlanningGroupCount == 0 && !PreparePlanningGroups(owner->config.maximumPlanningWriters, failure))
            return false;
        if (flowGroup.value >= owner->preparedPlanningGroupCount)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "queue request flow group exceeds the prepared group storage");
        detail::QueueRequestGroup& group = owner->queueRequestGroups[flowGroup.value];
        if (group.count != 1 || group.requests[0].kind != detail::QueueRequestKind::Begin)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "GPU flow group queue was ended without a matching begin");
        group.requests[group.count++] = {flowGroup, group.requests[0].queue, rhi::CommandListSyncType::None, detail::QueueRequestKind::End};
        static_cast<void>(owner->recordedQueueRequestCount.Increment());
        return true;
    }

    bool RenderFlowResourceAllocator::RequestQueueSync(const GpuFlowGroupId flowGroup, const rhi::CommandListSyncType sync, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not accepting queue requests");
        if (!flowGroup.IsValid() || (sync != rhi::CommandListSyncType::None && sync != rhi::CommandListSyncType::ForkAsyncCompute && sync != rhi::CommandListSyncType::JoinAsyncCompute))
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "queue synchronization request is invalid");
        if (owner->preparedPlanningGroupCount == 0 && !PreparePlanningGroups(owner->config.maximumPlanningWriters, failure))
            return false;
        if (flowGroup.value >= owner->preparedPlanningGroupCount)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "queue request flow group exceeds the prepared group storage");
        detail::QueueRequestGroup& group = owner->queueRequestGroups[flowGroup.value];
        if (group.count != 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "GPU flow group already owns a queue or synchronization request");
        group.requests[group.count++] = {flowGroup, rhi::QueueType::Graphics, sync, detail::QueueRequestKind::Sync};
        static_cast<void>(owner->recordedQueueRequestCount.Increment());
        static_cast<void>(owner->recordedQueueGroupCount.Increment());
        return true;
    }

    bool RenderFlowResourceAllocator::SealPlanning(const PlanningJoinToken join, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not planning");
        if (!join.IsReady())
            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompletePlanning, owner->state, "every planning job and writer must complete successfully before sealing");

        // RED records useful request cardinality and ordering metadata while the
        // per-group tapes are still the authoritative source. Seal in one
        // ascending group walk as well: validate writer identity, gather every
        // aggregate count, and preserve the dense writer/group stream that
        // Resolve will consume. Rejected seals never mutate candidate tapes.
        owner->sealedWriters.Clear();
        owner->sealedQueueGroups.Clear();
        owner->sealedPlanning.Reset();
        detail::ResolveScratchHashSet& planningNodeIds = owner->resolveScratch.identityIds;
        planningNodeIds.Reset();
        owner->sealedWriters.Reserve(owner->stats.planningWriters);
        owner->sealedQueueGroups.Reserve(owner->recordedQueueGroupCount.GetValue());
        planningNodeIds.Reserve(owner->stats.planningWriters);
        const auto failSeal = [&](const RenderFlowResourceFailureCode code, const char* const message, const RenderFlowNodeId node = {}) noexcept
        {
            owner->sealedWriters.Clear();
            owner->sealedQueueGroups.Clear();
            owner->sealedPlanning.Reset();
            planningNodeIds.Reset();
            return detail::Fail(failure, code, owner->state, message, node);
        };

        u64 aggregateOperationCount = owner->recordedQueueRequestCount.GetValue();
        u64 logicalResourceCount = 0;
        u64 viewCount = 0;
        u64 declarationCount = 0;
        u64 textureViewCount = 0;
        u64 bufferViewCount = 0;
        u64 useBeginCount = 0;
        u64 explicitScopeOpenCount = 0;
        u64 exportCount = 0;
        u64 resourceSlotOffset = 0;
        u64 textureViewOffset = 0;
        u64 bufferViewOffset = 0;
        u64 useRecordOffset = 0;
        for (u32 groupIndex = 0; groupIndex < owner->preparedPlanningGroupCount; ++groupIndex)
        {
            const detail::QueueRequestGroup& queueGroup = owner->queueRequestGroups[groupIndex];
            if (queueGroup.count != 0)
            {
                const u32 queueIndex = owner->sealedQueueGroups.Size();
                owner->sealedQueueGroups.PushBack(groupIndex);
                if (owner->sealedQueueGroups.Size() != queueIndex + 1u)
                    return failSeal(RenderFlowResourceFailureCode::CapacityExceeded, "sealed queue-group stream allocation failed");
            }
            const detail::WriterReservation& reservation = owner->writerReservations[groupIndex];
            if (!reservation.node.IsValid())
                continue;
            if (reservation.open || reservation.failed)
                return failSeal(RenderFlowResourceFailureCode::IncompletePlanning, "every planning job and writer must complete successfully before sealing");
            if (!reservation.flowGroup.IsValid() || reservation.flowGroup.value != groupIndex || reservation.batch.flowGroup != reservation.flowGroup || reservation.batch.node != reservation.node)
                return failSeal(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "planning writer was published to the wrong render-flow group", reservation.node);
            if (planningNodeIds.Contains(reservation.node.value))
                return failSeal(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "render-flow node owns more than one planning group", reservation.node);
            if (!planningNodeIds.Insert(reservation.node.value))
                return failSeal(RenderFlowResourceFailureCode::CapacityExceeded, "planning-writer identity index allocation failed", reservation.node);

            const u32 sealedIndex = owner->sealedWriters.Size();
            detail::SealedWriterRecord record;
            record.groupIndex = groupIndex;
            record.resourceSlotOffset = resourceSlotOffset <= 0xffffffffull ? static_cast<u32>(resourceSlotOffset) : InvalidRenderFlowResourceIndex;
            record.textureViewOffset = textureViewOffset <= 0xffffffffull ? static_cast<u32>(textureViewOffset) : InvalidRenderFlowResourceIndex;
            record.bufferViewOffset = bufferViewOffset <= 0xffffffffull ? static_cast<u32>(bufferViewOffset) : InvalidRenderFlowResourceIndex;
            record.useRecordOffset = useRecordOffset <= 0xffffffffull ? static_cast<u32>(useRecordOffset) : InvalidRenderFlowResourceIndex;
            owner->sealedWriters.PushBack(record);
            if (owner->sealedWriters.Size() != sealedIndex + 1u)
                return failSeal(RenderFlowResourceFailureCode::CapacityExceeded, "sealed planning writer stream allocation failed", reservation.node);

            const detail::CandidateWriterBatch& batch = reservation.batch;
            aggregateOperationCount += batch.semanticSequenceCount;
            logicalResourceCount += batch.resourceCount;
            viewCount += static_cast<u64>(batch.textureViews.Size()) + batch.bufferViews.Size();
            declarationCount += batch.declarationCount;
            textureViewCount += batch.textureViews.Size();
            bufferViewCount += batch.bufferViews.Size();
            useBeginCount += batch.uses.Size();
            explicitScopeOpenCount += batch.explicitScopeOpenCount;
            exportCount += batch.exportCount;
            resourceSlotOffset += batch.resourceCount;
            textureViewOffset += batch.textureViews.Size();
            bufferViewOffset += batch.bufferViews.Size();
            useRecordOffset += batch.uses.Size();
        }
        if (aggregateOperationCount > owner->config.maximumOperations || logicalResourceCount > owner->config.maximumLogicalResources || viewCount > owner->config.maximumViews)
            return failSeal(RenderFlowResourceFailureCode::CapacityExceeded, "sealed frame planning metadata exceeds an aggregate allocator capacity");

        detail::SealedPlanningSummary& sealed = owner->sealedPlanning;
        sealed.declarationCount = static_cast<u32>(declarationCount);
        sealed.resourceSlotCount = static_cast<u32>(logicalResourceCount);
        sealed.textureViewCount = static_cast<u32>(textureViewCount);
        sealed.bufferViewCount = static_cast<u32>(bufferViewCount);
        sealed.useBeginCount = static_cast<u32>(useBeginCount);
        sealed.explicitScopeOpenCount = static_cast<u32>(explicitScopeOpenCount);
        sealed.exportCount = static_cast<u32>(exportCount);
        owner->state = RenderFlowResourceSessionState::CandidatesSealed;
        owner->stats.state = owner->state;
        return true;
    }

    bool RenderFlowResourceAllocator::Resolve(jobs::Builder* const, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (owner->state != RenderFlowResourceSessionState::CandidatesSealed)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator candidates are not sealed");
        return detail::ResolveFrame(*owner, failure);
    }

    void RenderFlowResourceAllocator::CancelBeforePublication() noexcept
    {
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return;
        if (owner->state == RenderFlowResourceSessionState::Planning || owner->state == RenderFlowResourceSessionState::CandidatesSealed || owner->state == RenderFlowResourceSessionState::Resolving)
            detail::CancelSession(*owner, owner->sessionGeneration);
    }

    void RenderFlowResourceAllocator::AbandonPublishedExecution() noexcept
    {
        if (m_impl != nullptr)
            detail::AbandonAllocatorSession(*m_impl, m_impl->sessionGeneration);
    }

    RenderFlowResourceAllocator::~RenderFlowResourceAllocator()
    {
        if (m_impl == nullptr)
            return;
        detail::AbandonAllocatorSession(*m_impl, m_impl->sessionGeneration);
        if (m_impl->publishedGeneration != nullptr ||
            (m_impl->state != RenderFlowResourceSessionState::Idle && m_impl->state != RenderFlowResourceSessionState::DeviceUnavailable))
        {
            Impl* const stranded = m_impl;
            m_impl = nullptr;
            static_cast<void>(stranded);
            VG_FATAL("render flow allocator destructor could not establish a safe terminal state; allocator ownership was stranded");
            return;
        }
        RenderFlowResourceFailure failure;
        if (!Shutdown(&failure))
        {
            Impl* const stranded = m_impl;
            m_impl = nullptr;
            static_cast<void>(stranded);
            VG_FATAL("render flow allocator destructor could not gracefully retire persistent resource ownership; allocator ownership was stranded");
        }
    }

    bool RenderFlowResourceAllocator::Initialize(const RenderFlowResourceAllocatorConfig& config, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl != nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::AlreadyInitialized, m_impl->state, "render flow resource allocator is already initialized");
        if (config.maximumPlanningWriters == 0 || config.maximumOperationsPerPlanningWriter == 0 || config.maximumOperationsPerPlanningWriter > config.maximumOperations || config.maximumOperations == 0 ||
            config.maximumLogicalResources == 0 || config.maximumViews == 0 || config.maximumTrackedTextureSubresources == 0 ||
            config.maximumExecutionPackets == 0 || config.maximumCompiledResourceActions == 0 || config.maximumCommandScopeEntryStates == 0 || config.maximumRetainedImports == 0 || config.maximumTerminalExports == 0 ||
            config.maximumOutstandingPublishedExports == 0 || config.hardNativeByteLimit == 0 || config.descriptorCacheTarget > config.descriptorCacheMaximum)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidConfiguration, RenderFlowResourceSessionState::Idle, "render flow resource allocator configuration is invalid");
        if (config.resourceDescriptors.IsValid())
        {
            if (!rhi::IsInitialized())
                return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle,
                                    "render flow resource descriptor domain requires an initialized RHI");
            const rhi::DescriptorDomainStats descriptorStats = rhi::GetDescriptorDomainStats(config.resourceDescriptors);
            if (descriptorStats.capacity == 0 || config.descriptorCacheMaximum > descriptorStats.capacity)
                return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidConfiguration, RenderFlowResourceSessionState::Idle,
                                    "render flow descriptor cache maximum exceeds the configured descriptor domain capacity");
        }
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, RenderFlowResourceSessionState::Idle, "render flow resource allocator metadata allocation failed");
        m_impl = new (block.address) Impl(config);
        return true;
    }

    bool RenderFlowResourceAllocator::Shutdown(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if ((m_impl->state != RenderFlowResourceSessionState::Idle && m_impl->state != RenderFlowResourceSessionState::DeviceUnavailable) || m_impl->publishedGeneration != nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, m_impl->state, "render flow resource allocator still owns an active frame session");
        Impl* const impl = m_impl;
        if (!rhi::IsInitialized())
        {
            const detail::AllocatorNativeByteLedgerStats native = impl->nativeByteLedger.GetStats();
            const detail::AllocatorDescriptorLedgerStats descriptors = impl->descriptorLedger.GetStats();
            const bool ownsNoRhiState = !impl->resourceDescriptors.IsValid() && native.chargedBytes == 0 && descriptors.liveSlots == 0 &&
                                        impl->retainedImports.Empty() && impl->publishedExports.Empty();
            if (!ownsNoRhiState)
                return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, impl->state,
                                    "render flow resource allocator must shut down while the RHI is still initialized");

            // Logical-only validation allocators can legitimately outlive an RHI
            // session because they never own native resources, descriptor-domain
            // references, imports, or exports. Releasing this metadata-only
            // implementation is safe without touching RHI teardown APIs.
            m_impl = nullptr;
            detail::ReleaseAllocator(impl);
            return true;
        }

        const auto commitDeviceLoss = [&]() noexcept
        {
            impl->publishedExports.Clear();
            impl->publishedExportIndices.Reset();
            impl->structuralPlan.Clear();
            detail::ReleasePersistentDedicatedPlan(*impl, false);
            impl->dedicatedPool.DeviceLost();
            impl->placedPool.DeviceLost();
            impl->state = RenderFlowResourceSessionState::DeviceUnavailable;
            impl->stats.state = impl->state;
            m_impl = nullptr;
            detail::ReleaseAllocator(impl);
            return true;
        };
        if (ActualDeviceLoss())
            return commitDeviceLoss();

        bool descriptorDeviceLost = false;
        if (!impl->dedicatedPool.PrepareHealthyShutdown(descriptorDeviceLost, failure))
        {
            if (descriptorDeviceLost || ActualDeviceLoss())
                return commitDeviceLoss();
            return false;
        }
        bool placedDescriptorDeviceLost = false;
        if (!impl->placedPool.PrepareHealthyShutdown(placedDescriptorDeviceLost, failure))
        {
            if (placedDescriptorDeviceLost || ActualDeviceLoss())
                return commitDeviceLoss();
            return false;
        }
        rhi::Failure drainFailure;
        bool drained = false;
        if (impl->shutdownFailureInjection.point == detail::RenderFlowResourceShutdownFailurePoint::RetirementFlush)
        {
            impl->shutdownFailureInjection = {};
            drainFailure.code = rhi::FailureCode::BackendFailure;
            drained = false;
        }
        else
        {
            drained = rhi::FlushRetiredResources(&drainFailure);
        }
        if (!drained)
        {
            if (drainFailure.code == rhi::FailureCode::DeviceLost || ActualDeviceLoss())
                return commitDeviceLoss();
            return detail::Fail(failure, detail::MapRhiFailure(drainFailure, detail::RhiFailureContext::BackendContract), impl->state,
                                drainFailure.message[0] != '\0' ? drainFailure.message : "render flow resource retirement drain failed");
        }
        // The shared flush is the final recoverable operation.  Preflight both
        // pools before releasing either owner so terminal commit itself cannot
        // leave the allocator half-committed.
        if (!impl->dedicatedPool.ValidateHealthyShutdownCommit(failure) || !impl->placedPool.ValidateHealthyShutdownCommit(failure))
        {
            if (ActualDeviceLoss())
                return commitDeviceLoss();
            return false;
        }
        impl->dedicatedPool.CommitHealthyShutdown();
        impl->placedPool.CommitHealthyShutdown();
        impl->publishedExports.Clear();
        impl->publishedExportIndices.Reset();
        impl->state = RenderFlowResourceSessionState::DeviceUnavailable;
        impl->stats.state = impl->state;
        m_impl = nullptr;
        detail::ReleaseAllocator(impl);
        return true;
    }

    bool RenderFlowResourceAllocator::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    RenderFlowResourceSessionState RenderFlowResourceAllocator::GetState() const noexcept
    {
        return m_impl != nullptr ? m_impl->state : RenderFlowResourceSessionState::Idle;
    }

    ExecutionGenerationId RenderFlowResourceAllocator::GetExecutionGeneration() const noexcept
    {
        return m_impl != nullptr && m_impl->publishedGeneration != nullptr ? m_impl->publishedGeneration->id : ExecutionGenerationId{};
    }

    bool RenderFlowResourceAllocator::ClearPersistentCaches(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (m_impl->state != RenderFlowResourceSessionState::Idle || m_impl->publishedGeneration != nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, m_impl->state, "persistent caches may be cleared only between frame sessions");
        if (!m_impl->placedPool.ClearPersistentCache(failure) || !m_impl->dedicatedPool.ClearPersistentCache(failure))
            return false;
        m_impl->structuralPlan.Clear();
        detail::ReleasePersistentDedicatedPlan(*m_impl, false);
        return true;
    }

    bool RenderFlowResourceAllocator::BeginFrame(const u64 frameSerial, const FrameResourcePolicy& policy, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (m_impl->state != RenderFlowResourceSessionState::Idle || m_impl->publishedGeneration != nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, m_impl->state, "render flow resource allocator is not idle");
        if (!m_impl->config.allowLogicalOnlyValidation && !rhi::IsInitialized())
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, m_impl->state, "render flow resource allocator requires an initialized RHI for physical assignment");
        if (policy.enablePlacedResources && (m_impl->config.allowLogicalOnlyValidation || !rhi::GetCapabilities().placedResources.IsSupported()))
            return detail::Fail(failure, RenderFlowResourceFailureCode::UnsupportedCapability, m_impl->state, "placed resources require physical execution and a supported RHI placed-resource profile");
        if (m_impl->structuralPlan.valid &&
            (m_impl->structuralPlan.policy.enablePlacedResources != policy.enablePlacedResources ||
             m_impl->structuralPlan.policy.processEviction != policy.processEviction))
            m_impl->structuralPlan.Clear();

        if (!m_impl->config.allowLogicalOnlyValidation)
        {
            m_impl->dedicatedPool.Poll();
            m_impl->placedPool.Poll();
            if (policy.processEviction)
            {
                if (m_impl->descriptorLedger.GetStats().reusableSlots > m_impl->descriptorLedger.GetTarget())
                {
                    if (!detail::TrimCanonicalDescriptorCacheToTarget(*m_impl, failure))
                        return false;
                }
                const detail::AllocatorNativeByteLedgerStats charged = m_impl->nativeByteLedger.GetStats();
                if (charged.textureBytes > m_impl->config.textureSoftTargetBytes || charged.bufferBytes > m_impl->config.bufferSoftTargetBytes)
                {
                    detail::ResourcePoolTrimState trim{charged.textureBytes, charged.bufferBytes};
                    if (!m_impl->dedicatedPool.TrimToSoftTargets(trim, failure) || !m_impl->placedPool.TrimToSoftTargets(trim, failure))
                        return false;
                }
            }
        }

        detail::ResetPlanningStorage(*m_impl);
        m_impl->retainedImports.Clear();
        m_impl->reservedExportSlots = 0;
        m_impl->frameSerial = frameSerial;
        m_impl->activePolicy = policy;
        m_impl->sessionGeneration = detail::NextGeneration(m_impl->sessionGeneration);
        m_impl->state = RenderFlowResourceSessionState::Planning;
        ++m_impl->stats.begunFrames;
        m_impl->stats.planningWriters = 0;
        m_impl->stats.logicalAllocations = 0;
        m_impl->stats.compiledPackets = 0;
        m_impl->stats.retainedImports = 0;
        m_impl->stats.state = m_impl->state;
        return true;
    }

    RenderFlowResourceAllocatorStats RenderFlowResourceAllocator::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->writerLock);
        RenderFlowResourceAllocatorStats stats = m_impl->stats;
        stats.rejectedOperations += m_impl->pendingRejectedOperations.GetValue();
        const detail::DedicatedResourcePoolStats pool = m_impl->dedicatedPool.GetStats();
        const detail::PlacedResourcePoolStats placed = m_impl->placedPool.GetStats();
        const detail::AllocatorNativeByteLedgerStats ledger = m_impl->nativeByteLedger.GetStats();
        const detail::AllocatorDescriptorLedgerStats descriptorLedger = m_impl->descriptorLedger.GetStats();
        stats.chargedNativeBytes = ledger.chargedBytes;
        stats.chargedTextureBytes = ledger.textureBytes;
        stats.chargedBufferBytes = ledger.bufferBytes;
        stats.canonicalDescriptorLiveSlots = descriptorLedger.liveSlots;
        stats.canonicalDescriptorReusableSlots = descriptorLedger.reusableSlots;
        stats.canonicalDescriptorPeakLiveSlots = descriptorLedger.peakLiveSlots;
        stats.canonicalDescriptorPeakReusableSlots = descriptorLedger.peakReusableSlots;
        stats.canonicalDescriptorPublicationHits = descriptorLedger.publicationHits;
        stats.canonicalDescriptorPublicationMisses = descriptorLedger.publicationMisses;
        stats.canonicalDescriptorPressureEvictions = descriptorLedger.pressureEvictions;
        stats.canonicalDescriptorMaintenanceEvictions = descriptorLedger.maintenanceEvictions;
        stats.canonicalDescriptorRejectedReservations = descriptorLedger.rejectedReservations;
        stats.dedicatedResourcePoolHits = pool.hits;
        stats.dedicatedResourcePoolMisses = pool.misses;
        stats.placedHeapPoolHits = placed.heapHits;
        stats.placedHeapPoolMisses = placed.heapMisses;
        stats.placedObjectPoolHits = placed.objectHits;
        stats.placedObjectPoolMisses = placed.objectMisses;
        stats.pendingRetirementResources = pool.pendingRetirement;
        stats.pendingNativeDestructionResources = pool.pendingNativeDestruction;
        stats.pendingRetirementPlacedHeaps = placed.pendingRetirementHeaps;
        stats.pendingNativeDestructionPlacedHeaps = placed.pendingNativeDestructionHeaps;
        stats.outstandingPublishedExports = m_impl->publishedExports.Size();
        stats.state = m_impl->state;
        return stats;
    }

    void detail::RetainAllocator(RenderFlowResourceAllocator::Impl* const allocator) noexcept
    {
        if (allocator != nullptr)
            static_cast<void>(allocator->references.Increment());
    }

    void detail::DedicatedResourceProviderTestAccess::Set(RenderFlowResourceAllocator& allocator, const DedicatedResourceProviderFailureInjection& injection) noexcept
    {
        if (allocator.m_impl != nullptr)
            allocator.m_impl->dedicatedPool.SetProviderFailureInjection(injection);
    }

    void detail::DedicatedResourceProviderTestAccess::Clear(RenderFlowResourceAllocator& allocator) noexcept
    {
        if (allocator.m_impl != nullptr)
            allocator.m_impl->dedicatedPool.ClearProviderFailureInjection();
    }

    void detail::RenderFlowResourceShutdownTestAccess::Set(RenderFlowResourceAllocator& allocator,
                                                           const RenderFlowResourceShutdownFailureInjection& injection) noexcept
    {
        if (allocator.m_impl != nullptr)
            allocator.m_impl->shutdownFailureInjection = injection;
    }

    void detail::RenderFlowResourceShutdownTestAccess::Clear(RenderFlowResourceAllocator& allocator) noexcept
    {
        if (allocator.m_impl != nullptr)
            allocator.m_impl->shutdownFailureInjection = {};
    }

    void detail::RenderFlowResourceShutdownTestAccess::AbandonPublishedExecution(RenderFlowResourceAllocator& allocator) noexcept
    {
        allocator.AbandonPublishedExecution();
    }

    bool detail::RenderFlowResourceShutdownTestAccess::HasStructuralPlan(const RenderFlowResourceAllocator& allocator) noexcept
    {
        return allocator.m_impl != nullptr && allocator.m_impl->structuralPlan.valid;
    }

    void detail::ReleaseAllocator(RenderFlowResourceAllocator::Impl* const allocator) noexcept
    {
        if (allocator == nullptr || allocator->references.Decrement() != 0)
            return;
        allocator->~Impl();
        memory::MemoryBlock block{allocator, sizeof(RenderFlowResourceAllocator::Impl), memory::PoolId::Rendering};
        memory::Free(block);
    }

    namespace
    {
        [[nodiscard]] bool CanonicalCandidateLess(const detail::CanonicalDescriptorEvictionCandidate& left,
                                                  const detail::CanonicalDescriptorEvictionCandidate& right) noexcept
        {
            if (!right.IsValid())
                return left.IsValid();
            if (!left.IsValid())
                return false;
            if (left.lastDemandFrame != right.lastDemandFrame)
                return left.lastDemandFrame < right.lastDemandFrame;
            if (left.ownerClass != right.ownerClass)
                return static_cast<u8>(left.ownerClass) < static_cast<u8>(right.ownerClass);
            return left.entry < right.entry;
        }

        [[nodiscard]] bool SelectCanonicalDescriptorVictim(RenderFlowResourceAllocator::Impl& allocator,
                                                           detail::CanonicalDescriptorEvictionCandidate& selected) noexcept
        {
            selected = {};
            detail::CanonicalDescriptorEvictionCandidate dedicated;
            detail::CanonicalDescriptorEvictionCandidate placed;
            static_cast<void>(allocator.dedicatedPool.GetColdestCanonicalDescriptorCandidate(dedicated));
            static_cast<void>(allocator.placedPool.GetColdestCanonicalDescriptorCandidate(placed));
            if (CanonicalCandidateLess(dedicated, placed))
                selected = dedicated;
            else if (placed.IsValid())
                selected = placed;
            return selected.IsValid();
        }

        [[nodiscard]] detail::CanonicalDescriptorEvictionResult EvictCanonicalDescriptorVictim(RenderFlowResourceAllocator::Impl& allocator,
                                                                                                 const detail::CanonicalDescriptorEvictionCandidate& candidate,
                                                                                                 const bool pressure,
                                                                                                 RenderFlowResourceFailure* const failure) noexcept
        {
            return candidate.ownerClass == detail::CanonicalDescriptorOwnerClass::Dedicated
                       ? allocator.dedicatedPool.EvictCanonicalDescriptorCandidate(candidate, pressure, failure)
                       : allocator.placedPool.EvictCanonicalDescriptorCandidate(candidate, pressure, failure);
        }

        void RefreshCanonicalDescriptorVictims(RenderFlowResourceAllocator::Impl& allocator) noexcept
        {
            allocator.dedicatedPool.RefreshCanonicalDescriptorCandidates();
            allocator.placedPool.RefreshCanonicalDescriptorCandidates();
        }
    } // namespace

    bool detail::RelieveCanonicalDescriptorPressure(RenderFlowResourceAllocator::Impl& allocator, const CanonicalDescriptorPressure& pressure,
                                                     RenderFlowResourceFailure* const failure) noexcept
    {
        if (!pressure.IsRequired() || !allocator.resourceDescriptors.IsValid())
            return true;
        RefreshCanonicalDescriptorVictims(allocator);
        for (;;)
        {
            const AllocatorDescriptorLedgerStats stats = allocator.descriptorLedger.GetStats();
            const bool relieved = pressure.kind == CanonicalDescriptorPressureKind::LocalLimit
                                      ? allocator.descriptorLedger.CanReserve(pressure.requiredSlots) && stats.reusableSlots <= allocator.descriptorLedger.GetTarget()
                                      : rhi::GetDescriptorDomainStats(allocator.resourceDescriptors.GetRef()).free >= pressure.requiredSlots;
            if (relieved)
                return true;

            CanonicalDescriptorEvictionCandidate candidate;
            if (!SelectCanonicalDescriptorVictim(allocator, candidate))
                return Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, RenderFlowResourceSessionState::Resolving,
                            pressure.kind == CanonicalDescriptorPressureKind::LocalLimit ? "canonical descriptor cache hard limit is exhausted"
                                                                                         : "canonical descriptor shared domain is exhausted");
            const CanonicalDescriptorEvictionResult result = EvictCanonicalDescriptorVictim(allocator, candidate, true, failure);
            if (result == CanonicalDescriptorEvictionResult::Failed)
                return false;
            // Skipped candidates are re-queried. Revalidation demotes them out of
            // the reusable descriptor set, so this loop still makes progress
            // without holding either pool lock across the other pool call.
        }
    }

    bool detail::TrimCanonicalDescriptorCacheToTarget(RenderFlowResourceAllocator::Impl& allocator, RenderFlowResourceFailure* const failure) noexcept
    {
        if (!allocator.resourceDescriptors.IsValid())
            return true;
        if (allocator.descriptorLedger.GetStats().reusableSlots <= allocator.descriptorLedger.GetTarget())
            return true;
        RefreshCanonicalDescriptorVictims(allocator);
        while (allocator.descriptorLedger.GetStats().reusableSlots > allocator.descriptorLedger.GetTarget())
        {
            CanonicalDescriptorEvictionCandidate candidate;
            if (!SelectCanonicalDescriptorVictim(allocator, candidate))
                return allocator.descriptorLedger.GetStats().reusableSlots <= allocator.descriptorLedger.GetTarget();
            const CanonicalDescriptorEvictionResult result = EvictCanonicalDescriptorVictim(allocator, candidate, false, failure);
            if (result == CanonicalDescriptorEvictionResult::Failed)
                return false;
        }
        return true;
    }

    void detail::ResetPlanningStorage(RenderFlowResourceAllocator::Impl& allocator, const bool retainBoundedRecording) noexcept
    {
        const u32 groupCount = allocator.preparedPlanningGroupCount;
        for (u32 groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            if (groupIndex < allocator.writerReservations.Size())
                ResetWriterReservation(allocator.writerReservations[groupIndex], retainBoundedRecording);
            if (groupIndex < allocator.queueRequestGroups.Size())
                allocator.queueRequestGroups[groupIndex].count = 0;
        }
        allocator.sealedWriters.Clear();
        allocator.sealedQueueGroups.Clear();
        allocator.sealedPlanning.Reset();
        allocator.resolveScratch.identityIds.Reset();
        allocator.resolveScratch.retainedImportTokens.Reset();
        allocator.resolveScratch.retainedImportResources.Reset();
        allocator.resolveScratch.presentationImports.Reset();
        allocator.recordedQueueRequestCount.SetValue(0);
        allocator.recordedQueueGroupCount.SetValue(0);
        allocator.preparedPlanningGroupCount = 0;
    }

    void detail::AbandonAllocatorSession(RenderFlowResourceAllocator::Impl& allocator, const u32 generation) noexcept
    {
        if (allocator.sessionGeneration != generation || allocator.state == RenderFlowResourceSessionState::Idle ||
            (allocator.state == RenderFlowResourceSessionState::DeviceUnavailable && allocator.publishedGeneration == nullptr))
            return;
        if (allocator.publishedGeneration == nullptr)
        {
            CancelSession(allocator, generation);
            return;
        }

        const detail::AllocatorNativeByteLedgerStats nativeOwnership = allocator.nativeByteLedger.GetStats();
        const detail::AllocatorDescriptorLedgerStats descriptorOwnership = allocator.descriptorLedger.GetStats();
        const bool metadataOnlyWithoutRhi = !rhi::IsInitialized() && allocator.config.allowLogicalOnlyValidation && !allocator.resourceDescriptors.IsValid() &&
                                            nativeOwnership.chargedBytes == 0 && descriptorOwnership.liveSlots == 0 && allocator.publishedExports.Empty() &&
                                            !allocator.publishedGeneration->hasNativeResourceBindings;
        const bool deviceLost = ActualDeviceLoss();
        rhi::ResidencyFenceSet submittedFences;
        if (!deviceLost && !metadataOnlyWithoutRhi)
        {
            if (!rhi::IsInitialized())
            {
                VG_FATAL("healthy render flow abandonment requires an initialized RHI; allocator ownership was preserved");
                return;
            }
            if (!rhi::GetSubmittedResidencyFences(submittedFences))
            {
                if (!ActualDeviceLoss())
                {
                    VG_FATAL("healthy render flow abandonment could not discover submitted GPU fences; allocator ownership was preserved");
                    return;
                }
            }
        }

        // The coordinator must join recording and its continuations before teardown.
        // This is quiescent abandonment, not cancellation of running workers.
        // Retained packet views may survive, but cannot start recording afterward.
        ExecutionGenerationRef::Impl* const published = allocator.publishedGeneration;
        {
            // Validate before invalidating any packet. The caller's job join is
            // still required: an unclaimed packet may belong to a queued job.
            for (detail::CompiledPacket& packet : published->packets)
            {
                if (packet.runtime.state == detail::PacketRuntimeState::Executing)
                    VG_FATAL("allocator teardown requires joined recording jobs and closed packet cursors");
            }
            for (detail::CompiledPacket& packet : published->packets)
            {
                if (packet.runtime.state == detail::PacketRuntimeState::Unclaimed)
                    packet.runtime.state = detail::PacketRuntimeState::Canceled;
                packet.runtime.activeUseCount = 0;
                if (packet.runtime.liveness != nullptr)
                    for (u8& active : packet.runtime.liveness->useActive)
                        active = 0;
            }
            published->terminal.SetValue(true);
        }
        allocator.publishedGeneration = nullptr;
        allocator.stats.rejectedOperations += allocator.pendingRejectedOperations.Exchange(0);
        ResetPlanningStorage(allocator);
        allocator.retainedImports.Clear();
        allocator.publishedExports.Clear();
        allocator.publishedExportIndices.Reset();
        allocator.reservedExportSlots = 0;
        allocator.activePolicy = {};
        const bool nowDeviceLost = deviceLost || ActualDeviceLoss();
        RenderFlowResourceFailure descriptorRetirementFailure;
        if (!RetireRetainedImportDescriptors(allocator, *published, submittedFences, nowDeviceLost, &descriptorRetirementFailure) && !nowDeviceLost)
            VG_FATAL(descriptorRetirementFailure.message != nullptr ? descriptorRetirementFailure.message
                                                                    : "healthy render flow abandonment could not retire retained import descriptors");
        if (!metadataOnlyWithoutRhi && nowDeviceLost)
        {
            allocator.structuralPlan.Clear();
            ReleasePersistentDedicatedPlan(allocator, false);
            allocator.dedicatedPool.DeviceLost(submittedFences);
            allocator.placedPool.DeviceLost(submittedFences);
        }
        else if (!metadataOnlyWithoutRhi)
        {
            containers::DynamicArray<u32>& abandonedDedicated = allocator.resolveScratch.batchResourceSlots;
            abandonedDedicated.Clear();
            for (const PhysicalBindingRecord& binding : published->physicalBindings)
                if (binding.kind == PhysicalBindingKind::DedicatedPool)
                    abandonedDedicated.PushBack(binding.poolEntry);
            // Healthy abandonment is terminal for this allocator, but it is not
            // device loss and must not route physical owners through the
            // provider-failure destruction path before graceful Shutdown() has
            // admitted canonical descriptor retirement.  Keep completed owners
            // reusable (there can be no later frame in DeviceUnavailable) so
            // their canonical bundles stay attached until shutdown preparation;
            // incomplete owners remain PendingRetirement behind submittedFences.
            allocator.dedicatedPool.Retire(abandonedDedicated, submittedFences, true);
            allocator.placedPool.Retire(published->placedBatch, submittedFences, true);
        }
        // Retained packet views are metadata witnesses only after abandonment;
        // they must not pin descriptors, imports, or native allocator bindings.
        ReleaseGenerationRuntimeOwners(published);
        allocator.state = RenderFlowResourceSessionState::DeviceUnavailable;
        allocator.structuralPlan.Clear();
        allocator.stats.state = allocator.state;
        ++allocator.stats.abortedFrames;
        ReleaseGeneration(published);
    }

    void detail::CancelSession(RenderFlowResourceAllocator::Impl& allocator, const u32 generation) noexcept
    {
        if (allocator.sessionGeneration != generation || allocator.state == RenderFlowResourceSessionState::Idle)
            return;
        if (allocator.publishedGeneration != nullptr ||
            (allocator.state != RenderFlowResourceSessionState::Planning && allocator.state != RenderFlowResourceSessionState::CandidatesSealed && allocator.state != RenderFlowResourceSessionState::Resolving))
            return;
        allocator.stats.rejectedOperations += allocator.pendingRejectedOperations.Exchange(0);
        ResetPlanningStorage(allocator);
        allocator.retainedImports.Clear();
        allocator.reservedExportSlots = 0;
        allocator.activePolicy = {};
        allocator.state = RenderFlowResourceSessionState::Idle;
        allocator.stats.state = allocator.state;
        ++allocator.stats.abortedFrames;
    }
} // namespace vanguard::rendering

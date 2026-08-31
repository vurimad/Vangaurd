#include <vanguard/rendering/render_flow_resource_allocator.hpp>

#include <vanguard/rendering/render_flow_resource_internal.hpp>

#include <cstring>
#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        [[nodiscard]] bool ValidWriter(const ResourcePlanningWriter::Impl* const writer) noexcept
        {
            return writer != nullptr && !writer->closed && writer->owner != nullptr && writer->owner->sessionGeneration == writer->batch.sessionGeneration &&
                   writer->owner->state == RenderFlowResourceSessionState::Planning;
        }

        [[nodiscard]] bool FailWriter(ResourcePlanningWriter::Impl* const writer, RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code,
                                      const char* const message, const ResourceUseId use = {}) noexcept
        {
            const RenderFlowResourceSessionState phase = writer != nullptr && writer->owner != nullptr ? writer->owner->state : RenderFlowResourceSessionState::Idle;
            const RenderFlowNodeId node = writer != nullptr ? writer->batch.node : RenderFlowNodeId{};
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

        void DestroyWriter(ResourcePlanningWriter::Impl*& writer) noexcept
        {
            if (writer == nullptr)
                return;
            ResourcePlanningWriter::Impl* const value = writer;
            writer = nullptr;
            value->~Impl();
            memory::MemoryBlock block{value, sizeof(ResourcePlanningWriter::Impl), memory::PoolId::Rendering};
            memory::Free(block);
        }

        [[nodiscard]] bool ValidResource(const ResourcePlanningWriter::Impl& writer, const LogicalResourceId resource) noexcept
        {
            return resource.IsValid() && resource.flowGroup == writer.batch.flowGroup && resource.generation == writer.batch.sessionGeneration &&
                   resource.index < writer.batch.resources.Size();
        }

        [[nodiscard]] bool ValidTextureView(const ResourcePlanningWriter::Impl& writer, const LogicalTextureViewId view) noexcept
        {
            return view.IsValid() && view.flowGroup == writer.batch.flowGroup && view.generation == writer.batch.sessionGeneration && view.index < writer.batch.textureViews.Size();
        }

        [[nodiscard]] bool ValidBufferView(const ResourcePlanningWriter::Impl& writer, const LogicalBufferViewId view) noexcept
        {
            return view.IsValid() && view.flowGroup == writer.batch.flowGroup && view.generation == writer.batch.sessionGeneration && view.index < writer.batch.bufferViews.Size();
        }

        [[nodiscard]] bool PushOperation(ResourcePlanningWriter::Impl& writer, detail::CandidateOperation operation, RenderFlowResourceFailure* const failure) noexcept
        {
            if (!ValidWriter(&writer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
            if (writer.failed)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::IncompletePlanning, "planning writer is poisoned by an earlier rejected operation");
            if (writer.batch.operations.Size() >= writer.owner->config.maximumOperations)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer operation capacity is exhausted");
            operation.ordinal = writer.batch.operations.Size();
            writer.batch.operations.PushBack(std::move(operation));
            return true;
        }

        [[nodiscard]] bool FindOrCreateNamedResource(ResourcePlanningWriter::Impl& writer, const LogicalResourceKey key, LogicalResourceId& resource,
                                                     RenderFlowResourceFailure* const failure) noexcept
        {
            resource = {};
            if (!ValidWriter(&writer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
            if (writer.failed)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::IncompletePlanning, "planning writer is poisoned by an earlier rejected operation");
            if (!key.flowSpace.IsValid() || key.name.Data() == nullptr || key.name.Length() == 0)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "logical resource key is invalid");

            for (u32 index = 0; index < writer.batch.resources.Size(); ++index)
            {
                const detail::CandidateResource& candidate = writer.batch.resources[index];
                if (candidate.identity == detail::CandidateIdentityKind::Named && candidate.flowSpace == key.flowSpace && containers::StringView(candidate.name) == key.name)
                {
                    resource = {writer.batch.flowGroup, index, writer.batch.sessionGeneration};
                    return true;
                }
            }
            if (writer.batch.resources.Size() >= writer.owner->config.maximumLogicalResources)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer logical-resource capacity is exhausted");

            detail::CandidateResource& candidate = writer.batch.resources.EmplaceBack();
            candidate.identity = detail::CandidateIdentityKind::Named;
            candidate.flowSpace = key.flowSpace;
            candidate.name.Set(key.name);
            resource = {writer.batch.flowGroup, writer.batch.resources.Size() - 1u, writer.batch.sessionGeneration};
            return true;
        }

        [[nodiscard]] bool DeclareNamed(ResourcePlanningWriter::Impl& writer, const LogicalResourceKey key, const FrameResourceDesc& desc, const detail::CandidateOperationKind kind,
                                        LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
        {
            const u32 resourceCount = writer.batch.resources.Size();
            if (!FindOrCreateNamedResource(writer, key, resource, failure))
                return false;
            detail::CandidateOperation operation;
            operation.kind = kind;
            operation.resource = resource;
            operation.resourceDesc = desc;
            if (PushOperation(writer, std::move(operation), failure))
                return true;
            if (writer.batch.resources.Size() > resourceCount)
                writer.batch.resources.PopBack();
            resource = {};
            return false;
        }

        [[nodiscard]] bool DeclareTemporary(ResourcePlanningWriter::Impl& writer, const containers::StringView displayName, const FrameResourceDesc& desc,
                                            const detail::CandidateOperationKind kind, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
        {
            resource = {};
            if (!ValidWriter(&writer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
            if (writer.batch.resources.Size() >= writer.owner->config.maximumLogicalResources)
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer logical-resource capacity is exhausted");

            const u32 resourceIndex = writer.batch.resources.Size();
            detail::CandidateResource& candidate = writer.batch.resources.EmplaceBack();
            candidate.identity = detail::CandidateIdentityKind::Temporary;
            candidate.flowSpace = {};
            if (displayName.Data() != nullptr && displayName.Length() != 0)
                candidate.name.Set(displayName);
            candidate.declarationPosition = {writer.batch.flowGroup, writer.batch.operations.Size()};
            resource = {writer.batch.flowGroup, resourceIndex, writer.batch.sessionGeneration};

            detail::CandidateOperation operation;
            operation.kind = kind;
            operation.resource = resource;
            operation.resourceDesc = desc;
            if (!PushOperation(writer, std::move(operation), failure))
            {
                writer.batch.resources.PopBack();
                resource = {};
                return false;
            }
            return true;
        }

        [[nodiscard]] bool BeginTextureUseImpl(ResourcePlanningWriter::Impl& writer, const LogicalResourceId resource, const LogicalTextureViewId view, const TextureUseDesc& desc,
                                               ResourceUseId& use, RenderFlowResourceFailure* const failure) noexcept
        {
            use = {};
            if ((!resource.IsValid() || !ValidResource(writer, resource)) && (!view.IsValid() || !ValidTextureView(writer, view)))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture use references an invalid resource or view");
            detail::CandidateOperation operation;
            operation.kind = detail::CandidateOperationKind::TextureUseBegin;
            operation.resource = resource;
            operation.textureView = view;
            operation.textureUse = desc;
            operation.use = {writer.batch.flowGroup, writer.batch.operations.Size(), writer.batch.sessionGeneration};
            const ResourceUseId result = operation.use;
            if (!PushOperation(writer, std::move(operation), failure))
                return false;
            use = result;
            return true;
        }

        [[nodiscard]] bool BeginBufferUseImpl(ResourcePlanningWriter::Impl& writer, const LogicalResourceId resource, const LogicalBufferViewId view, const BufferUseDesc& desc,
                                              ResourceUseId& use, RenderFlowResourceFailure* const failure) noexcept
        {
            use = {};
            if ((!resource.IsValid() || !ValidResource(writer, resource)) && (!view.IsValid() || !ValidBufferView(writer, view)))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer use references an invalid resource or view");
            detail::CandidateOperation operation;
            operation.kind = detail::CandidateOperationKind::BufferUseBegin;
            operation.resource = resource;
            operation.bufferView = view;
            operation.bufferUse = desc;
            operation.use = {writer.batch.flowGroup, writer.batch.operations.Size(), writer.batch.sessionGeneration};
            const ResourceUseId result = operation.use;
            if (!PushOperation(writer, std::move(operation), failure))
                return false;
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

    bool SurvivingGraphOverlay::Contains(const RenderFlowNodeId node) const noexcept
    {
        if (allNodesSurvive)
            return true;
        for (const RenderFlowNodeId candidate : nodes)
            if (candidate == node)
                return true;
        return false;
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
        return m_impl != nullptr ? m_impl->batch.node : RenderFlowNodeId{};
    }

    GpuFlowGroupId ResourcePlanningWriter::GetFlowGroup() const noexcept
    {
        return m_impl != nullptr ? m_impl->batch.flowGroup : GpuFlowGroupId{};
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

    bool ResourcePlanningWriter::DeclareTemporaryTexture(const containers::StringView displayName, const FrameTextureDesc& desc, LogicalResourceId& resource,
                                                         RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, RenderFlowResourceSessionState::Idle, "planning writer is invalid");
        if (!detail::ValidTextureDesc(desc))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::DescriptorConflict, "temporary texture descriptor is invalid");
        return DeclareTemporary(*m_impl, displayName, FrameResourceDesc::Texture(desc), detail::CandidateOperationKind::DeclareTexture, resource, failure);
    }

    bool ResourcePlanningWriter::DeclareTemporaryBuffer(const containers::StringView displayName, const FrameBufferDesc& desc, LogicalResourceId& resource,
                                                        RenderFlowResourceFailure* const failure) noexcept
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
        const u32 resourceCount = m_impl->batch.resources.Size();
        if (!FindOrCreateNamedResource(*m_impl, key, resource, failure))
            return false;
        if (resource == source)
        {
            resource = {};
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "declare-like destination must differ from its source");
        }
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::DeclareLike;
        operation.resource = resource;
        operation.otherResource = source;
        if (!PushOperation(*m_impl, std::move(operation), failure))
        {
            if (m_impl->batch.resources.Size() > resourceCount)
                m_impl->batch.resources.PopBack();
            resource = {};
            return false;
        }
        return true;
    }

    bool ResourcePlanningWriter::CreateTextureView(const LogicalResourceId resource, const rhi::TextureViewDesc& desc, LogicalTextureViewId& view,
                                                   RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        view = {};
        if (m_impl == nullptr || !ValidResource(*m_impl, resource))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture view references an invalid logical resource");
        if (m_impl->batch.textureViews.Size() >= m_impl->owner->config.maximumViews)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::CapacityExceeded, "texture-view capacity is exhausted");
        view = {m_impl->batch.flowGroup, m_impl->batch.textureViews.Size(), m_impl->batch.sessionGeneration};
        m_impl->batch.textureViews.PushBack({resource, desc});
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::CreateTextureView;
        operation.resource = resource;
        operation.textureView = view;
        if (!PushOperation(*m_impl, std::move(operation), failure))
        {
            m_impl->batch.textureViews.PopBack();
            view = {};
            return false;
        }
        return true;
    }

    bool ResourcePlanningWriter::CreateBufferView(const LogicalResourceId resource, const rhi::BufferViewDesc& desc, LogicalBufferViewId& view,
                                                  RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        view = {};
        if (m_impl == nullptr || !ValidResource(*m_impl, resource))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer view references an invalid logical resource");
        if (m_impl->batch.bufferViews.Size() >= m_impl->owner->config.maximumViews)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::CapacityExceeded, "buffer-view capacity is exhausted");
        view = {m_impl->batch.flowGroup, m_impl->batch.bufferViews.Size(), m_impl->batch.sessionGeneration};
        m_impl->batch.bufferViews.PushBack({resource, desc});
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::CreateBufferView;
        operation.resource = resource;
        operation.bufferView = view;
        if (!PushOperation(*m_impl, std::move(operation), failure))
        {
            m_impl->batch.bufferViews.PopBack();
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
        if (!use.IsValid() || use.flowGroup != m_impl->batch.flowGroup || use.generation != m_impl->batch.sessionGeneration || use.ordinal >= m_impl->batch.operations.Size())
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use identity is invalid", use);
        detail::CandidateOperation& begin = m_impl->batch.operations[use.ordinal];
        if ((begin.kind != detail::CandidateOperationKind::TextureUseBegin && begin.kind != detail::CandidateOperationKind::BufferUseBegin) || begin.use != use)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use does not identify a begin operation", use);
        if (begin.useEnded)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use was already ended", use);

        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::UseEnd;
        operation.use = use;
        begin.useEnded = true;
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::OpenResourceScope(const LogicalResourceId resource, const ResourceScopeId scope, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, resource) || !scope.IsValid())
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope begin is invalid");
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::ScopeOpen;
        operation.resource = resource;
        operation.scope = scope;
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::CloseResourceScope(const ResourceScopeId scope, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !scope.IsValid())
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope end is invalid");
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::ScopeClose;
        operation.scope = scope;
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::SwapLogicalMappings(const LogicalResourceId left, const LogicalResourceId right, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, left) || !ValidResource(*m_impl, right) || left == right)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "logical swap identities are invalid");
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::SwapMappings;
        operation.resource = left;
        operation.otherResource = right;
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::CaptureDecision(const bool value, DecisionId& decision, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        decision = {};
        if (m_impl == nullptr || !ValidWriter(m_impl))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning writer is not open");
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::Decision;
        operation.decision = {m_impl->batch.flowGroup, m_impl->batch.operations.Size(), m_impl->batch.sessionGeneration};
        operation.decisionValue = value;
        const DecisionId result = operation.decision;
        if (!PushOperation(*m_impl, std::move(operation), failure))
            return false;
        decision = result;
        return true;
    }

    bool ResourcePlanningWriter::RequestExport(const LogicalResourceId resource, const ExportSlotId slot, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, resource) || !slot.IsValid())
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export request is invalid");
        return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "terminal resource exports require the Stage 2 physical-resource provider");
    }

    bool ResourcePlanningWriter::Close(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || m_impl->closed || m_impl->owner == nullptr || m_impl->owner->sessionGeneration != m_impl->batch.sessionGeneration ||
            m_impl->owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase,
                                m_impl != nullptr && m_impl->owner != nullptr ? m_impl->owner->state : RenderFlowResourceSessionState::Idle, "planning writer is not open",
                                m_impl != nullptr ? m_impl->batch.node : RenderFlowNodeId{});

        RenderFlowResourceAllocator::Impl& owner = *m_impl->owner;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(owner.writerLock);
            if (owner.sessionGeneration != m_impl->batch.sessionGeneration || owner.state != RenderFlowResourceSessionState::Planning)
                return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidPhase, "planning session changed before writer close");

            detail::WriterReservation* reservation = nullptr;
            for (detail::WriterReservation& candidate : owner.writerReservations)
                if (candidate.node == m_impl->batch.node && candidate.flowGroup == m_impl->batch.flowGroup)
                    reservation = &candidate;
            if (reservation == nullptr || !reservation->open)
                return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "planning writer reservation is not open");

            owner.stats.rejectedOperations += m_impl->rejectedOperations;
            if (m_impl->failed)
            {
                reservation->open = false;
                reservation->failed = true;
                --owner.openWriters;
                ++owner.failedWriters;
                m_impl->closed = true;
                const RenderFlowResourceFailureCode code =
                    m_impl->firstFailureCode != RenderFlowResourceFailureCode::None ? m_impl->firstFailureCode : RenderFlowResourceFailureCode::IncompletePlanning;
                const char* const message = m_impl->firstFailureMessage != nullptr ? m_impl->firstFailureMessage : "planning writer was poisoned by a rejected operation";
                const bool result = detail::Fail(failure, code, owner.state, message, m_impl->batch.node);
                DestroyWriter(m_impl);
                return result;
            }

            owner.writerBatches.PushBack(std::move(m_impl->batch));
            reservation->open = false;
            --owner.openWriters;
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
            concurrency::ScopedLock<concurrency::SpinLock> guard(owner->writerLock);
            if (owner->sessionGeneration == m_impl->batch.sessionGeneration)
            {
                owner->stats.rejectedOperations += m_impl->rejectedOperations;
                for (detail::WriterReservation& reservation : owner->writerReservations)
                {
                    if (reservation.node == m_impl->batch.node && reservation.flowGroup == m_impl->batch.flowGroup && reservation.open)
                    {
                        reservation.open = false;
                        reservation.failed = true;
                        if (owner->openWriters != 0)
                            --owner->openWriters;
                        ++owner->failedWriters;
                        break;
                    }
                }
            }
            m_impl->closed = true;
        }
        DestroyWriter(m_impl);
    }

    FrameResourceSession::~FrameResourceSession()
    {
        CancelBeforePublication();
    }

    FrameResourceSession::FrameResourceSession(FrameResourceSession&& other) noexcept : m_owner(other.m_owner), m_generation(other.m_generation)
    {
        other.m_owner = nullptr;
        other.m_generation = 0;
    }

    FrameResourceSession& FrameResourceSession::operator=(FrameResourceSession&& other) noexcept
    {
        if (this == &other)
            return *this;
        CancelBeforePublication();
        // A published generation may only leave through Finish with a terminal
        // receipt. Never overwrite its last session handle implicitly.
        if (m_owner != nullptr)
            return *this;
        m_owner = other.m_owner;
        m_generation = other.m_generation;
        other.m_owner = nullptr;
        other.m_generation = 0;
        return *this;
    }

    bool FrameResourceSession::IsValid() const noexcept
    {
        const auto* const owner = static_cast<const RenderFlowResourceAllocator::Impl*>(m_owner);
        return owner != nullptr && m_generation != 0 && owner->sessionGeneration == m_generation && owner->state != RenderFlowResourceSessionState::Idle;
    }

    RenderFlowResourceSessionState FrameResourceSession::GetState() const noexcept
    {
        const auto* const owner = static_cast<const RenderFlowResourceAllocator::Impl*>(m_owner);
        return owner != nullptr && owner->sessionGeneration == m_generation ? owner->state : RenderFlowResourceSessionState::Idle;
    }

    bool FrameResourceSession::CreatePlanningWriter(const RenderFlowNodeId node, const GpuFlowGroupId flowGroup, const CommandScopeId commandScope, ResourcePlanningWriter& writer,
                                                    RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (writer.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, GetState(), "output planning writer is already active", node);
        auto* const owner = static_cast<RenderFlowResourceAllocator::Impl*>(m_owner);
        if (owner == nullptr || owner->sessionGeneration != m_generation || owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, GetState(), "frame resource session is not planning", node);
        if (!node.IsValid() || !flowGroup.IsValid() || !commandScope.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "planning writer identity is invalid", node);

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(ResourcePlanningWriter::Impl), alignof(ResourcePlanningWriter::Impl));
        if (!block)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "planning writer metadata allocation failed", node);
        auto* const impl = new (block.address) ResourcePlanningWriter::Impl(*owner);
        impl->batch.node = node;
        impl->batch.flowGroup = flowGroup;
        impl->batch.commandScope = commandScope;
        impl->batch.sessionGeneration = m_generation;

        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(owner->writerLock);
            if (owner->state != RenderFlowResourceSessionState::Planning || owner->sessionGeneration != m_generation)
            {
                impl->~Impl();
                memory::MemoryBlock writerBlock{impl, sizeof(ResourcePlanningWriter::Impl), memory::PoolId::Rendering};
                memory::Free(writerBlock);
                return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "planning session changed during writer creation", node);
            }
            if (owner->writerReservations.Size() >= owner->config.maximumPlanningWriters)
            {
                impl->~Impl();
                memory::MemoryBlock writerBlock{impl, sizeof(ResourcePlanningWriter::Impl), memory::PoolId::Rendering};
                memory::Free(writerBlock);
                return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "planning writer capacity is exhausted", node);
            }
            for (const detail::WriterReservation& reservation : owner->writerReservations)
            {
                if (reservation.node == node || reservation.flowGroup == flowGroup)
                {
                    impl->~Impl();
                    memory::MemoryBlock writerBlock{impl, sizeof(ResourcePlanningWriter::Impl), memory::PoolId::Rendering};
                    memory::Free(writerBlock);
                    return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "node and GPU flow group must each own exactly one planning writer",
                                        node);
                }
            }
            owner->writerReservations.PushBack({node, flowGroup, true, false});
            ++owner->openWriters;
            ++owner->stats.planningWriters;
        }
        writer.m_impl = impl;
        return true;
    }

    bool FrameResourceSession::SealCandidates(const PlanningJoinToken join, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        auto* const owner = static_cast<RenderFlowResourceAllocator::Impl*>(m_owner);
        if (owner == nullptr || owner->sessionGeneration != m_generation || owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, GetState(), "frame resource session is not planning");
        concurrency::ScopedLock<concurrency::SpinLock> guard(owner->writerLock);
        if (!join.IsReady() || owner->openWriters != 0 || owner->failedWriters != 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::IncompletePlanning, owner->state, "every planning job and writer must complete successfully before sealing");
        u64 operationCount = 0;
        u64 logicalResourceCount = 0;
        u64 viewCount = 0;
        for (const detail::CandidateWriterBatch& batch : owner->writerBatches)
        {
            operationCount += batch.operations.Size();
            logicalResourceCount += batch.resources.Size();
            viewCount += static_cast<u64>(batch.textureViews.Size()) + batch.bufferViews.Size();
        }
        if (operationCount > owner->config.maximumOperations || logicalResourceCount > owner->config.maximumLogicalResources || viewCount > owner->config.maximumViews)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "sealed frame planning metadata exceeds an aggregate allocator capacity");
        owner->operationCount = static_cast<u32>(operationCount);
        owner->state = RenderFlowResourceSessionState::CandidatesSealed;
        owner->stats.state = owner->state;
        return true;
    }

    bool FrameResourceSession::Resolve(const SurvivingGraphOverlay& surviving, const CompiledQueueSchedule& schedule, ExecutionGenerationRef& generation, jobs::Builder* const,
                                       RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        generation.Reset();
        auto* const owner = static_cast<RenderFlowResourceAllocator::Impl*>(m_owner);
        if (owner == nullptr || owner->sessionGeneration != m_generation || owner->state != RenderFlowResourceSessionState::CandidatesSealed)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, GetState(), "frame resource session candidates are not sealed");
        ExecutionGenerationRef::Impl* resolved = nullptr;
        if (!detail::ResolveFrame(*owner, surviving, schedule, resolved, failure))
            return false;
        generation = ExecutionGenerationRef(resolved);
        return true;
    }

    void FrameResourceSession::CancelBeforePublication() noexcept
    {
        auto* const owner = static_cast<RenderFlowResourceAllocator::Impl*>(m_owner);
        if (owner != nullptr && owner->sessionGeneration == m_generation)
        {
            if (owner->state == RenderFlowResourceSessionState::Planning || owner->state == RenderFlowResourceSessionState::CandidatesSealed ||
                owner->state == RenderFlowResourceSessionState::Resolving)
                detail::CancelSession(*owner, m_generation);
            else if (owner->state != RenderFlowResourceSessionState::Idle)
                return;
        }
        m_owner = nullptr;
        m_generation = 0;
    }

    RenderFlowResourceAllocator::~RenderFlowResourceAllocator()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool RenderFlowResourceAllocator::Initialize(const RenderFlowResourceAllocatorConfig& config, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl != nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::AlreadyInitialized, m_impl->state, "render flow resource allocator is already initialized");
        if (config.maximumPlanningWriters == 0 || config.maximumOperations == 0 || config.maximumLogicalResources == 0 || config.maximumViews == 0 ||
            config.maximumTrackedTextureSubresources == 0 || config.maximumExecutionPackets == 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidConfiguration, RenderFlowResourceSessionState::Idle,
                                "render flow resource allocator configuration is invalid");
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
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool RenderFlowResourceAllocator::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool RenderFlowResourceAllocator::BeginFrame(const u64 frameSerial, const FrameResourcePolicy& policy, FrameResourceSession& session, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized");
        if (session.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, m_impl->state, "output frame resource session is already active");
        session.CancelBeforePublication();
        if (m_impl->state != RenderFlowResourceSessionState::Idle || m_impl->publishedGeneration != nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, m_impl->state, "render flow resource allocator is not idle");
        if (policy.enablePlacedResources)
            return detail::Fail(failure, RenderFlowResourceFailureCode::UnsupportedCapability, m_impl->state, "placed resources are not implemented in Stage 1");

        m_impl->writerReservations.Clear();
        m_impl->writerBatches.Clear();
        m_impl->frameSerial = frameSerial;
        m_impl->policy = policy;
        m_impl->sessionGeneration = detail::NextGeneration(m_impl->sessionGeneration);
        m_impl->openWriters = 0;
        m_impl->failedWriters = 0;
        m_impl->operationCount = 0;
        m_impl->state = RenderFlowResourceSessionState::Planning;
        ++m_impl->stats.begunFrames;
        m_impl->stats.planningWriters = 0;
        m_impl->stats.logicalAllocations = 0;
        m_impl->stats.compiledPackets = 0;
        m_impl->stats.state = m_impl->state;
        session.m_owner = m_impl;
        session.m_generation = m_impl->sessionGeneration;
        return true;
    }

    RenderFlowResourceAllocatorStats RenderFlowResourceAllocator::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->writerLock);
        RenderFlowResourceAllocatorStats stats = m_impl->stats;
        stats.state = m_impl->state;
        return stats;
    }

    void detail::CancelSession(RenderFlowResourceAllocator::Impl& allocator, const u32 generation) noexcept
    {
        if (allocator.sessionGeneration != generation || allocator.state == RenderFlowResourceSessionState::Idle)
            return;
        if (allocator.publishedGeneration != nullptr || (allocator.state != RenderFlowResourceSessionState::Planning && allocator.state != RenderFlowResourceSessionState::CandidatesSealed &&
                                                         allocator.state != RenderFlowResourceSessionState::Resolving))
            return;
        allocator.writerReservations.Clear();
        allocator.writerBatches.Clear();
        allocator.openWriters = 0;
        allocator.failedWriters = 0;
        allocator.operationCount = 0;
        allocator.state = RenderFlowResourceSessionState::Idle;
        allocator.stats.state = allocator.state;
        ++allocator.stats.abortedFrames;
    }
} // namespace vanguard::rendering

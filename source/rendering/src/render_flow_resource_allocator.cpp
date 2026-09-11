#include <vanguard/rendering/render_flow_resource_allocator.hpp>

#include <vanguard/rendering/render_flow_resource_internal.hpp>
#include <vanguard/system/assert.hpp>

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
        [[nodiscard]] bool ValidWriter(const ResourcePlanningWriter::Impl* const writer) noexcept
        {
            return writer != nullptr && !writer->closed && writer->owner != nullptr && writer->owner->sessionGeneration == writer->batch.sessionGeneration &&
                   writer->owner->state == RenderFlowResourceSessionState::Planning;
        }

        [[nodiscard]] bool FailWriter(ResourcePlanningWriter::Impl* const writer, RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message,
                                      const ResourceUseId use = {}) noexcept
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
            RenderFlowResourceAllocator::Impl* const owner = value->owner;
            value->~Impl();
            memory::MemoryBlock block{value, sizeof(ResourcePlanningWriter::Impl), memory::PoolId::Rendering};
            memory::Free(block);
            detail::ReleaseAllocator(owner);
        }

        [[nodiscard]] bool ValidResource(const ResourcePlanningWriter::Impl& writer, const LogicalResourceId resource) noexcept
        {
            return resource.IsValid() && resource.flowGroup == writer.batch.flowGroup && resource.generation == writer.batch.sessionGeneration && resource.index < writer.batch.resources.Size();
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

        [[nodiscard]] bool FindOrCreateNamedResource(ResourcePlanningWriter::Impl& writer, const LogicalResourceKey key, LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
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

        [[nodiscard]] bool DeclareNamed(ResourcePlanningWriter::Impl& writer, const LogicalResourceKey key, const FrameResourceDesc& desc, const detail::CandidateOperationKind kind, LogicalResourceId& resource,
                                        RenderFlowResourceFailure* const failure) noexcept
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

        [[nodiscard]] bool DeclareTemporary(ResourcePlanningWriter::Impl& writer, const containers::StringView displayName, const FrameResourceDesc& desc, const detail::CandidateOperationKind kind,
                                            LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
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

        [[nodiscard]] bool ImportNamed(ResourcePlanningWriter::Impl& writer, const LogicalResourceKey key, const ImportedResourceId imported, const detail::CandidateOperationKind kind,
                                       LogicalResourceId& resource, RenderFlowResourceFailure* const failure) noexcept
        {
            resource = {};
            if (!imported.IsValid() || imported.generation != writer.batch.sessionGeneration || imported.index >= writer.owner->retainedImports.Size())
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "import operation references an invalid session import");
            const detail::RetainedImportRecord& retained = writer.owner->retainedImports[imported.index];
            if ((kind == detail::CandidateOperationKind::ImportTexture && retained.desc.kind != FrameResourceKind::Texture) ||
                (kind == detail::CandidateOperationKind::ImportBuffer && retained.desc.kind != FrameResourceKind::Buffer))
                return FailWriter(&writer, failure, RenderFlowResourceFailureCode::DescriptorConflict, "import operation kind disagrees with the registered physical resource");
            const u32 resourceCount = writer.batch.resources.Size();
            if (!FindOrCreateNamedResource(writer, key, resource, failure))
                return false;
            detail::CandidateOperation operation;
            operation.kind = kind;
            operation.resource = resource;
            operation.importedResource = imported;
            if (PushOperation(writer, std::move(operation), failure))
                return true;
            if (writer.batch.resources.Size() > resourceCount)
                writer.batch.resources.PopBack();
            resource = {};
            return false;
        }

        [[nodiscard]] bool BeginTextureUseImpl(ResourcePlanningWriter::Impl& writer, const LogicalResourceId resource, const LogicalTextureViewId view, const TextureUseDesc& desc, ResourceUseId& use,
                                               RenderFlowResourceFailure* const failure) noexcept
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

        [[nodiscard]] bool BeginBufferUseImpl(ResourcePlanningWriter::Impl& writer, const LogicalResourceId resource, const LogicalBufferViewId view, const BufferUseDesc& desc, ResourceUseId& use,
                                              RenderFlowResourceFailure* const failure) noexcept
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

    bool ResourcePlanningWriter::DeclareTemporaryLike(const containers::StringView displayName, const LogicalResourceId source, LogicalResourceId& resource,
                                                      RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        resource = {};
        if (m_impl == nullptr || !ValidResource(*m_impl, source))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "temporary declare-like source is not owned by this planning writer");
        if (m_impl->batch.resources.Size() >= m_impl->owner->config.maximumLogicalResources)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::CapacityExceeded, "planning writer logical-resource capacity is exhausted");

        detail::CandidateResource& candidate = m_impl->batch.resources.EmplaceBack();
        candidate.identity = detail::CandidateIdentityKind::Temporary;
        if (displayName.Data() != nullptr && displayName.Length() != 0)
            candidate.name.Set(displayName);
        candidate.declarationPosition = {m_impl->batch.flowGroup, m_impl->batch.operations.Size()};
        resource = {m_impl->batch.flowGroup, m_impl->batch.resources.Size() - 1u, m_impl->batch.sessionGeneration};

        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::DeclareLike;
        operation.resource = resource;
        operation.otherResource = source;
        if (!PushOperation(*m_impl, std::move(operation), failure))
        {
            m_impl->batch.resources.PopBack();
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

    bool ResourcePlanningWriter::CreateBufferView(const LogicalResourceId resource, const rhi::BufferViewDesc& desc, LogicalBufferViewId& view, RenderFlowResourceFailure* const failure) noexcept
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

    bool ResourcePlanningWriter::OpenResourceScope(const LogicalResourceId resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, resource))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope begin is invalid");
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::NamedScopeOpen;
        operation.resource = resource;
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

    bool ResourcePlanningWriter::CloseResourceScope(const LogicalResourceId resource, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, resource))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope end is invalid");
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::NamedScopeClose;
        operation.resource = resource;
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

    bool ResourcePlanningWriter::RequestExport(const LogicalResourceId resource, const ExportSlotId slot, const TerminalResourceExportDesc& desc, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || !ValidResource(*m_impl, resource) || !slot.IsValid() || slot.generation != m_impl->batch.sessionGeneration || slot.index >= m_impl->owner->reservedExportSlots)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export request is invalid");
        if (desc.readiness != ExportReadinessKind::SameQueueContinuation && desc.readiness != ExportReadinessKind::ExplicitFenceSignal)
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "export readiness kind is unsupported");
        if (!detail::ValidQueue(desc.terminalQueue))
            return FailWriter(m_impl, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "export terminal queue is invalid");
        detail::CandidateOperation operation;
        operation.kind = detail::CandidateOperationKind::Export;
        operation.resource = resource;
        operation.exportSlot = slot;
        operation.exportDesc = desc;
        return PushOperation(*m_impl, std::move(operation), failure);
    }

    bool ResourcePlanningWriter::Close(RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl == nullptr || m_impl->closed || m_impl->owner == nullptr || m_impl->owner->sessionGeneration != m_impl->batch.sessionGeneration ||
            m_impl->owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, m_impl != nullptr && m_impl->owner != nullptr ? m_impl->owner->state : RenderFlowResourceSessionState::Idle,
                                "planning writer is not open", m_impl != nullptr ? m_impl->batch.node : RenderFlowNodeId{});

        RenderFlowResourceAllocator::Impl& owner = *m_impl->owner;
        {
            detail::WriterReservation* const reservation = m_impl->reservation;
            if (reservation == nullptr || !reservation->open)
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
                const bool result = detail::Fail(failure, code, owner.state, message, m_impl->batch.node);
                DestroyWriter(m_impl);
                return result;
            }

            reservation->batch = std::move(m_impl->batch);
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
            if (owner->sessionGeneration == m_impl->batch.sessionGeneration && owner->state == RenderFlowResourceSessionState::Planning)
            {
                detail::WriterReservation& reservation = *m_impl->reservation;
                if (m_impl->rejectedOperations != 0)
                    static_cast<void>(owner->pendingRejectedOperations.ExchangeAdd(m_impl->rejectedOperations));
                reservation.open = false;
                reservation.failed = true;
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
        if (owner->writerReservations.Size() != 0)
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
        for (u32 index = 0; index < owner->retainedImports.Size(); ++index)
        {
            const detail::RetainedImportRecord& existing = owner->retainedImports[index];
            if (existing.presentationAcquisition.IsValid() && existing.presentationAcquisition.swapChain == acquisition.swapChain &&
                existing.presentationAcquisition.serial == acquisition.serial)
            {
                imported = {index, owner->sessionGeneration};
                return true;
            }
            if (existing.texture.GetRef() == acquisition.texture)
                return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state,
                                    "one acquired back buffer cannot be registered as multiple retained imports");
        }
        if (owner->retainedImports.Size() >= owner->config.maximumRetainedImports)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "retained import capacity is exhausted");

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
        imported = {owner->retainedImports.Size() - 1u, owner->sessionGeneration};
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
        if (owner->writerReservations.Size() != 0)
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
        for (u32 index = 0; index < owner->retainedImports.Size(); ++index)
        {
            const detail::RetainedImportRecord& existing = owner->retainedImports[index];
            if (existing.token == desc.token)
            {
                if (existing.texture.GetRef() != desc.texture || !detail::ResourceDescEqual(existing.desc, resourceDesc) || existing.initialState != desc.initialState ||
                    existing.terminalState != desc.terminalState || existing.initialQueue != desc.initialQueue || existing.terminalQueue != desc.terminalQueue ||
                    existing.readiness != desc.readiness || existing.incomingWait != desc.incomingWait)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "repeated external texture token disagrees with its registered contract");
                imported = {index, owner->sessionGeneration};
                return true;
            }
            if (existing.texture.GetRef() == desc.texture)
                return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "one external texture cannot be registered under multiple tokens");
        }
        if (owner->retainedImports.Size() >= owner->config.maximumRetainedImports)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "retained import capacity is exhausted");
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
        imported = {owner->retainedImports.Size() - 1u, owner->sessionGeneration};
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
        if (owner->writerReservations.Size() != 0)
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
        for (u32 index = 0; index < owner->retainedImports.Size(); ++index)
        {
            const detail::RetainedImportRecord& existing = owner->retainedImports[index];
            if (existing.token == desc.token)
            {
                if (existing.buffer.GetRef() != desc.buffer || !detail::ResourceDescEqual(existing.desc, resourceDesc) || existing.initialState != desc.initialState ||
                    existing.terminalState != desc.terminalState || existing.initialQueue != desc.initialQueue || existing.terminalQueue != desc.terminalQueue ||
                    existing.readiness != desc.readiness || existing.incomingWait != desc.incomingWait)
                    return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "repeated external buffer token disagrees with its registered contract");
                imported = {index, owner->sessionGeneration};
                return true;
            }
            if (existing.buffer.GetRef() == desc.buffer)
                return detail::Fail(failure, RenderFlowResourceFailureCode::DescriptorConflict, owner->state, "one external buffer cannot be registered under multiple tokens");
        }
        if (owner->retainedImports.Size() >= owner->config.maximumRetainedImports)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "retained import capacity is exhausted");
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
        imported = {owner->retainedImports.Size() - 1u, owner->sessionGeneration};
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
        if (owner->writerReservations.Size() != 0)
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
        if (!owner->writerReservations.Empty() || !owner->queueRequestGroups.Empty())
        {
            if (owner->writerReservations.Size() == groupCount && owner->queueRequestGroups.Size() == groupCount)
                return true;
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render-flow group storage was already prepared with a different count");
        }
        owner->writerReservations.Resize(groupCount);
        owner->queueRequestGroups.Resize(groupCount);
        if (owner->writerReservations.Size() != groupCount || owner->queueRequestGroups.Size() != groupCount)
        {
            owner->writerReservations.Clear();
            owner->queueRequestGroups.Clear();
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "render-flow group storage allocation failed");
        }
        return true;
    }

    bool RenderFlowResourceAllocator::CreatePlanningWriter(const RenderFlowNodeId node, const GpuFlowGroupId flowGroup, const CommandScopeId commandScope,
                                                           ResourcePlanningWriter& writer, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (writer.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, GetState(), "output planning writer is already active", node);
        Impl* const owner = m_impl;
        if (owner == nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::NotInitialized, RenderFlowResourceSessionState::Idle, "render flow resource allocator is not initialized", node);
        if (owner->state != RenderFlowResourceSessionState::Planning)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, owner->state, "render flow resource allocator is not planning", node);
        if (!node.IsValid() || !flowGroup.IsValid() || !commandScope.IsValid())
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "planning writer identity is invalid", node);
        if (owner->writerReservations.Empty() && !PreparePlanningGroups(owner->config.maximumPlanningWriters, failure))
            return false;
        if (flowGroup.value >= owner->writerReservations.Size())
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "planning writer flow group exceeds the prepared group storage", node);
        const u32 generation = owner->sessionGeneration;

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(ResourcePlanningWriter::Impl), alignof(ResourcePlanningWriter::Impl));
        if (!block)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "planning writer metadata allocation failed", node);
        auto* const impl = new (block.address) ResourcePlanningWriter::Impl(*owner);
        impl->batch.node = node;
        impl->batch.flowGroup = flowGroup;
        impl->batch.commandScope = commandScope;
        impl->batch.sessionGeneration = generation;

        detail::WriterReservation& reservation = owner->writerReservations[flowGroup.value];
        if (reservation.node.IsValid())
        {
            impl->~Impl();
            memory::MemoryBlock writerBlock{impl, sizeof(ResourcePlanningWriter::Impl), memory::PoolId::Rendering};
            memory::Free(writerBlock);
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "GPU flow group already owns a planning writer", node);
        }
        reservation.node = node;
        reservation.flowGroup = flowGroup;
        reservation.open = true;
        reservation.failed = false;
        impl->reservation = &reservation;
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
        if (owner->queueRequestGroups.Empty() && !PreparePlanningGroups(owner->config.maximumPlanningWriters, failure))
            return false;
        if (flowGroup.value >= owner->queueRequestGroups.Size())
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "queue request flow group exceeds the prepared group storage");
        detail::QueueRequestGroup& group = owner->queueRequestGroups[flowGroup.value];
        if (group.count != 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "GPU flow group already owns a queue or synchronization request");
        group.requests[group.count++] = {flowGroup, queue, rhi::CommandListSyncType::None, detail::QueueRequestKind::Begin};
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
        if (owner->queueRequestGroups.Empty() && !PreparePlanningGroups(owner->config.maximumPlanningWriters, failure))
            return false;
        if (flowGroup.value >= owner->queueRequestGroups.Size())
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "queue request flow group exceeds the prepared group storage");
        detail::QueueRequestGroup& group = owner->queueRequestGroups[flowGroup.value];
        if (group.count != 1 || group.requests[0].kind != detail::QueueRequestKind::Begin)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "GPU flow group queue was ended without a matching begin");
        group.requests[group.count++] = {flowGroup, group.requests[0].queue, rhi::CommandListSyncType::None, detail::QueueRequestKind::End};
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
        if (owner->queueRequestGroups.Empty() && !PreparePlanningGroups(owner->config.maximumPlanningWriters, failure))
            return false;
        if (flowGroup.value >= owner->queueRequestGroups.Size())
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "queue request flow group exceeds the prepared group storage");
        detail::QueueRequestGroup& group = owner->queueRequestGroups[flowGroup.value];
        if (group.count != 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, owner->state, "GPU flow group already owns a queue or synchronization request");
        group.requests[group.count++] = {flowGroup, rhi::QueueType::Graphics, sync, detail::QueueRequestKind::Sync};
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
        containers::HashMap<u32, u32> nodes{memory::pools::Rendering::GetInstance()};
        u32 writerCount = 0;
        for (u32 groupIndex = 0; groupIndex < owner->writerReservations.Size(); ++groupIndex)
        {
            const detail::WriterReservation& reservation = owner->writerReservations[groupIndex];
            if (!reservation.node.IsValid())
                continue;
            if (reservation.open || reservation.failed)
                return detail::Fail(failure, RenderFlowResourceFailureCode::IncompletePlanning, owner->state, "every planning job and writer must complete successfully before sealing");
            if (!reservation.flowGroup.IsValid() || reservation.flowGroup.value != groupIndex || reservation.batch.flowGroup != reservation.flowGroup || reservation.batch.node != reservation.node)
                return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "planning writer was published to the wrong render-flow group", reservation.node);
            u32 existingGroup = InvalidRenderFlowResourceIndex;
            if (nodes.Find(reservation.node.value, existingGroup))
                return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, owner->state, "render-flow node owns more than one planning group", reservation.node);
            if (!nodes.Insert(reservation.node.value, reservation.flowGroup.value).IsSuccessful())
                return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "planning-writer identity index allocation failed", reservation.node);
            ++writerCount;
        }
        u64 operationCount = 0;
        for (const detail::QueueRequestGroup& group : owner->queueRequestGroups)
            operationCount += group.count;
        u64 logicalResourceCount = 0;
        u64 viewCount = 0;
        for (const detail::WriterReservation& reservation : owner->writerReservations)
        {
            if (!reservation.node.IsValid())
                continue;
            const detail::CandidateWriterBatch& batch = reservation.batch;
            operationCount += batch.operations.Size();
            logicalResourceCount += batch.resources.Size();
            viewCount += static_cast<u64>(batch.textureViews.Size()) + batch.bufferViews.Size();
        }
        if (operationCount > owner->config.maximumOperations || logicalResourceCount > owner->config.maximumLogicalResources || viewCount > owner->config.maximumViews)
            return detail::Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, owner->state, "sealed frame planning metadata exceeds an aggregate allocator capacity");
        // Collect only after validation, so a rejected seal leaves every tape intact.
        owner->writerBatches.Reserve(writerCount);
        for (detail::WriterReservation& reservation : owner->writerReservations)
            if (reservation.node.IsValid())
                owner->writerBatches.PushBack(std::move(reservation.batch));
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
        Impl* const impl = m_impl;
        m_impl = nullptr;
        detail::AbandonAllocatorSession(*impl, impl->sessionGeneration);
        impl->publishedExports.Clear();
        impl->dedicatedPool.DeviceLost();
        impl->placedPool.DeviceLost();
        impl->state = RenderFlowResourceSessionState::DeviceUnavailable;
        impl->stats.state = impl->state;
        detail::ReleaseAllocator(impl);
    }

    bool RenderFlowResourceAllocator::Initialize(const RenderFlowResourceAllocatorConfig& config, RenderFlowResourceFailure* const failure) noexcept
    {
        detail::ClearFailure(failure);
        if (m_impl != nullptr)
            return detail::Fail(failure, RenderFlowResourceFailureCode::AlreadyInitialized, m_impl->state, "render flow resource allocator is already initialized");
        if (config.maximumPlanningWriters == 0 || config.maximumOperations == 0 || config.maximumLogicalResources == 0 || config.maximumViews == 0 || config.maximumTrackedTextureSubresources == 0 ||
            config.maximumExecutionPackets == 0 || config.maximumCompiledResourceActions == 0 || config.maximumCommandScopeEntryStates == 0 || config.maximumRetainedImports == 0 || config.maximumTerminalExports == 0 ||
            config.maximumOutstandingPublishedExports == 0 || config.hardNativeByteLimit == 0)
            return detail::Fail(failure, RenderFlowResourceFailureCode::InvalidConfiguration, RenderFlowResourceSessionState::Idle, "render flow resource allocator configuration is invalid");
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
        impl->publishedExports.Clear();
        impl->dedicatedPool.DeviceLost();
        impl->placedPool.DeviceLost();
        impl->state = RenderFlowResourceSessionState::DeviceUnavailable;
        impl->stats.state = impl->state;
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
        return m_impl->placedPool.ClearPersistentCache(failure) && m_impl->dedicatedPool.ClearPersistentCache(failure);
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

        if (!m_impl->config.allowLogicalOnlyValidation)
        {
            m_impl->dedicatedPool.Poll();
            m_impl->placedPool.Poll();
            if (policy.processEviction)
            {
                const detail::AllocatorNativeByteLedgerStats charged = m_impl->nativeByteLedger.GetStats();
                detail::ResourcePoolTrimState trim{charged.textureBytes, charged.bufferBytes};
                if (!m_impl->dedicatedPool.TrimToSoftTargets(trim, failure) || !m_impl->placedPool.TrimToSoftTargets(trim, failure))
                    return false;
            }
        }

        m_impl->writerReservations.Clear();
        m_impl->writerBatches.Clear();
        m_impl->queueRequestGroups.Clear();
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
        stats.chargedNativeBytes = ledger.chargedBytes;
        stats.chargedTextureBytes = ledger.textureBytes;
        stats.chargedBufferBytes = ledger.bufferBytes;
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

    void detail::ReleaseAllocator(RenderFlowResourceAllocator::Impl* const allocator) noexcept
    {
        if (allocator == nullptr || allocator->references.Decrement() != 0)
            return;
        allocator->~Impl();
        memory::MemoryBlock block{allocator, sizeof(RenderFlowResourceAllocator::Impl), memory::PoolId::Rendering};
        memory::Free(block);
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
        allocator.writerReservations.Clear();
        allocator.writerBatches.Clear();
        allocator.queueRequestGroups.Clear();
        allocator.retainedImports.Clear();
        allocator.publishedExports.Clear();
        allocator.reservedExportSlots = 0;
        allocator.activePolicy = {};
        allocator.dedicatedPool.DeviceLost();
        allocator.placedPool.DeviceLost();
        published->descriptorDeviceLost = true;
        // Abandonment can follow an incomplete terminal receipt after a real
        // submission. Keep descriptors behind all submitted work, too.
        rhi::ResidencyFenceSet descriptorFences;
        if (rhi::GetSubmittedResidencyFences(descriptorFences))
            published->descriptorRetirement = {descriptorFences.graphics, descriptorFences.compute, descriptorFences.copy};
        else
            published->descriptorRetirement = {~u64{0}, ~u64{0}, ~u64{0}};
        allocator.state = RenderFlowResourceSessionState::DeviceUnavailable;
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
        allocator.writerReservations.Clear();
        allocator.writerBatches.Clear();
        allocator.queueRequestGroups.Clear();
        allocator.retainedImports.Clear();
        allocator.reservedExportSlots = 0;
        allocator.activePolicy = {};
        allocator.state = RenderFlowResourceSessionState::Idle;
        allocator.stats.state = allocator.state;
        ++allocator.stats.abortedFrames;
    }
} // namespace vanguard::rendering

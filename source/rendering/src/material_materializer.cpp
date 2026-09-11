#include <vanguard/rendering/material_materializer.hpp>

#include <vanguard/materials/materials.hpp>
#include <vanguard/rendering/material_materializer_internal.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>

#include <cstring>
#include <new>

namespace vanguard::rendering
{
    namespace
    {
        enum class OperationState : u8
        {
            Free,
            Resolving,
            ReadyForBatch,
            Frozen,
            Ready,
            Failed,
            Leased,
            Retiring
        };

        [[nodiscard]] constexpr bool CompleteCutover(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return fences.Covers(rhi::QueueType::Graphics) && fences.Covers(rhi::QueueType::Compute) && fences.Covers(rhi::QueueType::Copy);
        }

        void ClearFailure(MaterialMaterializerFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MaterialMaterializerFailure* const failure, const MaterialMaterializerFailureCode code, const char* const message, const MaterialProgramLayoutFailure& layoutFailure = {},
                                const MaterialResourceResolverFailure& resourceFailure = {}, const GpuSceneDefinitionFailure& definitionFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, message, layoutFailure, resourceFailure, definitionFailure};
            return false;
        }

        void HashU8(crypto::Sha256Builder& hash, const u8 value) noexcept
        {
            static_cast<void>(hash.Update(&value, sizeof(value)));
        }

        void HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
        {
            const u8 bytes[]{static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
            static_cast<void>(hash.Update(bytes, sizeof(bytes)));
        }

        void HashShape(crypto::Sha256Builder& hash, const shaders::MaterialResourceShape& shape) noexcept
        {
            HashU8(hash, static_cast<u8>(shape.access));
            HashU8(hash, static_cast<u8>(shape.textureDimension));
            HashU8(hash, static_cast<u8>(shape.bufferKind));
            HashU8(hash, static_cast<u8>(shape.samplerKind));
            HashU8(hash, static_cast<u8>(shape.scalarType));
            HashU8(hash, shape.componentCount);
            HashU8(hash, static_cast<u8>(shape.flags));
            HashU8(hash, shape.reserved);
            HashU32(hash, shape.elementStride);
        }

        [[nodiscard]] shaders::MaterialResourceKind ToShaderKind(const materials::ResourceParameterKind kind) noexcept
        {
            switch (kind)
            {
            case materials::ResourceParameterKind::Texture:
                return shaders::MaterialResourceKind::Texture;
            case materials::ResourceParameterKind::Buffer:
                return shaders::MaterialResourceKind::Buffer;
            case materials::ResourceParameterKind::Sampler:
                return shaders::MaterialResourceKind::Sampler;
            case materials::ResourceParameterKind::AccelerationStructure:
                return shaders::MaterialResourceKind::AccelerationStructure;
            }
            return shaders::MaterialResourceKind::Texture;
        }

        [[nodiscard]] const materials::LoadedMaterialDependency* FindDependency(const containers::ArraySpan<const materials::LoadedMaterialDependency> dependencies,
                                                                                const resources::ResourceReference reference) noexcept
        {
            u32 first = 0;
            u32 count = dependencies.Size();
            while (count != 0)
            {
                const u32 step = count / 2u;
                const u32 index = first + step;
                const resources::ResourceReference candidate = dependencies[index].resource;
                const bool candidateLess = candidate.GetPath() != reference.GetPath() ? candidate.GetPath() < reference.GetPath() : candidate.ExpectedType() < reference.ExpectedType();
                if (candidateLess)
                {
                    first = index + 1u;
                    count -= step + 1u;
                }
                else
                    count = step;
            }
            if (first < dependencies.Size() && dependencies[first].resource == reference)
                return &dependencies[first];
            return nullptr;
        }
    } // namespace

    struct MaterialMaterializer::Impl
    {
        struct Operation
        {
            Operation() noexcept
                : resolveTickets(memory::pools::Rendering::GetInstance()), resourceReferences(memory::pools::Rendering::GetInstance()), gpuResources(memory::pools::Rendering::GetInstance()),
                  parameterBytes(memory::pools::Rendering::GetInstance())
            {
            }

            resources::ResourceHandle root;
            containers::DynamicArray<MaterialResourceResolveTicket> resolveTickets;
            containers::DynamicArray<MaterialResourceReference> resourceReferences;
            containers::DynamicArray<GpuMaterialResource> gpuResources;
            containers::DynamicArray<u8> parameterBytes;
            MaterialProgramLayoutId layout;
            GpuSceneDefinitionKey key;
            GpuMaterialHandle handle;
            MaterialMaterializerFailure failure;
            rhi::ResidencyFenceSet retirementFences;
            u32 generation = 0;
            u32 nextResourceReferenceToRetire = 0;
            u32 resolvingWorkIndex = InvalidMaterialMaterializationIndex;
            u32 readyWorkIndex = InvalidMaterialMaterializationIndex;
            u32 nextRoleToPoll = 0;
            u32 resolvedRoleCount = 0;
            OperationState state = OperationState::Free;
            bool cancelRequested = false;
            bool definitionReleased = false;
        };

        struct WorkRef
        {
            u32 index = InvalidMaterialMaterializationIndex;
            u32 generation = 0;
        };

        explicit Impl(const MaterialMaterializerConfig& value) noexcept
            : config(value), operations(memory::pools::Rendering::GetInstance()), recycledOperations(memory::pools::Rendering::GetInstance()), resolvingWork(memory::pools::Rendering::GetInstance()),
              resolvingScratch(memory::pools::Rendering::GetInstance()), readyWork(memory::pools::Rendering::GetInstance()), frozenOperations(memory::pools::Rendering::GetInstance()),
              frozenDefinitions(memory::pools::Rendering::GetInstance()), consumedHandles(memory::pools::Rendering::GetInstance())
        {
            operations.Reserve(config.maximumOperations);
            recycledOperations.Reserve(config.maximumOperations);
            resolvingWork.Reserve(config.maximumOperations);
            resolvingScratch.Reserve(config.maximumOperationsProgressedPerUpdate);
            readyWork.Reserve(config.maximumOperations);
            frozenOperations.Reserve(config.maximumMaterialsPerBatch);
            frozenDefinitions.Reserve(config.maximumMaterialsPerBatch);
            consumedHandles.Reserve(config.maximumMaterialsPerBatch);
        }

        MaterialProgramLayoutRegistry* layouts = nullptr;
        MaterialResourceResolver* resources = nullptr;
        GpuSceneRuntime* runtime = nullptr;
        GpuSceneDefinitions* definitions = nullptr;
        MaterialMaterializerConfig config;
        containers::DynamicArray<Operation> operations;
        containers::DynamicArray<u32> recycledOperations;
        containers::DynamicArray<u32> resolvingWork;
        containers::DynamicArray<WorkRef> resolvingScratch;
        containers::DynamicArray<u32> readyWork;
        containers::DynamicArray<u32> frozenOperations;
        containers::DynamicArray<GpuMaterialDefinition> frozenDefinitions;
        containers::DynamicArray<GpuMaterialHandle> consumedHandles;
        GpuMaterialDefinitionBatch batch;
        MaterialMaterializerStats stats;
        u32 nextResolvingWork = 0;

        [[nodiscard]] Operation* Find(const u32 index, const u32 generation) noexcept
        {
            if (index >= operations.Size())
                return nullptr;
            Operation& operation = operations[index];
            return operation.state != OperationState::Free && operation.generation == generation ? &operation : nullptr;
        }

        [[nodiscard]] u32 AllocateOperation() noexcept
        {
            u32 index = InvalidMaterialMaterializationIndex;
            if (!recycledOperations.Empty())
            {
                index = recycledOperations.Back();
                recycledOperations.PopBack();
            }
            else if (operations.Size() < config.maximumOperations)
            {
                index = operations.Size();
                operations.EmplaceBack();
            }
            if (index == InvalidMaterialMaterializationIndex)
                return index;
            Operation& operation = operations[index];
            operation.generation = operation.generation == 0xffffffffu ? 1u : operation.generation + 1u;
            if (operation.generation == 0)
                operation.generation = 1;
            operation.state = OperationState::Resolving;
            ++stats.activeOperations;
            ++stats.pendingOperations;
            return index;
        }

        void AddResolvingWork(const u32 index) noexcept
        {
            Operation& operation = operations[index];
            if (operation.resolvingWorkIndex != InvalidMaterialMaterializationIndex)
                return;
            operation.resolvingWorkIndex = resolvingWork.Size();
            resolvingWork.PushBack(index);
        }

        void RemoveResolvingWork(const u32 index) noexcept
        {
            Operation& operation = operations[index];
            if (operation.resolvingWorkIndex == InvalidMaterialMaterializationIndex)
                return;
            const u32 workIndex = operation.resolvingWorkIndex;
            const u32 moved = resolvingWork.Back();
            resolvingWork[workIndex] = moved;
            resolvingWork.PopBack();
            operation.resolvingWorkIndex = InvalidMaterialMaterializationIndex;
            if (workIndex < resolvingWork.Size())
                operations[moved].resolvingWorkIndex = workIndex;
            if (nextResolvingWork >= resolvingWork.Size())
                nextResolvingWork = 0;
        }

        void AddReadyWork(const u32 index) noexcept
        {
            Operation& operation = operations[index];
            if (operation.readyWorkIndex != InvalidMaterialMaterializationIndex)
                return;
            operation.readyWorkIndex = readyWork.Size();
            readyWork.PushBack(index);
        }

        void RemoveReadyWork(const u32 index) noexcept
        {
            Operation& operation = operations[index];
            if (operation.readyWorkIndex == InvalidMaterialMaterializationIndex)
                return;
            const u32 workIndex = operation.readyWorkIndex;
            const u32 moved = readyWork.Back();
            readyWork[workIndex] = moved;
            readyWork.PopBack();
            operation.readyWorkIndex = InvalidMaterialMaterializationIndex;
            if (workIndex < readyWork.Size())
                operations[moved].readyWorkIndex = workIndex;
        }

        void BuildResolvingScratch() noexcept
        {
            resolvingScratch.Clear();
            if (resolvingWork.Empty())
            {
                nextResolvingWork = 0;
                return;
            }
            const u32 count = resolvingWork.Size() < config.maximumOperationsProgressedPerUpdate ? resolvingWork.Size() : config.maximumOperationsProgressedPerUpdate;
            for (u32 offset = 0; offset < count; ++offset)
            {
                const u32 workIndex = (nextResolvingWork + offset) % resolvingWork.Size();
                const u32 operationIndex = resolvingWork[workIndex];
                resolvingScratch.PushBack({operationIndex, operations[operationIndex].generation});
            }
            nextResolvingWork = (nextResolvingWork + count) % resolvingWork.Size();
        }

        void Recycle(const u32 index) noexcept
        {
            Operation& operation = operations[index];
            RemoveResolvingWork(index);
            RemoveReadyWork(index);
            operation.root.Reset();
            operation.resolveTickets.Clear();
            operation.resourceReferences.Clear();
            operation.gpuResources.Clear();
            operation.parameterBytes.Clear();
            operation.layout = {};
            operation.key = {};
            operation.handle = {};
            operation.failure = {};
            operation.retirementFences = {};
            operation.nextResourceReferenceToRetire = 0;
            operation.nextRoleToPoll = 0;
            operation.resolvedRoleCount = 0;
            operation.state = OperationState::Free;
            operation.cancelRequested = false;
            operation.definitionReleased = false;
            recycledOperations.PushBack(index);
            --stats.activeOperations;
        }

        void CancelResolution(Operation& operation) noexcept
        {
            for (MaterialResourceResolveTicket& ticket : operation.resolveTickets)
                if (ticket.IsValid())
                    static_cast<void>(resources->Cancel(ticket));
            for (MaterialResourceReference& reference : operation.resourceReferences)
                reference.Reset();
            operation.resolveTickets.Clear();
            operation.resourceReferences.Clear();
            operation.gpuResources.Clear();
        }

        [[nodiscard]] GpuSceneDefinitionKey BuildKey(const Operation& operation, const MaterialProgramLayoutView& layout, const containers::ArraySpan<const u8> parameterBytes) const noexcept
        {
            static constexpr char Domain[] = "Vanguard.GpuMaterial.ResolvedImage.v1";
            crypto::Sha256Builder hash;
            static_cast<void>(hash.Update(Domain, sizeof(Domain) - 1u));
            static_cast<void>(hash.Update(layout.layoutFingerprint.bytes, crypto::Digest256::ByteCount));
            HashU32(hash, parameterBytes.Size());
            static_cast<void>(hash.Update(parameterBytes.Data(), parameterBytes.Size()));
            HashU32(hash, layout.resources.Size());
            for (u32 index = 0; index < layout.resources.Size(); ++index)
            {
                const shaders::MaterialResourceRole& role = layout.resources[index];
                const MaterialResourceResolvedIdentity identity = operation.resourceReferences[index].GetIdentity();
                const GpuMaterialResource& gpuResource = operation.resourceReferences[index].GetGpuResource();
                HashU32(hash, role.slot);
                HashU8(hash, static_cast<u8>(role.kind));
                static_cast<void>(hash.Update(role.typeFingerprint.bytes, crypto::Digest256::ByteCount));
                HashShape(hash, role.shape);
                HashU8(hash, static_cast<u8>(identity.kind));
                HashU8(hash, static_cast<u8>(identity.descriptorDomain));
                HashU32(hash, identity.index);
                HashU32(hash, identity.generation);
                HashU32(hash, gpuResource.resource);
                HashU32(hash, gpuResource.samplerDescriptor);
                HashU32(hash, gpuResource.flags);
                HashU32(hash, static_cast<u32>(gpuResource.type));
            }
            crypto::Digest256 digest;
            static_cast<void>(hash.Finalize(digest));
            GpuSceneDefinitionKey key;
            std::memcpy(key.words, digest.bytes, sizeof(key.words));
            return key;
        }

        void ClearBatch() noexcept
        {
            frozenOperations.Clear();
            frozenDefinitions.Clear();
            consumedHandles.Clear();
            batch = {};
        }
    };

    GpuMaterialReference::~GpuMaterialReference()
    {
        VG_ASSERT_MSG(!IsValid(), "GPU material reference must be retired before destruction");
    }

    GpuMaterialReference::GpuMaterialReference(GpuMaterialReference&& other) noexcept
        : m_owner(other.m_owner), m_index(other.m_index), m_generation(other.m_generation), m_handle(other.m_handle), m_key(other.m_key)
    {
        other.m_owner = nullptr;
        other.m_index = InvalidMaterialMaterializationIndex;
        other.m_generation = 0;
        other.m_handle = {};
        other.m_key = {};
    }

    GpuMaterialReference& GpuMaterialReference::operator=(GpuMaterialReference&& other) noexcept
    {
        if (this == &other)
            return *this;
        VG_ASSERT_MSG(!IsValid(), "move assignment cannot overwrite a live GPU material reference");
        m_owner = other.m_owner;
        m_index = other.m_index;
        m_generation = other.m_generation;
        m_handle = other.m_handle;
        m_key = other.m_key;
        other.m_owner = nullptr;
        other.m_index = InvalidMaterialMaterializationIndex;
        other.m_generation = 0;
        other.m_handle = {};
        other.m_key = {};
        return *this;
    }

    bool GpuMaterialReference::IsValid() const noexcept
    {
        return m_owner != nullptr && m_owner->IsReferenceValid(m_index, m_generation);
    }

    void GpuMaterialReference::Abandon() noexcept
    {
        if (m_owner != nullptr)
            static_cast<void>(m_owner->AbandonReference(m_index, m_generation));
        m_owner = nullptr;
        m_index = InvalidMaterialMaterializationIndex;
        m_generation = 0;
        m_handle = {};
        m_key = {};
    }

    GpuMaterialHandle GpuMaterialReference::GetHandle() const noexcept
    {
        return IsValid() ? m_handle : GpuMaterialHandle{};
    }

    GpuSceneDefinitionKey GpuMaterialReference::GetKey() const noexcept
    {
        return IsValid() ? m_key : GpuSceneDefinitionKey{};
    }

    bool GpuMaterialReference::Retire(const rhi::ResidencyFenceSet& safeAfter, MaterialMaterializerFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsValid())
            return Fail(failure, MaterialMaterializerFailureCode::InvalidArgument, "GPU material reference is invalid");
        if (!m_owner->RetireReference(m_index, m_generation, safeAfter, failure))
            return false;
        m_owner = nullptr;
        m_index = InvalidMaterialMaterializationIndex;
        m_generation = 0;
        m_handle = {};
        m_key = {};
        return true;
    }

    MaterialMaterializer::~MaterialMaterializer()
    {
        VG_ASSERT_MSG(m_impl == nullptr, "material materializer must be shut down before destruction");
    }

    bool MaterialMaterializer::Initialize(MaterialProgramLayoutRegistry& layouts, MaterialResourceResolver& resources, GpuSceneRuntime& runtime, const MaterialMaterializerConfig& config,
                                          MaterialMaterializerFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, MaterialMaterializerFailureCode::AlreadyInitialized, "material materializer is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialMaterializerFailureCode::WrongThread, "material materializer initialization must run on the main thread");
        if (!layouts.IsInitialized() || !resources.IsInitialized() || !runtime.IsInitialized() || config.maximumOperations == 0 || config.maximumRolesPerMaterial == 0 || config.maximumMaterialsPerBatch == 0 ||
            config.maximumOperationsProgressedPerUpdate == 0 || config.maximumResourceRolePollsPerUpdate == 0 || config.maximumMaterialsPerBatch > config.maximumOperations)
            return Fail(failure, MaterialMaterializerFailureCode::InvalidConfiguration, "material materializer configuration or dependency is invalid");
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, MaterialMaterializerFailureCode::CapacityExceeded, "material materializer allocation failed");
        m_impl = new (block.address) Impl(config);
        m_impl->layouts = &layouts;
        m_impl->resources = &resources;
        m_impl->runtime = &runtime;
        m_impl->definitions = &runtime.GetDefinitions();
        return true;
    }

    bool MaterialMaterializer::Shutdown(MaterialMaterializerFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialMaterializerFailureCode::WrongThread, "material materializer shutdown must run on the main thread");
        if (m_impl->stats.activeOperations != 0 || m_impl->batch.IsValid())
            return Fail(failure, MaterialMaterializerFailureCode::LiveWorkRemains, "material materializer cannot shut down with live work or references");
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool MaterialMaterializer::AbandonDevice(MaterialMaterializerFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialMaterializerFailureCode::WrongThread, "material materializer abandonment must run on the main thread");
        m_impl->ClearBatch();
        for (u32 index = 0; index < m_impl->operations.Size(); ++index)
        {
            Impl::Operation& operation = m_impl->operations[index];
            if (operation.state == OperationState::Free)
                continue;
            for (MaterialResourceResolveTicket& ticket : operation.resolveTickets)
                if (ticket.IsValid())
                    static_cast<void>(m_impl->resources->Cancel(ticket));
            for (MaterialResourceReference& reference : operation.resourceReferences)
                reference.Abandon();
            operation.resolveTickets.Clear();
            operation.resourceReferences.Clear();
            m_impl->Recycle(index);
        }
        m_impl->stats.pendingOperations = 0;
        m_impl->stats.readyResults = 0;
        m_impl->stats.liveReferences = 0;
        return true;
    }

    bool MaterialMaterializer::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool MaterialMaterializer::Begin(const resources::ResourceHandle& material, MaterialMaterializationTicket& ticket, MaterialMaterializerFailure* const failure) noexcept
    {
        ClearFailure(failure);
        ticket = {};
        if (m_impl == nullptr)
            return Fail(failure, MaterialMaterializerFailureCode::NotInitialized, "material materializer is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialMaterializerFailureCode::WrongThread, "material materialization must begin on the main thread");
        if (!material.IsValid() || material.GetType() != materials::MaterialResourceType)
            return Fail(failure, MaterialMaterializerFailureCode::InvalidArgument, "material materialization requires a loaded VMAT handle");
        const auto* const object = static_cast<const materials::MaterialResourceObject*>(material.Get());
        if (object == nullptr || !object->IsOpen())
            return Fail(failure, MaterialMaterializerFailureCode::InvalidClosure, "material VMAT object is not open");
        const materials::MaterialFile& file = object->GetFile();
        const materials::LoadedMaterialDependency* const shaderDependency = FindDependency(object->GetLoadedDependencies(), file.GetShader());
        if (shaderDependency == nullptr || !shaderDependency->handle.IsValid() || shaderDependency->handle.GetType() != shaders::ShaderResourceType)
            return Fail(failure, MaterialMaterializerFailureCode::InvalidClosure, "material closure has no retained shader generation");
        const auto* const shader = static_cast<const shaders::ShaderResourceObject*>(shaderDependency->handle.Get());
        if (shader == nullptr || !shader->IsOpen())
            return Fail(failure, MaterialMaterializerFailureCode::InvalidClosure, "material closure retained an invalid shader object");

        MaterialProgramLayoutId layout;
        MaterialProgramLayoutFailure layoutFailure;
        if (!m_impl->layouts->Register(*shader, layout, &layoutFailure))
            return Fail(failure, MaterialMaterializerFailureCode::LayoutFailure, layoutFailure.message != nullptr ? layoutFailure.message : "material program-layout registration failed", layoutFailure);
        MaterialProgramLayoutView layoutView;
        if (!m_impl->layouts->Get(layout, layoutView) || layoutView.layoutFingerprint != file.GetMaterialLayoutFingerprint() || layoutView.domainFingerprint != file.GetMaterialDomainFingerprint() ||
            layoutView.parameterByteSize != file.GetParameterData().Size() || layoutView.resources.Size() != file.GetResourceParameters().Size() ||
            layoutView.resources.Size() > m_impl->config.maximumRolesPerMaterial)
            return Fail(failure, layoutView.resources.Size() > m_impl->config.maximumRolesPerMaterial ? MaterialMaterializerFailureCode::CapacityExceeded : MaterialMaterializerFailureCode::InvalidClosure,
                        "material closure disagrees with its registered shader layout");

        const u32 operationIndex = m_impl->AllocateOperation();
        if (operationIndex == InvalidMaterialMaterializationIndex)
            return Fail(failure, MaterialMaterializerFailureCode::CapacityExceeded, "material materialization operation capacity is exhausted");
        Impl::Operation& operation = m_impl->operations[operationIndex];
        operation.root = material;
        operation.layout = layout;
        operation.resolveTickets.Resize(layoutView.resources.Size());
        operation.resourceReferences.Resize(layoutView.resources.Size());
        operation.gpuResources.Resize(layoutView.resources.Size());
        operation.parameterBytes = file.GetParameterData();
        const containers::ArraySpan<const materials::ResourceParameterRecord> parameters = file.GetResourceParameters();

        for (u32 roleIndex = 0; roleIndex < layoutView.resources.Size(); ++roleIndex)
        {
            const shaders::MaterialResourceRole& role = layoutView.resources[roleIndex];
            const materials::ResourceParameterRecord& parameter = parameters[roleIndex];
            if (parameter.slot != roleIndex || role.slot != roleIndex || ToShaderKind(parameter.kind) != role.kind || parameter.expectedAssetType == resources::InvalidResourceTypeId)
            {
                m_impl->CancelResolution(operation);
                --m_impl->stats.pendingOperations;
                m_impl->Recycle(operationIndex);
                return Fail(failure, MaterialMaterializerFailureCode::InvalidClosure, "material resource slot disagrees with shader reflection");
            }
            MaterialResourceResolveRequest request;
            request.role = role;
            request.expectedAssetType = parameter.expectedAssetType;
            request.dependency = parameter.dependency;
            if (parameter.resource.IsValid() && parameter.dependency != resources::DependencyKind::Soft)
            {
                const materials::LoadedMaterialDependency* const dependency = FindDependency(object->GetLoadedDependencies(), parameter.resource);
                if (dependency == nullptr || dependency->kind != parameter.dependency)
                {
                    m_impl->CancelResolution(operation);
                    --m_impl->stats.pendingOperations;
                    m_impl->Recycle(operationIndex);
                    return Fail(failure, MaterialMaterializerFailureCode::InvalidClosure, "material resource dependency is absent from the loaded closure");
                }
                request.resource = dependency->handle;
            }
            MaterialResourceResolverFailure resourceFailure;
            if (!m_impl->resources->Begin(request, operation.resolveTickets[roleIndex], &resourceFailure))
            {
                m_impl->CancelResolution(operation);
                --m_impl->stats.pendingOperations;
                m_impl->Recycle(operationIndex);
                return Fail(failure, MaterialMaterializerFailureCode::ResourceFailure, resourceFailure.message != nullptr ? resourceFailure.message : "material role resolution failed to begin", {},
                            resourceFailure);
            }
        }

        m_impl->AddResolvingWork(operationIndex);
        ticket = {operationIndex, operation.generation};
        ++m_impl->stats.requestsBegun;
        return true;
    }

    bool MaterialMaterializer::Update(MaterialMaterializerFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialMaterializerFailureCode::NotInitialized, "material materializer is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialMaterializerFailureCode::WrongThread, "material materializer update must run on the main thread");

        if (m_impl->batch.IsValid())
        {
            const GpuMaterialDefinitionBatchState batchState = m_impl->definitions->GetMaterialBatchState(m_impl->batch);
            if (batchState == GpuMaterialDefinitionBatchState::Invalid)
                return Fail(failure, MaterialMaterializerFailureCode::DefinitionFailure, "material publication batch became invalid");
            bool cancelBatch = false;
            for (const u32 operationIndex : m_impl->frozenOperations)
                cancelBatch = cancelBatch || m_impl->operations[operationIndex].cancelRequested;
            if (batchState == GpuMaterialDefinitionBatchState::Prepared && cancelBatch)
            {
                GpuSceneDefinitionFailure definitionFailure;
                if (!m_impl->definitions->CancelMaterials(m_impl->batch, &definitionFailure))
                    return Fail(failure, MaterialMaterializerFailureCode::DefinitionFailure, definitionFailure.message != nullptr ? definitionFailure.message : "material publication rollback failed", {}, {},
                                definitionFailure);
                for (const u32 operationIndex : m_impl->frozenOperations)
                {
                    Impl::Operation& operation = m_impl->operations[operationIndex];
                    --m_impl->stats.publicationPending;
                    if (operation.cancelRequested)
                    {
                        m_impl->CancelResolution(operation);
                        m_impl->Recycle(operationIndex);
                    }
                    else
                    {
                        operation.state = OperationState::ReadyForBatch;
                        m_impl->AddReadyWork(operationIndex);
                        ++m_impl->stats.pendingOperations;
                    }
                }
                m_impl->ClearBatch();
            }
            else if (batchState == GpuMaterialDefinitionBatchState::Prepared)
            {
                GpuSceneDefinitionFailure definitionFailure;
                if (!m_impl->definitions->StageMaterials(*m_impl->runtime, m_impl->batch, &definitionFailure))
                    return Fail(failure, MaterialMaterializerFailureCode::DefinitionFailure, definitionFailure.message != nullptr ? definitionFailure.message : "material publication staging failed", {}, {},
                                definitionFailure);
            }
            else if (batchState == GpuMaterialDefinitionBatchState::Accepted)
            {
                m_impl->consumedHandles.Resize(m_impl->frozenOperations.Size());
                GpuSceneDefinitionPublication publication;
                GpuSceneDefinitionFailure definitionFailure;
                if (!m_impl->definitions->ConsumeMaterials(m_impl->batch, {m_impl->consumedHandles.TypedData(), m_impl->consumedHandles.Size()}, publication, &definitionFailure))
                    return Fail(failure, MaterialMaterializerFailureCode::DefinitionFailure,
                                definitionFailure.message != nullptr ? definitionFailure.message : "accepted material publication could not be consumed", {}, {}, definitionFailure);
                for (u32 frozenIndex = 0; frozenIndex < m_impl->frozenOperations.Size(); ++frozenIndex)
                {
                    const u32 operationIndex = m_impl->frozenOperations[frozenIndex];
                    Impl::Operation& operation = m_impl->operations[operationIndex];
                    operation.handle = m_impl->consumedHandles[frozenIndex];
                    --m_impl->stats.publicationPending;
                    if (operation.cancelRequested)
                    {
                        static_cast<void>(m_impl->definitions->Release(operation.handle));
                        operation.handle = {};
                        m_impl->CancelResolution(operation);
                        m_impl->Recycle(operationIndex);
                        continue;
                    }
                    operation.state = OperationState::Ready;
                    ++m_impl->stats.readyResults;
                    ++m_impl->stats.materializationsPublished;
                }
                m_impl->stats.definitionReuses += publication.reusedDefinitions;
                m_impl->ClearBatch();
            }
        }

        m_impl->BuildResolvingScratch();
        u32 remainingRolePolls = m_impl->config.maximumResourceRolePollsPerUpdate;
        for (u32 workOffset = 0; workOffset < m_impl->resolvingScratch.Size(); ++workOffset)
        {
            const Impl::WorkRef work = m_impl->resolvingScratch[workOffset];
            Impl::Operation* const current = m_impl->Find(work.index, work.generation);
            if (current == nullptr || current->state != OperationState::Resolving)
                continue;
            Impl::Operation& operation = *current;
            const u32 operationsLeft = m_impl->resolvingScratch.Size() - workOffset;
            u32 operationPollBudget = remainingRolePolls / operationsLeft;
            if (operationPollBudget == 0 && remainingRolePolls != 0)
                operationPollBudget = 1;
            ++m_impl->stats.operationStateChecks;
            for (u32 operationPoll = 0; operationPoll < operationPollBudget && remainingRolePolls != 0 && operation.resolvedRoleCount < operation.resolveTickets.Size(); ++operationPoll)
            {
                const u32 roleIndex = operation.nextRoleToPoll;
                operation.nextRoleToPoll = (operation.nextRoleToPoll + 1u) % operation.resolveTickets.Size();
                if (operation.resourceReferences[roleIndex].IsValid())
                    continue;
                --remainingRolePolls;
                ++m_impl->stats.resourceRolePolls;
                MaterialResourceResolverFailure resourceFailure;
                const MaterialResourceResolveStatus status = m_impl->resources->Poll(operation.resolveTickets[roleIndex], operation.resourceReferences[roleIndex], &resourceFailure);
                if (status == MaterialResourceResolveStatus::Pending)
                    continue;
                if (status == MaterialResourceResolveStatus::Failed)
                {
                    operation.failure = {
                        MaterialMaterializerFailureCode::ResourceFailure, resourceFailure.message != nullptr ? resourceFailure.message : "material role resolution failed", {}, resourceFailure, {}};
                    m_impl->CancelResolution(operation);
                    operation.state = OperationState::Failed;
                    m_impl->RemoveResolvingWork(work.index);
                    --m_impl->stats.pendingOperations;
                    ++m_impl->stats.materializationsFailed;
                    break;
                }
                ++operation.resolvedRoleCount;
            }
            if (operation.state != OperationState::Resolving || operation.resolvedRoleCount != operation.resolveTickets.Size())
                continue;
            MaterialProgramLayoutView layout;
            const auto* const object = static_cast<const materials::MaterialResourceObject*>(operation.root.Get());
            if (object == nullptr || !m_impl->layouts->Get(operation.layout, layout))
            {
                operation.failure = {MaterialMaterializerFailureCode::InvalidClosure, "material closure or program layout became invalid"};
                m_impl->CancelResolution(operation);
                operation.state = OperationState::Failed;
                m_impl->RemoveResolvingWork(work.index);
                --m_impl->stats.pendingOperations;
                ++m_impl->stats.materializationsFailed;
                continue;
            }
            for (u32 roleIndex = 0; roleIndex < operation.resourceReferences.Size(); ++roleIndex)
                operation.gpuResources[roleIndex] = operation.resourceReferences[roleIndex].GetGpuResource();
            operation.key = m_impl->BuildKey(operation, layout, operation.parameterBytes);
            operation.state = OperationState::ReadyForBatch;
            m_impl->RemoveResolvingWork(work.index);
            m_impl->AddReadyWork(work.index);
        }

        if (!m_impl->batch.IsValid())
        {
            m_impl->frozenOperations.Clear();
            m_impl->frozenDefinitions.Clear();
            while (!m_impl->readyWork.Empty() && m_impl->frozenOperations.Size() < m_impl->config.maximumMaterialsPerBatch)
            {
                const u32 operationIndex = m_impl->readyWork.Back();
                Impl::Operation& operation = m_impl->operations[operationIndex];
                m_impl->RemoveReadyWork(operationIndex);
                GpuMaterialDefinition definition;
                definition.key = operation.key;
                definition.material.materialLayout = operation.layout.index;
                definition.material.flags = GpuMaterialFlags::Resident;
                definition.resources = {operation.gpuResources.TypedData(), operation.gpuResources.Size()};
                definition.parameterBytes = operation.parameterBytes;
                m_impl->frozenOperations.PushBack(operationIndex);
                m_impl->frozenDefinitions.PushBack(definition);
            }
            if (!m_impl->frozenOperations.Empty())
            {
                GpuSceneDefinitionFailure definitionFailure;
                if (!m_impl->definitions->PrepareMaterials({m_impl->frozenDefinitions.TypedData(), m_impl->frozenDefinitions.Size()}, m_impl->batch, &definitionFailure))
                {
                    for (const u32 operationIndex : m_impl->frozenOperations)
                        m_impl->AddReadyWork(operationIndex);
                    m_impl->ClearBatch();
                    return Fail(failure, MaterialMaterializerFailureCode::DefinitionFailure, definitionFailure.message != nullptr ? definitionFailure.message : "material definition preparation failed", {}, {},
                                definitionFailure);
                }
                for (const u32 operationIndex : m_impl->frozenOperations)
                {
                    Impl::Operation& operation = m_impl->operations[operationIndex];
                    operation.state = OperationState::Frozen;
                    --m_impl->stats.pendingOperations;
                    ++m_impl->stats.publicationPending;
                }
            }
        }
        return true;
    }

    MaterialMaterializationStatus MaterialMaterializer::Poll(MaterialMaterializationTicket& ticket, GpuMaterialReference& material, MaterialMaterializerFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread() || !ticket.IsValid() || material.IsValid())
        {
            static_cast<void>(Fail(failure,
                                   m_impl == nullptr              ? MaterialMaterializerFailureCode::NotInitialized
                                   : !concurrency::IsMainThread() ? MaterialMaterializerFailureCode::WrongThread
                                                                  : MaterialMaterializerFailureCode::InvalidArgument,
                                   "material materialization poll is invalid"));
            return MaterialMaterializationStatus::Failed;
        }
        Impl::Operation* const operation = m_impl->Find(ticket.index, ticket.generation);
        if (operation == nullptr)
        {
            ticket = {};
            static_cast<void>(Fail(failure, MaterialMaterializerFailureCode::StaleTicket, "material materialization ticket is stale"));
            return MaterialMaterializationStatus::Failed;
        }
        if (operation->state == OperationState::Failed)
        {
            if (failure != nullptr)
                *failure = operation->failure;
            const u32 index = ticket.index;
            ticket = {};
            m_impl->Recycle(index);
            return MaterialMaterializationStatus::Failed;
        }
        if (operation->state == OperationState::Ready)
        {
            material.m_owner = this;
            material.m_index = ticket.index;
            material.m_generation = ticket.generation;
            material.m_handle = operation->handle;
            material.m_key = operation->key;
            operation->state = OperationState::Leased;
            --m_impl->stats.readyResults;
            ++m_impl->stats.liveReferences;
            ticket = {};
            return MaterialMaterializationStatus::Ready;
        }
        return operation->state == OperationState::Frozen ? MaterialMaterializationStatus::PublicationPending : MaterialMaterializationStatus::Pending;
    }

    bool MaterialMaterializer::Cancel(MaterialMaterializationTicket& ticket, MaterialMaterializerFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialMaterializerFailureCode::NotInitialized, "material materializer is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialMaterializerFailureCode::WrongThread, "material cancellation must run on the main thread");
        if (!ticket.IsValid())
            return Fail(failure, MaterialMaterializerFailureCode::InvalidArgument, "material cancellation ticket is invalid");
        Impl::Operation* const operation = m_impl->Find(ticket.index, ticket.generation);
        if (operation == nullptr)
        {
            ticket = {};
            return Fail(failure, MaterialMaterializerFailureCode::StaleTicket, "material cancellation ticket is stale");
        }
        const u32 operationIndex = ticket.index;
        ticket = {};
        ++m_impl->stats.materializationsCancelled;
        if (operation->state == OperationState::Frozen)
        {
            operation->cancelRequested = true;
            return true;
        }
        if (operation->state == OperationState::Ready)
        {
            static_cast<void>(m_impl->definitions->Release(operation->handle));
            --m_impl->stats.readyResults;
        }
        else if (operation->state == OperationState::Resolving || operation->state == OperationState::ReadyForBatch)
            --m_impl->stats.pendingOperations;
        m_impl->CancelResolution(*operation);
        m_impl->Recycle(operationIndex);
        return true;
    }

    bool MaterialMaterializer::RetireReference(const u32 index, const u32 generation, const rhi::ResidencyFenceSet& safeAfter, MaterialMaterializerFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return Fail(failure, m_impl == nullptr ? MaterialMaterializerFailureCode::NotInitialized : MaterialMaterializerFailureCode::WrongThread, "GPU material reference retirement is invalid");
        Impl::Operation* const operation = m_impl->Find(index, generation);
        if (operation == nullptr || (operation->state != OperationState::Leased && operation->state != OperationState::Retiring))
            return Fail(failure, MaterialMaterializerFailureCode::StaleTicket, "GPU material reference is stale");
        if (!CompleteCutover(safeAfter))
            return Fail(failure, MaterialMaterializerFailureCode::MissingRetirementFence, "GPU material retirement requires graphics, compute, and copy cutover fences");
        operation->state = OperationState::Retiring;
        operation->retirementFences = safeAfter;
        if (!operation->definitionReleased)
        {
            GpuSceneDefinitionFailure definitionFailure;
            if (!m_impl->definitions->Release(operation->handle, &definitionFailure))
                return Fail(failure, MaterialMaterializerFailureCode::DefinitionFailure, definitionFailure.message != nullptr ? definitionFailure.message : "GPU material definition retirement failed", {}, {},
                            definitionFailure);
            operation->definitionReleased = true;
            operation->handle = {};
        }
        while (operation->nextResourceReferenceToRetire < operation->resourceReferences.Size())
        {
            MaterialResourceResolverFailure resourceFailure;
            if (!operation->resourceReferences[operation->nextResourceReferenceToRetire].Retire(operation->retirementFences, &resourceFailure))
                return Fail(failure, MaterialMaterializerFailureCode::ResourceFailure, resourceFailure.message != nullptr ? resourceFailure.message : "material resource reference retirement failed", {},
                            resourceFailure);
            ++operation->nextResourceReferenceToRetire;
        }
        --m_impl->stats.liveReferences;
        m_impl->Recycle(index);
        return true;
    }

    bool MaterialMaterializer::AbandonReference(const u32 index, const u32 generation) noexcept
    {
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return false;
        Impl::Operation* const operation = m_impl->Find(index, generation);
        if (operation == nullptr || (operation->state != OperationState::Leased && operation->state != OperationState::Retiring))
            return false;
        for (MaterialResourceReference& reference : operation->resourceReferences)
            reference.Abandon();
        operation->resourceReferences.Clear();
        --m_impl->stats.liveReferences;
        m_impl->Recycle(index);
        return true;
    }

    bool MaterialMaterializer::IsReferenceValid(const u32 index, const u32 generation) const noexcept
    {
        if (m_impl == nullptr || index >= m_impl->operations.Size())
            return false;
        const Impl::Operation& operation = m_impl->operations[index];
        return operation.generation == generation && (operation.state == OperationState::Leased || operation.state == OperationState::Retiring);
    }

    MaterialMaterializerStats MaterialMaterializer::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : MaterialMaterializerStats{};
    }

    namespace detail
    {
        bool MaterialMaterializerAccess::BeginResolved(MaterialMaterializer& materializer, const MaterialProgramLayoutId layout, const containers::ArraySpan<const u8> parameterBytes,
                                                       const containers::ArraySpan<MaterialResourceReference> references, MaterialMaterializationTicket& ticket,
                                                       MaterialMaterializerFailure* const failure) noexcept
        {
            ClearFailure(failure);
            ticket = {};
            MaterialMaterializer::Impl* const impl = materializer.m_impl;
            if (impl == nullptr)
                return Fail(failure, MaterialMaterializerFailureCode::NotInitialized, "material materializer is not initialized");
            if (!concurrency::IsMainThread())
                return Fail(failure, MaterialMaterializerFailureCode::WrongThread, "resolved material injection must run on the main thread");
            MaterialProgramLayoutView layoutView;
            if (!impl->layouts->Get(layout, layoutView) || layoutView.resources.Size() != references.Size() || layoutView.parameterByteSize != parameterBytes.Size() ||
                references.Size() > impl->config.maximumRolesPerMaterial)
                return Fail(failure, MaterialMaterializerFailureCode::InvalidArgument, "resolved material image disagrees with its program layout");
            for (const MaterialResourceReference& reference : references)
                if (!reference.IsValid())
                    return Fail(failure, MaterialMaterializerFailureCode::InvalidArgument, "resolved material image contains an invalid resource reference");
            const u32 operationIndex = impl->AllocateOperation();
            if (operationIndex == InvalidMaterialMaterializationIndex)
                return Fail(failure, MaterialMaterializerFailureCode::CapacityExceeded, "material materialization operation capacity is exhausted");
            MaterialMaterializer::Impl::Operation& operation = impl->operations[operationIndex];
            operation.layout = layout;
            operation.resourceReferences.Resize(references.Size());
            operation.gpuResources.Resize(references.Size());
            operation.parameterBytes = parameterBytes;
            for (u32 index = 0; index < references.Size(); ++index)
            {
                operation.resourceReferences[index] = static_cast<MaterialResourceReference&&>(references[index]);
                operation.gpuResources[index] = operation.resourceReferences[index].GetGpuResource();
            }
            operation.key = impl->BuildKey(operation, layoutView, operation.parameterBytes);
            operation.state = OperationState::ReadyForBatch;
            operation.resolvedRoleCount = references.Size();
            impl->AddReadyWork(operationIndex);
            ticket = {operationIndex, operation.generation};
            ++impl->stats.requestsBegun;
            return true;
        }
    } // namespace detail
} // namespace vanguard::rendering

#include <vanguard/rendering/gpu_scene_definitions.hpp>
#include <vanguard/rendering/gpu_scene_runtime.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        enum class DefinitionState : u8
        {
            Free,
            Publishing,
            Active
        };
        enum class PayloadKind : u8
        {
            Geometry,
            VertexStreams,
            PositionDecode,
            Material,
            MaterialResources,
            MaterialParameters,
            Renderable,
            Lods,
            Primitives,
            PhaseParticipations
        };

        struct Payload
        {
            GpuSceneAllocation allocation;
            PayloadKind kind = PayloadKind::Geometry;
            u32 definition = 0;
        };

        [[nodiscard]] constexpr u64 HandleIdentity(const u32 index, const u32 generation) noexcept
        {
            return (static_cast<u64>(generation) << 32u) | index;
        }

        template <typename Handle> [[nodiscard]] constexpr u64 HandleIdentity(const Handle handle) noexcept
        {
            return HandleIdentity(handle.index, handle.generation);
        }

        void ClearFailure(GpuSceneDefinitionFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(GpuSceneDefinitionFailure* const failure, const GpuSceneDefinitionFailureCode code, const char* const message, const GpuSceneDefinitionKey key = {},
                                const GpuSceneLifetimeFailure lifetimeFailure = {}, const GpuSceneUploadFailure uploadFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, key, message, lifetimeFailure, uploadFailure};
            return false;
        }

        [[nodiscard]] bool ValidIndexFormat(const GpuIndexFormat format) noexcept
        {
            return format == GpuIndexFormat::UInt16 || format == GpuIndexFormat::UInt32;
        }

        [[nodiscard]] bool ValidRange(const u32 first, const u32 count, const u32 capacity) noexcept
        {
            return first <= capacity && count <= capacity - first;
        }

        template <typename T> void CopyElements(T* const destination, const containers::ArraySpan<const T> source) noexcept
        {
            for (u32 index = 0; index < source.Size(); ++index)
                destination[index] = source[index];
        }
    } // namespace

    u32 GpuSceneDefinitionKey::CalcHash() const noexcept
    {
        u64 value = words[0] ^ (words[1] + 0x9e3779b97f4a7c15ull + (words[0] << 6u) + (words[0] >> 2u));
        value ^= words[2] + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
        value ^= words[3] + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
        value ^= value >> 33u;
        value *= 0xff51afd7ed558ccdull;
        value ^= value >> 33u;
        return static_cast<u32>(value ^ (value >> 32u));
    }

    struct GpuSceneDefinitions::Impl
    {
        struct GeometryRecord
        {
            GpuSceneDefinitionKey key;
            GpuSceneAllocation geometry;
            GpuSceneAllocation vertexStreams;
            GpuSceneAllocation positionDecode;
            u32 references = 0;
            u32 vertexStreamCount = 0;
            bool hasPositionDecode = false;
            DefinitionState state = DefinitionState::Free;
        };

        struct MaterialRecord
        {
            GpuSceneDefinitionKey key;
            GpuSceneAllocation material;
            GpuSceneAllocation resources;
            GpuSceneAllocation parameters;
            u32 references = 0;
            u32 resourceCount = 0;
            u32 parameterByteSize = 0;
            u32 parameterWordCount = 0;
            DefinitionState state = DefinitionState::Free;
        };

        struct FrozenMaterialDefinition
        {
            GpuSceneDefinitionKey key;
            GpuMaterial material;
            u32 firstResource = 0;
            u32 resourceCount = 0;
            u32 firstParameterWord = 0;
            u32 parameterWordCount = 0;
            u32 parameterByteSize = 0;
        };

        enum class MaterialBatchState : u8
        {
            None,
            Prepared,
            Staged,
            Written,
            Accepted
        };

        struct RenderableRecord
        {
            RenderableRecord() noexcept : materials(memory::pools::Rendering::GetInstance()) {}

            GpuSceneDefinitionKey key;
            GpuSceneAllocation renderable;
            GpuSceneAllocation lods;
            GpuSceneAllocation primitives;
            GpuSceneAllocation phaseParticipations;
            containers::DynamicArray<GpuMaterialHandle> materials;
            u32 references = 0;
            u32 lodCount = 0;
            u32 primitiveCount = 0;
            u32 phaseParticipationCount = 0;
            DefinitionState state = DefinitionState::Free;
        };

        Impl(GpuSceneLifetime& lifetimeOwner, GpuSceneUploader& uploaderOwner, const GpuSceneDefinitionsConfig& value) noexcept
            : geometries(memory::pools::Rendering::GetInstance()), materials(memory::pools::Rendering::GetInstance()), renderables(memory::pools::Rendering::GetInstance()),
              freeGeometries(memory::pools::Rendering::GetInstance()), freeMaterials(memory::pools::Rendering::GetInstance()), freeRenderables(memory::pools::Rendering::GetInstance()),
              geometryByKey(memory::pools::Rendering::GetInstance()), materialByKey(memory::pools::Rendering::GetInstance()), renderableByKey(memory::pools::Rendering::GetInstance()),
              geometryByHandle(memory::pools::Rendering::GetInstance()), materialByHandle(memory::pools::Rendering::GetInstance()), renderableByHandle(memory::pools::Rendering::GetInstance()),
              allocationRequests(memory::pools::Rendering::GetInstance()), allocations(memory::pools::Rendering::GetInstance()), payloads(memory::pools::Rendering::GetInstance()),
              uploadRequests(memory::pools::Rendering::GetInstance()), reservations(memory::pools::Rendering::GetInstance()), transactionRecords(memory::pools::Rendering::GetInstance()),
              transactionReuses(memory::pools::Rendering::GetInstance()), retirementAllocations(memory::pools::Rendering::GetInstance()), geometryReleaseCounts(memory::pools::Rendering::GetInstance()),
              materialReleaseCounts(memory::pools::Rendering::GetInstance()), frozenMaterials(memory::pools::Rendering::GetInstance()), frozenMaterialResources(memory::pools::Rendering::GetInstance()),
              frozenMaterialParameters(memory::pools::Rendering::GetInstance()), frozenMaterialAllocations(memory::pools::Rendering::GetInstance()),
              frozenMaterialPayloads(memory::pools::Rendering::GetInstance()), frozenMaterialUploadRequests(memory::pools::Rendering::GetInstance()),
              frozenMaterialRecords(memory::pools::Rendering::GetInstance()), frozenMaterialReuses(memory::pools::Rendering::GetInstance()), frozenMaterialHandles(memory::pools::Rendering::GetInstance()),
              lifetime(&lifetimeOwner), uploader(&uploaderOwner), config(value)
        {
            geometries.Reserve(config.maximumGeometries);
            materials.Reserve(config.maximumMaterials);
            renderables.Reserve(config.maximumRenderables);
            freeGeometries.Reserve(config.maximumGeometries);
            freeMaterials.Reserve(config.maximumMaterials);
            freeRenderables.Reserve(config.maximumRenderables);
            geometryByKey.Reserve(config.maximumGeometries);
            materialByKey.Reserve(config.maximumMaterials);
            renderableByKey.Reserve(config.maximumRenderables);
            geometryByHandle.Reserve(config.maximumGeometries);
            materialByHandle.Reserve(config.maximumMaterials);
            renderableByHandle.Reserve(config.maximumRenderables);
            allocationRequests.Reserve(config.maximumAllocationsPerBatch);
            allocations.Reserve(config.maximumAllocationsPerBatch);
            payloads.Reserve(config.maximumAllocationsPerBatch);
            uploadRequests.Reserve(config.maximumAllocationsPerBatch);
            reservations.Reserve(config.maximumAllocationsPerBatch);
            transactionRecords.Reserve(config.maximumDefinitionsPerBatch);
            transactionReuses.Reserve(config.maximumDefinitionsPerBatch);
            retirementAllocations.Reserve(config.maximumAllocationsPerBatch);
            geometryReleaseCounts.Reserve(config.maximumAllocationsPerBatch);
            materialReleaseCounts.Reserve(config.maximumAllocationsPerBatch);
            frozenMaterials.Reserve(config.maximumDefinitionsPerBatch);
            frozenMaterialResources.Reserve(config.maximumAllocationsPerBatch);
            frozenMaterialParameters.Reserve(config.maximumAllocationsPerBatch);
            frozenMaterialAllocations.Reserve(config.maximumAllocationsPerBatch);
            frozenMaterialPayloads.Reserve(config.maximumAllocationsPerBatch);
            frozenMaterialUploadRequests.Reserve(config.maximumAllocationsPerBatch);
            frozenMaterialRecords.Reserve(config.maximumDefinitionsPerBatch);
            frozenMaterialReuses.Reserve(config.maximumDefinitionsPerBatch);
            frozenMaterialHandles.Reserve(config.maximumDefinitionsPerBatch);
        }

        containers::DynamicArray<GeometryRecord> geometries;
        containers::DynamicArray<MaterialRecord> materials;
        containers::DynamicArray<RenderableRecord> renderables;
        containers::DynamicArray<u32> freeGeometries;
        containers::DynamicArray<u32> freeMaterials;
        containers::DynamicArray<u32> freeRenderables;
        containers::HashMap<GpuSceneDefinitionKey, u32> geometryByKey;
        containers::HashMap<GpuSceneDefinitionKey, u32> materialByKey;
        containers::HashMap<GpuSceneDefinitionKey, u32> renderableByKey;
        containers::HashMap<u64, u32> geometryByHandle;
        containers::HashMap<u64, u32> materialByHandle;
        containers::HashMap<u64, u32> renderableByHandle;
        containers::DynamicArray<GpuSceneAllocationRequest> allocationRequests;
        containers::DynamicArray<GpuSceneAllocation> allocations;
        containers::DynamicArray<Payload> payloads;
        containers::DynamicArray<GpuSceneUploadRequest> uploadRequests;
        containers::DynamicArray<GpuSceneUploadReservation> reservations;
        containers::DynamicArray<u32> transactionRecords;
        containers::DynamicArray<u32> transactionReuses;
        containers::DynamicArray<GpuSceneAllocation> retirementAllocations;
        containers::HashMap<u64, u32> geometryReleaseCounts;
        containers::HashMap<u64, u32> materialReleaseCounts;
        containers::DynamicArray<FrozenMaterialDefinition> frozenMaterials;
        containers::DynamicArray<GpuMaterialResource> frozenMaterialResources;
        containers::DynamicArray<GpuMaterialParameterWord> frozenMaterialParameters;
        containers::DynamicArray<GpuSceneAllocation> frozenMaterialAllocations;
        containers::DynamicArray<Payload> frozenMaterialPayloads;
        containers::DynamicArray<GpuSceneUploadRequest> frozenMaterialUploadRequests;
        containers::DynamicArray<u32> frozenMaterialRecords;
        containers::DynamicArray<u32> frozenMaterialReuses;
        containers::DynamicArray<GpuMaterialHandle> frozenMaterialHandles;
        GpuSceneDefinitionPublication frozenMaterialPublication;
        MaterialBatchState materialBatchState = MaterialBatchState::None;
        u32 materialBatchSerial = 0;
        GpuSceneLifetime* lifetime = nullptr;
        GpuSceneUploader* uploader = nullptr;
        GpuSceneDefinitionsConfig config;
        GpuSceneDefinitionsStats stats;

        void ResetTransaction() noexcept
        {
            allocationRequests.Clear();
            allocations.Clear();
            payloads.Clear();
            uploadRequests.Clear();
            reservations.Clear();
            transactionRecords.Clear();
            transactionReuses.Clear();
        }

        static constexpr u64 MaterialContributionMagic = 0x4d41544c44454653ull;

        [[nodiscard]] static constexpr GpuSceneContributionToken EncodeMaterialBatch(const GpuMaterialDefinitionBatch batch) noexcept
        {
            return {static_cast<u64>(batch.serial) | (static_cast<u64>(batch.definitionCount) << 32u), MaterialContributionMagic};
        }

        [[nodiscard]] static constexpr GpuMaterialDefinitionBatch DecodeMaterialBatch(const GpuSceneContributionToken token) noexcept
        {
            return token.value1 == MaterialContributionMagic ? GpuMaterialDefinitionBatch{static_cast<u32>(token.value0), static_cast<u32>(token.value0 >> 32u)} : GpuMaterialDefinitionBatch{};
        }

        void ResetMaterialBatch() noexcept
        {
            frozenMaterials.Clear();
            frozenMaterialResources.Clear();
            frozenMaterialParameters.Clear();
            frozenMaterialAllocations.Clear();
            frozenMaterialPayloads.Clear();
            frozenMaterialUploadRequests.Clear();
            frozenMaterialRecords.Clear();
            frozenMaterialReuses.Clear();
            frozenMaterialHandles.Clear();
            frozenMaterialPublication = {};
            materialBatchState = MaterialBatchState::None;
        }

        [[nodiscard]] u32 NewGeometryRecord() noexcept
        {
            if (!freeGeometries.Empty())
            {
                const u32 index = freeGeometries.PopBack();
                geometries[index] = {};
                return index;
            }
            if (geometries.Size() == config.maximumGeometries)
                return InvalidGpuSceneIndex;
            geometries.PushBack({});
            return geometries.Size() - 1u;
        }

        [[nodiscard]] u32 NewMaterialRecord() noexcept
        {
            if (!freeMaterials.Empty())
            {
                const u32 index = freeMaterials.PopBack();
                materials[index] = {};
                return index;
            }
            if (materials.Size() == config.maximumMaterials)
                return InvalidGpuSceneIndex;
            materials.PushBack({});
            return materials.Size() - 1u;
        }

        [[nodiscard]] u32 NewRenderableRecord() noexcept
        {
            if (!freeRenderables.Empty())
            {
                const u32 index = freeRenderables.PopBack();
                RenderableRecord& record = renderables[index];
                record.key = {};
                record.renderable = {};
                record.lods = {};
                record.primitives = {};
                record.phaseParticipations = {};
                record.materials.Clear();
                record.references = 0;
                record.lodCount = 0;
                record.primitiveCount = 0;
                record.phaseParticipationCount = 0;
                record.state = DefinitionState::Free;
                return index;
            }
            if (renderables.Size() == config.maximumRenderables)
                return InvalidGpuSceneIndex;
            renderables.PushBack(RenderableRecord{});
            return renderables.Size() - 1u;
        }

        void RecycleGeometry(const u32 index) noexcept
        {
            geometries[index] = {};
            freeGeometries.PushBack(index);
        }

        void RecycleMaterial(const u32 index) noexcept
        {
            materials[index] = {};
            freeMaterials.PushBack(index);
        }

        void RecycleRenderable(const u32 index) noexcept
        {
            RenderableRecord& record = renderables[index];
            record.key = {};
            record.renderable = {};
            record.lods = {};
            record.primitives = {};
            record.phaseParticipations = {};
            record.materials.Clear();
            record.references = 0;
            record.state = DefinitionState::Free;
            freeRenderables.PushBack(index);
        }

        [[nodiscard]] GeometryRecord* Find(const GpuGeometryHandle handle) noexcept
        {
            const u32* const index = geometryByHandle.FindPtr(HandleIdentity(handle));
            return index != nullptr && *index < geometries.Size() && geometries[*index].state == DefinitionState::Active ? &geometries[*index] : nullptr;
        }

        [[nodiscard]] MaterialRecord* Find(const GpuMaterialHandle handle) noexcept
        {
            const u32* const index = materialByHandle.FindPtr(HandleIdentity(handle));
            return index != nullptr && *index < materials.Size() && materials[*index].state == DefinitionState::Active ? &materials[*index] : nullptr;
        }

        [[nodiscard]] RenderableRecord* Find(const GpuRenderableHandle handle) noexcept
        {
            const u32* const index = renderableByHandle.FindPtr(HandleIdentity(handle));
            return index != nullptr && *index < renderables.Size() && renderables[*index].state == DefinitionState::Active ? &renderables[*index] : nullptr;
        }

        [[nodiscard]] bool BeginPublication(GpuSceneDefinitionFailure* const failure) noexcept
        {
            uploadRequests.Resize(payloads.Size());
            reservations.Resize(payloads.Size());
            for (u32 index = 0; index < payloads.Size(); ++index)
                uploadRequests[index] = {payloads[index].allocation, 0, payloads[index].allocation.count};
            GpuSceneUploadFailure uploadFailure;
            if (!uploader->Begin({uploadRequests.TypedData(), uploadRequests.Size()}, {reservations.TypedData(), reservations.Size()}, &uploadFailure))
                return Fail(failure, GpuSceneDefinitionFailureCode::UploadFailure, "GPU Scene definition upload planning failed", {}, {}, uploadFailure);
            return true;
        }
    };

    GpuSceneDefinitions::~GpuSceneDefinitions()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool GpuSceneDefinitions::Initialize(GpuSceneLifetime& lifetime, GpuSceneUploader& uploader, const GpuSceneDefinitionsConfig& config, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::AlreadyInitialized, "GPU Scene definitions are already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene definitions must initialize on the main thread");
        if (!lifetime.IsInitialized() || !uploader.IsInitialized() || config.maximumGeometries == 0 || config.maximumMaterials == 0 || config.maximumRenderables == 0 || config.maximumDefinitionsPerBatch == 0 ||
            config.maximumAllocationsPerBatch < 4 || config.maximumDefinitionsPerBatch > config.maximumAllocationsPerBatch || config.maximumMaterialResourcesPerBatch == 0 ||
            config.maximumMaterialParameterBytesPerBatch == 0)
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidConfiguration, "GPU Scene definition configuration is invalid");
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, GpuSceneDefinitionFailureCode::CapacityExceeded, "GPU Scene definition owner allocation failed");
        m_impl = ::new (block.address) Impl(lifetime, uploader, config);
        return true;
    }

    bool GpuSceneDefinitions::Shutdown(GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene definitions must shutdown on the main thread");
        if (m_impl->materialBatchState != Impl::MaterialBatchState::None)
            return Fail(failure, GpuSceneDefinitionFailureCode::Busy, "GPU Scene definitions cannot shutdown with an unconsumed material batch");
        if (m_impl->stats.geometries != 0 || m_impl->stats.materials != 0 || m_impl->stats.renderables != 0)
            return Fail(failure, GpuSceneDefinitionFailureCode::LiveDefinitionsRemain, "GPU Scene definitions cannot shutdown while references remain live");
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool GpuSceneDefinitions::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool GpuSceneDefinitions::AcquireGeometries(const containers::ArraySpan<const GpuGeometryDefinition> definitions, containers::ArraySpan<GpuGeometryHandle> handles,
                                                GpuSceneDefinitionPublication& publication, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        publication = {};
        for (GpuGeometryHandle& handle : handles)
            handle = {};
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene geometry acquisition must run on the main thread");
        if (definitions.Empty() || definitions.Size() != handles.Size() || definitions.Size() > m_impl->config.maximumDefinitionsPerBatch)
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidDefinition, "GPU Scene geometry definition batch is invalid");

        m_impl->ResetTransaction();
        auto rollback = [&]() noexcept
        {
            if (!m_impl->allocations.Empty())
                static_cast<void>(m_impl->lifetime->CancelBatch({m_impl->allocations.TypedData(), m_impl->allocations.Size()}));
            for (const u32 index : m_impl->transactionReuses)
                if (m_impl->geometries[index].references != 0)
                    --m_impl->geometries[index].references;
            for (const u32 index : m_impl->transactionRecords)
            {
                Impl::GeometryRecord& record = m_impl->geometries[index];
                static_cast<void>(m_impl->geometryByKey.Remove(record.key));
                m_impl->RecycleGeometry(index);
            }
            for (GpuGeometryHandle& handle : handles)
                handle = {};
            m_impl->ResetTransaction();
        };

        for (u32 definitionIndex = 0; definitionIndex < definitions.Size(); ++definitionIndex)
        {
            const GpuGeometryDefinition& definition = definitions[definitionIndex];
            if (!definition.key.IsValid() || definition.vertexStreams.Empty() || !ValidIndexFormat(definition.geometry.indexFormat))
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::InvalidDefinition, "GPU Scene geometry definition is invalid", definition.key);
            }
            if (const u32* const existingIndex = m_impl->geometryByKey.FindPtr(definition.key))
            {
                Impl::GeometryRecord& existing = m_impl->geometries[*existingIndex];
                if (existing.state == DefinitionState::Free || existing.vertexStreamCount != definition.vertexStreams.Size() || existing.hasPositionDecode != (definition.positionDecode != nullptr))
                {
                    rollback();
                    ++m_impl->stats.rejectedOperations;
                    return Fail(failure, GpuSceneDefinitionFailureCode::IncompatibleDefinition, "equal geometry keys describe incompatible definitions", definition.key);
                }
                ++existing.references;
                m_impl->transactionReuses.PushBack(*existingIndex);
                handles[definitionIndex] = existing.geometry.AsSlotHandle<GpuGeometryHandle>();
                continue;
            }

            const u32 recordIndex = m_impl->NewGeometryRecord();
            if (recordIndex == InvalidGpuSceneIndex)
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::CapacityExceeded, "GPU Scene geometry definition capacity is exhausted", definition.key);
            }
            Impl::GeometryRecord& record = m_impl->geometries[recordIndex];
            record.key = definition.key;
            record.references = 1;
            record.vertexStreamCount = definition.vertexStreams.Size();
            record.hasPositionDecode = definition.positionDecode != nullptr;
            record.state = DefinitionState::Publishing;
            static_cast<void>(m_impl->geometryByKey.Insert(record.key, recordIndex));
            m_impl->transactionRecords.PushBack(recordIndex);
            m_impl->allocationRequests.PushBack({GpuSceneTableKind::GeometryRange, 1});
            m_impl->allocationRequests.PushBack({GpuSceneTableKind::VertexStream, record.vertexStreamCount});
            if (record.hasPositionDecode)
                m_impl->allocationRequests.PushBack({GpuSceneTableKind::PositionDecode, 1});
        }

        if (m_impl->allocationRequests.Size() > m_impl->config.maximumAllocationsPerBatch)
        {
            rollback();
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::CapacityExceeded, "GPU Scene geometry batch exceeds allocation capacity");
        }
        if (!m_impl->allocationRequests.Empty())
        {
            m_impl->allocations.Resize(m_impl->allocationRequests.Size());
            GpuSceneLifetimeFailure lifetimeFailure;
            if (!m_impl->lifetime->AllocateBatch({m_impl->allocationRequests.TypedData(), m_impl->allocationRequests.Size()}, {m_impl->allocations.TypedData(), m_impl->allocations.Size()}, &lifetimeFailure))
            {
                m_impl->allocations.Clear();
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::LifetimeFailure, "GPU Scene geometry allocation failed", {}, lifetimeFailure);
            }

            u32 allocationIndex = 0;
            u32 newDefinitionIndex = 0;
            for (u32 definitionIndex = 0; definitionIndex < definitions.Size(); ++definitionIndex)
            {
                const u32* const recordIndex = m_impl->geometryByKey.FindPtr(definitions[definitionIndex].key);
                if (recordIndex == nullptr || newDefinitionIndex >= m_impl->transactionRecords.Size() || *recordIndex != m_impl->transactionRecords[newDefinitionIndex])
                    continue;
                Impl::GeometryRecord& record = m_impl->geometries[*recordIndex];
                record.geometry = m_impl->allocations[allocationIndex++];
                record.vertexStreams = m_impl->allocations[allocationIndex++];
                if (record.hasPositionDecode)
                    record.positionDecode = m_impl->allocations[allocationIndex++];
                handles[definitionIndex] = record.geometry.AsSlotHandle<GpuGeometryHandle>();
                m_impl->payloads.PushBack({record.geometry, PayloadKind::Geometry, definitionIndex});
                m_impl->payloads.PushBack({record.vertexStreams, PayloadKind::VertexStreams, definitionIndex});
                if (record.hasPositionDecode)
                    m_impl->payloads.PushBack({record.positionDecode, PayloadKind::PositionDecode, definitionIndex});
                ++newDefinitionIndex;
            }

            if (!m_impl->BeginPublication(failure))
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return false;
            }
            for (u32 payloadIndex = 0; payloadIndex < m_impl->payloads.Size(); ++payloadIndex)
            {
                const Payload payload = m_impl->payloads[payloadIndex];
                const GpuGeometryDefinition& definition = definitions[payload.definition];
                const u32* const recordIndex = m_impl->geometryByKey.FindPtr(definition.key);
                Impl::GeometryRecord& record = m_impl->geometries[*recordIndex];
                switch (payload.kind)
                {
                case PayloadKind::Geometry:
                {
                    GpuGeometryRange value = definition.geometry;
                    value.firstVertexStream = record.vertexStreams.first;
                    value.vertexStreamCount = record.vertexStreams.count;
                    value.positionDecode = record.hasPositionDecode ? record.positionDecode.first : InvalidGpuSceneIndex;
                    value.generation = record.geometry.generation;
                    *static_cast<GpuGeometryRange*>(m_impl->reservations[payloadIndex].destination) = value;
                    break;
                }
                case PayloadKind::VertexStreams:
                    CopyElements(static_cast<GpuVertexStream*>(m_impl->reservations[payloadIndex].destination), definition.vertexStreams);
                    break;
                case PayloadKind::PositionDecode:
                    *static_cast<GpuPositionDecode*>(m_impl->reservations[payloadIndex].destination) = *definition.positionDecode;
                    break;
                default:
                    break;
                }
                GpuSceneUploadFailure uploadFailure;
                if (!m_impl->uploader->Complete(m_impl->reservations[payloadIndex], &uploadFailure))
                {
                    static_cast<void>(m_impl->uploader->Cancel());
                    rollback();
                    ++m_impl->stats.rejectedOperations;
                    return Fail(failure, GpuSceneDefinitionFailureCode::UploadFailure, "GPU Scene geometry upload completion failed", definition.key, {}, uploadFailure);
                }
            }
            GpuSceneUploadResult uploadResult;
            GpuSceneUploadFailure uploadFailure;
            if (!m_impl->uploader->Submit(uploadResult, &uploadFailure))
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::UploadFailure, "GPU Scene geometry upload submission failed", {}, {}, uploadFailure);
            }
            publication.completion = uploadResult.completion;
            publication.uploadedRanges = uploadResult.uniqueUpdates;
            publication.uploadedBytes = uploadResult.uploadedBytes;
        }

        for (const u32 recordIndex : m_impl->transactionRecords)
        {
            Impl::GeometryRecord& record = m_impl->geometries[recordIndex];
            record.state = DefinitionState::Active;
            static_cast<void>(m_impl->geometryByHandle.Insert(HandleIdentity(record.geometry.first, record.geometry.generation), recordIndex));
        }
        for (u32 definitionIndex = 0; definitionIndex < definitions.Size(); ++definitionIndex)
        {
            const u32* const recordIndex = m_impl->geometryByKey.FindPtr(definitions[definitionIndex].key);
            handles[definitionIndex] = m_impl->geometries[*recordIndex].geometry.AsSlotHandle<GpuGeometryHandle>();
        }
        publication.requestedDefinitions = definitions.Size();
        publication.createdDefinitions = m_impl->transactionRecords.Size();
        publication.reusedDefinitions = definitions.Size() - publication.createdDefinitions;
        m_impl->stats.geometries += publication.createdDefinitions;
        m_impl->stats.references += definitions.Size();
        m_impl->stats.acquisitions += definitions.Size();
        m_impl->stats.reuses += publication.reusedDefinitions;
        m_impl->ResetTransaction();
        return true;
    }

    bool GpuSceneDefinitions::PrepareMaterials(const containers::ArraySpan<const GpuMaterialDefinition> definitions, GpuMaterialDefinitionBatch& batch, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        batch = {};
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene material preparation must run on the main thread");
        if (m_impl->materialBatchState != Impl::MaterialBatchState::None)
            return Fail(failure, GpuSceneDefinitionFailureCode::Busy, "GPU Scene definitions already own a material publication batch");
        if (definitions.Empty() || definitions.Size() > m_impl->config.maximumDefinitionsPerBatch)
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidDefinition, "GPU Scene material definition batch is invalid");

        m_impl->ResetTransaction();
        m_impl->ResetMaterialBatch();
        auto rollback = [&]() noexcept
        {
            if (!m_impl->frozenMaterialAllocations.Empty())
                static_cast<void>(m_impl->lifetime->CancelBatch({m_impl->frozenMaterialAllocations.TypedData(), m_impl->frozenMaterialAllocations.Size()}));
            for (const u32 index : m_impl->frozenMaterialReuses)
                if (m_impl->materials[index].references != 0)
                    --m_impl->materials[index].references;
            for (const u32 index : m_impl->frozenMaterialRecords)
            {
                Impl::MaterialRecord& record = m_impl->materials[index];
                static_cast<void>(m_impl->materialByKey.Remove(record.key));
                m_impl->RecycleMaterial(index);
            }
            m_impl->ResetTransaction();
            m_impl->ResetMaterialBatch();
        };

        u64 totalParameterBytes = 0;
        u64 totalResources = 0;
        for (u32 definitionIndex = 0; definitionIndex < definitions.Size(); ++definitionIndex)
        {
            const GpuMaterialDefinition& definition = definitions[definitionIndex];
            if (!definition.key.IsValid() || definition.material.parameterByteOffset != 0 || definition.material.parameterByteSize != 0 || definition.material.firstResource != 0 ||
                definition.material.resourceCount != 0 || definition.material.generation != 0 || (definition.resources.Size() != 0 && definition.resources.Data() == nullptr) ||
                (definition.parameterBytes.Size() != 0 && definition.parameterBytes.Data() == nullptr))
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::InvalidDefinition, "GPU Scene material definition contains pre-resolved ranges or invalid source spans", definition.key);
            }
            Impl::FrozenMaterialDefinition frozen;
            frozen.key = definition.key;
            frozen.material = definition.material;
            frozen.firstResource = m_impl->frozenMaterialResources.Size();
            frozen.resourceCount = definition.resources.Size();
            frozen.firstParameterWord = m_impl->frozenMaterialParameters.Size();
            frozen.parameterByteSize = definition.parameterBytes.Size();
            frozen.parameterWordCount = (frozen.parameterByteSize + 3u) / 4u;

            if (const u32* const existingIndex = m_impl->materialByKey.FindPtr(definition.key))
            {
                Impl::MaterialRecord& existing = m_impl->materials[*existingIndex];
                if (existing.state == DefinitionState::Free || existing.resourceCount != frozen.resourceCount || existing.parameterByteSize != frozen.parameterByteSize ||
                    existing.parameterWordCount != frozen.parameterWordCount)
                {
                    rollback();
                    ++m_impl->stats.rejectedOperations;
                    return Fail(failure, GpuSceneDefinitionFailureCode::IncompatibleDefinition, "equal material keys describe incompatible compound definitions", definition.key);
                }
                ++existing.references;
                m_impl->frozenMaterialReuses.PushBack(*existingIndex);
                m_impl->frozenMaterials.PushBack(frozen);
                continue;
            }

            totalResources += definition.resources.Size();
            totalParameterBytes += definition.parameterBytes.Size();
            if (totalResources > m_impl->config.maximumMaterialResourcesPerBatch || totalParameterBytes > m_impl->config.maximumMaterialParameterBytesPerBatch)
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::CapacityExceeded, "GPU Scene material payload batch exceeds configured capacity", definition.key);
            }
            for (const GpuMaterialResource& resource : definition.resources)
                m_impl->frozenMaterialResources.PushBack(resource);
            for (u32 wordIndex = 0; wordIndex < frozen.parameterWordCount; ++wordIndex)
            {
                GpuMaterialParameterWord word;
                for (u32 byteIndex = 0; byteIndex < 4; ++byteIndex)
                {
                    const u32 sourceIndex = wordIndex * 4u + byteIndex;
                    if (sourceIndex < frozen.parameterByteSize)
                        word.value |= static_cast<u32>(definition.parameterBytes[sourceIndex]) << (byteIndex * 8u);
                }
                m_impl->frozenMaterialParameters.PushBack(word);
            }
            m_impl->frozenMaterials.PushBack(frozen);

            const u32 recordIndex = m_impl->NewMaterialRecord();
            if (recordIndex == InvalidGpuSceneIndex)
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::CapacityExceeded, "GPU Scene material definition capacity is exhausted", definition.key);
            }
            Impl::MaterialRecord& record = m_impl->materials[recordIndex];
            record.key = definition.key;
            record.references = 1;
            record.resourceCount = frozen.resourceCount;
            record.parameterByteSize = frozen.parameterByteSize;
            record.parameterWordCount = frozen.parameterWordCount;
            record.state = DefinitionState::Publishing;
            static_cast<void>(m_impl->materialByKey.Insert(record.key, recordIndex));
            m_impl->frozenMaterialRecords.PushBack(recordIndex);
            m_impl->allocationRequests.PushBack({GpuSceneTableKind::Material, 1});
            if (record.resourceCount != 0)
                m_impl->allocationRequests.PushBack({GpuSceneTableKind::MaterialResource, record.resourceCount});
            if (record.parameterWordCount != 0)
                m_impl->allocationRequests.PushBack({GpuSceneTableKind::MaterialParameterWord, record.parameterWordCount});
        }

        if (m_impl->allocationRequests.Size() > m_impl->config.maximumAllocationsPerBatch)
        {
            rollback();
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::CapacityExceeded, "GPU Scene material batch exceeds allocation capacity");
        }

        m_impl->materialBatchSerial = m_impl->materialBatchSerial == 0xffffffffu ? 1u : m_impl->materialBatchSerial + 1u;
        if (m_impl->materialBatchSerial == 0)
            m_impl->materialBatchSerial = 1;
        batch = {m_impl->materialBatchSerial, definitions.Size()};
        m_impl->frozenMaterialPublication.requestedDefinitions = definitions.Size();
        m_impl->frozenMaterialPublication.createdDefinitions = m_impl->frozenMaterialRecords.Size();
        m_impl->frozenMaterialPublication.reusedDefinitions = definitions.Size() - m_impl->frozenMaterialRecords.Size();

        if (m_impl->frozenMaterialRecords.Empty())
        {
            m_impl->frozenMaterialHandles.Resize(definitions.Size());
            for (u32 index = 0; index < definitions.Size(); ++index)
            {
                const u32* const recordIndex = m_impl->materialByKey.FindPtr(definitions[index].key);
                m_impl->frozenMaterialHandles[index] = m_impl->materials[*recordIndex].material.AsSlotHandle<GpuMaterialHandle>();
            }
            m_impl->stats.references += definitions.Size();
            m_impl->stats.acquisitions += definitions.Size();
            m_impl->stats.reuses += definitions.Size();
            m_impl->materialBatchState = Impl::MaterialBatchState::Accepted;
            m_impl->ResetTransaction();
            return true;
        }

        m_impl->frozenMaterialAllocations.Resize(m_impl->allocationRequests.Size());
        GpuSceneLifetimeFailure lifetimeFailure;
        if (!m_impl->lifetime->AllocateBatch({m_impl->allocationRequests.TypedData(), m_impl->allocationRequests.Size()},
                                             {m_impl->frozenMaterialAllocations.TypedData(), m_impl->frozenMaterialAllocations.Size()}, &lifetimeFailure))
        {
            m_impl->frozenMaterialAllocations.Clear();
            rollback();
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::LifetimeFailure, "GPU Scene material compound allocation failed", {}, lifetimeFailure);
        }

        u32 allocationIndex = 0;
        u32 newDefinitionIndex = 0;
        for (u32 definitionIndex = 0; definitionIndex < m_impl->frozenMaterials.Size(); ++definitionIndex)
        {
            const Impl::FrozenMaterialDefinition& frozen = m_impl->frozenMaterials[definitionIndex];
            const u32* const recordIndex = m_impl->materialByKey.FindPtr(frozen.key);
            if (recordIndex == nullptr || newDefinitionIndex >= m_impl->frozenMaterialRecords.Size() || *recordIndex != m_impl->frozenMaterialRecords[newDefinitionIndex])
                continue;
            Impl::MaterialRecord& record = m_impl->materials[*recordIndex];
            record.material = m_impl->frozenMaterialAllocations[allocationIndex++];
            if (record.resourceCount != 0)
                record.resources = m_impl->frozenMaterialAllocations[allocationIndex++];
            if (record.parameterWordCount != 0)
                record.parameters = m_impl->frozenMaterialAllocations[allocationIndex++];
            m_impl->frozenMaterialPayloads.PushBack({record.material, PayloadKind::Material, definitionIndex});
            if (record.resourceCount != 0)
                m_impl->frozenMaterialPayloads.PushBack({record.resources, PayloadKind::MaterialResources, definitionIndex});
            if (record.parameterWordCount != 0)
                m_impl->frozenMaterialPayloads.PushBack({record.parameters, PayloadKind::MaterialParameters, definitionIndex});
            ++newDefinitionIndex;
        }
        m_impl->frozenMaterialUploadRequests.Resize(m_impl->frozenMaterialPayloads.Size());
        for (u32 index = 0; index < m_impl->frozenMaterialPayloads.Size(); ++index)
        {
            const GpuSceneAllocation allocation = m_impl->frozenMaterialPayloads[index].allocation;
            m_impl->frozenMaterialUploadRequests[index] = {allocation, 0, allocation.count};
        }
        m_impl->materialBatchState = Impl::MaterialBatchState::Prepared;
        m_impl->ResetTransaction();
        return true;
    }

    void GpuSceneDefinitions::AbandonDevice() noexcept
    {
        if (m_impl == nullptr)
            return;
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
    }

    bool GpuSceneDefinitions::StageMaterials(GpuSceneRuntime& runtime, const GpuMaterialDefinitionBatch& batch, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene material staging must run on the main thread");
        if (!batch.IsValid() || batch.serial != m_impl->materialBatchSerial || batch.definitionCount != m_impl->frozenMaterials.Size())
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidBatch, "GPU Scene material batch is stale or invalid");
        if (m_impl->materialBatchState == Impl::MaterialBatchState::Accepted)
            return true;
        if (m_impl->materialBatchState != Impl::MaterialBatchState::Prepared || !runtime.IsInitialized() || &runtime.GetDefinitions() != this)
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidBatch, "GPU Scene material batch is not ready for this runtime");

        GpuSceneContributionDesc contribution;
        contribution.owner = this;
        contribution.token = Impl::EncodeMaterialBatch(batch);
        contribution.requests = {m_impl->frozenMaterialUploadRequests.TypedData(), m_impl->frozenMaterialUploadRequests.Size()};
        contribution.write = &GpuSceneDefinitions::WriteMaterialContribution;
        contribution.accept = &GpuSceneDefinitions::AcceptMaterialContribution;
        contribution.retry = &GpuSceneDefinitions::RetryMaterialContribution;
        GpuSceneRuntimeFailure runtimeFailure;
        if (!runtime.StageContribution(contribution, &runtimeFailure))
            return Fail(failure, GpuSceneDefinitionFailureCode::ContributionFailure, runtimeFailure.message != nullptr ? runtimeFailure.message : "GPU Scene material contribution staging failed");
        m_impl->materialBatchState = Impl::MaterialBatchState::Staged;
        return true;
    }

    bool GpuSceneDefinitions::CancelMaterials(const GpuMaterialDefinitionBatch& batch, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene material cancellation must run on the main thread");
        if (!batch.IsValid() || batch.serial != m_impl->materialBatchSerial || batch.definitionCount != m_impl->frozenMaterials.Size() || m_impl->materialBatchState != Impl::MaterialBatchState::Prepared)
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidBatch, "only a current unstaged material batch can cancel");
        if (!m_impl->frozenMaterialAllocations.Empty())
        {
            GpuSceneLifetimeFailure lifetimeFailure;
            if (!m_impl->lifetime->CancelBatch({m_impl->frozenMaterialAllocations.TypedData(), m_impl->frozenMaterialAllocations.Size()}, &lifetimeFailure))
                return Fail(failure, GpuSceneDefinitionFailureCode::LifetimeFailure, "GPU Scene material compound cancellation failed", {}, lifetimeFailure);
        }
        for (const u32 index : m_impl->frozenMaterialReuses)
            --m_impl->materials[index].references;
        for (const u32 index : m_impl->frozenMaterialRecords)
        {
            const GpuSceneDefinitionKey key = m_impl->materials[index].key;
            static_cast<void>(m_impl->materialByKey.Remove(key));
            m_impl->RecycleMaterial(index);
        }
        m_impl->ResetMaterialBatch();
        return true;
    }

    bool GpuSceneDefinitions::ConsumeMaterials(const GpuMaterialDefinitionBatch& batch, containers::ArraySpan<GpuMaterialHandle> handles, GpuSceneDefinitionPublication& publication,
                                               GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        publication = {};
        for (GpuMaterialHandle& handle : handles)
            handle = {};
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene material results must be consumed on the main thread");
        if (!batch.IsValid() || batch.serial != m_impl->materialBatchSerial || batch.definitionCount != m_impl->frozenMaterials.Size() || m_impl->materialBatchState != Impl::MaterialBatchState::Accepted ||
            handles.Size() != m_impl->frozenMaterialHandles.Size())
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidBatch, "GPU Scene material batch has no accepted result");
        for (u32 index = 0; index < handles.Size(); ++index)
            handles[index] = m_impl->frozenMaterialHandles[index];
        publication = m_impl->frozenMaterialPublication;
        m_impl->ResetMaterialBatch();
        return true;
    }

    GpuMaterialDefinitionBatchState GpuSceneDefinitions::GetMaterialBatchState(const GpuMaterialDefinitionBatch& batch) const noexcept
    {
        if (m_impl == nullptr || !batch.IsValid() || batch.serial != m_impl->materialBatchSerial || batch.definitionCount != m_impl->frozenMaterials.Size())
            return GpuMaterialDefinitionBatchState::Invalid;
        if (m_impl->materialBatchState == Impl::MaterialBatchState::Prepared)
            return GpuMaterialDefinitionBatchState::Prepared;
        if (m_impl->materialBatchState == Impl::MaterialBatchState::Staged || m_impl->materialBatchState == Impl::MaterialBatchState::Written)
            return GpuMaterialDefinitionBatchState::Staged;
        return m_impl->materialBatchState == Impl::MaterialBatchState::Accepted ? GpuMaterialDefinitionBatchState::Accepted : GpuMaterialDefinitionBatchState::Invalid;
    }

    bool GpuSceneDefinitions::WriteMaterialContribution(void* const owner, const GpuSceneContributionToken token, const containers::ArraySpan<const GpuSceneUploadReservation> reservations,
                                                        const char*& failureMessage) noexcept
    {
        auto* const definitions = static_cast<GpuSceneDefinitions*>(owner);
        const GpuMaterialDefinitionBatch batch = Impl::DecodeMaterialBatch(token);
        if (definitions == nullptr || definitions->m_impl == nullptr || !batch.IsValid() || batch.serial != definitions->m_impl->materialBatchSerial ||
            batch.definitionCount != definitions->m_impl->frozenMaterials.Size() || definitions->m_impl->materialBatchState != Impl::MaterialBatchState::Staged ||
            reservations.Size() != definitions->m_impl->frozenMaterialPayloads.Size())
        {
            failureMessage = "GPU Scene material contribution is stale or has mismatched reservations";
            return false;
        }
        Impl& impl = *definitions->m_impl;
        for (u32 payloadIndex = 0; payloadIndex < impl.frozenMaterialPayloads.Size(); ++payloadIndex)
        {
            const Payload payload = impl.frozenMaterialPayloads[payloadIndex];
            const GpuSceneUploadReservation& reservation = reservations[payloadIndex];
            const Impl::FrozenMaterialDefinition& frozen = impl.frozenMaterials[payload.definition];
            const u32* const recordIndex = impl.materialByKey.FindPtr(frozen.key);
            if (recordIndex == nullptr || !reservation.IsValid() ||
                reservation.size != static_cast<u64>(payload.allocation.count) * (payload.kind == PayloadKind::Material            ? sizeof(GpuMaterial)
                                                                                  : payload.kind == PayloadKind::MaterialResources ? sizeof(GpuMaterialResource)
                                                                                                                                   : sizeof(GpuMaterialParameterWord)))
            {
                failureMessage = "GPU Scene material contribution contains an invalid payload reservation";
                return false;
            }
            const Impl::MaterialRecord& record = impl.materials[*recordIndex];
            if (payload.kind == PayloadKind::Material)
            {
                GpuMaterial value = frozen.material;
                value.parameterByteOffset = record.parameterWordCount != 0 ? record.parameters.first * sizeof(GpuMaterialParameterWord) : 0;
                value.parameterByteSize = record.parameterByteSize;
                value.firstResource = record.resourceCount != 0 ? record.resources.first : 0;
                value.resourceCount = record.resourceCount;
                value.generation = record.material.generation;
                *static_cast<GpuMaterial*>(reservation.destination) = value;
            }
            else if (payload.kind == PayloadKind::MaterialResources)
            {
                CopyElements(static_cast<GpuMaterialResource*>(reservation.destination), {impl.frozenMaterialResources.TypedData() + frozen.firstResource, frozen.resourceCount});
            }
            else
            {
                CopyElements(static_cast<GpuMaterialParameterWord*>(reservation.destination), {impl.frozenMaterialParameters.TypedData() + frozen.firstParameterWord, frozen.parameterWordCount});
            }
        }
        impl.materialBatchState = Impl::MaterialBatchState::Written;
        failureMessage = nullptr;
        return true;
    }

    void GpuSceneDefinitions::AcceptMaterialContribution(void* const owner, const GpuSceneContributionToken token, const rhi::GpuFence sharedCompletion) noexcept
    {
        auto* const definitions = static_cast<GpuSceneDefinitions*>(owner);
        const GpuMaterialDefinitionBatch batch = Impl::DecodeMaterialBatch(token);
        const bool valid = definitions != nullptr && definitions->m_impl != nullptr && batch.IsValid() && sharedCompletion.IsValid() && batch.serial == definitions->m_impl->materialBatchSerial &&
                           batch.definitionCount == definitions->m_impl->frozenMaterials.Size() && definitions->m_impl->materialBatchState == Impl::MaterialBatchState::Written;
        if (!valid)
            return;
        Impl& impl = *definitions->m_impl;
        for (const u32 recordIndex : impl.frozenMaterialRecords)
        {
            Impl::MaterialRecord& record = impl.materials[recordIndex];
            record.state = DefinitionState::Active;
            static_cast<void>(impl.materialByHandle.Insert(HandleIdentity(record.material.first, record.material.generation), recordIndex));
        }
        impl.frozenMaterialHandles.Resize(impl.frozenMaterials.Size());
        for (u32 index = 0; index < impl.frozenMaterials.Size(); ++index)
        {
            const u32* const recordIndex = impl.materialByKey.FindPtr(impl.frozenMaterials[index].key);
            impl.frozenMaterialHandles[index] = impl.materials[*recordIndex].material.AsSlotHandle<GpuMaterialHandle>();
        }
        impl.frozenMaterialPublication.completion = sharedCompletion;
        impl.frozenMaterialPublication.uploadedRanges = impl.frozenMaterialPayloads.Size();
        for (const Payload& payload : impl.frozenMaterialPayloads)
        {
            const u32 stride = payload.kind == PayloadKind::Material ? sizeof(GpuMaterial) : payload.kind == PayloadKind::MaterialResources ? sizeof(GpuMaterialResource) : sizeof(GpuMaterialParameterWord);
            impl.frozenMaterialPublication.uploadedBytes += static_cast<u64>(payload.allocation.count) * stride;
        }
        impl.stats.materials += impl.frozenMaterialRecords.Size();
        impl.stats.references += impl.frozenMaterials.Size();
        impl.stats.acquisitions += impl.frozenMaterials.Size();
        impl.stats.reuses += impl.frozenMaterialReuses.Size();
        impl.materialBatchState = Impl::MaterialBatchState::Accepted;
    }

    bool GpuSceneDefinitions::RetryMaterialContribution(void* const owner, const GpuSceneContributionToken token, const char*& failureMessage) noexcept
    {
        auto* const definitions = static_cast<GpuSceneDefinitions*>(owner);
        const GpuMaterialDefinitionBatch batch = Impl::DecodeMaterialBatch(token);
        if (definitions == nullptr || definitions->m_impl == nullptr || !batch.IsValid() || batch.serial != definitions->m_impl->materialBatchSerial ||
            batch.definitionCount != definitions->m_impl->frozenMaterials.Size() ||
            (definitions->m_impl->materialBatchState != Impl::MaterialBatchState::Staged && definitions->m_impl->materialBatchState != Impl::MaterialBatchState::Written))
        {
            failureMessage = "GPU Scene material contribution cannot return to its prepared state";
            return false;
        }
        definitions->m_impl->materialBatchState = Impl::MaterialBatchState::Prepared;
        failureMessage = nullptr;
        return true;
    }

    bool GpuSceneDefinitions::AcquireRenderables(const containers::ArraySpan<const GpuRenderableDefinition> definitions, containers::ArraySpan<GpuRenderableHandle> handles,
                                                 GpuSceneDefinitionPublication& publication, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        publication = {};
        for (GpuRenderableHandle& handle : handles)
            handle = {};
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene renderable acquisition must run on the main thread");
        if (definitions.Empty() || definitions.Size() != handles.Size() || definitions.Size() > m_impl->config.maximumDefinitionsPerBatch)
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidDefinition, "GPU Scene renderable definition batch is invalid");

        m_impl->ResetTransaction();
        auto rollback = [&]() noexcept
        {
            if (!m_impl->allocations.Empty())
                static_cast<void>(m_impl->lifetime->CancelBatch({m_impl->allocations.TypedData(), m_impl->allocations.Size()}));
            for (const u32 index : m_impl->transactionReuses)
                if (m_impl->renderables[index].references != 0)
                    --m_impl->renderables[index].references;
            for (const u32 index : m_impl->transactionRecords)
            {
                Impl::RenderableRecord& record = m_impl->renderables[index];
                for (const GpuMaterialHandle dependency : record.materials)
                {
                    Impl::MaterialRecord* const material = m_impl->Find(dependency);
                    if (material != nullptr && material->references != 0)
                        --material->references;
                }
                static_cast<void>(m_impl->renderableByKey.Remove(record.key));
                m_impl->RecycleRenderable(index);
            }
            for (GpuRenderableHandle& handle : handles)
                handle = {};
            m_impl->ResetTransaction();
        };

        for (u32 definitionIndex = 0; definitionIndex < definitions.Size(); ++definitionIndex)
        {
            const GpuRenderableDefinition& definition = definitions[definitionIndex];
            bool valid = definition.key.IsValid() && !definition.lods.Empty() && !definition.primitives.Empty() && !definition.phaseParticipations.Empty();
            for (u32 lodIndex = 0; valid && lodIndex < definition.lods.Size(); ++lodIndex)
                valid = ValidRange(definition.lods[lodIndex].firstPrimitive, definition.lods[lodIndex].primitiveCount, definition.primitives.Size());
            for (u32 primitiveIndex = 0; valid && primitiveIndex < definition.primitives.Size(); ++primitiveIndex)
            {
                const GpuPrimitiveDefinition& primitive = definition.primitives[primitiveIndex];
                valid = ValidRange(primitive.firstPhaseParticipation, primitive.phaseParticipationCount, definition.phaseParticipations.Size()) && primitive.phaseParticipationCount != 0 &&
                        m_impl->Find(primitive.material) != nullptr;
            }
            for (u32 phaseIndex = 0; valid && phaseIndex < definition.phaseParticipations.Size(); ++phaseIndex)
                valid = definition.phaseParticipations[phaseIndex].phase < MaximumRenderPhases;
            if (!valid)
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::InvalidDependency, "GPU Scene renderable definition or dependency is invalid", definition.key);
            }

            if (const u32* const existingIndex = m_impl->renderableByKey.FindPtr(definition.key))
            {
                Impl::RenderableRecord& existing = m_impl->renderables[*existingIndex];
                if (existing.state == DefinitionState::Free || existing.lodCount != definition.lods.Size() || existing.primitiveCount != definition.primitives.Size() ||
                    existing.phaseParticipationCount != definition.phaseParticipations.Size())
                {
                    rollback();
                    ++m_impl->stats.rejectedOperations;
                    return Fail(failure, GpuSceneDefinitionFailureCode::IncompatibleDefinition, "equal renderable keys describe incompatible definitions", definition.key);
                }
                ++existing.references;
                m_impl->transactionReuses.PushBack(*existingIndex);
                continue;
            }

            const u32 recordIndex = m_impl->NewRenderableRecord();
            if (recordIndex == InvalidGpuSceneIndex)
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::CapacityExceeded, "GPU Scene renderable definition capacity is exhausted", definition.key);
            }
            Impl::RenderableRecord& record = m_impl->renderables[recordIndex];
            record.key = definition.key;
            record.references = 1;
            record.lodCount = definition.lods.Size();
            record.primitiveCount = definition.primitives.Size();
            record.phaseParticipationCount = definition.phaseParticipations.Size();
            record.state = DefinitionState::Publishing;
            record.materials.Reserve(record.primitiveCount);
            for (const GpuPrimitiveDefinition& primitive : definition.primitives)
            {
                Impl::MaterialRecord* const material = m_impl->Find(primitive.material);
                ++material->references;
                record.materials.PushBack(primitive.material);
            }
            static_cast<void>(m_impl->renderableByKey.Insert(record.key, recordIndex));
            m_impl->transactionRecords.PushBack(recordIndex);
            m_impl->allocationRequests.PushBack({GpuSceneTableKind::Renderable, 1});
            m_impl->allocationRequests.PushBack({GpuSceneTableKind::Lod, record.lodCount});
            m_impl->allocationRequests.PushBack({GpuSceneTableKind::Primitive, record.primitiveCount});
            m_impl->allocationRequests.PushBack({GpuSceneTableKind::PhaseParticipation, record.phaseParticipationCount});
        }

        if (m_impl->allocationRequests.Size() > m_impl->config.maximumAllocationsPerBatch)
        {
            rollback();
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::CapacityExceeded, "GPU Scene renderable batch exceeds allocation capacity");
        }
        if (!m_impl->allocationRequests.Empty())
        {
            m_impl->allocations.Resize(m_impl->allocationRequests.Size());
            GpuSceneLifetimeFailure lifetimeFailure;
            if (!m_impl->lifetime->AllocateBatch({m_impl->allocationRequests.TypedData(), m_impl->allocationRequests.Size()}, {m_impl->allocations.TypedData(), m_impl->allocations.Size()}, &lifetimeFailure))
            {
                m_impl->allocations.Clear();
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::LifetimeFailure, "GPU Scene renderable allocation failed", {}, lifetimeFailure);
            }

            u32 allocationIndex = 0;
            u32 newDefinitionIndex = 0;
            for (u32 definitionIndex = 0; definitionIndex < definitions.Size(); ++definitionIndex)
            {
                const u32* const recordIndex = m_impl->renderableByKey.FindPtr(definitions[definitionIndex].key);
                if (recordIndex == nullptr || newDefinitionIndex >= m_impl->transactionRecords.Size() || *recordIndex != m_impl->transactionRecords[newDefinitionIndex])
                    continue;
                Impl::RenderableRecord& record = m_impl->renderables[*recordIndex];
                record.renderable = m_impl->allocations[allocationIndex++];
                record.lods = m_impl->allocations[allocationIndex++];
                record.primitives = m_impl->allocations[allocationIndex++];
                record.phaseParticipations = m_impl->allocations[allocationIndex++];
                m_impl->payloads.PushBack({record.renderable, PayloadKind::Renderable, definitionIndex});
                m_impl->payloads.PushBack({record.lods, PayloadKind::Lods, definitionIndex});
                m_impl->payloads.PushBack({record.primitives, PayloadKind::Primitives, definitionIndex});
                m_impl->payloads.PushBack({record.phaseParticipations, PayloadKind::PhaseParticipations, definitionIndex});
                ++newDefinitionIndex;
            }

            if (!m_impl->BeginPublication(failure))
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return false;
            }
            for (u32 payloadIndex = 0; payloadIndex < m_impl->payloads.Size(); ++payloadIndex)
            {
                const Payload payload = m_impl->payloads[payloadIndex];
                const GpuRenderableDefinition& definition = definitions[payload.definition];
                const u32* const recordIndex = m_impl->renderableByKey.FindPtr(definition.key);
                Impl::RenderableRecord& record = m_impl->renderables[*recordIndex];
                switch (payload.kind)
                {
                case PayloadKind::Renderable:
                {
                    GpuRenderable value;
                    value.firstLod = record.lods.first;
                    value.lodCount = record.lods.count;
                    value.generation = record.renderable.generation;
                    value.flags = definition.flags;
                    for (const GpuPrimitiveDefinition& primitive : definition.primitives)
                    {
                        for (u32 phaseOffset = 0; phaseOffset < primitive.phaseParticipationCount; ++phaseOffset)
                        {
                            const GpuPhaseParticipation participation = definition.phaseParticipations[primitive.firstPhaseParticipation + phaseOffset];
                            if (participation.phase < 32u)
                                value.phaseMaskLow |= 1u << participation.phase;
                            else
                                value.phaseMaskHigh |= 1u << (participation.phase - 32u);
                        }
                    }
                    *static_cast<GpuRenderable*>(m_impl->reservations[payloadIndex].destination) = value;
                    break;
                }
                case PayloadKind::Lods:
                {
                    auto* const destination = static_cast<GpuLod*>(m_impl->reservations[payloadIndex].destination);
                    for (u32 index = 0; index < definition.lods.Size(); ++index)
                    {
                        destination[index] = definition.lods[index];
                        destination[index].firstPrimitive += record.primitives.first;
                    }
                    break;
                }
                case PayloadKind::Primitives:
                {
                    auto* const destination = static_cast<GpuPrimitive*>(m_impl->reservations[payloadIndex].destination);
                    for (u32 index = 0; index < definition.primitives.Size(); ++index)
                    {
                        const GpuPrimitiveDefinition& source = definition.primitives[index];
                        destination[index] = {
                            source.material.index, record.phaseParticipations.first + source.firstPhaseParticipation, source.phaseParticipationCount, source.sourceSubmesh, source.flags, 0, 0, 0};
                    }
                    break;
                }
                case PayloadKind::PhaseParticipations:
                    CopyElements(static_cast<GpuPhaseParticipation*>(m_impl->reservations[payloadIndex].destination), definition.phaseParticipations);
                    break;
                default:
                    break;
                }
                GpuSceneUploadFailure uploadFailure;
                if (!m_impl->uploader->Complete(m_impl->reservations[payloadIndex], &uploadFailure))
                {
                    static_cast<void>(m_impl->uploader->Cancel());
                    rollback();
                    ++m_impl->stats.rejectedOperations;
                    return Fail(failure, GpuSceneDefinitionFailureCode::UploadFailure, "GPU Scene renderable upload completion failed", definition.key, {}, uploadFailure);
                }
            }
            GpuSceneUploadResult uploadResult;
            GpuSceneUploadFailure uploadFailure;
            if (!m_impl->uploader->Submit(uploadResult, &uploadFailure))
            {
                rollback();
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneDefinitionFailureCode::UploadFailure, "GPU Scene renderable upload submission failed", {}, {}, uploadFailure);
            }
            publication.completion = uploadResult.completion;
            publication.uploadedRanges = uploadResult.uniqueUpdates;
            publication.uploadedBytes = uploadResult.uploadedBytes;
        }

        u64 dependencyReferences = 0;
        for (const u32 recordIndex : m_impl->transactionRecords)
        {
            Impl::RenderableRecord& record = m_impl->renderables[recordIndex];
            record.state = DefinitionState::Active;
            dependencyReferences += record.primitiveCount;
            static_cast<void>(m_impl->renderableByHandle.Insert(HandleIdentity(record.renderable.first, record.renderable.generation), recordIndex));
        }
        for (u32 definitionIndex = 0; definitionIndex < definitions.Size(); ++definitionIndex)
        {
            const u32* const recordIndex = m_impl->renderableByKey.FindPtr(definitions[definitionIndex].key);
            handles[definitionIndex] = m_impl->renderables[*recordIndex].renderable.AsSlotHandle<GpuRenderableHandle>();
        }
        publication.requestedDefinitions = definitions.Size();
        publication.createdDefinitions = m_impl->transactionRecords.Size();
        publication.reusedDefinitions = definitions.Size() - publication.createdDefinitions;
        m_impl->stats.renderables += publication.createdDefinitions;
        m_impl->stats.references += definitions.Size() + dependencyReferences;
        m_impl->stats.acquisitions += definitions.Size();
        m_impl->stats.reuses += publication.reusedDefinitions;
        m_impl->ResetTransaction();
        return true;
    }

    bool GpuSceneDefinitions::AddReference(const GpuGeometryHandle handle, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene definition references must change on the main thread");
        Impl::GeometryRecord* const record = m_impl->Find(handle);
        if (record == nullptr)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidDependency, "GPU Scene geometry handle is stale");
        }
        ++record->references;
        ++m_impl->stats.references;
        ++m_impl->stats.acquisitions;
        return true;
    }

    bool GpuSceneDefinitions::AddReference(const GpuMaterialHandle handle, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene definition references must change on the main thread");
        Impl::MaterialRecord* const record = m_impl->Find(handle);
        if (record == nullptr)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidDependency, "GPU Scene material handle is stale");
        }
        ++record->references;
        ++m_impl->stats.references;
        ++m_impl->stats.acquisitions;
        return true;
    }

    bool GpuSceneDefinitions::AddReference(const GpuRenderableHandle handle, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene definition references must change on the main thread");
        Impl::RenderableRecord* const record = m_impl->Find(handle);
        if (record == nullptr)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::InvalidDependency, "GPU Scene renderable handle is stale");
        }
        ++record->references;
        ++m_impl->stats.references;
        ++m_impl->stats.acquisitions;
        return true;
    }

    bool GpuSceneDefinitions::Release(const GpuGeometryHandle handle, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene definition release must run on the main thread");
        const u64 identity = HandleIdentity(handle);
        const u32* const recordIndex = m_impl->geometryByHandle.FindPtr(identity);
        if (recordIndex == nullptr || *recordIndex >= m_impl->geometries.Size())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::ReferenceUnderflow, "GPU Scene geometry reference is stale or already released");
        }
        Impl::GeometryRecord& record = m_impl->geometries[*recordIndex];
        if (record.references > 1)
        {
            --record.references;
            --m_impl->stats.references;
            ++m_impl->stats.releases;
            return true;
        }
        m_impl->retirementAllocations.Clear();
        m_impl->retirementAllocations.PushBack(record.geometry);
        m_impl->retirementAllocations.PushBack(record.vertexStreams);
        if (record.positionDecode.IsValid())
            m_impl->retirementAllocations.PushBack(record.positionDecode);
        GpuSceneLifetimeFailure lifetimeFailure;
        if (!m_impl->lifetime->RetireBatch({m_impl->retirementAllocations.TypedData(), m_impl->retirementAllocations.Size()}, &lifetimeFailure))
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::LifetimeFailure, "GPU Scene geometry retirement failed", record.key, lifetimeFailure);
        }
        const GpuSceneDefinitionKey key = record.key;
        const u32 releasedIndex = *recordIndex;
        static_cast<void>(m_impl->geometryByHandle.Remove(identity));
        static_cast<void>(m_impl->geometryByKey.Remove(key));
        m_impl->RecycleGeometry(releasedIndex);
        --m_impl->stats.geometries;
        --m_impl->stats.references;
        ++m_impl->stats.releases;
        ++m_impl->stats.retirements;
        return true;
    }

    bool GpuSceneDefinitions::Release(const GpuMaterialHandle handle, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene definition release must run on the main thread");
        const u64 identity = HandleIdentity(handle);
        const u32* const recordIndex = m_impl->materialByHandle.FindPtr(identity);
        if (recordIndex == nullptr || *recordIndex >= m_impl->materials.Size())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::ReferenceUnderflow, "GPU Scene material reference is stale or already released");
        }
        Impl::MaterialRecord& record = m_impl->materials[*recordIndex];
        if (record.references > 1)
        {
            --record.references;
            --m_impl->stats.references;
            ++m_impl->stats.releases;
            return true;
        }
        m_impl->retirementAllocations.Clear();
        m_impl->retirementAllocations.PushBack(record.material);
        if (record.resources.IsValid())
            m_impl->retirementAllocations.PushBack(record.resources);
        if (record.parameters.IsValid())
            m_impl->retirementAllocations.PushBack(record.parameters);
        GpuSceneLifetimeFailure lifetimeFailure;
        if (!m_impl->lifetime->RetireBatch({m_impl->retirementAllocations.TypedData(), m_impl->retirementAllocations.Size()}, &lifetimeFailure))
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::LifetimeFailure, "GPU Scene material retirement failed", record.key, lifetimeFailure);
        }
        const GpuSceneDefinitionKey key = record.key;
        const u32 releasedIndex = *recordIndex;
        static_cast<void>(m_impl->materialByHandle.Remove(identity));
        static_cast<void>(m_impl->materialByKey.Remove(key));
        m_impl->RecycleMaterial(releasedIndex);
        --m_impl->stats.materials;
        --m_impl->stats.references;
        ++m_impl->stats.releases;
        ++m_impl->stats.retirements;
        return true;
    }

    bool GpuSceneDefinitions::Release(const GpuRenderableHandle handle, GpuSceneDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneDefinitionFailureCode::NotInitialized, "GPU Scene definitions are not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneDefinitionFailureCode::WrongThread, "GPU Scene definition release must run on the main thread");
        const u64 identity = HandleIdentity(handle);
        const u32* const recordIndex = m_impl->renderableByHandle.FindPtr(identity);
        if (recordIndex == nullptr || *recordIndex >= m_impl->renderables.Size())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::ReferenceUnderflow, "GPU Scene renderable reference is stale or already released");
        }
        Impl::RenderableRecord& record = m_impl->renderables[*recordIndex];
        if (record.references > 1)
        {
            --record.references;
            --m_impl->stats.references;
            ++m_impl->stats.releases;
            return true;
        }

        m_impl->materialReleaseCounts.Clear();
        for (const GpuMaterialHandle dependency : record.materials)
            ++m_impl->materialReleaseCounts.GetRef(HandleIdentity(dependency), 0u);

        m_impl->retirementAllocations.Clear();
        m_impl->retirementAllocations.PushBack(record.renderable);
        m_impl->retirementAllocations.PushBack(record.lods);
        m_impl->retirementAllocations.PushBack(record.primitives);
        m_impl->retirementAllocations.PushBack(record.phaseParticipations);
        u32 retiredMaterials = 0;
        for (auto iterator = m_impl->materialReleaseCounts.Begin(); iterator != m_impl->materialReleaseCounts.End(); ++iterator)
        {
            const u32* const dependencyIndex = m_impl->materialByHandle.FindPtr(iterator.Key());
            if (dependencyIndex == nullptr || m_impl->materials[*dependencyIndex].references < iterator.Value())
                return Fail(failure, GpuSceneDefinitionFailureCode::ReferenceUnderflow, "GPU Scene renderable material references are inconsistent", record.key);
            const Impl::MaterialRecord& dependency = m_impl->materials[*dependencyIndex];
            if (dependency.references != iterator.Value())
                continue;
            m_impl->retirementAllocations.PushBack(dependency.material);
            if (dependency.resources.IsValid())
                m_impl->retirementAllocations.PushBack(dependency.resources);
            if (dependency.parameters.IsValid())
                m_impl->retirementAllocations.PushBack(dependency.parameters);
            ++retiredMaterials;
        }
        GpuSceneLifetimeFailure lifetimeFailure;
        if (!m_impl->lifetime->RetireBatch({m_impl->retirementAllocations.TypedData(), m_impl->retirementAllocations.Size()}, &lifetimeFailure))
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneDefinitionFailureCode::LifetimeFailure, "GPU Scene renderable retirement failed", record.key, lifetimeFailure);
        }

        const u32 primitiveCount = record.primitiveCount;
        const GpuSceneDefinitionKey renderableKey = record.key;
        const u32 releasedRenderableIndex = *recordIndex;
        for (auto iterator = m_impl->materialReleaseCounts.Begin(); iterator != m_impl->materialReleaseCounts.End(); ++iterator)
        {
            const u32 dependencyIndex = *m_impl->materialByHandle.FindPtr(iterator.Key());
            Impl::MaterialRecord& dependency = m_impl->materials[dependencyIndex];
            dependency.references -= iterator.Value();
            if (dependency.references != 0)
                continue;
            const GpuSceneDefinitionKey key = dependency.key;
            static_cast<void>(m_impl->materialByHandle.Remove(iterator.Key()));
            static_cast<void>(m_impl->materialByKey.Remove(key));
            m_impl->RecycleMaterial(dependencyIndex);
        }
        static_cast<void>(m_impl->renderableByHandle.Remove(identity));
        static_cast<void>(m_impl->renderableByKey.Remove(renderableKey));
        m_impl->RecycleRenderable(releasedRenderableIndex);
        --m_impl->stats.renderables;
        m_impl->stats.materials -= retiredMaterials;
        m_impl->stats.references -= 1u + primitiveCount;
        ++m_impl->stats.releases;
        m_impl->stats.retirements += 1u + retiredMaterials;
        return true;
    }

    bool GpuSceneDefinitions::GetRenderableAllocations(const GpuRenderableHandle handle, GpuRenderableAllocationView& output) const noexcept
    {
        if (!concurrency::IsMainThread() || m_impl == nullptr)
            return false;
        const auto* record = m_impl->Find(handle);
        if (record == nullptr)
            return false;
        output = {record->renderable, record->primitives, record->phaseParticipations};
        return true;
    }

    bool GpuSceneDefinitions::IsValid(const GpuGeometryHandle handle) const noexcept
    {
        return m_impl != nullptr && m_impl->Find(handle) != nullptr;
    }

    bool GpuSceneDefinitions::IsValid(const GpuMaterialHandle handle) const noexcept
    {
        return m_impl != nullptr && m_impl->Find(handle) != nullptr;
    }

    bool GpuSceneDefinitions::IsValid(const GpuRenderableHandle handle) const noexcept
    {
        return m_impl != nullptr && m_impl->Find(handle) != nullptr;
    }

    GpuSceneDefinitionsStats GpuSceneDefinitions::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : GpuSceneDefinitionsStats{};
    }
} // namespace vanguard::rendering

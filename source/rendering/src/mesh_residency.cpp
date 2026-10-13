#include <vanguard/rendering/mesh_residency.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/rendering/render_phase.hpp>
#include <vanguard/rendering/mesh_draw_layout.hpp>
#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/system/assert.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        template <typename T, typename... Args> [[nodiscard]] T* AllocateObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteObject(T* const object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void ClearFailure(MeshResidencyFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MeshResidencyFailure* const failure, const MeshResidencyFailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] const resources::ResourceHandle* FindDependency(const meshes::MeshResourceObject& mesh, const resources::ResourceReference reference) noexcept
        {
            for (const resources::ResourceHandle& dependency : mesh.GetDependencies())
                if (dependency.IsValid() && dependency.GetPath() == reference.GetPath() && dependency.GetType() == reference.ExpectedType())
                    return &dependency;
            return nullptr;
        }

        [[nodiscard]] const resources::ResourceHandle* FindDependency(const materials::MaterialResourceObject& material, const resources::ResourceReference reference) noexcept
        {
            for (const materials::LoadedMaterialDependency& dependency : material.GetLoadedDependencies())
                if (dependency.resource == reference && dependency.handle.IsValid())
                    return &dependency.handle;
            return nullptr;
        }

        [[nodiscard]] const MeshDrawPhaseContext* FindDrawPhaseContext(const containers::ArraySpan<const MeshDrawPhaseContext> contexts,
                                                                       const RenderPhaseKey phase) noexcept
        {
            for (const MeshDrawPhaseContext& context : contexts)
                if (context.phase == phase)
                    return &context;
            return nullptr;
        }

        [[nodiscard]] bool ValidCameraSurfaceState(const RenderPhaseKey phase, const pipelines::GraphicsState& state,
                                                   const pipelines::AttachmentSignature& attachments) noexcept
        {
            const bool transparent = phase == standardRenderPhases::Transparent;
            if (phase != standardRenderPhases::DepthPrepass && phase != standardRenderPhases::Opaque && !transparent)
                return true;
            if (state.blend.attachmentCount != attachments.colorCount || state.blend.logicOperationEnable ||
                state.multisample.sampleCount != attachments.sampleCount || state.multisample.alphaToCoverage || state.multisample.sampleMask != 0xffffffffu ||
                !state.depthStencil.depthTest || (transparent ? state.depthStencil.depthWrite : !state.depthStencil.depthWrite) ||
                state.depthStencil.stencilTest || state.depthStencil.depthBoundsTest ||
                (state.depthStencil.depthCompare != pipelines::CompareOperation::LessEqual && state.depthStencil.depthCompare != pipelines::CompareOperation::GreaterEqual))
                return false;
            if (transparent)
            {
                if (attachments.colorCount != 1u)
                    return false;
                const auto& attachment = state.blend.attachments[0];
                if (!attachment.blendEnable || attachment.sourceColor != pipelines::BlendFactor::SourceAlpha ||
                    attachment.destinationColor != pipelines::BlendFactor::OneMinusSourceAlpha ||
                    attachment.colorOperation != pipelines::BlendOperation::Add ||
                    attachment.sourceAlpha != pipelines::BlendFactor::One ||
                    attachment.destinationAlpha != pipelines::BlendFactor::OneMinusSourceAlpha ||
                    attachment.alphaOperation != pipelines::BlendOperation::Add || attachment.writeMask != 0x0f)
                    return false;
            }
            else
            {
                for (u32 index = 0; index < attachments.colorCount; ++index)
                {
                    const auto& attachment = state.blend.attachments[state.blend.independentBlend ? index : 0u];
                    if (attachment.blendEnable || attachment.writeMask != 0x0f)
                        return false;
                }
            }
            // Keep raster coverage identical between the optional prepass and
            // GBuffer. Special depth-bias/conservative paths use other phases.
            return state.rasterizer.fill == pipelines::FillMode::Solid && !state.rasterizer.rasterizerDiscard &&
                   !state.rasterizer.conservativeRasterization && state.rasterizer.depthBias == 0 &&
                   state.rasterizer.depthBiasClamp == 0.0f && state.rasterizer.slopeScaledDepthBias == 0.0f;
        }

        [[nodiscard]] bool ValidDrawPhaseContexts(const RenderPhaseRegistry& phases,
                                                  const containers::ArraySpan<const MeshDrawPhaseContext> contexts) noexcept
        {
            if (contexts.Empty() || contexts.Data() == nullptr || contexts.Size() > MaximumRenderPhases)
                return false;
            for (u32 index = 0; index < contexts.Size(); ++index)
            {
                if (!contexts[index].phase.IsValid() || !phases.Find(contexts[index].phase).IsValid() ||
                    contexts[index].attachments.colorCount > pipelines::MaximumColorAttachments)
                    return false;
                for (u32 previous = 0; previous < index; ++previous)
                    if (contexts[previous].phase == contexts[index].phase)
                        return false;
            }
            return true;
        }

        [[nodiscard]] GpuSceneDefinitionKey BuildRenderableKey(const meshes::MeshFile& mesh, const MeshResidencyHandle residency, const u32 resourceGeneration,
                                                               const containers::ArraySpan<const GpuMaterialHandle> materials, const containers::ArraySpan<const GpuLod> lods,
                                                               const containers::ArraySpan<const GpuPrimitiveDefinition> primitives, const containers::ArraySpan<const GpuPhaseParticipation> phases) noexcept
        {
            constexpr u32 version = 2;
            crypto::Sha256Builder hash;
            static_cast<void>(hash.Update("vanguard.mesh.renderable", sizeof("vanguard.mesh.renderable") - 1u));
            static_cast<void>(hash.Update(&version, sizeof(version)));
            // Mutable parallel placement has one residency owner. Equal content
            // in distinct live VMESH/residency generations must not alias it.
            static_cast<void>(hash.Update(&residency, sizeof(residency)));
            static_cast<void>(hash.Update(&resourceGeneration, sizeof(resourceGeneration)));
            static_cast<void>(hash.Update(mesh.GetContentFingerprint().bytes, crypto::Digest256::ByteCount));
            static_cast<void>(hash.Update(materials.Data(), materials.SizeInBytes()));
            static_cast<void>(hash.Update(lods.Data(), lods.SizeInBytes()));
            static_cast<void>(hash.Update(primitives.Data(), primitives.SizeInBytes()));
            static_cast<void>(hash.Update(phases.Data(), phases.SizeInBytes()));
            crypto::Digest256 digest;
            static_cast<void>(hash.Finalize(digest));
            GpuSceneDefinitionKey key;
            std::memcpy(key.words, digest.bytes, sizeof(key.words));
            return key;
        }
    } // namespace

    struct RetainedMeshDrawable
    {
        MeshResidencyManager* owner = nullptr;
        RetainedMeshDrawable* previous = nullptr;
        RetainedMeshDrawable* next = nullptr;
        bool queued = false;
        u32 references = 1;
        MeshDrawableState state = MeshDrawableState::Preparing;
        GeometryBatchResult failure = GeometryBatchResult::Success;
        MeshDemandHandle demand;
        MeshDrawableInfo info;
        GpuRenderableAllocationView allocations;
        containers::DynamicArray<MeshDrawPreparation> preparations{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<GeometryBatchLease> leases{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<GpuPrimitivePlacement> primitives{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<GpuPhasePlacement> phases{memory::pools::Rendering::GetInstance()};
        GpuRenderableResidency residency;
        u32 firstPrimitive = 0;
        u32 firstPhase = 0;
        bool staged = false;
        bool placementAccepted = false;
        bool withdrawalAccepted = false;
        u32 preparedMembers = 0;
        u32 preparationCursor = 0;
    };

    MeshDrawableBinding::~MeshDrawableBinding() { Reset(); }
    MeshDrawableBinding::MeshDrawableBinding(MeshDrawableBinding&& other) noexcept : m_drawable(std::exchange(other.m_drawable, nullptr)) {}
    MeshDrawableBinding& MeshDrawableBinding::operator=(MeshDrawableBinding&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_drawable = std::exchange(other.m_drawable, nullptr);
        }
        return *this;
    }
    bool MeshDrawableBinding::IsValid() const noexcept { return m_drawable != nullptr && m_drawable->owner != nullptr; }
    MeshDrawableState MeshDrawableBinding::GetState() const noexcept { return IsValid() ? m_drawable->state : MeshDrawableState::Invalid; }
    GeometryBatchResult MeshDrawableBinding::GetFailure() const noexcept { return IsValid() ? m_drawable->failure : GeometryBatchResult::InvalidPreparation; }
    const MeshDrawableInfo* MeshDrawableBinding::GetDrawable() const noexcept
    {
        return GetState() == MeshDrawableState::Ready ? &m_drawable->info : nullptr;
    }
    const GeometryBatchPlacement* MeshDrawableBinding::GetPlacement(const u32 index) const noexcept
    {
        return GetDrawable() != nullptr && index < m_drawable->leases.Size() ? m_drawable->leases[index].GetPlacement() : nullptr;
    }
    bool MeshDrawableBinding::Retain(MeshDrawableBinding& output) const noexcept
    {
        if (!concurrency::IsMainThread() || !IsValid() || output.m_drawable != nullptr || m_drawable->references == 0xffffffffu)
            return false;
        ++m_drawable->references;
        output.m_drawable = m_drawable;
        return true;
    }
    void MeshDrawableBinding::Reset() noexcept
    {
        if (m_drawable == nullptr)
            return;
        if (!concurrency::IsMainThread())
            VG_FATAL("drawable mesh bindings must be released on the main thread after recording joins");
        auto* drawable = std::exchange(m_drawable, nullptr);
        if (--drawable->references == 0)
        {
            if (drawable->owner == nullptr)
                DeleteObject(drawable);
            else
                drawable->owner->ScheduleDrawable(drawable);
        }
        // The residency work owns cancellation/withdrawal and eventual deletion.
    }

    struct MeshResidencyManager::Impl
    {
        struct ResidencyWork
        {
            ResidencyWork() noexcept
                : materialDemands(memory::pools::Rendering::GetInstance()), materialHandles(memory::pools::Rendering::GetInstance()), lods(memory::pools::Rendering::GetInstance()),
                  primitives(memory::pools::Rendering::GetInstance()), phases(memory::pools::Rendering::GetInstance()), phasePipelines(memory::pools::Rendering::GetInstance()),
                  retiringAllocations(memory::pools::Rendering::GetInstance())
            {
            }

            ~ResidencyWork()
            {
                VG_ASSERT_MSG(!geometry.HasOwnership(), "mesh residency work must retire or transfer pending geometry before destruction");
                VG_ASSERT_MSG(!definitions.HasOwnership(), "mesh residency work must retire or transfer pending definitions before destruction");
                VG_ASSERT_MSG(!renderable.IsValid(), "mesh residency work must release renderable topology before destruction");
            }

            MeshLodGeometryUploadRequestId uploadRequest;
            PendingMeshLodGeometry geometry;
            PendingMeshLodDefinitions definitions;
            containers::DynamicArray<MaterialDemandHandle> materialDemands;
            containers::DynamicArray<GpuMaterialHandle> materialHandles;
            containers::DynamicArray<GpuLod> lods;
            containers::DynamicArray<GpuPrimitiveDefinition> primitives;
            containers::DynamicArray<GpuPhaseParticipation> phases;
            // Borrowed from the exact mesh's retained material dependency closure.
            containers::DynamicArray<const pipelines::PipelineFile*> phasePipelines;
            GpuRenderableHandle renderable;
            RetainedMeshDrawable* drawable = nullptr;
            u32 placementRevision = 0;
            containers::DynamicArray<GeometryAllocationId> retiringAllocations;
            MeshLodGeometryUploadFailure uploadFailure;
        };

        struct ResidencySlot
        {
            resources::ResourceHandle resource;
            ResidencyWork* work = nullptr;
            u32 generation = 1;
            u32 demandCount = 0;
            u32 previousSamePath = InvalidMeshResidencyIndex;
            u32 nextSamePath = InvalidMeshResidencyIndex;
            u32 activeListIndex = InvalidMeshResidencyIndex;
            u32 retiringListIndex = InvalidMeshResidencyIndex;
            u16 anchorLod = 0xffffu;
            MeshResidencyState state = MeshResidencyState::Invalid;
            bool active = false;
        };

        struct StartCandidate
        {
            u32 index = InvalidMeshResidencyIndex;
            u32 generation = 0;
            u16 anchorLod = 0xffffu;
        };

        struct ProgressWorkItem
        {
            ResidencyWork* work = nullptr;
            u32 index = InvalidMeshResidencyIndex;
            u32 generation = 0;
            u32 demandCount = 0;
            MeshResidencyState state = MeshResidencyState::Invalid;
        };

        struct RetirementCandidate
        {
            ResidencyWork* work = nullptr;
            u32 index = InvalidMeshResidencyIndex;
            u32 generation = 0;
        };

        // Batched capture under the mesh lock must remain ownership-free. In
        // particular, adding ResourceHandle-like RAII state here would reintroduce
        // nested external owner work during high-cardinality traversal.
        static_assert(std::is_trivially_copyable_v<StartCandidate>);
        static_assert(std::is_trivially_copyable_v<ProgressWorkItem>);
        static_assert(std::is_trivially_copyable_v<RetirementCandidate>);

        struct DemandSlot
        {
            MeshResidencyHandle residency;
            u32 generation = 1;
            bool active = false;
        };

        explicit Impl(const MeshResidencyConfig& residencyConfig) noexcept
            : residencies(memory::pools::Rendering::GetInstance()), recycledResidencies(memory::pools::Rendering::GetInstance()), residencyByPath(memory::pools::Rendering::GetInstance()),
              activeResidencies(memory::pools::Rendering::GetInstance()), retiringResidencies(memory::pools::Rendering::GetInstance()),
              startScratch(memory::pools::Rendering::GetInstance()), progressScratch(memory::pools::Rendering::GetInstance()),
              retirementScratch(memory::pools::Rendering::GetInstance()), retirementReadyScratch(memory::pools::Rendering::GetInstance()),
              retirementDeleteScratch(memory::pools::Rendering::GetInstance()), retirementResourceReleaseScratch(memory::pools::Rendering::GetInstance()),
              demands(memory::pools::Rendering::GetInstance()), recycledDemands(memory::pools::Rendering::GetInstance()), drawPhases(memory::pools::Rendering::GetInstance()), config(residencyConfig),
              stagedRequests(memory::pools::Rendering::GetInstance())
        {
            residencies.Reserve(config.maximumResidencyRecords);
            recycledResidencies.Reserve(config.maximumResidencyRecords);
            residencyByPath.Reserve(config.maximumResidencyRecords);
            activeResidencies.Reserve(config.maximumResidencyRecords);
            retiringResidencies.Reserve(config.maximumResidencyRecords);
            startScratch.Reserve(config.maximumResidencyRecords);
            progressScratch.Reserve(config.maximumResidencyRecords);
            retirementScratch.Reserve(config.maximumResidencyRecords);
            retirementReadyScratch.Reserve(config.maximumResidencyRecords);
            retirementDeleteScratch.Reserve(config.maximumResidencyRecords);
            retirementResourceReleaseScratch.Reserve(config.maximumResidencyRecords);
            demands.Reserve(config.maximumDemands);
            recycledDemands.Reserve(config.maximumDemands);
            stagedRequests.Reserve(config.geometryBatcher.maximumShells + config.geometryBatcher.maximumBins + MaximumDrawablePublications * 3u);
        }

        [[nodiscard]] ResidencySlot* Find(const MeshResidencyHandle residency) noexcept
        {
            return residency.IsValid() && residency.index < residencies.Size() && residencies[residency.index].active && residencies[residency.index].generation == residency.generation
                       ? &residencies[residency.index]
                       : nullptr;
        }

        [[nodiscard]] const ResidencySlot* Find(const MeshResidencyHandle residency) const noexcept
        {
            return residency.IsValid() && residency.index < residencies.Size() && residencies[residency.index].active && residencies[residency.index].generation == residency.generation
                       ? &residencies[residency.index]
                       : nullptr;
        }

        [[nodiscard]] DemandSlot* Find(const MeshDemandId demand) noexcept
        {
            return demand.IsValid() && demand.index < demands.Size() && demands[demand.index].active && demands[demand.index].generation == demand.generation ? &demands[demand.index] : nullptr;
        }

        [[nodiscard]] MeshResidencyHandle ResidencyId(const u32 index) const noexcept
        {
            return {index, residencies[index].generation};
        }

        [[nodiscard]] MeshDemandId DemandId(const u32 index) const noexcept
        {
            return {index, demands[index].generation};
        }

        u32 AllocateResidency() noexcept
        {
            if (!recycledResidencies.Empty())
            {
                const u32 index = recycledResidencies.Back();
                recycledResidencies.PopBack();
                return index;
            }
            residencies.PushBack({});
            return residencies.Size() - 1u;
        }

        u32 AllocateDemand() noexcept
        {
            if (!recycledDemands.Empty())
            {
                const u32 index = recycledDemands.Back();
                recycledDemands.PopBack();
                return index;
            }
            demands.PushBack({});
            return demands.Size() - 1u;
        }

        void AddActiveResidency(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            VG_ASSERT_MSG(slot.activeListIndex == InvalidMeshResidencyIndex, "mesh residency record is already active-indexed");
            slot.activeListIndex = activeResidencies.Size();
            activeResidencies.PushBack(index);
        }

        void RemoveActiveResidency(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.activeListIndex == InvalidMeshResidencyIndex)
                return;
            const u32 listIndex = slot.activeListIndex;
            const u32 moved = activeResidencies.Back();
            activeResidencies[listIndex] = moved;
            activeResidencies.PopBack();
            slot.activeListIndex = InvalidMeshResidencyIndex;
            if (listIndex < activeResidencies.Size())
                residencies[moved].activeListIndex = listIndex;
        }

        void AddRetiringResidency(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.retiringListIndex != InvalidMeshResidencyIndex)
                return;
            slot.retiringListIndex = retiringResidencies.Size();
            retiringResidencies.PushBack(index);
        }

        void RemoveRetiringResidency(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.retiringListIndex == InvalidMeshResidencyIndex)
                return;
            const u32 listIndex = slot.retiringListIndex;
            const u32 moved = retiringResidencies.Back();
            retiringResidencies[listIndex] = moved;
            retiringResidencies.PopBack();
            slot.retiringListIndex = InvalidMeshResidencyIndex;
            if (listIndex < retiringResidencies.Size())
                residencies[moved].retiringListIndex = listIndex;
        }

        void RemoveResidency(const u32 index, resources::ResourceHandle& releasedResource) noexcept
        {
            ResidencySlot& slot = residencies[index];
            VG_ASSERT_MSG(slot.active && slot.demandCount == 0, "only an unreferenced mesh residency record may be recycled");
            VG_ASSERT_MSG(slot.work == nullptr, "mesh residency work must finish before its record may be recycled");
            VG_ASSERT_MSG(releasedResource.GetGeneration() == 0, "mesh residency release output must be empty");
            const resources::ResourceId path = slot.resource.GetPath().Id();
            u32 head = InvalidMeshResidencyIndex;
            const bool found = residencyByPath.Find(path, head);
            VG_ASSERT_MSG(found, "active mesh residency record must be reachable from its resource-path lookup");
            if (slot.previousSamePath == InvalidMeshResidencyIndex)
            {
                VG_ASSERT_MSG(head == index, "mesh residency path head backlink is inconsistent");
                if (slot.nextSamePath == InvalidMeshResidencyIndex)
                    static_cast<void>(residencyByPath.Remove(path));
                else
                    static_cast<void>(residencyByPath.Set(path, slot.nextSamePath));
            }
            else
                residencies[slot.previousSamePath].nextSamePath = slot.nextSamePath;
            if (slot.nextSamePath != InvalidMeshResidencyIndex)
                residencies[slot.nextSamePath].previousSamePath = slot.previousSamePath;
            RemoveRetiringResidency(index);
            RemoveActiveResidency(index);
            // Move the strong resource ownership out while the slot is protected, but
            // let the caller release it only after the mesh lock has been dropped.
            releasedResource = static_cast<resources::ResourceHandle&&>(slot.resource);
            slot.generation = NextGeneration(slot.generation);
            slot.previousSamePath = InvalidMeshResidencyIndex;
            slot.nextSamePath = InvalidMeshResidencyIndex;
            slot.anchorLod = 0xffffu;
            slot.state = MeshResidencyState::Invalid;
            slot.active = false;
            recycledResidencies.PushBack(index);
            --residencyCount;
        }

        mutable concurrency::SpinLock lock;
        containers::DynamicArray<ResidencySlot> residencies;
        containers::DynamicArray<u32> recycledResidencies;
        containers::HashMap<resources::ResourceId, u32> residencyByPath;
        containers::DynamicArray<u32> activeResidencies;
        containers::DynamicArray<u32> retiringResidencies;
        containers::DynamicArray<StartCandidate> startScratch;
        containers::DynamicArray<ProgressWorkItem> progressScratch;
        containers::DynamicArray<RetirementCandidate> retirementScratch;
        containers::DynamicArray<RetirementCandidate> retirementReadyScratch;
        containers::DynamicArray<ResidencyWork*> retirementDeleteScratch;
        containers::DynamicArray<resources::ResourceHandle> retirementResourceReleaseScratch;
        containers::DynamicArray<DemandSlot> demands;
        containers::DynamicArray<u32> recycledDemands;
        // One immutable renderer-generation copy. Worlds/components never mirror it.
        containers::DynamicArray<MeshDrawPhaseContext> drawPhases;
        MeshResidencyConfig config;
        u32 residencyCount = 0;
        u32 demandCount = 0;
        u64 demandsIssued = 0;
        u64 demandsCoalesced = 0;
        u64 demandsReleased = 0;
        u64 progressRecordVisits = 0;
        u64 retirementRecordVisits = 0;
        u64 publicationSerial = 0;
        RetainedMeshDrawable* firstDrawable = nullptr;
        RetainedMeshDrawable* lastDrawable = nullptr;
        u32 queuedDrawables = 0;
        static constexpr u32 MaximumDrawableChecks = 64;
        static constexpr u32 MaximumPreparationChecks = 256;
        static constexpr u32 MaximumPreparationChecksPerDrawable = 64;
        void QueueDrawable(RetainedMeshDrawable* drawable) noexcept
        {
            if (drawable->queued)
                return;
            drawable->previous = lastDrawable;
            drawable->next = nullptr;
            if (lastDrawable != nullptr)
                lastDrawable->next = drawable;
            else
                firstDrawable = drawable;
            lastDrawable = drawable;
            drawable->queued = true;
            ++queuedDrawables;
        }
        void RemoveDrawable(RetainedMeshDrawable* drawable) noexcept
        {
            if (!drawable->queued)
                return;
            if (drawable->previous != nullptr)
                drawable->previous->next = drawable->next;
            else
                firstDrawable = drawable->next;
            if (drawable->next != nullptr)
                drawable->next->previous = drawable->previous;
            else
                lastDrawable = drawable->previous;
            drawable->previous = nullptr;
            drawable->next = nullptr;
            drawable->queued = false;
            --queuedDrawables;
        }
        static constexpr u32 MaximumDrawablePublications = 64;
        RetainedMeshDrawable* stagedDrawables[MaximumDrawablePublications]{};
        containers::DynamicArray<GpuSceneUploadRequest> stagedRequests;
        GpuSceneAllocation catalogAllocations[2];
        u32 stagedCatalogRequestCount = 0;
        u64 stagedCatalogRevision = 0;
        u32 stagedDrawableCount = 0;
    };

    MeshDemandHandle::MeshDemandHandle(MeshResidencyManager& manager, const MeshDemandId demand, const MeshResidencyHandle residency) noexcept : m_manager(&manager), m_demand(demand), m_residency(residency) {}

    MeshDemandHandle::~MeshDemandHandle()
    {
        Reset();
    }

    MeshDemandHandle::MeshDemandHandle(MeshDemandHandle&& other) noexcept : m_manager(other.m_manager), m_demand(other.m_demand), m_residency(other.m_residency)
    {
        other.m_manager = nullptr;
        other.m_demand = {};
        other.m_residency = {};
    }

    MeshDemandHandle& MeshDemandHandle::operator=(MeshDemandHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_manager = other.m_manager;
            m_demand = other.m_demand;
            m_residency = other.m_residency;
            other.m_manager = nullptr;
            other.m_demand = {};
            other.m_residency = {};
        }
        return *this;
    }

    bool MeshDemandHandle::IsValid() const noexcept
    {
        return m_manager != nullptr && m_manager->IsDemandValid(m_demand);
    }

    MeshResidencyHandle MeshDemandHandle::GetResidency() const noexcept
    {
        return IsValid() ? m_residency : MeshResidencyHandle{};
    }

    void MeshDemandHandle::Reset() noexcept
    {
        if (m_manager != nullptr && m_demand.IsValid())
            m_manager->ReleaseDemand(m_demand);
        m_manager = nullptr;
        m_demand = {};
        m_residency = {};
    }

    MeshResidencyManager::~MeshResidencyManager()
    {
        VG_ASSERT_MSG(m_impl == nullptr && m_definitions == nullptr && m_runtime == nullptr && m_materials == nullptr && m_phases == nullptr && !m_geometryAllocator.IsInitialized() && !m_geometryUploader.IsInitialized() &&
                          !m_lodUploader.IsInitialized(),
                      "mesh residency manager must be shut down before destruction");
    }

    bool MeshResidencyManager::Initialize(GpuSceneDefinitions& definitions, MaterialResidencyRuntime& materials, const RenderPhaseRegistry& phases,
                                          const containers::ArraySpan<const MeshDrawPhaseContext> drawPhases, const MeshResidencyConfig& config,
                                          MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!materials.IsInitialized() || !phases.IsSealed())
            return Fail(failure, MeshResidencyFailureCode::InvalidDependency, "mesh renderable topology requires initialized material residency and a sealed render phase registry");
        if (!ValidDrawPhaseContexts(phases, drawPhases))
            return Fail(failure, MeshResidencyFailureCode::InvalidConfiguration, "mesh renderable topology requires valid renderer-owned draw phase contexts");
        if (!Initialize(definitions, config, failure))
            return false;
        m_impl->drawPhases.Reserve(drawPhases.Size());
        for (const MeshDrawPhaseContext& context : drawPhases)
            m_impl->drawPhases.PushBack(context);
        m_materials = &materials;
        m_phases = &phases;
        return true;
    }

    bool MeshResidencyManager::Initialize(GpuSceneRuntime& runtime, MaterialResidencyRuntime& materials, const RenderPhaseRegistry& phases,
                                          const containers::ArraySpan<const MeshDrawPhaseContext> drawPhases, const MeshResidencyConfig& config,
                                          MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!runtime.IsInitialized())
            return Fail(failure, MeshResidencyFailureCode::InvalidDependency, "drawable meshes require the shared GPU Scene runtime");
        if (!Initialize(runtime.GetDefinitions(), materials, phases, drawPhases, config, failure))
            return false;
        m_runtime = &runtime;
        return true;
    }

    bool MeshResidencyManager::Initialize(GpuSceneDefinitions& definitions, const MeshResidencyConfig& config, MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (IsInitialized() || m_impl != nullptr || m_geometryAllocator.IsInitialized() || m_geometryUploader.IsInitialized() || m_lodUploader.IsInitialized())
            return Fail(failure, MeshResidencyFailureCode::AlreadyInitialized, "mesh residency manager is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "mesh residency manager must initialize on the main thread");
        if (!definitions.IsInitialized())
            return Fail(failure, MeshResidencyFailureCode::InvalidDependency, "mesh residency manager requires initialized GPU Scene definitions");
        if (config.maximumResidencyRecords == 0 || config.maximumDemands == 0 ||
            static_cast<u64>(config.geometryBatcher.maximumShells) + config.geometryBatcher.maximumBins + Impl::MaximumDrawablePublications * 3u >= InvalidGpuSceneIndex)
            return Fail(failure, MeshResidencyFailureCode::InvalidConfiguration, "mesh residency record configuration is invalid");

        GeometryAllocatorFailure allocatorFailure;
        if (!m_geometryAllocator.Initialize(config.geometryAllocator, &allocatorFailure))
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                failure->message = "mesh residency geometry allocator initialization failed";
                failure->allocatorFailure = allocatorFailure;
            }
            return false;
        }

        GeometryUploadFailure uploadFailure;
        if (!m_geometryUploader.Initialize(m_geometryAllocator, config.geometryUpload, &uploadFailure))
        {
            static_cast<void>(m_geometryAllocator.Shutdown());
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryUploaderFailure;
                failure->message = "mesh residency geometry uploader initialization failed";
                failure->uploadFailure = uploadFailure;
            }
            return false;
        }

        MeshLodGeometryUploadFailure lodUploadFailure;
        if (!m_lodUploader.Initialize(m_geometryAllocator, m_geometryUploader, config.lodUpload, &lodUploadFailure))
        {
            static_cast<void>(m_geometryUploader.Shutdown());
            static_cast<void>(m_geometryAllocator.Shutdown());
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::LodUploaderFailure;
                failure->message = "mesh residency LOD uploader initialization failed";
                failure->lodUploadFailure = lodUploadFailure;
            }
            return false;
        }

        m_impl = AllocateObject<Impl>(config);
        if (m_impl == nullptr)
        {
            static_cast<void>(m_lodUploader.Shutdown());
            static_cast<void>(m_geometryUploader.Shutdown());
            static_cast<void>(m_geometryAllocator.Shutdown());
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency record storage allocation failed");
        }
        if (!m_geometryBatcher.Initialize(config.geometryBatcher))
        {
            DeleteObject(m_impl);
            m_impl = nullptr;
            static_cast<void>(m_lodUploader.Shutdown());
            static_cast<void>(m_geometryUploader.Shutdown());
            static_cast<void>(m_geometryAllocator.Shutdown());
            return Fail(failure, MeshResidencyFailureCode::InvalidConfiguration, "mesh geometry batcher initialization failed");
        }
        m_definitions = &definitions;
        return true;
    }

    bool MeshResidencyManager::Shutdown(MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr && m_definitions == nullptr && !m_geometryAllocator.IsInitialized() && !m_geometryUploader.IsInitialized() && !m_lodUploader.IsInitialized())
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "mesh residency manager must shut down on the main thread");

        if (m_impl != nullptr)
        {
            if (!m_impl->stagedRequests.Empty())
                return Fail(failure, MeshResidencyFailureCode::DrawablePublicationFailure, "mesh publication must resolve before shutdown");
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                if (m_impl->demandCount != 0 || m_impl->residencyCount != 0)
                    return Fail(failure, MeshResidencyFailureCode::LiveResidencyRemains, "mesh residency manager still owns live demands or residency records");
            }
            if (m_geometryBatcher.HasLiveLeases())
                return Fail(failure, MeshResidencyFailureCode::LiveResidencyRemains, "mesh geometry batcher still owns retained placements");
            if (m_runtime != nullptr)
                for (auto& allocation : m_impl->catalogAllocations)
                {
                    if (!allocation.IsValid())
                        continue;
                    auto& lifetime = m_runtime->GetLifetime();
                    const bool released = lifetime.GetState(allocation) == GpuSceneAllocationState::Allocated ? lifetime.Cancel(allocation) : lifetime.Retire(allocation);
                    if (!released)
                        return Fail(failure, MeshResidencyFailureCode::DrawablePublicationFailure, "geometry catalog retirement failed");
                    allocation = {};
                }
            if (m_geometryBatcher.Shutdown() != GeometryBatchResult::Success)
                return Fail(failure, MeshResidencyFailureCode::LiveResidencyRemains, "mesh geometry batcher still owns retained placements");
            DeleteObject(m_impl);
            m_impl = nullptr;
        }

        if (m_lodUploader.IsInitialized())
        {
            MeshLodGeometryUploadFailure lodUploadFailure;
            if (!m_lodUploader.Shutdown(&lodUploadFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = MeshResidencyFailureCode::LodUploaderFailure;
                    failure->message = "mesh residency LOD uploader shutdown failed";
                    failure->lodUploadFailure = lodUploadFailure;
                }
                return false;
            }
        }

        if (m_geometryUploader.IsInitialized())
        {
            GeometryUploadFailure uploadFailure;
            if (!m_geometryUploader.Shutdown(&uploadFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = MeshResidencyFailureCode::GeometryUploaderFailure;
                    failure->message = "mesh residency geometry uploader shutdown failed";
                    failure->uploadFailure = uploadFailure;
                }
                return false;
            }
        }

        if (m_geometryAllocator.IsInitialized())
        {
            GeometryAllocatorFailure allocatorFailure;
            if (!m_geometryAllocator.Shutdown(&allocatorFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                    failure->message = "mesh residency geometry allocator shutdown failed";
                    failure->allocatorFailure = allocatorFailure;
                }
                return false;
            }
        }

        m_definitions = nullptr;
        m_runtime = nullptr;
        m_materials = nullptr;
        m_phases = nullptr;
        return true;
    }

    bool MeshResidencyManager::AbandonDevice(MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr && m_definitions == nullptr && !m_geometryAllocator.IsInitialized() && !m_geometryUploader.IsInitialized() && !m_lodUploader.IsInitialized())
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "mesh residency abandonment must run on the main thread");
        // Invalidate leases and release their requests while these residency
        // owners still exist. Outstanding invalid CPU leases remain resettable.
        if (m_impl != nullptr)
            for (auto& slot : m_impl->residencies)
                if (slot.work != nullptr && slot.work->drawable != nullptr)
                {
                    auto* drawable = slot.work->drawable;
                    m_impl->RemoveDrawable(drawable);
                    drawable->preparations.Clear();
                    drawable->leases.Clear();
                    drawable->demand.Reset();
                    drawable->owner = nullptr;
                    drawable->state = MeshDrawableState::Invalid;
                    slot.work->drawable = nullptr;
                    if (drawable->references == 0)
                        DeleteObject(drawable);
                }
        m_geometryBatcher.AbandonDevice();
        m_lodUploader.AbandonDevice();
        if (m_impl != nullptr)
        {
            for (Impl::ResidencySlot& slot : m_impl->residencies)
            {
                Impl::ResidencyWork* const work = slot.work;
                if (work == nullptr)
                    continue;
                work->geometry.geometries.Clear();
                work->geometry.Reset();
                work->definitions.geometry.geometries.Clear();
                work->definitions.geometry.Reset();
                work->definitions.geometryDefinitions.Clear();
                work->definitions.Reset();
                work->renderable = {};
                work->materialDemands.Clear();
                work->materialHandles.Clear();
                work->lods.Clear();
                work->primitives.Clear();
                work->phases.Clear();
                DeleteObject(work);
                slot.work = nullptr;
            }
        }
        m_geometryUploader.AbandonDevice();
        m_geometryAllocator.AbandonDevice();
        DeleteObject(m_impl);
        m_impl = nullptr;
        m_definitions = nullptr;
        m_runtime = nullptr;
        m_materials = nullptr;
        m_phases = nullptr;
        return true;
    }

    bool MeshResidencyManager::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_definitions != nullptr && m_geometryAllocator.IsInitialized() && m_geometryUploader.IsInitialized() && m_lodUploader.IsInitialized();
    }

    bool MeshResidencyManager::RequestMesh(const resources::ResourceHandle& resource, MeshDemandHandle& demand, MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        if (demand.IsValid() || !resource.IsValid() || resource.GetType() != meshes::MeshResourceType)
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument, "mesh residency demand requires a live mesh resource and empty output handle");
        auto* const mesh = static_cast<meshes::MeshResourceObject*>(resource.Get());
        if (mesh == nullptr || !mesh->IsOpen() || mesh->GetMetadata().GetLods().Empty() || mesh->GetMetadata().GetLods().Size() > 0xffffu)
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument, "mesh residency demand references invalid or empty mesh metadata");
        const u16 anchorLod = static_cast<u16>(mesh->GetMetadata().GetLods().Size() - 1u);

        const auto findCoalescedResidency = [&]() noexcept
        {
            u32 candidate = InvalidMeshResidencyIndex;
            if (!m_impl->residencyByPath.Find(resource.GetPath().Id(), candidate))
                return InvalidMeshResidencyIndex;
            while (candidate != InvalidMeshResidencyIndex)
            {
                const Impl::ResidencySlot& slot = m_impl->residencies[candidate];
                if (slot.active && slot.state != MeshResidencyState::Failed && slot.state != MeshResidencyState::Retiring && slot.resource.GetGeneration() == resource.GetGeneration())
                    return candidate;
                candidate = slot.nextSamePath;
            }
            return InvalidMeshResidencyIndex;
        };

        // Fast coalescing never needs another strong resource reference. Only a fresh
        // residency retains one, and that ResourceRegistry operation happens outside
        // the mesh lock before the exact admission state is revalidated.
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            const u32 residencyIndex = findCoalescedResidency();
            if (residencyIndex != InvalidMeshResidencyIndex)
            {
                if (m_impl->demandCount >= m_impl->config.maximumDemands)
                    return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency demand capacity is exhausted");
                Impl::ResidencySlot& residency = m_impl->residencies[residencyIndex];
                if (residency.demandCount == 0xffffffffu)
                    return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency demand reference count overflowed");
                const u32 demandIndex = m_impl->AllocateDemand();
                Impl::DemandSlot& demandSlot = m_impl->demands[demandIndex];
                demandSlot.residency = m_impl->ResidencyId(residencyIndex);
                demandSlot.active = true;
                ++residency.demandCount;
                ++m_impl->demandCount;
                ++m_impl->demandsIssued;
                ++m_impl->demandsCoalesced;
                demand = MeshDemandHandle(*this, m_impl->DemandId(demandIndex), demandSlot.residency);
                return true;
            }
            if (m_impl->residencyCount >= m_impl->config.maximumResidencyRecords)
                return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency record capacity is exhausted");
            if (m_impl->demandCount >= m_impl->config.maximumDemands)
                return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency demand capacity is exhausted");
        }

        resources::ResourceHandle retainedResource = resource;
        resources::ResourceHandle releasedResource;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        u32 residencyIndex = findCoalescedResidency();
        const bool coalesced = residencyIndex != InvalidMeshResidencyIndex;
        if (!coalesced && m_impl->residencyCount >= m_impl->config.maximumResidencyRecords)
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency record capacity is exhausted");
        if (m_impl->demandCount >= m_impl->config.maximumDemands)
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency demand capacity is exhausted");

        if (!coalesced)
        {
            residencyIndex = m_impl->AllocateResidency();
            Impl::ResidencySlot& slot = m_impl->residencies[residencyIndex];
            VG_ASSERT_MSG(slot.resource.GetGeneration() == 0, "recycled mesh residency slot retained resource ownership");
            slot.resource = static_cast<resources::ResourceHandle&&>(retainedResource);
            slot.demandCount = 0;
            u32 previousHead = InvalidMeshResidencyIndex;
            static_cast<void>(m_impl->residencyByPath.Find(resource.GetPath().Id(), previousHead));
            slot.previousSamePath = InvalidMeshResidencyIndex;
            slot.nextSamePath = previousHead;
            if (previousHead != InvalidMeshResidencyIndex)
                m_impl->residencies[previousHead].previousSamePath = residencyIndex;
            slot.anchorLod = anchorLod;
            slot.state = MeshResidencyState::MetadataRetained;
            slot.active = true;
            m_impl->AddActiveResidency(residencyIndex);
            const bool indexed = previousHead == InvalidMeshResidencyIndex ? m_impl->residencyByPath.Insert(resource.GetPath().Id(), residencyIndex).IsSuccessful()
                                                                           : m_impl->residencyByPath.Set(resource.GetPath().Id(), residencyIndex).IsSuccessful();
            VG_ASSERT_MSG(indexed, "reserved mesh residency lookup capacity must accept a fresh record");
            ++m_impl->residencyCount;
        }

        Impl::ResidencySlot& residency = m_impl->residencies[residencyIndex];
        if (residency.demandCount == 0xffffffffu)
        {
            if (!coalesced)
                m_impl->RemoveResidency(residencyIndex, releasedResource);
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency demand reference count overflowed");
        }
        const u32 demandIndex = m_impl->AllocateDemand();
        Impl::DemandSlot& demandSlot = m_impl->demands[demandIndex];
        demandSlot.residency = m_impl->ResidencyId(residencyIndex);
        demandSlot.active = true;
        ++residency.demandCount;
        ++m_impl->demandCount;
        ++m_impl->demandsIssued;
        if (coalesced)
            ++m_impl->demandsCoalesced;
        demand = MeshDemandHandle(*this, m_impl->DemandId(demandIndex), demandSlot.residency);
        return true;
    }

    bool MeshResidencyManager::CancelDemand(MeshDemandHandle& demand, MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (demand.m_manager != this || !demand.m_demand.IsValid())
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument, "mesh demand does not belong to this residency manager");
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            if (m_impl->Find(demand.m_demand) == nullptr)
                return Fail(failure, MeshResidencyFailureCode::StaleDemand, "mesh demand handle is stale");
        }
        demand.Reset();
        return true;
    }

    void MeshResidencyManager::ReleaseDemand(const MeshDemandId demand) noexcept
    {
        if (m_impl == nullptr)
            return;
        resources::ResourceHandle releasedResource;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        Impl::DemandSlot* const demandSlot = m_impl->Find(demand);
        if (demandSlot == nullptr)
            return;
        const MeshResidencyHandle residencyId = demandSlot->residency;
        demandSlot->residency = {};
        demandSlot->active = false;
        demandSlot->generation = NextGeneration(demandSlot->generation);
        m_impl->recycledDemands.PushBack(demand.index);
        --m_impl->demandCount;
        ++m_impl->demandsReleased;

        Impl::ResidencySlot* const residency = m_impl->Find(residencyId);
        if (residency == nullptr || residency->demandCount == 0)
            return;
        --residency->demandCount;
        if (residency->demandCount == 0 && residency->state == MeshResidencyState::MetadataRetained)
            m_impl->RemoveResidency(residencyId.index, releasedResource);
    }

    bool MeshResidencyManager::Tick(MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "mesh residency progress must run on the main thread");
        if (!ProgressDrawables(failure))
            return false;

        const auto failLodUpload = [failure](const MeshLodGeometryUploadFailure& uploadFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::LodUploaderFailure;
                failure->message = message;
                failure->lodUploadFailure = uploadFailure;
            }
            return false;
        };
        const auto failAllocator = [failure](const GeometryAllocatorFailure& allocatorFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                failure->message = message;
                failure->allocatorFailure = allocatorFailure;
            }
            return false;
        };
        const auto failDefinitions = [failure](const MeshLodDefinitionFailure& definitionFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::LodDefinitionFailure;
                failure->message = message;
                failure->lodDefinitionFailure = definitionFailure;
            }
            return false;
        };
        const auto failMaterial = [failure](const MaterialResidencyRuntimeFailure& materialFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::MaterialFailure;
                failure->message = message;
                failure->materialFailure = materialFailure;
            }
            return false;
        };
        const auto failRenderable = [failure](const GpuSceneDefinitionFailure& definitionFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::RenderableDefinitionFailure;
                failure->message = message;
                failure->renderableDefinitionFailure = definitionFailure;
            }
            return false;
        };

        m_impl->startScratch.Clear();
        m_impl->progressScratch.Clear();
        struct TickScratchReset final
        {
            Impl* impl = nullptr;
            ~TickScratchReset()
            {
                impl->startScratch.Clear();
                impl->progressScratch.Clear();
            }
        } scratchReset{m_impl};

        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            m_impl->progressRecordVisits += m_impl->activeResidencies.Size();
            for (const u32 index : m_impl->activeResidencies)
            {
                const Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (slot.state == MeshResidencyState::MetadataRetained && slot.demandCount != 0 && slot.work == nullptr)
                    m_impl->startScratch.PushBack({index, slot.generation, slot.anchorLod});
            }
        }

        // Start each exact resource generation once. RequestMesh remains cheap and thread-safe; all page-source and
        // uploader mutation stays serialized here on the renderer/main thread.
        for (const Impl::StartCandidate& candidate : m_impl->startScratch)
        {
            Impl::ResidencyWork* const work = AllocateObject<Impl::ResidencyWork>();
            if (work == nullptr)
                return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency anchor-LOD work allocation failed");
            bool accepted = false;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[candidate.index];
                if (slot.active && slot.generation == candidate.generation && slot.state == MeshResidencyState::MetadataRetained &&
                    slot.demandCount != 0 && slot.work == nullptr)
                {
                    slot.work = work;
                    slot.state = MeshResidencyState::AnchorLodLoading;
                    accepted = true;
                }
            }
            if (!accepted)
            {
                DeleteObject(work);
                continue;
            }

            // Installing work transitions the slot out of MetadataRetained, so a
            // concurrent demand release can no longer recycle it. The pre-reserved
            // slot storage and exact work claim keep this owning handle stable while
            // the ResourceRegistry/uploader work runs without the mesh lock.
            const resources::ResourceHandle& resource = m_impl->residencies[candidate.index].resource;
            MeshLodGeometryUploadFailure uploadFailure;
            MeshLodGeometryUploadRequestId request;
            if (!m_lodUploader.Request(resource, candidate.anchorLod, request, io::eAsyncPriority_Streaming, &uploadFailure))
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[candidate.index];
                if (slot.active && slot.generation == candidate.generation && slot.work == work)
                {
                    work->uploadFailure = uploadFailure;
                    slot.state = MeshResidencyState::Failed;
                }
            }
            else
            {
                work->uploadRequest = request;
            }
        }
        m_impl->startScratch.Clear();

        MeshLodGeometryUploadFailure tickFailure;
        if (!m_lodUploader.Tick(&tickFailure))
            return failLodUpload(tickFailure, "mesh residency anchor-LOD uploader progress failed");

        const auto removeUnreferenced = [this](const u32 index, const u32 generation, Impl::ResidencyWork* const expectedWork) noexcept
        {
            Impl::ResidencyWork* removed = nullptr;
            resources::ResourceHandle releasedResource;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.generation != generation || slot.demandCount != 0 || slot.work != expectedWork)
                    return;
                removed = slot.work;
                slot.work = nullptr;
                m_impl->RemoveResidency(index, releasedResource);
            }
            DeleteObject(removed);
            releasedResource.Reset();
        };

        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            m_impl->progressRecordVisits += m_impl->activeResidencies.Size();
            for (const u32 index : m_impl->activeResidencies)
            {
                const Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (slot.work != nullptr)
                    m_impl->progressScratch.PushBack({slot.work, index, slot.generation, slot.demandCount, slot.state});
            }
        }

        for (const Impl::ProgressWorkItem& snapshot : m_impl->progressScratch)
        {
            const u32 index = snapshot.index;
            const u32 residencyGeneration = snapshot.generation;
            Impl::ResidencyWork* const work = snapshot.work;
            // Work-bearing records cannot be recycled by ReleaseDemand. Tick is the
            // sole owner that clears work, so the reserved slot's strong handle is a
            // stable borrowed reference for this serialized progress iteration.
            const resources::ResourceHandle& resource = m_impl->residencies[index].resource;
            const MeshResidencyState residencyState = snapshot.state;
            const u32 demandCount = snapshot.demandCount;
            const auto poison = [this, index, residencyGeneration, work]() noexcept
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (slot.active && slot.generation == residencyGeneration && slot.work == work)
                    slot.state = MeshResidencyState::Failed;
            };

            if (residencyState == MeshResidencyState::Failed)
            {
                if (demandCount != 0)
                    continue;
                if (!work->geometry.HasOwnership() && !work->definitions.HasOwnership() && !work->renderable.IsValid())
                {
                    removeUnreferenced(index, residencyGeneration, work);
                    continue;
                }
            }

            if (residencyState == MeshResidencyState::AnchorLodUploadSubmitted)
            {
                if (demandCount != 0)
                {
                    MeshLodDefinitionFailure definitionFailure;
                    if (!PreparePendingMeshLodDefinitions(work->geometry, *m_definitions, work->definitions, &definitionFailure))
                        return failDefinitions(definitionFailure, "mesh residency could not acquire anchor-LOD geometry definitions");
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot& slot = m_impl->residencies[index];
                    if (slot.active && slot.generation == residencyGeneration && slot.work == work)
                        slot.state = MeshResidencyState::AnchorLodDefinitionsSubmitted;
                    continue;
                }
            }
            else if (residencyState == MeshResidencyState::AnchorLodDefinitionsSubmitted)
            {
                if (demandCount != 0 && m_materials == nullptr)
                    continue;
                if (demandCount != 0)
                {
                    const auto* const mesh = static_cast<const meshes::MeshResourceObject*>(resource.Get());
                    if (mesh == nullptr || !mesh->IsOpen())
                    {
                        poison();
                        return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "mesh renderable topology lost its retained mesh metadata");
                    }
                    const auto materialSlots = mesh->GetMetadata().GetMaterialSlots();
                    work->materialDemands.Clear();
                    work->materialDemands.Reserve(materialSlots.Size());
                    for (const meshes::MaterialSlot& materialSlot : materialSlots)
                    {
                        const resources::ResourceHandle* const material = FindDependency(*mesh, materialSlot.material);
                        if (material == nullptr)
                        {
                            work->materialDemands.Clear();
                            poison();
                            return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "mesh material slot has no retained exact VMAT dependency");
                        }
                        MaterialDemandHandle materialDemand;
                        MaterialResidencyRuntimeFailure materialFailure;
                        if (!m_materials->RequestMaterial(*material, materialDemand, &materialFailure))
                        {
                            work->materialDemands.Clear();
                            poison();
                            return failMaterial(materialFailure, "mesh material-slot residency request failed");
                        }
                        work->materialDemands.PushBack(static_cast<MaterialDemandHandle&&>(materialDemand));
                    }
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot& slot = m_impl->residencies[index];
                    if (slot.active && slot.generation == residencyGeneration && slot.work == work)
                        slot.state = MeshResidencyState::MaterialsResolving;
                    continue;
                }
            }
            else if (residencyState == MeshResidencyState::MaterialsResolving)
            {
                if (demandCount != 0)
                {
                    bool materialsReady = true;
                    work->materialHandles.Clear();
                    work->materialHandles.Reserve(work->materialDemands.Size());
                    for (const MaterialDemandHandle& materialDemand : work->materialDemands)
                    {
                        MaterialResidencyInfo materialInfo;
                        MaterialResidencyRuntimeFailure materialFailure;
                        if (!m_materials->GetInfo(materialDemand.GetResidency(), materialInfo, &materialFailure))
                        {
                            poison();
                            return failMaterial(materialFailure, "mesh material-slot residency query failed");
                        }
                        if (materialInfo.state == MaterialResidencyState::Failed)
                        {
                            poison();
                            return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "mesh material-slot residency failed");
                        }
                        if (materialInfo.state != MaterialResidencyState::Resident || !materialInfo.material.IsValid())
                        {
                            materialsReady = false;
                            break;
                        }
                        work->materialHandles.PushBack(materialInfo.material);
                    }
                    if (!materialsReady)
                    {
                        work->materialHandles.Clear();
                        continue;
                    }

                    const auto* const mesh = static_cast<const meshes::MeshResourceObject*>(resource.Get());
                    if (mesh == nullptr || !mesh->IsOpen() || work->materialHandles.Size() != mesh->GetMetadata().GetMaterialSlots().Size())
                    {
                        poison();
                        return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "mesh renderable topology material closure is inconsistent");
                    }
                    const meshes::MeshFile& metadata = mesh->GetMetadata();
                    const auto authoredLods = metadata.GetLods();
                    const auto authoredSubmeshes = metadata.GetSubmeshes();
                    work->lods.Clear();
                    work->primitives.Clear();
                    work->phases.Clear();
                    work->phasePipelines.Clear();
                    work->lods.Reserve(authoredLods.Size());
                    work->primitives.Reserve(authoredSubmeshes.Size());
                    for (const meshes::LodRecord& authoredLod : authoredLods)
                        work->lods.PushBack({authoredLod.firstSubmesh, authoredLod.submeshCount, authoredLod.minimumScreenCoverage, 0.0f});
                    for (u32 submeshIndex = 0; submeshIndex < authoredSubmeshes.Size(); ++submeshIndex)
                    {
                        const meshes::SubmeshRecord& authoredSubmesh = authoredSubmeshes[submeshIndex];
                        const resources::ResourceHandle* const materialResource = FindDependency(*mesh, metadata.GetMaterialSlots()[authoredSubmesh.materialSlot].material);
                        const auto* const material = materialResource != nullptr ? static_cast<const materials::MaterialResourceObject*>(materialResource->Get()) : nullptr;
                        if (material == nullptr || !material->IsOpen())
                        {
                            poison();
                            return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "mesh renderable topology requires open VMAT material-slot dependencies");
                        }
                        const u32 firstPhase = work->phases.Size();
                        const pipelines::GraphicsState* depthState = nullptr;
                        const pipelines::GraphicsState* opaqueState = nullptr;
                        for (const materials::TechniqueRecord& technique : material->GetFile().GetTechniques())
                        {
                            const RenderPhaseKey phaseKey{technique.name};
                            const RenderPhaseId phase = m_phases->Find(phaseKey);
                            const MeshDrawPhaseContext* const drawContext = FindDrawPhaseContext(m_impl->drawPhases, phaseKey);
                            if (!phase.IsValid() || drawContext == nullptr)
                                continue;
                            const resources::ResourceHandle* const pipelineResource = FindDependency(*material, technique.pipeline);
                            const auto* const pipeline = pipelineResource != nullptr ? static_cast<const pipelines::PipelineResourceObject*>(pipelineResource->Get()) : nullptr;
                            if (pipeline == nullptr || !pipeline->IsOpen() || pipeline->GetFile().GetKind() != pipelines::PipelineKind::Graphics)
                            {
                                poison();
                                return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "mesh render phase technique requires a retained graphics pipeline");
                            }
                            crypto::Digest256 concretePipelineKey;
                            if (pipelines::CalculateConcretePipelineKey(pipeline->GetFile(), &drawContext->attachments, concretePipelineKey) != pipelines::Result::Success)
                            {
                                poison();
                                return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology,
                                            "mesh render phase pipeline is incompatible with the selected renderer attachments");
                            }
                            const auto& graphics = pipeline->GetFile().GetGraphics();
                            const bool validSurfaceState = ValidCameraSurfaceState(phaseKey, graphics, drawContext->attachments);
                            if (!validSurfaceState)
                            {
                                poison();
                                return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology,
                                            "camera surface technique has fixed-function state incompatible with its renderer phase");
                            }
                            if (phaseKey == standardRenderPhases::DepthPrepass)
                                depthState = &graphics;
                            else if (phaseKey == standardRenderPhases::Opaque)
                                opaqueState = &graphics;
                            work->phases.PushBack({phase.index, GpuPhaseParticipationFlags::None, 0, 0});
                            work->phasePipelines.PushBack(&pipeline->GetFile());
                        }
                        if (depthState != nullptr && opaqueState != nullptr)
                        {
                            const bool twoSided = (static_cast<u16>(authoredSubmesh.flags) & static_cast<u16>(meshes::SubmeshFlags::TwoSided)) != 0;
                            if (depthState->depthStencil.depthCompare != opaqueState->depthStencil.depthCompare ||
                                depthState->rasterizer.frontFace != opaqueState->rasterizer.frontFace ||
                                depthState->rasterizer.depthClipEnable != opaqueState->rasterizer.depthClipEnable ||
                                (!twoSided && depthState->rasterizer.cull != opaqueState->rasterizer.cull))
                            {
                                poison();
                                return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "depth and GBuffer techniques disagree on depth direction or raster coverage");
                            }
                        }
                        const u32 phaseCount = work->phases.Size() - firstPhase;
                        if (phaseCount == 0)
                        {
                            poison();
                            return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "mesh material exposes no technique supported by the selected renderer");
                        }
                        GpuPrimitiveFlags primitiveFlags = GpuPrimitiveFlags::Indexed;
                        if ((static_cast<u16>(authoredSubmesh.flags) & static_cast<u16>(meshes::SubmeshFlags::TwoSided)) != 0)
                            primitiveFlags = static_cast<GpuPrimitiveFlags>(static_cast<u32>(primitiveFlags) | static_cast<u32>(GpuPrimitiveFlags::TwoSided));
                        work->primitives.PushBack({work->materialHandles[authoredSubmesh.materialSlot], firstPhase, phaseCount, submeshIndex, primitiveFlags});
                    }

                    GpuRenderableDefinition definition;
                    const MeshResidencyHandle residency{index, residencyGeneration};
                    definition.key = BuildRenderableKey(metadata, residency, resource.GetGeneration(), work->materialHandles, work->lods, work->primitives, work->phases);
                    definition.flags = metadata.GetKind() == meshes::MeshKind::Skinned ? GpuRenderableFlags::Skinned : GpuRenderableFlags::None;
                    definition.lods = work->lods;
                    definition.primitives = work->primitives;
                    definition.phaseParticipations = work->phases;
                    GpuRenderableHandle renderable;
                    GpuSceneDefinitionPublication publication;
                    GpuSceneDefinitionFailure definitionFailure;
                    if (!m_definitions->AcquireRenderables({&definition, 1}, {&renderable, 1}, publication, &definitionFailure))
                    {
                        poison();
                        return failRenderable(definitionFailure, "mesh renderable topology acquisition failed");
                    }
                    work->renderable = renderable;
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot& slot = m_impl->residencies[index];
                    if (slot.active && slot.generation == residencyGeneration && slot.work == work)
                        slot.state = MeshResidencyState::RenderableTopologySubmitted;
                    continue;
                }
            }
            else if (residencyState == MeshResidencyState::RenderableTopologySubmitted)
            {
                if (demandCount != 0)
                    continue;
            }
            else if (residencyState == MeshResidencyState::AnchorLodLoading || residencyState == MeshResidencyState::Cancelling)
            {
                const MeshLodGeometryUploadState uploadState = m_lodUploader.GetState(work->uploadRequest);
                if (uploadState == MeshLodGeometryUploadState::Complete)
                {
                    MeshLodGeometryUploadFailure takeFailure;
                    if (!m_lodUploader.TakeCompleted(work->uploadRequest, work->geometry, &takeFailure))
                        return failLodUpload(takeFailure, "mesh residency could not take submitted anchor-LOD geometry");
                    work->uploadRequest = {};
                    if (demandCount != 0)
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot& slot = m_impl->residencies[index];
                        if (slot.active && slot.generation == residencyGeneration && slot.work == work)
                            slot.state = MeshResidencyState::AnchorLodUploadSubmitted;
                        continue;
                    }
                }
                else if (uploadState == MeshLodGeometryUploadState::Failed)
                {
                    static_cast<void>(m_lodUploader.GetRequestFailure(work->uploadRequest, work->uploadFailure));
                    MeshLodGeometryUploadFailure cancelFailure;
                    if (!m_lodUploader.Cancel(work->uploadRequest, &cancelFailure))
                        return failLodUpload(cancelFailure, "mesh residency could not release a failed anchor-LOD request");
                    work->uploadRequest = {};
                    bool remove = false;
                    bool retry = false;
                    resources::ResourceHandle releasedResource;
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot& slot = m_impl->residencies[index];
                        if (slot.active && slot.generation == residencyGeneration && slot.work == work)
                        {
                            remove = slot.demandCount == 0;
                            retry = !remove && residencyState == MeshResidencyState::Cancelling;
                            if (remove || retry)
                                slot.work = nullptr;
                            if (remove)
                                m_impl->RemoveResidency(index, releasedResource);
                            else
                                slot.state = retry ? MeshResidencyState::MetadataRetained : MeshResidencyState::Failed;
                        }
                    }
                    if (remove || retry)
                        DeleteObject(work);
                    releasedResource.Reset();
                    continue;
                }
                else if (uploadState == MeshLodGeometryUploadState::Invalid)
                {
                    bool remove = false;
                    bool retry = false;
                    resources::ResourceHandle releasedResource;
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot& slot = m_impl->residencies[index];
                        if (slot.active && slot.generation == residencyGeneration && slot.work == work)
                        {
                            remove = slot.demandCount == 0;
                            retry = !remove && residencyState == MeshResidencyState::Cancelling;
                            if (remove || retry)
                                slot.work = nullptr;
                            if (remove)
                                m_impl->RemoveResidency(index, releasedResource);
                            else if (retry)
                                slot.state = MeshResidencyState::MetadataRetained;
                        }
                    }
                    if (remove || retry)
                        DeleteObject(work);
                    releasedResource.Reset();
                    continue;
                }
                else if (demandCount == 0 && residencyState != MeshResidencyState::Cancelling)
                {
                    MeshLodGeometryUploadFailure cancelFailure;
                    if (!m_lodUploader.Cancel(work->uploadRequest, &cancelFailure))
                        return failLodUpload(cancelFailure, "mesh residency anchor-LOD cancellation failed");
                    if (m_lodUploader.GetState(work->uploadRequest) == MeshLodGeometryUploadState::Invalid)
                    {
                        work->uploadRequest = {};
                        removeUnreferenced(index, residencyGeneration, work);
                    }
                    else
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot& slot = m_impl->residencies[index];
                        if (slot.active && slot.generation == residencyGeneration && slot.work == work)
                            slot.state = MeshResidencyState::Cancelling;
                    }
                    continue;
                }
                else
                {
                    continue;
                }
            }
            else
            {
                continue;
            }

            // No demand remains and ownership has reached physical geometry. Preserve the exact allocation IDs
            // before Abort clears the movable geometry object; record recycling waits for allocator collection.
            PendingMeshLodGeometry& ownedGeometry = work->definitions.HasOwnership() ? work->definitions.geometry : work->geometry;
            work->retiringAllocations.Clear();
            work->retiringAllocations.Reserve(ownedGeometry.geometries.Size());
            for (const GeometryPlacement placement : ownedGeometry.geometries)
                work->retiringAllocations.PushBack(placement.allocation);
            bool cancellationClaimed = false;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.generation != residencyGeneration || slot.work != work)
                    continue;
                if (slot.demandCount != 0)
                {
                    slot.state = work->renderable.IsValid() ? MeshResidencyState::RenderableTopologySubmitted :
                                 work->definitions.HasOwnership() ? MeshResidencyState::AnchorLodDefinitionsSubmitted : MeshResidencyState::AnchorLodUploadSubmitted;
                    continue;
                }
                slot.state = MeshResidencyState::Retiring;
                cancellationClaimed = true;
            }
            if (!cancellationClaimed)
                continue;
            const auto restoreCancellationClaim = [this, index, residencyGeneration, work]() noexcept
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.generation != residencyGeneration || slot.work != work || slot.state != MeshResidencyState::Retiring)
                    return;
                slot.state = work->renderable.IsValid() ? MeshResidencyState::RenderableTopologySubmitted :
                             work->definitions.HasOwnership() ? MeshResidencyState::AnchorLodDefinitionsSubmitted :
                             work->geometry.HasOwnership() ? MeshResidencyState::AnchorLodUploadSubmitted : MeshResidencyState::Failed;
            };
            if (work->renderable.IsValid())
            {
                GpuSceneDefinitionFailure definitionFailure;
                if (!m_definitions->Release(work->renderable, &definitionFailure))
                {
                    restoreCancellationClaim();
                    return failRenderable(definitionFailure, "mesh residency could not release cancelled renderable topology");
                }
                work->renderable = {};
            }
            work->materialDemands.Clear();
            work->materialHandles.Clear();
            work->lods.Clear();
            work->primitives.Clear();
            work->phases.Clear();
            if (work->definitions.HasOwnership())
            {
                MeshLodDefinitionFailure definitionFailure;
                if (!AbortPendingMeshLodDefinitions(work->definitions, *m_definitions, m_geometryAllocator, &definitionFailure))
                {
                    restoreCancellationClaim();
                    return failDefinitions(definitionFailure, "mesh residency could not retire cancelled anchor-LOD definitions");
                }
            }
            else
            {
                GeometryAllocatorFailure allocatorFailure;
                if (!AbortPendingMeshLodGeometry(work->geometry, m_geometryAllocator, &allocatorFailure))
                {
                    restoreCancellationClaim();
                    return failAllocator(allocatorFailure, "mesh residency could not retire cancelled anchor-LOD geometry");
                }
            }
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.generation != residencyGeneration || slot.work != work || slot.state != MeshResidencyState::Retiring)
                    continue;
                VG_ASSERT_MSG(slot.demandCount == 0, "mesh residency retirement claim admitted an additional demand");
                m_impl->AddRetiringResidency(index);
                m_impl->RemoveActiveResidency(index);
            }
        }
        return true;
    }

    bool MeshResidencyManager::SealRetirements(const rhi::ResidencyFenceSet& safeAfter, MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        GeometryAllocatorFailure allocatorFailure;
        if (!m_geometryAllocator.SealRetirements(safeAfter, &allocatorFailure))
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                failure->message = "mesh residency geometry retirement sealing failed";
                failure->allocatorFailure = allocatorFailure;
            }
            return false;
        }
        return true;
    }

    u32 MeshResidencyManager::CollectRetirements(MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            static_cast<void>(Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized"));
            return 0;
        }
        GeometryAllocatorFailure allocatorFailure;
        const u32 reclaimed = m_geometryAllocator.Collect(&allocatorFailure);
        if (allocatorFailure.code != GeometryAllocatorFailureCode::None)
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                failure->message = "mesh residency geometry retirement collection failed";
                failure->allocatorFailure = allocatorFailure;
            }
            return reclaimed;
        }

        m_impl->retirementScratch.Clear();
        m_impl->retirementReadyScratch.Clear();
        m_impl->retirementDeleteScratch.Clear();
        m_impl->retirementResourceReleaseScratch.Clear();
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            m_impl->retirementRecordVisits += m_impl->retiringResidencies.Size();
            for (const u32 index : m_impl->retiringResidencies)
            {
                const Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (slot.active && slot.state == MeshResidencyState::Retiring && slot.demandCount == 0 && slot.work != nullptr)
                    m_impl->retirementScratch.PushBack({slot.work, index, slot.generation});
            }
        }
        for (const Impl::RetirementCandidate& snapshot : m_impl->retirementScratch)
        {
            bool complete = true;
            for (const GeometryAllocationId allocation : snapshot.work->retiringAllocations)
                complete = complete && m_geometryAllocator.GetState(allocation) == GeometryAllocationState::Invalid;
            if (complete)
                m_impl->retirementReadyScratch.PushBack(snapshot);
        }
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            for (const Impl::RetirementCandidate& snapshot : m_impl->retirementReadyScratch)
            {
                Impl::ResidencySlot& slot = m_impl->residencies[snapshot.index];
                if (!slot.active || slot.generation != snapshot.generation || slot.state != MeshResidencyState::Retiring ||
                    slot.demandCount != 0 || slot.work != snapshot.work)
                    continue;
                slot.work = nullptr;
                m_impl->retirementResourceReleaseScratch.PushBack({});
                m_impl->RemoveResidency(snapshot.index, m_impl->retirementResourceReleaseScratch.Back());
                m_impl->retirementDeleteScratch.PushBack(snapshot.work);
            }
        }
        for (Impl::ResidencyWork* const work : m_impl->retirementDeleteScratch)
            DeleteObject(work);
        m_impl->retirementResourceReleaseScratch.Clear();
        m_impl->retirementScratch.Clear();
        m_impl->retirementReadyScratch.Clear();
        m_impl->retirementDeleteScratch.Clear();
        return reclaimed;
    }

    bool MeshResidencyManager::GetInfo(const MeshResidencyHandle residency, MeshResidencyInfo& info, MeshResidencyFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const Impl::ResidencySlot* const slot = m_impl->Find(residency);
        if (slot == nullptr)
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument, "mesh residency handle is stale");
        info.residency = residency;
        info.resourcePath = slot->resource.GetPath();
        info.resourceGeneration = slot->resource.GetGeneration();
        info.demandCount = slot->demandCount;
        info.anchorLod = slot->anchorLod;
        info.state = slot->state;
        if (slot->work != nullptr)
        {
            const PendingMeshLodGeometry& geometry = slot->work->definitions.IsValid() ? slot->work->definitions.geometry : slot->work->geometry;
            if (geometry.IsValid())
            {
                info.geometryCount = geometry.geometries.Size();
                info.vertexBytes = geometry.vertexBytes;
                info.indexBytes = geometry.indexBytes;
            }
            info.materialCount = slot->work->materialDemands.Size();
            info.renderable = slot->work->renderable;
        }
        return true;
    }

    bool MeshResidencyManager::PrepareDraw(const MeshDemandHandle& demand, const u32 sourceSubmesh, const RenderPhaseKey phaseKey, const pipelines::AttachmentSignature& attachments, MeshDrawPreparation& output,
                                           MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || m_materials == nullptr || m_phases == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh draw preparation requires the topology-enabled residency manager");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "mesh draw preparation must run on the main thread");
        if (output.mesh.IsValid() || output.normal.IsValid() || output.mirrored.IsValid())
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument, "mesh draw preparation output still owns a request");
        const resources::ResourceHandle* resource = nullptr;
        Impl::ResidencyWork* work = nullptr;
        u16 anchorLod = 0;
        {
            // Reuse the demand registry's existing short lock only for the
            // ownership snapshot. No validation or pipeline work under this lock.
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            const auto* slot = m_impl->Find(demand.m_residency);
            if (demand.m_manager != this || m_impl->Find(demand.m_demand) == nullptr || slot == nullptr)
                return Fail(failure, MeshResidencyFailureCode::StaleDemand, "mesh draw preparation demand is stale");
            if (slot->state != MeshResidencyState::RenderableTopologySubmitted)
                return Fail(failure, MeshResidencyFailureCode::DrawPreparationNotReady, "mesh topology is not ready for draw preparation");
            resource = &slot->resource;
            work = slot->work;
            anchorLod = slot->anchorLod;
        }
        const auto& mesh = static_cast<const meshes::MeshResourceObject*>(resource->Get())->GetMetadata();
        const auto& lod = mesh.GetLods()[anchorLod];
        if (sourceSubmesh < lod.firstSubmesh || sourceSubmesh - lod.firstSubmesh >= lod.submeshCount)
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument, "mesh draw preparation requires an uploaded anchor-LOD primitive");
        const auto& primitive = work->primitives[sourceSubmesh];
        const RenderPhaseId phase = m_phases->Find(phaseKey);
        const pipelines::PipelineFile* pipeline = nullptr;
        for (u32 index = 0; index < primitive.phaseParticipationCount; ++index)
        {
            const u32 slot = primitive.firstPhaseParticipation + index;
            if (work->phases[slot].phase == phase.index)
            {
                pipeline = work->phasePipelines[slot];
                break;
            }
        }
        if (!phase.IsValid() || pipeline == nullptr)
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument, "mesh primitive does not participate in the requested render phase");
        const bool drawLayoutValid = ValidateMeshDrawLayout(mesh, sourceSubmesh, *pipeline);
        if (!drawLayoutValid)
            return Fail(failure, MeshResidencyFailureCode::InvalidDrawLayout, "mesh vertex layout does not match its cooked phase pipeline");
        const u32 localSubmesh = sourceSubmesh - lod.firstSubmesh;
        const auto& uploaded = work->definitions.geometry;
        if (localSubmesh >= uploaded.submeshes.Size() || uploaded.submeshes[localSubmesh].submesh != sourceSubmesh)
            return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "anchor geometry is not in authored LOD order");
        const u32 geometry = uploaded.submeshes[localSubmesh].geometry;
        MeshDrawPreparation prepared;
        const bool meshRequested = RequestMesh(*resource, prepared.mesh, failure);
        if (!meshRequested)
            return false;
        const auto& submesh = mesh.GetSubmeshes()[sourceSubmesh];
        MaterialTechniqueDesc request;
        request.material = work->materialDemands[submesh.materialSlot].GetResidency();
        request.technique = phaseKey.value;
        request.attachments = &attachments;
        request.twoSided = (static_cast<u16>(submesh.flags) & static_cast<u16>(meshes::SubmeshFlags::TwoSided)) != 0;
        MaterialResidencyRuntimeFailure materialFailure;
        const bool normalRequested = m_materials->RequestTechnique(request, prepared.normal, &materialFailure);
        if (!normalRequested)
        {
            if (failure != nullptr)
                failure->materialFailure = materialFailure;
            return Fail(failure, MeshResidencyFailureCode::MaterialFailure, "normal mesh pipeline request failed");
        }
        request.mirrored = true;
        const bool mirroredRequested = m_materials->RequestTechnique(request, prepared.mirrored, &materialFailure);
        if (!mirroredRequested)
        {
            if (failure != nullptr)
                failure->materialFailure = materialFailure;
            return Fail(failure, MeshResidencyFailureCode::MaterialFailure, "mirrored mesh pipeline request failed");
        }
        prepared.sourceSubmesh = sourceSubmesh;
        prepared.phase = phase;
        prepared.sortCenter[0] = submesh.bounds.sphereCenter[0];
        prepared.sortCenter[1] = submesh.bounds.sphereCenter[1];
        prepared.sortCenter[2] = submesh.bounds.sphereCenter[2];
        prepared.geometry = work->definitions.geometryDefinitions[geometry];
        prepared.placement = uploaded.geometries[geometry];
        prepared.m_normal = prepared.normal.m_technique;
        prepared.m_mirrored = prepared.mirrored.m_technique;
        prepared.m_phase = prepared.phase;
        prepared.m_sourceSubmesh = prepared.sourceSubmesh;
        prepared.m_sortCenter[0] = prepared.sortCenter[0];
        prepared.m_sortCenter[1] = prepared.sortCenter[1];
        prepared.m_sortCenter[2] = prepared.sortCenter[2];
        output = std::move(prepared);
        return true;
    }

    GeometryBatchResult MeshResidencyManager::AcquireDrawPlacement(MeshDrawPreparation& preparation, GeometryBatchLease& output) noexcept
    {
        if (m_impl == nullptr || m_materials == nullptr)
            return GeometryBatchResult::NotInitialized;
        if (!concurrency::IsMainThread())
            return GeometryBatchResult::WrongThread;
        if (preparation.mesh.m_manager != this || output.IsValid())
            return GeometryBatchResult::InvalidPreparation;
        if (preparation.normal.m_runtime != m_materials || preparation.mirrored.m_runtime != m_materials ||
            preparation.normal.m_technique.index != preparation.m_normal.index || preparation.normal.m_technique.generation != preparation.m_normal.generation ||
            preparation.mirrored.m_technique.index != preparation.m_mirrored.index || preparation.mirrored.m_technique.generation != preparation.m_mirrored.generation ||
            preparation.phase != preparation.m_phase || preparation.sourceSubmesh != preparation.m_sourceSubmesh ||
            preparation.sortCenter[0] != preparation.m_sortCenter[0] || preparation.sortCenter[1] != preparation.m_sortCenter[1] ||
            preparation.sortCenter[2] != preparation.m_sortCenter[2])
            return GeometryBatchResult::InvalidPreparation;
        Impl::ResidencyWork* work = nullptr;
        u16 anchorLod = 0;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            const auto* slot = m_impl->Find(preparation.mesh.m_residency);
            if (slot == nullptr || m_impl->Find(preparation.mesh.m_demand) == nullptr || slot->state != MeshResidencyState::RenderableTopologySubmitted)
                return GeometryBatchResult::InvalidPreparation;
            work = slot->work;
            anchorLod = slot->anchorLod;
        }
        // Validate against retained authored topology, not caller-supplied raw
        // geometry IDs. Pipeline readiness queries remain outside the demand lock.
        const auto& lod = work->lods[anchorLod];
        if (preparation.sourceSubmesh < lod.firstPrimitive || preparation.sourceSubmesh - lod.firstPrimitive >= lod.primitiveCount)
            return GeometryBatchResult::InvalidPreparation;
        const auto& uploaded = work->definitions.geometry;
        const u32 localPrimitive = preparation.sourceSubmesh - lod.firstPrimitive;
        if (localPrimitive >= uploaded.submeshes.Size() || uploaded.submeshes[localPrimitive].submesh != preparation.sourceSubmesh)
            return GeometryBatchResult::InvalidPreparation;
        const u32 geometryIndex = uploaded.submeshes[localPrimitive].geometry;
        if (geometryIndex >= uploaded.geometries.Size() || geometryIndex >= work->definitions.geometryDefinitions.Size())
            return GeometryBatchResult::InvalidPreparation;
        const auto& expected = uploaded.geometries[geometryIndex];
        const auto& actual = preparation.placement;
        if (preparation.geometry != work->definitions.geometryDefinitions[geometryIndex] || actual.allocation != expected.allocation ||
            actual.vertex.arena != expected.vertex.arena || actual.vertex.firstVertex != expected.vertex.firstVertex || actual.vertex.vertexCount != expected.vertex.vertexCount ||
            actual.index.arena != expected.index.arena || actual.index.firstIndex != expected.index.firstIndex || actual.index.indexCount != expected.index.indexCount || actual.index.format != expected.index.format)
            return GeometryBatchResult::InvalidPreparation;
        const auto& primitive = work->primitives[preparation.sourceSubmesh];
        bool participates = false;
        for (u32 index = 0; index < primitive.phaseParticipationCount; ++index)
            participates |= work->phases[primitive.firstPhaseParticipation + index].phase == preparation.phase.index;
        MaterialTechniqueInfo normal;
        MaterialTechniqueInfo mirrored;
        if (!participates)
            return GeometryBatchResult::InvalidPreparation;
        const bool normalResolved = m_materials->GetTechniqueInfo(preparation.normal, normal);
        if (!normalResolved)
            return GeometryBatchResult::InvalidPreparation;
        const bool mirroredResolved = m_materials->GetTechniqueInfo(preparation.mirrored, mirrored);
        if (!mirroredResolved || normal.material != primitive.material || mirrored.material != primitive.material)
            return GeometryBatchResult::InvalidPreparation;
        return m_geometryBatcher.Acquire(preparation, *m_materials, output);
    }

    bool MeshResidencyManager::RequestDrawable(const MeshDemandHandle& demand, MeshDrawableBinding& output,
                                                MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || m_runtime == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "drawable mesh requests require shared publication ownership");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "drawable mesh preparation must run on the main thread");
        if (output.m_drawable != nullptr || m_impl->drawPhases.Empty())
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument, "drawable mesh request requires renderer draw policy and empty output");
        Impl::ResidencyWork* work = nullptr;
        const resources::ResourceHandle* resource = nullptr;
        u16 anchor = 0;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            const auto* slot = m_impl->Find(demand.m_residency);
            if (demand.m_manager != this || m_impl->Find(demand.m_demand) == nullptr || slot == nullptr)
                return Fail(failure, MeshResidencyFailureCode::StaleDemand, "drawable mesh demand is stale");
            if (slot->state != MeshResidencyState::RenderableTopologySubmitted)
                return Fail(failure, MeshResidencyFailureCode::DrawPreparationNotReady, "drawable mesh topology is not ready");
            work = slot->work;
            resource = &slot->resource;
            anchor = slot->anchorLod;
        }
        if (work->drawable != nullptr)
        {
            auto* drawable = work->drawable;
            if (drawable->references == 0 || drawable->state == MeshDrawableState::Withdrawing || drawable->state == MeshDrawableState::Retiring)
                return Fail(failure, MeshResidencyFailureCode::DrawPreparationNotReady, "previous drawable closure is withdrawing");
            if (drawable->references == 0xffffffffu)
                return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "drawable reference count is exhausted");
            ++drawable->references;
            output.m_drawable = drawable;
            return true;
        }
        if (anchor >= 64 || work->placementRevision == 0xffffffffu)
            return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "drawable anchor or placement revision is exhausted");
        const auto& lod = work->lods[anchor];
        if (lod.primitiveCount == 0 || lod.firstPrimitive > work->primitives.Size() || lod.primitiveCount > work->primitives.Size() - lod.firstPrimitive)
            return Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "drawable anchor primitive range is invalid");
        u32 memberCount = 0;
        for (u32 local = 0; local < lod.primitiveCount; ++local)
        {
            const auto& primitive = work->primitives[lod.firstPrimitive + local];
            if (primitive.phaseParticipationCount > m_impl->config.geometryBatcher.maximumPreparations - memberCount)
                return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "complete drawable exceeds placement capacity");
            memberCount += primitive.phaseParticipationCount;
        }
        const auto budget = m_runtime->GetContributionBudget();
        const u64 payloadBytes = sizeof(GpuRenderableResidency) + static_cast<u64>(lod.primitiveCount) * sizeof(GpuPrimitivePlacement) + static_cast<u64>(memberCount) * sizeof(GpuPhasePlacement);
        if (memberCount == 0 || budget.maximumUpdates < 3 || payloadBytes > budget.maximumBytes)
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "complete drawable placement cannot fit one shared upload compound");
        auto* drawable = AllocateObject<RetainedMeshDrawable>();
        if (drawable == nullptr)
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "drawable closure allocation failed");
        const auto rollback = [drawable]() noexcept { DeleteObject(drawable); return false; };
        if (!RequestMesh(*resource, drawable->demand, failure))
            return rollback();
        if (!m_definitions->GetRenderableAllocations(work->renderable, drawable->allocations))
        {
            static_cast<void>(Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "drawable lost retained GPU Scene allocations"));
            return rollback();
        }
        drawable->preparations.Resize(memberCount);
        drawable->leases.Resize(memberCount);
        drawable->primitives.Resize(lod.primitiveCount);
        drawable->phases.Resize(memberCount);
        drawable->firstPrimitive = lod.firstPrimitive;
        drawable->firstPhase = work->primitives[lod.firstPrimitive].firstPhaseParticipation;
        u32 member = 0;
        for (u32 local = 0; local < lod.primitiveCount; ++local)
        {
            const u32 source = lod.firstPrimitive + local;
            const auto& primitive = work->primitives[source];
            if (primitive.firstPhaseParticipation != drawable->firstPhase + member)
            {
                static_cast<void>(Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "drawable anchor phases are not contiguous"));
                return rollback();
            }
            for (u32 phaseIndex = 0; phaseIndex < primitive.phaseParticipationCount; ++phaseIndex)
            {
                const u32 phaseOrdinal = work->phases[primitive.firstPhaseParticipation + phaseIndex].phase;
                if (phaseOrdinal >= MaximumRenderPhases)
                {
                    static_cast<void>(Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "drawable topology contains an invalid phase"));
                    return rollback();
                }
                RenderPhaseDefinition phase;
                const bool phaseFound = m_phases->Get({static_cast<u8>(phaseOrdinal)}, phase);
                if (!phaseFound)
                {
                    static_cast<void>(Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology, "drawable topology contains a stale phase"));
                    return rollback();
                }
                const MeshDrawPhaseContext* const drawContext = FindDrawPhaseContext(m_impl->drawPhases, phase.key);
                if (drawContext == nullptr)
                {
                    static_cast<void>(Fail(failure, MeshResidencyFailureCode::InvalidArgument, "drawable request is missing a required anchor phase context"));
                    return rollback();
                }
                if (!PrepareDraw(demand, source, phase.key, drawContext->attachments, drawable->preparations[member++], failure))
                    return rollback();
            }
        }
        drawable->owner = this;
        if (!drawable->allocations.primitives.IsValid() || drawable->allocations.primitives.count < work->primitives.Size())
        {
            static_cast<void>(Fail(failure, MeshResidencyFailureCode::InvalidRenderableTopology,
                                   "drawable GPU primitive allocation does not cover retained topology"));
            return rollback();
        }
        drawable->info.renderable = work->renderable;
        drawable->info.placementRevision = ++work->placementRevision;
        drawable->info.anchorLod = anchor;
        drawable->info.placementCount = memberCount;
        drawable->info.firstGpuPrimitive = drawable->allocations.primitives.first;
        drawable->info.gpuPrimitiveCount = drawable->allocations.primitives.count;
        drawable->residency.generation = work->renderable.generation;
        drawable->residency.placementRevision = work->placementRevision;
        drawable->residency.anchorLod = anchor;
        if (anchor < 32)
            drawable->residency.residentLodMaskLow = 1u << anchor;
        else
            drawable->residency.residentLodMaskHigh = 1u << (anchor - 32);
        work->drawable = drawable;
        ScheduleDrawable(drawable);
        output.m_drawable = drawable;
        return true;
    }

    void MeshResidencyManager::ScheduleDrawable(RetainedMeshDrawable* drawable) noexcept
    {
        // Staged writers own a frozen closure. Their acceptance/retry callback
        // reschedules it, including a last release while staging was in flight.
        if (drawable->staged || (drawable->references != 0 &&
            (drawable->state == MeshDrawableState::Ready || drawable->state == MeshDrawableState::Failed)))
            return;
        m_impl->QueueDrawable(drawable);
    }

    bool MeshResidencyManager::ProgressDrawables(MeshResidencyFailure* const failure) noexcept
    {
        if (m_runtime == nullptr)
            return true;
        const u32 count = m_impl->queuedDrawables < Impl::MaximumDrawableChecks ? m_impl->queuedDrawables : Impl::MaximumDrawableChecks;
        u32 preparationChecks = 0;
        for (u32 check = 0; check < count && preparationChecks < Impl::MaximumPreparationChecks; ++check)
        {
            auto* drawable = m_impl->firstDrawable;
            m_impl->RemoveDrawable(drawable);
            m_impl->QueueDrawable(drawable);
            const u32 index = drawable->demand.m_residency.index;
            Impl::ResidencyWork* work = nullptr;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                work = m_impl->residencies[index].work;
            }
            if (drawable->references == 0 && drawable->state != MeshDrawableState::Retiring)
            {
                if (!drawable->placementAccepted)
                {
                    work->drawable = nullptr;
                    m_impl->RemoveDrawable(drawable);
                    DeleteObject(drawable);
                    continue;
                }
                drawable->state = MeshDrawableState::Withdrawing;
                drawable->residency.residentLodMaskLow = 0;
                drawable->residency.residentLodMaskHigh = 0;
                drawable->residency.anchorLod = InvalidGpuSceneIndex;
                if (drawable->withdrawalAccepted)
                {
                    // Retirement of these actual table allocations is the epoch
                    // ticket for the complete CPU closure. No second scheduler.
                    GpuSceneDefinitionFailure definitionFailure;
                    if (!m_definitions->Release(work->renderable, &definitionFailure))
                    {
                        if (failure != nullptr)
                            failure->renderableDefinitionFailure = definitionFailure;
                        return Fail(failure, MeshResidencyFailureCode::DrawablePublicationFailure, "withdrawn drawable definition retirement failed");
                    }
                    work->renderable = {};
                    drawable->state = MeshDrawableState::Retiring;
                }
            }
            if (drawable->state == MeshDrawableState::Retiring)
            {
                if (m_runtime->GetLifetime().GetState(drawable->allocations.renderable) != GpuSceneAllocationState::Invalid)
                    continue;
                work->drawable = nullptr;
                m_impl->RemoveDrawable(drawable);
                DeleteObject(drawable);
                // A remaining metadata demand may build fresh topology; it never
                // overwrites the old accepted generation during withdrawal.
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                m_impl->residencies[index].state = MeshResidencyState::AnchorLodDefinitionsSubmitted;
                continue;
            }
            if (drawable->state == MeshDrawableState::ResidencyPending && drawable->info.readyAfter.IsValid() && rhi::IsGpuFenceComplete(drawable->info.readyAfter))
                drawable->state = MeshDrawableState::Ready;
            if (drawable->state != MeshDrawableState::Preparing)
            {
                if (drawable->state == MeshDrawableState::Ready || drawable->state == MeshDrawableState::Failed)
                    m_impl->RemoveDrawable(drawable);
                continue;
            }
            bool failed = false;
            for (u32 checked = 0; checked < drawable->preparations.Size() && checked < Impl::MaximumPreparationChecksPerDrawable &&
                                  preparationChecks < Impl::MaximumPreparationChecks; ++checked)
            {
                ++preparationChecks;
                const u32 member = drawable->preparationCursor++ % drawable->preparations.Size();
                if (drawable->leases[member].IsValid())
                    continue;
                const auto result = AcquireDrawPlacement(drawable->preparations[member], drawable->leases[member]);
                if (result == GeometryBatchResult::Success)
                    ++drawable->preparedMembers;
                else if (result != GeometryBatchResult::Pending)
                {
                    drawable->failure = result;
                    failed = true;
                    break;
                }
            }
            if (failed)
            {
                drawable->preparations.Clear();
                drawable->leases.Clear();
                drawable->state = MeshDrawableState::Failed;
                m_impl->RemoveDrawable(drawable);
                continue;
            }
            if (drawable->preparedMembers != drawable->preparations.Size())
                continue;
            u32 member = 0;
            for (u32 local = 0; local < drawable->primitives.Size(); ++local)
            {
                const auto& primitive = work->primitives[drawable->firstPrimitive + local];
                for (u32 phase = 0; phase < primitive.phaseParticipationCount; ++phase, ++member)
                {
                    const auto& placement = *drawable->leases[member].GetPlacement();
                    const auto geometry = placement.normalGeometry.geometry;
                    drawable->primitives[local] = {geometry.index, geometry.generation, drawable->info.placementRevision, 0};
                    drawable->phases[member] = {placement.normalShell.index, placement.normalShell.generation, placement.normalBin.index, placement.normalBin.generation,
                                                placement.mirroredShell.index, placement.mirroredShell.generation, placement.mirroredBin.index, placement.mirroredBin.generation,
                                                drawable->info.placementRevision, 0, 0, 0};
                }
            }
            drawable->preparations.Clear();
            drawable->state = MeshDrawableState::PlacementPending;
        }
        return true;
    }

    bool MeshResidencyManager::StageGpuSceneContribution(bool& staged, MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        staged = false;
        if (m_impl == nullptr || m_runtime == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "drawable publication requires the shared GPU Scene runtime");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "drawable publication staging must run on the main thread");
        if (!m_impl->stagedRequests.Empty())
            return Fail(failure, MeshResidencyFailureCode::DrawablePublicationFailure, "previous drawable contribution has not resolved");
        const u32 count = m_impl->queuedDrawables < Impl::MaximumDrawableChecks ? m_impl->queuedDrawables : Impl::MaximumDrawableChecks;
        // A bounded set of complete compounds shares one contribution slot.
        // Tick rotates the active queue. Ready/failed bindings occupy no queue
        // slot, and publication performs no second preparation pass.
        u32 requestCount = 0;
        u64 requestBytes = 0;
        const auto budget = m_runtime->GetContributionBudget();
        const auto catalogChanges = m_geometryBatcher.GetCatalogChanges();
        m_impl->stagedCatalogRevision = 0;
        m_impl->stagedCatalogRequestCount = 0;
        if (!catalogChanges.Empty())
        {
            // Dedicated table ranges preserve the batcher's direct slot indices.
            // Their first publication initializes the full range once; subsequent
            // publications contain only coalesced dirty runs, including tombstones.
            auto& lifetime = m_runtime->GetLifetime();
            if (!m_impl->catalogAllocations[0].IsValid())
            {
                const GpuSceneAllocationRequest allocations[] = {
                    {GpuSceneTableKind::GeometryShell, m_impl->config.geometryBatcher.maximumShells},
                    {GpuSceneTableKind::GeometryBin, m_impl->config.geometryBatcher.maximumBins}};
                if (!lifetime.AllocateBatch({allocations, 2}, {m_impl->catalogAllocations, 2}))
                    return Fail(failure, MeshResidencyFailureCode::DrawablePublicationFailure, "geometry catalog table allocation failed");
                if (m_impl->catalogAllocations[0].first != 0 || m_impl->catalogAllocations[1].first != 0)
                {
                    if (!lifetime.CancelBatch({m_impl->catalogAllocations, 2}))
                        VG_FATAL("unpublished geometry catalog allocation rollback failed");
                    m_impl->catalogAllocations[0] = {};
                    m_impl->catalogAllocations[1] = {};
                    return Fail(failure, MeshResidencyFailureCode::DrawablePublicationFailure, "geometry catalog tables already have another owner");
                }
            }
            for (u32 table = 0; table < 2; ++table)
            {
                const auto allocation = m_impl->catalogAllocations[table];
                if (lifetime.GetState(allocation) == GpuSceneAllocationState::Allocated)
                    m_impl->stagedRequests.PushBack({allocation, 0, allocation.count});
                else
                {
                    const auto indices = table == 0 ? catalogChanges.shellIndices : catalogChanges.binIndices;
                    for (const u32 index : indices)
                        m_impl->stagedRequests.PushBack({allocation, index, 1});
                }
            }
            auto* requests = m_impl->stagedRequests.TypedData();
            std::sort(requests, requests + m_impl->stagedRequests.Size(), [](const GpuSceneUploadRequest& left, const GpuSceneUploadRequest& right) noexcept
            {
                return left.allocation.table != right.allocation.table ? left.allocation.table < right.allocation.table : left.allocationOffset < right.allocationOffset;
            });
            for (u32 index = 0; index < m_impl->stagedRequests.Size(); ++index)
            {
                const auto request = requests[index];
                if (requestCount != 0 && requests[requestCount - 1u].allocation == request.allocation &&
                    requests[requestCount - 1u].allocationOffset + requests[requestCount - 1u].elementCount == request.allocationOffset)
                    requests[requestCount - 1u].elementCount += request.elementCount;
                else
                    requests[requestCount++] = request;
            }
            while (m_impl->stagedRequests.Size() > requestCount)
                m_impl->stagedRequests.PopBack();
            for (const auto& request : m_impl->stagedRequests)
                requestBytes += static_cast<u64>(request.elementCount) *
                    (request.allocation.table == GpuSceneTableKind::GeometryShell ? sizeof(GpuGeometryShell) : sizeof(GpuGeometryBin));
            if (requestCount > budget.availableUpdates || requestBytes > budget.availableBytes)
            {
                m_impl->stagedRequests.Clear();
                // A partial catalog would mix shell counts and moved-bin ordinals.
                // Fail the tick instead of exposing that inconsistent publication.
                return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "complete dirty geometry catalog exceeds the shared publication budget");
            }
            m_impl->stagedCatalogRequestCount = requestCount;
            m_impl->stagedCatalogRevision = catalogChanges.revision;
        }
        auto* drawable = m_impl->firstDrawable;
        for (u32 attempt = 0; attempt < count && m_impl->stagedDrawableCount < Impl::MaximumDrawablePublications; ++attempt, drawable = drawable->next)
        {
            const u32 index = drawable->demand.m_residency.index;
            Impl::ResidencyWork* work = nullptr;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                work = m_impl->residencies[index].work;
            }
            if ((drawable->references == 0 && drawable->state != MeshDrawableState::Withdrawing) || (drawable->state != MeshDrawableState::PlacementPending && drawable->state != MeshDrawableState::ResidencyPending &&
                                        drawable->state != MeshDrawableState::Withdrawing) || drawable->withdrawalAccepted)
                continue;
            if (drawable->state == MeshDrawableState::ResidencyPending && drawable->info.readyAfter.IsValid())
                continue;
            // Initial copy-produced geometry is admitted only after its actual
            // GPU fence completes. This non-blocking poll is not a CPU job wait
            // and does not mislabel a copy producer as same-queue graphics work.
            if (!rhi::IsGpuFenceComplete(work->definitions.geometry.copyCompletion))
                continue;
            const u32 requiredUpdates = drawable->state == MeshDrawableState::PlacementPending ? 3u : 1u;
            const u64 requiredBytes = sizeof(GpuRenderableResidency) + (requiredUpdates == 3 ? static_cast<u64>(drawable->primitives.Size()) * sizeof(GpuPrimitivePlacement) +
                                                                                           static_cast<u64>(drawable->phases.Size()) * sizeof(GpuPhasePlacement) : 0);
            if (requiredUpdates > budget.availableUpdates - requestCount || requiredBytes > budget.availableBytes - requestBytes)
                continue;
            if (drawable->state == MeshDrawableState::PlacementPending)
            {
                // Establish a disabled residency image together with the full
                // placement payload, including recycled table storage.
                m_impl->stagedRequests.PushBack({drawable->allocations.renderable, 0, 1, GpuSceneTableKind::RenderableResidency});
                m_impl->stagedRequests.PushBack({drawable->allocations.primitives, drawable->firstPrimitive, drawable->primitives.Size(), GpuSceneTableKind::PrimitivePlacement});
                m_impl->stagedRequests.PushBack({drawable->allocations.phases, drawable->firstPhase, drawable->phases.Size(), GpuSceneTableKind::PhasePlacement});
            }
            else
                m_impl->stagedRequests.PushBack({drawable->allocations.renderable, 0, 1, GpuSceneTableKind::RenderableResidency});
            requestCount += requiredUpdates;
            m_impl->stagedDrawables[m_impl->stagedDrawableCount++] = drawable;
            requestBytes += requiredBytes;
        }
        if (requestCount == 0)
            return true;
        if (m_impl->publicationSerial == 0xffffffffffffffffull)
        {
            m_impl->stagedDrawableCount = 0;
            m_impl->stagedRequests.Clear();
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "drawable publication serial is exhausted");
        }
        const GpuSceneContributionToken token{++m_impl->publicationSerial, requestCount};
        const GpuSceneContributionDesc contribution{this, token, {m_impl->stagedRequests.TypedData(), requestCount}, &WriteDrawableContribution, &AcceptDrawableContribution, &RetryDrawableContribution};
        GpuSceneRuntimeFailure runtimeFailure;
        if (!m_runtime->StageContribution(contribution, &runtimeFailure))
        {
            m_impl->stagedDrawableCount = 0;
            m_impl->stagedRequests.Clear();
            return Fail(failure, MeshResidencyFailureCode::DrawablePublicationFailure, "shared GPU Scene rejected drawable contribution staging");
        }
        for (u32 index = 0; index < m_impl->stagedDrawableCount; ++index)
        {
            auto* stagedDrawable = m_impl->stagedDrawables[index];
            stagedDrawable->staged = true;
            m_impl->RemoveDrawable(stagedDrawable);
        }
        staged = true;
        return true;
    }

    bool MeshResidencyManager::WriteDrawableContribution(void* owner, const GpuSceneContributionToken token,
                                                         const containers::ArraySpan<const GpuSceneUploadReservation> reservations, const char*& message) noexcept
    {
        auto* manager = static_cast<MeshResidencyManager*>(owner);
        auto* impl = manager->m_impl;
        if (impl == nullptr || impl->stagedRequests.Empty() || token.value0 != impl->publicationSerial || token.value1 != impl->stagedRequests.Size())
        {
            message = "stale drawable contribution writer";
            return false;
        }
        u32 reservation = 0;
        if (impl->stagedCatalogRequestCount != 0 && manager->m_geometryBatcher.GetCatalogChanges().revision != impl->stagedCatalogRevision)
        {
            message = "geometry catalog changed across the joined publication boundary";
            return false;
        }
        for (; reservation < impl->stagedCatalogRequestCount; ++reservation)
        {
            const auto& request = impl->stagedRequests[reservation];
            const bool shell = request.allocation.table == GpuSceneTableKind::GeometryShell;
            const u64 bytes = static_cast<u64>(request.elementCount) * (shell ? sizeof(GpuGeometryShell) : sizeof(GpuGeometryBin));
            if (reservation >= reservations.Size() || !reservations[reservation].IsValid() || reservations[reservation].size != bytes)
            {
                message = "invalid geometry catalog upload reservation";
                return false;
            }
            for (u32 element = 0; element < request.elementCount; ++element)
            {
                const u32 index = request.allocationOffset + element;
                if (shell)
                {
                    GeometryShellCatalogEntry source;
                    static_cast<void>(manager->m_geometryBatcher.GetShellCatalogEntry(index, source));
                    GpuGeometryShell record;
                    record.generation = source.id.generation;
                    if (source.active)
                    {
                        record.flags = GpuGeometryCatalogActive;
                        record.phase = source.state.phase.index;
                        record.binCount = source.binCount;
                        record.vertexArena = source.state.vertexArena.index;
                        record.vertexArenaGeneration = source.state.vertexArena.generation;
                        record.indexArena = source.state.indexArena.index;
                        record.indexArenaGeneration = source.state.indexArena.generation;
                    }
                    static_cast<GpuGeometryShell*>(reservations[reservation].destination)[element] = record;
                }
                else
                {
                    GeometryBinCatalogEntry source;
                    static_cast<void>(manager->m_geometryBatcher.GetBinCatalogEntry(index, source));
                    GpuGeometryBin record;
                    record.generation = source.id.generation;
                    if (source.active)
                    {
                        record.flags = GpuGeometryCatalogActive;
                        record.shell = source.shell.index;
                        record.shellGeneration = source.shell.generation;
                        record.geometry = source.geometry.index;
                        record.geometryGeneration = source.geometry.generation;
                        record.shellOrdinal = source.shellOrdinal;
                        record.firstIndex = source.firstIndex;
                        record.indexCount = source.indexCount;
                        record.firstVertex = source.firstVertex;
                        record.vertexCount = source.vertexCount;
                    }
                    static_cast<GpuGeometryBin*>(reservations[reservation].destination)[element] = record;
                }
            }
        }
        const auto write = [&](const void* bytes, const u64 size) noexcept
        {
            if (reservation >= reservations.Size() || !reservations[reservation].IsValid() || reservations[reservation].size != size)
            {
                message = "drawable contribution was superseded or has invalid reservation size";
                return false;
            }
            std::memcpy(reservations[reservation++].destination, bytes, static_cast<size_t>(size));
            return true;
        };
        for (u32 index = 0; index < impl->stagedDrawableCount; ++index)
        {
            const auto* drawable = impl->stagedDrawables[index];
            if (!drawable->staged)
            {
                message = "drawable contribution lost its frozen ownership";
                return false;
            }
            if (drawable->state == MeshDrawableState::PlacementPending)
            {
                GpuRenderableResidency disabled;
                disabled.generation = drawable->info.renderable.generation;
                disabled.placementRevision = drawable->info.placementRevision;
                if (!write(&disabled, sizeof(disabled)) || !write(drawable->primitives.TypedData(), drawable->primitives.Size() * sizeof(GpuPrimitivePlacement)) ||
                    !write(drawable->phases.TypedData(), drawable->phases.Size() * sizeof(GpuPhasePlacement)))
                    return false;
            }
            else if (!write(&drawable->residency, sizeof(drawable->residency)))
                return false;
        }
        if (reservation != reservations.Size())
        {
            message = "drawable contribution reservation count mismatch";
            return false;
        }
        return true;
    }

    void MeshResidencyManager::AcceptDrawableContribution(void* owner, const GpuSceneContributionToken token, const rhi::GpuFence completion) noexcept
    {
        auto* manager = static_cast<MeshResidencyManager*>(owner);
        auto* impl = manager->m_impl;
        if (!concurrency::IsMainThread() || impl == nullptr || impl->stagedRequests.Empty() || token.value0 != impl->publicationSerial ||
            token.value1 != impl->stagedRequests.Size() || !completion.IsValid())
            VG_FATAL("invalid drawable contribution acceptance");
        for (u32 index = 0; index < impl->stagedDrawableCount; ++index)
        {
            auto* drawable = impl->stagedDrawables[index];
            if (!drawable->staged)
                VG_FATAL("drawable acceptance lost frozen ownership");
            if (drawable->state == MeshDrawableState::PlacementPending)
            {
                drawable->placementAccepted = true;
                drawable->state = MeshDrawableState::ResidencyPending;
            }
            else if (drawable->state == MeshDrawableState::ResidencyPending)
            {
                drawable->info.readyAfter = completion;
                // Initial readiness waits non-blockingly for actual GPU completion.
            }
            else if (drawable->state == MeshDrawableState::Withdrawing)
                drawable->withdrawalAccepted = true;
            else
                VG_FATAL("invalid drawable publication state");
            drawable->staged = false;
            manager->ScheduleDrawable(drawable);
            impl->stagedDrawables[index] = nullptr;
        }
        if (impl->stagedCatalogRequestCount != 0)
            static_cast<void>(manager->m_geometryBatcher.AcknowledgeCatalogChanges(impl->stagedCatalogRevision));
        impl->stagedCatalogRequestCount = 0;
        impl->stagedCatalogRevision = 0;
        impl->stagedRequests.Clear();
        impl->stagedDrawableCount = 0;
    }

    bool MeshResidencyManager::RetryDrawableContribution(void* owner, const GpuSceneContributionToken token, const char*& message) noexcept
    {
        auto* manager = static_cast<MeshResidencyManager*>(owner);
        auto* impl = manager->m_impl;
        if (!concurrency::IsMainThread() || impl == nullptr || impl->stagedRequests.Empty() || token.value0 != impl->publicationSerial || token.value1 != impl->stagedRequests.Size())
        {
            message = "stale drawable contribution retry";
            return false;
        }
        for (u32 index = 0; index < impl->stagedDrawableCount; ++index)
        {
            impl->stagedDrawables[index]->staged = false;
            manager->ScheduleDrawable(impl->stagedDrawables[index]);
            impl->stagedDrawables[index] = nullptr;
        }
        impl->stagedDrawableCount = 0;
        impl->stagedCatalogRequestCount = 0;
        impl->stagedCatalogRevision = 0;
        impl->stagedRequests.Clear();
        return true;
    }

    MeshResidencyStats MeshResidencyManager::GetStats() const noexcept
    {
        MeshResidencyStats stats;
        if (m_geometryAllocator.IsInitialized())
            stats.allocator = m_geometryAllocator.GetStats();
        if (m_geometryUploader.IsInitialized())
            stats.upload = m_geometryUploader.GetStats();
        if (m_lodUploader.IsInitialized())
            stats.lodUpload = m_lodUploader.GetStats();
        if (m_impl != nullptr)
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            stats.residencyRecords = m_impl->residencyCount;
            stats.activeResidencyRecords = m_impl->activeResidencies.Size();
            stats.retiringResidencyRecords = m_impl->retiringResidencies.Size();
            stats.liveDemands = m_impl->demandCount;
            stats.demandsIssued = m_impl->demandsIssued;
            stats.demandsCoalesced = m_impl->demandsCoalesced;
            stats.demandsReleased = m_impl->demandsReleased;
            stats.progressRecordVisits = m_impl->progressRecordVisits;
            stats.retirementRecordVisits = m_impl->retirementRecordVisits;
        }
        return stats;
    }

    bool MeshResidencyManager::IsDemandValid(const MeshDemandId demand) const noexcept
    {
        if (m_impl == nullptr || !demand.IsValid())
            return false;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        return demand.index < m_impl->demands.Size() && m_impl->demands[demand.index].active && m_impl->demands[demand.index].generation == demand.generation;
    }
} // namespace vanguard::rendering

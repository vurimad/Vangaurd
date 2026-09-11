#include <vanguard/rendering/material_residency_runtime.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] constexpr bool CompleteCutover(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return fences.Covers(rhi::QueueType::Graphics) && fences.Covers(rhi::QueueType::Compute) && fences.Covers(rhi::QueueType::Copy);
        }

        template <typename T, typename... Args> [[nodiscard]] T* AllocateObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(T), alignof(T));
            return block ? new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteObject(T* const object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void ClearFailure(MaterialResidencyRuntimeFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MaterialResidencyRuntimeFailure* const failure, const MaterialResidencyRuntimeFailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] const materials::LoadedMaterialDependency* FindDependency(const materials::MaterialResourceObject& material, const resources::ResourceReference reference) noexcept
        {
            const containers::ArraySpan<const materials::LoadedMaterialDependency> dependencies = material.GetLoadedDependencies();
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

    struct MaterialResidencyRuntime::Impl
    {
        struct ResidencySlot
        {
            resources::ResourceHandle root;
            MaterialMaterializationTicket ticket;
            GpuMaterialReference material;
            MaterialMaterializerFailure failure;
            u32 generation = 1;
            u32 demandCount = 0;
            u32 techniqueCount = 0;
            u32 nextSamePath = InvalidMaterialResidencyIndex;
            u32 pendingWorkIndex = InvalidMaterialResidencyIndex;
            u32 retirementIndex = InvalidMaterialResidencyIndex;
            MaterialResidencyState state = MaterialResidencyState::Invalid;
            bool active = false;
        };

        struct DemandSlot
        {
            u32 residencyIndex = InvalidMaterialResidencyIndex;
            u32 residencyGeneration = 0;
            u32 generation = 1;
            bool active = false;
        };

        struct NativeProgramSlot
        {
            resources::ResourceHandle resource;
            RenderShader* shader = nullptr;
            u64 lastUse = 0;
            u32 references = 0;
            u32 nextSamePath = InvalidMaterialTechniqueIndex;
            bool active = false;
        };

        struct WorkRef
        {
            u32 index = InvalidMaterialResidencyIndex;
            u32 generation = 0;
        };

        struct TechniqueSlot
        {
            TechniqueSlot() noexcept : programs(memory::pools::Rendering::GetInstance()) {}

            PipelineRequest pipeline;
            containers::DynamicArray<u32> programs;
            MaterialResidencyHandle residency;
            u32 generation = 1;
            bool active = false;
            bool depthTest = false;
            bool reverseDepth = false;
        };

        explicit Impl(const MaterialResidencyRuntimeConfig& value) noexcept
            : residencies(memory::pools::Rendering::GetInstance()), recycledResidencies(memory::pools::Rendering::GetInstance()), residencyByPath(memory::pools::Rendering::GetInstance()),
              pendingWork(memory::pools::Rendering::GetInstance()), pendingScratch(memory::pools::Rendering::GetInstance()), retirements(memory::pools::Rendering::GetInstance()),
              demands(memory::pools::Rendering::GetInstance()), recycledDemands(memory::pools::Rendering::GetInstance()), programs(memory::pools::Rendering::GetInstance()),
              recycledPrograms(memory::pools::Rendering::GetInstance()), programByPath(memory::pools::Rendering::GetInstance()), techniques(memory::pools::Rendering::GetInstance()),
              recycledTechniques(memory::pools::Rendering::GetInstance()), bindingLayouts(memory::pools::Rendering::GetInstance()), descriptorDomains(memory::pools::Rendering::GetInstance()), config(value)
        {
            residencies.Reserve(config.maximumResidencies);
            recycledResidencies.Reserve(config.maximumResidencies);
            residencyByPath.Reserve(config.maximumResidencies);
            pendingWork.Reserve(config.maximumResidencies);
            pendingScratch.Reserve(config.maximumResidencyChecksPerUpdate);
            retirements.Reserve(config.maximumResidencies);
            demands.Reserve(config.maximumDemands);
            recycledDemands.Reserve(config.maximumDemands);
            programs.Reserve(config.maximumNativePrograms);
            recycledPrograms.Reserve(config.maximumNativePrograms);
            programByPath.Reserve(config.maximumNativePrograms);
            techniques.Reserve(config.maximumTechniqueRequests);
            recycledTechniques.Reserve(config.maximumTechniqueRequests);
        }

        void AddPendingWork(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.pendingWorkIndex != InvalidMaterialResidencyIndex)
                return;
            slot.pendingWorkIndex = pendingWork.Size();
            pendingWork.PushBack(index);
        }

        void RemovePendingWork(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.pendingWorkIndex == InvalidMaterialResidencyIndex)
                return;
            const u32 workIndex = slot.pendingWorkIndex;
            const u32 moved = pendingWork.Back();
            pendingWork[workIndex] = moved;
            pendingWork.PopBack();
            slot.pendingWorkIndex = InvalidMaterialResidencyIndex;
            if (workIndex < pendingWork.Size())
                residencies[moved].pendingWorkIndex = workIndex;
            if (nextPendingWork >= pendingWork.Size())
                nextPendingWork = 0;
        }

        void BuildPendingScratch() noexcept
        {
            pendingScratch.Clear();
            if (pendingWork.Empty())
            {
                nextPendingWork = 0;
                return;
            }
            const u32 count = pendingWork.Size() < config.maximumResidencyChecksPerUpdate ? pendingWork.Size() : config.maximumResidencyChecksPerUpdate;
            for (u32 offset = 0; offset < count; ++offset)
            {
                const u32 workIndex = (nextPendingWork + offset) % pendingWork.Size();
                const u32 residencyIndex = pendingWork[workIndex];
                pendingScratch.PushBack({residencyIndex, residencies[residencyIndex].generation});
            }
            nextPendingWork = (nextPendingWork + count) % pendingWork.Size();
        }

        void AddRetirement(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.retirementIndex != InvalidMaterialResidencyIndex)
                return;
            slot.retirementIndex = retirements.Size();
            retirements.PushBack(index);
        }

        void RemoveRetirement(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.retirementIndex == InvalidMaterialResidencyIndex)
                return;
            const u32 retirementIndex = slot.retirementIndex;
            const u32 moved = retirements.Back();
            retirements[retirementIndex] = moved;
            retirements.PopBack();
            slot.retirementIndex = InvalidMaterialResidencyIndex;
            if (retirementIndex < retirements.Size())
                residencies[moved].retirementIndex = retirementIndex;
        }

        void IndexResidency(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            u32 previousHead = InvalidMaterialResidencyIndex;
            static_cast<void>(residencyByPath.Find(slot.root.GetPath().Id(), previousHead));
            slot.nextSamePath = previousHead;
            const bool indexed =
                previousHead == InvalidMaterialResidencyIndex ? residencyByPath.Insert(slot.root.GetPath().Id(), index).IsSuccessful() : residencyByPath.Set(slot.root.GetPath().Id(), index).IsSuccessful();
            VG_ASSERT_MSG(indexed, "reserved material residency lookup capacity must accept a fresh record");
        }

        void UnindexResidency(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            const resources::ResourceId path = slot.root.GetPath().Id();
            u32 head = InvalidMaterialResidencyIndex;
            const bool found = residencyByPath.Find(path, head);
            VG_ASSERT_MSG(found, "active material residency must be reachable from its path lookup");
            if (!found)
                return;
            if (head == index)
            {
                if (slot.nextSamePath == InvalidMaterialResidencyIndex)
                    static_cast<void>(residencyByPath.Remove(path));
                else
                    static_cast<void>(residencyByPath.Set(path, slot.nextSamePath));
            }
            else
            {
                u32 previous = head;
                while (previous != InvalidMaterialResidencyIndex && residencies[previous].nextSamePath != index)
                    previous = residencies[previous].nextSamePath;
                VG_ASSERT_MSG(previous != InvalidMaterialResidencyIndex, "active material residency must be linked from its path head");
                if (previous != InvalidMaterialResidencyIndex)
                    residencies[previous].nextSamePath = slot.nextSamePath;
            }
            slot.nextSamePath = InvalidMaterialResidencyIndex;
        }

        [[nodiscard]] ResidencySlot* FindResidency(const MaterialResidencyHandle id) noexcept
        {
            return id.IsValid() && id.index < residencies.Size() && residencies[id.index].active && residencies[id.index].generation == id.generation ? &residencies[id.index] : nullptr;
        }

        [[nodiscard]] const ResidencySlot* FindResidency(const MaterialResidencyHandle id) const noexcept
        {
            return id.IsValid() && id.index < residencies.Size() && residencies[id.index].active && residencies[id.index].generation == id.generation ? &residencies[id.index] : nullptr;
        }

        [[nodiscard]] DemandSlot* FindDemand(const MaterialDemandId id) noexcept
        {
            return id.IsValid() && id.index < demands.Size() && demands[id.index].active && demands[id.index].generation == id.generation ? &demands[id.index] : nullptr;
        }

        [[nodiscard]] TechniqueSlot* FindTechnique(const MaterialTechniqueId id) noexcept
        {
            return id.IsValid() && id.index < techniques.Size() && techniques[id.index].active && techniques[id.index].generation == id.generation ? &techniques[id.index] : nullptr;
        }

        [[nodiscard]] const TechniqueSlot* FindTechnique(const MaterialTechniqueId id) const noexcept
        {
            return id.IsValid() && id.index < techniques.Size() && techniques[id.index].active && techniques[id.index].generation == id.generation ? &techniques[id.index] : nullptr;
        }

        [[nodiscard]] u32 AllocateResidency() noexcept
        {
            if (!recycledResidencies.Empty())
            {
                const u32 index = recycledResidencies.Back();
                recycledResidencies.PopBack();
                return index;
            }
            if (residencies.Size() >= config.maximumResidencies)
                return InvalidMaterialResidencyIndex;
            residencies.PushBack({});
            return residencies.Size() - 1u;
        }

        [[nodiscard]] u32 AllocateDemand() noexcept
        {
            if (!recycledDemands.Empty())
            {
                const u32 index = recycledDemands.Back();
                recycledDemands.PopBack();
                return index;
            }
            if (demands.Size() >= config.maximumDemands)
                return InvalidMaterialDemandIndex;
            demands.PushBack({});
            return demands.Size() - 1u;
        }

        [[nodiscard]] u32 AllocateTechnique() noexcept
        {
            if (!recycledTechniques.Empty())
            {
                const u32 index = recycledTechniques.Back();
                recycledTechniques.PopBack();
                return index;
            }
            if (techniques.Size() >= config.maximumTechniqueRequests)
                return InvalidMaterialTechniqueIndex;
            techniques.EmplaceBack();
            techniques.Back().programs.Reserve(16);
            return techniques.Size() - 1u;
        }

        void RecycleResidency(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            RemovePendingWork(index);
            RemoveRetirement(index);
            UnindexResidency(index);
            slot.root.Reset();
            slot.ticket = {};
            slot.material = {};
            slot.failure = {};
            slot.demandCount = 0;
            slot.techniqueCount = 0;
            slot.nextSamePath = InvalidMaterialResidencyIndex;
            slot.state = MaterialResidencyState::Invalid;
            slot.active = false;
            slot.generation = NextGeneration(slot.generation);
            recycledResidencies.PushBack(index);
            --stats.residencyRecords;
        }

        void ReleasePrograms(TechniqueSlot& technique) noexcept
        {
            for (const u32 programIndex : technique.programs)
            {
                NativeProgramSlot& program = programs[programIndex];
                if (program.references != 0)
                {
                    if (program.references == 1)
                        --stats.referencedNativePrograms;
                    --program.references;
                }
            }
            technique.programs.Clear();
        }

        void RecycleTechnique(const u32 index) noexcept
        {
            TechniqueSlot& slot = techniques[index];
            ReleasePrograms(slot);
            slot.pipeline.Reset();
            slot.residency = {};
            slot.active = false;
            slot.generation = NextGeneration(slot.generation);
            recycledTechniques.PushBack(index);
            --stats.liveTechniqueRequests;
        }

        [[nodiscard]] u32 FindResidencyFor(const resources::ResourceHandle& root) noexcept
        {
            u32 index = InvalidMaterialResidencyIndex;
            if (!residencyByPath.Find(root.GetPath().Id(), index))
                return InvalidMaterialResidencyIndex;
            while (index != InvalidMaterialResidencyIndex)
            {
                const ResidencySlot& slot = residencies[index];
                ++stats.residencyLookupProbes;
                if (slot.active && slot.state != MaterialResidencyState::Retiring && slot.root.GetPath() == root.GetPath() && slot.root.GetGeneration() == root.GetGeneration() && slot.root.Get() == root.Get())
                    return index;
                index = slot.nextSamePath;
            }
            return InvalidMaterialResidencyIndex;
        }

        [[nodiscard]] u32 FindProgram(const resources::ResourceHandle& resource) noexcept
        {
            u32 index = InvalidMaterialTechniqueIndex;
            if (!programByPath.Find(resource.GetPath().Id(), index))
                return InvalidMaterialTechniqueIndex;
            while (index != InvalidMaterialTechniqueIndex)
            {
                const NativeProgramSlot& slot = programs[index];
                ++stats.nativeProgramLookupProbes;
                if (slot.active && slot.resource.GetPath() == resource.GetPath() && slot.resource.GetGeneration() == resource.GetGeneration() && slot.resource.Get() == resource.Get())
                    return index;
                index = slot.nextSamePath;
            }
            return InvalidMaterialTechniqueIndex;
        }

        void IndexProgram(const u32 index) noexcept
        {
            NativeProgramSlot& slot = programs[index];
            u32 previousHead = InvalidMaterialTechniqueIndex;
            static_cast<void>(programByPath.Find(slot.resource.GetPath().Id(), previousHead));
            slot.nextSamePath = previousHead;
            const bool indexed =
                previousHead == InvalidMaterialTechniqueIndex ? programByPath.Insert(slot.resource.GetPath().Id(), index).IsSuccessful() : programByPath.Set(slot.resource.GetPath().Id(), index).IsSuccessful();
            VG_ASSERT_MSG(indexed, "reserved native material program lookup capacity must accept a fresh record");
        }

        void UnindexProgram(const u32 index) noexcept
        {
            NativeProgramSlot& slot = programs[index];
            const resources::ResourceId path = slot.resource.GetPath().Id();
            u32 head = InvalidMaterialTechniqueIndex;
            const bool found = programByPath.Find(path, head);
            VG_ASSERT_MSG(found, "cached native material program must be reachable from its path lookup");
            if (!found)
                return;
            if (head == index)
            {
                if (slot.nextSamePath == InvalidMaterialTechniqueIndex)
                    static_cast<void>(programByPath.Remove(path));
                else
                    static_cast<void>(programByPath.Set(path, slot.nextSamePath));
            }
            else
            {
                u32 previous = head;
                while (previous != InvalidMaterialTechniqueIndex && programs[previous].nextSamePath != index)
                    previous = programs[previous].nextSamePath;
                VG_ASSERT_MSG(previous != InvalidMaterialTechniqueIndex, "cached native material program must be linked from its path head");
                if (previous != InvalidMaterialTechniqueIndex)
                    programs[previous].nextSamePath = slot.nextSamePath;
            }
            slot.nextSamePath = InvalidMaterialTechniqueIndex;
        }

        [[nodiscard]] u32 SelectProgramSlot() noexcept
        {
            if (!recycledPrograms.Empty())
            {
                const u32 index = recycledPrograms.Back();
                recycledPrograms.PopBack();
                return index;
            }
            if (programs.Size() < config.maximumNativePrograms)
            {
                programs.PushBack({});
                return programs.Size() - 1u;
            }
            u32 selected = InvalidMaterialTechniqueIndex;
            u64 oldest = ~u64{0};
            for (u32 index = 0; index < programs.Size(); ++index)
                if (programs[index].active && programs[index].references == 0 && programs[index].lastUse < oldest)
                {
                    oldest = programs[index].lastUse;
                    selected = index;
                }
            if (selected != InvalidMaterialTechniqueIndex)
            {
                UnindexProgram(selected);
                DeleteObject(programs[selected].shader);
                programs[selected].shader = nullptr;
                programs[selected].resource.Reset();
                programs[selected].nextSamePath = InvalidMaterialTechniqueIndex;
                programs[selected].active = false;
                --stats.cachedNativePrograms;
            }
            return selected;
        }

        void RecycleProgramSlot(const u32 index) noexcept
        {
            NativeProgramSlot& slot = programs[index];
            VG_ASSERT_MSG(!slot.active && slot.shader == nullptr && !slot.resource.IsValid(), "only an empty native program slot may be recycled");
            slot.lastUse = 0;
            slot.references = 0;
            slot.nextSamePath = InvalidMaterialTechniqueIndex;
            recycledPrograms.PushBack(index);
        }

        MaterialMaterializer* materializer = nullptr;
        PipelineCache* pipelineCache = nullptr;
        containers::DynamicArray<ResidencySlot> residencies;
        containers::DynamicArray<u32> recycledResidencies;
        containers::HashMap<resources::ResourceId, u32> residencyByPath;
        containers::DynamicArray<u32> pendingWork;
        containers::DynamicArray<WorkRef> pendingScratch;
        containers::DynamicArray<u32> retirements;
        containers::DynamicArray<DemandSlot> demands;
        containers::DynamicArray<u32> recycledDemands;
        containers::DynamicArray<NativeProgramSlot> programs;
        containers::DynamicArray<u32> recycledPrograms;
        containers::HashMap<resources::ResourceId, u32> programByPath;
        containers::DynamicArray<TechniqueSlot> techniques;
        containers::DynamicArray<u32> recycledTechniques;
        containers::DynamicArray<rhi::BindingLayoutRef> bindingLayouts;
        containers::DynamicArray<rhi::DescriptorDomainRef> descriptorDomains;
        MaterialResidencyRuntimeConfig config;
        MaterialResidencyRuntimeStats stats;
        u32 nextPendingWork = 0;
        u64 useSerial = 0;
    };

    MaterialDemandHandle::~MaterialDemandHandle()
    {
        Reset();
    }

    MaterialDemandHandle::MaterialDemandHandle(MaterialDemandHandle&& other) noexcept : m_runtime(other.m_runtime), m_demand(other.m_demand), m_residency(other.m_residency)
    {
        other.m_runtime = nullptr;
        other.m_demand = {};
        other.m_residency = {};
    }

    MaterialDemandHandle& MaterialDemandHandle::operator=(MaterialDemandHandle&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_runtime = other.m_runtime;
        m_demand = other.m_demand;
        m_residency = other.m_residency;
        other.m_runtime = nullptr;
        other.m_demand = {};
        other.m_residency = {};
        return *this;
    }

    bool MaterialDemandHandle::IsValid() const noexcept
    {
        return m_runtime != nullptr && m_runtime->IsDemandValid(m_demand);
    }

    MaterialResidencyHandle MaterialDemandHandle::GetResidency() const noexcept
    {
        return IsValid() ? m_residency : MaterialResidencyHandle{};
    }

    void MaterialDemandHandle::Reset() noexcept
    {
        if (m_runtime != nullptr)
            m_runtime->ReleaseDemand(m_demand);
        m_runtime = nullptr;
        m_demand = {};
        m_residency = {};
    }

    MaterialTechniqueRequest::~MaterialTechniqueRequest()
    {
        Reset();
    }

    MaterialTechniqueRequest::MaterialTechniqueRequest(MaterialTechniqueRequest&& other) noexcept : m_runtime(other.m_runtime), m_technique(other.m_technique)
    {
        other.m_runtime = nullptr;
        other.m_technique = {};
    }

    MaterialTechniqueRequest& MaterialTechniqueRequest::operator=(MaterialTechniqueRequest&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_runtime = other.m_runtime;
        m_technique = other.m_technique;
        other.m_runtime = nullptr;
        other.m_technique = {};
        return *this;
    }

    bool MaterialTechniqueRequest::IsValid() const noexcept
    {
        return m_runtime != nullptr && m_runtime->IsTechniqueValid(m_technique);
    }

    void MaterialTechniqueRequest::Reset() noexcept
    {
        if (m_runtime != nullptr)
            m_runtime->ReleaseTechnique(m_technique);
        m_runtime = nullptr;
        m_technique = {};
    }

    MaterialResidencyRuntime::~MaterialResidencyRuntime()
    {
        VG_ASSERT_MSG(m_impl == nullptr, "material residency runtime must be shut down before destruction");
    }

    bool MaterialResidencyRuntime::Initialize(MaterialMaterializer& materializer, PipelineCache& pipelines, const PipelineInterfaceResources& interfaceResources, const MaterialResidencyRuntimeConfig& config,
                                              MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::AlreadyInitialized, "material residency runtime is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material residency runtime must initialize on the main thread");
        if (!materializer.IsInitialized() || !pipelines.IsInitialized())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidDependency, "material residency dependencies are not initialized");
        if (config.maximumResidencies == 0 || config.maximumDemands == 0 || config.maximumTechniqueRequests == 0 || config.maximumNativePrograms == 0 || config.maximumResidencyChecksPerUpdate == 0 ||
            config.maximumRetirementsPerSeal == 0 || interfaceResources.bindingLayouts.Size() > rhi::MaximumBindingLayoutsPerPipeline ||
            interfaceResources.descriptorDomains.Size() > rhi::MaximumDescriptorDomainsPerPipeline)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidConfiguration, "material residency runtime configuration is invalid");
        m_impl = AllocateObject<Impl>(config);
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::CapacityExceeded, "material residency runtime allocation failed");
        m_impl->materializer = &materializer;
        m_impl->pipelineCache = &pipelines;
        m_impl->bindingLayouts = interfaceResources.bindingLayouts;
        m_impl->descriptorDomains = interfaceResources.descriptorDomains;
        return true;
    }

    bool MaterialResidencyRuntime::Shutdown(MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material residency runtime must shutdown on the main thread");
        if (m_impl->stats.residencyRecords != 0 || m_impl->stats.liveDemands != 0 || m_impl->stats.liveTechniqueRequests != 0 || m_impl->stats.referencedNativePrograms != 0)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::LiveWorkRemains, "material residency runtime shutdown requires zero live work");
        for (Impl::NativeProgramSlot& program : m_impl->programs)
        {
            DeleteObject(program.shader);
            program.shader = nullptr;
            program.resource.Reset();
        }
        DeleteObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool MaterialResidencyRuntime::AbandonDevice(MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material residency abandonment must run on the main thread");
        for (u32 index = 0; index < m_impl->techniques.Size(); ++index)
            if (m_impl->techniques[index].active)
                m_impl->RecycleTechnique(index);
        for (u32 index = 0; index < m_impl->demands.Size(); ++index)
        {
            Impl::DemandSlot& demand = m_impl->demands[index];
            if (!demand.active)
                continue;
            demand.active = false;
            demand.residencyIndex = InvalidMaterialResidencyIndex;
            demand.residencyGeneration = 0;
            demand.generation = NextGeneration(demand.generation);
            m_impl->recycledDemands.PushBack(index);
        }
        for (u32 index = 0; index < m_impl->residencies.Size(); ++index)
        {
            Impl::ResidencySlot& residency = m_impl->residencies[index];
            if (!residency.active)
                continue;
            if (residency.ticket.IsValid())
                static_cast<void>(m_impl->materializer->Cancel(residency.ticket));
            residency.material.Abandon();
            m_impl->RecycleResidency(index);
        }
        for (u32 index = 0; index < m_impl->programs.Size(); ++index)
        {
            Impl::NativeProgramSlot& program = m_impl->programs[index];
            if (!program.active)
                continue;
            m_impl->UnindexProgram(index);
            DeleteObject(program.shader);
            program = {};
            m_impl->recycledPrograms.PushBack(index);
        }
        m_impl->stats.liveDemands = 0;
        m_impl->stats.liveTechniqueRequests = 0;
        m_impl->stats.cachedNativePrograms = 0;
        m_impl->stats.referencedNativePrograms = 0;
        return true;
    }

    bool MaterialResidencyRuntime::ClearNativeProgramCache(MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "native material program cache must clear on the main thread");
        if (m_impl->stats.referencedNativePrograms != 0)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::LiveWorkRemains, "native material program cache still has referenced programs");
        for (u32 index = 0; index < m_impl->programs.Size(); ++index)
        {
            Impl::NativeProgramSlot& program = m_impl->programs[index];
            if (!program.active)
                continue;
            m_impl->UnindexProgram(index);
            DeleteObject(program.shader);
            program.shader = nullptr;
            program.resource.Reset();
            program.nextSamePath = InvalidMaterialTechniqueIndex;
            program.active = false;
            m_impl->RecycleProgramSlot(index);
        }
        m_impl->stats.cachedNativePrograms = 0;
        return true;
    }

    bool MaterialResidencyRuntime::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool MaterialResidencyRuntime::RequestMaterial(const resources::ResourceHandle& material, MaterialDemandHandle& demand, MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material residency requests must run on the main thread");
        if (demand.IsValid() || !material.IsValid())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidArgument, "material residency request or output demand is invalid");
        if (material.GetType() != materials::MaterialResourceType)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidMaterial, "material residency accepts only a loaded VMAT resource");
        const auto* const object = static_cast<const materials::MaterialResourceObject*>(material.Get());
        if (object == nullptr || !object->IsOpen())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidMaterial, "material residency requires an open VMAT closure");
        const u32 demandIndex = m_impl->AllocateDemand();
        if (demandIndex == InvalidMaterialDemandIndex)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::CapacityExceeded, "material demand capacity is exhausted");

        u32 residencyIndex = m_impl->FindResidencyFor(material);
        if (residencyIndex == InvalidMaterialResidencyIndex)
        {
            residencyIndex = m_impl->AllocateResidency();
            if (residencyIndex == InvalidMaterialResidencyIndex)
            {
                m_impl->recycledDemands.PushBack(demandIndex);
                return Fail(failure, MaterialResidencyRuntimeFailureCode::CapacityExceeded, "material residency record capacity is exhausted");
            }
            Impl::ResidencySlot& residency = m_impl->residencies[residencyIndex];
            residency.active = true;
            residency.root = material;
            m_impl->IndexResidency(residencyIndex);
            ++m_impl->stats.residencyRecords;
            MaterialMaterializerFailure materializationFailure;
            if (!m_impl->materializer->Begin(residency.root, residency.ticket, &materializationFailure))
            {
                m_impl->RecycleResidency(residencyIndex);
                m_impl->recycledDemands.PushBack(demandIndex);
                if (failure != nullptr)
                {
                    failure->code = materializationFailure.code == MaterialMaterializerFailureCode::CapacityExceeded ? MaterialResidencyRuntimeFailureCode::CapacityExceeded
                                                                                                                     : MaterialResidencyRuntimeFailureCode::MaterializationFailure;
                    failure->message = materializationFailure.message;
                    failure->materializationFailure = materializationFailure;
                }
                return false;
            }
            residency.state = MaterialResidencyState::Resolving;
            m_impl->AddPendingWork(residencyIndex);
        }
        else
        {
            ++m_impl->stats.demandsCoalesced;
        }

        Impl::ResidencySlot& residency = m_impl->residencies[residencyIndex];
        ++residency.demandCount;
        Impl::DemandSlot& demandSlot = m_impl->demands[demandIndex];
        demandSlot.active = true;
        demandSlot.residencyIndex = residencyIndex;
        demandSlot.residencyGeneration = residency.generation;
        demand.m_runtime = this;
        demand.m_demand = {demandIndex, demandSlot.generation};
        demand.m_residency = {residencyIndex, residency.generation};
        ++m_impl->stats.liveDemands;
        ++m_impl->stats.demandsIssued;
        return true;
    }

    bool MaterialResidencyRuntime::CancelDemand(MaterialDemandHandle& demand, MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material demand cancellation must run on the main thread");
        if (demand.m_runtime != this || m_impl->FindDemand(demand.m_demand) == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::StaleHandle, "material demand is stale");
        demand.Reset();
        return true;
    }

    bool MaterialResidencyRuntime::Update(MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material residency update must run on the main thread");
        MaterialMaterializerFailure materializationFailure;
        if (!m_impl->materializer->Update(&materializationFailure))
        {
            if (failure != nullptr)
            {
                failure->code = MaterialResidencyRuntimeFailureCode::MaterializationFailure;
                failure->message = materializationFailure.message;
                failure->materializationFailure = materializationFailure;
            }
            return false;
        }
        m_impl->BuildPendingScratch();
        for (const Impl::WorkRef work : m_impl->pendingScratch)
        {
            Impl::ResidencySlot* const current = m_impl->FindResidency({work.index, work.generation});
            if (current == nullptr || (current->state != MaterialResidencyState::Resolving && current->state != MaterialResidencyState::PublicationPending))
                continue;
            Impl::ResidencySlot& residency = *current;
            ++m_impl->stats.residencyStateChecks;
            GpuMaterialReference material;
            const MaterialMaterializationStatus status = m_impl->materializer->Poll(residency.ticket, material, &materializationFailure);
            if (status == MaterialMaterializationStatus::Pending)
                residency.state = MaterialResidencyState::Resolving;
            else if (status == MaterialMaterializationStatus::PublicationPending)
                residency.state = MaterialResidencyState::PublicationPending;
            else if (status == MaterialMaterializationStatus::Ready)
            {
                residency.material = static_cast<GpuMaterialReference&&>(material);
                residency.state = MaterialResidencyState::Resident;
                m_impl->RemovePendingWork(work.index);
            }
            else
            {
                residency.failure = materializationFailure;
                residency.state = MaterialResidencyState::Failed;
                m_impl->RemovePendingWork(work.index);
                ++m_impl->stats.materializationsFailed;
            }
        }
        return true;
    }

    bool MaterialResidencyRuntime::SealRetirements(const rhi::ResidencyFenceSet& safeAfter, MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material retirement sealing must run on the main thread");
        if (!CompleteCutover(safeAfter))
            return Fail(failure, MaterialResidencyRuntimeFailureCode::MissingRetirementFence, "material retirement requires graphics, compute, and copy cutover fences");
        u32 retired = 0;
        while (!m_impl->retirements.Empty() && retired < m_impl->config.maximumRetirementsPerSeal)
        {
            const u32 index = m_impl->retirements.Back();
            Impl::ResidencySlot& residency = m_impl->residencies[index];
            ++m_impl->stats.retirementChecks;
            MaterialMaterializerFailure materializationFailure;
            if (!residency.material.Retire(safeAfter, &materializationFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = MaterialResidencyRuntimeFailureCode::MaterializationFailure;
                    failure->message = materializationFailure.message;
                    failure->materializationFailure = materializationFailure;
                }
                return false;
            }
            m_impl->RecycleResidency(index);
            ++retired;
        }
        return true;
    }

    bool MaterialResidencyRuntime::GetInfo(const MaterialResidencyHandle material, MaterialResidencyInfo& info, MaterialResidencyRuntimeFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material residency queries must run on the main thread");
        const Impl::ResidencySlot* const residency = m_impl->FindResidency(material);
        if (residency == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::StaleHandle, "material residency handle is stale");
        info.residency = material;
        info.resourcePath = residency->root.GetPath();
        info.resourceGeneration = residency->root.GetGeneration();
        info.demandCount = residency->demandCount;
        info.techniqueCount = residency->techniqueCount;
        info.state = residency->state;
        info.material = residency->state == MaterialResidencyState::Resident ? residency->material.GetHandle() : GpuMaterialHandle{};
        info.failure = residency->failure;
        return true;
    }

    bool MaterialResidencyRuntime::RequestTechnique(const MaterialTechniqueDesc& desc, MaterialTechniqueRequest& request, MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material technique requests must run on the main thread");
        if (request.IsValid() || desc.technique == 0)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidArgument, "material technique request is invalid");
        Impl::ResidencySlot* const residency = m_impl->FindResidency(desc.material);
        if (residency == nullptr || residency->state != MaterialResidencyState::Resident || !residency->material.IsValid())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::StaleHandle, "material technique requires a resident material");
        const auto* const materialObject = static_cast<const materials::MaterialResourceObject*>(residency->root.Get());
        const materials::TechniqueRecord* technique = nullptr;
        for (const materials::TechniqueRecord& candidate : materialObject->GetFile().GetTechniques())
            if (candidate.name == desc.technique)
            {
                technique = &candidate;
                break;
            }
        if (technique == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::TechniqueNotFound, "material technique is not declared by this VMAT");
        const materials::LoadedMaterialDependency* const pipelineDependency = FindDependency(*materialObject, technique->pipeline);
        if (pipelineDependency == nullptr || !pipelineDependency->handle.IsValid() || pipelineDependency->handle.GetType() != pipelines::PipelineResourceType)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidMaterial, "material technique has no exact loaded pipeline dependency");
        const auto* const pipelineObject = static_cast<const pipelines::PipelineResourceObject*>(pipelineDependency->handle.Get());
        if (pipelineObject == nullptr || !pipelineObject->IsOpen())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidMaterial, "material technique pipeline dependency is not open");
        const containers::ArraySpan<const resources::ResourceHandle> shaderDependencies = pipelineObject->GetShaderDependencies();
        const containers::ArraySpan<const pipelines::ShaderReference> shaderReferences = pipelineObject->GetFile().GetShaders();
        if (shaderDependencies.Size() != shaderReferences.Size())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidMaterial, "material technique shader closure is inconsistent");
        const u32 techniqueIndex = m_impl->AllocateTechnique();
        if (techniqueIndex == InvalidMaterialTechniqueIndex)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::CapacityExceeded, "material technique request capacity is exhausted");
        Impl::TechniqueSlot& techniqueSlot = m_impl->techniques[techniqueIndex];
        techniqueSlot.active = true;
        techniqueSlot.residency = desc.material;
        const bool graphicsPipeline = pipelineObject->GetFile().GetKind() == pipelines::PipelineKind::Graphics;
        techniqueSlot.depthTest = graphicsPipeline && pipelineObject->GetFile().GetGraphics().depthStencil.depthTest;
        techniqueSlot.reverseDepth = graphicsPipeline &&
            (pipelineObject->GetFile().GetGraphics().depthStencil.depthCompare == pipelines::CompareOperation::Greater ||
             pipelineObject->GetFile().GetGraphics().depthStencil.depthCompare == pipelines::CompareOperation::GreaterEqual);
        ++m_impl->stats.liveTechniqueRequests;
        containers::DynamicArray<ResolvedRenderShader> resolved{memory::pools::Rendering::GetInstance()};
        resolved.Reserve(shaderDependencies.Size());

        for (u32 shaderIndex = 0; shaderIndex < shaderDependencies.Size(); ++shaderIndex)
        {
            const resources::ResourceHandle& shaderResource = shaderDependencies[shaderIndex];
            if (!shaderResource.IsValid() || shaderResource.GetType() != shaders::ShaderResourceType)
            {
                m_impl->RecycleTechnique(techniqueIndex);
                return Fail(failure, MaterialResidencyRuntimeFailureCode::InvalidMaterial, "material technique contains an invalid shader dependency");
            }
            u32 programIndex = m_impl->FindProgram(shaderResource);
            if (programIndex == InvalidMaterialTechniqueIndex)
            {
                programIndex = m_impl->SelectProgramSlot();
                if (programIndex == InvalidMaterialTechniqueIndex)
                {
                    m_impl->RecycleTechnique(techniqueIndex);
                    return Fail(failure, MaterialResidencyRuntimeFailureCode::CapacityExceeded, "native material program cache is exhausted");
                }
                Impl::NativeProgramSlot& program = m_impl->programs[programIndex];
                program.shader = AllocateObject<RenderShader>();
                if (program.shader == nullptr)
                {
                    m_impl->RecycleProgramSlot(programIndex);
                    m_impl->RecycleTechnique(techniqueIndex);
                    return Fail(failure, MaterialResidencyRuntimeFailureCode::CapacityExceeded, "native material program allocation failed");
                }
                const auto* const shaderObject = static_cast<const shaders::ShaderResourceObject*>(shaderResource.Get());
                rhi::Failure rhiFailure;
                const RenderShaderResult shaderResult = program.shader->Load(shaderObject->GetFile(), &rhiFailure);
                if (shaderResult != RenderShaderResult::Success)
                {
                    DeleteObject(program.shader);
                    program.shader = nullptr;
                    m_impl->RecycleProgramSlot(programIndex);
                    if (failure != nullptr)
                    {
                        failure->code = MaterialResidencyRuntimeFailureCode::NativeShaderFailure;
                        failure->message = "native material shader creation failed";
                        failure->shaderResult = shaderResult;
                        failure->rhiFailure = rhiFailure;
                    }
                    m_impl->RecycleTechnique(techniqueIndex);
                    return false;
                }
                program.resource = shaderResource;
                program.active = true;
                m_impl->IndexProgram(programIndex);
                ++m_impl->stats.cachedNativePrograms;
                ++m_impl->stats.nativeProgramLoads;
            }
            else
            {
                ++m_impl->stats.nativeProgramReuses;
            }
            Impl::NativeProgramSlot& program = m_impl->programs[programIndex];
            if (program.references == 0)
                ++m_impl->stats.referencedNativePrograms;
            ++program.references;
            program.lastUse = ++m_impl->useSerial;
            techniqueSlot.programs.PushBack(programIndex);
            resolved.PushBack({shaderReferences[shaderIndex].resource, program.shader});
        }

        const PipelineInterfaceResources interfaceResources{
            m_impl->bindingLayouts.Empty() ? containers::ArraySpan<const rhi::BindingLayoutRef>{}
                                           : containers::ArraySpan<const rhi::BindingLayoutRef>{m_impl->bindingLayouts.TypedData(), m_impl->bindingLayouts.Size()},
            m_impl->descriptorDomains.Empty() ? containers::ArraySpan<const rhi::DescriptorDomainRef>{}
                                              : containers::ArraySpan<const rhi::DescriptorDomainRef>{m_impl->descriptorDomains.TypedData(), m_impl->descriptorDomains.Size()}};
        const RenderPipelineRequest pipelineRequest{&pipelineObject->GetFile(), {resolved.TypedData(), resolved.Size()}, desc.attachments, interfaceResources, desc.priority, desc.mirrored, desc.twoSided};
        const RenderPipelineResult pipelineResult = RequestRenderPipeline(pipelineRequest, *m_impl->pipelineCache, techniqueSlot.pipeline);
        if (pipelineResult != RenderPipelineResult::Success)
        {
            m_impl->RecycleTechnique(techniqueIndex);
            if (failure != nullptr)
            {
                failure->code = MaterialResidencyRuntimeFailureCode::PipelineFailure;
                failure->message = "material technique pipeline admission failed";
                failure->pipelineResult = pipelineResult;
            }
            return false;
        }
        ++residency->techniqueCount;
        ++m_impl->stats.techniquesRequested;
        request.m_runtime = this;
        request.m_technique = {techniqueIndex, techniqueSlot.generation};
        return true;
    }

    bool MaterialResidencyRuntime::GetTechniqueInfo(const MaterialTechniqueRequest& request, MaterialTechniqueInfo& info, MaterialResidencyRuntimeFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material technique queries must run on the main thread");
        if (request.m_runtime != this)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::StaleHandle, "material technique request is stale");
        const Impl::TechniqueSlot* const technique = m_impl->FindTechnique(request.m_technique);
        if (technique == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::StaleHandle, "material technique request is stale");
        const Impl::ResidencySlot* const residency = m_impl->FindResidency(technique->residency);
        if (residency == nullptr || !residency->material.IsValid())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::StaleHandle, "material technique residency became stale");
        info.residency = technique->residency;
        info.depthTest = technique->depthTest;
        info.reverseDepth = technique->reverseDepth;
        info.material = residency->material.GetHandle();
        if (!technique->pipeline.HasFinished())
            info.state = MaterialTechniqueState::Pending;
        else if (technique->pipeline.HasSucceeded())
        {
            info.state = MaterialTechniqueState::Ready;
            info.pipeline = technique->pipeline.GetPipeline();
        }
        else
        {
            info.state = MaterialTechniqueState::Failed;
            info.pipelineFailure = technique->pipeline.GetError();
        }
        return true;
    }

    bool MaterialResidencyRuntime::ReleaseTechnique(MaterialTechniqueRequest& request, MaterialResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::NotInitialized, "material residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialResidencyRuntimeFailureCode::WrongThread, "material technique release must run on the main thread");
        if (request.m_runtime != this || m_impl->FindTechnique(request.m_technique) == nullptr)
            return Fail(failure, MaterialResidencyRuntimeFailureCode::StaleHandle, "material technique request is stale");
        request.Reset();
        return true;
    }

    MaterialResidencyRuntimeStats MaterialResidencyRuntime::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : MaterialResidencyRuntimeStats{};
    }

    void MaterialResidencyRuntime::ReleaseDemand(const MaterialDemandId demand) noexcept
    {
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return;
        Impl::DemandSlot* const demandSlot = m_impl->FindDemand(demand);
        if (demandSlot == nullptr)
            return;
        Impl::ResidencySlot* const residency = m_impl->FindResidency({demandSlot->residencyIndex, demandSlot->residencyGeneration});
        if (residency != nullptr && residency->demandCount != 0)
        {
            --residency->demandCount;
            if (residency->demandCount == 0 && residency->techniqueCount == 0)
            {
                const u32 residencyIndex = demandSlot->residencyIndex;
                if (residency->state == MaterialResidencyState::Resolving || residency->state == MaterialResidencyState::PublicationPending)
                {
                    MaterialMaterializerFailure ignored;
                    static_cast<void>(m_impl->materializer->Cancel(residency->ticket, &ignored));
                    m_impl->RecycleResidency(residencyIndex);
                }
                else if (residency->state == MaterialResidencyState::Failed)
                    m_impl->RecycleResidency(residencyIndex);
                else if (residency->state == MaterialResidencyState::Resident)
                {
                    residency->state = MaterialResidencyState::Retiring;
                    m_impl->AddRetirement(residencyIndex);
                }
            }
        }
        const u32 demandIndex = demand.index;
        demandSlot->active = false;
        demandSlot->residencyIndex = InvalidMaterialResidencyIndex;
        demandSlot->residencyGeneration = 0;
        demandSlot->generation = NextGeneration(demandSlot->generation);
        m_impl->recycledDemands.PushBack(demandIndex);
        --m_impl->stats.liveDemands;
        ++m_impl->stats.demandsReleased;
    }

    void MaterialResidencyRuntime::ReleaseTechnique(const MaterialTechniqueId technique) noexcept
    {
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return;
        Impl::TechniqueSlot* const techniqueSlot = m_impl->FindTechnique(technique);
        if (techniqueSlot == nullptr)
            return;
        Impl::ResidencySlot* const residency = m_impl->FindResidency(techniqueSlot->residency);
        const u32 residencyIndex = techniqueSlot->residency.index;
        const u32 techniqueIndex = technique.index;
        m_impl->RecycleTechnique(techniqueIndex);
        if (residency != nullptr && residency->techniqueCount != 0)
        {
            --residency->techniqueCount;
            if (residency->techniqueCount == 0 && residency->demandCount == 0 && residency->state == MaterialResidencyState::Resident)
            {
                residency->state = MaterialResidencyState::Retiring;
                m_impl->AddRetirement(residencyIndex);
            }
        }
    }

    bool MaterialResidencyRuntime::IsDemandValid(const MaterialDemandId demand) const noexcept
    {
        return m_impl != nullptr && demand.IsValid() && demand.index < m_impl->demands.Size() && m_impl->demands[demand.index].active && m_impl->demands[demand.index].generation == demand.generation;
    }

    bool MaterialResidencyRuntime::IsTechniqueValid(const MaterialTechniqueId technique) const noexcept
    {
        return m_impl != nullptr && m_impl->FindTechnique(technique) != nullptr;
    }
} // namespace vanguard::rendering

#include <vanguard/entities/rendering_runtime.hpp>
#include <vanguard/entities/static_mesh_component.hpp>
#include <vanguard/entities/scene_components.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/system/assert.hpp>

#include <cmath>
#include <new>

namespace vanguard::entities
{
    namespace
    {
        [[nodiscard]] rendering::RenderSceneMode SceneMode(const game::WorldMode mode) noexcept
        {
            switch (mode)
            {
            case game::WorldMode::Game:
            case game::WorldMode::Headless:
                return rendering::RenderSceneMode::Runtime;
            case game::WorldMode::Preview:
                return rendering::RenderSceneMode::Preview;
            case game::WorldMode::Thumbnail:
                return rendering::RenderSceneMode::Thumbnail;
            }
            return rendering::RenderSceneMode::Runtime;
        }

        void ClearFailure(rendering::RenderSceneFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(rendering::RenderSceneFailure* const failure, const rendering::RenderSceneFailureCode code,
                                const char* const message, const rendering::RenderSceneHandle scene = {},
                                const rendering::RenderProxyHandle proxy = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, scene, proxy, message};
            return false;
        }
    } // namespace

    struct RenderingRuntime::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Gameplay);

        enum class AdmissionKind : u8
        {
            Proxy,
            Mesh,
            Light,
            Decal
        };

        enum class AdmissionState : u8
        {
            Vacant,
            Queued,
            Processing
        };

        union AdmissionDescriptor
        {
            rendering::RenderProxyDesc proxy;
            rendering::MeshProxyDesc mesh;
            rendering::LightProxyDesc light;
            rendering::DecalProxyDesc decal;

            AdmissionDescriptor() noexcept {}
            ~AdmissionDescriptor() noexcept {}
        };

        struct AdmissionSlot
        {
            AdmissionDescriptor descriptor;
            ProxyAdmissionSink sink;
            u32 generation = 1;
            u32 nextFree = ~0u;
            u32 previousQueued = ~0u;
            u32 nextQueued = ~0u;
            AdmissionKind kind = AdmissionKind::Proxy;
            AdmissionState state = AdmissionState::Vacant;
        };

        struct PendingRetirement
        {
            rendering::RenderProxyHandle proxy;
            u32 remainingFrames = 0;
        };

        enum class DistantProxyPhase : u8
        {
            Inactive,
            AdmissionPending,
            Resident,
            ReleasePending
        };

        struct DistantProxyInstance
        {
            RenderingRuntime* owner = nullptr;
            resources::ResourceHandle resource;
            ProxyAdmissionHandle admission;
            rendering::RenderProxyHandle proxy;
            u32 recordIndex = 0;
            DistantProxyPhase phase = DistantProxyPhase::Inactive;
        };

        explicit Impl(const RenderingRuntimeConfig& config) noexcept
            : processingAdmissions(memory::pools::Rendering::GetInstance()),
              pendingRetirements(memory::pools::Rendering::GetInstance()), distantProxies(memory::pools::Rendering::GetInstance()),
              meshDrawPhases(memory::pools::Rendering::GetInstance())
        {
            admissionStorage = memory::Allocate(memory::PoolId::Rendering,
                                                static_cast<usize>(config.maximumPendingProxyAdmissions) * sizeof(AdmissionSlot), alignof(AdmissionSlot));
            if (!admissionStorage)
                return;
            admissionSlots = static_cast<AdmissionSlot*>(admissionStorage.address);
            admissionCapacity = config.maximumPendingProxyAdmissions;
            for (u32 index = 0; index < admissionCapacity; ++index)
                ::new (&admissionSlots[index]) AdmissionSlot;
            processingAdmissions.Reserve(config.maximumProxyAdmissionsPerFrame);
            pendingRetirements.Reserve(config.maximumPendingProxyRetirements);
            for (u32 index = 0; index < admissionCapacity; ++index)
                admissionSlots[index].nextFree = index + 1u < admissionCapacity ? index + 1u : ~0u;
            firstFreeAdmission = 0u;
            meshDrawPhases.Reserve(config.meshDrawPhases.Count());
            for (const auto& phase : config.meshDrawPhases)
                meshDrawPhases.PushBack(phase);
        }

        ~Impl()
        {
            for (u32 index = 0; index < admissionCapacity; ++index)
            {
                AdmissionSlot& slot = admissionSlots[index];
                if (slot.state != AdmissionState::Vacant)
                    DestroyAdmissionDescriptor(slot);
                slot.~AdmissionSlot();
            }
            memory::Free(admissionStorage);
        }

        [[nodiscard]] bool HasAdmissionStorage() const noexcept
        {
            return admissionSlots != nullptr && admissionCapacity != 0;
        }

        static void DestroyAdmissionDescriptor(AdmissionSlot& slot) noexcept
        {
            switch (slot.kind)
            {
            case AdmissionKind::Proxy:
                slot.descriptor.proxy.~RenderProxyDesc();
                break;
            case AdmissionKind::Mesh:
                slot.descriptor.mesh.~MeshProxyDesc();
                break;
            case AdmissionKind::Light:
                slot.descriptor.light.~LightProxyDesc();
                break;
            case AdmissionKind::Decal:
                slot.descriptor.decal.~DecalProxyDesc();
                break;
            }
        }

        [[nodiscard]] AdmissionSlot* FindAdmission(const ProxyAdmissionHandle admission) noexcept
        {
            if (!admission.IsValid() || admission.index >= admissionCapacity)
                return nullptr;
            AdmissionSlot& slot = admissionSlots[admission.index];
            return slot.state != AdmissionState::Vacant && slot.generation == admission.generation ? &slot : nullptr;
        }

        [[nodiscard]] bool AllocateAdmission(const AdmissionKind kind, const ProxyAdmissionSink& sink, ProxyAdmissionHandle& admission) noexcept
        {
            admission = {};
            if (!sink.IsValid() || firstFreeAdmission == ~0u)
                return false;
            const u32 index = firstFreeAdmission;
            AdmissionSlot& slot = admissionSlots[index];
            firstFreeAdmission = slot.nextFree;
            slot.nextFree = ~0u;
            slot.previousQueued = lastQueuedAdmission;
            slot.nextQueued = ~0u;
            slot.kind = kind;
            slot.state = AdmissionState::Queued;
            slot.sink = sink;
            if (lastQueuedAdmission != ~0u)
                admissionSlots[lastQueuedAdmission].nextQueued = index;
            else
                firstQueuedAdmission = index;
            lastQueuedAdmission = index;
            ++queuedAdmissionCount;
            admission = {index, slot.generation};
            return true;
        }

        void ReleaseAdmission(const u32 index) noexcept
        {
            AdmissionSlot& slot = admissionSlots[index];
            DestroyAdmissionDescriptor(slot);
            const u32 generation = slot.generation + 1u;
            slot.sink = {};
            slot.generation = generation != 0 ? generation : 1u;
            slot.nextFree = firstFreeAdmission;
            slot.previousQueued = ~0u;
            slot.nextQueued = ~0u;
            slot.kind = AdmissionKind::Proxy;
            slot.state = AdmissionState::Vacant;
            firstFreeAdmission = index;
        }

        void RemoveQueuedAdmission(AdmissionSlot& slot) noexcept
        {
            if (slot.previousQueued != ~0u)
                admissionSlots[slot.previousQueued].nextQueued = slot.nextQueued;
            else
                firstQueuedAdmission = slot.nextQueued;
            if (slot.nextQueued != ~0u)
                admissionSlots[slot.nextQueued].previousQueued = slot.previousQueued;
            else
                lastQueuedAdmission = slot.previousQueued;
            slot.previousQueued = ~0u;
            slot.nextQueued = ~0u;
            --queuedAdmissionCount;
        }

        rendering::RenderSceneHandle scene;
        TransformRuntime* transforms = nullptr;
        memory::MemoryBlock admissionStorage;
        AdmissionSlot* admissionSlots = nullptr;
        u32 admissionCapacity = 0;
        u32 firstQueuedAdmission = ~0u;
        u32 lastQueuedAdmission = ~0u;
        u32 queuedAdmissionCount = 0;
        containers::DynamicArray<u32> processingAdmissions;
        containers::DynamicArray<PendingRetirement> pendingRetirements;
        containers::DynamicArray<DistantProxyInstance> distantProxies;
        containers::DynamicArray<rendering::MeshDrawPhaseContext> meshDrawPhases;
        StaticMeshComponent* firstPendingMesh = nullptr;
        StaticMeshComponent* lastPendingMesh = nullptr;
        u32 pendingMeshCount = 0;
        concurrency::Atomic<CameraComponent*> dirtyCameras{nullptr};
        concurrency::Atomic<u32> dirtyCameraCount{0};
        u32 firstFreeAdmission = ~0u;
        u64 admittedProxies = 0;
        u64 cancelledProxyAdmissions = 0;
        u64 failedProxyAdmissions = 0;
        u64 retiredProxies = 0;
        u64 admittedDistantProxies = 0;
        u64 releasedDistantProxies = 0;
        u64 failedDistantProxies = 0;
        bool admissionFailureClaimed = false;
        rendering::RenderSceneFailure firstAdmissionFailure;
        bool retirementFailureClaimed = false;
        rendering::RenderSceneFailure firstRetirementFailure;
        concurrency::Atomic<bool> relinkFailureClaimed{false};
        concurrency::Atomic<u64> relinkFailures{0};
        VisualRelinkFailure firstRelinkFailure;
        bool ownsScene = false;
    };

    RenderingRuntime::RenderingRuntime(rendering::RenderSceneManager& scenes, const RenderingRuntimeConfig& config) noexcept
        : RuntimeSystem({RenderingRuntimeSystemId, "RenderingRuntime", game::RuntimeSystemFlags::All}), m_scenes(&scenes), m_config(config)
    {
    }

    RenderingRuntime::~RenderingRuntime()
    {
        if (m_impl != nullptr)
        {
            static_cast<void>(ReleaseScene());
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
        }
    }

    bool RenderingRuntime::BindMeshResidency(rendering::MeshResidencyManager& residency) noexcept
    {
        if (m_impl != nullptr || !concurrency::IsMainThread())
            return false;
        m_meshResidency = &residency;
        return true;
    }

    rendering::MeshResidencyManager* RenderingRuntime::GetMeshResidency() noexcept { return m_meshResidency; }

    bool RenderingRuntime::BindCommands(rendering::RenderCommandSystem& commands, const rendering::RenderPhaseRegistry& phases) noexcept
    {
        if (m_impl != nullptr || !concurrency::IsMainThread())
            return false;
        m_commands = &commands;
        m_renderPhases = &phases;
        return true;
    }

    void RenderingRuntime::QueueCameraTransform(CameraComponent& component) noexcept
    {
        // Different cameras can be updated concurrently, but a placed component
        // has one transform writer. Structural mutation and draining require a join.
        if (component.m_dirty)
            return;
        if (m_impl->dirtyCameraCount.Increment() > m_config.maximumPendingCameraTransforms)
        {
            static_cast<void>(m_impl->dirtyCameraCount.Decrement());
            const rendering::RenderSceneFailure failure{rendering::RenderSceneFailureCode::CapacityExceeded, m_impl->scene, {},
                                                        "camera transform queue capacity exceeded"};
            ReportRelinkFailure({{}, failure}, this);
            return;
        }
        component.m_dirty = true;
        component.m_previousDirty = nullptr;
        CameraComponent* previous = m_impl->dirtyCameras.GetValue();
        for (;;)
        {
            component.m_nextDirty = previous;
            CameraComponent* const observed = m_impl->dirtyCameras.CompareExchange(&component, previous);
            if (observed == previous)
            {
                // Only this successful successor writes the previous head's
                // backlink. Owner-thread removal waits for all producers to join.
                if (previous != nullptr)
                    previous->m_previousDirty = &component;
                break;
            }
            previous = observed;
        }
    }

    void RenderingRuntime::CancelCameraTransform(CameraComponent& component) noexcept
    {
        if (!component.m_dirty || m_impl == nullptr)
            return;
        VG_ASSERT_MSG(concurrency::IsMainThread() && (m_impl->transforms == nullptr || !m_impl->transforms->IsProcessing()),
                      "camera dirty-node removal requires joined transform producers");
        // Structural cancellation after the transform join; no dirty-list search.
        if (component.m_previousDirty != nullptr)
            component.m_previousDirty->m_nextDirty = component.m_nextDirty;
        else
            m_impl->dirtyCameras.SetValue(component.m_nextDirty);
        if (component.m_nextDirty != nullptr)
            component.m_nextDirty->m_previousDirty = component.m_previousDirty;
        static_cast<void>(m_impl->dirtyCameraCount.Decrement());
        component.m_nextDirty = nullptr;
        component.m_previousDirty = nullptr;
        component.m_dirty = false;
    }

    bool RenderingRuntime::FlushCameraTransforms() noexcept
    {
        if (!concurrency::IsMainThread())
            return false;
        if (m_impl == nullptr)
            return true;
        if (m_impl->transforms != nullptr && m_impl->transforms->IsProcessing())
            return false;
        CameraComponent* camera = m_impl->dirtyCameras.Exchange(nullptr);
        bool succeeded = true;
        while (camera != nullptr)
        {
            CameraComponent* const next = camera->m_nextDirty;
            camera->m_nextDirty = nullptr;
            camera->m_previousDirty = nullptr;
            camera->m_dirty = false;
            static_cast<void>(m_impl->dirtyCameraCount.Decrement());
            if (!camera->ApplyTransform())
            {
                ReportComponentFailure("camera transform publication failed");
                succeeded = false;
            }
            camera = next;
        }
        return succeeded;
    }

    containers::ArraySpan<const rendering::MeshDrawPhaseContext> RenderingRuntime::GetMeshDrawPhases() const noexcept
    {
        return m_impl != nullptr ? containers::ArraySpan<const rendering::MeshDrawPhaseContext>{m_impl->meshDrawPhases.TypedData(), m_impl->meshDrawPhases.Size()}
                                 : containers::ArraySpan<const rendering::MeshDrawPhaseContext>{};
    }

    bool RenderingRuntime::QueueMeshPreparation(StaticMeshComponent& component) noexcept
    {
        if (!concurrency::IsMainThread() || m_impl == nullptr || !m_impl->scene.IsValid() || component.m_rendering != this)
            return false;
        if (component.m_pending)
            return true;
        if (m_impl->pendingMeshCount == m_config.maximumPendingMeshPreparations)
            return false;
        component.m_previousPending = m_impl->lastPendingMesh;
        component.m_nextPending = nullptr;
        if (m_impl->lastPendingMesh != nullptr)
            m_impl->lastPendingMesh->m_nextPending = &component;
        else
            m_impl->firstPendingMesh = &component;
        m_impl->lastPendingMesh = &component;
        component.m_pending = true;
        ++m_impl->pendingMeshCount;
        return true;
    }

    void RenderingRuntime::CancelMeshPreparation(StaticMeshComponent& component) noexcept
    {
        if (!component.m_pending || component.m_rendering != this || m_impl == nullptr)
            return;
        if (component.m_previousPending != nullptr)
            component.m_previousPending->m_nextPending = component.m_nextPending;
        else
            m_impl->firstPendingMesh = component.m_nextPending;
        if (component.m_nextPending != nullptr)
            component.m_nextPending->m_previousPending = component.m_previousPending;
        else
            m_impl->lastPendingMesh = component.m_previousPending;
        component.m_previousPending = nullptr;
        component.m_nextPending = nullptr;
        component.m_pending = false;
        --m_impl->pendingMeshCount;
    }

    void RenderingRuntime::ReportComponentFailure(const char* const message) noexcept
    {
        if (m_impl != nullptr && !m_impl->admissionFailureClaimed)
        {
            m_impl->firstAdmissionFailure = {rendering::RenderSceneFailureCode::InvalidState, m_impl->scene, {}, message};
            m_impl->admissionFailureClaimed = true;
        }
    }

    rendering::RenderSceneHandle RenderingRuntime::GetScene() const noexcept
    {
        return m_impl != nullptr ? m_impl->scene : rendering::RenderSceneHandle{};
    }

    rendering::RenderSceneManager& RenderingRuntime::GetScenes() noexcept
    {
        return *m_scenes;
    }

    const rendering::RenderSceneManager& RenderingRuntime::GetScenes() const noexcept
    {
        return *m_scenes;
    }

    TransformRuntime* RenderingRuntime::GetTransforms() noexcept
    {
        return m_impl != nullptr ? m_impl->transforms : nullptr;
    }

    const TransformRuntime* RenderingRuntime::GetTransforms() const noexcept
    {
        return m_impl != nullptr ? m_impl->transforms : nullptr;
    }

    bool RenderingRuntime::BindDistantProxyStreaming(const world::WorldFile& world, world::WorldStreamingExecutor& executor) noexcept
    {
        if (!concurrency::IsMainThread() || m_impl != nullptr || m_streamingWorld != nullptr || m_streamingExecutor != nullptr || !world.IsOpen() ||
            !executor.IsInitialized())
            return false;
        m_streamingWorld = &world;
        m_streamingExecutor = &executor;
        return true;
    }

    bool RenderingRuntime::HandleStreamingEvent(const world::StreamingResourceEvent& event) noexcept
    {
        if (!concurrency::IsMainThread() || event.key.kind != world::StreamingNodeKind::DistantProxy || m_impl == nullptr ||
            m_streamingWorld == nullptr || m_streamingExecutor == nullptr)
            return false;
        const world::DistantProxyRecord* const record = m_streamingWorld->FindDistantProxy(event.key.id);
        if (record == nullptr)
            return false;
        const u32 recordIndex = static_cast<u32>(record - m_streamingWorld->GetDistantProxies().Data());
        if (recordIndex >= m_impl->distantProxies.Size())
            return false;
        Impl::DistantProxyInstance& instance = m_impl->distantProxies[recordIndex];

        if (event.type == world::StreamingResourceEventType::ResourceAvailable)
        {
            const resources::ResourceHandle* const resource = m_streamingExecutor->GetResource(event.key);
            if (instance.phase != Impl::DistantProxyPhase::Inactive || resource == nullptr || !resource->IsValid() ||
                resource->GetType() != meshes::MeshResourceType)
            {
                static_cast<void>(m_streamingExecutor->FailResident(event.key, resources::Failure::InternalError));
                ++m_impl->failedDistantProxies;
                return false;
            }

            rendering::MeshProxyDesc desc;
            desc.proxy.producerId = record->proxyId;
            desc.proxy.producerGeneration = 1;
            desc.proxy.spatialMode = rendering::RenderProxySpatialMode::Bounds;
            desc.proxy.visibility = rendering::RenderProxyVisibilityFlags::Visible;
            const f64* const origin = m_streamingWorld->GetOrigin();
            for (u32 axis = 0; axis < 3; ++axis)
            {
                desc.proxy.bounds.minimum[axis] = static_cast<f32>(record->bounds.minimum[axis] - origin[axis]);
                desc.proxy.bounds.maximum[axis] = static_cast<f32>(record->bounds.maximum[axis] - origin[axis]);
                if (!std::isfinite(desc.proxy.bounds.minimum[axis]) || !std::isfinite(desc.proxy.bounds.maximum[axis]))
                {
                    static_cast<void>(m_streamingExecutor->FailResident(event.key, resources::Failure::DeserializationFailure));
                    ++m_impl->failedDistantProxies;
                    return false;
                }
            }
            desc.mesh = record->mesh;
            desc.meshHandle = *resource;
            ProxyAdmissionHandle admission;
            if (!QueueProxyAdmission(desc, {&CompleteDistantProxyAdmission, &instance}, admission))
            {
                static_cast<void>(m_streamingExecutor->FailResident(event.key, resources::Failure::OutOfMemory));
                ++m_impl->failedDistantProxies;
                return false;
            }
            instance.resource = *resource;
            instance.admission = admission;
            instance.phase = Impl::DistantProxyPhase::AdmissionPending;
            return true;
        }

        if (event.type == world::StreamingResourceEventType::ReleaseRequested)
        {
            if (instance.phase == Impl::DistantProxyPhase::AdmissionPending)
            {
                if (!CancelProxyAdmission(instance.admission))
                    return false;
                instance.admission = {};
            }
            else if (instance.phase == Impl::DistantProxyPhase::Resident)
            {
                rendering::RenderSceneFailure failure;
                if (!RetireProxy(instance.proxy, &failure))
                    return false;
                instance.proxy = {};
            }
            else
                return false;

            instance.phase = Impl::DistantProxyPhase::ReleasePending;
            if (!m_streamingExecutor->CompleteRelease(event.key))
                return false;
            instance.resource.Reset();
            instance.phase = Impl::DistantProxyPhase::Inactive;
            ++m_impl->releasedDistantProxies;
            return true;
        }

        if (event.type == world::StreamingResourceEventType::ResourceFailed)
        {
            ++m_impl->failedDistantProxies;
            return instance.phase == Impl::DistantProxyPhase::Inactive;
        }
        return event.type == world::StreamingResourceEventType::RequestCancelled && instance.phase == Impl::DistantProxyPhase::Inactive;
    }

    bool RenderingRuntime::DestroyProxy(const rendering::RenderProxyHandle proxy, rendering::RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !m_impl->scene.IsValid())
            return Fail(failure, rendering::RenderSceneFailureCode::InvalidState, "RenderingRuntime has no active RenderScene", {}, proxy);
        if (proxy.scene != m_impl->scene)
            return Fail(failure, rendering::RenderSceneFailureCode::WrongScene, "RenderProxy belongs to another RenderingRuntime", m_impl->scene, proxy);
        return m_scenes->DestroyProxy(proxy, failure);
    }

    bool RenderingRuntime::QueueProxyAdmission(rendering::RenderProxyDesc desc, const ProxyAdmissionSink& sink,
                                                ProxyAdmissionHandle& admission) noexcept
    {
        if (!concurrency::IsMainThread() || m_impl == nullptr || !m_impl->scene.IsValid() ||
            !m_impl->AllocateAdmission(Impl::AdmissionKind::Proxy, sink, admission))
            return false;
        desc.scene = m_impl->scene;
        ::new (&m_impl->admissionSlots[admission.index].descriptor.proxy) rendering::RenderProxyDesc(desc);
        return true;
    }

    bool RenderingRuntime::QueueProxyAdmission(rendering::MeshProxyDesc desc, const ProxyAdmissionSink& sink,
                                                ProxyAdmissionHandle& admission) noexcept
    {
        if (!concurrency::IsMainThread() || m_impl == nullptr || !m_impl->scene.IsValid() ||
            !m_impl->AllocateAdmission(Impl::AdmissionKind::Mesh, sink, admission))
            return false;
        desc.proxy.scene = m_impl->scene;
        ::new (&m_impl->admissionSlots[admission.index].descriptor.mesh) rendering::MeshProxyDesc(desc);
        return true;
    }

    bool RenderingRuntime::QueueProxyAdmission(rendering::LightProxyDesc desc, const ProxyAdmissionSink& sink,
                                                ProxyAdmissionHandle& admission) noexcept
    {
        if (!concurrency::IsMainThread() || m_impl == nullptr || !m_impl->scene.IsValid() ||
            !m_impl->AllocateAdmission(Impl::AdmissionKind::Light, sink, admission))
            return false;
        desc.proxy.scene = m_impl->scene;
        ::new (&m_impl->admissionSlots[admission.index].descriptor.light) rendering::LightProxyDesc(desc);
        return true;
    }

    bool RenderingRuntime::QueueProxyAdmission(rendering::DecalProxyDesc desc, const ProxyAdmissionSink& sink,
                                                ProxyAdmissionHandle& admission) noexcept
    {
        if (!concurrency::IsMainThread() || m_impl == nullptr || !m_impl->scene.IsValid() ||
            !m_impl->AllocateAdmission(Impl::AdmissionKind::Decal, sink, admission))
            return false;
        desc.proxy.scene = m_impl->scene;
        ::new (&m_impl->admissionSlots[admission.index].descriptor.decal) rendering::DecalProxyDesc(desc);
        return true;
    }

    bool RenderingRuntime::CancelProxyAdmission(const ProxyAdmissionHandle admission) noexcept
    {
        if (!concurrency::IsMainThread() || m_impl == nullptr)
            return false;
        Impl::AdmissionSlot* const slot = m_impl->FindAdmission(admission);
        if (slot == nullptr || slot->state != Impl::AdmissionState::Queued)
            return false;
        m_impl->RemoveQueuedAdmission(*slot);
        m_impl->ReleaseAdmission(admission.index);
        ++m_impl->cancelledProxyAdmissions;
        return true;
    }

    bool RenderingRuntime::RetireProxy(const rendering::RenderProxyHandle proxy, rendering::RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread() || m_impl == nullptr || !m_impl->scene.IsValid())
            return Fail(failure, rendering::RenderSceneFailureCode::InvalidState, "RenderingRuntime has no active RenderScene", {}, proxy);
        if (proxy.scene != m_impl->scene)
            return Fail(failure, rendering::RenderSceneFailureCode::WrongScene, "RenderProxy belongs to another RenderingRuntime", m_impl->scene, proxy);
        if (m_impl->pendingRetirements.Size() == m_config.maximumPendingProxyRetirements)
            return Fail(failure, rendering::RenderSceneFailureCode::CapacityExceeded, "RenderingRuntime proxy retirement capacity exceeded", m_impl->scene,
                        proxy);
        if (!m_scenes->BeginProxyRetirement(proxy, failure))
            return false;
        m_impl->pendingRetirements.PushBack({proxy, m_config.proxyRetirementDelayFrames});
        return true;
    }

    bool RenderingRuntime::GetVisualProxyBinding(const rendering::RenderProxyHandle proxy, const u64 producerGeneration,
                                                  VisualProxyBinding& binding) noexcept
    {
        binding = {};
        if (m_impl == nullptr || proxy.scene != m_impl->scene || !m_scenes->IsProxyAlive(proxy))
            return false;
        binding.scenes = m_scenes;
        binding.proxy = proxy;
        binding.producerGeneration = producerGeneration;
        binding.failures = {&ReportRelinkFailure, this};
        return true;
    }

    bool RenderingRuntime::ConsumeRelinkFailure(VisualRelinkFailure& failure) noexcept
    {
        failure = {};
        if (m_impl == nullptr || !m_impl->relinkFailureClaimed.Exchange(false))
            return false;
        failure = m_impl->firstRelinkFailure;
        m_impl->firstRelinkFailure = {};
        return true;
    }

    bool RenderingRuntime::ConsumeProxyLifecycleFailure(rendering::RenderSceneFailure& failure) noexcept
    {
        failure = {};
        if (m_impl == nullptr)
            return false;
        if (m_impl->admissionFailureClaimed)
        {
            failure = m_impl->firstAdmissionFailure;
            m_impl->firstAdmissionFailure = {};
            m_impl->admissionFailureClaimed = false;
            return true;
        }
        if (!m_impl->retirementFailureClaimed)
            return false;
        failure = m_impl->firstRetirementFailure;
        m_impl->firstRetirementFailure = {};
        m_impl->retirementFailureClaimed = false;
        return true;
    }

    bool RenderingRuntime::ReleaseScene(rendering::RenderSceneFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !m_impl->scene.IsValid())
            return true;
        // Components must cancel their borrowed queue nodes before scene teardown.
        if (m_impl->pendingMeshCount != 0 || m_impl->dirtyCameraCount.GetValue() != 0)
            return Fail(failure, rendering::RenderSceneFailureCode::InvalidState,
                        "RenderingRuntime still has attached mesh preparations", m_impl->scene);
        if (m_impl->relinkFailureClaimed.GetValue())
            return Fail(failure, rendering::RenderSceneFailureCode::InvalidState,
                        "RenderingRuntime has an unconsumed transform-worker relink failure", m_impl->scene);
        while (m_impl->firstQueuedAdmission != ~0u)
        {
            const u32 slotIndex = m_impl->firstQueuedAdmission;
            m_impl->RemoveQueuedAdmission(m_impl->admissionSlots[slotIndex]);
            m_impl->ReleaseAdmission(slotIndex);
            ++m_impl->cancelledProxyAdmissions;
        }
        for (Impl::DistantProxyInstance& instance : m_impl->distantProxies)
        {
            instance.resource.Reset();
            instance.admission = {};
            instance.proxy = {};
            instance.phase = Impl::DistantProxyPhase::Inactive;
        }
        for (u32 index = m_impl->pendingRetirements.Size(); index > 0; --index)
        {
            if (!m_scenes->DestroyProxy(m_impl->pendingRetirements[index - 1u].proxy, failure))
                return false;
            static_cast<void>(m_impl->pendingRetirements.PopBack());
            ++m_impl->retiredProxies;
        }
        if (m_impl->ownsScene && !m_scenes->DestroyScene(m_impl->scene, failure))
            return false;
        m_impl->scene = {};
        m_impl->transforms = nullptr;
        m_impl->ownsScene = false;
        m_impl->admissionFailureClaimed = false;
        m_impl->firstAdmissionFailure = {};
        m_impl->retirementFailureClaimed = false;
        m_impl->firstRetirementFailure = {};
        return true;
    }

    RenderingRuntimeStats RenderingRuntime::GetStats() const noexcept
    {
        RenderingRuntimeStats stats;
        if (m_impl == nullptr)
            return stats;
        stats.scene = m_impl->scene;
        stats.pendingProxyAdmissions = m_impl->queuedAdmissionCount;
        stats.pendingProxyRetirements = m_impl->pendingRetirements.Size();
        stats.pendingMeshPreparations = m_impl->pendingMeshCount;
        stats.pendingCameraTransforms = m_impl->dirtyCameraCount.GetValue();
        stats.admittedProxies = m_impl->admittedProxies;
        stats.cancelledProxyAdmissions = m_impl->cancelledProxyAdmissions;
        stats.failedProxyAdmissions = m_impl->failedProxyAdmissions;
        stats.retiredProxies = m_impl->retiredProxies;
        stats.relinkFailures = m_impl->relinkFailures.GetValue();
        stats.admittedDistantProxies = m_impl->admittedDistantProxies;
        stats.releasedDistantProxies = m_impl->releasedDistantProxies;
        stats.failedDistantProxies = m_impl->failedDistantProxies;
        for (const Impl::DistantProxyInstance& instance : m_impl->distantProxies)
        {
            stats.residentDistantProxies += instance.phase == Impl::DistantProxyPhase::Resident;
            stats.pendingDistantProxies += instance.phase == Impl::DistantProxyPhase::AdmissionPending ||
                                            instance.phase == Impl::DistantProxyPhase::ReleasePending;
        }
        stats.ownsScene = m_impl->ownsScene;
        stats.initialized = m_impl->scene.IsValid();
        rendering::RenderSceneSnapshot snapshot;
        if (m_scenes->GetSnapshot(m_impl->scene, snapshot))
            stats.activeProxies = snapshot.activeProxies;
        return stats;
    }

    bool RenderingRuntime::OnInitialize(game::GameWorld& world) noexcept
    {
        const bool hasStreamingBinding = m_streamingWorld != nullptr && m_streamingExecutor != nullptr;
        if (m_impl != nullptr || m_scenes == nullptr || !m_scenes->IsInitialized() ||
            ((m_streamingWorld == nullptr) != (m_streamingExecutor == nullptr)) ||
            (hasStreamingBinding && (!m_streamingWorld->IsOpen() || !m_streamingExecutor->IsInitialized())))
            return false;
        auto* const transforms = static_cast<TransformRuntime*>(world.GetSystem(TransformRuntimeSystemId));
        if (transforms == nullptr)
            return false;
        if (m_config.maximumPendingProxyAdmissions == 0 || m_config.maximumProxyAdmissionsPerFrame == 0 ||
            m_config.maximumProxyAdmissionsPerFrame > m_config.maximumPendingProxyAdmissions || m_config.maximumPendingProxyRetirements == 0 ||
            m_config.maximumPendingCameraTransforms == 0 || m_config.maximumPendingMeshPreparations == 0 || m_config.maximumMeshPreparationsPerFrame == 0 ||
            m_config.maximumMeshPreparationsPerFrame > m_config.maximumPendingMeshPreparations ||
            (!m_config.meshDrawPhases.Empty() && (m_config.meshDrawPhases.Data() == nullptr || m_meshResidency == nullptr)))
            return false;
        if (m_config.meshDrawPhases.Count() > rendering::MaximumRenderPhases)
            return false;
        for (u32 index = 0; index < m_config.meshDrawPhases.Count(); ++index)
        {
            const auto phase = m_config.meshDrawPhases[index].phase;
            if (!phase.IsValid() || (m_renderPhases != nullptr && !m_renderPhases->Find(phase).IsValid()))
                return false;
            for (u32 previous = 0; previous < index; ++previous)
                if (m_config.meshDrawPhases[previous].phase == phase)
                    return false;
        }
        m_impl = VANGUARD_NEW(Impl)(m_config);
        if (m_impl == nullptr || !m_impl->HasAdmissionStorage() || m_impl->meshDrawPhases.Size() != m_config.meshDrawPhases.Count())
        {
            if (m_impl != nullptr)
                VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return false;
        }
        m_impl->transforms = transforms;
        m_impl->distantProxies.Resize(hasStreamingBinding ? m_streamingWorld->GetDistantProxies().Size() : 0u);
        for (u32 index = 0; index < m_impl->distantProxies.Size(); ++index)
        {
            m_impl->distantProxies[index].owner = this;
            m_impl->distantProxies[index].recordIndex = index;
        }
        return true;
    }

    bool RenderingRuntime::OnSetup(game::GameWorld& world) noexcept
    {
        if (m_impl == nullptr || m_impl->scene.IsValid())
            return false;
        if (m_config.existingScene.IsValid())
        {
            rendering::RenderSceneSnapshot snapshot;
            if (!m_scenes->GetSnapshot(m_config.existingScene, snapshot))
                return false;
            m_impl->scene = m_config.existingScene;
            m_impl->ownsScene = false;
            return true;
        }

        rendering::RenderSceneDesc desc = m_config.scene;
        desc.mode = SceneMode(world.Mode());
        desc.ownership = rendering::RenderSceneOwnership::Service;
        rendering::RenderSceneFailure failure;
        if (!m_scenes->CreateScene(desc, m_impl->scene, &failure))
            return false;
        m_impl->ownsScene = true;
        return true;
    }

    void RenderingRuntime::OnUninitialize(game::GameWorld&) noexcept
    {
        static_cast<void>(ReleaseScene());
    }

    void RenderingRuntime::OnBeginFrame(game::GameWorld&, const f32) noexcept
    {
        if (m_impl == nullptr || !m_impl->scene.IsValid())
            return;

        const u32 meshCount = m_impl->pendingMeshCount < m_config.maximumMeshPreparationsPerFrame
                                  ? m_impl->pendingMeshCount : m_config.maximumMeshPreparationsPerFrame;
        for (u32 index = 0; index < meshCount; ++index)
        {
            StaticMeshComponent& component = *m_impl->firstPendingMesh;
            CancelMeshPreparation(component);
            if (component.Progress() && !QueueMeshPreparation(component))
                component.Fail("static mesh preparation could not be requeued");
        }

        m_impl->processingAdmissions.Clear();
        const u32 admissionCount = m_impl->queuedAdmissionCount < m_config.maximumProxyAdmissionsPerFrame
                                       ? m_impl->queuedAdmissionCount
                                       : m_config.maximumProxyAdmissionsPerFrame;
        for (u32 index = 0; index < admissionCount; ++index)
        {
            const u32 slotIndex = m_impl->firstQueuedAdmission;
            Impl::AdmissionSlot& slot = m_impl->admissionSlots[slotIndex];
            m_impl->RemoveQueuedAdmission(slot);
            slot.state = Impl::AdmissionState::Processing;
            m_impl->processingAdmissions.PushBack(slotIndex);
        }

        for (const u32 slotIndex : m_impl->processingAdmissions)
        {
            Impl::AdmissionSlot& slot = m_impl->admissionSlots[slotIndex];
            const ProxyAdmissionHandle admission{slotIndex, slot.generation};
            rendering::RenderProxyHandle proxy;
            rendering::RenderSceneFailure failure;
            bool succeeded = false;
            switch (slot.kind)
            {
            case Impl::AdmissionKind::Proxy:
                succeeded = m_scenes->CreateProxy(slot.descriptor.proxy, proxy, &failure);
                break;
            case Impl::AdmissionKind::Mesh:
                succeeded = m_scenes->CreateMeshProxy(slot.descriptor.mesh, proxy, &failure);
                break;
            case Impl::AdmissionKind::Light:
                succeeded = m_scenes->CreateLightProxy(slot.descriptor.light, proxy, &failure);
                break;
            case Impl::AdmissionKind::Decal:
                succeeded = m_scenes->CreateDecalProxy(slot.descriptor.decal, proxy, &failure);
                break;
            }

            if (succeeded)
            {
                ++m_impl->admittedProxies;
                if (!slot.sink.complete(admission, proxy, nullptr, slot.sink.userData))
                {
                    rendering::RenderSceneFailure rejectionFailure;
                    if (!m_scenes->DestroyProxy(proxy, &rejectionFailure) && !m_impl->retirementFailureClaimed)
                    {
                        m_impl->firstRetirementFailure = rejectionFailure;
                        m_impl->retirementFailureClaimed = true;
                    }
                }
            }
            else
            {
                ++m_impl->failedProxyAdmissions;
                if (!m_impl->admissionFailureClaimed)
                {
                    m_impl->firstAdmissionFailure = failure;
                    m_impl->admissionFailureClaimed = true;
                }
                static_cast<void>(slot.sink.complete(admission, {}, &failure, slot.sink.userData));
            }
            m_impl->ReleaseAdmission(slotIndex);
        }

        for (u32 index = m_impl->pendingRetirements.Size(); index > 0; --index)
        {
            Impl::PendingRetirement& retirement = m_impl->pendingRetirements[index - 1u];
            if (retirement.remainingFrames != 0)
            {
                --retirement.remainingFrames;
                if (retirement.remainingFrames != 0)
                    continue;
            }
            rendering::RenderSceneFailure failure;
            if (!m_scenes->DestroyProxy(retirement.proxy, &failure))
            {
                if (!m_impl->retirementFailureClaimed)
                {
                    m_impl->firstRetirementFailure = failure;
                    m_impl->retirementFailureClaimed = true;
                }
                continue;
            }
            // Reverse traversal has already visited the last entry this frame.
            // Preserve its countdown without shifting every later failed entry.
            const u32 last = m_impl->pendingRetirements.Size() - 1u;
            if (index - 1u != last)
                m_impl->pendingRetirements[index - 1u] = m_impl->pendingRetirements[last];
            static_cast<void>(m_impl->pendingRetirements.PopBack());
            ++m_impl->retiredProxies;
        }
    }

    const char* RenderingRuntime::ReadinessBlocker() const noexcept
    {
        if (m_impl == nullptr || !m_impl->scene.IsValid())
            return "world RenderingRuntime has no RenderScene";
        if (m_impl->admissionFailureClaimed)
            return "world RenderingRuntime has a failed proxy admission";
        if (m_impl->pendingMeshCount != 0)
            return "world RenderingRuntime has pending mesh preparation";
        if (m_impl->queuedAdmissionCount != 0)
            return "world RenderingRuntime has pending proxies to admit";
        rendering::RenderSceneSnapshot sceneSnapshot;
        if (m_scenes != nullptr && m_scenes->GetSnapshot(m_impl->scene, sceneSnapshot) && sceneSnapshot.pendingMeshBindings != 0)
            return "world RenderingRuntime has pending mesh binding acceptance";
        if (m_impl->retirementFailureClaimed)
            return "world RenderingRuntime has a failed proxy retirement";
        if (m_impl->relinkFailureClaimed.GetValue())
            return "world RenderingRuntime has a transform relink failure";
        return m_scenes->GetRenderingBlockReason(m_impl->scene);
    }

    bool RenderingRuntime::CompleteDistantProxyAdmission(const ProxyAdmissionHandle admission, const rendering::RenderProxyHandle proxy,
                                                          const rendering::RenderSceneFailure* const failure, void* const userData) noexcept
    {
        auto* const instance = static_cast<Impl::DistantProxyInstance*>(userData);
        if (instance == nullptr || instance->owner == nullptr || instance->owner->m_impl == nullptr ||
            instance->phase != Impl::DistantProxyPhase::AdmissionPending || instance->admission != admission)
            return false;
        RenderingRuntime& runtime = *instance->owner;
        const world::DistantProxyRecord& record = runtime.m_streamingWorld->GetDistantProxies()[instance->recordIndex];
        const world::StreamingNodeKey key{record.proxyId, world::StreamingNodeKind::DistantProxy};
        instance->admission = {};
        if (failure != nullptr)
        {
            instance->resource.Reset();
            instance->phase = Impl::DistantProxyPhase::Inactive;
            ++runtime.m_impl->failedDistantProxies;
            static_cast<void>(runtime.m_streamingExecutor->FailResident(key, resources::Failure::InternalError));
            return true;
        }
        if (!runtime.m_streamingExecutor->SetReady(key, true))
        {
            instance->resource.Reset();
            instance->phase = Impl::DistantProxyPhase::Inactive;
            ++runtime.m_impl->failedDistantProxies;
            static_cast<void>(runtime.m_streamingExecutor->FailResident(key, resources::Failure::InternalError));
            return false;
        }
        instance->proxy = proxy;
        instance->phase = Impl::DistantProxyPhase::Resident;
        ++runtime.m_impl->admittedDistantProxies;
        return true;
    }

    void RenderingRuntime::ReportRelinkFailure(const VisualRelinkFailure& failure, void* const userData) noexcept
    {
        auto* const runtime = static_cast<RenderingRuntime*>(userData);
        if (runtime == nullptr || runtime->m_impl == nullptr)
            return;
        static_cast<void>(runtime->m_impl->relinkFailures.Increment());
        if (!runtime->m_impl->relinkFailureClaimed.CompareExchange(true, false))
            runtime->m_impl->firstRelinkFailure = failure;
    }
} // namespace vanguard::entities

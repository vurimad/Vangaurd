#include <vanguard/engine/streaming_observer_service.hpp>

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/engine/game_world_service.hpp>
#include <vanguard/engine/world_service.hpp>
#include <vanguard/memory/memory.hpp>

#include <cmath>
#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;

    [[nodiscard]] bool IsFinitePosition(const vanguard::f64 position[3]) noexcept
    {
        return std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]);
    }

    [[nodiscard]] bool IsValidPredictionConfig(const engine::StreamingObserverPredictionConfig& config) noexcept
    {
        return std::isfinite(config.maximumOnFootSpeed) && config.maximumOnFootSpeed >= 0.0f &&
               std::isfinite(config.maximumGroundVehicleSpeed) && config.maximumGroundVehicleSpeed >= 0.0f &&
               std::isfinite(config.maximumAirVehicleSpeed) && config.maximumAirVehicleSpeed >= 0.0f;
    }

    [[nodiscard]] bool CopyName(const char* const source, char* const destination) noexcept
    {
        if (source == nullptr || source[0] == '\0') return false;
        vanguard::u32 length = 0;
        while (source[length] != '\0' && length < engine::MaximumStreamingObserverNameBytes - 1u)
        {
            destination[length] = source[length];
            ++length;
        }
        if (source[length] != '\0') return false;
        destination[length] = '\0';
        return true;
    }

    class ManagedStreamingObserverService final : public engine::StreamingObserverService
    {
    public:
        [[nodiscard]] bool ConfigurePrediction(
            const engine::StreamingObserverPredictionConfig& config) noexcept override
        {
            if (!IsValidPredictionConfig(config)) return false;
            m_lock.Acquire();
            m_prediction = config;
            m_lock.Release();
            return true;
        }

        [[nodiscard]] bool RegisterObserver(const engine::StreamingObserverDescriptor& descriptor,
                                            engine::StreamingObserverHandle& observer) noexcept override
        {
            observer = {};
            if (descriptor.velocityClass > engine::StreamingObserverVelocityClass::Unbounded ||
                !std::isfinite(descriptor.predictionSeconds) || descriptor.predictionSeconds < 0.0f)
                return false;

            char name[engine::MaximumStreamingObserverNameBytes]{};
            if (!CopyName(descriptor.name, name)) return false;

            m_lock.Acquire();
            for (vanguard::u32 index = 0; index < vanguard::world::MaximumStreamingObservers; ++index)
            {
                Slot& slot = m_slots[index];
                if (slot.registered) continue;
                slot.registered = true;
                slot.enabled = descriptor.enabled;
                slot.positionValid = false;
                slot.velocityClass = descriptor.velocityClass;
                slot.predictionSeconds = descriptor.predictionSeconds;
                for (vanguard::u32 character = 0; character < engine::MaximumStreamingObserverNameBytes; ++character)
                    slot.name[character] = name[character];
                ++m_registeredObservers;
                observer = {index, slot.generation};
                m_lock.Release();
                return true;
            }
            m_lock.Release();
            return false;
        }

        [[nodiscard]] bool UnregisterObserver(const engine::StreamingObserverHandle observer) noexcept override
        {
            m_lock.Acquire();
            Slot* const slot = FindSlot(observer);
            if (slot == nullptr)
            {
                m_lock.Release();
                return false;
            }
            slot->registered = false;
            slot->enabled = false;
            slot->positionValid = false;
            slot->name[0] = '\0';
            ++slot->generation;
            if (slot->generation == 0) slot->generation = 1;
            if (m_primaryObserver == observer) m_primaryObserver = {};
            --m_registeredObservers;
            m_lock.Release();
            return true;
        }

        [[nodiscard]] bool UpdateObserver(const engine::StreamingObserverHandle observer,
                                          const engine::StreamingObserverUpdate& update) noexcept override
        {
            if ((update.positionValid && !IsFinitePosition(update.position)) || !IsFinitePosition(update.velocity))
                return RejectUpdate();
            m_lock.Acquire();
            Slot* const slot = FindSlot(observer);
            if (slot == nullptr)
            {
                ++m_rejectedUpdates;
                m_lock.Release();
                return false;
            }
            for (vanguard::u32 axis = 0; axis < 3; ++axis)
            {
                slot->position[axis] = update.position[axis];
                slot->velocity[axis] = update.velocity[axis];
            }
            slot->positionValid = update.positionValid;
            m_lock.Release();
            return true;
        }

        [[nodiscard]] bool SetObserverEnabled(const engine::StreamingObserverHandle observer,
                                              const bool enabled) noexcept override
        {
            m_lock.Acquire();
            Slot* const slot = FindSlot(observer);
            if (slot == nullptr)
            {
                m_lock.Release();
                return false;
            }
            slot->enabled = enabled;
            m_lock.Release();
            return true;
        }

        [[nodiscard]] bool SetPrimaryObserver(const engine::StreamingObserverHandle observer) noexcept override
        {
            m_lock.Acquire();
            if (FindSlot(observer) == nullptr)
            {
                m_lock.Release();
                return false;
            }
            m_primaryObserver = observer;
            m_lock.Release();
            return true;
        }

        void ClearPrimaryObserver() noexcept override
        {
            m_lock.Acquire();
            m_primaryObserver = {};
            m_lock.Release();
        }

        [[nodiscard]] bool SetGlobalDistanceScale(const vanguard::f32 scale) noexcept override
        {
            if (!std::isfinite(scale) || scale <= 0.0f) return false;
            m_lock.Acquire();
            m_globalDistanceScale = scale;
            m_lock.Release();
            return true;
        }

        [[nodiscard]] bool Snapshot(engine::StreamingObserverSnapshot& snapshot) const noexcept override
        {
            m_lock.AcquireShared();
            snapshot = m_snapshot;
            m_lock.ReleaseShared();
            return snapshot.sequence != 0;
        }

        [[nodiscard]] engine::StreamingObserverServiceStats GetStats() const noexcept override
        {
            m_lock.AcquireShared();
            const engine::StreamingObserverServiceStats stats{
                m_registeredObservers, m_validObservers, m_submittedSnapshots, m_rejectedUpdates,
                m_snapshot.sequence, m_snapshot.usingWorldOriginFallback};
            m_lock.ReleaseShared();
            return stats;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_framePipeline = engine::FindFramePipelineService(context);
            m_gameWorld = engine::FindGameWorldService(context);
            m_world = engine::FindWorldService(context);
            if (m_framePipeline == nullptr || m_gameWorld == nullptr || m_world == nullptr)
                return app::LifecycleStatus::Failure("Streaming Observer dependencies are unavailable");

            engine::FrameParticipantDescriptor descriptor;
            descriptor.id = engine::StreamingObserverFrameParticipantId;
            descriptor.name = "streamingObservers";
            descriptor.phase = engine::FramePhase::PreSimulation;
            descriptor.profiles = app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            engine::FrameFailure failure;
            if (!m_framePipeline->RegisterParticipant(descriptor, &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message
                                                                               : "Streaming Observer frame registration failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            m_lock.AcquireShared();
            const bool empty = m_registeredObservers == 0;
            m_lock.ReleaseShared();
            return empty ? app::LifecycleStatus::Success()
                         : app::LifecycleStatus::Failure("Streaming observers must be unregistered before shutdown");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_registeredObservers != 0)
                return app::LifecycleStatus::Failure("Streaming observer registrations remain live during shutdown");
            m_framePipeline = nullptr;
            m_gameWorld = nullptr;
            m_world = nullptr;
            m_snapshot = {};
            return app::LifecycleStatus::Success();
        }

    private:
        struct Slot
        {
            vanguard::f64 position[3]{};
            vanguard::f64 velocity[3]{};
            char name[engine::MaximumStreamingObserverNameBytes]{};
            vanguard::f32 predictionSeconds = 1.0f;
            vanguard::u32 generation = 1;
            engine::StreamingObserverVelocityClass velocityClass = engine::StreamingObserverVelocityClass::OnFoot;
            bool registered = false;
            bool enabled = false;
            bool positionValid = false;
        };

        [[nodiscard]] Slot* FindSlot(const engine::StreamingObserverHandle observer) noexcept
        {
            if (!observer.IsValid()) return nullptr;
            Slot& slot = m_slots[observer.index];
            return slot.registered && slot.generation == observer.generation ? &slot : nullptr;
        }

        [[nodiscard]] bool RejectUpdate() noexcept
        {
            m_lock.Acquire();
            ++m_rejectedUpdates;
            m_lock.Release();
            return false;
        }

        [[nodiscard]] vanguard::f64 MaximumSpeed(const Slot& slot) const noexcept
        {
            switch (slot.velocityClass)
            {
            case engine::StreamingObserverVelocityClass::OnFoot: return m_prediction.maximumOnFootSpeed;
            case engine::StreamingObserverVelocityClass::GroundVehicle: return m_prediction.maximumGroundVehicleSpeed;
            case engine::StreamingObserverVelocityClass::AirVehicle: return m_prediction.maximumAirVehicleSpeed;
            case engine::StreamingObserverVelocityClass::Unbounded: return -1.0;
            }
            return 0.0;
        }

        void BuildSnapshot() noexcept
        {
            vanguard::f64 worldOrigin[3]{};
            bool hasWorldOrigin = false;
            if (m_world != nullptr)
            {
                const vanguard::world::WorldResource* const resource = m_world->Resource();
                if (resource != nullptr)
                {
                    for (vanguard::u32 axis = 0; axis < 3; ++axis) worldOrigin[axis] = resource->File().Origin()[axis];
                    hasWorldOrigin = true;
                }
            }

            engine::StreamingObserverSnapshot snapshot;
            m_lock.Acquire();
            const Slot* camera = FindSlot(m_primaryObserver);
            if (camera == nullptr || !camera->enabled || !camera->positionValid) camera = nullptr;
            const Slot* first = nullptr;
            for (const Slot& slot : m_slots)
            {
                if (!slot.registered || !slot.enabled || !slot.positionValid) continue;
                if (first == nullptr) first = &slot;
                vanguard::world::StreamingObserver& output = snapshot.observers[snapshot.observerCount++];
                const vanguard::f64 speedSquared = slot.velocity[0] * slot.velocity[0] + slot.velocity[1] * slot.velocity[1] +
                                         slot.velocity[2] * slot.velocity[2];
                vanguard::f64 velocityScale = m_prediction.enabled ? slot.predictionSeconds : 0.0;
                const vanguard::f64 maximumSpeed = MaximumSpeed(slot);
                if (velocityScale != 0.0 && maximumSpeed >= 0.0 && speedSquared > maximumSpeed * maximumSpeed)
                {
                    const vanguard::f64 speed = std::sqrt(speedSquared);
                    velocityScale *= speed > 0.0 ? maximumSpeed / speed : 0.0;
                }
                for (vanguard::u32 axis = 0; axis < 3; ++axis)
                    output.predictedPosition[axis] = slot.position[axis] + slot.velocity[axis] * velocityScale;
            }

            const Slot* const cameraSource = camera != nullptr ? camera : first;
            if (cameraSource != nullptr)
            {
                for (vanguard::u32 axis = 0; axis < 3; ++axis) snapshot.cameraPosition[axis] = cameraSource->position[axis];
            }
            else if (hasWorldOrigin)
            {
                snapshot.observerCount = 1;
                snapshot.usingWorldOriginFallback = true;
                for (vanguard::u32 axis = 0; axis < 3; ++axis)
                {
                    snapshot.observers[0].predictedPosition[axis] = worldOrigin[axis];
                    snapshot.cameraPosition[axis] = worldOrigin[axis];
                }
            }
            snapshot.globalDistanceScale = m_globalDistanceScale;
            snapshot.sequence = m_snapshot.sequence + 1u;
            m_validObservers = cameraSource != nullptr ? snapshot.observerCount : 0;
            m_snapshot = snapshot;
            m_lock.Release();
        }

        [[nodiscard]] static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext&,
                                                                          void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedStreamingObserverService*>(userData);
            if (service == nullptr || service->m_gameWorld == nullptr)
                return engine::FrameParticipantStatus::Failure("Streaming Observer service is unavailable");
            service->BuildSnapshot();
            if (service->m_gameWorld->Status() != engine::GameWorldStatus::Running)
                return engine::FrameParticipantStatus::Success();

            engine::StreamingObserverSnapshot snapshot;
            if (!service->Snapshot(snapshot) || snapshot.observerCount == 0)
                return engine::FrameParticipantStatus::Failure("Streaming Observer snapshot has no valid world position");
            vanguard::world::StreamingProcessInput input;
            input.observers = {snapshot.observers, snapshot.observerCount};
            for (vanguard::u32 axis = 0; axis < 3; ++axis) input.cameraPosition[axis] = snapshot.cameraPosition[axis];
            input.globalDistanceScale = snapshot.globalDistanceScale;
            if (!service->m_gameWorld->SetStreamingInput(input))
                return engine::FrameParticipantStatus::Failure("Game World rejected the streaming observer snapshot");
            service->m_lock.Acquire();
            ++service->m_submittedSnapshots;
            service->m_lock.Release();
            return engine::FrameParticipantStatus::Success();
        }

        mutable vanguard::concurrency::RWLock m_lock;
        Slot m_slots[vanguard::world::MaximumStreamingObservers]{};
        engine::StreamingObserverPredictionConfig m_prediction;
        engine::StreamingObserverSnapshot m_snapshot;
        engine::StreamingObserverHandle m_primaryObserver;
        engine::FramePipelineService* m_framePipeline = nullptr;
        engine::GameWorldService* m_gameWorld = nullptr;
        engine::WorldService* m_world = nullptr;
        vanguard::f32 m_globalDistanceScale = 1.0f;
        vanguard::u32 m_registeredObservers = 0;
        vanguard::u32 m_validObservers = 0;
        vanguard::u64 m_submittedSnapshots = 0;
        vanguard::u64 m_rejectedUpdates = 0;
    };

    app::Service* CreateStreamingObserverService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::World, sizeof(ManagedStreamingObserverService),
            alignof(ManagedStreamingObserverService));
        return block ? ::new (block.address) ManagedStreamingObserverService() : nullptr;
    }

    void DestroyStreamingObserverService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedStreamingObserverService*>(service)->~ManagedStreamingObserverService();
        vanguard::memory::MemoryBlock block{
            service, sizeof(ManagedStreamingObserverService), vanguard::memory::PoolId::World};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterStreamingObserverService(application::EngineHost& host,
                                          application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {FramePipelineServiceId, application::DependencyKind::Required},
            {WorldServiceId, application::DependencyKind::Required},
            {GameWorldServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{StreamingObserverCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = StreamingObserverServiceId;
        descriptor.name = "streamingObservers";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor;
        descriptor.scope = application::ServiceScope::Engine;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 3};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateStreamingObserverService;
        descriptor.destroy = DestroyStreamingObserverService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    StreamingObserverService* FindStreamingObserverService(application::EngineHost& host) noexcept
    {
        return static_cast<StreamingObserverService*>(host.FindCapability(StreamingObserverCapabilityId));
    }

    StreamingObserverService* FindStreamingObserverService(application::ServiceContext& context) noexcept
    {
        return static_cast<StreamingObserverService*>(context.FindCapability(StreamingObserverCapabilityId));
    }
} // namespace vanguard::engine

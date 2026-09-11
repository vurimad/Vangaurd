#include <vanguard/engine/world_session_service.hpp>

#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/game_input_service.hpp>
#include <vanguard/engine/world_service.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/world/worlds.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace resources = vanguard::resources;

    class ManagedWorldSessionService final : public engine::WorldSessionService
    {
    public:
        [[nodiscard]] bool Begin(const engine::WorldSessionStartRequest& request, engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_stopRequested || m_status != engine::WorldSessionStatus::Idle)
            {
                // Rejected commands must not corrupt an already running session.
                if (failure != nullptr)
                    *failure = {engine::WorldSessionFailureCode::InvalidState, resources::Failure::None,
                                "world session cannot begin in its current state"};
                return false;
            }
            if (!request.world.IsValid() || request.world.ExpectedType() != vanguard::world::WorldResourceType)
                return Fail(failure, engine::WorldSessionFailureCode::InvalidRequest, "world session requires an explicit typed world resource");
            if (request.inputMode != engine::WorldSessionInputMode::None && request.inputMode != engine::WorldSessionInputMode::Mapping)
                return Fail(failure, engine::WorldSessionFailureCode::InvalidRequest, "world session requires an explicit input policy");
            if (request.inputMode == engine::WorldSessionInputMode::Mapping &&
                (!request.input.IsValid() || request.input.ExpectedType() != vanguard::game_input::MappingResourceType))
                return Fail(failure, engine::WorldSessionFailureCode::InvalidRequest, "world session requires a typed input mapping");
            if (request.inputMode == engine::WorldSessionInputMode::None && request.input.IsValid())
                return Fail(failure, engine::WorldSessionFailureCode::InvalidRequest, "no-input session cannot specify an input resource");

            // The prior session is fully detached before replacing its map. This
            // also prevents edit-only worlds from inheriting gameplay contexts.
            const bool inputCleared = m_gameInput->Clear();
            if (!inputCleared)
                return Fail(failure, engine::WorldSessionFailureCode::InputClearFailure, "world session input clearing failed");
            m_ownsInput = true;
            m_inputInstalled = request.inputMode == engine::WorldSessionInputMode::None;
            if (request.inputMode == engine::WorldSessionInputMode::Mapping)
            {
                m_inputRequest = m_streaming->GetStreamer().Request(request.input, resources::LoadPriority::Critical);
                if (!m_inputRequest.IsValid())
                    return Fail(failure, engine::WorldSessionFailureCode::InputRequestFailure, "world session default input request was rejected");
            }

            const bool worldRequested = m_world->BeginWorld(request.world);
            if (!worldRequested)
            {
                static_cast<void>(m_inputRequest.Cancel());
                m_inputRequest.Reset();
                return Fail(failure, engine::WorldSessionFailureCode::WorldRequestFailure, "world session resource request was rejected");
            }

            m_status = engine::WorldSessionStatus::LoadingWorld;
            m_lastFailure = {};
            return true;
        }

        [[nodiscard]] engine::WorldSessionStatus Poll(engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_stopRequested)
                return PollStop(failure);
            if (m_status != engine::WorldSessionStatus::LoadingWorld)
                return m_status;

            const engine::WorldResourceStatus worldStatus = m_world->PollWorld();
            if (worldStatus == engine::WorldResourceStatus::Loading)
                return m_status;
            if (worldStatus == engine::WorldResourceStatus::Failed)
            {
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::WorldLoadFailure, "world session resource loading failed",
                                       m_world->GetLastFailure()));
                return m_status;
            }
            if (worldStatus != engine::WorldResourceStatus::Ready)
            {
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::WorldLoadFailure, "world session resource entered an invalid state"));
                return m_status;
            }
            if (!m_inputInstalled)
            {
                if (!m_inputRequest.HasFinished())
                    return m_status;
                if (!m_inputRequest.HasLoaded())
                {
                    const resources::Failure inputFailure = m_inputRequest.GetError();
                    m_inputRequest.Reset();
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputLoadFailure, "world session default input loading failed",
                                           inputFailure));
                    return m_status;
                }
                resources::ResourceHandle input = m_inputRequest.Acquire();
                m_inputRequest.Reset();
                if (!input.IsValid() || input.Get()->GetType() != vanguard::game_input::MappingResourceType)
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputLoadFailure,
                                           "world session input resource has an invalid type",
                                           resources::Failure::InternalError));
                    return m_status;
                }
                const auto* const mapping = static_cast<const vanguard::game_input::MappingResource*>(input.Get());
                const vanguard::game_input::MappingResult installed = m_gameInput->Install(mapping->GetFile());
                if (installed != vanguard::game_input::MappingResult::Success)
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputInstallFailure, "world session default input installation failed"));
                    return m_status;
                }
                m_inputInstalled = true;
            }
            const bool worldStarted = m_gameWorld->BeginWorld();
            if (!worldStarted)
            {
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::GameWorldStartFailure, "world session game-world initialization failed"));
                return m_status;
            }

            m_status = engine::WorldSessionStatus::Running;
            m_lastFailure = {};
            return m_status;
        }

        [[nodiscard]] bool RequestStop(engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_stopRequested)
                return true;
            if (m_status == engine::WorldSessionStatus::Idle)
                return true;

            m_stopRequested = true;
            m_status = engine::WorldSessionStatus::Stopping;
            return true;
        }

        [[nodiscard]] engine::WorldSessionStatus GetStatus() const noexcept override
        {
            return m_status;
        }
        [[nodiscard]] const engine::WorldSessionFailure& GetLastFailure() const noexcept override
        {
            return m_lastFailure;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_streaming = engine::FindResourceStreamingService(context);
            m_world = engine::FindWorldService(context);
            m_gameWorld = engine::FindGameWorldService(context);
            m_gameInput = engine::FindGameInputService(context);
            if (m_streaming == nullptr || m_world == nullptr || m_gameWorld == nullptr || m_gameInput == nullptr)
                return app::LifecycleStatus::Failure("World Session dependencies are not running");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            return m_status == engine::WorldSessionStatus::Idle && !m_stopRequested
                       ? app::LifecycleStatus::Success()
                       : app::LifecycleStatus::Failure("World Session must release active state before engine shutdown");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_status != engine::WorldSessionStatus::Idle || m_stopRequested || m_ownsInput || m_inputRequest.IsValid())
                return app::LifecycleStatus::Failure("World Session state remains live during shutdown");
            m_streaming = nullptr;
            m_world = nullptr;
            m_gameWorld = nullptr;
            m_gameInput = nullptr;
            m_lastFailure = {};
            return app::LifecycleStatus::Success();
        }

    private:
        [[nodiscard]] engine::WorldSessionStatus PollStop(engine::WorldSessionFailure* const failure) noexcept
        {
            if (m_inputRequest.IsValid())
            {
                static_cast<void>(m_inputRequest.Cancel());
                m_inputRequest.Reset();
            }
            const engine::GameWorldStopResult gameWorldResult = m_gameWorld->StopWorld();
            if (gameWorldResult == engine::GameWorldStopResult::Pending)
                return m_status;
            if (gameWorldResult == engine::GameWorldStopResult::Failure)
            {
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::GameWorldStopFailure, "world session game-world drain failed"));
                return m_status;
            }
            const bool worldReleased = m_world->ReleaseWorld();
            if (!worldReleased)
                return m_status;

            if (m_ownsInput)
            {
                const bool inputCleared = m_gameInput->Clear();
                if (!inputCleared)
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputClearFailure, "world session input clearing failed"));
                    return m_status;
                }
                m_ownsInput = false;
            }

            m_stopRequested = false;
            m_inputInstalled = false;
            m_status = engine::WorldSessionStatus::Idle;
            m_lastFailure = {};
            return m_status;
        }

        void ClearOutput(engine::WorldSessionFailure* const failure) const noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(engine::WorldSessionFailure* const output, const engine::WorldSessionFailureCode code, const char* const message,
                                const resources::Failure resourceFailure = resources::Failure::None) noexcept
        {
            m_status = engine::WorldSessionStatus::Failed;
            m_lastFailure = {code, resourceFailure, message};
            if (output != nullptr)
                *output = m_lastFailure;
            return false;
        }

        engine::ResourceStreamingService* m_streaming = nullptr;
        engine::WorldService* m_world = nullptr;
        engine::GameWorldService* m_gameWorld = nullptr;
        engine::GameInputService* m_gameInput = nullptr;
        resources::PipelineRequest m_inputRequest;
        engine::WorldSessionFailure m_lastFailure;
        engine::WorldSessionStatus m_status = engine::WorldSessionStatus::Idle;
        bool m_stopRequested = false;
        bool m_inputInstalled = false;
        bool m_ownsInput = false;
    };

    app::Service* CreateWorldSessionService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::World, sizeof(ManagedWorldSessionService), alignof(ManagedWorldSessionService));
        return block ? ::new (block.address) ManagedWorldSessionService() : nullptr;
    }

    void DestroyWorldSessionService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedWorldSessionService*>(service)->~ManagedWorldSessionService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedWorldSessionService), vanguard::memory::PoolId::World};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::engine
{
    bool RegisterWorldSessionService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{{ResourceStreamingServiceId, application::DependencyKind::Required},
                                                                {WorldServiceId, application::DependencyKind::Required},
                                                                {GameWorldServiceId, application::DependencyKind::Required},
                                                                {GameInputServiceId, application::DependencyKind::Required},
                                                                {StreamingObserverServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId providedCapabilities[]{WorldSessionCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = WorldSessionServiceId;
        descriptor.name = "worldSession";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor;
        descriptor.scope = application::ServiceScope::Engine;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 5};
        descriptor.provides = {providedCapabilities, 1};
        descriptor.create = CreateWorldSessionService;
        descriptor.destroy = DestroyWorldSessionService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    WorldSessionService* FindWorldSessionService(application::EngineHost& host) noexcept
    {
        return static_cast<WorldSessionService*>(host.FindCapability(WorldSessionCapabilityId));
    }

    WorldSessionService* FindWorldSessionService(application::ServiceContext& context) noexcept
    {
        return static_cast<WorldSessionService*>(context.FindCapability(WorldSessionCapabilityId));
    }
} // namespace vanguard::engine

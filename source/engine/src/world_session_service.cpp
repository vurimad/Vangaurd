#include <vanguard/engine/world_session_service.hpp>

#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/game_input_service.hpp>
#include <vanguard/engine/world_service.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace resources = vanguard::resources;
    namespace streaming = vanguard::streaming;

    class ManagedWorldSessionService final : public engine::WorldSessionService
    {
    public:
        [[nodiscard]] bool Begin(const engine::WorldSessionStartRequest& request, engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_stopRequested || (m_status != engine::WorldSessionStatus::Idle && m_status != engine::WorldSessionStatus::Mounted))
                return Fail(failure, engine::WorldSessionFailureCode::InvalidState, "world session cannot begin in its current state");

            const bool packagesMounted = m_streaming->GetPackageSet().IsMounted();
            if (request.mountPackages)
            {
                if (m_status != engine::WorldSessionStatus::Idle || packagesMounted)
                    return Fail(failure, engine::WorldSessionFailureCode::InvalidState, "world session package set is already mounted");
                const streaming::PackageSetMountResult result =
                    m_streaming->GetPackageSet().Mount(m_streaming->GetStreamer(), request.gameDirectory, request.packageConfig);
                if (result != streaming::PackageSetMountResult::Success)
                    return Fail(failure, engine::WorldSessionFailureCode::PackageMountFailure, "world session package mounting failed", result);
                m_status = engine::WorldSessionStatus::Mounted;
            }
            else if (!packagesMounted || m_status != engine::WorldSessionStatus::Mounted)
            {
                return Fail(failure, engine::WorldSessionFailureCode::InvalidState, "world session requires a retained package set");
            }

            if (!m_inputInstalled && !m_inputRequest.IsValid())
            {
                const resources::ResourceReference input = m_streaming->GetPackageSet().GetDefaultInput();
                if (!input.IsValid() || input.ExpectedType() != vanguard::game_input::MappingResourceType)
                    return Fail(failure, engine::WorldSessionFailureCode::InvalidRequest, "world session package set has no valid default input mapping");
                m_inputRequest = m_streaming->GetStreamer().Request(input, resources::LoadPriority::Critical);
                if (!m_inputRequest.IsValid())
                    return Fail(failure, engine::WorldSessionFailureCode::InputRequestFailure, "world session default input request was rejected");
            }

            const resources::ResourceReference world = request.world.IsValid() ? request.world : m_streaming->GetPackageSet().StartupWorld();
            if (!world.IsValid())
                return Fail(failure, engine::WorldSessionFailureCode::InvalidRequest, "world session start request has no valid world resource");
            if (!m_world->BeginWorld(world))
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
                                       streaming::PackageSetMountResult::Success, m_world->GetLastFailure()));
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
                                           streaming::PackageSetMountResult::Success, inputFailure));
                    return m_status;
                }
                resources::ResourceHandle input = m_inputRequest.Acquire();
                m_inputRequest.Reset();
                if (!input.IsValid() || input.Get()->GetType() != vanguard::game_input::MappingResourceType)
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputLoadFailure,
                                           "world session default input resource has an invalid type", streaming::PackageSetMountResult::Success,
                                           resources::Failure::InternalError));
                    return m_status;
                }
                const auto* const mapping = static_cast<const vanguard::game_input::MappingResource*>(input.Get());
                if (m_gameInput->Install(mapping->GetFile()) != vanguard::game_input::MappingResult::Success)
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputInstallFailure, "world session default input installation failed"));
                    return m_status;
                }
                m_inputInstalled = true;
            }
            if (!m_gameWorld->BeginWorld())
            {
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::GameWorldStartFailure, "world session game-world initialization failed"));
                return m_status;
            }

            m_status = engine::WorldSessionStatus::Running;
            m_lastFailure = {};
            return m_status;
        }

        [[nodiscard]] bool RequestStop(const engine::WorldSessionStopMode mode, engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (static_cast<vanguard::u32>(mode) > static_cast<vanguard::u32>(engine::WorldSessionStopMode::ReleaseEverything))
                return Fail(failure, engine::WorldSessionFailureCode::InvalidRequest, "invalid world session stop mode");

            if (m_stopRequested)
            {
                if (mode == engine::WorldSessionStopMode::ReleaseEverything)
                    m_stopMode = engine::WorldSessionStopMode::ReleaseEverything;
                return true;
            }
            if (m_status == engine::WorldSessionStatus::Idle)
                return true;
            if (m_status == engine::WorldSessionStatus::Mounted && mode == engine::WorldSessionStopMode::ReleaseWorld)
                return true;

            m_stopRequested = true;
            m_stopMode = mode;
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
            if (m_streaming->GetPackageSet().IsMounted())
                return app::LifecycleStatus::Failure("World Session requires exclusive package-set ownership");
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
            if (m_status != engine::WorldSessionStatus::Idle || m_stopRequested || m_streaming->GetPackageSet().IsMounted())
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
            if (m_stopMode == engine::WorldSessionStopMode::ReleaseEverything && m_inputRequest.IsValid())
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
            if (!m_world->ReleaseWorld())
                return m_status;

            if (m_stopMode == engine::WorldSessionStopMode::ReleaseEverything && m_streaming->GetPackageSet().IsMounted())
            {
                const streaming::PackageSetMountResult result = m_streaming->GetPackageSet().Unmount();
                if (result == streaming::PackageSetMountResult::Busy)
                    return m_status;
                if (result != streaming::PackageSetMountResult::Success)
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::PackageUnmountFailure, "world session package unmount failed", result));
                    return m_status;
                }
            }

            m_stopRequested = false;
            if (m_stopMode == engine::WorldSessionStopMode::ReleaseEverything)
                m_inputInstalled = false;
            m_status = m_streaming->GetPackageSet().IsMounted() ? engine::WorldSessionStatus::Mounted : engine::WorldSessionStatus::Idle;
            m_lastFailure = {};
            return m_status;
        }

        void ClearOutput(engine::WorldSessionFailure* const failure) const noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(engine::WorldSessionFailure* const output, const engine::WorldSessionFailureCode code, const char* const message,
                                const streaming::PackageSetMountResult packageResult = streaming::PackageSetMountResult::Success,
                                const resources::Failure resourceFailure = resources::Failure::None) noexcept
        {
            m_status = engine::WorldSessionStatus::Failed;
            m_lastFailure = {code, packageResult, resourceFailure, message};
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
        engine::WorldSessionStopMode m_stopMode = engine::WorldSessionStopMode::ReleaseEverything;
        bool m_stopRequested = false;
        bool m_inputInstalled = false;
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

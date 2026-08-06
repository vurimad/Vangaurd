#include <vanguard/engine/window_service.hpp>

#include <vanguard/application/platform_host.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace win = vanguard::window;

    class ManagedWindowService final : public engine::WindowService, public win::IWindowEventSink
    {
    public:
        explicit ManagedWindowService(app::IPlatformHost* const platform) noexcept : m_platform(platform) {}

        [[nodiscard]] win::WindowManager& Manager() noexcept override { return m_manager; }
        [[nodiscard]] const win::WindowManager& Manager() const noexcept override { return m_manager; }
        [[nodiscard]] win::WindowHandle PrimaryWindow() const noexcept override { return m_primaryWindow; }

        [[nodiscard]] win::BackendStatus RefreshDisplayTopology() noexcept override
        {
            win::Failure failure;
            return m_manager.RefreshDisplays(&failure)
                ? win::BackendStatus::Success()
                : win::BackendStatus::Failure(failure.backendCode,
                                              failure.message != nullptr ? failure.message : "display topology refresh failed");
        }

        [[nodiscard]] win::WindowEventSinkResult ProcessWindowEvent(const win::BackendWindowEvent& event) noexcept override
        {
            win::Failure failure;
            if (!m_manager.ProcessBackendEvent(event, &failure))
                return {win::BackendStatus::Failure(
                            failure.backendCode,
                            failure.message != nullptr ? failure.message : "window event processing failed"),
                        win::WindowEventSinkAction::Continue};
            const bool primaryClose = event.type == win::BackendEventType::CloseRequested &&
                                      m_manager.ResolveBackendWindow(event.window) == m_primaryWindow;
            return {win::BackendStatus::Success(), primaryClose ? win::WindowEventSinkAction::RequestApplicationExit
                                                                : win::WindowEventSinkAction::Continue};
        }

        [[nodiscard]] win::WindowHandle ResolveWindow(const win::BackendWindowId backendWindow) const noexcept override
        {
            return m_manager.ResolveBackendWindow(backendWindow);
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            if (m_platform == nullptr || m_platform->WindowBackend() == nullptr)
                return app::LifecycleStatus::Failure("Window service requires a platform window backend");
            win::Failure failure;
            if (!m_manager.Initialize(*m_platform->WindowBackend(), &failure))
                return app::LifecycleStatus::Failure(
                    failure.message != nullptr ? failure.message : "Window manager initialization failed");
            if (!m_platform->AttachWindowEventSink(this))
            {
                static_cast<void>(m_manager.Shutdown(&failure));
                return app::LifecycleStatus::Failure("platform rejected the Window service event sink");
            }
            m_attached = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext& context) noexcept override
        {
            const bool editor = app::HasProfile(context.Profile(), app::ApplicationProfile::Editor);
            win::WindowDescriptor descriptor;
            descriptor.title = editor ? "RED Vanguard Editor" : "RED Vanguard";
            descriptor.role = editor ? win::WindowRole::EditorMain : win::WindowRole::Primary;
            descriptor.placement.display = m_manager.PrimaryDisplay();
            descriptor.placement.logicalExtent = editor ? win::WindowExtent{1600, 900} : win::WindowExtent{1280, 720};
            descriptor.placement.visible = true;
            win::Failure failure;
            if (!m_manager.Create(descriptor, m_primaryWindow, &failure))
            {
                if (vanguard::diagnostics::IsInitialized())
                    VG_LOG_ERROR(vanguard::diagnostics::Category::Engine,
                                 "primary window creation failed: code=%u backend=%d message=%s",
                                 static_cast<vanguard::u32>(failure.code), failure.backendCode,
                                 failure.message != nullptr ? failure.message : "unspecified");
                return app::LifecycleStatus::Failure("primary application window creation failed");
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStop(app::ServiceContext&) noexcept override
        {
            win::Failure failure;
            if (m_primaryWindow.IsValid())
            {
                win::WindowSnapshot snapshot;
                const bool found = m_manager.Snapshot(m_primaryWindow, snapshot);
                const bool destroyed = found && snapshot.lifecycle == win::WindowLifecycleState::CloseRequested
                    ? m_manager.ResolveCloseRequest(m_primaryWindow, snapshot.closeRequestSerial,
                                                    win::CloseDecision::Accept, &failure)
                    : m_manager.DestroyWindow(m_primaryWindow, &failure);
                if (!destroyed)
                    return app::LifecycleStatus::Failure(
                        failure.message != nullptr ? failure.message : "primary window destruction failed");
                m_primaryWindow = {};
            }
            if (m_manager.GetStats().activeWindows != 0)
                return app::LifecycleStatus::Failure("application windows remain alive during Window service stop");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_attached)
            {
                m_platform->DetachWindowEventSink(this);
                m_attached = false;
            }
            win::Failure failure;
            if (!m_manager.Shutdown(&failure))
                return app::LifecycleStatus::Failure(
                    failure.message != nullptr ? failure.message : "Window manager shutdown failed");
            m_platform = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        app::IPlatformHost* m_platform = nullptr;
        win::WindowManager m_manager;
        win::WindowHandle m_primaryWindow;
        bool m_attached = false;
    };

    app::Service* CreateWindowService(void* const userData) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Window, sizeof(ManagedWindowService), alignof(ManagedWindowService));
        return block ? ::new (block.address) ManagedWindowService(static_cast<app::IPlatformHost*>(userData)) : nullptr;
    }

    void DestroyWindowService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedWindowService*>(service)->~ManagedWindowService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedWindowService), vanguard::memory::PoolId::Window};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterWindowService(application::EngineHost& host, application::IPlatformHost* const platform,
                               application::HostFailure* const failure) noexcept
    {
        if (platform == nullptr || platform->WindowBackend() == nullptr)
        {
            if (failure != nullptr)
                *failure = {application::HostFailureCode::InvalidArgument, WindowServiceId,
                            application::InvalidServiceId, application::InvalidCapabilityId,
                            "Window service requires an initialized platform window backend"};
            return false;
        }
        constexpr application::CapabilityId capabilities[]{WindowCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = WindowServiceId;
        descriptor.name = "window";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor |
                              application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::Process;
        descriptor.affinity = application::ThreadAffinity::PlatformThread;
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateWindowService;
        descriptor.destroy = DestroyWindowService;
        descriptor.userData = platform;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    WindowService* FindWindowService(application::EngineHost& host) noexcept
    {
        return static_cast<WindowService*>(host.FindCapability(WindowCapabilityId));
    }

    WindowService* FindWindowService(application::ServiceContext& context) noexcept
    {
        return static_cast<WindowService*>(context.FindCapability(WindowCapabilityId));
    }
} // namespace vanguard::engine

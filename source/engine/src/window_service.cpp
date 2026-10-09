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
        explicit ManagedWindowService(const engine::WindowServiceConfig& config) noexcept
            : m_platform(config.platform), m_primaryLogicalExtent(config.primaryLogicalExtent)
        {
        }

        [[nodiscard]] win::WindowManager& GetManager() noexcept override
        {
            return m_manager;
        }
        [[nodiscard]] const win::WindowManager& GetManager() const noexcept override
        {
            return m_manager;
        }
        [[nodiscard]] win::WindowHandle GetPrimaryWindow() const noexcept override
        {
            return m_primaryWindow;
        }
        [[nodiscard]] bool AttachFileDropSink(engine::IWindowFileDropSink* const sink) noexcept override
        {
            if (sink == nullptr || (m_fileDropSink != nullptr && m_fileDropSink != sink))
                return false;
            m_fileDropSink = sink;
            return true;
        }
        void DetachFileDropSink(engine::IWindowFileDropSink* const sink) noexcept override
        {
            if (m_fileDropSink == sink)
                m_fileDropSink = nullptr;
        }

        [[nodiscard]] win::BackendStatus RefreshDisplayTopology() noexcept override
        {
            win::Failure failure;
            return m_manager.RefreshDisplays(&failure)
                       ? win::BackendStatus::Success()
                       : win::BackendStatus::Failure(failure.backendCode, failure.message != nullptr ? failure.message : "display topology refresh failed");
        }

        [[nodiscard]] win::WindowEventSinkResult ProcessWindowEvent(const win::BackendWindowEvent& event) noexcept override
        {
            if (event.type == win::BackendEventType::FileDropBegin || event.type == win::BackendEventType::FileDropped ||
                event.type == win::BackendEventType::FileDropComplete)
            {
                const win::WindowHandle window = m_manager.ResolveBackendWindow(event.window);
                if (!window.IsValid())
                    return {win::BackendStatus::Failure(-1, "file-drop event references an unknown window"),
                            win::WindowEventSinkAction::Continue};
                if (m_fileDropSink != nullptr)
                {
                    engine::WindowFileDropEvent drop;
                    drop.type = event.type == win::BackendEventType::FileDropBegin
                                    ? engine::WindowFileDropEventType::Begin
                                : event.type == win::BackendEventType::FileDropped
                                    ? engine::WindowFileDropEventType::File
                                    : engine::WindowFileDropEventType::Complete;
                    drop.window = window;
                    drop.position = event.dropPosition;
                    drop.timestampNanoseconds = event.timestampNanoseconds;
                    drop.path = event.dropPath;
                    if (!m_fileDropSink->ProcessFileDrop(drop))
                        return {win::BackendStatus::Failure(-1, "file-drop sink rejected the event"),
                                win::WindowEventSinkAction::Continue};
                }
                return {win::BackendStatus::Success(), win::WindowEventSinkAction::Continue};
            }
            win::Failure failure;
            if (!m_manager.ProcessBackendEvent(event, &failure))
                return {win::BackendStatus::Failure(failure.backendCode, failure.message != nullptr ? failure.message : "window event processing failed"),
                        win::WindowEventSinkAction::Continue};
            const bool primaryClose = event.type == win::BackendEventType::CloseRequested && m_manager.ResolveBackendWindow(event.window) == m_primaryWindow;
            return {win::BackendStatus::Success(), primaryClose ? win::WindowEventSinkAction::RequestApplicationExit : win::WindowEventSinkAction::Continue};
        }

        [[nodiscard]] win::WindowHandle ResolveWindow(const win::BackendWindowId backendWindow) const noexcept override
        {
            return m_manager.ResolveBackendWindow(backendWindow);
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            if (m_platform == nullptr || m_platform->GetWindowBackend() == nullptr)
                return app::LifecycleStatus::Failure("Window service requires a platform window backend");
            win::Failure failure;
            if (!m_manager.Initialize(*m_platform->GetWindowBackend(), &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "Window manager initialization failed");
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
            const bool editor = app::HasProfile(context.GetProfile(), app::ApplicationProfile::Editor);
            win::WindowDescriptor descriptor;
            descriptor.title = editor ? "RED Vanguard Editor" : "RED Vanguard";
            descriptor.role = editor ? win::WindowRole::EditorMain : win::WindowRole::Primary;
            descriptor.placement.display = m_manager.GetPrimaryDisplay();
            descriptor.placement.logicalExtent = m_primaryLogicalExtent;
            descriptor.initialPlacement = win::InitialWindowPlacement::CenteredOnDisplay;
            // Editor user state restores the primary placement while the native window is
            // still hidden, then performs the one authoritative reveal before service
            // startup completes. Runtime/tool profiles keep the existing immediate show.
            descriptor.placement.visible = !editor;
            if (editor)
                descriptor.flags = descriptor.flags | win::WindowFlag::Borderless | win::WindowFlag::AcceptFileDrop;
            win::Failure failure;
            if (!m_manager.Create(descriptor, m_primaryWindow, &failure))
            {
                VG_LOG_ERROR(vanguard::diagnostics::Category::Engine, "primary window creation failed: code=%u backend=%d message=%s",
                             static_cast<vanguard::u32>(failure.code), failure.backendCode, failure.message != nullptr ? failure.message : "unspecified");
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
                const bool found = m_manager.GetSnapshot(m_primaryWindow, snapshot);
                const bool destroyed = found && snapshot.lifecycle == win::WindowLifecycleState::CloseRequested
                                           ? m_manager.ResolveCloseRequest(m_primaryWindow, snapshot.closeRequestSerial, win::CloseDecision::Accept, &failure)
                                           : m_manager.DestroyWindow(m_primaryWindow, &failure);
                if (!destroyed)
                    return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "primary window destruction failed");
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
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "Window manager shutdown failed");
            m_platform = nullptr;
            m_fileDropSink = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        app::IPlatformHost* m_platform = nullptr;
        win::WindowExtent m_primaryLogicalExtent;
        win::WindowManager m_manager;
        win::WindowHandle m_primaryWindow;
        engine::IWindowFileDropSink* m_fileDropSink = nullptr;
        bool m_attached = false;
    };

    app::Service* CreateWindowService(void* const userData) noexcept
    {
        const auto* const config = static_cast<const engine::WindowServiceConfig*>(userData);
        if (config == nullptr)
            return nullptr;
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Window, sizeof(ManagedWindowService), alignof(ManagedWindowService));
        return block ? ::new (block.address) ManagedWindowService(*config) : nullptr;
    }

    void DestroyWindowService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedWindowService*>(service)->~ManagedWindowService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedWindowService), vanguard::memory::PoolId::Window};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::engine
{
    bool RegisterWindowService(application::EngineHost& host, const WindowServiceConfig& config, application::HostFailure* const failure) noexcept
    {
        const window::WindowConstraints constraints;
        if (config.platform == nullptr || config.platform->GetWindowBackend() == nullptr || !config.primaryLogicalExtent.IsValid() ||
            config.primaryLogicalExtent.width > constraints.maximum.width || config.primaryLogicalExtent.height > constraints.maximum.height)
        {
            if (failure != nullptr)
                *failure = {application::HostFailureCode::InvalidArgument, WindowServiceId, application::InvalidServiceId, application::InvalidCapabilityId,
                            "Window service requires an initialized platform window backend and a valid primary extent"};
            return false;
        }
        constexpr application::CapabilityId capabilities[]{WindowCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = WindowServiceId;
        descriptor.name = "window";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::Process;
        descriptor.affinity = application::ThreadAffinity::PlatformThread;
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateWindowService;
        descriptor.destroy = DestroyWindowService;
        descriptor.userData = const_cast<WindowServiceConfig*>(&config);
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

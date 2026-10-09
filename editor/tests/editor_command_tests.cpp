#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_ui.hpp>
#include <editor_extension_registration_test_adapter.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/input/input.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstdio>
#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;
    namespace input = vanguard::input;
    namespace memory = vanguard::memory;
    using Registry = editor::testing::EditorExtensionRegistrationTestAdapter;
    using vanguard::u32;
    using vanguard::u64;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[editorCommandTests] FAILED: %s\n", message);
        ++g_failures;
    }

    class FixtureInputBackend final : public input::IInputBackend
    {
    public:
        [[nodiscard]] bool Push(const input::RawEvent& event) noexcept
        {
            if (m_count >= input::MaximumBackendEventsPerFrame)
                return false;
            m_events[m_count++] = event;
            return true;
        }

        void Clear() noexcept { m_count = 0; }

        [[nodiscard]] input::BackendDrainResult Drain(input::RawEvent* const destination, const u32 capacity) noexcept override
        {
            const u32 count = m_count < capacity ? m_count : capacity;
            for (u32 index = 0; index < count; ++index)
                destination[index] = m_events[index];
            const u32 dropped = m_count > capacity ? m_count - capacity : 0u;
            m_count = 0;
            return {count, dropped, false};
        }
        [[nodiscard]] bool SetRumble(input::DeviceId, vanguard::f32, vanguard::f32, u32) noexcept override { return false; }
        void RequestDeviceRefresh() noexcept override {}

    private:
        input::RawEvent m_events[input::MaximumBackendEventsPerFrame]{};
        u32 m_count = 0;
    };

    class FixtureIoService final : public app::Service
    {
    };

    app::Service* CreateFixtureIo(void*) noexcept
    {
        const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Tools, sizeof(FixtureIoService), alignof(FixtureIoService));
        return block ? ::new (block.address) FixtureIoService() : nullptr;
    }

    void DestroyFixtureIo(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<FixtureIoService*>(service)->~FixtureIoService();
        memory::MemoryBlock block{service, sizeof(FixtureIoService), memory::PoolId::Tools};
        memory::Free(block);
    }

    class FixtureFramePipelineService final : public engine::FramePipelineService
    {
    public:
        [[nodiscard]] bool Configure(const engine::FramePipelineConfig&, engine::FrameFailure*) noexcept override { return true; }
        [[nodiscard]] bool RegisterParticipant(const engine::FrameParticipantDescriptor&, engine::FrameFailure*) noexcept override { return true; }
        [[nodiscard]] bool Compile(engine::FrameFailure*) noexcept override { return true; }
        [[nodiscard]] bool RunFrame(engine::FrameFailure*) noexcept override { return true; }
        [[nodiscard]] bool SetPaused(bool) noexcept override { return true; }
        [[nodiscard]] bool SetTimeScale(vanguard::f32) noexcept override { return true; }
        [[nodiscard]] engine::FramePipelineState GetState() const noexcept override { return engine::FramePipelineState::Building; }
        [[nodiscard]] engine::FramePipelineStats GetStats() const noexcept override { return {}; }
        void VisitParticipantStats(engine::FrameParticipantVisitor, void*) const noexcept override {}
        [[nodiscard]] const engine::FrameFailure& GetLastFailure() const noexcept override { return m_failure; }

    private:
        engine::FrameFailure m_failure;
    };

    class FixtureInputService final : public engine::InputService
    {
    public:
        [[nodiscard]] const input::FrameSnapshot& GetSnapshot() const noexcept override { return m_snapshot; }
        [[nodiscard]] vanguard::containers::ArraySpan<const input::RawEvent> GetEvents() const noexcept override { return {}; }
        [[nodiscard]] const input::GamepadState* FindGamepad(input::DeviceId) const noexcept override { return nullptr; }
        [[nodiscard]] bool SetRumble(input::DeviceId, vanguard::f32, vanguard::f32, u32) noexcept override { return false; }
        void RequestReset() noexcept override {}
        void RequestDeviceRefresh() noexcept override {}
        [[nodiscard]] input::InputStats GetStats() const noexcept override { return {}; }

    private:
        input::FrameSnapshot m_snapshot;
    };

    app::Service* CreateFixtureFramePipeline(void*) noexcept
    {
        const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Tools, sizeof(FixtureFramePipelineService), alignof(FixtureFramePipelineService));
        return block ? ::new (block.address) FixtureFramePipelineService() : nullptr;
    }

    void DestroyFixtureFramePipeline(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<FixtureFramePipelineService*>(service)->~FixtureFramePipelineService();
        memory::MemoryBlock block{service, sizeof(FixtureFramePipelineService), memory::PoolId::Tools};
        memory::Free(block);
    }

    app::Service* CreateFixtureInputService(void*) noexcept
    {
        const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Tools, sizeof(FixtureInputService), alignof(FixtureInputService));
        return block ? ::new (block.address) FixtureInputService() : nullptr;
    }

    void DestroyFixtureInputService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<FixtureInputService*>(service)->~FixtureInputService();
        memory::MemoryBlock block{service, sizeof(FixtureInputService), memory::PoolId::Tools};
        memory::Free(block);
    }

    struct FixtureUiState final
    {
        bool checkQuiescedRouting = false;
        bool quiescedRoutingRejected = false;
        bool quiescedOverrideRejected = false;
        bool quiescedClaimsCleared = false;
        bool textInputCaptured = false;
        bool keyboardCaptured = false;
        bool captureMatchesFrame = true;
        u64 executedFrame = 0;
        editor::EditorUiInputCapture capture;
        editor::EditorCommandId command;
    };

    class FixturePanelRegistry final : public editor::EditorPanelRegistry
    {
    public:
        [[nodiscard]] bool RegisterType(const editor::EditorPanelTypeDesc&, editor::EditorPanelTypeToken&, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool UnregisterType(editor::EditorPanelTypeToken, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool CreatePanel(const editor::EditorPanelDesc&, editor::EditorPanelHandle&, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool CreatePlaceholder(const editor::EditorPanelPlaceholderDesc&, editor::EditorPanelHandle&, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool DestroyPanel(editor::EditorPanelHandle, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool SetOpen(editor::EditorPanelHandle, bool, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool IsOpen(editor::EditorPanelHandle) const noexcept override { return false; }
        void VisitPanels(editor::EditorPanelVisitor, void*) const noexcept override {}
    };

    class FixtureUiService final : public editor::EditorUiService
    {
    public:
        explicit FixtureUiService(FixtureUiState& state) noexcept : m_state(state) {}

        [[nodiscard]] editor::EditorPanelRegistry& GetPanels() noexcept override { return m_panels; }
        [[nodiscard]] const editor::EditorPanelRegistry& GetPanels() const noexcept override { return m_panels; }
        [[nodiscard]] bool RegisterHost(const editor::EditorHostWindowDesc&, editor::EditorHostWindowHandle&, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool UpdateHost(editor::EditorHostWindowHandle, const editor::EditorHostWindowDesc&, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool UnregisterHost(editor::EditorHostWindowHandle, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool GetHost(editor::EditorHostWindowHandle, editor::EditorHostWindowInfo&) const noexcept override { return false; }
        void VisitHosts(editor::EditorHostWindowVisitor, void*) const noexcept override {}
        [[nodiscard]] bool RegisterTexture(const editor::EditorTextureDesc&, editor::EditorTextureHandle&, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool UnregisterTexture(editor::EditorTextureHandle, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool ResolveTexture(editor::EditorTextureHandle, editor::ResolvedEditorTexture&) const noexcept override { return false; }
        [[nodiscard]] bool AcquireFrameData(editor::EditorHostWindowHandle, editor::EditorUiFrameData&) const noexcept override { return false; }
        [[nodiscard]] bool AcquireTextureFrameData(editor::EditorUiTextureFrameData&) const noexcept override { return false; }
        [[nodiscard]] bool BeginFrame(const engine::FrameContext& frame, editor::EditorUiFailure*) noexcept override
        {
            PublishCapture(frame.frame);
            return true;
        }
        [[nodiscard]] bool DrawOpenPanels(editor::EditorUiFailure*) noexcept override { return true; }
        [[nodiscard]] bool EndFrame(editor::EditorUiFailure*) noexcept override { return true; }
        void AbortFrame() noexcept override { m_state.capture = {}; }
        [[nodiscard]] editor::EditorUiInputCapture GetInputCapture() const noexcept override { return m_state.capture; }
        [[nodiscard]] editor::EditorPanelFocusSnapshot GetPanelFocus() const noexcept override { return {}; }
        [[nodiscard]] vanguard::f32 GetUserScale() const noexcept override { return 1.0f; }
        [[nodiscard]] bool SetUserScale(vanguard::f32, editor::EditorUiFailure*) noexcept override { return true; }
        [[nodiscard]] editor::EditorUiState GetState() const noexcept override { return m_uiState; }
        [[nodiscard]] u64 GetFrame() const noexcept override { return m_state.executedFrame; }
        [[nodiscard]] bool RequestContextMenuForPanel(editor::EditorPanelHandle, u64, editor::EditorUiSurfaceId,
                                                     editor::EditorUiFailure*) noexcept override { return false; }

        [[nodiscard]] bool RegisterSurfaceForOwner(const editor::EditorExtensionOwner&,
                                                  const editor::detail::EditorUiSurfaceDesc&,
                                                  editor::detail::EditorUiSurfaceToken&,
                                                  editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool UnregisterSurfaceForOwner(const editor::EditorExtensionOwner&,
                                                    const editor::detail::EditorUiSurfaceToken&,
                                                    editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool RegisterContributionForOwner(const editor::EditorExtensionOwner&,
                                                       const editor::detail::EditorUiContributionDesc&,
                                                       editor::detail::EditorUiContributionToken&,
                                                       editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool UnregisterContributionForOwner(const editor::EditorExtensionOwner&,
                                                         const editor::detail::EditorUiContributionToken&,
                                                         editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        void RevokeSurfaceOwner(const editor::EditorExtensionOwner&) noexcept override {}
        [[nodiscard]] bool RegisterSurfaceCommandBridge(const editor::detail::EditorUiCommandBridge&,
                                                       editor::detail::EditorUiCommandBridgeToken&,
                                                       editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool UnregisterSurfaceCommandBridge(const editor::detail::EditorUiCommandBridgeToken&,
                                                         editor::EditorUiFailure*) noexcept override
        {
            return false;
        }

        [[nodiscard]] bool RegisterPanelFocusSink(editor::detail::EditorPanelFocusSink, void*,
                                                 editor::detail::EditorPanelFocusSinkToken& token,
                                                 editor::EditorUiFailure*) noexcept override
        {
            token = {};
            return false;
        }
        [[nodiscard]] bool UnregisterPanelFocusSink(editor::detail::EditorPanelFocusSinkToken,
                                                   editor::EditorUiFailure*) noexcept override
        {
            return false;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_frames = engine::FindFramePipelineService(context);
            if (m_frames == nullptr)
                return app::LifecycleStatus::Failure("fixture UI frame pipeline is unavailable");
            constexpr engine::FrameParticipantId dependencies[]{engine::InputFrameParticipantId};
            engine::FrameParticipantDescriptor descriptor;
            descriptor.id = editor::EditorUiFrameParticipantId;
            descriptor.name = "fixtureEditorUi";
            descriptor.phase = engine::FramePhase::BeginFrame;
            descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies, 1};
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            engine::FrameFailure failure;
            if (!m_frames->RegisterParticipant(descriptor, &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "fixture UI participant registration failed");
            m_uiState = editor::EditorUiState::Ready;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext& context) noexcept override
        {
            m_state.capture = {};
            if (!m_state.checkQuiescedRouting)
            {
                m_uiState = editor::EditorUiState::Quiesced;
                return app::LifecycleStatus::Success();
            }
            editor::EditorCommandService* const commands = editor::FindEditorCommandService(context);
            editor::EditorCommandState state;
            editor::EditorCommandFailure failure;
            m_state.quiescedRoutingRejected = commands != nullptr && !commands->Query(m_state.command, state, &failure) &&
                                                failure.code == editor::EditorCommandFailureCode::WrongPhase;
            failure = {};
            m_state.quiescedOverrideRejected =
                commands != nullptr &&
                !commands->SetShortcutOverride(editor::EditorWorkspaceCommandContextTypeId, m_state.command,
                                               {editor::EditorShortcutOverrideMode::Unbound, {}}, &failure) &&
                failure.code == editor::EditorCommandFailureCode::WrongPhase;
            m_state.quiescedClaimsCleared = commands != nullptr && !commands->IsInputEventClaimed(commands->GetStatus().currentFrame, 0u);
            m_uiState = editor::EditorUiState::Quiesced;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_state.capture = {};
            m_frames = nullptr;
            m_uiState = editor::EditorUiState::Offline;
            return app::LifecycleStatus::Success();
        }

    private:
        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<FixtureUiService*>(userData);
            if (service == nullptr)
                return engine::FrameParticipantStatus::Failure("fixture UI service is unavailable");
            service->PublishCapture(context.frame);
            return engine::FrameParticipantStatus::Success();
        }

        void PublishCapture(const u64 frame) noexcept
        {
            m_state.executedFrame = frame;
            m_state.capture = {m_state.captureMatchesFrame ? frame : frame + 1u, m_state.textInputCaptured, m_state.keyboardCaptured};
        }

        FixtureUiState& m_state;
        FixturePanelRegistry m_panels;
        engine::FramePipelineService* m_frames = nullptr;
        editor::EditorUiState m_uiState = editor::EditorUiState::Offline;
    };

    app::Service* CreateFixtureUi(void* const userData) noexcept
    {
        auto& state = *static_cast<FixtureUiState*>(userData);
        const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Tools, sizeof(FixtureUiService), alignof(FixtureUiService));
        return block ? ::new (block.address) FixtureUiService(state) : nullptr;
    }

    void DestroyFixtureUi(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<FixtureUiService*>(service)->~FixtureUiService();
        memory::MemoryBlock block{service, sizeof(FixtureUiService), memory::PoolId::Tools};
        memory::Free(block);
    }

    bool RegisterFixtureIo(app::EngineHost& host, app::HostFailure& failure) noexcept
    {
        app::ServiceDescriptor descriptor;
        descriptor.id = engine::IoServiceId;
        descriptor.name = "fixtureIo";
        descriptor.profiles = app::ApplicationProfile::All;
        descriptor.scope = app::ServiceScope::Process;
        descriptor.affinity = app::ThreadAffinity::MainThread;
        descriptor.create = CreateFixtureIo;
        descriptor.destroy = DestroyFixtureIo;
        return host.RegisterService(engine::EngineModuleId, descriptor, &failure);
    }

    bool RegisterFixtureUi(app::EngineHost& host, FixtureUiState& state, app::HostFailure& failure) noexcept;

    bool RegisterLightweightCommandDependencies(app::EngineHost& host, FixtureUiState& state, app::HostFailure& failure) noexcept
    {
        app::ServiceDescriptor frame;
        frame.id = engine::FramePipelineServiceId;
        frame.name = "stubFramePipeline";
        frame.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        frame.scope = app::ServiceScope::Engine;
        frame.affinity = app::ThreadAffinity::MainThread;
        constexpr app::CapabilityId frameCapabilities[]{engine::FramePipelineCapabilityId};
        frame.provides = {frameCapabilities, 1};
        frame.create = CreateFixtureFramePipeline;
        frame.destroy = DestroyFixtureFramePipeline;
        if (!host.RegisterService(engine::EngineModuleId, frame, &failure))
            return false;

        app::ServiceDescriptor inputService;
        inputService.id = engine::InputServiceId;
        inputService.name = "stubInput";
        inputService.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        inputService.scope = app::ServiceScope::Engine;
        inputService.affinity = app::ThreadAffinity::MainThread;
        constexpr app::CapabilityId inputCapabilities[]{engine::InputCapabilityId};
        inputService.provides = {inputCapabilities, 1};
        inputService.create = CreateFixtureInputService;
        inputService.destroy = DestroyFixtureInputService;
        if (!host.RegisterService(engine::EngineModuleId, inputService, &failure))
            return false;

        return RegisterFixtureUi(host, state, failure);
    }

    bool RegisterFixtureUi(app::EngineHost& host, FixtureUiState& state, app::HostFailure& failure) noexcept
    {
        constexpr app::ServiceDependency dependencies[]{{engine::FramePipelineServiceId, app::DependencyKind::Required},
                                                        {engine::InputServiceId, app::DependencyKind::Required}};
        constexpr app::CapabilityId capabilities[]{editor::EditorUiCapabilityId};
        app::ServiceDescriptor descriptor;
        descriptor.id = editor::EditorUiServiceId;
        descriptor.name = "fixtureUi";
        descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        descriptor.scope = app::ServiceScope::EditorWorkspace;
        descriptor.affinity = app::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 2};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateFixtureUi;
        descriptor.destroy = DestroyFixtureUi;
        descriptor.userData = &state;
        return host.RegisterService(editor::EditorModuleId, descriptor, &failure);
    }

    struct CommandHostFixture final
    {
        app::EngineHost host;
        app::HostFailure failure;
        FixtureInputBackend inputBackend;
        FixtureUiState ui;

        [[nodiscard]] bool Compile(const app::ApplicationProfile profile = app::ApplicationProfile::Editor) noexcept
        {
            if (!engine::RegisterEngineModule(host, &failure) || !host.RegisterModule({editor::EditorModuleId, "editor", 1}, &failure) ||
                !RegisterFixtureIo(host, failure) || !engine::RegisterJobsService(host, &failure) || !engine::RegisterFramePipelineService(host, &failure) ||
                !engine::RegisterInputService(host, &inputBackend, &failure) || !RegisterFixtureUi(host, ui, failure) ||
                !editor::RegisterEditorCommandService(host, &failure))
                return false;
            return host.Compile(profile, &failure);
        }

        [[nodiscard]] editor::EditorCommandService* Start() noexcept
        {
            return host.Start(&failure) ? editor::FindEditorCommandService(host) : nullptr;
        }
    };

    struct LightweightCommandHostFixture final
    {
        app::EngineHost host;
        app::HostFailure failure;
        FixtureUiState ui;

        [[nodiscard]] bool Compile(const app::ApplicationProfile profile = app::ApplicationProfile::Editor) noexcept
        {
            if (!host.RegisterModule({engine::EngineModuleId, "engine", 1}, &failure) ||
                !host.RegisterModule({editor::EditorModuleId, "editor", 1}, &failure) ||
                !RegisterLightweightCommandDependencies(host, ui, failure) || !editor::RegisterEditorCommandService(host, &failure))
                return false;
            return host.Compile(profile, &failure);
        }

        [[nodiscard]] editor::EditorCommandService* Start() noexcept
        {
            return host.Start(&failure) ? editor::FindEditorCommandService(host) : nullptr;
        }
    };

    struct CompiledCommandProof final
    {
        bool seen = false;
        app::ServiceScope scope = app::ServiceScope::Engine;
        app::ThreadAffinity affinity = app::ThreadAffinity::CompositionThread;
        u32 frameIndex = ~u32{0};
        u32 inputIndex = ~u32{0};
        u32 uiIndex = ~u32{0};
        u32 commandIndex = ~u32{0};
    };

    void VisitCompiledCommand(const app::CompiledServiceInfo& info, void* const userData) noexcept
    {
        auto& proof = *static_cast<CompiledCommandProof*>(userData);
        if (info.service == engine::FramePipelineServiceId)
            proof.frameIndex = info.startupIndex;
        else if (info.service == engine::InputServiceId)
            proof.inputIndex = info.startupIndex;
        else if (info.service == editor::EditorUiServiceId)
            proof.uiIndex = info.startupIndex;
        else if (info.service == editor::EditorCommandServiceId)
        {
            proof.seen = true;
            proof.scope = info.scope;
            proof.affinity = info.affinity;
            proof.commandIndex = info.startupIndex;
        }
    }

    [[nodiscard]] editor::EditorCommandDescriptor Command(const u64 id, const char* const name = "command",
                                                          const editor::EditorCommandFlags flags = editor::EditorCommandFlags::None) noexcept
    {
        return {{id}, name, flags};
    }

    [[nodiscard]] editor::EditorCommandContextTypeDescriptor ContextType(const u64 id, const char* const name,
                                                                         const editor::EditorCommandContextTier tier) noexcept
    {
        return {{id}, name, tier};
    }

    [[nodiscard]] editor::EditorShortcutChord Shortcut(
        const input::Key key, const editor::EditorShortcutModifiers modifiers = editor::EditorShortcutModifiers::None) noexcept
    {
        return {key, modifiers};
    }

    struct HandlerFixture final
    {
        editor::EditorCommandService* service = nullptr;
        editor::EditorCommandState state = editor::EditorCommandState::Enabled();
        editor::EditorCommandId recursiveCommand;
        editor::EditorCommandFailureCode recursiveFailure = editor::EditorCommandFailureCode::None;
        editor::EditorCommandFailureCode mutationFailure = editor::EditorCommandFailureCode::None;
        u32 queryCalls = 0;
        u32 invokeCalls = 0;
        bool invocationResult = true;
        bool receiverWasNull = true;
        void* lastReceiver = nullptr;
        bool tryRecursiveQuery = false;
        bool tryRecursiveInvoke = false;
        bool tryMutation = false;
        bool tryContextMutation = false;
        bool tryShortcutMutation = false;
        bool recursiveAccepted = false;
        bool mutationAccepted = false;
        bool contextMutationAccepted = false;
        bool shortcutMutationAccepted = false;
        editor::EditorCommandContextTypeId mutationContext;
        editor::EditorCommandFailureCode contextMutationFailure = editor::EditorCommandFailureCode::None;
        editor::EditorCommandId shortcutMutationCommand;
        editor::EditorCommandContextTypeId shortcutMutationContext = editor::EditorWorkspaceCommandContextTypeId;
        editor::EditorCommandFailureCode shortcutMutationFailure = editor::EditorCommandFailureCode::None;
    };

    editor::EditorCommandState QueryHandler(void* const receiverData, void* const handlerData) noexcept
    {
        auto& fixture = *static_cast<HandlerFixture*>(handlerData);
        fixture.receiverWasNull = fixture.receiverWasNull && receiverData == nullptr;
        fixture.lastReceiver = receiverData;
        ++fixture.queryCalls;
        if (fixture.tryRecursiveQuery && fixture.service != nullptr)
        {
            editor::EditorCommandState nestedState;
            editor::EditorCommandFailure failure;
            fixture.recursiveAccepted = fixture.service->Query(fixture.recursiveCommand, nestedState, &failure);
            fixture.recursiveFailure = failure.code;
        }
        if (fixture.tryMutation && fixture.service != nullptr)
        {
            editor::EditorCommandToken token;
            editor::EditorCommandFailure failure;
            fixture.mutationAccepted = Registry::RegisterCommand(*fixture.service, Command(0xf001u, "callbackMutation"), token, &failure);
            fixture.mutationFailure = failure.code;
        }
        if (fixture.tryContextMutation && fixture.service != nullptr)
        {
            editor::EditorCommandContextToken token;
            editor::EditorCommandFailure failure;
            fixture.contextMutationAccepted = fixture.service->PushContext(fixture.mutationContext, nullptr,
                                                                           editor::EditorCommandContextMode::Inherit, token, &failure);
            fixture.contextMutationFailure = failure.code;
        }
        if (fixture.tryShortcutMutation && fixture.service != nullptr)
        {
            editor::EditorCommandFailure failure;
            fixture.shortcutMutationAccepted = fixture.service->SetShortcutOverride(
                fixture.shortcutMutationContext, fixture.shortcutMutationCommand,
                {editor::EditorShortcutOverrideMode::Unbound, {}}, &failure);
            fixture.shortcutMutationFailure = failure.code;
        }
        return fixture.state;
    }

    bool InvokeHandler(void* const receiverData, void* const handlerData) noexcept
    {
        auto& fixture = *static_cast<HandlerFixture*>(handlerData);
        fixture.receiverWasNull = fixture.receiverWasNull && receiverData == nullptr;
        fixture.lastReceiver = receiverData;
        ++fixture.invokeCalls;
        if (fixture.tryRecursiveInvoke && fixture.service != nullptr)
        {
            editor::EditorCommandFailure failure;
            fixture.recursiveAccepted = fixture.service->Invoke(fixture.recursiveCommand, &failure);
            fixture.recursiveFailure = failure.code;
        }
        return fixture.invocationResult;
    }

    [[nodiscard]] editor::EditorCommandHandlerDescriptor Handler(
        const editor::EditorCommandId command, HandlerFixture& fixture,
        const editor::EditorShortcutChord shortcut = {}) noexcept
    {
        return {editor::EditorWorkspaceCommandContextTypeId, command, QueryHandler, InvokeHandler, &fixture, shortcut};
    }

    [[nodiscard]] editor::EditorCommandHandlerDescriptor Handler(const editor::EditorCommandContextTypeId context,
                                                                 const editor::EditorCommandId command, HandlerFixture& fixture,
                                                                 const editor::EditorShortcutChord shortcut = {}) noexcept
    {
        return {context, command, QueryHandler, InvokeHandler, &fixture, shortcut};
    }

    class WrongThreadQueryWorker final : public vanguard::concurrency::Thread
    {
    public:
        WrongThreadQueryWorker(editor::EditorCommandService& service, const editor::EditorCommandId command) noexcept
            : Thread("commandWrongThread"), m_service(service), m_command(command)
        {
        }

        void ThreadFunction() noexcept override
        {
            editor::EditorCommandState state;
            m_result = m_service.Query(m_command, state, &m_failure);
        }

        [[nodiscard]] bool Result() const noexcept { return m_result; }
        [[nodiscard]] editor::EditorCommandFailureCode Failure() const noexcept { return m_failure.code; }

    private:
        editor::EditorCommandService& m_service;
        editor::EditorCommandId m_command;
        editor::EditorCommandFailure m_failure;
        bool m_result = false;
    };

    class WrongThreadPushWorker final : public vanguard::concurrency::Thread
    {
    public:
        WrongThreadPushWorker(editor::EditorCommandService& service, const editor::EditorCommandContextTypeId context) noexcept
            : Thread("commandWrongThreadPush"), m_service(service), m_context(context)
        {
        }

        void ThreadFunction() noexcept override
        {
            editor::EditorCommandContextToken token;
            m_result = m_service.PushContext(m_context, nullptr, editor::EditorCommandContextMode::Inherit, token, &m_failure);
        }

        [[nodiscard]] bool Result() const noexcept { return m_result; }
        [[nodiscard]] editor::EditorCommandFailureCode Failure() const noexcept { return m_failure.code; }

    private:
        editor::EditorCommandService& m_service;
        editor::EditorCommandContextTypeId m_context;
        editor::EditorCommandFailure m_failure;
        bool m_result = false;
    };

    class WrongThreadShortcutWorker final : public vanguard::concurrency::Thread
    {
    public:
        WrongThreadShortcutWorker(editor::EditorCommandService& service,
                                  const editor::EditorCommandContextTypeId context,
                                  const editor::EditorCommandId command) noexcept
            : Thread("commandWrongThreadShortcut"), m_service(service), m_context(context), m_command(command)
        {
        }

        void ThreadFunction() noexcept override
        {
            m_result = m_service.SetShortcutOverride(
                m_context, m_command, {editor::EditorShortcutOverrideMode::Unbound, {}}, &m_failure);
        }

        [[nodiscard]] bool Result() const noexcept { return m_result; }
        [[nodiscard]] editor::EditorCommandFailureCode Failure() const noexcept { return m_failure.code; }

    private:
        editor::EditorCommandService& m_service;
        editor::EditorCommandContextTypeId m_context;
        editor::EditorCommandId m_command;
        editor::EditorCommandFailure m_failure;
        bool m_result = false;
    };

    void TestProfileExclusion(const app::ApplicationProfile profile, const char* const message) noexcept
    {
        app::EngineHost host;
        app::HostFailure failure;
        Check(host.RegisterModule({editor::EditorModuleId, "editor", 1}, &failure), "profile exclusion editor module registers");
        Check(editor::RegisterEditorCommandService(host, &failure), "profile exclusion command service registers");
        Check(host.Compile(profile, &failure), message);
        Check(host.GetStats().selectedServices == 0 && editor::FindEditorCommandService(host) == nullptr,
              "non-editor profile excludes the editor command service and its missing dependencies");
        Check(host.Start(&failure), "non-editor profile starts without command service");
        Check(host.Shutdown(&failure), "non-editor profile shuts down cleanly");
    }

    void TestDescriptorAndDirectRouting(editor::EditorCommandService& service) noexcept
    {
        editor::EditorCommandFailure failure;
        editor::EditorCommandToken token;
        Check(!Registry::RegisterCommand(service, Command(0, "zero"), token, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidIdentity && !token.IsValid(),
              "zero command identity is rejected without a token");
        Check(!Registry::RegisterCommand(service, Command(1, nullptr), token, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "null command name is rejected");
        Check(!Registry::RegisterCommand(service, Command(1, ""), token, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "empty command name is rejected");
        char tooLong[editor::MaximumEditorCommandNameBytes + 1u]{};
        for (u32 index = 0; index < editor::MaximumEditorCommandNameBytes; ++index)
            tooLong[index] = 'x';
        Check(!Registry::RegisterCommand(service, Command(1, tooLong), token, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "unterminated-within-bound command name is rejected");
        Check(!Registry::RegisterCommand(service, Command(1, "flags", static_cast<editor::EditorCommandFlags>(0x80u)), token, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidFlags,
              "reserved command flags are rejected");

        editor::EditorCommandToken unsupportedToken;
        const editor::EditorCommandId unsupportedId{10};
        Check(Registry::RegisterCommand(service, Command(unsupportedId.value, "unsupported"), unsupportedToken, &failure), "valid command descriptor registers");
        editor::EditorCommandToken duplicate;
        Check(!Registry::RegisterCommand(service, Command(unsupportedId.value, "duplicate"), duplicate, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::DuplicateCommand && !duplicate.IsValid(),
              "duplicate command identity fails without partial registration");

        editor::EditorCommandState state;
        Check(service.Query(unsupportedId, state, &failure) && state.kind == editor::EditorCommandStateKind::Unsupported && !state.checked,
              "registered command with no workspace handler queries as Unsupported");
        Check(!service.Invoke(unsupportedId, &failure) && failure.code == editor::EditorCommandFailureCode::CommandUnsupported,
              "unsupported direct invocation fails without a callback");
        Check(service.GetStatus().lastRoute == editor::EditorCommandRouteResult::Unsupported &&
                  service.GetStatus().failure.code == editor::EditorCommandFailureCode::CommandUnsupported,
              "unsupported direct invocation records deterministic route/failure status");
        Check(!service.Query({9999}, state, &failure) && failure.code == editor::EditorCommandFailureCode::MissingCommand,
              "unknown command query reports MissingCommand");

        HandlerFixture unknownFixture;
        editor::EditorCommandHandlerToken handlerToken;
        editor::EditorCommandHandlerDescriptor invalidContext = Handler(unsupportedId, unknownFixture);
        invalidContext.contextType = {0x1234};
        Check(!Registry::RegisterHandler(service, invalidContext, handlerToken, &failure) && failure.code == editor::EditorCommandFailureCode::MissingContext,
              "rejects non-workspace command contexts without leaking registration");
        editor::EditorCommandHandlerDescriptor invalidCallbacks = Handler(unsupportedId, unknownFixture);
        invalidCallbacks.query = nullptr;
        Check(!Registry::RegisterHandler(service, invalidCallbacks, handlerToken, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "workspace handler requires both callbacks");
        editor::EditorCommandHandlerDescriptor missingCommand = Handler({9999}, unknownFixture);
        Check(!Registry::RegisterHandler(service, missingCommand, handlerToken, &failure) && failure.code == editor::EditorCommandFailureCode::MissingCommand,
              "workspace handler cannot reference an unknown command");

        const editor::EditorCommandId routedId{20};
        editor::EditorCommandToken routedToken;
        Check(Registry::RegisterCommand(service, Command(routedId.value, "routed", editor::EditorCommandFlags::AllowRepeat), routedToken, &failure),
              "routed command registers with repeat metadata");
        HandlerFixture routed;
        routed.service = &service;
        routed.recursiveCommand = routedId;
        Check(Registry::RegisterHandler(service, Handler(routedId, routed), handlerToken, &failure) && handlerToken.IsValid(), "workspace handler registers");
        editor::EditorCommandHandlerToken duplicateHandler;
        Check(!Registry::RegisterHandler(service, Handler(routedId, routed), duplicateHandler, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::DuplicateHandler && !duplicateHandler.IsValid(),
              "duplicate workspace handler is rejected deterministically");

        routed.state = editor::EditorCommandState::Disabled();
        const u32 disabledInvokes = routed.invokeCalls;
        Check(service.Query(routedId, state, &failure) && state.kind == editor::EditorCommandStateKind::Disabled && !state.checked,
              "workspace query preserves Disabled ownership state");
        Check(!service.Invoke(routedId, &failure) && failure.code == editor::EditorCommandFailureCode::CommandDisabled &&
                  routed.invokeCalls == disabledInvokes,
              "disabled direct invoke never calls the invocation callback");
        Check(service.GetStatus().lastRoute == editor::EditorCommandRouteResult::Disabled,
              "disabled invocation records Disabled as the selected route result");

        routed.state = editor::EditorCommandState::Unsupported();
        const u32 unsupportedInvokes = routed.invokeCalls;
        Check(service.Query(routedId, state, &failure) && state.kind == editor::EditorCommandStateKind::Unsupported,
              "explicit workspace handler may report Unsupported");
        Check(!service.Invoke(routedId, &failure) && failure.code == editor::EditorCommandFailureCode::CommandUnsupported &&
                  routed.invokeCalls == unsupportedInvokes,
              "unsupported handler never invokes or falls through");

        routed.state = editor::EditorCommandState::Enabled(true);
        Check(service.Query(routedId, state, &failure) && state.kind == editor::EditorCommandStateKind::Enabled && state.checked,
              "enabled command query preserves checked state");
        const u32 invokesBeforeSuccess = routed.invokeCalls;
        Check(service.Invoke(routedId, &failure) && routed.invokeCalls == invokesBeforeSuccess + 1u,
              "enabled direct invoke calls exactly one workspace handler");
        Check(service.GetStatus().lastRoute == editor::EditorCommandRouteResult::Invoked &&
                  service.GetStatus().failure.code == editor::EditorCommandFailureCode::None,
              "successful invocation records Invoked and clears prior route failure");
        Check(routed.receiverWasNull, "workspace routing supplies null receiver data while preserving handler data");

        routed.invocationResult = false;
        const u32 invokesBeforeFailure = routed.invokeCalls;
        Check(!service.Invoke(routedId, &failure) && failure.code == editor::EditorCommandFailureCode::HandlerInvocationFailure &&
                  routed.invokeCalls == invokesBeforeFailure + 1u,
              "handler invocation failure reports once without retry or fallback");
        Check(service.GetStatus().lastRoute == editor::EditorCommandRouteResult::Failed,
              "handler invocation failure records a failed route without fallback");
        routed.invocationResult = true;

        routed.tryRecursiveQuery = true;
        routed.recursiveAccepted = true;
        routed.recursiveFailure = editor::EditorCommandFailureCode::None;
        Check(service.Query(routedId, state, &failure) && !routed.recursiveAccepted &&
                  routed.recursiveFailure == editor::EditorCommandFailureCode::RecursiveDispatch,
              "recursive command query from a query callback fails closed");
        routed.tryRecursiveQuery = false;
        routed.tryRecursiveInvoke = true;
        routed.recursiveAccepted = true;
        routed.recursiveFailure = editor::EditorCommandFailureCode::None;
        Check(service.Invoke(routedId, &failure) && !routed.recursiveAccepted &&
                  routed.recursiveFailure == editor::EditorCommandFailureCode::RecursiveDispatch,
              "recursive command invocation from an invocation callback fails closed");
        routed.tryRecursiveInvoke = false;

        routed.tryMutation = true;
        routed.mutationAccepted = true;
        routed.mutationFailure = editor::EditorCommandFailureCode::None;
        Check(service.Query(routedId, state, &failure) && !routed.mutationAccepted &&
                  routed.mutationFailure == editor::EditorCommandFailureCode::RecursiveDispatch,
              "registry mutation from a command callback is rejected");
        routed.tryMutation = false;

        routed.state = {static_cast<editor::EditorCommandStateKind>(0xffu), true};
        Check(!service.Query(routedId, state, &failure) && failure.code == editor::EditorCommandFailureCode::HandlerQueryFailure,
              "invalid callback state fails closed as a handler-query failure");
        routed.state = editor::EditorCommandState::Enabled();

        Check(!Registry::UnregisterCommand(service, routedToken, &failure) && failure.code == editor::EditorCommandFailureCode::InUse,
              "command removal is blocked while a live handler references it");
        Check(Registry::UnregisterHandler(service, handlerToken, &failure), "workspace handler unregisters");
        Check(Registry::UnregisterCommand(service, routedToken, &failure), "command unregisters after its handler is removed");
        Check(Registry::UnregisterCommand(service, unsupportedToken, &failure), "handlerless command unregisters");

        const editor::EditorCommandServiceStatus status = service.GetStatus();
        Check(status.registeredCommandCount == 0 && status.registeredHandlerCount == 0 && status.registeredContextCount == 1 &&
                  status.activeContextCount == 0 && status.currentFrame == 0,
              "status exposes bounded registry counts and no active runtime state");
    }

    void TestStableTokenReuse(editor::EditorCommandService& service) noexcept
    {
        editor::EditorCommandFailure failure;
        editor::EditorCommandToken firstCommand;
        const editor::EditorCommandId id{30};
        Check(Registry::RegisterCommand(service, Command(id.value, "tokenA"), firstCommand, &failure), "first token command registers");
        Check(Registry::UnregisterCommand(service, firstCommand, &failure), "first token command unregisters");
        editor::EditorCommandToken secondCommand;
        Check(Registry::RegisterCommand(service, Command(id.value, "tokenB"), secondCommand, &failure) && secondCommand.index == firstCommand.index &&
                  secondCommand.generation != firstCommand.generation,
              "inactive command slot is reused only with a fresh non-zero generation");
        Check(!Registry::UnregisterCommand(service, firstCommand, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidToken,
              "stale command token cannot remove a reused slot");

        HandlerFixture fixture;
        editor::EditorCommandHandlerToken firstHandler;
        Check(Registry::RegisterHandler(service, Handler(id, fixture), firstHandler, &failure), "first handler token registers");
        Check(Registry::UnregisterHandler(service, firstHandler, &failure), "first handler token unregisters");
        editor::EditorCommandHandlerToken secondHandler;
        Check(Registry::RegisterHandler(service, Handler(id, fixture), secondHandler, &failure) && secondHandler.index == firstHandler.index &&
                  secondHandler.generation != firstHandler.generation,
              "inactive handler slot is reused only with a fresh non-zero generation");
        Check(!Registry::UnregisterHandler(service, firstHandler, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidToken,
              "stale handler token cannot remove a reused slot");
        Check(Registry::UnregisterHandler(service, secondHandler, &failure) && Registry::UnregisterCommand(service, secondCommand, &failure),
              "fresh handler and command tokens clean up successfully");
    }

    void TestCommandCapacity(editor::EditorCommandService& service) noexcept
    {
        editor::EditorCommandToken tokens[editor::MaximumEditorCommands]{};
        editor::EditorCommandHandlerToken handlerTokens[editor::MaximumEditorCommands]{};
        editor::EditorCommandFailure failure;
        bool registeredAll = true;
        for (u32 index = 0; index < editor::MaximumEditorCommands; ++index)
            if (!Registry::RegisterCommand(service, Command(1000u + index, "capacity"), tokens[index], &failure))
            {
                registeredAll = false;
                break;
            }
        Check(registeredAll && service.GetStatus().registeredCommandCount == editor::MaximumEditorCommands,
              "exact declared command capacity is admitted");
        editor::EditorCommandToken overflow;
        Check(!Registry::RegisterCommand(service, Command(1000u + editor::MaximumEditorCommands, "overflow"), overflow, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::CapacityExceeded && !overflow.IsValid() &&
                  service.GetStatus().registeredCommandCount == editor::MaximumEditorCommands,
              "513th command fails cleanly without partial state");
        editor::EditorCommandState state;
        Check(!service.Query({1000u + editor::MaximumEditorCommands}, state, &failure) && failure.code == editor::EditorCommandFailureCode::MissingCommand,
              "capacity rejection exposes no partially registered command");

        HandlerFixture handler;
        bool registeredAllHandlers = true;
        for (u32 index = 0; index < editor::MaximumEditorCommands; ++index)
            if (!Registry::RegisterHandler(service, Handler({1000u + index}, handler), handlerTokens[index], &failure))
            {
                registeredAllHandlers = false;
                break;
            }
        Check(registeredAllHandlers && service.GetStatus().registeredHandlerCount == editor::MaximumEditorCommands,
              "admits one Workspace handler for every command at the maximum reachable cardinality");
        editor::EditorCommandHandlerToken extraHandler;
        Check(!Registry::RegisterHandler(service, Handler({1000u}, handler), extraHandler, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::DuplicateHandler && !extraHandler.IsValid() &&
                  service.GetStatus().registeredHandlerCount == editor::MaximumEditorCommands,
              "cannot exceed 512 live Workspace handlers without a context and duplicate rejection leaves no partial handler");
        bool removedAllHandlers = true;
        for (u32 index = editor::MaximumEditorCommands; index > 0; --index)
            if (!Registry::UnregisterHandler(service, handlerTokens[index - 1u], &failure))
                removedAllHandlers = false;
        Check(removedAllHandlers && service.GetStatus().registeredHandlerCount == 0,
              "maximum reachable Workspace-handler fixture releases every admitted handler");

        bool removedAll = true;
        for (u32 index = editor::MaximumEditorCommands; index > 0; --index)
            if (!Registry::UnregisterCommand(service, tokens[index - 1u], &failure))
                removedAll = false;
        Check(removedAll && service.GetStatus().registeredCommandCount == 0, "capacity fixture releases every admitted command");
    }

    void TestContextDescriptorsAndRouting(editor::EditorCommandService& service) noexcept
    {
        editor::EditorCommandFailure failure;
        editor::EditorCommandContextTypeToken contextToken;
        Check(!Registry::RegisterContextType(service, ContextType(0, "zero", editor::EditorCommandContextTier::Local), contextToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidIdentity && !contextToken.IsValid(),
              "zero context identity is rejected without a token");
        Check(!Registry::RegisterContextType(service, ContextType(editor::EditorWorkspaceCommandContextTypeId.value, "workspaceReserved",
                                                       editor::EditorCommandContextTier::Local),
                                           contextToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidIdentity,
              "built-in Workspace context identity cannot be caller-registered");
        Check(!Registry::RegisterContextType(service, ContextType(0x2001u, nullptr, editor::EditorCommandContextTier::Local), contextToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "null context name is rejected");
        Check(!Registry::RegisterContextType(service, ContextType(0x2001u, "", editor::EditorCommandContextTier::Local), contextToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "empty context name is rejected");
        char tooLong[editor::MaximumEditorCommandNameBytes + 1u]{};
        for (u32 index = 0; index < editor::MaximumEditorCommandNameBytes; ++index)
            tooLong[index] = 'c';
        Check(!Registry::RegisterContextType(service, ContextType(0x2001u, tooLong, editor::EditorCommandContextTier::Local), contextToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "unterminated-within-bound context name is rejected");
        Check(!Registry::RegisterContextType(service, ContextType(0x2001u, "workspaceTier", editor::EditorCommandContextTier::Workspace), contextToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "caller-defined Workspace tier is reserved");
        Check(!Registry::RegisterContextType(service, ContextType(0x2001u, "invalidTier", static_cast<editor::EditorCommandContextTier>(0xffu)),
                                           contextToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "invalid context tier is rejected");

        constexpr editor::EditorCommandContextTypeId localId{0x2101u};
        constexpr editor::EditorCommandContextTypeId controlId{0x2102u};
        constexpr editor::EditorCommandContextTypeId barrierId{0x2103u};
        editor::EditorCommandContextTypeToken localTypeToken;
        editor::EditorCommandContextTypeToken controlTypeToken;
        editor::EditorCommandContextTypeToken barrierTypeToken;
        Check(Registry::RegisterContextType(service, ContextType(localId.value, "local", editor::EditorCommandContextTier::Local), localTypeToken, &failure),
              "Local context type registers");
        Check(Registry::RegisterContextType(service, ContextType(controlId.value, "control", editor::EditorCommandContextTier::ControlTool), controlTypeToken, &failure),
              "ControlTool context type registers");
        Check(Registry::RegisterContextType(service, ContextType(barrierId.value, "barrier", editor::EditorCommandContextTier::Local), barrierTypeToken, &failure),
              "barrier context type registers");
        editor::EditorCommandContextTypeToken duplicateType;
        Check(!Registry::RegisterContextType(service, ContextType(localId.value, "duplicate", editor::EditorCommandContextTier::Local), duplicateType, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::DuplicateContext && !duplicateType.IsValid(),
              "duplicate context identity fails without partial registration");

        editor::EditorCommandContextToken invalidActive;
        Check(!service.PushContext(editor::EditorWorkspaceCommandContextTypeId, nullptr, editor::EditorCommandContextMode::Inherit,
                                   invalidActive, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "built-in Workspace context cannot be pushed");
        Check(!service.PushContext({0x2fffu}, nullptr, editor::EditorCommandContextMode::Inherit, invalidActive, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::MissingContext,
              "unknown context type cannot be pushed");
        Check(!service.PushContext(localId, nullptr, static_cast<editor::EditorCommandContextMode>(0xffu), invalidActive, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "invalid active-context mode is rejected");

        WrongThreadPushWorker wrongThread(service, localId);
        wrongThread.InitThread();
        wrongThread.JoinThread();
        Check(!wrongThread.Result() && wrongThread.Failure() == editor::EditorCommandFailureCode::WrongThread,
              "worker-thread context push is rejected");

        constexpr editor::EditorCommandId commandId{70};
        editor::EditorCommandToken commandToken;
        Check(Registry::RegisterCommand(service, Command(commandId.value, "contextRoute"), commandToken, &failure), "context-routing command registers");
        HandlerFixture workspace;
        HandlerFixture local;
        HandlerFixture control;
        workspace.service = &service;
        local.service = &service;
        control.service = &service;
        editor::EditorCommandHandlerToken workspaceHandler;
        editor::EditorCommandHandlerToken localHandler;
        editor::EditorCommandHandlerToken controlHandler;
        Check(Registry::RegisterHandler(service, Handler(commandId, workspace), workspaceHandler, &failure), "Workspace routing handler registers");
        Check(Registry::RegisterHandler(service, Handler(localId, commandId, local), localHandler, &failure), "Local routing handler registers");
        Check(Registry::RegisterHandler(service, Handler(controlId, commandId, control), controlHandler, &failure), "ControlTool routing handler registers");

        int localReceiverA = 1;
        int localReceiverB = 2;
        int controlReceiverA = 3;
        int controlReceiverB = 4;
        editor::EditorCommandState state;
        editor::EditorCommandContextToken localA;
        editor::EditorCommandContextToken localB;
        Check(service.PushContext(localId, &localReceiverA, editor::EditorCommandContextMode::Inherit, localA, &failure),
              "first Local receiver pushes");
        Check(service.PushContext(localId, &localReceiverB, editor::EditorCommandContextMode::Inherit, localB, &failure),
              "newer Local receiver pushes");
        Check(service.Query(commandId, state, &failure) && state.kind == editor::EditorCommandStateKind::Enabled &&
                  local.lastReceiver == &localReceiverB && service.GetStatus().lastContext == localId,
              "newest Local receiver wins within its tier");
        Check(!service.PopContext(localA, &failure) && failure.code == editor::EditorCommandFailureCode::ContextOrderViolation &&
                  service.GetStatus().activeContextCount == 2,
              "out-of-order context pop fails without mutation");
        Check(service.PopContext(localB, &failure) && service.PopContext(localA, &failure),
              "nested Local contexts pop in exact reverse order");

        editor::EditorCommandContextToken controlA;
        editor::EditorCommandContextToken localNewer;
        Check(service.PushContext(controlId, &controlReceiverA, editor::EditorCommandContextMode::Inherit, controlA, &failure),
              "ControlTool receiver pushes before newer Local receiver");
        Check(service.PushContext(localId, &localReceiverA, editor::EditorCommandContextMode::Inherit, localNewer, &failure),
              "newer Local receiver pushes beneath ControlTool precedence");
        Check(service.Query(commandId, state, &failure) && control.lastReceiver == &controlReceiverA &&
                  service.GetStatus().lastContext == controlId,
              "ControlTool tier beats a newer Local receiver");
        const u32 controlInvokes = control.invokeCalls;
        Check(service.Invoke(commandId, &failure) && control.invokeCalls == controlInvokes + 1u && control.lastReceiver == &controlReceiverA,
              "selected active receiver data reaches both query and invocation callbacks");
        Check(service.PopContext(localNewer, &failure) && service.PopContext(controlA, &failure),
              "cross-tier nested contexts pop in exact reverse order");

        editor::EditorCommandContextToken controlOlder;
        editor::EditorCommandContextToken controlNewer;
        Check(service.PushContext(controlId, &controlReceiverA, editor::EditorCommandContextMode::Inherit, controlOlder, &failure) &&
                  service.PushContext(controlId, &controlReceiverB, editor::EditorCommandContextMode::Inherit, controlNewer, &failure),
              "two ControlTool receivers push");
        Check(service.Query(commandId, state, &failure) && control.lastReceiver == &controlReceiverB,
              "newest ControlTool receiver wins within the highest tier");
        Check(service.PopContext(controlNewer, &failure) && service.PopContext(controlOlder, &failure),
              "ControlTool receivers pop in reverse order");

        local.state = editor::EditorCommandState::Unsupported();
        editor::EditorCommandContextToken inheritLocal;
        Check(service.PushContext(localId, &localReceiverA, editor::EditorCommandContextMode::Inherit, inheritLocal, &failure),
              "Unsupported Inherit Local context pushes");
        Check(service.Query(commandId, state, &failure) && state.kind == editor::EditorCommandStateKind::Enabled &&
                  workspace.lastReceiver == nullptr && service.GetStatus().lastContext == editor::EditorWorkspaceCommandContextTypeId,
              "Unsupported Inherit falls through to Workspace");
        Check(service.PopContext(inheritLocal, &failure), "Unsupported Inherit Local context pops");

        editor::EditorCommandContextToken overrideLocal;
        Check(service.PushContext(localId, &localReceiverA, editor::EditorCommandContextMode::Override, overrideLocal, &failure),
              "Unsupported Override Local context pushes");
        Check(service.Query(commandId, state, &failure) && state.kind == editor::EditorCommandStateKind::Unsupported &&
                  service.GetStatus().lastContext == localId,
              "Unsupported Override handler stops lower receiver lookup");
        const u32 workspaceInvokesBeforeOverride = workspace.invokeCalls;
        Check(!service.Invoke(commandId, &failure) && failure.code == editor::EditorCommandFailureCode::CommandUnsupported &&
                  workspace.invokeCalls == workspaceInvokesBeforeOverride && service.GetStatus().lastContext == localId,
              "Unsupported Override invocation stops without Workspace fallback");
        Check(service.PopContext(overrideLocal, &failure), "Unsupported Override Local context pops");

        local.state = editor::EditorCommandState::Disabled();
        editor::EditorCommandContextToken disabledLocal;
        Check(service.PushContext(localId, &localReceiverA, editor::EditorCommandContextMode::Inherit, disabledLocal, &failure),
              "Disabled Local context pushes");
        Check(service.Query(commandId, state, &failure) && state.kind == editor::EditorCommandStateKind::Disabled &&
                  service.GetStatus().lastContext == localId,
              "Disabled Local receiver owns the command");
        const u32 workspaceInvokesBeforeDisabled = workspace.invokeCalls;
        Check(!service.Invoke(commandId, &failure) && failure.code == editor::EditorCommandFailureCode::CommandDisabled &&
                  workspace.invokeCalls == workspaceInvokesBeforeDisabled,
              "Disabled Local receiver never falls through to Workspace invocation");
        Check(service.PopContext(disabledLocal, &failure), "Disabled Local context pops");
        local.state = editor::EditorCommandState::Enabled();

        editor::EditorCommandContextToken barrier;
        Check(service.PushContext(barrierId, &localReceiverB, editor::EditorCommandContextMode::Override, barrier, &failure),
              "handlerless Override barrier pushes");
        Check(service.Query(commandId, state, &failure) && state.kind == editor::EditorCommandStateKind::Unsupported &&
                  service.GetStatus().lastContext == barrierId,
              "handlerless Override barrier reports its own selected context");
        Check(!Registry::UnregisterContextType(service, barrierTypeToken, &failure) && failure.code == editor::EditorCommandFailureCode::InUse,
              "active context prevents its type from unregistering");
        Check(service.PopContext(barrier, &failure), "handlerless Override barrier pops");

        local.tryContextMutation = true;
        local.mutationContext = barrierId;
        local.contextMutationAccepted = true;
        local.contextMutationFailure = editor::EditorCommandFailureCode::None;
        editor::EditorCommandContextToken mutationLocal;
        Check(service.PushContext(localId, &localReceiverA, editor::EditorCommandContextMode::Inherit, mutationLocal, &failure),
              "callback-mutation Local context pushes");
        Check(service.Query(commandId, state, &failure) && !local.contextMutationAccepted &&
                  local.contextMutationFailure == editor::EditorCommandFailureCode::RecursiveDispatch,
              "context mutation from a query callback fails closed");
        Check(service.PopContext(mutationLocal, &failure), "callback-mutation Local context pops");
        local.tryContextMutation = false;

        editor::EditorCommandContextToken staleActive;
        editor::EditorCommandContextToken freshActive;
        Check(service.PushContext(barrierId, nullptr, editor::EditorCommandContextMode::Inherit, staleActive, &failure) &&
                  service.PopContext(staleActive, &failure) &&
                  service.PushContext(barrierId, nullptr, editor::EditorCommandContextMode::Inherit, freshActive, &failure) &&
                  freshActive.index == staleActive.index && freshActive.generation != staleActive.generation,
              "active-context slot reuse advances its non-zero generation");
        Check(!service.PopContext(staleActive, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidToken,
              "stale active-context token cannot pop a reused slot");
        Check(service.PopContext(freshActive, &failure), "fresh active-context token pops reused slot");

        Check(!Registry::UnregisterContextType(service, localTypeToken, &failure) && failure.code == editor::EditorCommandFailureCode::InUse,
              "live handler prevents context-type unregister");
        Check(Registry::UnregisterHandler(service, controlHandler, &failure) && Registry::UnregisterHandler(service, localHandler, &failure) &&
                  Registry::UnregisterHandler(service, workspaceHandler, &failure),
              "context-routing handlers unregister");
        Check(Registry::UnregisterCommand(service, commandToken, &failure), "context-routing command unregisters");

        Check(Registry::UnregisterContextType(service, barrierTypeToken, &failure), "barrier context type unregisters");
        editor::EditorCommandContextTypeToken freshBarrierType;
        Check(Registry::RegisterContextType(service, ContextType(barrierId.value, "barrierFresh", editor::EditorCommandContextTier::Local),
                                          freshBarrierType, &failure) &&
                  freshBarrierType.index == barrierTypeToken.index && freshBarrierType.generation != barrierTypeToken.generation,
              "context-type slot reuse advances its non-zero generation");
        Check(!Registry::UnregisterContextType(service, barrierTypeToken, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidToken,
              "stale context-type token cannot remove a reused slot");
        Check(Registry::UnregisterContextType(service, freshBarrierType, &failure) && Registry::UnregisterContextType(service, controlTypeToken, &failure) &&
                  Registry::UnregisterContextType(service, localTypeToken, &failure),
              "fresh context types unregister cleanly");
        Check(service.GetStatus().registeredContextCount == 1 && service.GetStatus().activeContextCount == 0,
              "routing fixture returns to the built-in Workspace context only");
    }

    void TestContextCapacity(editor::EditorCommandService& service) noexcept
    {
        constexpr u32 callerCapacity = editor::MaximumEditorCommandContextTypes - 1u;
        editor::EditorCommandContextTypeToken tokens[callerCapacity]{};
        editor::EditorCommandFailure failure;
        bool registeredAll = true;
        for (u32 index = 0; index < callerCapacity; ++index)
            if (!Registry::RegisterContextType(service, ContextType(0x3000u + index, "contextCapacity", editor::EditorCommandContextTier::Local),
                                             tokens[index], &failure))
            {
                registeredAll = false;
                break;
            }
        Check(registeredAll && service.GetStatus().registeredContextCount == editor::MaximumEditorCommandContextTypes,
              "Workspace plus 255 caller context types reaches the exact 256-context bound");
        editor::EditorCommandContextTypeToken overflow;
        Check(!Registry::RegisterContextType(service, ContextType(0x4000u, "contextOverflow", editor::EditorCommandContextTier::ControlTool),
                                           overflow, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::CapacityExceeded && !overflow.IsValid() &&
                  service.GetStatus().registeredContextCount == editor::MaximumEditorCommandContextTypes,
              "257th total context type fails without partial state");
        bool removedAll = true;
        for (u32 index = callerCapacity; index > 0; --index)
            if (!Registry::UnregisterContextType(service, tokens[index - 1u], &failure))
                removedAll = false;
        Check(removedAll && service.GetStatus().registeredContextCount == 1,
              "maximum context-type fixture releases all caller-owned types");
    }

    void TestActiveContextCapacity(editor::EditorCommandService& service) noexcept
    {
        editor::EditorCommandFailure failure;
        constexpr editor::EditorCommandContextTypeId contextId{0x4100u};
        editor::EditorCommandContextTypeToken typeToken;
        Check(Registry::RegisterContextType(service, ContextType(contextId.value, "activeCapacity", editor::EditorCommandContextTier::Local), typeToken, &failure),
              "active-capacity context type registers");
        editor::EditorCommandContextToken tokens[editor::MaximumActiveEditorCommandContexts]{};
        bool pushedAll = true;
        for (u32 index = 0; index < editor::MaximumActiveEditorCommandContexts; ++index)
            if (!service.PushContext(contextId, nullptr, editor::EditorCommandContextMode::Inherit, tokens[index], &failure))
            {
                pushedAll = false;
                break;
            }
        Check(pushedAll && service.GetStatus().activeContextCount == editor::MaximumActiveEditorCommandContexts,
              "exact 64 active-context bound is admitted");
        editor::EditorCommandContextToken overflow;
        Check(!service.PushContext(contextId, nullptr, editor::EditorCommandContextMode::Inherit, overflow, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::CapacityExceeded && !overflow.IsValid() &&
                  service.GetStatus().activeContextCount == editor::MaximumActiveEditorCommandContexts,
              "65th active context fails without partial state");
        bool poppedAll = true;
        for (u32 index = editor::MaximumActiveEditorCommandContexts; index > 0; --index)
            if (!service.PopContext(tokens[index - 1u], &failure))
                poppedAll = false;
        Check(poppedAll && service.GetStatus().activeContextCount == 0,
              "maximum active-context fixture releases in exact reverse order");
        Check(Registry::UnregisterContextType(service, typeToken, &failure), "active-capacity context type unregisters");
    }

    void TestHandlerCapacity(editor::EditorCommandService& service) noexcept
    {
        editor::EditorCommandFailure failure;
        constexpr editor::EditorCommandContextTypeId firstContext{0x4201u};
        constexpr editor::EditorCommandContextTypeId secondContext{0x4202u};
        constexpr editor::EditorCommandContextTypeId overflowContext{0x4203u};
        editor::EditorCommandContextTypeToken firstContextToken;
        editor::EditorCommandContextTypeToken secondContextToken;
        editor::EditorCommandContextTypeToken overflowContextToken;
        Check(Registry::RegisterContextType(service, ContextType(firstContext.value, "handlersA", editor::EditorCommandContextTier::Local),
                                          firstContextToken, &failure) &&
                  Registry::RegisterContextType(service, ContextType(secondContext.value, "handlersB", editor::EditorCommandContextTier::ControlTool),
                                              secondContextToken, &failure) &&
                  Registry::RegisterContextType(service, ContextType(overflowContext.value, "handlersOverflow", editor::EditorCommandContextTier::Local),
                                              overflowContextToken, &failure),
              "handler-capacity context types register");

        editor::EditorCommandToken commands[editor::MaximumEditorCommands]{};
        bool registeredCommands = true;
        for (u32 index = 0; index < editor::MaximumEditorCommands; ++index)
            if (!Registry::RegisterCommand(service, Command(0x5000u + index, "handlerCapacity"), commands[index], &failure))
            {
                registeredCommands = false;
                break;
            }
        Check(registeredCommands, "handler-capacity fixture registers 512 commands");

        HandlerFixture handler;
        editor::EditorCommandHandlerToken handlers[editor::MaximumEditorCommandHandlers]{};
        u32 admitted = 0;
        for (u32 index = 0; index < editor::MaximumEditorCommands; ++index)
            if (Registry::RegisterHandler(service, Handler(firstContext, {0x5000u + index}, handler), handlers[admitted], &failure))
                ++admitted;
        for (u32 index = 0; index < editor::MaximumEditorCommands; ++index)
            if (Registry::RegisterHandler(service, Handler(secondContext, {0x5000u + index}, handler), handlers[admitted], &failure))
                ++admitted;
        Check(admitted == editor::MaximumEditorCommandHandlers &&
                  service.GetStatus().registeredHandlerCount == editor::MaximumEditorCommandHandlers,
              "reaches the exact 1024-handler physical bound");
        editor::EditorCommandHandlerToken overflowHandler;
        Check(!Registry::RegisterHandler(service, Handler(overflowContext, {0x5000u}, handler), overflowHandler, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::CapacityExceeded && !overflowHandler.IsValid() &&
                  service.GetStatus().registeredHandlerCount == editor::MaximumEditorCommandHandlers,
              "1025th handler fails without partial state");

        bool removedHandlers = true;
        for (u32 index = admitted; index > 0; --index)
            if (!Registry::UnregisterHandler(service, handlers[index - 1u], &failure))
                removedHandlers = false;
        Check(removedHandlers && service.GetStatus().registeredHandlerCount == 0,
              "maximum handler-capacity fixture releases every handler");
        bool removedCommands = true;
        for (u32 index = editor::MaximumEditorCommands; index > 0; --index)
            if (!Registry::UnregisterCommand(service, commands[index - 1u], &failure))
                removedCommands = false;
        Check(removedCommands && service.GetStatus().registeredCommandCount == 0,
              "handler-capacity fixture releases every command");
        Check(Registry::UnregisterContextType(service, overflowContextToken, &failure) &&
                  Registry::UnregisterContextType(service, secondContextToken, &failure) &&
                  Registry::UnregisterContextType(service, firstContextToken, &failure),
              "handler-capacity context types unregister");
    }

    void TestWrongThread(editor::EditorCommandService& service) noexcept
    {
        editor::EditorCommandFailure failure;
        editor::EditorCommandToken token;
        const editor::EditorCommandId id{40};
        Check(Registry::RegisterCommand(service, Command(id.value, "wrongThread"), token, &failure), "wrong-thread command registers on main thread");
        const editor::EditorCommandServiceStatus before = service.GetStatus();
        WrongThreadQueryWorker worker(service, id);
        worker.InitThread();
        worker.JoinThread();
        Check(!worker.Result() && worker.Failure() == editor::EditorCommandFailureCode::WrongThread,
              "worker-thread direct query is rejected before callback routing");
        const editor::EditorCommandServiceStatus after = service.GetStatus();
        Check(after.lastRoute == before.lastRoute && after.lastCommand == before.lastCommand && after.failure.code == before.failure.code,
              "wrong-thread rejection does not race-write main-thread command status");
        Check(Registry::UnregisterCommand(service, token, &failure), "wrong-thread fixture command cleans up on main thread");
    }

    [[nodiscard]] input::RawEvent KeyEvent(const input::Key key, const bool pressed = true, const bool repeated = false) noexcept
    {
        input::RawEvent event;
        event.type = input::EventType::KeyChanged;
        event.deviceType = input::DeviceType::Keyboard;
        event.device = 1u;
        event.data.key = {key, pressed, repeated};
        return event;
    }

    [[nodiscard]] input::RawEvent FocusLostEvent() noexcept
    {
        input::RawEvent event;
        event.type = input::EventType::FocusLost;
        event.deviceType = input::DeviceType::Keyboard;
        event.device = 1u;
        return event;
    }

    struct DownstreamInputProof final
    {
        engine::InputService* input = nullptr;
        editor::EditorUiService* ui = nullptr;
        editor::EditorCommandService* commands = nullptr;
        u64 lastFrame = 0;
        u32 claimedSkipped = 0;
        u32 captureSkipped = 0;
        u32 visibleKeyEvents = 0;
        bool orderingObserved = false;
    };

    engine::FrameParticipantStatus ExecuteDownstreamInput(const engine::FrameContext& context, void* const userData) noexcept
    {
        auto* const proof = static_cast<DownstreamInputProof*>(userData);
        if (proof == nullptr || proof->input == nullptr || proof->ui == nullptr || proof->commands == nullptr)
            return engine::FrameParticipantStatus::Failure("downstream fixture dependencies are unavailable");
        proof->lastFrame = context.frame;
        const editor::EditorUiInputCapture capture = proof->ui->GetInputCapture();
        proof->orderingObserved = proof->orderingObserved ||
                                  (proof->input->GetSnapshot().frame == context.frame && capture.frame == context.frame &&
                                   proof->commands->GetStatus().currentFrame == context.frame);
        const bool uiCaptured = capture.frame == context.frame && (capture.keyboardCaptured || capture.textInputCaptured);
        const auto events = proof->input->GetEvents();
        for (u32 index = 0; index < events.Size(); ++index)
        {
            if (events[index].type != input::EventType::KeyChanged)
                continue;
            if (uiCaptured)
            {
                ++proof->captureSkipped;
                continue;
            }
            if (proof->commands->IsInputEventClaimed(context.frame, index))
            {
                ++proof->claimedSkipped;
                continue;
            }
            ++proof->visibleKeyEvents;
        }
        return engine::FrameParticipantStatus::Success();
    }

    void TestShortcutFrameRouting(CommandHostFixture& fixture, editor::EditorCommandService& service) noexcept
    {
        engine::FramePipelineService* const frames = engine::FindFramePipelineService(fixture.host);
        engine::InputService* const inputService = engine::FindInputService(fixture.host);
        auto* const uiService = static_cast<editor::EditorUiService*>(fixture.host.FindCapability(editor::EditorUiCapabilityId));
        Check(frames != nullptr && inputService != nullptr && uiService != nullptr,
              "focused fixture exposes real FramePipeline/Input and typed toolkit-neutral UI service");
        if (frames == nullptr || inputService == nullptr || uiService == nullptr)
            return;

        DownstreamInputProof downstream{inputService, uiService, &service};
        constexpr engine::FrameParticipantId downstreamId = 0x65647035646f776eull;
        constexpr engine::FrameParticipantId downstreamDependencies[]{editor::EditorCommandShortcutFrameParticipantId};
        engine::FrameParticipantDescriptor downstreamDescriptor;
        downstreamDescriptor.id = downstreamId;
        downstreamDescriptor.name = "editorCommandTests.downstream";
        downstreamDescriptor.phase = engine::FramePhase::BeginFrame;
        downstreamDescriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        downstreamDescriptor.affinity = app::ThreadAffinity::MainThread;
        downstreamDescriptor.after = {downstreamDependencies, 1};
        downstreamDescriptor.execute = ExecuteDownstreamInput;
        downstreamDescriptor.userData = &downstream;
        engine::FrameFailure frameFailure;
        Check(frames->RegisterParticipant(downstreamDescriptor, &frameFailure),
              "downstream editor input consumer registers after shortcut routing");
        Check(frames->Compile(&frameFailure), "focused frame schedule compiles Input -> UI -> shortcuts -> downstream");
        if (frames->GetState() != engine::FramePipelineState::Compiled)
            return;

        auto runFrame = [&]() noexcept
        {
            frameFailure = {};
            const bool result = frames->RunFrame(&frameFailure);
            Check(result, frameFailure.message != nullptr ? frameFailure.message : "focused frame executes");
            return result;
        };
        auto push = [&](const input::RawEvent& event) noexcept
        {
            Check(fixture.inputBackend.Push(event), "deterministic input backend admits event");
        };
        auto pushChord = [&](const input::Key key, const bool repeated = false) noexcept
        {
            push(KeyEvent(input::Key::LeftControl, true));
            push(KeyEvent(key, true, repeated));
            push(KeyEvent(key, false));
            push(KeyEvent(input::Key::LeftControl, false));
        };

        editor::EditorCommandFailure failure;
        constexpr editor::EditorCommandId workspaceCommand{0xb501u};
        constexpr editor::EditorCommandId repeatCommand{0xb502u};
        constexpr editor::EditorCommandId localCommand{0xb503u};
        constexpr editor::EditorCommandContextTypeId localContext{0xb511u};
        constexpr editor::EditorCommandContextTypeId barrierContext{0xb512u};
        const editor::EditorShortcutChord ctrlS = Shortcut(input::Key::S, editor::EditorShortcutModifiers::Control);
        const editor::EditorShortcutChord ctrlR = Shortcut(input::Key::R, editor::EditorShortcutModifiers::Control);

        editor::EditorCommandToken workspaceToken;
        editor::EditorCommandToken repeatToken;
        editor::EditorCommandToken localCommandToken;
        editor::EditorCommandContextTypeToken localTypeToken;
        editor::EditorCommandContextTypeToken barrierTypeToken;
        HandlerFixture workspace;
        HandlerFixture repeat;
        HandlerFixture local;
        workspace.service = &service;
        repeat.service = &service;
        local.service = &service;
        editor::EditorCommandHandlerToken workspaceHandler;
        editor::EditorCommandHandlerToken repeatHandler;
        editor::EditorCommandHandlerToken localHandler;
        Check(Registry::RegisterCommand(service, Command(workspaceCommand.value, "shortcutWorkspace"), workspaceToken, &failure) &&
                  Registry::RegisterCommand(service, Command(repeatCommand.value, "shortcutRepeat", editor::EditorCommandFlags::AllowRepeat), repeatToken, &failure) &&
                  Registry::RegisterCommand(service, Command(localCommand.value, "shortcutLocal"), localCommandToken, &failure) &&
                  Registry::RegisterContextType(service, ContextType(localContext.value, "shortcutLocalContext", editor::EditorCommandContextTier::Local),
                                               localTypeToken, &failure) &&
                  Registry::RegisterContextType(service, ContextType(barrierContext.value, "shortcutBarrierContext", editor::EditorCommandContextTier::ControlTool),
                                               barrierTypeToken, &failure) &&
                  Registry::RegisterHandler(service, Handler(workspaceCommand, workspace, ctrlS), workspaceHandler, &failure) &&
                  Registry::RegisterHandler(service, Handler(repeatCommand, repeat, ctrlR), repeatHandler, &failure) &&
                  Registry::RegisterHandler(service, Handler(localContext, localCommand, local, ctrlS), localHandler, &failure),
              "routing fixture registers workspace/local/repeat commands and disjoint-context shortcut reuse");

        fixture.ui.textInputCaptured = false;
        fixture.ui.keyboardCaptured = false;
        fixture.ui.captureMatchesFrame = true;
        const u32 workspaceQueryStart = workspace.queryCalls;
        const u32 workspaceInvokeStart = workspace.invokeCalls;
        pushChord(input::Key::S);
        Check(runFrame(), "same-frame modifier chord executes");
        const u64 modifierFrame = service.GetStatus().currentFrame;
        Check(workspace.queryCalls == workspaceQueryStart + 1u && workspace.invokeCalls == workspaceInvokeStart + 1u,
              "ordered raw traversal applies modifier transition before physical shortcut matching");
        Check(service.IsInputEventClaimed(modifierFrame, 1u) && !service.IsInputEventClaimed(modifierFrame, 0u) &&
                  downstream.claimedSkipped >= 1u && downstream.orderingObserved,
              "command claim is frame/index tagged and downstream runs after Input/UI/shortcut routing");

        const u64 priorClaimFrame = modifierFrame;
        Check(runFrame(), "empty frame executes for claim reset");
        Check(!service.IsInputEventClaimed(priorClaimFrame, 1u), "claims are invalidated when the next frame begins");

        const u32 repeatDisallowedQueries = workspace.queryCalls;
        const u32 repeatDisallowedInvokes = workspace.invokeCalls;
        pushChord(input::Key::S, true);
        Check(runFrame(), "repeat-disallowed frame executes");
        Check(workspace.queryCalls == repeatDisallowedQueries && workspace.invokeCalls == repeatDisallowedInvokes &&
                  service.IsInputEventClaimed(service.GetStatus().currentFrame, 1u),
              "found repeat-disallowed binding claims/stops without Query or Invoke");

        const u32 repeatQueries = repeat.queryCalls;
        const u32 repeatInvokes = repeat.invokeCalls;
        pushChord(input::Key::R, true);
        Check(runFrame(), "repeat-enabled frame executes");
        Check(repeat.queryCalls == repeatQueries + 1u && repeat.invokeCalls == repeatInvokes + 1u,
              "AllowRepeat binding follows normal Query-before-Invoke path");

        const u32 beforeFocusLoss = workspace.invokeCalls;
        push(KeyEvent(input::Key::LeftControl, true));
        push(FocusLostEvent());
        push(KeyEvent(input::Key::S, true));
        push(KeyEvent(input::Key::S, false));
        Check(runFrame(), "focus-loss frame executes");
        Check(workspace.invokeCalls == beforeFocusLoss,
              "FocusLost clears logical modifiers before later events in the same immutable span");

        const u32 beforeKeyboardCapture = workspace.invokeCalls;
        const u32 captureSkippedBefore = downstream.captureSkipped;
        fixture.ui.keyboardCaptured = true;
        pushChord(input::Key::S);
        Check(runFrame(), "keyboard-capture frame executes");
        Check(workspace.invokeCalls == beforeKeyboardCapture && downstream.captureSkipped > captureSkippedBefore &&
                  !service.IsInputEventClaimed(service.GetStatus().currentFrame, 1u),
              "current-frame UI keyboard capture suppresses shortcut dispatch without manufacturing command claims");

        fixture.ui.keyboardCaptured = false;
        fixture.ui.textInputCaptured = true;
        const u32 beforeTextCapture = workspace.invokeCalls;
        pushChord(input::Key::S);
        Check(runFrame(), "text/IME-capture frame executes");
        Check(workspace.invokeCalls == beforeTextCapture,
              "current-frame UI text/IME capture suppresses ordinary editor shortcuts");

        fixture.ui.textInputCaptured = false;
        fixture.ui.keyboardCaptured = true;
        fixture.ui.captureMatchesFrame = false;
        const u32 beforeStaleCapture = workspace.invokeCalls;
        pushChord(input::Key::S);
        Check(runFrame(), "capture-frame mismatch frame executes");
        Check(workspace.invokeCalls == beforeStaleCapture + 1u,
              "stale/mismatched UI capture cannot suppress a different frame's shortcut");
        fixture.ui.keyboardCaptured = false;
        fixture.ui.captureMatchesFrame = true;

        editor::EditorCommandContextToken barrier;
        Check(service.PushContext(barrierContext, nullptr, editor::EditorCommandContextMode::Inherit, barrier, &failure),
              "Inherit barrier fixture pushes");
        const u32 beforeInherit = workspace.invokeCalls;
        pushChord(input::Key::S);
        Check(runFrame() && workspace.invokeCalls == beforeInherit + 1u,
              "no-binding Inherit continues through ControlTool to lower Workspace binding");
        Check(service.PopContext(barrier, &failure), "Inherit barrier fixture pops");
        Check(service.PushContext(barrierContext, nullptr, editor::EditorCommandContextMode::Override, barrier, &failure),
              "Override barrier fixture pushes");
        const u32 beforeOverride = workspace.invokeCalls;
        pushChord(input::Key::S);
        Check(runFrame() && workspace.invokeCalls == beforeOverride && !service.IsInputEventClaimed(service.GetStatus().currentFrame, 1u),
              "no-binding Override stops lower dispatch without claiming the event");
        Check(service.PopContext(barrier, &failure), "Override barrier fixture pops");

        editor::EditorCommandContextToken localActive;
        int localReceiver = 55;
        Check(service.PushContext(localContext, &localReceiver, editor::EditorCommandContextMode::Inherit, localActive, &failure),
              "local shortcut context pushes");
        local.state = editor::EditorCommandState::Unsupported();
        const u32 unsupportedLocalQuery = local.queryCalls;
        const u32 unsupportedWorkspaceInvoke = workspace.invokeCalls;
        pushChord(input::Key::S);
        Check(runFrame() && local.queryCalls == unsupportedLocalQuery + 1u && local.invokeCalls == 0u &&
                  workspace.invokeCalls == unsupportedWorkspaceInvoke && service.IsInputEventClaimed(service.GetStatus().currentFrame, 1u),
              "found Unsupported local binding claims and shadows Workspace without invocation");

        local.state = editor::EditorCommandState::Disabled();
        const u32 disabledQuery = local.queryCalls;
        pushChord(input::Key::S);
        Check(runFrame() && local.queryCalls == disabledQuery + 1u && local.invokeCalls == 0u &&
                  workspace.invokeCalls == unsupportedWorkspaceInvoke,
              "found Disabled local binding claims/stops after one exact-handler query");

        local.state = editor::EditorCommandState::Enabled();
        const u32 enabledLocalInvoke = local.invokeCalls;
        pushChord(input::Key::S);
        Check(runFrame() && local.invokeCalls == enabledLocalInvoke + 1u && workspace.invokeCalls == unsupportedWorkspaceInvoke &&
                  local.lastReceiver == &localReceiver,
              "one raw key event invokes at most one highest-precedence enabled command with exact receiver data");
        Check(service.PopContext(localActive, &failure), "local shortcut context pops");

        const editor::EditorShortcutChord ctrlD = Shortcut(input::Key::D, editor::EditorShortcutModifiers::Control);
        Check(service.SetShortcutOverride(editor::EditorWorkspaceCommandContextTypeId, workspaceCommand,
                                          {editor::EditorShortcutOverrideMode::Custom, ctrlD}, &failure),
              "workspace shortcut changes to Custom for dispatch proof");
        const u32 beforeOldDefault = workspace.invokeCalls;
        pushChord(input::Key::S);
        Check(runFrame() && workspace.invokeCalls == beforeOldDefault,
              "Default chord stops dispatching after a Custom override");
        pushChord(input::Key::D);
        Check(runFrame() && workspace.invokeCalls == beforeOldDefault + 1u,
              "Custom override dispatches through the same command handler route");
        Check(service.SetShortcutOverride(editor::EditorWorkspaceCommandContextTypeId, workspaceCommand,
                                          {editor::EditorShortcutOverrideMode::Unbound, {}}, &failure),
              "workspace shortcut becomes explicitly Unbound");
        const u32 beforeUnbound = workspace.invokeCalls;
        pushChord(input::Key::D);
        Check(runFrame() && workspace.invokeCalls == beforeUnbound,
              "Unbound override removes actual shortcut dispatch");
        Check(service.SetShortcutOverride(editor::EditorWorkspaceCommandContextTypeId, workspaceCommand,
                                          {editor::EditorShortcutOverrideMode::Default, {}}, &failure),
              "workspace shortcut returns to Default");

        const u32 beforeDirectQuery = workspace.queryCalls;
        const u32 beforeDirectInvoke = workspace.invokeCalls;
        Check(service.Invoke(workspaceCommand, &failure) && workspace.queryCalls == beforeDirectQuery + 1u &&
                  workspace.invokeCalls == beforeDirectInvoke + 1u,
              "direct/UI-equivalent invocation shares the same exact query/invoke callback path used by shortcuts");

        constexpr editor::EditorExtensionOwner providerOwner{{0xb520u}, 301u};
        constexpr editor::EditorExtensionOwner consumerOwner{{0xb521u}, 301u};
        constexpr editor::EditorCommandId revokedCommand{0xb522u};
        const editor::EditorShortcutChord revokedShortcut = Shortcut(input::Key::P);
        editor::EditorCommandToken revokedCommandToken;
        editor::EditorCommandHandlerToken revokedHandlerToken;
        HandlerFixture revokedHandler;
        Check(Registry::RegisterOwnedCommand(service, providerOwner, Command(revokedCommand.value, "revokedShortcutProvider"), revokedCommandToken, &failure) &&
                  Registry::RegisterOwnedHandler(service, consumerOwner, Handler(revokedCommand, revokedHandler, revokedShortcut), revokedHandlerToken, &failure),
              "owner-revocation fixture registers provider command and dependent shortcut handler");
        Registry::RevokeCommandOwner(service, providerOwner);
        push(KeyEvent(input::Key::P, true));
        push(KeyEvent(input::Key::P, false));
        Check(runFrame() && revokedHandler.queryCalls == 0u && revokedHandler.invokeCalls == 0u &&
                  service.IsInputEventClaimed(service.GetStatus().currentFrame, 0u),
              "revoked provider command cannot make a dependent shortcut callback reachable and still shadows downstream");
        Registry::RevokeCommandOwner(service, consumerOwner);
        Check(Registry::UnregisterOwnedHandler(service, consumerOwner, revokedHandlerToken, &failure) &&
                  Registry::UnregisterOwnedCommand(service, providerOwner, revokedCommandToken, &failure),
              "owner-revocation shortcut fixture cleans up exact owned records");

        Check(Registry::UnregisterHandler(service, localHandler, &failure) && Registry::UnregisterHandler(service, repeatHandler, &failure) &&
                  Registry::UnregisterHandler(service, workspaceHandler, &failure) &&
                  Registry::UnregisterContextType(service, barrierTypeToken, &failure) && Registry::UnregisterContextType(service, localTypeToken, &failure) &&
                  Registry::UnregisterCommand(service, localCommandToken, &failure) && Registry::UnregisterCommand(service, repeatToken, &failure) &&
                  Registry::UnregisterCommand(service, workspaceToken, &failure),
              "focused shortcut routing fixture releases all registrations");
    }

    void TestShortcutModifierState() noexcept
    {
        editor::EditorShortcutModifierState modifiers;
        input::RawEvent event;
        event.type = input::EventType::KeyChanged;
        event.deviceType = input::DeviceType::Keyboard;

        event.data.key.key = input::Key::LeftControl;
        event.data.key.pressed = true;
        modifiers.Apply(event);
        Check(modifiers.GetModifiers() == editor::EditorShortcutModifiers::Control,
              "left Control contributes the collapsed logical Control modifier");

        event.data.key.key = input::Key::RightControl;
        modifiers.Apply(event);
        event.data.key.key = input::Key::LeftControl;
        event.data.key.pressed = false;
        modifiers.Apply(event);
        Check(modifiers.GetModifiers() == editor::EditorShortcutModifiers::Control,
              "releasing one physical Control side preserves logical Control while the other side remains down");

        event.data.key.key = input::Key::RightShift;
        event.data.key.pressed = true;
        modifiers.Apply(event);
        Check(modifiers.GetModifiers() ==
                  (editor::EditorShortcutModifiers::Control | editor::EditorShortcutModifiers::Shift),
              "ordered physical modifier transitions combine into the logical modifier mask");

        event.data.key.key = input::Key::A;
        event.data.key.pressed = true;
        modifiers.Apply(event);
        Check(modifiers.GetModifiers() ==
                  (editor::EditorShortcutModifiers::Control | editor::EditorShortcutModifiers::Shift),
              "non-modifier key transitions do not perturb tracked shortcut modifiers");

        event = {};
        event.type = input::EventType::FocusLost;
        modifiers.Apply(event);
        Check(modifiers.GetModifiers() == editor::EditorShortcutModifiers::None,
              "focus loss clears the command service's tracked shortcut modifiers");

        event = {};
        event.type = input::EventType::KeyChanged;
        event.deviceType = input::DeviceType::Keyboard;
        event.data.key.key = input::Key::RightControl;
        event.data.key.pressed = false;
        modifiers.Apply(event);
        Check(modifiers.GetModifiers() == editor::EditorShortcutModifiers::None,
              "stale modifier release after focus-loss reset cannot resurrect modifier state");
    }

    void TestShortcutBindingsAndOverrides(editor::EditorCommandService& service) noexcept
    {
        constexpr editor::EditorCommandContextTypeId localContext{0x9001u};
        constexpr editor::EditorCommandContextTypeId controlContext{0x9002u};
        constexpr editor::EditorCommandId localA{0x9101u};
        constexpr editor::EditorCommandId localB{0x9102u};
        constexpr editor::EditorCommandId controlRepeat{0x9103u};
        const editor::EditorShortcutChord ctrlS =
            Shortcut(input::Key::S, editor::EditorShortcutModifiers::Control);
        const editor::EditorShortcutChord ctrlShiftS =
            Shortcut(input::Key::S, editor::EditorShortcutModifiers::Control | editor::EditorShortcutModifiers::Shift);
        const editor::EditorShortcutChord altA =
            Shortcut(input::Key::A, editor::EditorShortcutModifiers::Alt);

        editor::EditorCommandFailure failure;
        editor::EditorCommandContextTypeToken localType;
        editor::EditorCommandContextTypeToken controlType;
        Check(Registry::RegisterContextType(service,
                                           ContextType(localContext.value, "shortcutLocal", editor::EditorCommandContextTier::Local),
                                           localType, &failure) &&
                  Registry::RegisterContextType(service,
                                               ContextType(controlContext.value, "shortcutControl",
                                                           editor::EditorCommandContextTier::ControlTool),
                                               controlType, &failure),
              "shortcut fixture registers disjoint Local and ControlTool context types");

        editor::EditorCommandToken commandA;
        editor::EditorCommandToken commandB;
        editor::EditorCommandToken commandRepeat;
        Check(Registry::RegisterCommand(service, Command(localA.value, "shortcutA"), commandA, &failure) &&
                  Registry::RegisterCommand(service, Command(localB.value, "shortcutB"), commandB, &failure) &&
                  Registry::RegisterCommand(service,
                                            Command(controlRepeat.value, "shortcutRepeat", editor::EditorCommandFlags::AllowRepeat),
                                            commandRepeat, &failure),
              "shortcut fixture registers commands including one repeat-enabled descriptor");

        HandlerFixture handlerA;
        HandlerFixture handlerB;
        HandlerFixture handlerControl;
        handlerA.service = &service;
        editor::EditorCommandHandlerToken tokenA;
        editor::EditorCommandHandlerToken tokenB;
        editor::EditorCommandHandlerToken tokenControl;

        editor::EditorCommandHandlerToken invalidDefault;
        Check(!Registry::RegisterHandler(
                  service,
                  Handler(localContext, localA, handlerA,
                          Shortcut(input::Key::Unknown, editor::EditorShortcutModifiers::Control)),
                  invalidDefault, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidShortcut &&
                  !invalidDefault.IsValid(),
              "default shortcut rejects Unknown primary with modifier state");
        Check(!Registry::RegisterHandler(
                  service,
                  Handler(localContext, localA, handlerA,
                          Shortcut(input::Key::RightAlt, editor::EditorShortcutModifiers::None)),
                  invalidDefault, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidShortcut &&
                  !invalidDefault.IsValid(),
              "default shortcut rejects modifier-only primary keys before slot admission");

        Check(Registry::RegisterHandler(service, Handler(localContext, localA, handlerA, ctrlS), tokenA, &failure),
              "handler registers a validated default physical shortcut");

        const u32 countBeforeConflict = service.GetStatus().registeredHandlerCount;
        editor::EditorCommandHandlerToken conflictToken;
        Check(!Registry::RegisterHandler(service, Handler(localContext, localB, handlerB, ctrlS), conflictToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::ShortcutConflict && !conflictToken.IsValid() &&
                  service.GetStatus().registeredHandlerCount == countBeforeConflict,
              "same-context default shortcut conflict fails transactionally without consuming a handler slot");

        Check(Registry::RegisterHandler(service, Handler(localContext, localB, handlerB, ctrlShiftS), tokenB, &failure),
              "second same-context handler registers with a distinct default chord");
        Check(Registry::RegisterHandler(service, Handler(controlContext, controlRepeat, handlerControl, ctrlS),
                                       tokenControl, &failure),
              "the identical chord is legal in a disjoint command context type");

        editor::EditorShortcutBinding binding;
        Check(service.GetShortcutBinding(localContext, localA, binding, &failure) &&
                  binding.mode == editor::EditorShortcutOverrideMode::Default && binding.bound &&
                  binding.chord == ctrlS && !binding.allowRepeat,
              "binding query exposes a non-repeat command's effective default shortcut");
        Check(service.GetShortcutBinding(controlContext, controlRepeat, binding, &failure) &&
                  binding.mode == editor::EditorShortcutOverrideMode::Default && binding.bound &&
                  binding.chord == ctrlS && binding.allowRepeat,
              "binding query preserves the command descriptor repeat policy");

        Check(!service.SetShortcutOverride(localContext, localA,
                                           {editor::EditorShortcutOverrideMode::Custom, ctrlShiftS}, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::ShortcutConflict,
              "conflicting Custom override fails without replacing the existing effective binding");
        Check(service.GetShortcutBinding(localContext, localA, binding, &failure) &&
                  binding.mode == editor::EditorShortcutOverrideMode::Default && binding.bound && binding.chord == ctrlS,
              "failed override leaves prior Default state unchanged");

        Check(service.SetShortcutOverride(localContext, localA,
                                         {editor::EditorShortcutOverrideMode::Unbound, {}}, &failure) &&
                  service.GetShortcutBinding(localContext, localA, binding, &failure) &&
                  binding.mode == editor::EditorShortcutOverrideMode::Unbound && !binding.bound,
              "Unbound override transactionally removes the handler's effective shortcut");
        Check(service.SetShortcutOverride(localContext, localB,
                                         {editor::EditorShortcutOverrideMode::Custom, ctrlS}, &failure),
              "another handler may claim a chord after the prior binding becomes Unbound");

        Check(!service.SetShortcutOverride(localContext, localA,
                                           {editor::EditorShortcutOverrideMode::Default, {}}, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::ShortcutConflict &&
                  service.GetShortcutBinding(localContext, localA, binding, &failure) &&
                  binding.mode == editor::EditorShortcutOverrideMode::Unbound && !binding.bound,
              "restoring Default also conflict-checks transactionally and preserves Unbound on failure");
        Check(service.SetShortcutOverride(localContext, localB,
                                         {editor::EditorShortcutOverrideMode::Default, {}}, &failure) &&
                  service.SetShortcutOverride(localContext, localA,
                                             {editor::EditorShortcutOverrideMode::Default, {}}, &failure),
              "Default mode clears Custom state and restores registered defaults once conflict-free");

        Check(service.SetShortcutOverride(localContext, localA,
                                         {editor::EditorShortcutOverrideMode::Custom, altA}, &failure) &&
                  service.GetShortcutBinding(localContext, localA, binding, &failure) &&
                  binding.mode == editor::EditorShortcutOverrideMode::Custom && binding.bound && binding.chord == altA,
              "Custom mode applies one validated replacement physical chord");

        Check(!service.SetShortcutOverride(localContext, localA,
                                           {editor::EditorShortcutOverrideMode::Custom, {}}, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidShortcut,
              "Custom override rejects an empty/Unknown primary key");
        Check(!service.SetShortcutOverride(
                  localContext, localA,
                  {editor::EditorShortcutOverrideMode::Custom,
                   Shortcut(input::Key::LeftControl, editor::EditorShortcutModifiers::None)},
                  &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidShortcut,
              "modifier-only primary keys are invalid shortcut chords");
        Check(!service.SetShortcutOverride(
                  localContext, localA,
                  {editor::EditorShortcutOverrideMode::Custom,
                   Shortcut(input::Key::A, static_cast<editor::EditorShortcutModifiers>(0x80u))},
                  &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidShortcut,
              "unknown modifier bits are rejected");
        Check(!service.SetShortcutOverride(
                  localContext, localA,
                  {editor::EditorShortcutOverrideMode::Custom,
                   Shortcut(static_cast<input::Key>(input::MaximumKeyboardKeys),
                            editor::EditorShortcutModifiers::None)},
                  &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidShortcut,
              "out-of-range physical key values are rejected");
        Check(!service.SetShortcutOverride(
                  localContext, localA,
                  {editor::EditorShortcutOverrideMode::Custom,
                   Shortcut(static_cast<input::Key>(300), editor::EditorShortcutModifiers::None)},
                  &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidShortcut,
              "undefined physical-key values inside the 512-bit keyboard storage domain are rejected");
        Check(!service.SetShortcutOverride(localContext, localA,
                                           {editor::EditorShortcutOverrideMode::Unbound, ctrlS}, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidShortcut,
              "Unbound requires an empty replacement chord and rejects hidden custom state");
        Check(!service.SetShortcutOverride(
                  localContext, localA,
                  {static_cast<editor::EditorShortcutOverrideMode>(0xffu), {}}, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidShortcut,
              "invalid override mode values fail closed");
        Check(service.GetShortcutBinding(localContext, localA, binding, &failure) &&
                  binding.mode == editor::EditorShortcutOverrideMode::Custom && binding.chord == altA,
              "every invalid override leaves the previous Custom binding unchanged");

        WrongThreadShortcutWorker wrongThread(service, localContext, localA);
        wrongThread.InitThread();
        wrongThread.JoinThread();
        Check(!wrongThread.Result() && wrongThread.Failure() == editor::EditorCommandFailureCode::WrongThread,
              "runtime override mutation is MainThread-owned");

        handlerA.tryShortcutMutation = true;
        handlerA.shortcutMutationContext = localContext;
        handlerA.shortcutMutationCommand = localA;
        editor::EditorCommandContextToken activeLocal;
        Check(service.PushContext(localContext, &handlerA, editor::EditorCommandContextMode::Inherit,
                                  activeLocal, &failure),
              "callback-mutation fixture activates the Local shortcut context");
        editor::EditorCommandState state;
        Check(service.Query(localA, state, &failure) && state.kind == editor::EditorCommandStateKind::Enabled &&
                  !handlerA.shortcutMutationAccepted &&
                  handlerA.shortcutMutationFailure == editor::EditorCommandFailureCode::RecursiveDispatch,
              "query callbacks cannot mutate runtime shortcut overrides reentrantly");
        Check(service.PopContext(activeLocal, &failure), "callback-mutation fixture pops its active Local context");
        handlerA.tryShortcutMutation = false;

        Check(Registry::UnregisterHandler(service, tokenA, &failure), "customized handler unregisters");
        editor::EditorCommandHandlerToken freshA;
        Check(Registry::RegisterHandler(service, Handler(localContext, localA, handlerA, ctrlS), freshA, &failure) &&
                  service.GetShortcutBinding(localContext, localA, binding, &failure) &&
                  binding.mode == editor::EditorShortcutOverrideMode::Default && binding.bound && binding.chord == ctrlS,
              "physical handler removal discards runtime override state instead of retaining hidden persistence");

        Check(Registry::UnregisterHandler(service, freshA, &failure) &&
                  Registry::UnregisterHandler(service, tokenB, &failure) &&
                  Registry::UnregisterHandler(service, tokenControl, &failure),
              "shortcut handlers unregister cleanly");
        Check(Registry::UnregisterCommand(service, commandRepeat, &failure) &&
                  Registry::UnregisterCommand(service, commandB, &failure) &&
                  Registry::UnregisterCommand(service, commandA, &failure),
              "shortcut commands unregister after their handlers");
        Check(Registry::UnregisterContextType(service, controlType, &failure) &&
                  Registry::UnregisterContextType(service, localType, &failure),
              "shortcut context types unregister after handlers are gone");
    }

    void TestFreshHostTokenGeneration() noexcept
    {
        editor::EditorCommandToken staleCommand;
        editor::EditorCommandHandlerToken staleHandler;
        editor::EditorCommandContextTypeToken staleContextType;
        editor::EditorCommandContextToken staleContext;
        constexpr editor::EditorCommandId commandId{60};
        constexpr editor::EditorCommandContextTypeId contextId{0x6001u};
        {
            LightweightCommandHostFixture first;
            Check(first.Compile(), "first fresh-token host compiles");
            editor::EditorCommandService* const service = first.Start();
            Check(service != nullptr, "first fresh-token host starts");
            if (service != nullptr)
            {
                editor::EditorCommandFailure failure;
                HandlerFixture handler;
                Check(Registry::RegisterCommand(*service, Command(commandId.value, "freshToken"), staleCommand, &failure),
                      "first host command token registers");
                Check(Registry::RegisterHandler(*service, Handler(commandId, handler), staleHandler, &failure),
                      "first host handler token registers");
                Check(Registry::RegisterContextType(*service, ContextType(contextId.value, "freshContext", editor::EditorCommandContextTier::Local),
                                                   staleContextType, &failure),
                      "first host context-type token registers");
                Check(service->PushContext(contextId, nullptr, editor::EditorCommandContextMode::Inherit, staleContext, &failure),
                      "first host active-context token pushes");
                Check(service->PopContext(staleContext, &failure) &&
                          Registry::UnregisterContextType(*service, staleContextType, &failure) &&
                          Registry::UnregisterHandler(*service, staleHandler, &failure) &&
                          Registry::UnregisterCommand(*service, staleCommand, &failure),
                      "first host releases registrations while retaining stale token values for the next host");
            }
            Check(first.host.Shutdown(&first.failure), "first fresh-token host shuts down cleanly");
        }

        {
            LightweightCommandHostFixture second;
            Check(second.Compile(), "second fresh-token host compiles");
            editor::EditorCommandService* const service = second.Start();
            Check(service != nullptr, "second fresh-token host starts");
            if (service != nullptr)
            {
                editor::EditorCommandFailure failure;
                editor::EditorCommandToken freshCommand;
                editor::EditorCommandHandlerToken freshHandler;
                editor::EditorCommandContextTypeToken freshContextType;
                editor::EditorCommandContextToken freshContext;
                HandlerFixture handler;
                Check(Registry::RegisterCommand(*service, Command(commandId.value, "freshToken"), freshCommand, &failure) &&
                          freshCommand.index == staleCommand.index && freshCommand.generation != staleCommand.generation,
                      "fresh host reuses the stable command slot only with a process-fresh generation");
                Check(Registry::RegisterHandler(*service, Handler(commandId, handler), freshHandler, &failure) &&
                          freshHandler.index == staleHandler.index && freshHandler.generation != staleHandler.generation,
                      "fresh host reuses the stable handler slot only with a process-fresh generation");
                Check(Registry::RegisterContextType(*service, ContextType(contextId.value, "freshContext", editor::EditorCommandContextTier::Local),
                                                   freshContextType, &failure) &&
                          freshContextType.index == staleContextType.index && freshContextType.generation != staleContextType.generation,
                      "fresh host reuses the context-type slot only with a process-fresh generation");
                Check(service->PushContext(contextId, nullptr, editor::EditorCommandContextMode::Inherit, freshContext, &failure) &&
                          freshContext.index == staleContext.index && freshContext.generation != staleContext.generation,
                      "fresh host reuses the active-context slot only with a process-fresh generation");
                Check(!Registry::UnregisterHandler(*service, staleHandler, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidToken,
                      "handler token from a prior host generation remains stale");
                Check(!Registry::UnregisterCommand(*service, staleCommand, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidToken,
                      "command token from a prior host generation remains stale");
                Check(!service->PopContext(staleContext, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidToken,
                      "active-context token from a prior host generation remains stale");
                Check(service->PopContext(freshContext, &failure), "fresh active-context token pops");
                Check(!Registry::UnregisterContextType(*service, staleContextType, &failure) &&
                          failure.code == editor::EditorCommandFailureCode::InvalidToken,
                      "context-type token from a prior host generation remains stale");
                Check(Registry::UnregisterContextType(*service, freshContextType, &failure) &&
                          Registry::UnregisterHandler(*service, freshHandler, &failure) && Registry::UnregisterCommand(*service, freshCommand, &failure),
                      "fresh-host token fixture cleans up with current tokens");
            }
            Check(second.host.Shutdown(&second.failure), "second fresh-token host shuts down");
        }
    }

    void TestExtensionOwnerRevocation(editor::EditorCommandService& service) noexcept
    {
        editor::EditorCommandFailure failure;

        constexpr editor::EditorExtensionOwner localOwner{{0xa001u}, 101u};
        constexpr editor::EditorExtensionOwner wrongOwner{{0xa002u}, 102u};
        constexpr editor::EditorExtensionOwner fallbackOwner{{0xa003u}, 103u};
        constexpr editor::EditorCommandId fallbackCommand{0x8100u};
        constexpr editor::EditorCommandContextTypeId localContext{0x8101u};

        editor::EditorCommandToken fallbackCommandToken;
        editor::EditorCommandHandlerToken workspaceHandlerToken;
        editor::EditorCommandContextTypeToken localContextToken;
        editor::EditorCommandHandlerToken localHandlerToken;
        HandlerFixture workspace;
        HandlerFixture local;
        int receiver = 17;

        Check(Registry::RegisterOwnedCommand(service, fallbackOwner, Command(fallbackCommand.value, "ownerFallback"), fallbackCommandToken, &failure),
              "owner fixture registers an independently owned fallback command");
        Check(Registry::RegisterOwnedHandler(service, fallbackOwner, Handler(fallbackCommand, workspace), workspaceHandlerToken, &failure),
              "owner fixture registers an independently owned Workspace handler");
        Check(Registry::RegisterOwnedContextType(service, localOwner,
                                                ContextType(localContext.value, "ownerLocal", editor::EditorCommandContextTier::Local),
                                                localContextToken, &failure),
              "extension owner registers a Local context type through the private owner seam");
        const editor::EditorShortcutChord ownerShortcut =
            Shortcut(input::Key::K, editor::EditorShortcutModifiers::Control);
        Check(Registry::RegisterOwnedHandler(service, localOwner,
                                            Handler(localContext, fallbackCommand, local, ownerShortcut),
                                            localHandlerToken, &failure),
              "extension owner registers a contextual handler through the private owner seam");
        editor::EditorShortcutBinding ownerBinding;
        Check(service.GetShortcutBinding(localContext, fallbackCommand, ownerBinding, &failure) &&
                  ownerBinding.bound && ownerBinding.chord == ownerShortcut,
              "extension-owned handlers expose their live default binding through the ordinary binding query");

        editor::EditorCommandContextToken activeContext;
        Check(service.PushContext(localContext, &receiver, editor::EditorCommandContextMode::Inherit, activeContext, &failure),
              "owned context type can be activated while its owner is live");
        editor::EditorCommandState state;
        Check(service.Query(fallbackCommand, state, &failure) && state.kind == editor::EditorCommandStateKind::Enabled &&
                  local.queryCalls == 1u && local.lastReceiver == &receiver && workspace.queryCalls == 0u,
              "live owner routes through its active Local receiver before Workspace fallback");

        Check(!Registry::UnregisterOwnedHandler(service, wrongOwner, localHandlerToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::OwnerRevoked,
              "physical removal rejects a valid token stamped with the wrong owner");

        Registry::RevokeCommandOwner(service, localOwner);
        Registry::RevokeCommandOwner(service, localOwner);
        Check(!service.GetShortcutBinding(localContext, fallbackCommand, ownerBinding, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::MissingContext,
              "owner revocation makes owned shortcut binding state unreachable before physical removal");
        Check(service.Query(fallbackCommand, state, &failure) && state.kind == editor::EditorCommandStateKind::Enabled &&
                  local.queryCalls == 1u && workspace.queryCalls == 1u,
              "logical owner revocation is idempotent and immediately removes owned callback reachability");
        Check(!service.PopContext(activeContext, &failure) && failure.code == editor::EditorCommandFailureCode::InvalidToken,
              "logical revoke invalidates active-context tokens owned by the revoked context type");
        editor::EditorCommandContextToken rejectedContext;
        Check(!service.PushContext(localContext, &receiver, editor::EditorCommandContextMode::Inherit, rejectedContext, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::MissingContext && !rejectedContext.IsValid(),
              "revoked context types cannot create new receiver borrows before physical removal");
        Check(Registry::UnregisterOwnedHandler(service, localOwner, localHandlerToken, &failure) &&
                  Registry::UnregisterOwnedContextType(service, localOwner, localContextToken, &failure),
              "exact owner tokens physically remove revoked handler/context records");
        Check(Registry::UnregisterOwnedHandler(service, fallbackOwner, workspaceHandlerToken, &failure) &&
                  Registry::UnregisterOwnedCommand(service, fallbackOwner, fallbackCommandToken, &failure),
              "fallback fixture releases its independently owned records");

        constexpr editor::EditorExtensionOwner providerOwner{{0xa010u}, 201u};
        constexpr editor::EditorExtensionOwner consumerOwner{{0xa011u}, 201u};
        constexpr editor::EditorExtensionOwner unrelatedOwner{{0xa012u}, 201u};
        constexpr editor::EditorCommandId providerCommand{0x8200u};
        constexpr editor::EditorCommandContextTypeId providerContext{0x8201u};
        editor::EditorCommandToken providerCommandToken;
        editor::EditorCommandContextTypeToken providerContextToken;
        editor::EditorCommandHandlerToken consumerHandlerToken;
        HandlerFixture consumer;

        Check(Registry::RegisterOwnedCommand(service, providerOwner, Command(providerCommand.value, "providerCommand"),
                                            providerCommandToken, &failure) &&
                  Registry::RegisterOwnedContextType(service, providerOwner,
                                                    ContextType(providerContext.value, "providerContext",
                                                                editor::EditorCommandContextTier::Local),
                                                    providerContextToken, &failure),
              "provider owner registers command and context records");
        Check(Registry::RegisterOwnedHandler(service, consumerOwner, Handler(providerContext, providerCommand, consumer),
                                            consumerHandlerToken, &failure),
              "dependent owner may register a handler against a live provider owner");

        Registry::RevokeCommandOwner(service, providerOwner);
        Check(!Registry::UnregisterOwnedCommand(service, unrelatedOwner, providerCommandToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::OwnerRevoked,
              "physical command cleanup verifies the exact stamped owner even after logical revoke");
        Check(!Registry::UnregisterOwnedContextType(service, providerOwner, providerContextToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InUse,
              "provider context physical removal fails closed while another owner still has a live handler");
        Check(!Registry::UnregisterOwnedCommand(service, providerOwner, providerCommandToken, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InUse,
              "provider command physical removal fails closed while another owner still has a live handler");
        Check(!service.Query(providerCommand, state, &failure) && failure.code == editor::EditorCommandFailureCode::MissingCommand &&
                  consumer.queryCalls == 0u,
              "failed physical cleanup never restores a revoked provider or dependent callback path");

        Registry::RevokeCommandOwner(service, consumerOwner);
        Check(Registry::UnregisterOwnedHandler(service, consumerOwner, consumerHandlerToken, &failure) &&
                  Registry::UnregisterOwnedContextType(service, providerOwner, providerContextToken, &failure) &&
                  Registry::UnregisterOwnedCommand(service, providerOwner, providerCommandToken, &failure),
              "reverse dependent-first physical cleanup resolves the cross-owner dependency safely");
    }

    void TestPanelLocalContextValidation(editor::EditorCommandService& service) noexcept
    {
        constexpr editor::EditorExtensionOwner owner{{0xa100u}, 301u};
        constexpr editor::EditorExtensionOwner otherOwner{{0xa101u}, 301u};
        constexpr editor::EditorCommandContextTypeId localContext{0x8301u};
        constexpr editor::EditorCommandContextTypeId controlContext{0x8302u};
        editor::EditorCommandFailure failure;
        editor::EditorCommandContextTypeToken localToken;
        editor::EditorCommandContextTypeToken controlToken;

        Check(Registry::RegisterOwnedContextType(
                  service, owner, ContextType(localContext.value, "panelLocal", editor::EditorCommandContextTier::Local),
                  localToken, &failure) &&
                  Registry::RegisterOwnedContextType(
                      service, owner, ContextType(controlContext.value, "panelControl", editor::EditorCommandContextTier::ControlTool),
                      controlToken, &failure),
              "panel focus fixture registers owned Local and ControlTool context types");
        Check(Registry::ValidateOwnedLocalContext(service, owner, localContext, &failure),
              "panel Local association accepts a live Local context owned by the same extension");
        Check(!Registry::ValidateOwnedLocalContext(service, otherOwner, localContext, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::OwnerRevoked,
              "panel Local association rejects cross-owner context borrowing");
        Check(!Registry::ValidateOwnedLocalContext(service, owner, controlContext, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::InvalidDescriptor,
              "panel Local association rejects a ControlTool context type");

        Registry::RevokeCommandOwner(service, owner);
        Check(!Registry::ValidateOwnedLocalContext(service, owner, localContext, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::MissingContext,
              "panel Local association rejects a logically revoked context");
        Check(Registry::UnregisterOwnedContextType(service, owner, controlToken, &failure) &&
                  Registry::UnregisterOwnedContextType(service, owner, localToken, &failure),
              "context-validation fixture physically cleans up revoked context records");
    }

    void TestSurfaceCommandOwnershipValidation(editor::EditorCommandService& service) noexcept
    {
        constexpr editor::EditorExtensionOwner owner{{0xa200u}, 401u};
        constexpr editor::EditorExtensionOwner otherOwner{{0xa201u}, 401u};
        constexpr editor::EditorCommandId command{0x8401u};
        constexpr editor::EditorCommandId missing{0x8402u};
        editor::EditorCommandFailure failure;
        editor::EditorCommandToken token;

        Check(Registry::RegisterOwnedCommand(service, owner, Command(command.value, "surfaceOwnedCommand"), token, &failure),
              "surface command-validation fixture registers an owned command");
        Check(Registry::ValidateOwnedCommand(service, owner, command, &failure),
              "surface command validation accepts a reachable command owned by the same extension");
        Check(!Registry::ValidateOwnedCommand(service, otherOwner, command, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::OwnerRevoked,
              "surface command validation rejects cross-owner command borrowing");
        Check(!Registry::ValidateOwnedCommand(service, owner, missing, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::MissingCommand,
              "surface command validation rejects a missing command");

        Registry::RevokeCommandOwner(service, owner);
        Check(!Registry::ValidateOwnedCommand(service, owner, command, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::MissingCommand,
              "surface command validation rejects a logically revoked command");
        Check(Registry::UnregisterOwnedCommand(service, owner, token, &failure),
              "surface command-validation fixture physically cleans up the revoked command record");
    }

    void TestSurfaceCommandPresentation(editor::EditorCommandService& service) noexcept
    {
        constexpr editor::EditorCommandId routedCommand{0x8501u};
        constexpr editor::EditorCommandId overrideUnsupportedCommand{0x8502u};
        constexpr editor::EditorCommandContextTypeId localContext{0x8503u};
        constexpr editor::EditorShortcutChord workspaceShortcut{input::Key::S, editor::EditorShortcutModifiers::Control};
        constexpr editor::EditorShortcutChord localShortcut{input::Key::A, editor::EditorShortcutModifiers::Alt};
        constexpr editor::EditorShortcutChord fallbackShortcut{input::Key::D, editor::EditorShortcutModifiers::Control};

        editor::EditorCommandFailure failure;
        editor::EditorCommandToken routedToken;
        editor::EditorCommandToken unsupportedToken;
        editor::EditorCommandContextTypeToken contextToken;
        editor::EditorCommandHandlerToken workspaceHandlerToken;
        editor::EditorCommandHandlerToken localHandlerToken;
        editor::EditorCommandHandlerToken fallbackHandlerToken;
        HandlerFixture workspace;
        HandlerFixture local;
        HandlerFixture fallback;
        workspace.state = editor::EditorCommandState::Enabled();
        local.state = editor::EditorCommandState::Enabled(true);
        fallback.state = editor::EditorCommandState::Enabled();

        Check(Registry::RegisterCommand(service, Command(routedCommand.value, "surfacePresentation",
                                                        editor::EditorCommandFlags::AllowRepeat),
                                        routedToken, &failure) &&
                  Registry::RegisterCommand(service, Command(overrideUnsupportedCommand.value, "surfaceOverrideUnsupported"),
                                            unsupportedToken, &failure) &&
                  Registry::RegisterContextType(
                      service, ContextType(localContext.value, "surfacePresentationLocal", editor::EditorCommandContextTier::Local),
                      contextToken, &failure) &&
                  Registry::RegisterHandler(service, Handler(routedCommand, workspace, workspaceShortcut),
                                            workspaceHandlerToken, &failure) &&
                  Registry::RegisterHandler(service, Handler(localContext, routedCommand, local, localShortcut),
                                            localHandlerToken, &failure) &&
                  Registry::RegisterHandler(service, Handler(overrideUnsupportedCommand, fallback, fallbackShortcut),
                                            fallbackHandlerToken, &failure),
              "surface presentation fixture registers commands, Local context and disjoint route handlers");

        int localReceiver = 7;
        editor::EditorCommandContextToken active;
        Check(service.PushContext(localContext, &localReceiver, editor::EditorCommandContextMode::Inherit, active, &failure),
              "surface presentation fixture activates the Local receiver");

        const u32 workspaceQueries = workspace.queryCalls;
        const u32 localQueries = local.queryCalls;
        editor::EditorCommandPresentation presentation;
        Check(Registry::QueryCommandPresentation(service, routedCommand, presentation, &failure) &&
                  local.queryCalls == localQueries + 1u && workspace.queryCalls == workspaceQueries &&
                  local.lastReceiver == &localReceiver &&
                  presentation.state.kind == editor::EditorCommandStateKind::Enabled && presentation.state.checked &&
                  presentation.binding.bound && presentation.binding.chord == localShortcut &&
                  presentation.binding.allowRepeat,
              "surface presentation resolves once and returns state plus shortcut from the same selected Local handler");

        Check(service.PopContext(active, &failure), "surface presentation Local receiver pops before override proof");
        active = {};
        Check(service.PushContext(localContext, &localReceiver, editor::EditorCommandContextMode::Override, active, &failure),
              "surface presentation fixture activates an overriding Local receiver");
        const u32 fallbackQueries = fallback.queryCalls;
        presentation = {};
        Check(Registry::QueryCommandPresentation(service, overrideUnsupportedCommand, presentation, &failure) &&
                  presentation.state.kind == editor::EditorCommandStateKind::Unsupported &&
                  !presentation.binding.bound && presentation.binding.chord.IsEmpty() &&
                  fallback.queryCalls == fallbackQueries,
              "surface presentation keeps a valid Unsupported no-handler override unbound without workspace fallthrough");

        Check(service.PopContext(active, &failure), "surface presentation override receiver pops");
        Check(Registry::UnregisterHandler(service, fallbackHandlerToken, &failure) &&
                  Registry::UnregisterHandler(service, localHandlerToken, &failure) &&
                  Registry::UnregisterHandler(service, workspaceHandlerToken, &failure) &&
                  Registry::UnregisterContextType(service, contextToken, &failure) &&
                  Registry::UnregisterCommand(service, unsupportedToken, &failure) &&
                  Registry::UnregisterCommand(service, routedToken, &failure),
              "surface presentation fixture cleans up exact registrations in dependency order");
    }

    void TestShortcutPreferencePersistence(editor::EditorCommandService& service) noexcept
    {
        auto& persistence =
            static_cast<editor::detail::EditorCommandPersistencePrivateService&>(service);
        editor::EditorCommandFailure failure;
        constexpr editor::EditorCommandContextTypeId context{0xd610u};
        constexpr editor::EditorCommandContextTypeId secondContext{0xd614u};
        constexpr editor::EditorCommandId first{0xd611u};
        constexpr editor::EditorCommandId second{0xd612u};
        constexpr editor::EditorCommandId missingHandler{0xd613u};

        editor::EditorCommandContextTypeToken contextToken, secondContextToken;
        editor::EditorCommandToken firstToken, secondToken, missingToken;
        HandlerFixture firstFixture, secondFixture, secondContextFixture, missingFixture;
        editor::EditorCommandHandlerToken firstHandler, secondHandler, secondContextHandler;
        Check(Registry::RegisterContextType(
                  service, ContextType(context.value, "persistContext",
                                       editor::EditorCommandContextTier::Local),
                  contextToken, &failure) &&
                  Registry::RegisterContextType(
                      service, ContextType(secondContext.value, "persistSecondContext",
                                           editor::EditorCommandContextTier::Local),
                      secondContextToken, &failure) &&
                  Registry::RegisterCommand(service, Command(first.value, "persistFirst"),
                                            firstToken, &failure) &&
                  Registry::RegisterCommand(service, Command(second.value, "persistSecond"),
                                            secondToken, &failure) &&
                  Registry::RegisterCommand(service, Command(missingHandler.value, "persistMissing"),
                                            missingToken, &failure) &&
                  Registry::RegisterHandler(
                      service, Handler(context, first, firstFixture,
                                       Shortcut(input::Key::A, editor::EditorShortcutModifiers::Alt)),
                      firstHandler, &failure) &&
                  Registry::RegisterHandler(
                      service, Handler(context, second, secondFixture,
                                       Shortcut(input::Key::B, editor::EditorShortcutModifiers::Alt)),
                      secondHandler, &failure) &&
                  Registry::RegisterHandler(
                      service, Handler(secondContext, first, secondContextFixture,
                                       Shortcut(input::Key::D, editor::EditorShortcutModifiers::Alt)),
                      secondContextHandler, &failure),
              "shortcut preference fixture registers two contexts, live handlers and one handler-missing command");

        const u64 baseline = persistence.GetShortcutPreferenceRevision();
        Check(baseline != 0 &&
                  service.SetShortcutOverride(context, first,
                                             {editor::EditorShortcutOverrideMode::Default, {}}, &failure) &&
                  persistence.GetShortcutPreferenceRevision() == baseline,
              "shortcut preference revision starts nonzero and an exact stored-override no-op does not advance it");
        editor::EditorShortcutPreferenceRecord swap[]{
            {context, first,
             {editor::EditorShortcutOverrideMode::Custom,
              Shortcut(input::Key::B, editor::EditorShortcutModifiers::Alt)}},
            {context, second,
             {editor::EditorShortcutOverrideMode::Custom,
              Shortcut(input::Key::A, editor::EditorShortcutModifiers::Alt)}}};
        editor::EditorShortcutPreferenceItemResult swapResults[2]{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {swap, 2}, {swapResults, 2}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied &&
                  swapResults[0] == editor::EditorShortcutPreferenceItemResult::Applied &&
                  swapResults[1] == editor::EditorShortcutPreferenceItemResult::Applied &&
                  persistence.GetShortcutPreferenceRevision() == baseline + 1u,
              "shortcut preference batch validates the complete candidate map so an order-independent swap commits once");
        const u64 afterSwap = persistence.GetShortcutPreferenceRevision();
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {swap, 2}, {swapResults, 2}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied &&
                  persistence.GetShortcutPreferenceRevision() == afterSwap,
              "reapplying the identical shortcut preference batch is a revision no-op");

        editor::EditorShortcutPreferenceRecord reversedOrderRestore[]{
            {context, second,
             {editor::EditorShortcutOverrideMode::Custom,
              Shortcut(input::Key::B, editor::EditorShortcutModifiers::Alt)}},
            {context, first,
             {editor::EditorShortcutOverrideMode::Custom,
              Shortcut(input::Key::A, editor::EditorShortcutModifiers::Alt)}}};
        editor::EditorShortcutPreferenceItemResult reversedResults[2]{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {reversedOrderRestore, 2}, {reversedResults, 2}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied &&
                  persistence.GetShortcutPreferenceRevision() == afterSwap + 1u,
              "shortcut preference transaction accepts a two-command swap in reverse record order without transient old-binding conflicts");
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {swap, 2}, {swapResults, 2}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied,
              "shortcut preference transaction can swap the same two bindings back independent of file order");

        const editor::EditorShortcutPreferenceRecord unbindThenClaim[]{
            {context, first, {editor::EditorShortcutOverrideMode::Unbound, {}}},
            {context, second,
             {editor::EditorShortcutOverrideMode::Custom,
              Shortcut(input::Key::B, editor::EditorShortcutModifiers::Alt)}}};
        editor::EditorShortcutPreferenceItemResult unbindThenClaimResults[2]{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {unbindThenClaim, 2}, {unbindThenClaimResults, 2}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied &&
                  unbindThenClaimResults[0] == editor::EditorShortcutPreferenceItemResult::Applied &&
                  unbindThenClaimResults[1] == editor::EditorShortcutPreferenceItemResult::Applied,
              "one context transaction can unbind the current chord owner while another command claims that old chord");
        const editor::EditorShortcutPreferenceRecord claimThenRestore[]{
            swap[1], swap[0]};
        editor::EditorShortcutPreferenceItemResult claimThenRestoreResults[2]{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {claimThenRestore, 2}, {claimThenRestoreResults, 2}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied,
              "restoring after the unbound-first transaction remains independent of record order and transient ownership");

        const editor::EditorShortcutPreferenceRecord crossContextFirst{
            context, first,
            {editor::EditorShortcutOverrideMode::Custom,
             Shortcut(input::Key::C, editor::EditorShortcutModifiers::Gui)}};
        const editor::EditorShortcutPreferenceRecord crossContextSecond{
            secondContext, first,
            {editor::EditorShortcutOverrideMode::Custom,
             Shortcut(input::Key::C, editor::EditorShortcutModifiers::Gui)}};
        editor::EditorShortcutPreferenceItemResult crossContextFirstResult{};
        editor::EditorShortcutPreferenceItemResult crossContextSecondResult{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {&crossContextFirst, 1}, {&crossContextFirstResult, 1}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied &&
                  persistence.ApplyShortcutPreferenceBatch(
                      secondContext, {&crossContextSecond, 1}, {&crossContextSecondResult, 1}, &failure) ==
                      editor::EditorShortcutPreferenceBatchResult::Applied,
              "the same restored custom chord is legal in two distinct command contexts");
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {swap, 2}, {swapResults, 2}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied &&
                  service.SetShortcutOverride(
                      secondContext, first,
                      {editor::EditorShortcutOverrideMode::Default, {}}, &failure),
              "cross-context reuse fixture restores its original context bindings before later persistence checks");
        const u64 afterRoundTrip = persistence.GetShortcutPreferenceRevision();

        editor::EditorShortcutBinding firstBinding, secondBinding;
        Check(service.GetShortcutBinding(context, first, firstBinding, &failure) &&
                  service.GetShortcutBinding(context, second, secondBinding, &failure) &&
                  firstBinding.chord == swap[0].shortcutOverride.chord &&
                  secondBinding.chord == swap[1].shortcutOverride.chord,
              "shortcut preference swap installs the exact requested live bindings");

        const editor::EditorShortcutPreferenceRecord conflict[]{
            {context, first,
             {editor::EditorShortcutOverrideMode::Custom,
              Shortcut(input::Key::C, editor::EditorShortcutModifiers::Alt)}},
            {context, second,
             {editor::EditorShortcutOverrideMode::Custom,
              Shortcut(input::Key::C, editor::EditorShortcutModifiers::Alt)}}};
        editor::EditorShortcutPreferenceItemResult conflictResults[2]{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {conflict, 2}, {conflictResults, 2}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Conflict &&
                  persistence.GetShortcutPreferenceRevision() == afterRoundTrip &&
                  service.GetShortcutBinding(context, first, firstBinding, &failure) &&
                  service.GetShortcutBinding(context, second, secondBinding, &failure) &&
                  firstBinding.chord == swap[0].shortcutOverride.chord &&
                  secondBinding.chord == swap[1].shortcutOverride.chord,
              "same-context restored conflict rejects the entire batch without partial live mutation");

        editor::EditorShortcutPreferenceRecord orphan{
            context, missingHandler,
            {editor::EditorShortcutOverrideMode::Unbound, {}}};
        editor::EditorShortcutPreferenceItemResult orphanResult{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {&orphan, 1}, {&orphanResult, 1}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied &&
                  orphanResult == editor::EditorShortcutPreferenceItemResult::MissingCommand,
              "registered command without the exact handler is reported as a stable-key orphan");

        editor::EditorCommandHandlerToken returnedHandler;
        Check(Registry::RegisterHandler(
                  service, Handler(context, missingHandler, missingFixture,
                                   Shortcut(input::Key::C, editor::EditorShortcutModifiers::Control)),
                  returnedHandler, &failure),
              "the formerly missing exact shortcut handler can register later without hot-applying retained intent");
        const editor::EditorShortcutPreferenceRecord unaffectedConflict{
            context, first,
            {editor::EditorShortcutOverrideMode::Custom,
             Shortcut(input::Key::C, editor::EditorShortcutModifiers::Control)}};
        editor::EditorShortcutPreferenceItemResult unaffectedResult{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {&unaffectedConflict, 1}, {&unaffectedResult, 1}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Conflict &&
                  persistence.GetShortcutPreferenceRevision() == afterRoundTrip,
              "candidate shortcut conflicts against an unaffected live handler and rejects the whole context transaction");
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {&orphan, 1}, {&orphanResult, 1}, &failure) ==
                      editor::EditorShortcutPreferenceBatchResult::Applied &&
                  orphanResult == editor::EditorShortcutPreferenceItemResult::Applied,
              "the same stable shortcut orphan applies when its exact handler later exists");

        editor::EditorShortcutPreferenceRecord captured[3]{};
        captured[0].command = {0xdeadbeefu};
        u32 count = 0;
        Check(!persistence.CaptureShortcutPreferences(captured, 1, count, &failure) &&
                  failure.code == editor::EditorCommandFailureCode::CapacityExceeded &&
                  count == 3 && captured[0].command.value == 0xdeadbeefu,
              "shortcut capture reports exact required count and copies no prefix when capacity is insufficient");
        Check(persistence.CaptureShortcutPreferences(captured, 3, count, &failure) && count == 3 &&
                  captured[0].command == first && captured[1].command == second &&
                  captured[2].command == missingHandler,
              "shortcut capture emits only current non-default overrides in canonical stable-key order");

        constexpr editor::EditorCommandContextTypeId missingContext{0xd6ffu};
        editor::EditorShortcutPreferenceRecord missingContextRecord{
            missingContext, first, {editor::EditorShortcutOverrideMode::Unbound, {}}};
        editor::EditorShortcutPreferenceItemResult missingContextResult{};
        const u64 beforeInvalidBatches = persistence.GetShortcutPreferenceRevision();
        Check(persistence.ApplyShortcutPreferenceBatch(
                  missingContext, {&missingContextRecord, 1}, {&missingContextResult, 1}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied &&
                  missingContextResult == editor::EditorShortcutPreferenceItemResult::MissingCommand &&
                  persistence.GetShortcutPreferenceRevision() == beforeInvalidBatches,
              "missing stable context reports an exact-key orphan without live shortcut mutation");
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {&missingContextRecord, 1}, {&missingContextResult, 1}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Invalid &&
                  persistence.GetShortcutPreferenceRevision() == beforeInvalidBatches,
              "batch context-key mismatch is invalid and leaves the context transaction untouched");
        const editor::EditorShortcutPreferenceRecord duplicateInput[]{swap[0], swap[0]};
        editor::EditorShortcutPreferenceItemResult duplicateResults[2]{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  context, {duplicateInput, 2}, {duplicateResults, 2}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Invalid &&
                  persistence.GetShortcutPreferenceRevision() == beforeInvalidBatches,
              "duplicate command keys in one shortcut batch reject without partial mutation");

        constexpr editor::EditorCommandContextTypeId returningContext{0xd620u};
        constexpr editor::EditorCommandId returningCommand{0xd621u};
        editor::EditorCommandContextTypeToken returningContextToken;
        editor::EditorCommandToken returningCommandToken;
        Check(Registry::RegisterContextType(
                  service, ContextType(returningContext.value, "persistReturningContext",
                                       editor::EditorCommandContextTier::Local),
                  returningContextToken, &failure) &&
                  Registry::RegisterCommand(
                      service, Command(returningCommand.value, "persistReturningCommand"),
                      returningCommandToken, &failure),
              "fresh-cycle orphan fixture initially registers stable context and command identity without a handler");
        editor::EditorShortcutPreferenceRecord returningRecord{
            returningContext, returningCommand,
            {editor::EditorShortcutOverrideMode::Custom,
             Shortcut(input::Key::E, editor::EditorShortcutModifiers::Control)}};
        editor::EditorShortcutPreferenceItemResult returningResult{};
        Check(persistence.ApplyShortcutPreferenceBatch(
                  returningContext, {&returningRecord, 1}, {&returningResult, 1}, &failure) ==
                  editor::EditorShortcutPreferenceBatchResult::Applied &&
                  returningResult == editor::EditorShortcutPreferenceItemResult::MissingCommand,
              "fresh-cycle exact stable key begins as a missing-handler orphan without reserving its chord");
        Check(Registry::UnregisterCommand(service, returningCommandToken, &failure) &&
                  Registry::UnregisterContextType(service, returningContextToken, &failure),
              "fresh-cycle orphan fixture removes the first owner generation");
        returningContextToken = {};
        returningCommandToken = {};
        HandlerFixture returningFixture;
        editor::EditorCommandHandlerToken returningHandler;
        Check(Registry::RegisterContextType(
                  service, ContextType(returningContext.value, "persistReturningContext",
                                       editor::EditorCommandContextTier::Local),
                  returningContextToken, &failure) &&
                  Registry::RegisterCommand(
                      service, Command(returningCommand.value, "persistReturningCommand"),
                      returningCommandToken, &failure) &&
                  Registry::RegisterHandler(
                      service, Handler(returningContext, returningCommand, returningFixture,
                                       Shortcut(input::Key::F, editor::EditorShortcutModifiers::Alt)),
                      returningHandler, &failure) &&
                  persistence.ApplyShortcutPreferenceBatch(
                      returningContext, {&returningRecord, 1}, {&returningResult, 1}, &failure) ==
                      editor::EditorShortcutPreferenceBatchResult::Applied &&
                  returningResult == editor::EditorShortcutPreferenceItemResult::Applied,
              "the same retained stable-key intent applies only after an exact fresh context+command handler generation returns");
        Check(Registry::UnregisterHandler(service, returningHandler, &failure) &&
                  Registry::UnregisterCommand(service, returningCommandToken, &failure) &&
                  Registry::UnregisterContextType(service, returningContextToken, &failure),
              "fresh-cycle orphan fixture cleans up its returned owner generation");

        Check(Registry::UnregisterHandler(service, returnedHandler, &failure) &&
                  Registry::UnregisterHandler(service, secondContextHandler, &failure) &&
                  Registry::UnregisterHandler(service, secondHandler, &failure) &&
                  Registry::UnregisterHandler(service, firstHandler, &failure) &&
                  Registry::UnregisterCommand(service, missingToken, &failure) &&
                  Registry::UnregisterCommand(service, secondToken, &failure) &&
                  Registry::UnregisterCommand(service, firstToken, &failure) &&
                  Registry::UnregisterContextType(service, secondContextToken, &failure) &&
                  Registry::UnregisterContextType(service, contextToken, &failure),
              "shortcut preference fixture cleans up exact registrations");
    }
} // namespace

int main()
{
    vanguard::concurrency::InitializeMainThread();
    Check(memory::Initialize(), "memory initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    TestProfileExclusion(app::ApplicationProfile::Runtime, "runtime profile compiles with command service excluded");
    TestProfileExclusion(app::ApplicationProfile::Headless, "headless profile compiles with command service excluded");

    {
        LightweightCommandHostFixture toolFixture;
        Check(toolFixture.Compile(app::ApplicationProfile::Tool), "Tool profile selects and compiles the editor command service");
        editor::EditorCommandService* const toolService = toolFixture.Start();
        Check(toolService != nullptr && toolService->GetStatus().state == editor::EditorCommandServiceState::Running,
              "Tool profile starts and exposes the command capability");
        Check(toolFixture.host.Shutdown(&toolFixture.failure), "Tool profile command fixture shuts down");
    }

    CommandHostFixture fixture;
    Check(fixture.Compile(), "command host compiles with real Jobs/Frame Pipeline/Input and fake UI");
    CompiledCommandProof compiled;
    fixture.host.VisitCompiledServices(VisitCompiledCommand, &compiled);
    Check(compiled.seen && compiled.scope == app::ServiceScope::EditorWorkspace && compiled.affinity == app::ThreadAffinity::MainThread,
          "command service compiles as an EditorWorkspace MainThread service");
    Check(compiled.frameIndex < compiled.commandIndex && compiled.inputIndex < compiled.commandIndex && compiled.uiIndex < compiled.commandIndex,
          "all three required command-service dependencies start before the command service");

    editor::EditorCommandService* const service = fixture.Start();
    Check(service != nullptr, "command host starts and exposes its command capability");
    if (service != nullptr)
    {
        const editor::EditorCommandServiceStatus initial = service->GetStatus();
        Check(initial.state == editor::EditorCommandServiceState::Running && initial.registeredContextCount == 1 &&
                  initial.registeredCommandCount == 0 && initial.registeredHandlerCount == 0,
              "command service starts with only its built-in workspace context");
        TestDescriptorAndDirectRouting(*service);
        TestStableTokenReuse(*service);
        TestCommandCapacity(*service);
        TestContextDescriptorsAndRouting(*service);
        TestActiveContextCapacity(*service);
        TestContextCapacity(*service);
        TestHandlerCapacity(*service);
        TestWrongThread(*service);
        TestShortcutModifierState();
        TestShortcutBindingsAndOverrides(*service);
        TestExtensionOwnerRevocation(*service);
        TestPanelLocalContextValidation(*service);
        TestSurfaceCommandOwnershipValidation(*service);
        TestSurfaceCommandPresentation(*service);
        TestShortcutPreferencePersistence(*service);
        TestShortcutFrameRouting(fixture, *service);

        fixture.ui.command = {50};
        fixture.ui.checkQuiescedRouting = true;
    }

    Check(fixture.host.Shutdown(&fixture.failure), "command host shuts down cleanly");
    Check(fixture.ui.quiescedRoutingRejected, "dependency quiesce observes command routing already closed with WrongPhase");
    Check(fixture.ui.quiescedOverrideRejected, "dependency quiesce observes runtime shortcut override admission already closed");
    Check(fixture.ui.quiescedClaimsCleared, "dependency quiesce observes current-frame command claims cleared");

    TestFreshHostTokenGeneration();

    if (g_failures == 0)
        std::printf("Vanguard editor command shortcut routing, UI capture, claims, ownership and context tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}

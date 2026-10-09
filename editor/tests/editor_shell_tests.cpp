#include <vanguard/editor/editor_extension.hpp>
#include <vanguard/editor/editor_activity_preferences.hpp>
#include <vanguard/editor/editor_asset_browser_persistence.hpp>
#include <vanguard/editor/editor_asset_browser_policy.hpp>
#include <vanguard/editor/editor_asset_browser_tree.hpp>
#include <vanguard/editor/editor_console_persistence.hpp>
#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_file_dialog.hpp>
#include <vanguard/editor/editor_panels.hpp>
#include <vanguard/editor/editor_surfaces.hpp>
#include <vanguard/editor/editor_ui.hpp>
#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_static_extensions.hpp>
#include <editor_test_extensions.hpp>
#include <editor_extension_registration_test_adapter.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/input/input.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/projects/project.hpp>
#include <scratch_directory.hpp>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

namespace
{
    namespace containers = vanguard::containers;
    namespace editor = vanguard::editor;
    namespace app = vanguard::application;
    namespace assets = vanguard::assets;
    namespace assetStreaming = vanguard::asset_streaming;
    namespace diagnostics = vanguard::diagnostics;
    namespace engine = vanguard::engine;
    namespace filesystem = vanguard::filesystem;
    namespace input = vanguard::input;
    namespace jobs = vanguard::jobs;
    namespace memory = vanguard::memory;
    namespace resources = vanguard::resources;
    using vanguard::u32;
    using vanguard::u8;
    using vanguard::u64;

    int g_failures = 0;
    u32 g_activateCalls = 0;
    u32 g_deactivateCalls = 0;
    vanguard::u64 g_activationOrder[editor::MaximumEditorExtensions]{};
    vanguard::u64 g_deactivationOrder[editor::MaximumEditorExtensions]{};
    u32 g_activationOrderCount = 0;
    u32 g_deactivationOrderCount = 0;
    vanguard::u64 g_observedActivationGeneration = 0;
    bool g_observedWorkspace = false;
    u32 g_uiQuiesceCalls = 0;
    u32 g_pluginsRootQueries = 0;
    u32 g_panelLifecycleEventCount = 0;
    u32 g_panelLifecycleEvents[16]{};
    u32 g_surfaceLifecycleEventCount = 0;
    u32 g_surfaceLifecycleEvents[16]{};
    bool g_surfaceRevokeReducedReachableTopology = false;
    u32 g_bridgeLifecycleEventCount = 0;
    u32 g_bridgeLifecycleEvents[16]{};
    bool g_rejectSurfaceBridgeRegistration = false;
    u32 g_activityRegisterCalls = 0;
    u32 g_activityRevokeCalls = 0;
    u32 g_activityUnregisterCalls = 0;
    bool g_activityRevokeBeforeUnregister = false;
    bool g_preferenceBool = true;
    vanguard::i64 g_preferenceSigned = 7;
    u64 g_preferenceRevision = 1;
    u32 g_preferenceValidateCalls = 0;
    u32 g_preferenceApplyCalls = 0;
    bool g_preferenceFailSecondCapture = false;
    editor::EditorPreferenceProviderToken g_preferenceProviderToken;
    bool g_preferenceDeactivateSawRevokedProvider = false;

    inline constexpr editor::EditorPreferenceProviderId PreferenceFixtureProviderId = 0xe610u;
    inline constexpr editor::EditorPreferenceFieldId PreferenceFixtureBoolFieldId = 0xe611u;
    inline constexpr editor::EditorPreferenceFieldId PreferenceFixtureSignedFieldId = 0xe612u;

    inline constexpr editor::EditorExtensionId DemonstrationExtensionId{0x6564333764656d01ull};
    inline constexpr editor::EditorPanelTypeId DemonstrationPanelTypeId = 0x65643337646d7001ull;
    inline constexpr editor::EditorPanelInstanceId DemonstrationPanelInstanceId = 1u;
    inline constexpr editor::EditorCommandId DemonstrationCommandId{0x65643337636d6401ull};
    inline constexpr editor::EditorUiSurfaceId DemonstrationContextMenuSurfaceId{0x6564333763746d01ull};
    inline constexpr editor::EditorActivitySourceId DemonstrationActivitySourceId = 0x6564333761637401ull;
    inline constexpr editor::EditorPreferenceProviderId DemonstrationPreferenceProviderId = 0x6564333770726601ull;
    inline constexpr editor::EditorPreferenceFieldId DemonstrationDetailsPreferenceFieldId = 0x6564333770726602ull;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[editorShellTests] FAILED: %s\n", message);
        ++g_failures;
    }

    u64 PreferenceFixtureRevision(void* const privateState) noexcept
    {
        Check(privateState == nullptr, "stateless preference provider receives null private state");
        return g_preferenceRevision;
    }

    bool PreferenceFixtureCapture(void* const privateState, const editor::EditorPreferenceFieldId fieldId,
                                  editor::EditorPreferenceFieldValue& value) noexcept
    {
        Check(privateState == nullptr, "stateless preference capture accepts null private state");
        value = {};
        if (fieldId == PreferenceFixtureBoolFieldId)
        {
            value.type = editor::EditorPreferenceValueType::Bool;
            value.value.boolean = g_preferenceBool;
            return true;
        }
        if (fieldId == PreferenceFixtureSignedFieldId)
        {
            if (g_preferenceFailSecondCapture)
                return false;
            value.type = editor::EditorPreferenceValueType::SignedInteger;
            value.value.signedInteger = g_preferenceSigned;
            return true;
        }
        return false;
    }

    bool PreferenceFixtureValidate(void* const privateState, const editor::EditorPreferenceFieldId fieldId,
                                   const editor::EditorPreferenceFieldValue& value) noexcept
    {
        Check(privateState == nullptr, "stateless preference validation accepts null private state");
        ++g_preferenceValidateCalls;
        if (fieldId == PreferenceFixtureBoolFieldId)
            return value.type == editor::EditorPreferenceValueType::Bool;
        if (fieldId == PreferenceFixtureSignedFieldId)
            return value.type == editor::EditorPreferenceValueType::SignedInteger &&
                   value.value.signedInteger >= -10 && value.value.signedInteger <= 10;
        return false;
    }

    void PreferenceFixtureApply(void* const privateState, const editor::EditorPreferenceFieldId fieldId,
                                const editor::EditorPreferenceFieldValue& value) noexcept
    {
        Check(privateState == nullptr, "stateless preference apply accepts null private state");
        ++g_preferenceApplyCalls;
        bool changed = false;
        if (fieldId == PreferenceFixtureBoolFieldId)
        {
            changed = g_preferenceBool != value.value.boolean;
            g_preferenceBool = value.value.boolean;
        }
        else if (fieldId == PreferenceFixtureSignedFieldId)
        {
            changed = g_preferenceSigned != value.value.signedInteger;
            g_preferenceSigned = value.value.signedInteger;
        }
        if (changed)
            ++g_preferenceRevision;
    }

    void RecordPanelLifecycleEvent(const u32 event) noexcept
    {
        if (g_panelLifecycleEventCount < static_cast<u32>(sizeof(g_panelLifecycleEvents) / sizeof(g_panelLifecycleEvents[0])))
            g_panelLifecycleEvents[g_panelLifecycleEventCount++] = event;
    }

    void RecordSurfaceLifecycleEvent(const u32 event) noexcept
    {
        if (g_surfaceLifecycleEventCount < static_cast<u32>(sizeof(g_surfaceLifecycleEvents) / sizeof(g_surfaceLifecycleEvents[0])))
            g_surfaceLifecycleEvents[g_surfaceLifecycleEventCount++] = event;
    }

    void RecordBridgeLifecycleEvent(const u32 event) noexcept
    {
        if (g_bridgeLifecycleEventCount < static_cast<u32>(sizeof(g_bridgeLifecycleEvents) / sizeof(g_bridgeLifecycleEvents[0])))
            g_bridgeLifecycleEvents[g_bridgeLifecycleEventCount++] = event;
    }

    struct PanelExtensionFixture final
    {
        u64 id = 0;
        app::EngineHost* host = nullptr;
        bool failAfterRegistration = false;
        bool privateStateAlive = false;
        bool invalidPersistenceRejected = false;
        bool registrationSucceeded = false;
        bool instanceCreated = false;
        bool reachableDuringActivation = false;
        bool ownerScopedPanelAccessSucceeded = false;
        bool unownedRemovalRejected = false;
        bool cleanupCompleteDuringDeactivate = false;
        bool registrationRejectedDuringDeactivate = false;
        bool visibilityRejectedDuringDeactivate = false;
        bool destroySawPrivateStateAlive = true;
        bool destroySawUiReady = true;
        editor::EditorUiFailureCode unownedRemovalFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode invalidPersistenceFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode deactivateVisibilityFailure = editor::EditorUiFailureCode::None;
        u32 createCalls = 0;
        u32 destroyCalls = 0;
        u32 deactivateCalls = 0;
        editor::EditorUiFailureCode deactivateRegistrationFailure = editor::EditorUiFailureCode::None;
        editor::EditorExtensionOwner owner;
        editor::EditorPanelTypeId type = editor::InvalidEditorPanelTypeId;
        editor::EditorPanelInstanceId instance = editor::InvalidEditorPanelInstanceId;
        editor::EditorPanelTypeToken typeToken;
        editor::EditorPanelHandle panel;
    };

    void* OwnedPanelCreate(const editor::EditorPanelInstanceId, void* const factoryData) noexcept
    {
        auto& fixture = *static_cast<PanelExtensionFixture*>(factoryData);
        Check(fixture.privateStateAlive, "panel create callback runs while extension private state is live");
        ++fixture.createCalls;
        return &fixture;
    }

    void OwnedPanelDestroy(void* const state, void* const factoryData) noexcept
    {
        auto& fixture = *static_cast<PanelExtensionFixture*>(factoryData);
        Check(state == &fixture, "panel destroy receives the exact factory state");
        fixture.destroySawPrivateStateAlive = fixture.destroySawPrivateStateAlive && fixture.privateStateAlive;
        fixture.destroySawUiReady = fixture.destroySawUiReady && g_uiQuiesceCalls == 0;
        ++fixture.destroyCalls;
        RecordPanelLifecycleEvent(100u + static_cast<u32>(fixture.id));
    }

    void OwnedPanelDraw(editor::EditorPanelContext&, void*, void*) noexcept
    {
    }

    bool PanelExtensionActivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                void*& privateState) noexcept
    {
        auto& fixture = *const_cast<PanelExtensionFixture*>(static_cast<const PanelExtensionFixture*>(descriptorData));
        fixture.privateStateAlive = true;
        fixture.owner = context.GetOwner();
        fixture.type = 0xa300u + fixture.id;
        fixture.instance = 0xa400u + fixture.id;
        privateState = &fixture;

        editor::EditorPanelTypeDesc type;
        type.id = fixture.type;
        type.name = "ownedPanel";
        type.create = OwnedPanelCreate;
        type.destroy = OwnedPanelDestroy;
        type.draw = OwnedPanelDraw;
        type.factoryData = &fixture;
        editor::EditorUiFailure failure;
        editor::EditorPanelTypeDesc invalidPersistence = type;
        invalidPersistence.id = fixture.type + 0x100000u;
        invalidPersistence.name = "invalidPersistentPanel";
        invalidPersistence.persistence.stateVersion = 1;
        editor::EditorPanelTypeToken invalidPersistenceToken;
        fixture.invalidPersistenceRejected =
            !context.RegisterPanelType(invalidPersistence, invalidPersistenceToken, &failure) &&
            !invalidPersistenceToken.IsValid();
        fixture.invalidPersistenceFailure = failure.code;
        fixture.registrationSucceeded = context.RegisterPanelType(type, fixture.typeToken, &failure);
        if (!fixture.registrationSucceeded || fixture.host == nullptr)
            return false;

        auto* const ui = static_cast<editor::EditorUiService*>(fixture.host->FindCapability(editor::EditorUiCapabilityId));
        if (ui == nullptr)
            return false;
        editor::EditorPanelRegistry& panels = ui->GetPanels();
        const editor::EditorOwnedPanelAccess ownedPanels = context.GetOwnedPanels();
        fixture.instanceCreated = ownedPanels.CreatePanel(
            {fixture.instance, fixture.type, "owned panel", true}, fixture.panel, &failure);
        fixture.reachableDuringActivation = fixture.instanceCreated && ownedPanels.IsOpen(fixture.panel);
        fixture.ownerScopedPanelAccessSucceeded =
            fixture.reachableDuringActivation && panels.IsOpen(fixture.panel);
        fixture.unownedRemovalRejected = !panels.UnregisterType(fixture.typeToken, &failure);
        fixture.unownedRemovalFailure = failure.code;
        fixture.unownedRemovalRejected =
            fixture.unownedRemovalRejected && failure.code == editor::EditorUiFailureCode::OwnerRevoked;
        return fixture.ownerScopedPanelAccessSucceeded && fixture.instanceCreated && fixture.reachableDuringActivation &&
               fixture.invalidPersistenceRejected &&
               fixture.invalidPersistenceFailure == editor::EditorUiFailureCode::InvalidDescriptor &&
               fixture.unownedRemovalRejected &&
               !fixture.failAfterRegistration;
    }

    void PanelExtensionDeactivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                  void* const privateState) noexcept
    {
        auto& fixture = *const_cast<PanelExtensionFixture*>(static_cast<const PanelExtensionFixture*>(descriptorData));
        ++fixture.deactivateCalls;
        Check(privateState == &fixture && fixture.privateStateAlive,
              "panel extension private state survives revoke and physical panel cleanup");
        auto* const ui = fixture.host != nullptr
                             ? static_cast<editor::EditorUiService*>(fixture.host->FindCapability(editor::EditorUiCapabilityId))
                             : nullptr;
        if (ui != nullptr)
        {
            editor::EditorUiFailure failure;
            editor::EditorPanelRegistry& panels = ui->GetPanels();
            fixture.cleanupCompleteDuringDeactivate =
                fixture.destroyCalls == (fixture.instanceCreated ? 1u : 0u) && !panels.IsOpen(fixture.panel) &&
                !panels.DestroyPanel(fixture.panel, &failure) && failure.code == editor::EditorUiFailureCode::InvalidHandle &&
                !panels.UnregisterType(fixture.typeToken, &failure) && failure.code == editor::EditorUiFailureCode::InvalidHandle;
        }
        editor::EditorPanelTypeToken rejected;
        editor::EditorUiFailure registrationFailure;
        editor::EditorPanelTypeDesc late;
        late.id = 0xaf00u + fixture.id;
        late.name = "latePanel";
        late.draw = OwnedPanelDraw;
        fixture.registrationRejectedDuringDeactivate =
            !context.RegisterPanelType(late, rejected, &registrationFailure) && !rejected.IsValid();
        fixture.deactivateRegistrationFailure = registrationFailure.code;
        editor::EditorUiFailure visibilityFailure;
        fixture.visibilityRejectedDuringDeactivate =
            !context.GetOwnedPanels().SetOpen(fixture.panel, true, &visibilityFailure);
        fixture.deactivateVisibilityFailure = visibilityFailure.code;
        RecordPanelLifecycleEvent(200u + static_cast<u32>(fixture.id));
        fixture.privateStateAlive = false;
    }

    editor::EditorExtensionDescriptor PanelExtensionDescriptor(
        const u64 id, const char* const name, PanelExtensionFixture& fixture,
        const containers::ArraySpan<const editor::EditorExtensionId> dependencies = {}) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {id};
        descriptor.technicalName = name;
        descriptor.displayName = name;
        descriptor.requiredExtensions = dependencies;
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = PanelExtensionActivate;
        descriptor.deactivate = PanelExtensionDeactivate;
        descriptor.data = &fixture;
        fixture.id = id;
        return descriptor;
    }

    struct PanelFocusBridgeFixture final
    {
        app::EngineHost* host = nullptr;
        bool privateStateAlive = false;
        bool registered = false;
        bool panelCreated = false;
        bool secondPanelCreated = false;
        bool deactivateSawNoActiveContext = false;
        u32 queryCalls = 0;
        void* lastReceiverData = nullptr;
        editor::EditorExtensionOwner owner;
        editor::EditorCommandId command{0xb401u};
        editor::EditorCommandContextTypeId localContext{0xb402u};
        editor::EditorCommandContextTypeId controlContext{0xb403u};
        editor::EditorPanelTypeId panelType = 0xb404u;
        editor::EditorPanelInstanceId panelInstance = 0xb405u;
        editor::EditorPanelTypeId secondPanelType = 0xb406u;
        editor::EditorPanelInstanceId secondPanelInstance = 0xb407u;
        u32 panelStateA = 1u;
        u32 panelStateB = 2u;
        editor::EditorPanelTypeToken panelTypeToken;
        editor::EditorPanelTypeToken secondPanelTypeToken;
        editor::EditorPanelHandle panel;
        editor::EditorPanelHandle secondPanel;
    };

    editor::EditorCommandState PanelFocusQuery(void* const receiverData, void* const handlerData) noexcept
    {
        auto& fixture = *static_cast<PanelFocusBridgeFixture*>(handlerData);
        Check(fixture.privateStateAlive, "focused panel command query runs while extension state is live");
        Check(receiverData == &fixture.panelStateA || receiverData == &fixture.panelStateB,
              "focused panel Local context borrows one of the exact panel state receivers");
        fixture.lastReceiverData = receiverData;
        ++fixture.queryCalls;
        return editor::EditorCommandState::Enabled();
    }

    bool PanelFocusInvoke(void*, void*) noexcept { return true; }
    void* PanelFocusCreate(const editor::EditorPanelInstanceId instance, void* const data) noexcept
    {
        auto& fixture = *static_cast<PanelFocusBridgeFixture*>(data);
        if (instance == fixture.panelInstance)
            return &fixture.panelStateA;
        if (instance == fixture.secondPanelInstance)
            return &fixture.panelStateB;
        return nullptr;
    }
    void PanelFocusDestroy(void* const state, void* const data) noexcept
    {
        auto& fixture = *static_cast<PanelFocusBridgeFixture*>(data);
        Check((state == &fixture.panelStateA || state == &fixture.panelStateB) && fixture.privateStateAlive,
              "panel state is destroyed only while its extension private state is live");
    }
    void PanelFocusDraw(editor::EditorPanelContext&, void*, void*) noexcept {}

    bool PanelFocusActivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                            void*& privateState) noexcept
    {
        auto& fixture = *const_cast<PanelFocusBridgeFixture*>(static_cast<const PanelFocusBridgeFixture*>(descriptorData));
        fixture.privateStateAlive = true;
        fixture.owner = context.GetOwner();
        privateState = &fixture;
        editor::EditorCommandToken commandToken;
        editor::EditorCommandContextTypeToken localToken;
        editor::EditorCommandContextTypeToken controlToken;
        editor::EditorCommandHandlerToken handlerToken;
        editor::EditorCommandFailure commandFailure;
        editor::EditorPanelTypeDesc panelType;
        panelType.id = fixture.panelType;
        panelType.name = "focusedPanel";
        panelType.create = PanelFocusCreate;
        panelType.destroy = PanelFocusDestroy;
        panelType.draw = PanelFocusDraw;
        panelType.factoryData = &fixture;
        editor::EditorUiFailure uiFailure;
        const editor::EditorPanelLocalContextBinding binding{fixture.localContext, editor::EditorCommandContextMode::Inherit};
        fixture.registered =
            context.RegisterCommand({fixture.command, "focusedPanelCommand", editor::EditorCommandFlags::None}, commandToken,
                                    &commandFailure) &&
            context.RegisterContextType({fixture.localContext, "focusedPanelLocal", editor::EditorCommandContextTier::Local},
                                        localToken, &commandFailure) &&
            context.RegisterContextType({fixture.controlContext, "focusedPanelControl", editor::EditorCommandContextTier::ControlTool},
                                        controlToken, &commandFailure) &&
            context.RegisterHandler({fixture.localContext, fixture.command, PanelFocusQuery, PanelFocusInvoke, &fixture},
                                    handlerToken, &commandFailure) &&
            context.RegisterPanelType(panelType, binding, fixture.panelTypeToken, &uiFailure);
        if (fixture.registered)
        {
            panelType.id = fixture.secondPanelType;
            panelType.name = "focusedPanelSecond";
            fixture.registered = context.RegisterPanelType(panelType, binding, fixture.secondPanelTypeToken, &uiFailure);
        }
        if (!fixture.registered || fixture.host == nullptr)
            return false;
        auto* const ui = static_cast<editor::EditorUiService*>(fixture.host->FindCapability(editor::EditorUiCapabilityId));
        fixture.panelCreated = ui != nullptr &&
                               ui->GetPanels().CreatePanel(
                                   {fixture.panelInstance, fixture.panelType, "focused panel", true}, fixture.panel, &uiFailure);
        fixture.secondPanelCreated = fixture.panelCreated &&
                                     ui->GetPanels().CreatePanel(
                                         {fixture.secondPanelInstance, fixture.secondPanelType, "focused panel second", true},
                                         fixture.secondPanel, &uiFailure);
        return fixture.secondPanelCreated;
    }

    void PanelFocusDeactivate(editor::EditorExtensionActivationContext&, const void* const descriptorData,
                              void* const privateState) noexcept
    {
        auto& fixture = *const_cast<PanelFocusBridgeFixture*>(static_cast<const PanelFocusBridgeFixture*>(descriptorData));
        Check(privateState == &fixture && fixture.privateStateAlive,
              "focused panel extension remains live through owned focus/panel cleanup");
        editor::EditorCommandService* const commands =
            fixture.host != nullptr ? editor::FindEditorCommandService(*fixture.host) : nullptr;
        fixture.deactivateSawNoActiveContext =
            commands != nullptr && commands->GetStatus().activeContextCount == 0u;
        fixture.privateStateAlive = false;
    }

    editor::EditorExtensionDescriptor PanelFocusDescriptor(PanelFocusBridgeFixture& fixture) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {0xb400u};
        descriptor.technicalName = "panel-focus-bridge";
        descriptor.displayName = "panel-focus-bridge";
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = PanelFocusActivate;
        descriptor.deactivate = PanelFocusDeactivate;
        descriptor.data = &fixture;
        return descriptor;
    }

    struct CrossOwnerPanelContextFixture final
    {
        bool providerRegistered = false;
        bool consumerRejected = false;
        bool crossOwnerCreateRejected = false;
        bool crossOwnerVisibilityRejected = false;
        editor::EditorUiFailureCode consumerFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode crossOwnerCreateFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode crossOwnerVisibilityFailure = editor::EditorUiFailureCode::None;
        editor::EditorCommandContextTypeId providerLocal{0xb421u};
        editor::EditorPanelTypeId providerPanelType = 0xb420u;
        editor::EditorPanelInstanceId providerPanelInstance = 0xb424u;
        editor::EditorPanelTypeId consumerPanelType = 0xb422u;
        editor::EditorPanelHandle providerPanel;
    };

    bool CrossOwnerPanelProviderActivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                         void*& privateState) noexcept
    {
        auto& fixture = *const_cast<CrossOwnerPanelContextFixture*>(
            static_cast<const CrossOwnerPanelContextFixture*>(descriptorData));
        privateState = &fixture;
        editor::EditorCommandContextTypeToken contextToken;
        editor::EditorCommandFailure commandFailure;
        editor::EditorPanelTypeToken panelTypeToken;
        editor::EditorPanelTypeDesc panelType;
        panelType.id = fixture.providerPanelType;
        panelType.name = "providerPanel";
        panelType.draw = PanelFocusDraw;
        editor::EditorUiFailure uiFailure;
        fixture.providerRegistered =
            context.RegisterContextType(
                {fixture.providerLocal, "providerPanelLocal", editor::EditorCommandContextTier::Local},
                contextToken, &commandFailure) &&
            context.RegisterPanelType(panelType, panelTypeToken, &uiFailure) &&
            context.GetOwnedPanels().CreatePanel(
                {fixture.providerPanelInstance, fixture.providerPanelType, "provider panel", true},
                fixture.providerPanel, &uiFailure);
        return fixture.providerRegistered;
    }

    bool CrossOwnerPanelConsumerActivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                         void*& privateState) noexcept
    {
        auto& fixture = *const_cast<CrossOwnerPanelContextFixture*>(
            static_cast<const CrossOwnerPanelContextFixture*>(descriptorData));
        privateState = &fixture;
        editor::EditorUiFailure failure;
        editor::EditorPanelHandle rejectedPanel;
        const editor::EditorOwnedPanelAccess ownedPanels = context.GetOwnedPanels();
        fixture.crossOwnerCreateRejected =
            !ownedPanels.CreatePanel(
                {0xb425u, fixture.providerPanelType, "cross-owner provider panel", true},
                rejectedPanel, &failure) &&
            !rejectedPanel.IsValid();
        fixture.crossOwnerCreateFailure = failure.code;
        fixture.crossOwnerVisibilityRejected =
            !ownedPanels.SetOpen(fixture.providerPanel, false, &failure);
        fixture.crossOwnerVisibilityFailure = failure.code;

        editor::EditorPanelTypeDesc panelType;
        panelType.id = fixture.consumerPanelType;
        panelType.name = "crossOwnerPanel";
        panelType.draw = PanelFocusDraw;
        const editor::EditorPanelLocalContextBinding binding{fixture.providerLocal, editor::EditorCommandContextMode::Inherit};
        editor::EditorPanelTypeToken token;
        const bool registered = context.RegisterPanelType(panelType, binding, token, &failure);
        fixture.consumerRejected = !registered && !token.IsValid();
        fixture.consumerFailure = failure.code;
        return fixture.crossOwnerCreateRejected &&
               fixture.crossOwnerCreateFailure == editor::EditorUiFailureCode::OwnerMismatch &&
               fixture.crossOwnerVisibilityRejected &&
               fixture.crossOwnerVisibilityFailure == editor::EditorUiFailureCode::OwnerMismatch &&
               fixture.consumerRejected;
    }

    void CrossOwnerPanelDeactivate(editor::EditorExtensionActivationContext&, const void*, void*) noexcept
    {
    }

    void MakeCrossOwnerPanelDescriptors(CrossOwnerPanelContextFixture& fixture,
                                        editor::EditorExtensionDescriptor (&descriptors)[2]) noexcept
    {
        constexpr editor::EditorExtensionId providerId{0xb410u};
        static constexpr editor::EditorExtensionId consumerDependencies[]{providerId};

        descriptors[0].id = providerId;
        descriptors[0].technicalName = "panel-context-provider";
        descriptors[0].displayName = "panel-context-provider";
        descriptors[0].selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptors[0].activate = CrossOwnerPanelProviderActivate;
        descriptors[0].deactivate = CrossOwnerPanelDeactivate;
        descriptors[0].data = &fixture;

        descriptors[1].id = {0xb411u};
        descriptors[1].technicalName = "panel-context-consumer";
        descriptors[1].displayName = "panel-context-consumer";
        descriptors[1].requiredExtensions = consumerDependencies;
        descriptors[1].selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptors[1].activate = CrossOwnerPanelConsumerActivate;
        descriptors[1].deactivate = CrossOwnerPanelDeactivate;
        descriptors[1].data = &fixture;
    }

    struct CommandExtensionFixture final
    {
        u64 id = 0;
        app::EngineHost* host = nullptr;
        bool failAfterRegistration = false;
        bool privateStateAlive = false;
        bool registrationSucceeded = false;
        bool queryDuringActivation = false;
        bool invokeDuringActivation = false;
        bool cleanupCompleteDuringDeactivate = false;
        bool commandUnavailableDuringDeactivate = false;
        bool activeContextStaleDuringDeactivate = false;
        bool registrationRejectedDuringDeactivate = false;
        editor::EditorCommandFailureCode deactivateRegistrationFailure = editor::EditorCommandFailureCode::None;
        u32 queryCalls = 0;
        u32 invokeCalls = 0;
        u32 deactivateCalls = 0;
        editor::EditorExtensionOwner owner;
        editor::EditorCommandId command;
        editor::EditorCommandContextTypeId contextType;
        editor::EditorCommandToken commandToken;
        editor::EditorCommandContextTypeToken contextTypeToken;
        editor::EditorCommandHandlerToken handlerToken;
        editor::EditorCommandContextToken activeContextToken;
    };

    editor::EditorCommandState OwnedCommandQuery(void* const receiverData, void* const handlerData) noexcept
    {
        auto& fixture = *static_cast<CommandExtensionFixture*>(handlerData);
        Check(fixture.privateStateAlive, "owned command query only runs while extension private state is alive");
        Check(receiverData == &fixture, "owned command query receives the active context receiver");
        ++fixture.queryCalls;
        return editor::EditorCommandState::Enabled();
    }

    bool OwnedCommandInvoke(void* const receiverData, void* const handlerData) noexcept
    {
        auto& fixture = *static_cast<CommandExtensionFixture*>(handlerData);
        Check(fixture.privateStateAlive, "owned command invoke only runs while extension private state is alive");
        Check(receiverData == &fixture, "owned command invoke receives the active context receiver");
        ++fixture.invokeCalls;
        return true;
    }

    bool CommandExtensionActivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                  void*& privateState) noexcept
    {
        auto& fixture = *const_cast<CommandExtensionFixture*>(static_cast<const CommandExtensionFixture*>(descriptorData));
        fixture.privateStateAlive = true;
        fixture.owner = context.GetOwner();
        fixture.command = {0xc000u + fixture.id};
        fixture.contextType = {0xd000u + fixture.id};
        privateState = &fixture;

        editor::EditorCommandFailure failure;
        const editor::EditorCommandDescriptor command{fixture.command, "ownedCommand", editor::EditorCommandFlags::None};
        const editor::EditorCommandContextTypeDescriptor contextType{
            fixture.contextType, "ownedContext", editor::EditorCommandContextTier::Local};
        const editor::EditorCommandHandlerDescriptor handler{
            fixture.contextType, fixture.command, OwnedCommandQuery, OwnedCommandInvoke, &fixture};
        fixture.registrationSucceeded =
            context.RegisterCommand(command, fixture.commandToken, &failure) &&
            context.RegisterContextType(contextType, fixture.contextTypeToken, &failure) &&
            context.RegisterHandler(handler, fixture.handlerToken, &failure);
        if (!fixture.registrationSucceeded || fixture.host == nullptr)
            return false;

        editor::EditorCommandService* const commands = editor::FindEditorCommandService(*fixture.host);
        if (commands == nullptr ||
            !commands->PushContext(fixture.contextType, &fixture, editor::EditorCommandContextMode::Inherit,
                                   fixture.activeContextToken, &failure))
            return false;

        editor::EditorCommandState state;
        fixture.queryDuringActivation =
            commands->Query(fixture.command, state, &failure) && state.kind == editor::EditorCommandStateKind::Enabled;
        fixture.invokeDuringActivation = commands->Invoke(fixture.command, &failure);
        return fixture.queryDuringActivation && fixture.invokeDuringActivation && !fixture.failAfterRegistration;
    }

    void CommandExtensionDeactivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                    void* const privateState) noexcept
    {
        auto& fixture = *const_cast<CommandExtensionFixture*>(static_cast<const CommandExtensionFixture*>(descriptorData));
        ++fixture.deactivateCalls;
        Check(privateState == &fixture && fixture.privateStateAlive,
              "command extension private state survives logical revoke and physical owned cleanup");

        editor::EditorCommandService* const commands =
            fixture.host != nullptr ? editor::FindEditorCommandService(*fixture.host) : nullptr;
        if (commands != nullptr)
        {
            const editor::EditorCommandServiceStatus status = commands->GetStatus();
            fixture.cleanupCompleteDuringDeactivate =
                status.registeredCommandCount == 0u && status.registeredContextCount == 1u &&
                status.registeredHandlerCount == 0u && status.activeContextCount == 0u &&
                status.state == editor::EditorCommandServiceState::Running;

            editor::EditorCommandState state;
            editor::EditorCommandFailure failure;
            fixture.commandUnavailableDuringDeactivate =
                !commands->Query(fixture.command, state, &failure) &&
                failure.code == editor::EditorCommandFailureCode::MissingCommand;
            fixture.activeContextStaleDuringDeactivate =
                !commands->PopContext(fixture.activeContextToken, &failure) &&
                failure.code == editor::EditorCommandFailureCode::InvalidToken;
        }

        editor::EditorCommandToken rejected;
        editor::EditorCommandFailure registrationFailure;
        fixture.registrationRejectedDuringDeactivate =
            !context.RegisterCommand({{0xee00u + fixture.id}, "lateCommand", editor::EditorCommandFlags::None},
                                     rejected, &registrationFailure) &&
            !rejected.IsValid();
        fixture.deactivateRegistrationFailure = registrationFailure.code;
        fixture.privateStateAlive = false;
    }

    editor::EditorExtensionDescriptor CommandExtensionDescriptor(const u64 id, const char* const name,
                                                                 CommandExtensionFixture& fixture) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {id};
        descriptor.technicalName = name;
        descriptor.displayName = name;
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = CommandExtensionActivate;
        descriptor.deactivate = CommandExtensionDeactivate;
        descriptor.data = &fixture;
        fixture.id = id;
        return descriptor;
    }

    struct SurfaceExtensionFixture final
    {
        u64 id = 0;
        app::EngineHost* host = nullptr;
        bool failAfterRegistration = false;
        bool privateStateAlive = false;
        bool registrationSucceeded = false;
        bool missingCommandRejected = false;
        bool commandUnavailableDuringDeactivate = false;
        bool registrationRejectedDuringDeactivate = false;
        u32 deactivateCalls = 0;
        editor::EditorUiFailureCode missingCommandFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode deactivateRegistrationFailure = editor::EditorUiFailureCode::None;
        editor::EditorExtensionOwner owner;
        editor::EditorCommandId command;
        editor::EditorUiSurfaceId surface;
        editor::EditorCommandToken commandToken;
        editor::detail::EditorUiSurfaceToken surfaceToken;
        editor::detail::EditorUiContributionToken rootCommandToken;
        editor::detail::EditorUiContributionToken menuCommandToken;
        editor::detail::EditorUiContributionToken toolbarCommandToken;
    };

    bool SurfaceExtensionActivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                  void*& privateState) noexcept
    {
        auto& fixture = *const_cast<SurfaceExtensionFixture*>(static_cast<const SurfaceExtensionFixture*>(descriptorData));
        fixture.privateStateAlive = true;
        fixture.owner = context.GetOwner();
        fixture.command = {0x110000u + fixture.id};
        fixture.surface = {0x120000u + fixture.id};
        privateState = &fixture;

        editor::EditorCommandFailure commandFailure;
        if (!context.RegisterCommand({fixture.command, "surfaceCommand", editor::EditorCommandFlags::None},
                                     fixture.commandToken, &commandFailure))
            return false;

        editor::EditorUiFailure uiFailure;
        if (!context.RegisterContextMenuSurface(fixture.surface, fixture.surfaceToken, &uiFailure))
            return false;

        editor::detail::EditorUiContributionDesc rootCommand;
        rootCommand.id = {0x130000u + fixture.id};
        rootCommand.surface = fixture.surface;
        rootCommand.kind = editor::detail::EditorUiContributionKind::Command;
        rootCommand.command = fixture.command;
        rootCommand.label = "Owned Surface Command";
        if (!context.RegisterSurfaceContribution(rootCommand, fixture.rootCommandToken, &uiFailure))
            return false;

        editor::detail::EditorUiContributionDesc menuCommand;
        menuCommand.id = {0x140000u + fixture.id};
        menuCommand.surface = editor::detail::EditorMainMenuSurfaceId;
        menuCommand.kind = editor::detail::EditorUiContributionKind::Command;
        menuCommand.parent = editor::detail::EditorMainMenuFileAnchorId;
        menuCommand.command = fixture.command;
        menuCommand.label = "Owned Menu Command";
        if (!context.RegisterSurfaceContribution(menuCommand, fixture.menuCommandToken, &uiFailure))
            return false;

        editor::detail::EditorUiContributionDesc toolbarCommand;
        toolbarCommand.id = {0x145000u + fixture.id};
        toolbarCommand.surface = editor::detail::EditorMainToolbarSurfaceId;
        toolbarCommand.kind = editor::detail::EditorUiContributionKind::Command;
        toolbarCommand.sibling = editor::detail::EditorMainToolbarStartAnchorId;
        toolbarCommand.placement = editor::detail::EditorUiPlacementRelation::After;
        toolbarCommand.command = fixture.command;
        toolbarCommand.label = "Owned Toolbar Command";
        if (!context.RegisterSurfaceContribution(toolbarCommand, fixture.toolbarCommandToken, &uiFailure))
            return false;

        editor::detail::EditorUiContributionDesc missingCommand = menuCommand;
        missingCommand.id = {0x150000u + fixture.id};
        missingCommand.command = {0x160000u + fixture.id};
        editor::detail::EditorUiContributionToken rejected;
        uiFailure = {};
        fixture.missingCommandRejected =
            !context.RegisterSurfaceContribution(missingCommand, rejected, &uiFailure) && !rejected.IsValid();
        fixture.missingCommandFailure = uiFailure.code;
        fixture.registrationSucceeded =
            fixture.surfaceToken.IsValid() && fixture.rootCommandToken.IsValid() && fixture.menuCommandToken.IsValid() &&
            fixture.toolbarCommandToken.IsValid() &&
            fixture.missingCommandRejected && fixture.missingCommandFailure == editor::EditorUiFailureCode::SurfaceCommandFailure;
        return fixture.registrationSucceeded && !fixture.failAfterRegistration;
    }

    void SurfaceExtensionDeactivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                    void* const privateState) noexcept
    {
        auto& fixture = *const_cast<SurfaceExtensionFixture*>(static_cast<const SurfaceExtensionFixture*>(descriptorData));
        ++fixture.deactivateCalls;
        Check(privateState == &fixture && fixture.privateStateAlive,
              "surface extension private state survives logical revoke and reverse physical cleanup");

        editor::EditorCommandService* const commands =
            fixture.host != nullptr ? editor::FindEditorCommandService(*fixture.host) : nullptr;
        if (commands != nullptr)
        {
            editor::EditorCommandState state;
            editor::EditorCommandFailure failure;
            fixture.commandUnavailableDuringDeactivate =
                !commands->Query(fixture.command, state, &failure) &&
                failure.code == editor::EditorCommandFailureCode::MissingCommand;
        }

        editor::detail::EditorUiSurfaceToken rejected;
        editor::EditorUiFailure registrationFailure;
        fixture.registrationRejectedDuringDeactivate =
            !context.RegisterContextMenuSurface({0x170000u + fixture.id}, rejected, &registrationFailure) &&
            !rejected.IsValid();
        fixture.deactivateRegistrationFailure = registrationFailure.code;
        fixture.privateStateAlive = false;
    }

    editor::EditorExtensionDescriptor SurfaceExtensionDescriptor(const u64 id, const char* const name,
                                                                 SurfaceExtensionFixture& fixture) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {id};
        descriptor.technicalName = name;
        descriptor.displayName = name;
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = SurfaceExtensionActivate;
        descriptor.deactivate = SurfaceExtensionDeactivate;
        descriptor.data = &fixture;
        fixture.id = id;
        return descriptor;
    }

    enum class CrossOwnerSurfaceRole : u8
    {
        Provider,
        Consumer
    };

    inline constexpr editor::EditorCommandId CrossOwnerSurfaceCommand{0x181001u};
    inline constexpr editor::EditorUiSurfaceId CrossOwnerPrivateSurface{0x181002u};
    inline constexpr editor::EditorUiContributionId CrossOwnerPrivateMenuAnchor{0x181003u};

    struct CrossOwnerSurfaceFixture final
    {
        CrossOwnerSurfaceRole role = CrossOwnerSurfaceRole::Provider;
        bool providerRegistered = false;
        bool crossCommandRejected = false;
        bool privateSurfaceRejected = false;
        bool privateAnchorRejected = false;
        editor::EditorUiFailureCode crossCommandFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode privateSurfaceFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode privateAnchorFailure = editor::EditorUiFailureCode::None;
        editor::EditorCommandToken commandToken;
        editor::detail::EditorUiSurfaceToken surfaceToken;
        editor::detail::EditorUiContributionToken anchorToken;
    };

    bool CrossOwnerSurfaceActivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                   void*& privateState) noexcept
    {
        auto& fixture = *const_cast<CrossOwnerSurfaceFixture*>(static_cast<const CrossOwnerSurfaceFixture*>(descriptorData));
        privateState = &fixture;
        if (fixture.role == CrossOwnerSurfaceRole::Provider)
        {
            editor::EditorCommandFailure commandFailure;
            editor::EditorUiFailure uiFailure;
            if (!context.RegisterCommand({CrossOwnerSurfaceCommand, "crossOwnerSurfaceCommand", editor::EditorCommandFlags::None},
                                         fixture.commandToken, &commandFailure) ||
                !context.RegisterContextMenuSurface(CrossOwnerPrivateSurface, fixture.surfaceToken, &uiFailure))
                return false;

            editor::detail::EditorUiContributionDesc anchor;
            anchor.id = CrossOwnerPrivateMenuAnchor;
            anchor.surface = editor::detail::EditorMainMenuSurfaceId;
            anchor.kind = editor::detail::EditorUiContributionKind::Submenu;
            anchor.parent = editor::detail::EditorMainMenuToolsAnchorId;
            anchor.label = "Provider Private";
            fixture.providerRegistered =
                context.RegisterSurfaceContribution(anchor, fixture.anchorToken, &uiFailure);
            return fixture.providerRegistered;
        }

        editor::EditorUiFailure failure;
        editor::detail::EditorUiContributionToken rejected;
        editor::detail::EditorUiContributionDesc crossCommand;
        crossCommand.id = {0x181101u};
        crossCommand.surface = editor::detail::EditorMainMenuSurfaceId;
        crossCommand.kind = editor::detail::EditorUiContributionKind::Command;
        crossCommand.parent = editor::detail::EditorMainMenuFileAnchorId;
        crossCommand.command = CrossOwnerSurfaceCommand;
        crossCommand.label = "Cross Owner Command";
        fixture.crossCommandRejected =
            !context.RegisterSurfaceContribution(crossCommand, rejected, &failure) && !rejected.IsValid();
        fixture.crossCommandFailure = failure.code;

        rejected = {};
        failure = {};
        editor::detail::EditorUiContributionDesc privateSurface;
        privateSurface.id = {0x181102u};
        privateSurface.surface = CrossOwnerPrivateSurface;
        privateSurface.kind = editor::detail::EditorUiContributionKind::Separator;
        fixture.privateSurfaceRejected =
            !context.RegisterSurfaceContribution(privateSurface, rejected, &failure) && !rejected.IsValid();
        fixture.privateSurfaceFailure = failure.code;

        rejected = {};
        failure = {};
        editor::detail::EditorUiContributionDesc privateAnchor;
        privateAnchor.id = {0x181103u};
        privateAnchor.surface = editor::detail::EditorMainMenuSurfaceId;
        privateAnchor.kind = editor::detail::EditorUiContributionKind::Separator;
        privateAnchor.parent = CrossOwnerPrivateMenuAnchor;
        fixture.privateAnchorRejected =
            !context.RegisterSurfaceContribution(privateAnchor, rejected, &failure) && !rejected.IsValid();
        fixture.privateAnchorFailure = failure.code;
        return fixture.crossCommandRejected && fixture.privateSurfaceRejected && fixture.privateAnchorRejected;
    }

    void CrossOwnerSurfaceDeactivate(editor::EditorExtensionActivationContext&, const void*, void*) noexcept
    {
    }

    editor::EditorExtensionDescriptor CrossOwnerSurfaceDescriptor(
        const u64 id, const char* const name, CrossOwnerSurfaceFixture& fixture,
        const containers::ArraySpan<const editor::EditorExtensionId> dependencies = {}) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {id};
        descriptor.technicalName = name;
        descriptor.displayName = name;
        descriptor.requiredExtensions = dependencies;
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = CrossOwnerSurfaceActivate;
        descriptor.deactivate = CrossOwnerSurfaceDeactivate;
        descriptor.data = &fixture;
        return descriptor;
    }

    enum class CrossOwnerRole : u8
    {
        Provider,
        Consumer
    };

    struct CrossOwnerCommandFixture final
    {
        CrossOwnerRole role = CrossOwnerRole::Provider;
        app::EngineHost* host = nullptr;
        editor::EditorCommandId command{0xe100u};
        editor::EditorCommandContextTypeId contextType{0xe101u};
        bool privateStateAlive = false;
        bool registered = false;
        bool consumerRouted = false;
        bool cleanupOrderObserved = false;
        u32 queryCalls = 0;
        u32 deactivateCalls = 0;
        editor::EditorCommandToken commandToken;
        editor::EditorCommandContextTypeToken contextTypeToken;
        editor::EditorCommandHandlerToken handlerToken;
        editor::EditorCommandContextToken activeContextToken;
    };

    editor::EditorCommandState CrossOwnerQuery(void* const receiverData, void* const handlerData) noexcept
    {
        auto& fixture = *static_cast<CrossOwnerCommandFixture*>(handlerData);
        Check(fixture.privateStateAlive, "cross-owner handler data stays live while its consumer owner is reachable");
        Check(receiverData == &fixture, "cross-owner handler receives the consumer's active receiver");
        ++fixture.queryCalls;
        return editor::EditorCommandState::Enabled();
    }

    bool CrossOwnerInvoke(void*, void*) noexcept
    {
        return true;
    }

    bool CrossOwnerActivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                            void*& privateState) noexcept
    {
        auto& fixture = *const_cast<CrossOwnerCommandFixture*>(static_cast<const CrossOwnerCommandFixture*>(descriptorData));
        fixture.privateStateAlive = true;
        privateState = &fixture;
        editor::EditorCommandFailure failure;
        if (fixture.role == CrossOwnerRole::Provider)
        {
            fixture.registered =
                context.RegisterCommand({fixture.command, "crossOwnerCommand", editor::EditorCommandFlags::None},
                                        fixture.commandToken, &failure) &&
                context.RegisterContextType({fixture.contextType, "crossOwnerContext", editor::EditorCommandContextTier::Local},
                                            fixture.contextTypeToken, &failure);
            return fixture.registered;
        }

        fixture.registered =
            context.RegisterHandler({fixture.contextType, fixture.command, CrossOwnerQuery, CrossOwnerInvoke, &fixture},
                                    fixture.handlerToken, &failure);
        if (!fixture.registered || fixture.host == nullptr)
            return false;
        editor::EditorCommandService* const commands = editor::FindEditorCommandService(*fixture.host);
        if (commands == nullptr ||
            !commands->PushContext(fixture.contextType, &fixture, editor::EditorCommandContextMode::Inherit,
                                   fixture.activeContextToken, &failure))
            return false;
        editor::EditorCommandState state;
        fixture.consumerRouted =
            commands->Query(fixture.command, state, &failure) &&
            state.kind == editor::EditorCommandStateKind::Enabled && fixture.queryCalls == 1u;
        return fixture.consumerRouted;
    }

    void CrossOwnerDeactivate(editor::EditorExtensionActivationContext&, const void* const descriptorData,
                              void* const privateState) noexcept
    {
        auto& fixture = *const_cast<CrossOwnerCommandFixture*>(static_cast<const CrossOwnerCommandFixture*>(descriptorData));
        ++fixture.deactivateCalls;
        Check(privateState == &fixture && fixture.privateStateAlive,
              "cross-owner private state remains live through owner cleanup");
        editor::EditorCommandService* const commands =
            fixture.host != nullptr ? editor::FindEditorCommandService(*fixture.host) : nullptr;
        if (commands != nullptr)
        {
            const editor::EditorCommandServiceStatus status = commands->GetStatus();
            if (fixture.role == CrossOwnerRole::Consumer)
            {
                fixture.cleanupOrderObserved =
                    status.registeredCommandCount == 1u && status.registeredContextCount == 2u &&
                    status.registeredHandlerCount == 0u && status.activeContextCount == 1u;
            }
            else
            {
                fixture.cleanupOrderObserved =
                    status.registeredCommandCount == 0u && status.registeredContextCount == 1u &&
                    status.registeredHandlerCount == 0u && status.activeContextCount == 0u;
            }
        }
        fixture.privateStateAlive = false;
    }

    editor::EditorExtensionDescriptor CrossOwnerDescriptor(
        const u64 id, const char* const name, CrossOwnerCommandFixture& fixture,
        const containers::ArraySpan<const editor::EditorExtensionId> dependencies = {}) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {id};
        descriptor.technicalName = name;
        descriptor.displayName = name;
        descriptor.requiredExtensions = dependencies;
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = CrossOwnerActivate;
        descriptor.deactivate = CrossOwnerDeactivate;
        descriptor.data = &fixture;
        return descriptor;
    }

    struct MixedOwnedCapacityFixture final
    {
        app::EngineHost* host = nullptr;
        bool privateStateAlive = false;
        bool handlerReachedSharedLimit = false;
        bool extraHandlerRejectedAtSharedLimit = false;
        bool cleanupCompleteDuringDeactivate = false;
        u32 commandRegisteredCount = 0;
        u32 contextRegisteredCount = 0;
        u32 panelRegisteredCount = 0;
        u32 surfaceRegisteredCount = 0;
        u32 activityRegisteredCount = 0;
        u32 preferenceRegisteredCount = 0;
    };

    editor::EditorCommandState MixedCapacityQuery(void*, void*) noexcept
    {
        return editor::EditorCommandState::Enabled();
    }

    bool MixedCapacityInvoke(void*, void*) noexcept
    {
        return true;
    }

    void MixedCapacityPanelDraw(editor::EditorPanelContext&, void*, void*) noexcept
    {
    }

    bool MixedCapacityActivityQuery(editor::detail::EditorActivitySourceWriter&, void*,
                                    editor::EditorActivityFailure*) noexcept
    {
        return true;
    }

    u64 MixedCapacityPreferenceRevision(void*) noexcept
    {
        return 1;
    }

    bool MixedCapacityPreferenceCapture(void*, editor::EditorPreferenceFieldId,
                                        editor::EditorPreferenceFieldValue& value) noexcept
    {
        value = {};
        value.type = editor::EditorPreferenceValueType::Bool;
        value.value.boolean = false;
        return true;
    }

    bool MixedCapacityPreferenceValidate(void*, editor::EditorPreferenceFieldId,
                                         const editor::EditorPreferenceFieldValue& value) noexcept
    {
        return value.type == editor::EditorPreferenceValueType::Bool;
    }

    void MixedCapacityPreferenceApply(void*, editor::EditorPreferenceFieldId,
                                      const editor::EditorPreferenceFieldValue&) noexcept
    {
    }

    bool MixedOwnedCapacityActivate(editor::EditorExtensionActivationContext& context, const void* const descriptorData,
                                    void*& privateState) noexcept
    {
        auto& fixture = *const_cast<MixedOwnedCapacityFixture*>(static_cast<const MixedOwnedCapacityFixture*>(descriptorData));
        fixture.privateStateAlive = true;
        privateState = &fixture;

        editor::EditorCommandFailure commandFailure;
        for (u32 index = 0; index < editor::MaximumEditorCommands; ++index)
        {
            editor::EditorCommandToken token;
            if (!context.RegisterCommand({{0xf1000u + index}, "ledgerCommand", editor::EditorCommandFlags::None}, token,
                                         &commandFailure))
                return false;
            ++fixture.commandRegisteredCount;
        }

        constexpr u32 OwnedContextCount = editor::MaximumEditorCommandContextTypes - 1u;
        for (u32 index = 0; index < OwnedContextCount; ++index)
        {
            editor::EditorCommandContextTypeToken token;
            if (!context.RegisterContextType({{0xf2000u + index}, "ledgerContext", editor::EditorCommandContextTier::Local}, token,
                                             &commandFailure))
                return false;
            ++fixture.contextRegisteredCount;
        }

        constexpr u32 OwnedPanelCount = editor::MaximumEditorPanelTypes - 3u;
        for (u32 index = 0; index < OwnedPanelCount; ++index)
        {
            editor::EditorPanelTypeDesc panelType;
            panelType.id = 0xf3000u + index;
            panelType.name = "ledgerPanel";
            panelType.draw = MixedCapacityPanelDraw;
            editor::EditorPanelTypeToken token;
            editor::EditorUiFailure panelFailure;
            if (!context.RegisterPanelType(panelType, token, &panelFailure))
                return false;
            ++fixture.panelRegisteredCount;
        }

        editor::EditorUiFailure surfaceFailure;
        constexpr editor::EditorUiSurfaceId ledgerSurface{0xf4000u};
        editor::detail::EditorUiSurfaceToken surfaceToken;
        if (!context.RegisterContextMenuSurface(ledgerSurface, surfaceToken, &surfaceFailure))
            return false;
        ++fixture.surfaceRegisteredCount;

        editor::detail::EditorUiContributionDesc surfaceContribution;
        surfaceContribution.id = {0xf4001u};
        surfaceContribution.surface = ledgerSurface;
        surfaceContribution.kind = editor::detail::EditorUiContributionKind::Command;
        surfaceContribution.command = {0xf1000u};
        surfaceContribution.label = "ledgerSurfaceCommand";
        editor::detail::EditorUiContributionToken surfaceContributionToken;
        if (!context.RegisterSurfaceContribution(surfaceContribution, surfaceContributionToken, &surfaceFailure))
            return false;
        ++fixture.surfaceRegisteredCount;

        editor::detail::EditorActivitySourceDescriptor activitySource;
        activitySource.id = 0xf5000u;
        activitySource.displayName = "ledgerActivity";
        activitySource.query = &MixedCapacityActivityQuery;
        editor::detail::EditorActivitySourceToken activityToken;
        editor::EditorActivityFailure activityFailure;
        if (!context.RegisterActivitySource(activitySource, activityToken, &activityFailure))
            return false;
        ++fixture.activityRegisteredCount;

        const editor::EditorPreferenceFieldDescriptor preferenceField{
            0xf6001u, editor::EditorPreferenceValueType::Bool, 0};
        editor::EditorPreferenceProviderDescriptor preferenceProvider;
        preferenceProvider.providerId = 0xf6000u;
        preferenceProvider.fields = {&preferenceField, 1};
        preferenceProvider.queryRevision = MixedCapacityPreferenceRevision;
        preferenceProvider.capture = MixedCapacityPreferenceCapture;
        preferenceProvider.validate = MixedCapacityPreferenceValidate;
        preferenceProvider.apply = MixedCapacityPreferenceApply;
        editor::EditorPreferenceProviderToken preferenceToken;
        editor::EditorPreferenceFailure preferenceFailure;
        if (!context.RegisterPreferenceProvider(preferenceProvider, preferenceToken, &preferenceFailure))
            return false;
        ++fixture.preferenceRegisteredCount;

        constexpr editor::EditorCommandId firstCommand{0xf1000u};
        constexpr editor::EditorCommandContextTypeId firstContext{0xf2000u};
        editor::EditorCommandHandlerToken rejected;
        commandFailure = {};
        fixture.extraHandlerRejectedAtSharedLimit =
            !context.RegisterHandler({firstContext, firstCommand, MixedCapacityQuery, MixedCapacityInvoke, nullptr},
                                     rejected, &commandFailure) &&
            commandFailure.code == editor::EditorCommandFailureCode::CapacityExceeded && !rejected.IsValid();
        fixture.handlerReachedSharedLimit = fixture.extraHandlerRejectedAtSharedLimit;
        return false;
    }

    void MixedOwnedCapacityDeactivate(editor::EditorExtensionActivationContext&, const void* const descriptorData,
                                      void* const privateState) noexcept
    {
        auto& fixture = *const_cast<MixedOwnedCapacityFixture*>(static_cast<const MixedOwnedCapacityFixture*>(descriptorData));
        Check(privateState == &fixture && fixture.privateStateAlive,
              "shared-ledger rollback retains extension private state through owned-registration cleanup");
        editor::EditorCommandService* const commands =
            fixture.host != nullptr ? editor::FindEditorCommandService(*fixture.host) : nullptr;
        if (commands != nullptr)
        {
            const editor::EditorCommandServiceStatus status = commands->GetStatus();
            fixture.cleanupCompleteDuringDeactivate =
                status.registeredCommandCount == 0u && status.registeredContextCount == 1u &&
                status.registeredHandlerCount == 0u && status.activeContextCount == 0u;
        }
        fixture.privateStateAlive = false;
    }

    editor::EditorExtensionDescriptor MixedOwnedCapacityDescriptor(MixedOwnedCapacityFixture& fixture) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {71u};
        descriptor.technicalName = "mixed-capacity";
        descriptor.displayName = "mixed-capacity";
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = MixedOwnedCapacityActivate;
        descriptor.deactivate = MixedOwnedCapacityDeactivate;
        descriptor.data = &fixture;
        return descriptor;
    }

    enum class PreferenceCapacityMode : u8
    {
        Providers,
        Fields
    };

    struct PreferenceCapacityFixture final
    {
        PreferenceCapacityMode mode = PreferenceCapacityMode::Providers;
        u32 registeredProviders = 0;
        u32 registeredFields = 0;
        bool invalidDescriptorMatrixRejected = false;
        bool duplicateProviderRejected = false;
        bool overflowRejected = false;
    };

    [[nodiscard]] editor::EditorPreferenceProviderDescriptor PreferenceCapacityDescriptor(
        const editor::EditorPreferenceProviderId providerId,
        const containers::ArraySpan<const editor::EditorPreferenceFieldDescriptor> fields) noexcept
    {
        editor::EditorPreferenceProviderDescriptor descriptor;
        descriptor.providerId = providerId;
        descriptor.fields = fields;
        descriptor.queryRevision = MixedCapacityPreferenceRevision;
        descriptor.capture = MixedCapacityPreferenceCapture;
        descriptor.validate = MixedCapacityPreferenceValidate;
        descriptor.apply = MixedCapacityPreferenceApply;
        return descriptor;
    }

    bool PreferenceCapacityActivate(editor::EditorExtensionActivationContext& context,
                                    const void* const descriptorData, void*& privateState) noexcept
    {
        auto& fixture = *const_cast<PreferenceCapacityFixture*>(
            static_cast<const PreferenceCapacityFixture*>(descriptorData));
        privateState = &fixture;
        editor::EditorPreferenceFailure failure;
        editor::EditorPreferenceProviderToken token;

        if (fixture.mode == PreferenceCapacityMode::Providers)
        {
            const editor::EditorPreferenceFieldDescriptor field{
                0xf7101u, editor::EditorPreferenceValueType::Bool, 0};
            for (u32 index = 0; index < editor::MaximumEditorPreferenceProviders; ++index)
            {
                const editor::EditorPreferenceProviderDescriptor descriptor =
                    PreferenceCapacityDescriptor(0xf7200u + index, {&field, 1});
                token = {};
                if (!context.RegisterPreferenceProvider(descriptor, token, &failure) || !token.IsValid())
                    return false;
                ++fixture.registeredProviders;
                ++fixture.registeredFields;
                if (index == 0)
                {
                    editor::EditorPreferenceProviderToken duplicate;
                    fixture.duplicateProviderRejected =
                        !context.RegisterPreferenceProvider(descriptor, duplicate, &failure) &&
                        failure.code == editor::EditorPreferenceFailureCode::DuplicateProvider &&
                        !duplicate.IsValid();
                }
            }
            const editor::EditorPreferenceProviderDescriptor overflow =
                PreferenceCapacityDescriptor(0xf7ff0u, {&field, 1});
            token = {};
            fixture.overflowRejected =
                !context.RegisterPreferenceProvider(overflow, token, &failure) &&
                failure.code == editor::EditorPreferenceFailureCode::CapacityExceeded && !token.IsValid();
            return true;
        }

        editor::EditorPreferenceFieldDescriptor fields[editor::MaximumEditorPreferenceFieldsPerProvider]{};
        for (u32 index = 0; index < editor::MaximumEditorPreferenceFieldsPerProvider; ++index)
            fields[index] = {0xf8000u + index + 1u, editor::EditorPreferenceValueType::Bool, 0};

        editor::EditorPreferenceFieldDescriptor tooMany[editor::MaximumEditorPreferenceFieldsPerProvider + 1u]{};
        for (u32 index = 0; index < editor::MaximumEditorPreferenceFieldsPerProvider + 1u; ++index)
            tooMany[index] = {0xf8100u + index + 1u, editor::EditorPreferenceValueType::Bool, 0};
        editor::EditorPreferenceFieldDescriptor duplicateFields[]{
            {0xf8201u, editor::EditorPreferenceValueType::Bool, 0},
            {0xf8201u, editor::EditorPreferenceValueType::Bool, 0}};
        editor::EditorPreferenceFieldDescriptor zeroField{
            0, editor::EditorPreferenceValueType::Bool, 0};
        editor::EditorPreferenceFieldDescriptor badType{
            0xf8202u, static_cast<editor::EditorPreferenceValueType>(0xffu), 0};
        editor::EditorPreferenceFieldDescriptor oversizedString{
            0xf8203u, editor::EditorPreferenceValueType::Utf8String,
            editor::MaximumEditorPreferenceStringBytes + 1u};

        bool invalidMatrix = true;
        auto invalid = PreferenceCapacityDescriptor(0, {fields, 1});
        token = {};
        invalidMatrix = invalidMatrix &&
            !context.RegisterPreferenceProvider(invalid, token, &failure) &&
            failure.code == editor::EditorPreferenceFailureCode::InvalidDescriptor && !token.IsValid();
        invalid = PreferenceCapacityDescriptor(0xf8300u, {tooMany, editor::MaximumEditorPreferenceFieldsPerProvider + 1u});
        token = {};
        invalidMatrix = invalidMatrix &&
            !context.RegisterPreferenceProvider(invalid, token, &failure) &&
            failure.code == editor::EditorPreferenceFailureCode::InvalidDescriptor && !token.IsValid();
        invalid = PreferenceCapacityDescriptor(0xf8301u, {duplicateFields, 2});
        token = {};
        invalidMatrix = invalidMatrix &&
            !context.RegisterPreferenceProvider(invalid, token, &failure) &&
            failure.code == editor::EditorPreferenceFailureCode::InvalidDescriptor && !token.IsValid();
        invalid = PreferenceCapacityDescriptor(0xf8302u, {&zeroField, 1});
        token = {};
        invalidMatrix = invalidMatrix &&
            !context.RegisterPreferenceProvider(invalid, token, &failure) &&
            failure.code == editor::EditorPreferenceFailureCode::InvalidDescriptor && !token.IsValid();
        invalid = PreferenceCapacityDescriptor(0xf8303u, {&badType, 1});
        token = {};
        invalidMatrix = invalidMatrix &&
            !context.RegisterPreferenceProvider(invalid, token, &failure) &&
            failure.code == editor::EditorPreferenceFailureCode::InvalidDescriptor && !token.IsValid();
        invalid = PreferenceCapacityDescriptor(0xf8304u, {&oversizedString, 1});
        token = {};
        invalidMatrix = invalidMatrix &&
            !context.RegisterPreferenceProvider(invalid, token, &failure) &&
            failure.code == editor::EditorPreferenceFailureCode::InvalidDescriptor && !token.IsValid();
        invalid = PreferenceCapacityDescriptor(0xf8305u, {fields, 1});
        invalid.capture = nullptr;
        token = {};
        invalidMatrix = invalidMatrix &&
            !context.RegisterPreferenceProvider(invalid, token, &failure) &&
            failure.code == editor::EditorPreferenceFailureCode::InvalidDescriptor && !token.IsValid();
        fixture.invalidDescriptorMatrixRejected = invalidMatrix;

        constexpr u32 FullProviders =
            editor::MaximumEditorLivePreferenceFields / editor::MaximumEditorPreferenceFieldsPerProvider;
        for (u32 index = 0; index < FullProviders; ++index)
        {
            const editor::EditorPreferenceProviderDescriptor descriptor =
                PreferenceCapacityDescriptor(0xf8400u + index,
                                             {fields, editor::MaximumEditorPreferenceFieldsPerProvider});
            token = {};
            if (!context.RegisterPreferenceProvider(descriptor, token, &failure) || !token.IsValid())
                return false;
            ++fixture.registeredProviders;
            fixture.registeredFields += editor::MaximumEditorPreferenceFieldsPerProvider;
        }
        const editor::EditorPreferenceProviderDescriptor overflow =
            PreferenceCapacityDescriptor(0xf8500u, {fields, 1});
        token = {};
        fixture.overflowRejected =
            !context.RegisterPreferenceProvider(overflow, token, &failure) &&
            failure.code == editor::EditorPreferenceFailureCode::CapacityExceeded && !token.IsValid();
        return true;
    }

    void PreferenceCapacityDeactivate(editor::EditorExtensionActivationContext&, const void* const descriptorData,
                                      void* const privateState) noexcept
    {
        Check(privateState == descriptorData,
              "preference-capacity fixture retains extension private state until owned provider cleanup completes");
    }

    editor::EditorExtensionDescriptor PreferenceCapacityExtension(PreferenceCapacityFixture& fixture,
                                                                  const u64 id,
                                                                  const char* const name) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {id};
        descriptor.technicalName = name;
        descriptor.displayName = name;
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = PreferenceCapacityActivate;
        descriptor.deactivate = PreferenceCapacityDeactivate;
        descriptor.data = &fixture;
        return descriptor;
    }

    bool DummyActivate(editor::EditorExtensionActivationContext&, const void*, void*&) noexcept
    {
        ++g_activateCalls;
        return true;
    }

    void DummyDeactivate(editor::EditorExtensionActivationContext&, const void*, void*) noexcept
    {
        ++g_deactivateCalls;
    }

    bool RecordingActivate(editor::EditorExtensionActivationContext& context, const void* descriptorData, void*& privateState) noexcept
    {
        ++g_activateCalls;
        if (g_activationOrderCount < editor::MaximumEditorExtensions)
            g_activationOrder[g_activationOrderCount++] = context.GetExtension().value;
        if (g_observedActivationGeneration == 0)
            g_observedActivationGeneration = context.GetActivationGeneration();
        else
            Check(g_observedActivationGeneration == context.GetActivationGeneration(), "one shell start uses one activation generation");
        g_observedWorkspace = context.GetWorkspace().GetProject().technicalName == "fixture-project";
        privateState = const_cast<void*>(descriptorData);
        return true;
    }

    void RecordingDeactivate(editor::EditorExtensionActivationContext& context, const void*, void*) noexcept
    {
        ++g_deactivateCalls;
        if (g_deactivationOrderCount < editor::MaximumEditorExtensions)
            g_deactivationOrder[g_deactivationOrderCount++] = context.GetExtension().value;
        Check(context.GetActivationGeneration() == g_observedActivationGeneration, "deactivation preserves the shell activation generation");
    }

    editor::EditorExtensionDescriptor Descriptor(const vanguard::u64 id, const char* const name,
                                                 const editor::EditorExtensionSelectionPolicy policy =
                                                     editor::EditorExtensionSelectionPolicy::DependencyOnly,
                                                 const containers::ArraySpan<const editor::EditorExtensionId> dependencies = {}) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {id};
        descriptor.technicalName = name;
        descriptor.displayName = name;
        descriptor.requiredExtensions = dependencies;
        descriptor.selection = policy;
        descriptor.activate = DummyActivate;
        descriptor.deactivate = DummyDeactivate;
        return descriptor;
    }

    editor::EditorExtensionDescriptor RecordingDescriptor(
        const vanguard::u64 id, const char* const name, const editor::EditorExtensionSelectionPolicy policy,
        const containers::ArraySpan<const editor::EditorExtensionId> dependencies = {}) noexcept
    {
        editor::EditorExtensionDescriptor descriptor = Descriptor(id, name, policy, dependencies);
        descriptor.activate = RecordingActivate;
        descriptor.deactivate = RecordingDeactivate;
        descriptor.data = name;
        return descriptor;
    }

    bool PreferenceFixtureActivate(editor::EditorExtensionActivationContext& context, const void*,
                                   void*& privateState) noexcept
    {
        privateState = nullptr;
        const editor::EditorPreferenceFieldDescriptor fields[]{
            {PreferenceFixtureBoolFieldId, editor::EditorPreferenceValueType::Bool, 0},
            {PreferenceFixtureSignedFieldId, editor::EditorPreferenceValueType::SignedInteger, 0}};
        editor::EditorPreferenceProviderDescriptor descriptor;
        descriptor.providerId = PreferenceFixtureProviderId;
        descriptor.fields = fields;
        descriptor.queryRevision = PreferenceFixtureRevision;
        descriptor.capture = PreferenceFixtureCapture;
        descriptor.validate = PreferenceFixtureValidate;
        descriptor.apply = PreferenceFixtureApply;
        editor::EditorPreferenceProviderToken token;
        editor::EditorPreferenceFailure failure;
        if (!context.RegisterPreferenceProvider(descriptor, token, &failure))
            return false;
        g_preferenceProviderToken = token;
        return true;
    }

    void PreferenceFixtureDeactivate(editor::EditorExtensionActivationContext& context, const void*,
                                     void* const privateState) noexcept
    {
        Check(privateState == nullptr,
              "stateless preference extension retains null private state through deactivation");
        editor::EditorPreferenceFailure failure;
        g_preferenceDeactivateSawRevokedProvider =
            !context.UnregisterPreferenceProvider(g_preferenceProviderToken, &failure) &&
            failure.code == editor::EditorPreferenceFailureCode::OwnerRevoked;
    }

    editor::EditorExtensionDescriptor PreferenceFixtureDescriptor() noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = {0xe600u};
        descriptor.technicalName = "preference-fixture";
        descriptor.displayName = "preference-fixture";
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = PreferenceFixtureActivate;
        descriptor.deactivate = PreferenceFixtureDeactivate;
        return descriptor;
    }

    void CheckOrder(const editor::EditorExtensionSelection& selection, const char* const first, const char* const second = nullptr,
                    const char* const third = nullptr) noexcept
    {
        const auto order = selection.GetActivationOrder();
        const u32 expected = third != nullptr ? 3u : (second != nullptr ? 2u : 1u);
        Check(order.Size() == expected, "selection has expected activation count");
        if (order.Size() != expected)
            return;
        Check(containers::StringView(order[0]->technicalName) == first, "selection first entry is deterministic");
        if (second != nullptr)
            Check(containers::StringView(order[1]->technicalName) == second, "selection second entry is deterministic");
        if (third != nullptr)
            Check(containers::StringView(order[2]->technicalName) == third, "selection third entry is deterministic");
    }

    void TestValidCatalogAndSelection() noexcept
    {
        constexpr editor::EditorExtensionId betaId{2};
        const editor::EditorExtensionId gammaDependencies[]{betaId};
        const editor::EditorExtensionDescriptor descriptors[]{
            Descriptor(3, "gamma", editor::EditorExtensionSelectionPolicy::ProjectSelectable, gammaDependencies),
            Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn),
            Descriptor(2, "beta")};

        editor::EditorExtensionCatalog catalog;
        editor::EditorExtensionFailure failure;
        Check(catalog.Initialize(descriptors, &failure), "valid catalog initializes");
        Check(catalog.GetStatus().state == editor::EditorExtensionCatalogState::Ready && catalog.GetStatus().descriptorCount == 3,
              "valid catalog publishes ready bounded status");
        Check(catalog.Lookup(editor::EditorExtensionId{2}) == &descriptors[2], "catalog lookup by stable id resolves borrowed descriptor");
        Check(catalog.Lookup("gamma") == &descriptors[0], "catalog lookup by technical name resolves borrowed descriptor");

        containers::String requested[]{containers::String("gamma")};
        editor::EditorExtensionSelection selection;
        Check(catalog.ResolveSelection(requested, selection, &failure), "known project selection resolves");
        CheckOrder(selection, "alpha", "beta", "gamma");
        Check(g_activateCalls == 0 && g_deactivateCalls == 0, "catalog resolution never invokes extension lifecycle callbacks");

        const editor::EditorExtensionDescriptor permuted[]{
            Descriptor(2, "beta"),
            Descriptor(3, "gamma", editor::EditorExtensionSelectionPolicy::ProjectSelectable, gammaDependencies),
            Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn)};
        editor::EditorExtensionCatalog secondCatalog;
        editor::EditorExtensionSelection secondSelection;
        Check(secondCatalog.Initialize(permuted, &failure) && secondCatalog.ResolveSelection(requested, secondSelection, &failure),
              "permuted valid catalog resolves");
        CheckOrder(secondSelection, "alpha", "beta", "gamma");

        editor::EditorExtensionCatalog emptyCatalog;
        const containers::ArraySpan<const editor::EditorExtensionDescriptor> emptyDescriptors;
        editor::EditorExtensionSelection emptySelection;
        const containers::ArraySpan<const containers::String> noRequests;
        Check(emptyCatalog.Initialize(emptyDescriptors, &failure) && emptyCatalog.ResolveSelection(noRequests, emptySelection, &failure) &&
                  emptySelection.Empty(),
              "empty catalog and selection are valid");
    }

    void TestDescriptorFailures() noexcept
    {
        editor::EditorExtensionFailure failure;
        editor::EditorExtensionCatalog catalog;

        Check(!catalog.Initialize({nullptr, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "non-empty descriptor span with null backing is rejected");

        editor::EditorExtensionDescriptor invalid = Descriptor(1, "alpha");
        invalid.id = {};
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "zero extension identity is rejected");
        invalid = Descriptor(1, "alpha");
        invalid.technicalName = nullptr;
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "null technical name is rejected");
        invalid = Descriptor(1, "");
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "empty technical name is rejected");
        invalid = Descriptor(1, "Alpha");
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "non-canonical technical name is rejected through projects authority");
        char overlongTechnicalName[vanguard::projects::MaximumTechnicalNameBytes + 2]{};
        for (u32 index = 0; index < vanguard::projects::MaximumTechnicalNameBytes + 1; ++index)
            overlongTechnicalName[index] = 'a';
        invalid = Descriptor(1, overlongTechnicalName);
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "technical-name validation is bounded by the projects-owned maximum");
        invalid = Descriptor(1, "alpha");
        invalid.displayName = "";
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "empty display name is rejected");
        invalid = Descriptor(1, "alpha");
        invalid.displayName = nullptr;
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "null display name is rejected");
        char overlongDisplayName[editor::MaximumEditorExtensionDisplayNameBytes + 2]{};
        for (u32 index = 0; index < editor::MaximumEditorExtensionDisplayNameBytes + 1; ++index)
            overlongDisplayName[index] = 'a';
        invalid = Descriptor(1, "alpha");
        invalid.displayName = overlongDisplayName;
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "overlong display name is rejected before unbounded reads");
        invalid = Descriptor(1, "alpha");
        invalid.activate = nullptr;
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "missing lifecycle callback is rejected");
        invalid = Descriptor(1, "alpha");
        invalid.deactivate = nullptr;
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "missing deactivation callback is rejected");
        invalid = Descriptor(1, "alpha");
        invalid.selection = static_cast<editor::EditorExtensionSelectionPolicy>(static_cast<u8>(0x80));
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "unknown selection-policy bits are rejected");

        const editor::EditorExtensionDescriptor duplicateId[]{Descriptor(1, "alpha"), Descriptor(1, "beta")};
        Check(!catalog.Initialize(duplicateId, &failure) && failure.code == editor::EditorExtensionFailureCode::DuplicateIdentity,
              "duplicate stable identities are rejected");
        const editor::EditorExtensionDescriptor duplicateName[]{Descriptor(1, "alpha"), Descriptor(2, "alpha")};
        Check(!catalog.Initialize(duplicateName, &failure) && failure.code == editor::EditorExtensionFailureCode::DuplicateTechnicalName,
              "duplicate technical names are rejected");

        constexpr editor::EditorExtensionId missingId{99};
        const editor::EditorExtensionId missingDependencies[]{missingId};
        const editor::EditorExtensionDescriptor missing[]{Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn, missingDependencies)};
        Check(!catalog.Initialize(missing, &failure) && failure.code == editor::EditorExtensionFailureCode::MissingDependency &&
                  failure.relatedExtension == missingId,
              "missing dependency is rejected with stable related identity");

        const editor::EditorExtensionId zeroDependency[]{{}};
        const editor::EditorExtensionDescriptor invalidDependency[]{
            Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn, zeroDependency)};
        Check(!catalog.Initialize(invalidDependency, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "zero dependency identity is rejected");

        constexpr editor::EditorExtensionId selfId{1};
        const editor::EditorExtensionId selfDependencies[]{selfId};
        const editor::EditorExtensionDescriptor self[]{Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn, selfDependencies)};
        Check(!catalog.Initialize(self, &failure) && failure.code == editor::EditorExtensionFailureCode::SelfDependency,
              "self dependency is rejected");

        constexpr editor::EditorExtensionId betaId{2};
        const editor::EditorExtensionId duplicateDependencies[]{betaId, betaId};
        const editor::EditorExtensionDescriptor duplicateDependency[]{
            Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn, duplicateDependencies), Descriptor(2, "beta")};
        Check(!catalog.Initialize(duplicateDependency, &failure) && failure.code == editor::EditorExtensionFailureCode::DuplicateDependency,
              "duplicate dependency entries are rejected");

        constexpr editor::EditorExtensionId alphaId{1};
        const editor::EditorExtensionId alphaNeedsBeta[]{betaId};
        const editor::EditorExtensionId betaNeedsAlpha[]{alphaId};
        const editor::EditorExtensionDescriptor cycle[]{Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn, alphaNeedsBeta),
                                                        Descriptor(2, "beta", editor::EditorExtensionSelectionPolicy::DependencyOnly, betaNeedsAlpha)};
        Check(!catalog.Initialize(cycle, &failure) && failure.code == editor::EditorExtensionFailureCode::DependencyCycle,
              "dependency cycle is rejected before activation");

        editor::EditorExtensionId tooManyDependencies[editor::MaximumEditorExtensionDependencies + 1]{};
        invalid = Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn,
                             {tooManyDependencies, editor::MaximumEditorExtensionDependencies + 1});
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "dependency count is rejected before bounded append");

        invalid = Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn);
        invalid.requiredExtensions = {nullptr, 1};
        Check(!catalog.Initialize({&invalid, 1}, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidDescriptor,
              "non-empty dependency span with null backing is rejected");

        editor::EditorExtensionDescriptor tooMany[editor::MaximumEditorExtensions + 1]{};
        Check(!catalog.Initialize({tooMany, editor::MaximumEditorExtensions + 1}, &failure) &&
                  failure.code == editor::EditorExtensionFailureCode::CapacityExceeded,
              "catalog count is rejected before bounded append");
    }

    void TestProjectSelectionFailures() noexcept
    {
        const editor::EditorExtensionDescriptor descriptors[]{
            Descriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn),
            Descriptor(2, "beta"),
            Descriptor(3, "gamma", editor::EditorExtensionSelectionPolicy::ProjectSelectable)};
        editor::EditorExtensionCatalog catalog;
        editor::EditorExtensionFailure failure;
        Check(catalog.Initialize(descriptors, &failure), "selection-failure fixture initializes");

        editor::EditorExtensionSelection selection;
        containers::String invalid[]{containers::String("Gamma")};
        Check(!catalog.ResolveSelection(invalid, selection, &failure) && failure.code == editor::EditorExtensionFailureCode::InvalidProjectSelection,
              "invalid project technical name is rejected");
        containers::String unknown[]{containers::String("missing")};
        Check(!catalog.ResolveSelection(unknown, selection, &failure) && failure.code == editor::EditorExtensionFailureCode::UnknownProjectSelection,
              "unknown compiled extension selection is rejected");
        containers::String unavailable[]{containers::String("beta")};
        Check(!catalog.ResolveSelection(unavailable, selection, &failure) && failure.code == editor::EditorExtensionFailureCode::ProjectSelectionNotAllowed,
              "dependency-only extension cannot be selected directly by project");
        containers::String duplicate[]{containers::String("gamma"), containers::String("gamma")};
        Check(!catalog.ResolveSelection(duplicate, selection, &failure) && failure.code == editor::EditorExtensionFailureCode::DuplicateProjectSelection,
              "duplicate explicit project selections are rejected");

        containers::String tooMany[vanguard::projects::MaximumPlugins + 1];
        Check(!catalog.ResolveSelection({tooMany, vanguard::projects::MaximumPlugins + 1}, selection, &failure) &&
                  failure.code == editor::EditorExtensionFailureCode::CapacityExceeded,
              "project selection count is rejected before bounded work");

        editor::EditorExtensionCatalog notReady;
        containers::String gamma[]{containers::String("gamma")};
        Check(!notReady.ResolveSelection(gamma, selection, &failure) && failure.code == editor::EditorExtensionFailureCode::CatalogNotReady,
              "selection refuses an uninitialized catalog");
    }

    inline constexpr assets::SourceImporterId AssetBrowserOracleImporterId = 0x617373657462726full;
    inline constexpr u32 AssetBrowserOracleImporterVersion = 1u;
    inline constexpr resources::ResourceTypeId AssetBrowserOracleSourceType = 0x62727331u;
    inline constexpr resources::ResourceTypeId AssetBrowserOracleOutputType = 0x62726f31u;

    [[nodiscard]] bool RecognizeAssetBrowserOracleSource(containers::StringView, void*) noexcept
    {
        return true;
    }

    [[nodiscard]] filesystem::AbsolutePath AssetBrowserOracleMetadataPath(
        const filesystem::AbsolutePath& source) noexcept
    {
        const containers::StringView sourcePath = source.AsStringView();
        containers::String path(sourcePath.Data(), sourcePath.Length());
        path.Append(".vmeta", 6u);
        return filesystem::AbsolutePath::CreateFilePath(containers::StringView(path.AsChar(), path.Length()));
    }

    [[nodiscard]] bool WriteAssetBrowserOracleBytes(const filesystem::AbsolutePath& path,
                                                    const containers::StringView bytes) noexcept
    {
        auto writer = filesystem::RawFileWriter::Create(path, false);
        if (!writer)
            return false;
        if (!bytes.Empty())
            writer->Serialize(const_cast<char*>(bytes.Data()), bytes.Length());
        writer->Flush();
        return writer->GetSize() == bytes.Length();
    }

    [[nodiscard]] assets::AssetMetadata AssetBrowserOracleMetadata(const assets::AssetId id) noexcept
    {
        assets::AssetMetadata metadata;
        metadata.id = id;
        metadata.importer = AssetBrowserOracleImporterId;
        metadata.importerVersion = AssetBrowserOracleImporterVersion;
        metadata.settingsVersion = 1u;
        metadata.settings.PushBack(0x5au);
        assets::AssetOutput output;
        output.key = containers::String("main");
        output.type = AssetBrowserOracleOutputType;
        metadata.outputs.PushBack(static_cast<assets::AssetOutput&&>(output));
        return metadata;
    }

    [[nodiscard]] bool WriteAssetBrowserOracleMetadata(const filesystem::AbsolutePath& source,
                                                       const assets::AssetMetadata& metadata) noexcept
    {
        containers::String text;
        return assets::EncodeMetadata(metadata, text) == assets::MetadataResult::Success &&
               WriteAssetBrowserOracleBytes(
                   AssetBrowserOracleMetadataPath(source), containers::StringView(text.AsChar(), text.Length()));
    }

    void RemoveAssetBrowserOracleFixture(filesystem::Manager& files, const filesystem::AbsolutePath& root) noexcept
    {
        const filesystem::AbsolutePath assetsRoot = root.AddDirPath("Assets");
        const filesystem::AbsolutePath emptyNested = assetsRoot.AddDirPath("Empty").AddDirPath("Nested");
        const filesystem::AbsolutePath empty = assetsRoot.AddDirPath("Empty");
        const filesystem::AbsolutePath occupied = assetsRoot.AddDirPath("Occupied");
        const filesystem::AbsolutePath adopted = assetsRoot.AddFilePath("adopted.browser");
        const filesystem::AbsolutePath moved = assetsRoot.AddFilePath("moved-adopted.browser");
        const filesystem::AbsolutePath candidate = occupied.AddFilePath("candidate.browser");
        static_cast<void>(files.DeleteFile(adopted));
        static_cast<void>(files.DeleteFile(AssetBrowserOracleMetadataPath(adopted)));
        static_cast<void>(files.DeleteFile(moved));
        static_cast<void>(files.DeleteFile(AssetBrowserOracleMetadataPath(moved)));
        static_cast<void>(files.DeleteFile(candidate));
        static_cast<void>(files.DeleteFile(AssetBrowserOracleMetadataPath(candidate)));
        static_cast<void>(files.DeletePath(emptyNested));
        static_cast<void>(files.DeletePath(empty));
        static_cast<void>(files.DeletePath(occupied));
        static_cast<void>(files.DeletePath(assetsRoot));
        static_cast<void>(files.DeletePath(root));
    }

    [[nodiscard]] assetStreaming::ProjectAssetCatalogResult DrainAssetBrowserOracleCatalog(
        assetStreaming::ProjectAssetCatalog& catalog, const assets::SourceDatabase& sources) noexcept
    {
        assetStreaming::ProjectAssetCatalogResult result = assetStreaming::ProjectAssetCatalogResult::Pending;
        for (u32 attempt = 0u; attempt < 10000u && result == assetStreaming::ProjectAssetCatalogResult::Pending; ++attempt)
        {
            result = catalog.Refresh(sources, 64u, 64u * 1024u);
            if (result == assetStreaming::ProjectAssetCatalogResult::Pending)
                vanguard::concurrency::YieldCurrentThread();
        }
        return result;
    }

    class FixtureWorkspaceService final : public editor::ProjectWorkspaceService
    {
    public:
        explicit FixtureWorkspaceService(vanguard::projects::ProjectDescriptor& project) noexcept : m_project(project) {}

        [[nodiscard]] const vanguard::projects::ProjectDescriptor& GetProject() const noexcept override { return m_project; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetProjectFile() const noexcept override { return m_path; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetProjectRoot() const noexcept override { return m_path; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetAssetsRoot() const noexcept override { return m_path; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetDerivedDataRoot() const noexcept override { return m_path; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetIntermediateRoot() const noexcept override { return m_path; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetSavedRoot() const noexcept override { return m_path; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetBuildsRoot() const noexcept override { return m_path; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetConfigRoot() const noexcept override { return m_path; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetPluginsRoot() const noexcept override
        {
            ++g_pluginsRootQueries;
            return m_path;
        }
        [[nodiscard]] const vanguard::assets::SourceDatabase& GetSources() const noexcept override
        {
            std::abort();
        }
        [[nodiscard]] const vanguard::asset_streaming::ProjectAssetCatalog& GetAssetCatalog() const noexcept override
        {
            return m_catalog;
        }
        [[nodiscard]] vanguard::asset_streaming::ProjectAssetCatalog& GetMutableAssetCatalog() noexcept
        {
            return m_catalog;
        }
        [[nodiscard]] u32 GetTexturePreviewRequestCount() const noexcept { return m_texturePreviewRequests; }
        [[nodiscard]] vanguard::assets::SourceDatabaseResult RescanSources(vanguard::filesystem::ScanResult*) noexcept override
        {
            return vanguard::assets::SourceDatabaseResult::InvalidInventory;
        }
        [[nodiscard]] bool ClassifySources(containers::ArraySpan<const vanguard::assets::SourceImporterDescriptor>) noexcept override { return false; }
        [[nodiscard]] vanguard::assets::SourceDatabaseResult ScanSourceReferences(
            containers::ArraySpan<const vanguard::assets::SourceImporterDescriptor>, vanguard::assets::TargetPlatform) noexcept override
        {
            return vanguard::assets::SourceDatabaseResult::InvalidReferences;
        }

    private:
        [[nodiscard]] bool RequestTexturePreview(resources::ResourceReference,
                                                 editor::ProjectTexturePreviewPriority,
                                                 editor::ProjectTexturePreviewHandle& handle) noexcept override
        {
            ++m_texturePreviewRequests;
            handle = {};
            return false;
        }

        vanguard::projects::ProjectDescriptor& m_project;
        vanguard::asset_streaming::ProjectAssetCatalog m_catalog;
        vanguard::filesystem::AbsolutePath m_path;
        u32 m_texturePreviewRequests = 0u;
    };

    void TestAssetBrowserPersistenceWire() noexcept
    {
        using editor::detail::AssetBrowserContentPersistenceView;
        using editor::detail::AssetBrowserDecodedContentPersistenceState;
        using editor::detail::AssetBrowserSelectionIntent;
        using editor::detail::AssetBrowserTreeExpandedLocation;

        static u8 bytes[editor::MaximumEditorPanelStateBytes]{};
        static u8 recaptured[editor::MaximumEditorPanelStateBytes]{};
        static AssetBrowserDecodedContentPersistenceState decoded;
        static AssetBrowserTreeExpandedLocation expansions[61]{};
        std::memset(bytes, 0, sizeof(bytes));
        std::memset(recaptured, 0, sizeof(recaptured));
        std::memset(&decoded, 0, sizeof(decoded));
        std::memset(expansions, 0, sizeof(expansions));

        editor::detail::AssetBrowserContentViewState presentation;
        presentation.mode = editor::detail::AssetBrowserContentViewMode::CompactGrid;
        presentation.thumbnailExtent = 136.0f;
        presentation.compactThumbnailExtent = 72.0f;
        presentation.navigationSplitRatio = 0.37f;
        u32 byteCount = 0u;
        Check(editor::detail::CaptureAssetBrowserPresentationPersistence(
                  presentation, bytes, sizeof(bytes), byteCount) == editor::EditorPanelStateResult::Success &&
                  byteCount == 17u,
              "asset-browser presentation persistence emits the bounded ABP1 payload");
        editor::detail::AssetBrowserContentViewState restoredPresentation;
        Check(editor::detail::DecodeAssetBrowserPresentationPersistence(
                  {bytes, byteCount}, restoredPresentation) &&
                  restoredPresentation.mode == presentation.mode &&
                  restoredPresentation.thumbnailExtent == presentation.thumbnailExtent &&
                  restoredPresentation.compactThumbnailExtent == presentation.compactThumbnailExtent &&
                  restoredPresentation.navigationSplitRatio == presentation.navigationSplitRatio,
              "asset-browser presentation persistence roundtrips view mode, thumbnail extents and split ratio");

        u8 malformedPresentation[18]{};
        std::memcpy(malformedPresentation, bytes, byteCount);
        malformedPresentation[4] = 0xffu;
        Check(!editor::detail::DecodeAssetBrowserPresentationPersistence(
                  {malformedPresentation, byteCount}, restoredPresentation),
              "asset-browser presentation restore rejects unknown view modes");
        std::memcpy(malformedPresentation, bytes, byteCount);
        malformedPresentation[5] = 0x00u;
        malformedPresentation[6] = 0x00u;
        malformedPresentation[7] = 0xc0u;
        malformedPresentation[8] = 0x7fu;
        Check(!editor::detail::DecodeAssetBrowserPresentationPersistence(
                  {malformedPresentation, byteCount}, restoredPresentation),
              "asset-browser presentation restore rejects NaN thumbnail extents");
        std::memcpy(malformedPresentation, bytes, byteCount);
        const float belowMinimumExtent = editor::detail::AssetBrowserGridMinimumThumbnailExtent - 1.0f;
        std::memcpy(malformedPresentation + 5u, &belowMinimumExtent, sizeof(belowMinimumExtent));
        Check(!editor::detail::DecodeAssetBrowserPresentationPersistence(
                  {malformedPresentation, byteCount}, restoredPresentation),
              "asset-browser presentation restore rejects out-of-range thumbnail extents");
        std::memcpy(malformedPresentation, bytes, byteCount);
        const float invalidSplitRatio = editor::detail::AssetBrowserMaximumNavigationSplitRatio + 0.1f;
        std::memcpy(malformedPresentation + 13u, &invalidSplitRatio, sizeof(invalidSplitRatio));
        Check(!editor::detail::DecodeAssetBrowserPresentationPersistence(
                  {malformedPresentation, byteCount}, restoredPresentation),
              "asset-browser presentation restore rejects out-of-range split ratios");
        std::memcpy(malformedPresentation, bytes, byteCount);
        malformedPresentation[byteCount] = 0u;
        Check(!editor::detail::DecodeAssetBrowserPresentationPersistence(
                  {malformedPresentation, byteCount + 1u}, restoredPresentation),
              "asset-browser presentation restore rejects trailing bytes");
        editor::detail::AssetBrowserContentViewState invalidPresentation = presentation;
        invalidPresentation.mode = static_cast<editor::detail::AssetBrowserContentViewMode>(0xffu);
        u32 invalidPresentationBytes = 0u;
        Check(editor::detail::CaptureAssetBrowserPresentationPersistence(
                  invalidPresentation, bytes, sizeof(bytes), invalidPresentationBytes) ==
                  editor::EditorPanelStateResult::InvalidData && invalidPresentationBytes == 0u,
              "asset-browser presentation capture rejects impossible live presentation state");
        invalidPresentation = presentation;
        invalidPresentation.navigationSplitRatio = 0.0f;
        invalidPresentationBytes = 0u;
        Check(editor::detail::CaptureAssetBrowserPresentationPersistence(
                  invalidPresentation, bytes, sizeof(bytes), invalidPresentationBytes) ==
                  editor::EditorPanelStateResult::InvalidData && invalidPresentationBytes == 0u,
              "asset-browser presentation capture rejects impossible live split ratios");

        u8 legacyPresentation[13]{};
        editor::detail::AssetBrowserStateWriter legacyPresentationWriter{
            legacyPresentation, static_cast<u32>(sizeof(legacyPresentation))};
        Check(legacyPresentationWriter.WriteU32(editor::detail::AssetBrowserPresentationStateMagicV1) &&
                  legacyPresentationWriter.WriteU8(static_cast<u8>(presentation.mode)) &&
                  legacyPresentationWriter.WriteF32(presentation.thumbnailExtent) &&
                  legacyPresentationWriter.WriteF32(presentation.compactThumbnailExtent) &&
                  legacyPresentationWriter.position == sizeof(legacyPresentation),
              "asset-browser v1 presentation fixture matches the original 13-byte wire");
        u32 migratedPresentationBytes = 0u;
        Check(editor::detail::MigrateAssetBrowserPersistenceV1(
                  editor::EditorPanelStateScope::Presentation,
                  {legacyPresentation, static_cast<u32>(sizeof(legacyPresentation))},
                  recaptured, sizeof(recaptured), migratedPresentationBytes) ==
                  editor::EditorPanelStateResult::Success &&
                  editor::detail::DecodeAssetBrowserPresentationPersistence(
                      {recaptured, migratedPresentationBytes}, restoredPresentation) &&
                  restoredPresentation.mode == presentation.mode &&
                  restoredPresentation.thumbnailExtent == presentation.thumbnailExtent &&
                  restoredPresentation.compactThumbnailExtent == presentation.compactThumbnailExtent &&
                  restoredPresentation.navigationSplitRatio == editor::detail::AssetBrowserDefaultNavigationSplitRatio,
              "asset-browser v1 presentation migrates to v2 with the default provider-owned split ratio");

        vanguard::i32 candidateFilter = -1;
        for (u32 index = 0u; index < editor::detail::GetAssetBrowserImportFilterCount(); ++index)
            if (editor::detail::AssetBrowserImportFilters[index].state == assets::SourceImportState::Candidate)
                candidateFilter = static_cast<vanguard::i32>(index);
        const auto candidateTag = editor::detail::EncodeAssetBrowserImportFilter(candidateFilter);
        vanguard::i32 decodedFilter = -2;
        Check(candidateFilter >= 0 &&
                  candidateTag == editor::detail::AssetBrowserPersistedImportFilter::Candidate &&
                  editor::detail::DecodeAssetBrowserImportFilter(static_cast<u8>(candidateTag), decodedFilter) &&
                  decodedFilter == candidateFilter,
              "asset-browser persistence maps import filters by semantic SourceImportState rather than UI index");

        expansions[0].root = 0u;
        expansions[0].pathBytes = 5u;
        std::memcpy(expansions[0].path, "Alpha", 6u);
        expansions[1].root = 1u;
        expansions[1].pathBytes = 4u;
        std::memcpy(expansions[1].path, "Beta", 5u);

        assets::AssetId assetId;
        for (u32 index = 0u; index < sizeof(assetId.bytes); ++index)
            assetId.bytes[index] = static_cast<u8>(index + 1u);
        AssetBrowserContentPersistenceView content;
        content.root = 3u;
        content.folderPath = containers::StringView("Folder/Sub");
        content.search = containers::StringView("needle");
        content.importFilter = candidateFilter;
        content.recursive = true;
        content.selection = AssetBrowserSelectionIntent::Asset;
        content.selectedAsset = assetId;
        content.treeScrollFraction = 0.25f;
        content.contentScrollFractions[editor::detail::AssetBrowserContentViewModeIndex(
            editor::detail::AssetBrowserContentViewMode::Grid)] = 0.10f;
        content.contentScrollFractions[editor::detail::AssetBrowserContentViewModeIndex(
            editor::detail::AssetBrowserContentViewMode::CompactGrid)] = 0.55f;
        content.contentScrollFractions[editor::detail::AssetBrowserContentViewModeIndex(
            editor::detail::AssetBrowserContentViewMode::Details)] = 0.90f;
        content.expansions = containers::ArraySpan<const AssetBrowserTreeExpandedLocation>(expansions, 2u);

        static u8 legacyContent[editor::MaximumEditorPanelStateBytes]{};
        editor::detail::AssetBrowserStateWriter legacyContentWriter{legacyContent, sizeof(legacyContent)};
        Check(legacyContentWriter.WriteU32(editor::detail::AssetBrowserContentStateMagicV1) &&
                  legacyContentWriter.WriteU32(content.root) &&
                  legacyContentWriter.WriteString(content.folderPath) &&
                  legacyContentWriter.WriteString(content.search) &&
                  legacyContentWriter.WriteU8(static_cast<u8>(editor::detail::EncodeAssetBrowserImportFilter(
                      content.importFilter))) &&
                  legacyContentWriter.WriteU8(content.recursive ? 1u : 0u) &&
                  legacyContentWriter.WriteU8(static_cast<u8>(content.selection)) &&
                  legacyContentWriter.WriteBytes(content.selectedAsset.bytes, sizeof(content.selectedAsset.bytes)) &&
                  legacyContentWriter.WriteU8(0u) && legacyContentWriter.WriteU16(2u) &&
                  legacyContentWriter.WriteU32(expansions[0].root) &&
                  legacyContentWriter.WriteString(containers::StringView(expansions[0].path, expansions[0].pathBytes)) &&
                  legacyContentWriter.WriteU32(expansions[1].root) &&
                  legacyContentWriter.WriteString(containers::StringView(expansions[1].path, expansions[1].pathBytes)),
              "asset-browser v1 content fixture matches the original no-scroll wire");
        u32 migratedContentBytes = 0u;
        Check(editor::detail::MigrateAssetBrowserPersistenceV1(
                  editor::EditorPanelStateScope::Content,
                  {legacyContent, legacyContentWriter.position}, recaptured, sizeof(recaptured), migratedContentBytes) ==
                  editor::EditorPanelStateResult::Success &&
                  editor::detail::DecodeAssetBrowserContentPersistence(
                      {recaptured, migratedContentBytes}, decoded) &&
                  decoded.root == content.root &&
                  containers::StringView(decoded.folderPath, decoded.folderPathBytes) == content.folderPath &&
                  containers::StringView(decoded.search, decoded.searchBytes) == content.search &&
                  decoded.importFilter == content.importFilter && decoded.recursive == content.recursive &&
                  decoded.selection == content.selection &&
                  std::memcmp(decoded.selectedAsset.bytes, content.selectedAsset.bytes,
                              sizeof(content.selectedAsset.bytes)) == 0 &&
                  decoded.treeScrollFraction == 0.0f && decoded.contentScrollFractions[0] == 0.0f &&
                  decoded.contentScrollFractions[1] == 0.0f && decoded.contentScrollFractions[2] == 0.0f &&
                  decoded.expansionCount == 2u,
              "asset-browser v1 content migrates to v2 while preserving durable intent and defaulting new scroll intent");

        byteCount = 0u;
        Check(editor::detail::CaptureAssetBrowserContentPersistence(
                  content, bytes, sizeof(bytes), byteCount) == editor::EditorPanelStateResult::Success &&
                  editor::detail::DecodeAssetBrowserContentPersistence({bytes, byteCount}, decoded) &&
                  decoded.root == content.root &&
                  containers::StringView(decoded.folderPath, decoded.folderPathBytes) == content.folderPath &&
                  containers::StringView(decoded.search, decoded.searchBytes) == content.search &&
                  decoded.importFilter == candidateFilter && decoded.recursive &&
                  decoded.selection == AssetBrowserSelectionIntent::Asset &&
                  std::memcmp(decoded.selectedAsset.bytes, assetId.bytes, sizeof(assetId.bytes)) == 0 &&
                  decoded.treeScrollFraction == content.treeScrollFraction &&
                  decoded.contentScrollFractions[0] == content.contentScrollFractions[0] &&
                  decoded.contentScrollFractions[1] == content.contentScrollFractions[1] &&
                  decoded.contentScrollFractions[2] == content.contentScrollFractions[2] &&
                  decoded.expansionCount == 2u && !decoded.treePersistenceTruncated &&
                  decoded.expansions[0].root == 0u && decoded.expansions[1].root == 1u,
              "asset-browser content persistence roundtrips durable navigation/filter/selection/tree/scroll intent only");

        const u32 folderBytes = content.folderPath.Length();
        const u32 searchBytes = content.search.Length();
        const u32 filterOffset = 4u + 4u + 2u + folderBytes + 2u + searchBytes;
        const u32 recursiveOffset = filterOffset + 1u;
        const u32 selectionOffset = recursiveOffset + 1u;
        const u32 selectedAssetOffset = selectionOffset + 1u;
        const u32 scrollOffset = selectedAssetOffset + static_cast<u32>(sizeof(assetId.bytes));
        const u32 expansionHeaderOffset = scrollOffset + sizeof(float) * 4u;

        static u8 malformedContent[editor::MaximumEditorPanelStateBytes]{};
        std::memcpy(malformedContent, bytes, byteCount);
        malformedContent[0] ^= 0xffu;
        Check(!editor::detail::DecodeAssetBrowserContentPersistence({malformedContent, byteCount}, decoded),
              "asset-browser content restore rejects invalid wire magic");
        std::memcpy(malformedContent, bytes, byteCount);
        malformedContent[10u] = '\0';
        Check(!editor::detail::DecodeAssetBrowserContentPersistence({malformedContent, byteCount}, decoded),
              "asset-browser content restore rejects embedded NUL in persisted strings");
        std::memcpy(malformedContent, bytes, byteCount);
        malformedContent[filterOffset] = 0xffu;
        Check(!editor::detail::DecodeAssetBrowserContentPersistence({malformedContent, byteCount}, decoded),
              "asset-browser content restore rejects unknown semantic filter tags");
        std::memcpy(malformedContent, bytes, byteCount);
        malformedContent[recursiveOffset] = 2u;
        Check(!editor::detail::DecodeAssetBrowserContentPersistence({malformedContent, byteCount}, decoded),
              "asset-browser content restore rejects non-boolean recursive state");
        std::memcpy(malformedContent, bytes, byteCount);
        std::memset(malformedContent + selectedAssetOffset, 0, sizeof(assetId.bytes));
        Check(!editor::detail::DecodeAssetBrowserContentPersistence({malformedContent, byteCount}, decoded),
              "asset-browser content restore rejects invalid persisted AssetId selection");
        std::memcpy(malformedContent, bytes, byteCount);
        malformedContent[scrollOffset + 0u] = 0x00u;
        malformedContent[scrollOffset + 1u] = 0x00u;
        malformedContent[scrollOffset + 2u] = 0xc0u;
        malformedContent[scrollOffset + 3u] = 0x7fu;
        Check(!editor::detail::DecodeAssetBrowserContentPersistence({malformedContent, byteCount}, decoded),
              "asset-browser content restore rejects NaN scroll intent");
        std::memcpy(malformedContent, bytes, byteCount);
        malformedContent[expansionHeaderOffset] = 2u;
        Check(!editor::detail::DecodeAssetBrowserContentPersistence({malformedContent, byteCount}, decoded),
              "asset-browser content restore rejects unknown expansion flags");
        std::memcpy(malformedContent, bytes, byteCount);
        malformedContent[byteCount] = 0u;
        Check(!editor::detail::DecodeAssetBrowserContentPersistence({malformedContent, byteCount + 1u}, decoded),
              "asset-browser content restore rejects trailing bytes");
        const char embeddedNullPath[]{'A', '\0', 'B'};
        AssetBrowserContentPersistenceView invalidContent = content;
        invalidContent.folderPath = containers::StringView(embeddedNullPath, 3u);
        u32 invalidContentBytes = 0u;
        Check(editor::detail::CaptureAssetBrowserContentPersistence(
                  invalidContent, bytes, sizeof(bytes), invalidContentBytes) ==
                  editor::EditorPanelStateResult::InvalidData && invalidContentBytes == 0u,
              "asset-browser content capture rejects embedded NUL instead of reporting capacity exhaustion");
        invalidContent = content;
        invalidContent.contentScrollFractions[0] = 1.1f;
        invalidContentBytes = 0u;
        Check(editor::detail::CaptureAssetBrowserContentPersistence(
                  invalidContent, bytes, sizeof(bytes), invalidContentBytes) ==
                  editor::EditorPanelStateResult::InvalidData && invalidContentBytes == 0u,
              "asset-browser content capture rejects out-of-range scroll intent");

        content.selection = AssetBrowserSelectionIntent::Folder;
        content.selectedContentFolderRoot = 7u;
        content.selectedContentFolderPath = containers::StringView("Folder/Selected");
        byteCount = 0u;
        Check(editor::detail::CaptureAssetBrowserContentPersistence(
                  content, bytes, sizeof(bytes), byteCount) == editor::EditorPanelStateResult::Success &&
                  editor::detail::DecodeAssetBrowserContentPersistence({bytes, byteCount}, decoded) &&
                  decoded.selection == AssetBrowserSelectionIntent::Folder &&
                  decoded.selectedContentFolderRoot == content.selectedContentFolderRoot &&
                  containers::StringView(decoded.selectedContentFolderPath, decoded.selectedContentFolderPathBytes) ==
                      content.selectedContentFolderPath,
              "asset-browser content persistence roundtrips durable folder root/path selection intent");

        for (u32 index = 0u; index < 61u; ++index)
        {
            expansions[index] = {};
            expansions[index].root = index;
            expansions[index].pathBytes = resources::MaximumResourcePathBytes;
            std::memset(expansions[index].path, 'x', expansions[index].pathBytes);
            expansions[index].path[expansions[index].pathBytes] = '\0';
        }
        static char maximumFolderPath[resources::MaximumResourcePathBytes + 1u]{};
        static char maximumSearch[assetStreaming::MaximumProjectAssetCatalogSearchBytes]{};
        static char maximumSelectedFolderPath[resources::MaximumResourcePathBytes + 1u]{};
        std::memset(maximumFolderPath, 'f', resources::MaximumResourcePathBytes);
        maximumFolderPath[resources::MaximumResourcePathBytes] = '\0';
        std::memset(maximumSearch, 's', assetStreaming::MaximumProjectAssetCatalogSearchBytes - 1u);
        maximumSearch[assetStreaming::MaximumProjectAssetCatalogSearchBytes - 1u] = '\0';
        std::memset(maximumSelectedFolderPath, 'p', resources::MaximumResourcePathBytes);
        maximumSelectedFolderPath[resources::MaximumResourcePathBytes] = '\0';
        content = {};
        content.folderPath = containers::StringView(maximumFolderPath, resources::MaximumResourcePathBytes);
        content.search = containers::StringView(maximumSearch, assetStreaming::MaximumProjectAssetCatalogSearchBytes - 1u);
        content.selection = AssetBrowserSelectionIntent::Folder;
        content.selectedContentFolderRoot = 9u;
        content.selectedContentFolderPath =
            containers::StringView(maximumSelectedFolderPath, resources::MaximumResourcePathBytes);
        content.treeScrollFraction = 1.0f;
        content.contentScrollFractions[0] = 1.0f;
        content.contentScrollFractions[1] = 0.5f;
        content.contentScrollFractions[2] = 0.25f;
        content.expansions = containers::ArraySpan<const AssetBrowserTreeExpandedLocation>(expansions, 61u);
        byteCount = 0u;
        Check(editor::detail::CaptureAssetBrowserContentPersistence(
                  content, bytes, sizeof(bytes), byteCount) == editor::EditorPanelStateResult::Success &&
                  byteCount < editor::MaximumEditorPanelStateBytes &&
                  editor::detail::DecodeAssetBrowserContentPersistence({bytes, byteCount}, decoded) &&
                  decoded.expansionCount == editor::detail::MaximumPersistedAssetBrowserTreeExpansions &&
                  decoded.treePersistenceTruncated,
              "asset-browser content persistence deterministically caps worst-case tree expansion state below 64 KiB");
        u32 insufficientBytes = 0u;
        Check(editor::detail::CaptureAssetBrowserContentPersistence(
                  content, recaptured, byteCount - 1u, insufficientBytes) ==
                  editor::EditorPanelStateResult::CapacityExceeded,
              "asset-browser content persistence reports CapacityExceeded for a one-byte-short destination");

        AssetBrowserContentPersistenceView recaptureView;
        recaptureView.folderPath = containers::StringView(decoded.folderPath, decoded.folderPathBytes);
        recaptureView.search = containers::StringView(decoded.search, decoded.searchBytes);
        recaptureView.selection = decoded.selection;
        recaptureView.selectedContentFolderRoot = decoded.selectedContentFolderRoot;
        recaptureView.selectedContentFolderPath = containers::StringView(
            decoded.selectedContentFolderPath, decoded.selectedContentFolderPathBytes);
        recaptureView.treeScrollFraction = decoded.treeScrollFraction;
        for (u32 index = 0u; index < editor::detail::AssetBrowserContentViewModeCount; ++index)
            recaptureView.contentScrollFractions[index] = decoded.contentScrollFractions[index];
        recaptureView.treePersistenceTruncated = decoded.treePersistenceTruncated;
        recaptureView.expansions = containers::ArraySpan<const AssetBrowserTreeExpandedLocation>(
            decoded.expansions, decoded.expansionCount);
        u32 recapturedBytes = 0u;
        Check(editor::detail::CaptureAssetBrowserContentPersistence(
                  recaptureView, recaptured, sizeof(recaptured), recapturedBytes) ==
                  editor::EditorPanelStateResult::Success &&
                  recapturedBytes == byteCount && std::memcmp(recaptured, bytes, byteCount) == 0,
              "asset-browser tree truncation provenance survives restore and exact recapture");

        expansions[0] = {};
        expansions[0].root = 5u;
        expansions[0].pathBytes = 4u;
        std::memcpy(expansions[0].path, "Same", 5u);
        expansions[1] = expansions[0];
        content = {};
        content.expansions = containers::ArraySpan<const AssetBrowserTreeExpandedLocation>(expansions, 2u);
        byteCount = 0u;
        Check(editor::detail::CaptureAssetBrowserContentPersistence(
                  content, bytes, sizeof(bytes), byteCount) == editor::EditorPanelStateResult::InvalidData,
              "asset-browser content capture rejects duplicate or unsorted expansion identities");
    }

    void TestProductionAssetBrowserBrowsePolicy() noexcept
    {
        const bool diagnosticsStarted =
            diagnostics::Initialize(diagnostics::Mode::Synchronous, "editorShellTestsAssetBrowser");
        Check(diagnosticsStarted, "asset-browser oracle initializes diagnostics");
        if (!diagnosticsStarted)
            return;

        const bool ioStarted = vanguard::io::Initialize();
        Check(ioStarted, "asset-browser oracle initializes IO");
        if (!ioStarted)
        {
            diagnostics::Shutdown();
            return;
        }

        const filesystem::AbsolutePath working = vanguard::tests::ScratchDirectory("editor_shell_tests");
        const filesystem::AbsolutePath root = working.AddDirPath("asset_browser_policy");
        const bool filesystemStarted = filesystem::Initialize({working, working, working});
        Check(filesystemStarted, "asset-browser oracle initializes filesystem");
        if (!filesystemStarted)
        {
            vanguard::io::Shutdown();
            diagnostics::Shutdown();
            return;
        }

        filesystem::Manager& files = filesystem::GetManager();
        RemoveAssetBrowserOracleFixture(files, root);
        const filesystem::AbsolutePath assetsRoot = root.AddDirPath("Assets");
        const filesystem::AbsolutePath emptyFolder = assetsRoot.AddDirPath("Empty");
        const filesystem::AbsolutePath emptyNestedFolder = emptyFolder.AddDirPath("Nested");
        const filesystem::AbsolutePath occupiedFolder = assetsRoot.AddDirPath("Occupied");
        const bool fixtureCreated = files.CreatePath(root) && files.CreatePath(assetsRoot) &&
                                    files.CreatePath(emptyFolder) && files.CreatePath(emptyNestedFolder) &&
                                    files.CreatePath(occupiedFolder);
        Check(fixtureCreated, "asset-browser oracle creates focused source fixture");
        if (!fixtureCreated)
        {
            filesystem::Shutdown();
            vanguard::io::Shutdown();
            diagnostics::Shutdown();
            return;
        }

        const bool jobsStarted = jobs::Initialize(jobs::ToolConfig());
        Check(jobsStarted, "asset-browser oracle initializes focused catalog jobs");
        if (!jobsStarted)
        {
            RemoveAssetBrowserOracleFixture(files, root);
            filesystem::Shutdown();
            vanguard::io::Shutdown();
            diagnostics::Shutdown();
            return;
        }

        {
            assets::AssetId adoptedId;
            for (u32 index = 0u; index < 16u; ++index)
                adoptedId.bytes[index] = static_cast<u8>(index + 1u);

            const filesystem::AbsolutePath adopted = assetsRoot.AddFilePath("adopted.browser");
            const filesystem::AbsolutePath moved = assetsRoot.AddFilePath("moved-adopted.browser");
            const filesystem::AbsolutePath candidate = occupiedFolder.AddFilePath("candidate.browser");
            const assets::AssetMetadata adoptedMetadata = AssetBrowserOracleMetadata(adoptedId);
            Check(WriteAssetBrowserOracleBytes(adopted, containers::StringView("adopted")) &&
                      WriteAssetBrowserOracleMetadata(adopted, adoptedMetadata) &&
                      WriteAssetBrowserOracleBytes(candidate, containers::StringView("candidate")),
                  "asset-browser oracle authors one adopted source and one Candidate source");

            const assets::SourceRoot rootDesc{assetsRoot, false};
            assets::SourceDatabase sources;
            assets::SourceImporterDescriptor importer;
            importer.id = AssetBrowserOracleImporterId;
            importer.name = "asset-browser-oracle";
            importer.version = AssetBrowserOracleImporterVersion;
            importer.sourceType = AssetBrowserOracleSourceType;
            importer.primaryOutputType = AssetBrowserOracleOutputType;
            importer.recognizeSource = RecognizeAssetBrowserOracleSource;
            Check(sources.Rescan(files, {&rootDesc, 1u}) == assets::SourceDatabaseResult::Success &&
                      sources.ClassifySources({&importer, 1u}),
                  "asset-browser oracle builds classified source authority");

            vanguard::projects::ProjectDescriptor project;
            FixtureWorkspaceService workspace(project);
            assetStreaming::ProjectAssetCatalog& catalog = workspace.GetMutableAssetCatalog();
            Check(DrainAssetBrowserOracleCatalog(catalog, sources) == assetStreaming::ProjectAssetCatalogResult::Success,
                  "asset-browser oracle publishes initial production catalog generation");
            const u64 firstFolderGeneration = catalog.GetStatus().sourceGeneration;
            Check(firstFolderGeneration != 0u && firstFolderGeneration == sources.GetGeneration(),
                  "asset-browser oracle catalog generation matches source authority");
            const assetStreaming::ProjectAssetCatalogFolderId rootFolder = catalog.GetRootFolder(0u);
            const assetStreaming::ProjectAssetCatalogFolderId emptyCatalogFolder =
                catalog.ResolveFolder(0u, containers::StringView("Empty"));
            const assetStreaming::ProjectAssetCatalogFolderId emptyNestedCatalogFolder =
                catalog.ResolveFolder(0u, containers::StringView("Empty/Nested"));
            const assetStreaming::ProjectAssetCatalogFolderId occupiedCatalogFolder =
                catalog.ResolveFolder(0u, containers::StringView("Occupied"));
            assetStreaming::ProjectAssetCatalogFolderInfo rootFolderInfo;
            assetStreaming::ProjectAssetCatalogFolderInfo emptyFolderInfo;
            assetStreaming::ProjectAssetCatalogFolderInfo emptyNestedFolderInfo;
            assetStreaming::ProjectAssetCatalogFolderInfo occupiedFolderInfo;
            Check(rootFolder.IsValid() && emptyCatalogFolder.IsValid() && emptyNestedCatalogFolder.IsValid() &&
                      occupiedCatalogFolder.IsValid() &&
                      catalog.GetFolderInfo(rootFolder, rootFolderInfo) &&
                      catalog.GetFolderInfo(emptyCatalogFolder, emptyFolderInfo) &&
                      catalog.GetFolderInfo(emptyNestedCatalogFolder, emptyNestedFolderInfo) &&
                      catalog.GetFolderInfo(occupiedCatalogFolder, occupiedFolderInfo) &&
                      rootFolderInfo.childFolderCount == 2u &&
                      emptyFolderInfo.childFolderCount == 1u && emptyFolderInfo.directRowCount == 0u &&
                      emptyFolderInfo.subtreeRowCount == 0u &&
                      emptyNestedFolderInfo.childFolderCount == 0u &&
                      emptyNestedFolderInfo.directRowCount == 0u && emptyNestedFolderInfo.subtreeRowCount == 0u &&
                      occupiedFolderInfo.childFolderCount == 0u && occupiedFolderInfo.directRowCount == 1u &&
                      occupiedFolderInfo.subtreeRowCount == 1u,
                  "asset-browser oracle publishes empty folders and merges physical folders with row-derived hierarchy");
            const editor::detail::AssetBrowserContentViewState defaultContentView;
            const editor::detail::AssetBrowserContentRange folderOnlyRange =
                editor::detail::SplitAssetBrowserContentRange(0u, 2u, 3u);
            const editor::detail::AssetBrowserContentRange crossingRange =
                editor::detail::SplitAssetBrowserContentRange(2u, 5u, 3u);
            const editor::detail::AssetBrowserContentRange assetOnlyRange =
                editor::detail::SplitAssetBrowserContentRange(4u, 7u, 3u);
            Check(defaultContentView.mode == editor::detail::AssetBrowserContentViewMode::Grid &&
                      folderOnlyRange.firstFolder == 0u && folderOnlyRange.folderCount == 2u &&
                      folderOnlyRange.assetCount == 0u &&
                      crossingRange.firstFolder == 2u && crossingRange.folderCount == 1u &&
                      crossingRange.firstAsset == 0u && crossingRange.assetCount == 2u &&
                      assetOnlyRange.folderCount == 0u && assetOnlyRange.firstAsset == 1u &&
                      assetOnlyRange.assetCount == 3u,
                  "asset-browser content view defaults to Grid and splits clipped folder/asset ranges without materializing contents");
            char copiedRootFolder[resources::MaximumResourcePathBytes + 1u]{};
            char copiedNestedFolder[resources::MaximumResourcePathBytes + 1u]{};
            u32 copiedRootFolderBytes = ~u32{0};
            u32 copiedNestedFolderBytes = 0u;
            Check(catalog.CopyFolderRelativePath(rootFolder, copiedRootFolder, copiedRootFolderBytes) &&
                      copiedRootFolderBytes == 0u && copiedRootFolder[0] == '\0' &&
                      catalog.CopyFolderRelativePath(emptyNestedCatalogFolder, copiedNestedFolder,
                                                     copiedNestedFolderBytes) &&
                      containers::StringView(copiedNestedFolder, copiedNestedFolderBytes) ==
                      containers::StringView("Empty/Nested"),
                  "catalog converts generation-local folder handles back to durable root-relative paths");

            editor::detail::AssetBrowserTreeState tree(memory::pools::Tools::GetInstance());
            editor::detail::RevealAssetBrowserTreeLocation(tree, 0u, {});
            Check(editor::detail::RebuildAssetBrowserTreeVisible(tree, catalog) &&
                      tree.visible.Size() == 3u &&
                      tree.visible[0u].folder == rootFolder &&
                      tree.visible[1u].folder == emptyCatalogFolder &&
                      tree.visible[2u].folder == occupiedCatalogFolder,
                  "persistent folder tree starts with the root expanded and keeps sibling folders visible");
            Check(editor::detail::SetAssetBrowserTreeFolderExpanded(
                      tree, 0u, containers::StringView("Empty"), true) &&
                      editor::detail::RebuildAssetBrowserTreeVisible(tree, catalog) &&
                      tree.visible.Size() == 4u &&
                      tree.visible[0u].folder == rootFolder &&
                      tree.visible[1u].folder == emptyCatalogFolder &&
                      tree.visible[2u].folder == emptyNestedCatalogFolder &&
                      tree.visible[3u].folder == occupiedCatalogFolder,
                  "expanding a folder reveals its descendants without replacing parent or sibling tree context");
            Check(editor::detail::SetAssetBrowserTreeFolderExpanded(
                      tree, 0u, containers::StringView("Empty"), false) &&
                      editor::detail::RebuildAssetBrowserTreeVisible(tree, catalog) &&
                      tree.visible.Size() == 3u &&
                      tree.visible[1u].folder == emptyCatalogFolder &&
                      tree.visible[2u].folder == occupiedCatalogFolder,
                  "collapsing a tree branch hides only its descendants and preserves sibling navigation");
            editor::detail::RevealAssetBrowserTreeLocation(
                tree, 0u, containers::StringView("Empty/Nested"));
            Check(editor::detail::IsAssetBrowserTreeFolderExpanded(
                      tree, 0u, containers::StringView("Empty")) &&
                      editor::detail::RebuildAssetBrowserTreeVisible(tree, catalog) &&
                      tree.visible.Size() == 4u,
                  "revealing a selected nested folder expands ancestors but keeps expansion state independent from navigation");
            editor::detail::AssetBrowserTreeState secondaryTree(memory::pools::Tools::GetInstance());
            editor::detail::RevealAssetBrowserTreeLocation(secondaryTree, 0u, {});
            Check(editor::detail::RebuildAssetBrowserTreeVisible(secondaryTree, catalog) &&
                      secondaryTree.visible.Size() == 3u &&
                      !editor::detail::IsAssetBrowserTreeFolderExpanded(
                          secondaryTree, 0u, containers::StringView("Empty")) &&
                      editor::detail::IsAssetBrowserTreeFolderExpanded(
                          tree, 0u, containers::StringView("Empty")),
                  "multiple browser instances keep independent folder-tree expansion state");

            const filesystem::AbsolutePath addedLaterFolder = emptyFolder.AddDirPath("AddedLater");
            Check(files.CreatePath(addedLaterFolder) &&
                      sources.Rescan(files, {&rootDesc, 1u}) == assets::SourceDatabaseResult::Success &&
                      sources.ClassifySources({&importer, 1u}) &&
                      DrainAssetBrowserOracleCatalog(catalog, sources) == assetStreaming::ProjectAssetCatalogResult::Success,
                  "asset-browser oracle publishes a directory-only addition through source generation");
            const u64 addedFolderGeneration = catalog.GetStatus().sourceGeneration;
            Check(addedFolderGeneration != firstFolderGeneration &&
                      addedFolderGeneration == sources.GetGeneration() &&
                      catalog.ResolveFolder(0u, containers::StringView("Empty/AddedLater")).IsValid(),
                  "directory-only addition advances the authoritative catalog and becomes browsable");
            editor::detail::PruneAssetBrowserTreeExpansions(tree, catalog);
            Check(editor::detail::RebuildAssetBrowserTreeVisible(tree, catalog) &&
                      tree.visibleGeneration == addedFolderGeneration && tree.visible.Size() == 5u &&
                      editor::detail::IsAssetBrowserTreeFolderExpanded(
                          tree, 0u, containers::StringView("Empty")),
                  "tree expansion survives catalog generation changes and new siblings join the expanded hierarchy");
            editor::detail::AssetBrowserBrowsePolicyState removedFolderNavigation;
            const editor::detail::AssetBrowserCatalogSyncPolicyResult removedFolderInitial =
                editor::detail::SyncAssetBrowserBrowsePolicy(removedFolderNavigation, catalog);
            Check(removedFolderInitial.ready &&
                      editor::detail::NavigateAssetBrowserToFolder(
                          removedFolderNavigation, catalog, 0u, containers::StringView("Empty/AddedLater")) ==
                          editor::detail::AssetBrowserNavigationResult::Changed,
                  "browser oracle navigates into a folder before an external directory-only removal");
            Check(files.DeletePath(addedLaterFolder) &&
                      sources.Rescan(files, {&rootDesc, 1u}) == assets::SourceDatabaseResult::Success &&
                      sources.ClassifySources({&importer, 1u}) &&
                      DrainAssetBrowserOracleCatalog(catalog, sources) == assetStreaming::ProjectAssetCatalogResult::Success,
                  "asset-browser oracle publishes a directory-only removal through source generation");
            const u64 initialGeneration = catalog.GetStatus().sourceGeneration;
            Check(initialGeneration != addedFolderGeneration && initialGeneration == sources.GetGeneration() &&
                      !catalog.ResolveFolder(0u, containers::StringView("Empty/AddedLater")).IsValid(),
                  "directory-only removal advances the authoritative catalog and removes the folder");
            editor::detail::PruneAssetBrowserTreeExpansions(tree, catalog);
            Check(editor::detail::RebuildAssetBrowserTreeVisible(tree, catalog) &&
                      tree.visibleGeneration == initialGeneration && tree.visible.Size() == 4u &&
                      tree.visible[2u].folder == catalog.ResolveFolder(0u, containers::StringView("Empty/Nested")) &&
                      tree.visible[3u].folder == catalog.ResolveFolder(0u, containers::StringView("Occupied")),
                  "tree rebinds generation-local folder IDs after directory removal without losing durable expansion state");
            const editor::detail::AssetBrowserCatalogSyncPolicyResult removedFolderSync =
                editor::detail::SyncAssetBrowserBrowsePolicy(removedFolderNavigation, catalog);
            Check(removedFolderSync.ready &&
                      containers::StringView(removedFolderNavigation.folderPath) == containers::StringView("Empty") &&
                      removedFolderNavigation.folder ==
                          catalog.ResolveFolder(0u, containers::StringView("Empty")),
                  "browser keeps the nearest surviving ancestor when the current folder disappears");

            editor::detail::AssetBrowserBrowsePolicyState primary;
            editor::detail::AssetBrowserBrowsePolicyState secondary;
            editor::detail::AssetBrowserBrowsePolicyState navigation;
            const editor::detail::AssetBrowserCatalogSyncPolicyResult primaryInitial =
                editor::detail::SyncAssetBrowserBrowsePolicy(primary, catalog);
            const editor::detail::AssetBrowserCatalogSyncPolicyResult secondaryInitial =
                editor::detail::SyncAssetBrowserBrowsePolicy(secondary, catalog);
            const editor::detail::AssetBrowserCatalogSyncPolicyResult navigationInitial =
                editor::detail::SyncAssetBrowserBrowsePolicy(navigation, catalog);
            Check(primaryInitial.ready && secondaryInitial.ready && navigationInitial.ready &&
                      primary.folder.IsValid() && secondary.folder.IsValid() && navigation.folder.IsValid() &&
                      primary.catalogGeneration == initialGeneration && secondary.catalogGeneration == initialGeneration &&
                      primary.queryDirty && secondary.queryDirty,
                  "two panel-local browser policy states independently synchronize the initial catalog");

            navigation.queryDirty = false;
            Check(editor::detail::NavigateAssetBrowserToFolder(
                      navigation, catalog, 0u, containers::StringView("Empty/Nested")) ==
                          editor::detail::AssetBrowserNavigationResult::Changed &&
                      navigation.folder == catalog.ResolveFolder(0u, containers::StringView("Empty/Nested")) &&
                      containers::StringView(navigation.folderPath) == containers::StringView("Empty/Nested") &&
                      navigation.queryDirty,
                  "browser navigation commits durable root/path and resolves the generation-local folder handle once");
            navigation.queryDirty = false;
            Check(editor::detail::NavigateAssetBrowserToParentFolder(navigation, catalog) ==
                          editor::detail::AssetBrowserNavigationResult::Changed &&
                      containers::StringView(navigation.folderPath) == containers::StringView("Empty") &&
                      editor::detail::NavigateAssetBrowserToChildFolder(
                          navigation, catalog, containers::StringView("Nested")) ==
                          editor::detail::AssetBrowserNavigationResult::Changed &&
                      editor::detail::NavigateAssetBrowserToFolder(
                          navigation, catalog, 0u, containers::StringView("Empty/Nested")) ==
                          editor::detail::AssetBrowserNavigationResult::Unchanged,
                  "browser parent/child/same-location transitions share one canonical navigation policy");

            std::memcpy(primary.search, "adopted", 8u);
            primary.importFilter = 0;
            std::memcpy(secondary.search, "candidate", 10u);
            secondary.importFilter = 1;
            secondary.recursive = true;
            Check(editor::detail::BeginAssetBrowserBrowseQuery(primary, catalog) ==
                          assetStreaming::ProjectAssetCatalogResult::Success &&
                      editor::detail::BeginAssetBrowserBrowseQuery(secondary, catalog) ==
                          assetStreaming::ProjectAssetCatalogResult::Success &&
                      !primary.queryDirty && !secondary.queryDirty,
                  "two panel-local states begin independent production catalog queries");
            Check(catalog.StepQuery(primary.query, 64u, 64u * 1024u) ==
                          assetStreaming::ProjectAssetCatalogResult::Success &&
                      primary.query.GetStatus().state == assetStreaming::ProjectAssetCatalogQueryState::Complete &&
                      primary.query.GetStatus().matches == 1u &&
                      secondary.query.GetStatus().state == assetStreaming::ProjectAssetCatalogQueryState::Running,
                  "primary query completes without consuming secondary panel query state");

            primary.selectedRow = catalog.ResolveAsset(adoptedId);
            primary.selectedAsset = adoptedId;
            primary.selectedHasAsset = true;
            secondary.selectedRow = catalog.ResolveRow(0u, containers::StringView("Occupied/candidate.browser"));
            secondary.selectedHasAsset = false;
            Check(primary.selectedRow.IsValid() && secondary.selectedRow.IsValid(),
                  "asset-browser oracle captures adopted and Candidate selections in the initial generation");

            Check(WriteAssetBrowserOracleBytes(moved, containers::StringView("adopted-moved")) &&
                      WriteAssetBrowserOracleMetadata(moved, adoptedMetadata),
                  "asset-browser oracle stages adopted source relocation");
            static_cast<void>(files.DeleteFile(adopted));
            static_cast<void>(files.DeleteFile(AssetBrowserOracleMetadataPath(adopted)));
            Check(sources.Rescan(files, {&rootDesc, 1u}) == assets::SourceDatabaseResult::Success &&
                      sources.ClassifySources({&importer, 1u}),
                  "asset-browser oracle advances source authority after adopted relocation");
            Check(DrainAssetBrowserOracleCatalog(catalog, sources) == assetStreaming::ProjectAssetCatalogResult::Success,
                  "asset-browser oracle publishes replacement catalog generation");
            const u64 replacementGeneration = catalog.GetStatus().sourceGeneration;
            Check(replacementGeneration != initialGeneration && replacementGeneration == sources.GetGeneration(),
                  "replacement catalog generation advances exactly with source authority");

            const assetStreaming::ProjectAssetCatalogResult staleStep =
                catalog.StepQuery(secondary.query, 64u, 64u * 1024u);
            const assetStreaming::ProjectAssetCatalogQueryStatus staleStatus = secondary.query.GetStatus();
            Check(staleStep == assetStreaming::ProjectAssetCatalogResult::StaleSource &&
                      staleStatus.state == assetStreaming::ProjectAssetCatalogQueryState::Stale &&
                      editor::detail::FenceStaleAssetBrowserBrowseQuery(secondary, staleStatus) && secondary.queryDirty,
                  "published generation replacement fences the old browser query and schedules restart");

            const editor::detail::AssetBrowserCatalogSyncPolicyResult adoptedSync =
                editor::detail::SyncAssetBrowserBrowsePolicy(primary, catalog);
            const editor::detail::AssetBrowserCatalogSyncPolicyResult candidateSync =
                editor::detail::SyncAssetBrowserBrowsePolicy(secondary, catalog);
            const editor::detail::AssetBrowserCatalogSyncPolicyResult navigationSync =
                editor::detail::SyncAssetBrowserBrowsePolicy(navigation, catalog);
            editor::detail::AssetBrowserBrowsePolicyState restoredAdoptedSelection;
            restoredAdoptedSelection.selectedAsset = adoptedId;
            restoredAdoptedSelection.selectedHasAsset = true;
            const editor::detail::AssetBrowserCatalogSyncPolicyResult restoredSelectionSync =
                editor::detail::SyncAssetBrowserBrowsePolicy(restoredAdoptedSelection, catalog);
            char reboundPath[resources::MaximumResourcePathBytes + 1u]{};
            const u32 reboundBytes = catalog.CopyRelativePath(
                adoptedSync.rebindRow, {reboundPath, static_cast<u32>(sizeof(reboundPath))});
            Check(adoptedSync.ready &&
                      adoptedSync.selection == editor::detail::AssetBrowserSelectionRefreshPolicy::Rebind &&
                      adoptedSync.rebindRow.IsValid() && reboundBytes != 0u &&
                      containers::StringView(reboundPath, reboundBytes) == containers::StringView("moved-adopted.browser"),
                  "durable adopted AssetId rebases to the relocated row across catalog generations");
            Check(restoredSelectionSync.ready &&
                      restoredSelectionSync.selection == editor::detail::AssetBrowserSelectionRefreshPolicy::Rebind &&
                      restoredSelectionSync.rebindRow == adoptedSync.rebindRow,
                  "restored adopted AssetId rebinds from stable identity even without a generation-local selectedRow");
            Check(candidateSync.ready &&
                      candidateSync.selection == editor::detail::AssetBrowserSelectionRefreshPolicy::Clear,
                  "Candidate selection clears rather than retargeting across catalog generations");
            Check(navigationSync.ready &&
                      navigation.folder == catalog.ResolveFolder(0u, containers::StringView("Empty/Nested")) &&
                      containers::StringView(navigation.folderPath) == containers::StringView("Empty/Nested"),
                  "browser navigation rebases durable folder paths across catalog publications without persisting FolderId");

            Check(!editor::detail::IsAssetBrowserMutationSelectionCurrent(false, 77u, true, 77u) &&
                      !editor::detail::IsAssetBrowserMutationSelectionCurrent(true, 77u, true, 78u) &&
                      editor::detail::IsAssetBrowserMutationSelectionCurrent(true, 77u, true, 77u),
                  "adopted mutation policy disables on stale source/accepted generations and re-enables only when both are current");
            Check(editor::detail::CanRunAssetBrowserAdoptedOperation(true, true, false, true) &&
                      !editor::detail::CanRunAssetBrowserAdoptedOperation(true, true, true, true) &&
                      editor::detail::CanRunAssetBrowserAdoptedOperation(true, false, true, true) &&
                      !editor::detail::CanRunAssetBrowserAdoptedOperation(false, false, false, true),
                  "read-only adopted sources allow Reimport/reveal while source-mutating commands stay disabled");

            Check(editor::detail::BeginAssetBrowserBrowseQuery(secondary, catalog) ==
                          assetStreaming::ProjectAssetCatalogResult::Success &&
                      catalog.StepQuery(secondary.query, 64u, 64u * 1024u) ==
                          assetStreaming::ProjectAssetCatalogResult::Success &&
                      secondary.query.GetStatus().state == assetStreaming::ProjectAssetCatalogQueryState::Complete &&
                      secondary.query.GetStatus().matches == 1u,
                  "stale secondary browser query restarts against the replacement catalog generation");
            Check(workspace.GetTexturePreviewRequestCount() == 0u,
                  "catalog sync/search/filter traversal issues zero project texture runtime requests");
        }

        Check(jobs::Shutdown(), "asset-browser oracle shuts down catalog jobs");
        RemoveAssetBrowserOracleFixture(files, root);
        filesystem::Shutdown();
        vanguard::io::Shutdown();
        diagnostics::Shutdown();
    }

    class FixtureUiService final : public editor::EditorUiService
    {
    public:
        FixtureUiService() noexcept
            : m_panels(m_state, editor::detail::GetProductionEditorPanelGenerationSource()),
              m_surfaces(m_state, editor::detail::GetProductionEditorSurfaceGenerationSource()),
              m_surfaceCommands(m_state)
        {
        }

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
            m_frame = frame.frame;
            m_capture = {frame.frame, false, false};
            return true;
        }
        [[nodiscard]] bool DrawOpenPanels(editor::EditorUiFailure*) noexcept override { return true; }
        [[nodiscard]] bool EndFrame(editor::EditorUiFailure*) noexcept override { return true; }
        void AbortFrame() noexcept override { m_capture = {}; }
        [[nodiscard]] editor::EditorUiInputCapture GetInputCapture() const noexcept override { return m_capture; }
        [[nodiscard]] editor::EditorPanelFocusSnapshot GetPanelFocus() const noexcept override { return m_focus; }
        [[nodiscard]] vanguard::f32 GetUserScale() const noexcept override { return 1.0f; }
        [[nodiscard]] bool SetUserScale(vanguard::f32, editor::EditorUiFailure*) noexcept override { return true; }
        [[nodiscard]] editor::EditorUiState GetState() const noexcept override { return m_state; }
        [[nodiscard]] u64 GetFrame() const noexcept override { return m_frame; }
        [[nodiscard]] bool RequestContextMenuForPanel(editor::EditorPanelHandle, u64, editor::EditorUiSurfaceId,
                                                     editor::EditorUiFailure*) noexcept override { return false; }

        [[nodiscard]] bool RegisterSurfaceForOwner(const editor::EditorExtensionOwner& owner,
                                                  const editor::detail::EditorUiSurfaceDesc& descriptor,
                                                  editor::detail::EditorUiSurfaceToken& token,
                                                  editor::EditorUiFailure* const failure) noexcept override
        {
            return m_surfaces.RegisterSurface(owner, descriptor, token, failure);
        }
        [[nodiscard]] bool UnregisterSurfaceForOwner(const editor::EditorExtensionOwner& owner,
                                                    const editor::detail::EditorUiSurfaceToken& token,
                                                    editor::EditorUiFailure* const failure) noexcept override
        {
            const bool removed = m_surfaces.UnregisterSurface(owner, token, failure);
            if (removed)
                RecordSurfaceLifecycleEvent(300u);
            return removed;
        }
        [[nodiscard]] bool RegisterContributionForOwner(const editor::EditorExtensionOwner& owner,
                                                       const editor::detail::EditorUiContributionDesc& descriptor,
                                                       editor::detail::EditorUiContributionToken& token,
                                                       editor::EditorUiFailure* const failure) noexcept override
        {
            return m_surfaces.RegisterContribution(owner, descriptor, token, failure);
        }
        [[nodiscard]] bool UnregisterContributionForOwner(const editor::EditorExtensionOwner& owner,
                                                         const editor::detail::EditorUiContributionToken& token,
                                                         editor::EditorUiFailure* const failure) noexcept override
        {
            const bool removed = m_surfaces.UnregisterContribution(owner, token, failure);
            if (removed)
                RecordSurfaceLifecycleEvent(200u);
            return removed;
        }
        void RevokeSurfaceOwner(const editor::EditorExtensionOwner& owner) noexcept override
        {
            const editor::detail::EditorSurfaceRegistryStatus before = m_surfaces.GetStatus();
            m_surfaces.RevokeOwner(owner);
            const editor::detail::EditorSurfaceRegistryStatus after = m_surfaces.GetStatus();
            g_surfaceRevokeReducedReachableTopology =
                g_surfaceRevokeReducedReachableTopology ||
                after.surfaceCount < before.surfaceCount || after.contributionCount < before.contributionCount;
            RecordSurfaceLifecycleEvent(100u);
        }
        [[nodiscard]] bool RegisterSurfaceCommandBridge(const editor::detail::EditorUiCommandBridge& bridge,
                                                       editor::detail::EditorUiCommandBridgeToken& token,
                                                       editor::EditorUiFailure* const failure) noexcept override
        {
            if (g_rejectSurfaceBridgeRegistration)
            {
                token = {};
                if (failure != nullptr)
                {
                    *failure = {};
                    failure->code = editor::EditorUiFailureCode::InUse;
                    failure->message = "fixture rejects surface command bridge registration";
                }
                return false;
            }
            return m_surfaceCommands.Register(bridge, token, failure);
        }
        [[nodiscard]] bool UnregisterSurfaceCommandBridge(const editor::detail::EditorUiCommandBridgeToken& token,
                                                         editor::EditorUiFailure* const failure) noexcept override
        {
            const bool removed = m_surfaceCommands.Unregister(token, failure);
            if (removed)
                RecordBridgeLifecycleEvent(100u);
            return removed;
        }

        [[nodiscard]] bool PublishPanelFocus(const editor::detail::EditorPanelFocusUpdate& update,
                                             editor::EditorUiFailure* const failure = nullptr) noexcept
        {
            if (m_focusSink == nullptr)
                return false;
            if (!m_focusSink(update, m_focusSinkUserData, failure))
                return false;
            m_focus = update.focus;
            return true;
        }

        [[nodiscard]] editor::detail::EditorSurfaceRegistryStatus GetSurfaceStatus() const noexcept
        {
            return m_surfaces.GetStatus();
        }

        [[nodiscard]] bool HasSurfaceCommandBridge() const noexcept
        {
            return m_surfaceCommands.IsAttached();
        }

        [[nodiscard]] bool ResolveSurface(const editor::EditorUiSurfaceId id,
                                         editor::detail::EditorUiSurfaceToken& token,
                                         editor::EditorExtensionOwner& owner,
                                         editor::detail::EditorUiSurfaceKind& kind) const noexcept
        {
            return m_surfaces.ResolveSurface(id, token, owner, kind);
        }

        void VisitSurfaceContributions(const editor::EditorUiSurfaceId surface,
                                       editor::detail::EditorUiContributionVisitor visitor,
                                       void* const userData = nullptr) const noexcept
        {
            m_surfaces.VisitContributions(surface, visitor, userData);
        }

        [[nodiscard]] bool RegisterPanelFocusSink(editor::detail::EditorPanelFocusSink sink, void* const userData,
                                                 editor::detail::EditorPanelFocusSinkToken& token,
                                                 editor::EditorUiFailure*) noexcept override
        {
            token = {};
            if (sink == nullptr || m_focusSink != nullptr)
                return false;
            m_focusSink = sink;
            m_focusSinkUserData = userData;
            m_focusSinkToken = {++m_focusSinkGeneration};
            token = m_focusSinkToken;
            return true;
        }

        [[nodiscard]] bool UnregisterPanelFocusSink(const editor::detail::EditorPanelFocusSinkToken token,
                                                   editor::EditorUiFailure* const failure) noexcept override
        {
            if (m_focusSink == nullptr || token != m_focusSinkToken)
                return false;
            if (m_focus.HasFocus())
            {
                editor::detail::EditorPanelFocusUpdate cleared;
                cleared.focus.frame = m_frame;
                if (!m_focusSink(cleared, m_focusSinkUserData, failure))
                    return false;
                m_focus = cleared.focus;
            }
            m_focusSink = nullptr;
            m_focusSinkUserData = nullptr;
            m_focusSinkToken = {};
            RecordBridgeLifecycleEvent(200u);
            return true;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            if (auto* const frames = static_cast<engine::FramePipelineService*>(context.FindCapability(engine::FramePipelineCapabilityId)); frames != nullptr)
            {
                constexpr engine::FrameParticipantId dependencies[]{engine::InputFrameParticipantId};
                engine::FrameParticipantDescriptor descriptor;
                descriptor.id = editor::EditorUiFrameParticipantId;
                descriptor.name = "fixtureShellUi";
                descriptor.phase = engine::FramePhase::BeginFrame;
                descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
                descriptor.affinity = app::ThreadAffinity::MainThread;
                descriptor.after = {dependencies, 1};
                descriptor.execute = ExecuteFrame;
                descriptor.userData = this;
                engine::FrameFailure failure;
                if (!frames->RegisterParticipant(descriptor, &failure))
                    return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "fixture shell UI participant registration failed");
            }
            editor::EditorUiFailure surfaceFailure;
            if (!m_surfaces.InitializeBuiltIns(&surfaceFailure))
                return app::LifecycleStatus::Failure(surfaceFailure.message != nullptr ? surfaceFailure.message
                                                                                       : "fixture shell surface initialization failed");
            m_state = editor::EditorUiState::Ready;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            ++g_uiQuiesceCalls;
            m_capture = {};
            if (m_surfaceCommands.IsAttached())
                return app::LifecycleStatus::Failure("fixture UI surface command bridge still registered during quiesce");
            if (m_focusSink != nullptr)
                return app::LifecycleStatus::Failure("fixture UI focus sink still registered during quiesce");
            RecordBridgeLifecycleEvent(300u);
            m_state = editor::EditorUiState::Quiesced;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_capture = {};
            m_focus = {};
            m_focusSink = nullptr;
            m_focusSinkUserData = nullptr;
            m_focusSinkToken = {};
            m_surfaceCommands.Reset();
            m_frame = 0;
            m_surfaces.Reset();
            m_panels.Reset();
            m_state = editor::EditorUiState::Offline;
            return app::LifecycleStatus::Success();
        }

    private:
        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<FixtureUiService*>(userData);
            if (service == nullptr)
                return engine::FrameParticipantStatus::Failure("fixture shell UI is unavailable");
            service->m_frame = context.frame;
            service->m_capture = {context.frame, false, false};
            return engine::FrameParticipantStatus::Success();
        }

        editor::EditorUiState m_state = editor::EditorUiState::Offline;
        editor::detail::EditorPanelRegistryCore m_panels;
        editor::detail::EditorSurfaceRegistryCore m_surfaces;
        editor::detail::EditorUiCommandBridgeSlot m_surfaceCommands;
        editor::EditorUiInputCapture m_capture;
        editor::EditorPanelFocusSnapshot m_focus;
        editor::detail::EditorPanelFocusSink m_focusSink = nullptr;
        void* m_focusSinkUserData = nullptr;
        editor::detail::EditorPanelFocusSinkToken m_focusSinkToken;
        u32 m_focusSinkGeneration = 0;
        u64 m_frame = 0;
    };

    class FixtureActivityService final : public editor::detail::EditorActivityPrivateService
    {
    public:
        [[nodiscard]] bool CaptureDiagnostics(const editor::EditorActivityCursor&,
                                              containers::ArraySpan<editor::EditorActivityDiagnosticRecord>,
                                              editor::EditorActivityDiagnosticCapture& capture,
                                              editor::EditorActivityFailure*) const noexcept override
        {
            capture = {};
            capture.cursor.serviceGeneration = 1u;
            return true;
        }

        [[nodiscard]] bool CaptureActivities(const editor::EditorActivityCursor&,
                                             containers::ArraySpan<editor::EditorActivitySourceSnapshot>,
                                             containers::ArraySpan<editor::EditorActivityItem>,
                                             editor::EditorActivityCapture& capture,
                                             editor::EditorActivityFailure*) const noexcept override
        {
            capture = {};
            capture.cursor.serviceGeneration = 1u;
            return true;
        }

        [[nodiscard]] editor::EditorActivityCancelAdmission RequestCancel(
            editor::EditorActivityKey, editor::EditorActivityFailure*) noexcept override
        {
            return editor::EditorActivityCancelAdmission::Unsupported;
        }

        [[nodiscard]] bool ClearDiagnostics(editor::EditorActivityFailure*) noexcept override { return true; }

        [[nodiscard]] editor::EditorActivityServiceStatus GetStatus() const noexcept override
        {
            editor::EditorActivityServiceStatus status;
            status.state = editor::EditorActivityServiceState::Ready;
            status.serviceGeneration = 1u;
            for (const SourceRecord& source : m_sources)
                if (source.active && !source.revoked)
                    ++status.liveSources;
            return status;
        }

        [[nodiscard]] bool HasLiveSource(const editor::EditorActivitySourceId id) const noexcept
        {
            for (const SourceRecord& source : m_sources)
                if (source.active && !source.revoked && source.id == id && source.query != nullptr &&
                    source.userData != nullptr)
                    return true;
            return false;
        }

        [[nodiscard]] bool RegisterSourceForOwner(
            const editor::EditorExtensionOwner& owner,
            const editor::detail::EditorActivitySourceDescriptor& descriptor,
            editor::detail::EditorActivitySourceToken& token,
            editor::EditorActivityFailure* failure) noexcept override
        {
            if (failure != nullptr)
                *failure = {};
            if (!owner.IsValid() || descriptor.id == editor::InvalidEditorActivitySourceId ||
                descriptor.query == nullptr)
                return Reject(failure, editor::EditorActivityFailureCode::InvalidDescriptor,
                              descriptor.id, "fixture activity source descriptor is invalid");
            for (const SourceRecord& source : m_sources)
                if (source.active && !source.revoked && source.id == descriptor.id)
                    return Reject(failure, editor::EditorActivityFailureCode::DuplicateSource,
                                  descriptor.id, "fixture activity source is duplicate");
            if (m_sources.Size() >= editor::MaximumEditorActivitySources)
                return Reject(failure, editor::EditorActivityFailureCode::CapacityExceeded,
                              descriptor.id, "fixture activity source capacity is exhausted");

            SourceRecord source;
            source.owner = owner;
            source.id = descriptor.id;
            source.token = {m_sources.Size(), ++m_generation};
            source.query = descriptor.query;
            source.cancel = descriptor.requestCancel;
            source.userData = descriptor.userData;
            source.active = true;
            m_sources.PushBack(source);
            token = source.token;
            ++g_activityRegisterCalls;
            return true;
        }

        [[nodiscard]] bool UnregisterSourceForOwner(
            const editor::EditorExtensionOwner& owner,
            const editor::detail::EditorActivitySourceToken token,
            editor::EditorActivityFailure* failure) noexcept override
        {
            if (failure != nullptr)
                *failure = {};
            if (!token.IsValid() || token.index >= m_sources.Size())
                return Reject(failure, editor::EditorActivityFailureCode::StaleToken, {},
                              "fixture activity source token is stale");
            SourceRecord& source = m_sources[token.index];
            if (!source.active || source.token.generation != token.generation)
                return Reject(failure, editor::EditorActivityFailureCode::StaleToken, source.id,
                              "fixture activity source token is stale");
            if (!(source.owner == owner))
                return Reject(failure, editor::EditorActivityFailureCode::OwnerMismatch, source.id,
                              "fixture activity source owner mismatch");
            g_activityRevokeBeforeUnregister =
                g_activityRevokeBeforeUnregister || (source.revoked && source.query == nullptr &&
                                                     source.cancel == nullptr && source.userData == nullptr);
            source.active = false;
            ++g_activityUnregisterCalls;
            return true;
        }

        [[nodiscard]] bool RevokeSourceOwner(const editor::EditorExtensionOwner& owner,
                                             editor::EditorActivityFailure* failure) noexcept override
        {
            if (failure != nullptr)
                *failure = {};
            for (SourceRecord& source : m_sources)
            {
                if (!source.active || !(source.owner == owner))
                    continue;
                source.query = nullptr;
                source.cancel = nullptr;
                source.userData = nullptr;
                source.revoked = true;
            }
            ++g_activityRevokeCalls;
            return true;
        }

    private:
        struct SourceRecord final
        {
            editor::EditorExtensionOwner owner;
            editor::EditorActivitySourceId id = editor::InvalidEditorActivitySourceId;
            editor::detail::EditorActivitySourceToken token;
            editor::detail::EditorActivitySourceQuery query = nullptr;
            editor::detail::EditorActivitySourceCancel cancel = nullptr;
            void* userData = nullptr;
            bool active = false;
            bool revoked = false;
        };

        [[nodiscard]] static bool Reject(editor::EditorActivityFailure* const failure,
                                         const editor::EditorActivityFailureCode code,
                                         const editor::EditorActivitySourceId source,
                                         const char* const message) noexcept
        {
            if (failure != nullptr)
                *failure = {code, source, editor::InvalidEditorActivityItemId, message};
            return false;
        }

        containers::StaticArray<SourceRecord, editor::MaximumEditorActivitySources> m_sources;
        u64 m_generation = 0;
    };

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
        [[nodiscard]] containers::ArraySpan<const input::RawEvent> GetEvents() const noexcept override { return {}; }
        [[nodiscard]] const input::GamepadState* FindGamepad(input::DeviceId) const noexcept override { return nullptr; }
        [[nodiscard]] bool SetRumble(input::DeviceId, vanguard::f32, vanguard::f32, u32) noexcept override { return false; }
        void RequestReset() noexcept override {}
        void RequestDeviceRefresh() noexcept override {}
        [[nodiscard]] input::InputStats GetStats() const noexcept override { return {}; }

    private:
        input::FrameSnapshot m_snapshot;
    };

    struct WorkspaceFactoryData
    {
        vanguard::projects::ProjectDescriptor* project = nullptr;
    };

    app::Service* CreateFixtureWorkspace(void* const userData) noexcept
    {
        WorkspaceFactoryData& data = *static_cast<WorkspaceFactoryData*>(userData);
        const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Tools, sizeof(FixtureWorkspaceService), alignof(FixtureWorkspaceService));
        return block ? ::new (block.address) FixtureWorkspaceService(*data.project) : nullptr;
    }

    void DestroyFixtureWorkspace(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<FixtureWorkspaceService*>(service)->~FixtureWorkspaceService();
        memory::MemoryBlock block{service, sizeof(FixtureWorkspaceService), memory::PoolId::Tools};
        memory::Free(block);
    }

    app::Service* CreateFixtureUi(void*) noexcept
    {
        const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Tools, sizeof(FixtureUiService), alignof(FixtureUiService));
        return block ? ::new (block.address) FixtureUiService() : nullptr;
    }

    void DestroyFixtureUi(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<FixtureUiService*>(service)->~FixtureUiService();
        memory::MemoryBlock block{service, sizeof(FixtureUiService), memory::PoolId::Tools};
        memory::Free(block);
    }

    app::Service* CreateFixtureActivity(void*) noexcept
    {
        const memory::MemoryBlock block =
            memory::Allocate(memory::PoolId::Tools, sizeof(FixtureActivityService),
                             alignof(FixtureActivityService));
        return block ? ::new (block.address) FixtureActivityService() : nullptr;
    }

    void DestroyFixtureActivity(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<FixtureActivityService*>(service)->~FixtureActivityService();
        memory::MemoryBlock block{service, sizeof(FixtureActivityService), memory::PoolId::Tools};
        memory::Free(block);
    }

    app::Service* CreateFixtureFramePipeline(void*) noexcept
    {
        const memory::MemoryBlock block =
            memory::Allocate(memory::PoolId::Tools, sizeof(FixtureFramePipelineService), alignof(FixtureFramePipelineService));
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

    app::Service* CreateFixtureInput(void*) noexcept
    {
        const memory::MemoryBlock block =
            memory::Allocate(memory::PoolId::Tools, sizeof(FixtureInputService), alignof(FixtureInputService));
        return block ? ::new (block.address) FixtureInputService() : nullptr;
    }

    void DestroyFixtureInput(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<FixtureInputService*>(service)->~FixtureInputService();
        memory::MemoryBlock block{service, sizeof(FixtureInputService), memory::PoolId::Tools};
        memory::Free(block);
    }

    bool RegisterFixtureDependencies(app::EngineHost& host, WorkspaceFactoryData& workspaceFactory,
                                     app::HostFailure& failure, const bool commandDependencies = false,
                                     const bool activityDependency = true) noexcept
    {
        app::ServiceDescriptor workspace;
        workspace.id = editor::ProjectWorkspaceServiceId;
        workspace.name = "fixtureWorkspace";
        workspace.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        workspace.scope = app::ServiceScope::EditorWorkspace;
        workspace.affinity = app::ThreadAffinity::MainThread;
        workspace.create = CreateFixtureWorkspace;
        workspace.destroy = DestroyFixtureWorkspace;
        workspace.userData = &workspaceFactory;
        if (!host.RegisterService(editor::EditorModuleId, workspace, &failure))
            return false;

        if (activityDependency)
        {
            constexpr app::CapabilityId activityCapabilities[]{editor::EditorActivityCapabilityId};
            app::ServiceDescriptor activity;
            activity.id = editor::EditorActivityServiceId;
            activity.name = "fixtureActivity";
            activity.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            activity.scope = app::ServiceScope::EditorWorkspace;
            activity.affinity = app::ThreadAffinity::MainThread;
            activity.provides = {activityCapabilities, 1};
            activity.create = CreateFixtureActivity;
            activity.destroy = DestroyFixtureActivity;
            if (!host.RegisterService(editor::EditorModuleId, activity, &failure))
                return false;
        }

        const app::ServiceDependency uiDependencies[]{{editor::ProjectWorkspaceServiceId, app::DependencyKind::Required},
                                                      {engine::FramePipelineServiceId, app::DependencyKind::Required},
                                                      {engine::InputServiceId, app::DependencyKind::Required}};
        constexpr app::CapabilityId uiCapabilities[]{editor::EditorUiCapabilityId};
        app::ServiceDescriptor ui;
        ui.id = editor::EditorUiServiceId;
        ui.name = "fixtureUi";
        ui.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        ui.scope = app::ServiceScope::EditorWorkspace;
        ui.affinity = app::ThreadAffinity::MainThread;
        ui.dependencies = {uiDependencies, commandDependencies ? 3u : 1u};
        ui.provides = {uiCapabilities, 1};
        ui.create = CreateFixtureUi;
        ui.destroy = DestroyFixtureUi;
        return host.RegisterService(editor::EditorModuleId, ui, &failure);
    }

    bool RegisterFixtureCommandDependencies(app::EngineHost& host, app::HostFailure& failure) noexcept
    {
        constexpr app::CapabilityId frameCapabilities[]{engine::FramePipelineCapabilityId};
        app::ServiceDescriptor frame;
        frame.id = engine::FramePipelineServiceId;
        frame.name = "fixtureFramePipeline";
        frame.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        frame.scope = app::ServiceScope::Engine;
        frame.affinity = app::ThreadAffinity::MainThread;
        frame.provides = {frameCapabilities, 1};
        frame.create = CreateFixtureFramePipeline;
        frame.destroy = DestroyFixtureFramePipeline;
        if (!host.RegisterService(engine::EngineModuleId, frame, &failure))
            return false;

        constexpr app::CapabilityId inputCapabilities[]{engine::InputCapabilityId};
        app::ServiceDescriptor inputService;
        inputService.id = engine::InputServiceId;
        inputService.name = "fixtureInput";
        inputService.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        inputService.scope = app::ServiceScope::Engine;
        inputService.affinity = app::ThreadAffinity::MainThread;
        inputService.provides = {inputCapabilities, 1};
        inputService.create = CreateFixtureInput;
        inputService.destroy = DestroyFixtureInput;
        return host.RegisterService(engine::EngineModuleId, inputService, &failure);
    }

    struct ShellCompiledProof
    {
        bool seen = false;
        bool dependenciesPrecede = false;
        app::ServiceScope scope = app::ServiceScope::Engine;
        app::ThreadAffinity affinity = app::ThreadAffinity::CompositionThread;
        u32 workspaceIndex = ~u32{0};
        u32 uiIndex = ~u32{0};
        u32 commandIndex = ~u32{0};
        u32 activityIndex = ~u32{0};
        u32 shellIndex = ~u32{0};
    };

    void VisitShellPlan(const app::CompiledServiceInfo& service, void* const userData) noexcept
    {
        ShellCompiledProof& proof = *static_cast<ShellCompiledProof*>(userData);
        if (service.service == editor::ProjectWorkspaceServiceId)
            proof.workspaceIndex = service.startupIndex;
        else if (service.service == editor::EditorUiServiceId)
            proof.uiIndex = service.startupIndex;
        else if (service.service == editor::EditorCommandServiceId)
            proof.commandIndex = service.startupIndex;
        else if (service.service == editor::EditorActivityServiceId)
            proof.activityIndex = service.startupIndex;
        else if (service.service == editor::EditorShellServiceId)
        {
            proof.seen = true;
            proof.scope = service.scope;
            proof.affinity = service.affinity;
            proof.shellIndex = service.startupIndex;
            proof.dependenciesPrecede = proof.workspaceIndex < service.startupIndex && proof.uiIndex < service.startupIndex &&
                                       proof.commandIndex < service.startupIndex &&
                                       proof.activityIndex < service.startupIndex;
        }
    }

    void ResetLifecycleRecording() noexcept
    {
        g_activateCalls = 0;
        g_deactivateCalls = 0;
        g_activationOrderCount = 0;
        g_deactivationOrderCount = 0;
        g_observedActivationGeneration = 0;
        g_observedWorkspace = false;
    }

    void ResetOwnedRegistrationRecording() noexcept
    {
        g_uiQuiesceCalls = 0;
        g_activityRegisterCalls = 0;
        g_activityRevokeCalls = 0;
        g_activityUnregisterCalls = 0;
        g_activityRevokeBeforeUnregister = false;
    }

    void ResetPanelLifecycleRecording() noexcept
    {
        g_uiQuiesceCalls = 0;
        g_panelLifecycleEventCount = 0;
        for (u32& event : g_panelLifecycleEvents)
            event = 0;
    }

    void ResetSurfaceLifecycleRecording() noexcept
    {
        g_uiQuiesceCalls = 0;
        g_surfaceLifecycleEventCount = 0;
        g_surfaceRevokeReducedReachableTopology = false;
        for (u32& event : g_surfaceLifecycleEvents)
            event = 0;
    }

    void ResetBridgeLifecycleRecording() noexcept
    {
        g_uiQuiesceCalls = 0;
        g_bridgeLifecycleEventCount = 0;
        g_rejectSurfaceBridgeRegistration = false;
        for (u32& event : g_bridgeLifecycleEvents)
            event = 0;
    }

    void CheckPanelReverseEvents(const char* const message) noexcept
    {
        const u32 expected[]{182u, 282u, 181u, 281u};
        bool matches = g_panelLifecycleEventCount == static_cast<u32>(sizeof(expected) / sizeof(expected[0]));
        if (matches)
            for (u32 index = 0; index < g_panelLifecycleEventCount; ++index)
                matches = matches && g_panelLifecycleEvents[index] == expected[index];
        Check(matches, message);
    }

    bool PrepareShellHost(app::EngineHost& host, WorkspaceFactoryData& workspaceFactory, const editor::EditorShellServiceConfig& shellConfig,
                       app::HostFailure& failure) noexcept
    {
        return host.RegisterModule({engine::EngineModuleId, "engine", 1}, &failure) &&
               host.RegisterModule({editor::EditorModuleId, "editor", 1}, &failure) &&
               RegisterFixtureCommandDependencies(host, failure) &&
               RegisterFixtureDependencies(host, workspaceFactory, failure, true) &&
               editor::RegisterEditorCommandService(host, &failure) && editor::RegisterEditorShellService(host, shellConfig, &failure) &&
               host.Compile(app::ApplicationProfile::Editor, &failure);
    }

    [[nodiscard]] bool EqualProjectDescriptor(const vanguard::projects::ProjectDescriptor& left,
                                              const vanguard::projects::ProjectDescriptor& right) noexcept
    {
        if (!(left.id == right.id) || left.name != right.name || left.technicalName != right.technicalName ||
            left.engine.minimumMajor != right.engine.minimumMajor || left.engine.minimumMinor != right.engine.minimumMinor ||
            left.engine.minimumPatch != right.engine.minimumPatch || left.engine.maximumMajor != right.engine.maximumMajor ||
            left.engine.maximumMinor != right.engine.maximumMinor || left.engine.maximumPatchWildcard != right.engine.maximumPatchWildcard ||
            left.engine.maximumPatch != right.engine.maximumPatch || left.assets != right.assets || left.derivedData != right.derivedData ||
            left.intermediate != right.intermediate || left.saved != right.saved || left.builds != right.builds || left.config != right.config ||
            left.pluginsRoot != right.pluginsRoot || left.cookingPolicy != right.cookingPolicy || left.packagingPolicy != right.packagingPolicy ||
            left.editorWorld != right.editorWorld || left.runtimeWorld != right.runtimeWorld || left.input != right.input ||
            left.targets.Size() != right.targets.Size() || left.plugins.Size() != right.plugins.Size())
            return false;
        for (u32 index = 0; index < left.targets.Size(); ++index)
            if (left.targets[index] != right.targets[index])
                return false;
        for (u32 index = 0; index < left.plugins.Size(); ++index)
            if (left.plugins[index] != right.plugins[index])
                return false;
        return true;
    }

    void TestStaticBuiltInOnlyIntegration() noexcept
    {
        g_pluginsRootQueries = 0;

        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        project.pluginsRoot.Set("never-scan");
        const vanguard::projects::ProjectDescriptor projectBefore = project;
        WorkspaceFactoryData workspaceFactory{&project};
        const editor::EditorShellServiceConfig shellConfig{editor::testing::GetEditorTestExtensions()};
        app::EngineHost host;
        app::HostFailure failure;
        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure), "compiled static built-in host compiles");
        Check(host.Start(&failure), "compiled static built-in host starts");

        editor::EditorShellService* const shell = editor::FindEditorShellService(host);
        Check(shell != nullptr, "compiled static built-in host exposes shell");
        if (shell != nullptr)
        {
            const editor::EditorShellStatus status = shell->GetStatus();
            Check(status.catalogCount == 3 && status.selectedCount == 1 && status.activeCount == 1 &&
                      status.failure.code == editor::EditorExtensionFailureCode::None,
                  "compiled built-in-only selection activates only the built-in test extension");
        }
        Check(g_pluginsRootQueries == 0 && EqualProjectDescriptor(project, projectBefore),
              "shell does not query paths.plugins or mutate any built-in-only project descriptor field");

        Check(host.Shutdown(&failure), "compiled static built-in host shuts down");
        Check(g_pluginsRootQueries == 0 && EqualProjectDescriptor(project, projectBefore),
              "shutdown leaves paths.plugins untouched and every project descriptor field unchanged");
    }

    void TestStaticProjectAndDependencySelection() noexcept
    {
        g_pluginsRootQueries = 0;

        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        project.pluginsRoot.Set("never-scan");
        project.plugins.PushBack(containers::String("editor-test-project"));
        const vanguard::projects::ProjectDescriptor projectBefore = project;
        WorkspaceFactoryData workspaceFactory{&project};
        const editor::EditorShellServiceConfig shellConfig{editor::testing::GetEditorTestExtensions()};
        app::EngineHost host;
        app::HostFailure failure;
        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure), "known compiled project-selection host compiles");
        Check(host.Start(&failure), "known compiled project-selection host starts");

        editor::EditorShellService* const shell = editor::FindEditorShellService(host);
        Check(shell != nullptr, "known compiled project-selection host exposes shell");
        if (shell != nullptr)
        {
            const editor::EditorShellStatus status = shell->GetStatus();
            Check(status.catalogCount == 3 && status.selectedCount == 3 && status.activeCount == 3 &&
                      status.failure.code == editor::EditorExtensionFailureCode::None,
                  "known compiled selection activates the built-in, dependency closure and project test extensions");
        }
        Check(g_pluginsRootQueries == 0 && EqualProjectDescriptor(project, projectBefore),
              "known selection reads retained project names only; paths.plugins is not queried and no descriptor field is mutated");

        Check(host.Shutdown(&failure), "known compiled project-selection host shuts down");
        Check(g_pluginsRootQueries == 0 && EqualProjectDescriptor(project, projectBefore),
              "selected extension shutdown leaves every project descriptor field unchanged");
    }

    void TestProductionDemonstrationExtension() noexcept
    {
        ResetOwnedRegistrationRecording();
        ResetSurfaceLifecycleRecording();

        const containers::ArraySpan<const editor::EditorExtensionDescriptor> descriptors =
            editor::GetEditorDemonstrationExtensions();
        Check(descriptors.Size() == 1 && descriptors[0].id == DemonstrationExtensionId &&
                  descriptors[0].selection == editor::EditorExtensionSelectionPolicy::ProjectSelectable &&
                  std::strcmp(descriptors[0].technicalName, "editor-demo-tool") == 0,
              "production demonstration provider exposes the stable project-selectable descriptor");

        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("demonstration-fixture-project");
        project.plugins.PushBack(containers::String("editor-demo-tool"));
        WorkspaceFactoryData workspaceFactory{&project};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        app::EngineHost host;
        app::HostFailure hostFailure;
        Check(PrepareShellHost(host, workspaceFactory, shellConfig, hostFailure),
              "production demonstration host compiles");
        Check(host.Start(&hostFailure), "production demonstration host starts");

        editor::EditorShellService* const shell = editor::FindEditorShellService(host);
        editor::EditorCommandService* const commands = editor::FindEditorCommandService(host);
        auto* const ui = static_cast<FixtureUiService*>(host.FindCapability(editor::EditorUiCapabilityId));
        auto* const activity = static_cast<FixtureActivityService*>(host.FindCapability(editor::EditorActivityCapabilityId));
        Check(shell != nullptr && commands != nullptr && ui != nullptr && activity != nullptr,
              "production demonstration host exposes shell, command, UI and Activity owners");
        if (shell == nullptr || commands == nullptr || ui == nullptr || activity == nullptr)
            return;

        const editor::EditorShellStatus shellStatus = shell->GetStatus();
        Check(shellStatus.state == editor::EditorShellState::Active && shellStatus.catalogCount == 1 &&
                  shellStatus.selectedCount == 1 && shellStatus.activeCount == 1 &&
                  shellStatus.failure.code == editor::EditorExtensionFailureCode::None,
              "project selection activates exactly the production demonstration extension");

        struct PanelObservation final
        {
            editor::EditorPanelHandle handle;
            bool found = false;
            bool open = false;
        } panel;
        ui->GetPanels().VisitPanels(
            [](const editor::EditorPanelInfo& info, void* const userData) noexcept
            {
                auto* const observation = static_cast<PanelObservation*>(userData);
                if (observation == nullptr || info.type != DemonstrationPanelTypeId ||
                    info.id != DemonstrationPanelInstanceId)
                    return;
                observation->handle = info.handle;
                observation->found = true;
                observation->open = info.open;
            },
            &panel);
        Check(panel.found && panel.handle.IsValid() && panel.open,
              "demonstration activation creates the stable singleton panel");

        editor::EditorShortcutBinding binding;
        editor::EditorCommandFailure commandFailure;
        editor::EditorCommandState commandState;
        Check(commands->GetShortcutBinding(editor::EditorWorkspaceCommandContextTypeId,
                                           DemonstrationCommandId, binding, &commandFailure) &&
                  binding.bound && binding.mode == editor::EditorShortcutOverrideMode::Default &&
                  binding.chord.key == input::Key::D &&
                  binding.chord.modifiers ==
                      (editor::EditorShortcutModifiers::Control | editor::EditorShortcutModifiers::Shift) &&
                  commands->Query(DemonstrationCommandId, commandState, &commandFailure) &&
                  commandState.kind == editor::EditorCommandStateKind::Enabled,
              "demonstration command is routed through Workspace with the locked Ctrl+Shift+D binding");

        if (panel.handle.IsValid())
        {
            editor::EditorUiFailure uiFailure;
            Check(ui->GetPanels().SetOpen(panel.handle, false, &uiFailure) &&
                      !ui->GetPanels().IsOpen(panel.handle) &&
                      commands->Invoke(DemonstrationCommandId, &commandFailure) &&
                      ui->GetPanels().IsOpen(panel.handle),
                  "direct demonstration command invocation reopens its owned panel");
        }

        struct ContributionObservation final
        {
            u32 matching = 0;
            bool ownerMatches = true;
        } menu, toolbar, context;
        const auto collectCommand = [](const editor::detail::EditorUiContributionView& contribution,
                                       void* const userData) noexcept
        {
            auto* const observation = static_cast<ContributionObservation*>(userData);
            if (observation == nullptr || contribution.command != DemonstrationCommandId)
                return;
            ++observation->matching;
            observation->ownerMatches = observation->ownerMatches &&
                                        contribution.owner.extension == DemonstrationExtensionId;
        };
        ui->VisitSurfaceContributions(editor::detail::EditorMainMenuSurfaceId, collectCommand, &menu);
        ui->VisitSurfaceContributions(editor::detail::EditorMainToolbarSurfaceId, collectCommand, &toolbar);
        ui->VisitSurfaceContributions(DemonstrationContextMenuSurfaceId, collectCommand, &context);
        editor::detail::EditorUiSurfaceToken contextSurface;
        editor::EditorExtensionOwner contextOwner;
        editor::detail::EditorUiSurfaceKind contextKind = editor::detail::EditorUiSurfaceKind::MainMenu;
        Check(menu.matching == 1 && menu.ownerMatches && toolbar.matching == 1 && toolbar.ownerMatches &&
                  context.matching == 1 && context.ownerMatches &&
                  ui->ResolveSurface(DemonstrationContextMenuSurfaceId, contextSurface, contextOwner, contextKind) &&
                  contextOwner.extension == DemonstrationExtensionId &&
                  contextKind == editor::detail::EditorUiSurfaceKind::ContextMenu,
              "menu, toolbar and context-menu contributions all route the same owner-scoped command identity");

        Check(activity->GetStatus().liveSources == 1 && activity->HasLiveSource(DemonstrationActivitySourceId),
              "demonstration activation registers one live bounded Activity status source");

        auto& persistence = static_cast<editor::detail::EditorShellPersistencePrivateService&>(*shell);
        editor::EditorPreferenceFailure preferenceFailure;
        editor::EditorTypedPreferenceRecord captured[1]{};
        u32 capturedCount = 0;
        Check(persistence.CaptureTypedPreferences(captured, 1, capturedCount, &preferenceFailure) &&
                  capturedCount == 1 && captured[0].extension == DemonstrationExtensionId &&
                  captured[0].providerId == DemonstrationPreferenceProviderId &&
                  captured[0].fieldId == DemonstrationDetailsPreferenceFieldId &&
                  captured[0].value.type == editor::EditorPreferenceValueType::Bool &&
                  captured[0].value.value.boolean,
              "demonstration typed preference captures its default detailed-status value");

        editor::EditorTypedPreferenceRecord restored = captured[0];
        restored.value.value.boolean = false;
        editor::EditorTypedPreferenceItemResult result{};
        Check(persistence.ValidateTypedPreferences({&restored, 1}, {&result, 1}, &preferenceFailure) &&
                  result == editor::EditorTypedPreferenceItemResult::Valid &&
                  persistence.ApplyTypedPreferences({&restored, 1}, {&result, 1}, &preferenceFailure) &&
                  persistence.CaptureTypedPreferences(captured, 1, capturedCount, &preferenceFailure) &&
                  captured[0].value.type == editor::EditorPreferenceValueType::Bool &&
                  !captured[0].value.value.boolean,
              "demonstration typed preference validates, applies and captures through the Shell owner");

        Check(host.Shutdown(&hostFailure),
              "production demonstration host shuts down with every owner-scoped registration removed");
        Check(g_activityRegisterCalls == 1 && g_activityRevokeCalls == 1 &&
                  g_activityUnregisterCalls == 1 && g_activityRevokeBeforeUnregister &&
                  g_surfaceRevokeReducedReachableTopology,
              "demonstration teardown logically revokes Activity/surface callbacks before physical cleanup and state release");
    }

    void TestUnknownProjectSelectionFailsWithoutPluginPathAccess() noexcept
    {
        g_pluginsRootQueries = 0;

        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        project.pluginsRoot.Set("never-scan");
        project.plugins.PushBack(containers::String("missing-static-test"));
        const vanguard::projects::ProjectDescriptor projectBefore = project;
        WorkspaceFactoryData workspaceFactory{&project};
        const editor::EditorShellServiceConfig shellConfig{editor::testing::GetEditorTestExtensions()};
        app::EngineHost host;
        app::HostFailure failure;
        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure), "unknown project-selection host compiles");
        Check(!host.Start(&failure), "unknown project extension fails startup instead of scanning for a plugin");
        Check(editor::FindEditorShellService(host) == nullptr,
              "failed Initialize follows EngineHost rollback and leaves no partially live shell service");
        editor::EditorExtensionCatalog catalog;
        editor::EditorExtensionSelection selection;
        editor::EditorExtensionFailure selectionFailure;
        Check(catalog.Initialize(editor::testing::GetEditorTestExtensions(), &selectionFailure) &&
                  !catalog.ResolveSelection(project.plugins, selection, &selectionFailure) &&
                  selectionFailure.code == editor::EditorExtensionFailureCode::UnknownProjectSelection &&
                  selectionFailure.technicalName != nullptr && std::strcmp(selectionFailure.technicalName, "missing-static-test") == 0,
              "compiled selection authority reports stable unknown-selection code/name");
        Check(g_pluginsRootQueries == 0 && EqualProjectDescriptor(project, projectBefore),
              "unknown selection never queries paths.plugins and never mutates any project descriptor field");
    }

    void TestEmptyCatalogLifecycle() noexcept
    {
        const editor::EditorShellServiceConfig shellConfig{};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure), "empty-catalog host compiles");
        Check(host.Start(&failure), "empty-catalog host starts successfully");
        editor::EditorShellService* const shell = editor::FindEditorShellService(host);
        Check(shell != nullptr, "empty-catalog host exposes the shell");
        if (shell != nullptr)
        {
            const editor::EditorShellStatus status = shell->GetStatus();
            Check(status.state == editor::EditorShellState::Active && status.catalogCount == 0 && status.selectedCount == 0 &&
                      status.activeCount == 0 && status.activationGeneration != 0 && status.failure.code == editor::EditorExtensionFailureCode::None,
                  "empty catalog reaches an active shell with zero selected/live extension state");
        }
        Check(host.Shutdown(&failure), "empty-catalog host shuts down cleanly");
        Check(editor::FindEditorShellService(host) == nullptr,
              "completed shutdown destroys the shell after Drain proved zero live owned state");
    }

    void TestSurfaceCommandBridgeLifecycle() noexcept
    {
        ResetBridgeLifecycleRecording();
        const editor::EditorShellServiceConfig shellConfig{};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "surface-command bridge lifecycle host compiles");
        Check(host.Start(&failure), "surface-command bridge lifecycle host starts");
        auto* const ui = static_cast<FixtureUiService*>(host.FindCapability(editor::EditorUiCapabilityId));
        Check(ui != nullptr && ui->HasSurfaceCommandBridge(),
              "shell initialization installs one live surface-command bridge into the existing UI owner");
        Check(host.Shutdown(&failure), "surface-command bridge lifecycle host shuts down cleanly");

        const u32 expected[]{100u, 200u, 300u};
        bool ordered = g_bridgeLifecycleEventCount == static_cast<u32>(sizeof(expected) / sizeof(expected[0]));
        if (ordered)
            for (u32 index = 0; index < g_bridgeLifecycleEventCount; ++index)
                ordered = ordered && g_bridgeLifecycleEvents[index] == expected[index];
        Check(ordered && g_uiQuiesceCalls == 1u,
              "shell detaches the surface-command bridge before panel-focus teardown and UI quiesce");
    }

    void TestSurfaceCommandBridgeInstallRollback() noexcept
    {
        ResetBridgeLifecycleRecording();
        g_rejectSurfaceBridgeRegistration = true;
        const editor::EditorShellServiceConfig shellConfig{};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "surface-command bridge rollback host compiles");
        Check(!host.Start(&failure),
              "surface-command bridge installation failure rejects shell startup");
        Check(editor::FindEditorShellService(host) == nullptr,
              "failed bridge installation leaves no partially live shell service");
        Check(g_bridgeLifecycleEventCount >= 1u && g_bridgeLifecycleEvents[0] == 200u,
              "partial bridge installation failure removes the already-installed panel-focus bridge first");
        g_rejectSurfaceBridgeRegistration = false;
    }

    void TestProjectSelectionWithMissingCompiledDependency() noexcept
    {
        ResetLifecycleRecording();
        constexpr editor::EditorExtensionId missingDependency{99};
        const editor::EditorExtensionId dependencies[]{missingDependency};
        const editor::EditorExtensionDescriptor descriptors[]{
            RecordingDescriptor(3, "gamma", editor::EditorExtensionSelectionPolicy::ProjectSelectable, dependencies)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        project.plugins.PushBack(containers::String("gamma"));
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure), "dependency-incomplete project host compiles");
        Check(!host.Start(&failure), "project-selected descriptor with an absent compiled dependency fails before activation");
        Check(g_activateCalls == 0 && g_deactivateCalls == 0 && editor::FindEditorShellService(host) == nullptr,
              "dependency-incomplete project selection leaves no partial extension lifecycle or live shell instance");

        editor::EditorExtensionCatalog catalog;
        editor::EditorExtensionFailure catalogFailure;
        Check(!catalog.Initialize(descriptors, &catalogFailure) && catalogFailure.code == editor::EditorExtensionFailureCode::MissingDependency &&
                  catalogFailure.extension == editor::EditorExtensionId{3} && catalogFailure.relatedExtension == missingDependency,
              "dependency-incomplete compiled catalog reports stable missing-dependency identities");
    }

    void TestIndependentDependencyChainPermutation() noexcept
    {
        ResetLifecycleRecording();
        constexpr editor::EditorExtensionId deltaId{4};
        constexpr editor::EditorExtensionId betaId{2};
        const editor::EditorExtensionId betaDependencies[]{deltaId};
        const editor::EditorExtensionId gammaDependencies[]{betaId};
        const editor::EditorExtensionDescriptor descriptors[]{
            RecordingDescriptor(5, "epsilon", editor::EditorExtensionSelectionPolicy::BuiltIn),
            RecordingDescriptor(3, "gamma", editor::EditorExtensionSelectionPolicy::ProjectSelectable, gammaDependencies),
            RecordingDescriptor(2, "beta", editor::EditorExtensionSelectionPolicy::DependencyOnly, betaDependencies),
            RecordingDescriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn),
            RecordingDescriptor(4, "delta", editor::EditorExtensionSelectionPolicy::DependencyOnly)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        project.plugins.PushBack(containers::String("gamma"));
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure), "independent/dependency-chain host compiles");
        Check(host.Start(&failure), "independent/dependency-chain host starts");
        Check(g_activationOrderCount == 5 && g_activationOrder[0] == 1 && g_activationOrder[1] == 4 && g_activationOrder[2] == 2 &&
                  g_activationOrder[3] == 5 && g_activationOrder[4] == 3,
              "deterministic ordering combines independent built-ins with a multi-level dependency chain");
        Check(host.Shutdown(&failure), "independent/dependency-chain host shuts down");

        ResetLifecycleRecording();
        const editor::EditorExtensionDescriptor permuted[]{
            RecordingDescriptor(4, "delta", editor::EditorExtensionSelectionPolicy::DependencyOnly),
            RecordingDescriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn),
            RecordingDescriptor(2, "beta", editor::EditorExtensionSelectionPolicy::DependencyOnly, betaDependencies),
            RecordingDescriptor(5, "epsilon", editor::EditorExtensionSelectionPolicy::BuiltIn),
            RecordingDescriptor(3, "gamma", editor::EditorExtensionSelectionPolicy::ProjectSelectable, gammaDependencies)};
        const editor::EditorShellServiceConfig permutedConfig{permuted};
        WorkspaceFactoryData permutedWorkspaceFactory{&project};
        app::EngineHost permutedHost;
        app::HostFailure permutedFailure;
        Check(PrepareShellHost(permutedHost, permutedWorkspaceFactory, permutedConfig, permutedFailure),
              "permuted independent/dependency-chain host compiles");
        Check(permutedHost.Start(&permutedFailure), "permuted independent/dependency-chain host starts");
        Check(g_activationOrderCount == 5 && g_activationOrder[0] == 1 && g_activationOrder[1] == 4 && g_activationOrder[2] == 2 &&
                  g_activationOrder[3] == 5 && g_activationOrder[4] == 3,
              "activation order is invariant to descriptor input permutation");
        Check(permutedHost.Shutdown(&permutedFailure), "permuted independent/dependency-chain host shuts down");
    }

    void TestShellServiceSuccessfulActivation() noexcept
    {
        ResetLifecycleRecording();
        constexpr editor::EditorExtensionId betaId{2};
        const editor::EditorExtensionId gammaDependencies[]{betaId};
        const editor::EditorExtensionDescriptor descriptors[]{
            RecordingDescriptor(3, "gamma", editor::EditorExtensionSelectionPolicy::ProjectSelectable, gammaDependencies),
            RecordingDescriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn),
            RecordingDescriptor(2, "beta", editor::EditorExtensionSelectionPolicy::DependencyOnly)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};

        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        project.plugins.PushBack(containers::String("gamma"));
        WorkspaceFactoryData workspaceFactory{&project};

        app::EngineHost host;
        app::HostFailure failure;
        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure), "editor service graph compiles");

        ShellCompiledProof compiled;
        host.VisitCompiledServices(VisitShellPlan, &compiled);
        Check(compiled.seen && compiled.scope == app::ServiceScope::EditorWorkspace && compiled.affinity == app::ThreadAffinity::MainThread,
              "shell service is EditorWorkspace/main-thread scoped");
        Check(compiled.dependenciesPrecede, "shell starts after workspace, Activity, UI, and command dependencies");

        Check(host.Start(&failure), "editor service graph starts");
        editor::EditorShellService* const shell = editor::FindEditorShellService(host);
        Check(shell != nullptr, "running editor profile exposes shell capability");
        if (shell != nullptr)
        {
            const editor::EditorShellStatus status = shell->GetStatus();
            Check(status.state == editor::EditorShellState::Active, "shell reports active after successful activation");
            Check(status.catalogCount == 3 && status.selectedCount == 3 && status.activeCount == 3,
                  "shell status reports catalog/selected/active counts");
            Check(status.activationGeneration != 0 && status.activationGeneration == g_observedActivationGeneration,
                  "shell publishes the nonzero activation generation seen by callbacks");
            Check(status.currentExtension == editor::EditorExtensionId{} && status.currentTechnicalName == nullptr &&
                      status.failure.code == editor::EditorExtensionFailureCode::None,
                  "successful shell status has no current or failed extension");
        }
        Check(g_observedWorkspace, "activation context exposes the retained project workspace owner");
        Check(g_activationOrderCount == 3 && g_activationOrder[0] == 1 && g_activationOrder[1] == 2 && g_activationOrder[2] == 3,
              "activates built-in/dependency/project extension selection in deterministic order");

        Check(host.Shutdown(&failure), "successful shell host shuts down cleanly");
        Check(g_deactivationOrderCount == 3 && g_deactivationOrder[0] == 3 && g_deactivationOrder[1] == 2 && g_deactivationOrder[2] == 1,
              "baseline successful-path teardown deactivates in reverse order before owned-registration work");
    }

    void TestShellServiceProfileExclusion(const app::ApplicationProfile profile, const char* const compileMessage) noexcept
    {
        ResetLifecycleRecording();
        const editor::EditorExtensionDescriptor descriptors[]{
            RecordingDescriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};

        app::EngineHost host;
        app::HostFailure failure;
        Check(host.RegisterModule({editor::EditorModuleId, "editor", 1}, &failure), "runtime exclusion fixture module registers");
        Check(RegisterFixtureDependencies(host, workspaceFactory, failure), "runtime exclusion fixture dependencies register");
        Check(editor::RegisterEditorShellService(host, shellConfig, &failure), "runtime exclusion shell service registers");
        Check(host.Compile(profile, &failure), compileMessage);
        Check(host.GetStats().selectedServices == 0 && editor::FindEditorShellService(host) == nullptr,
              "non-editor profile does not select or expose the editor shell");
        Check(host.Start(&failure), "non-editor profile starts without editor shell");
        Check(g_activateCalls == 0, "non-editor profile invokes no editor extension callback");
        Check(host.Shutdown(&failure), "non-editor exclusion fixture shuts down");
    }

    class WrongThreadStartWorker final : public vanguard::concurrency::Thread
    {
    public:
        explicit WrongThreadStartWorker(app::EngineHost& host) noexcept : Thread("shellWrongThread"), m_host(host) {}

        void ThreadFunction() noexcept override
        {
            m_started = m_host.Start(&m_failure);
        }

        [[nodiscard]] bool Started() const noexcept { return m_started; }
        [[nodiscard]] const app::HostFailure& Failure() const noexcept { return m_failure; }

    private:
        app::EngineHost& m_host;
        app::HostFailure m_failure;
        bool m_started = false;
    };

    class WrongThreadShutdownWorker final : public vanguard::concurrency::Thread
    {
    public:
        explicit WrongThreadShutdownWorker(app::EngineHost& host) noexcept : Thread("shellWrongShutdown"), m_host(host) {}

        void ThreadFunction() noexcept override
        {
            m_shutdown = m_host.Shutdown(&m_failure);
        }

        [[nodiscard]] bool Shutdown() const noexcept { return m_shutdown; }
        [[nodiscard]] const app::HostFailure& Failure() const noexcept { return m_failure; }

    private:
        app::EngineHost& m_host;
        app::HostFailure m_failure;
        bool m_shutdown = false;
    };

    class WrongThreadPanelFocusWorker final : public vanguard::concurrency::Thread
    {
    public:
        WrongThreadPanelFocusWorker(FixtureUiService& ui, const editor::detail::EditorPanelFocusUpdate& update) noexcept
            : Thread("panelFocusWrongThread"), m_ui(ui), m_update(update)
        {
        }

        void ThreadFunction() noexcept override
        {
            m_published = m_ui.PublishPanelFocus(m_update, &m_failure);
        }

        [[nodiscard]] bool Published() const noexcept { return m_published; }
        [[nodiscard]] const editor::EditorUiFailure& Failure() const noexcept { return m_failure; }

    private:
        FixtureUiService& m_ui;
        editor::detail::EditorPanelFocusUpdate m_update;
        editor::EditorUiFailure m_failure;
        bool m_published = false;
    };

    void TestShellRejectsWrongThreadStart() noexcept
    {
        ResetLifecycleRecording();
        const editor::EditorExtensionDescriptor descriptors[]{
            RecordingDescriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};

        app::EngineHost host;
        app::HostFailure failure;
        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure), "wrong-thread fixture graph compiles on main thread");

        WrongThreadStartWorker worker(host);
        worker.InitThread();
        worker.JoinThread();
        Check(!worker.Started() && worker.Failure().code == app::HostFailureCode::WrongThread,
              "direct host Start from a worker is rejected before shell construction");
        Check(g_activateCalls == 0 && g_deactivateCalls == 0,
              "wrong-thread host entry never invokes extension activation or deactivation callbacks");
    }

    void TestShellRejectsWrongThreadShutdown() noexcept
    {
        ResetLifecycleRecording();
        const editor::EditorExtensionDescriptor descriptors[]{
            RecordingDescriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};

        app::EngineHost host;
        app::HostFailure failure;
        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure), "wrong-shutdown fixture graph compiles");
        Check(host.Start(&failure) && g_activateCalls == 1, "wrong-shutdown fixture activates on the main thread");

        WrongThreadShutdownWorker worker(host);
        worker.InitThread();
        worker.JoinThread();
        Check(!worker.Shutdown() && worker.Failure().code == app::HostFailureCode::WrongThread && host.State() == app::HostState::Running,
              "worker-thread Shutdown is rejected before destructive unwind and leaves the host running");
        Check(g_deactivateCalls == 0, "wrong-thread Shutdown invokes no extension deactivation callback");

        Check(host.Shutdown(&failure), "main-thread Shutdown retry succeeds after wrong-thread rejection");
        Check(g_deactivateCalls == 1 && g_deactivationOrderCount == 1 && g_deactivationOrder[0] == 1,
              "main-thread Shutdown retry performs the baseline reverse extension deactivation");
    }

    void TestRequiredCommandServiceDependency() noexcept
    {
        ResetLifecycleRecording();
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        const editor::EditorExtensionDescriptor descriptors[]{
            RecordingDescriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure failure;
        Check(host.RegisterModule({editor::EditorModuleId, "editor", 1}, &failure) &&
                  RegisterFixtureDependencies(host, workspaceFactory, failure) &&
                  editor::RegisterEditorShellService(host, shellConfig, &failure),
              "required command-service dependency fixture registers");
        Check(!host.Compile(app::ApplicationProfile::Editor, &failure) &&
                  failure.code == app::HostFailureCode::MissingDependency &&
                  failure.service == editor::EditorShellServiceId && failure.relatedService == editor::EditorCommandServiceId,
              "missing required command service fails the shell graph before startup");
        Check(g_activateCalls == 0u,
              "missing required command service cannot reach extension activation");
    }

    struct ActivityExtensionFixture final
    {
        editor::detail::EditorActivitySourceToken token;
        bool registrationSucceeded = false;
        bool borrowedActivityReady = false;
        bool failAfterRegistration = false;
        bool privateStateAlive = true;
        bool deactivateCalls = false;
        bool deactivateSawLogicalAndPhysicalCleanup = false;
        bool lateRegistrationRejected = false;
        editor::EditorActivityFailureCode lateFailure = editor::EditorActivityFailureCode::None;
    };

    bool EmptyActivityQuery(editor::detail::EditorActivitySourceWriter&, void*,
                            editor::EditorActivityFailure*) noexcept
    {
        return true;
    }

    bool ActivityExtensionActivate(editor::EditorExtensionActivationContext& context,
                                   const void* const descriptorData, void*& privateState) noexcept
    {
        auto& fixture = *const_cast<ActivityExtensionFixture*>(
            static_cast<const ActivityExtensionFixture*>(descriptorData));
        privateState = &fixture;
        fixture.borrowedActivityReady =
            context.GetActivity().GetStatus().state == editor::EditorActivityServiceState::Ready;
        editor::detail::EditorActivitySourceDescriptor source;
        source.id = 0xa501u;
        source.displayName = "shell activity fixture";
        source.query = &EmptyActivityQuery;
        source.userData = &fixture;
        editor::EditorActivityFailure failure;
        fixture.registrationSucceeded =
            context.RegisterActivitySource(source, fixture.token, &failure);
        return fixture.registrationSucceeded && !fixture.failAfterRegistration;
    }

    void ActivityExtensionDeactivate(editor::EditorExtensionActivationContext& context,
                                     const void* const descriptorData, void* const privateState) noexcept
    {
        auto& fixture = *const_cast<ActivityExtensionFixture*>(
            static_cast<const ActivityExtensionFixture*>(descriptorData));
        fixture.deactivateCalls = true;
        fixture.deactivateSawLogicalAndPhysicalCleanup =
            privateState == &fixture && fixture.privateStateAlive &&
            g_activityRevokeCalls != 0u && g_activityUnregisterCalls != 0u &&
            g_activityRevokeBeforeUnregister;

        editor::detail::EditorActivitySourceDescriptor late;
        late.id = 0xa502u;
        late.displayName = "late activity fixture";
        late.query = &EmptyActivityQuery;
        editor::detail::EditorActivitySourceToken rejected;
        editor::EditorActivityFailure failure;
        fixture.lateRegistrationRejected =
            !context.RegisterActivitySource(late, rejected, &failure) && !rejected.IsValid();
        fixture.lateFailure = failure.code;
        fixture.privateStateAlive = false;
    }

    editor::EditorExtensionDescriptor ActivityExtensionDescriptor(
        const editor::EditorExtensionId id, const char* const name,
        ActivityExtensionFixture& fixture) noexcept
    {
        editor::EditorExtensionDescriptor descriptor;
        descriptor.id = id;
        descriptor.technicalName = name;
        descriptor.displayName = name;
        descriptor.selection = editor::EditorExtensionSelectionPolicy::BuiltIn;
        descriptor.activate = &ActivityExtensionActivate;
        descriptor.deactivate = &ActivityExtensionDeactivate;
        descriptor.data = &fixture;
        return descriptor;
    }

    void TestRequiredActivityServiceDependency() noexcept
    {
        ResetLifecycleRecording();
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        const editor::EditorExtensionDescriptor descriptors[]{
            RecordingDescriptor(1, "alpha", editor::EditorExtensionSelectionPolicy::BuiltIn)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure failure;
        Check(host.RegisterModule({engine::EngineModuleId, "engine", 1}, &failure) &&
                  host.RegisterModule({editor::EditorModuleId, "editor", 1}, &failure) &&
                  RegisterFixtureCommandDependencies(host, failure) &&
                  RegisterFixtureDependencies(host, workspaceFactory, failure, true, false) &&
                  editor::RegisterEditorCommandService(host, &failure) &&
                  editor::RegisterEditorShellService(host, shellConfig, &failure),
              "required activity-service dependency fixture registers");
        Check(!host.Compile(app::ApplicationProfile::Editor, &failure) &&
                  failure.code == app::HostFailureCode::MissingDependency &&
                  failure.service == editor::EditorShellServiceId &&
                  failure.relatedService == editor::EditorActivityServiceId,
              "missing required Activity service fails the shell graph before startup");
        Check(g_activateCalls == 0u,
              "missing required Activity service cannot reach extension activation");
    }

    void TestOwnedActivitySourceRegistrationAndRollback() noexcept
    {
        ResetOwnedRegistrationRecording();
        ActivityExtensionFixture fixture;
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        const editor::EditorExtensionDescriptor descriptors[]{
            ActivityExtensionDescriptor({0xa510u}, "activity-owner", fixture)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "owned Activity source shell host compiles");
        ShellCompiledProof proof;
        host.VisitCompiledServices(VisitShellPlan, &proof);
        Check(proof.seen && proof.dependenciesPrecede && proof.activityIndex < proof.shellIndex,
              "Activity service is a structural shell dependency before activation");
        Check(host.Start(&failure), "owned Activity source extension activates");
        Check(fixture.borrowedActivityReady && fixture.registrationSucceeded &&
                  fixture.token.IsValid() && g_activityRegisterCalls == 1u,
              "activation context borrows Activity and records the exact source token in the shared ledger");
        Check(host.Shutdown(&failure), "owned Activity source host shuts down cleanly");
        Check(fixture.deactivateCalls && fixture.deactivateSawLogicalAndPhysicalCleanup &&
                  fixture.lateRegistrationRejected &&
                  (fixture.lateFailure == editor::EditorActivityFailureCode::NotReady ||
                   fixture.lateFailure == editor::EditorActivityFailureCode::OwnerMismatch),
              "Activity logical revoke and exact physical cleanup precede extension private-state teardown");

        ResetOwnedRegistrationRecording();
        ActivityExtensionFixture failing;
        failing.failAfterRegistration = true;
        const editor::EditorExtensionDescriptor failingDescriptors[]{
            ActivityExtensionDescriptor({0xa511u}, "failing-activity-owner", failing)};
        const editor::EditorShellServiceConfig failingConfig{failingDescriptors};
        app::EngineHost failingHost;
        Check(PrepareShellHost(failingHost, workspaceFactory, failingConfig, failure),
              "partial Activity activation fixture compiles");
        Check(!failingHost.Start(&failure),
              "activation failure after Activity source registration rolls back startup");
        Check(failing.registrationSucceeded && failing.deactivateCalls &&
                  failing.deactivateSawLogicalAndPhysicalCleanup &&
                  g_activityRegisterCalls == 1u && g_activityRevokeCalls == 1u &&
                  g_activityUnregisterCalls == 1u,
              "partial activation rollback uses the same Activity ledger revoke/cleanup path");
    }

    void TestOwnedCommandRegistrationAndOrdering() noexcept
    {
        ResetOwnedRegistrationRecording();
        CommandExtensionFixture fixture;
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        const editor::EditorExtensionDescriptor descriptors[]{CommandExtensionDescriptor(41, "command-owner", fixture)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        fixture.host = &host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "command-owned shell host compiles");
        ShellCompiledProof proof;
        host.VisitCompiledServices(VisitShellPlan, &proof);
        Check(proof.seen && proof.commandIndex < proof.shellIndex && proof.workspaceIndex < proof.shellIndex &&
                  proof.uiIndex < proof.shellIndex,
              "StartAfter orders the selected command service before shell without changing Workspace/UI requirements");
        Check(host.Start(&failure), "command-owned shell host starts");
        Check(fixture.registrationSucceeded && fixture.queryDuringActivation && fixture.invokeDuringActivation &&
                  fixture.queryCalls == 2u && fixture.invokeCalls == 1u,
              "extension activation registers and routes its owned command/context/handler through typed context methods");

        editor::EditorCommandService* const commands = editor::FindEditorCommandService(host);
        Check(commands != nullptr && commands->GetStatus().registeredCommandCount == 1u &&
                  commands->GetStatus().registeredContextCount == 2u &&
                  commands->GetStatus().registeredHandlerCount == 1u &&
                  commands->GetStatus().activeContextCount == 1u,
              "live shell owner is reflected in the bounded command registry while active");

        Check(host.Shutdown(&failure), "command-owned shell host shuts down cleanly");
        Check(fixture.deactivateCalls == 1u && fixture.cleanupCompleteDuringDeactivate &&
                  fixture.commandUnavailableDuringDeactivate && fixture.activeContextStaleDuringDeactivate,
              "shell revoke and reverse physical cleanup finish before extension deactivate while command service stays running");
        Check(fixture.registrationRejectedDuringDeactivate &&
                  fixture.deactivateRegistrationFailure == editor::EditorCommandFailureCode::WrongPhase,
              "retained activation context cannot mutate command registrations after activation admission closes");
        Check(fixture.queryCalls == 2u && fixture.invokeCalls == 1u && g_uiQuiesceCalls == 1u,
              "teardown reaches no stale command callback and completes before Editor UI quiesces");
    }

    void TestPartialActivationCommandRollback() noexcept
    {
        ResetOwnedRegistrationRecording();
        CommandExtensionFixture fixture;
        fixture.failAfterRegistration = true;
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        const editor::EditorExtensionDescriptor descriptors[]{CommandExtensionDescriptor(42, "failing-command-owner", fixture)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        fixture.host = &host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "partial command-activation fixture compiles");
        Check(!host.Start(&failure), "partial command activation failure rolls back host startup");
        Check(fixture.registrationSucceeded && fixture.deactivateCalls == 1u && fixture.cleanupCompleteDuringDeactivate &&
                  fixture.commandUnavailableDuringDeactivate && fixture.activeContextStaleDuringDeactivate,
              "failed activation synchronously revokes and physically removes every admitted command-domain registration");
        Check(fixture.registrationRejectedDuringDeactivate &&
                  fixture.deactivateRegistrationFailure == editor::EditorCommandFailureCode::WrongPhase &&
                  fixture.queryCalls == 2u && fixture.invokeCalls == 1u,
              "failure rollback preserves private-state teardown while preventing post-revoke registration/callback reachability");
    }

    void TestCrossOwnerDependencyCleanup() noexcept
    {
        ResetOwnedRegistrationRecording();
        CrossOwnerCommandFixture provider;
        provider.role = CrossOwnerRole::Provider;
        CrossOwnerCommandFixture consumer;
        consumer.role = CrossOwnerRole::Consumer;
        const editor::EditorExtensionId consumerDependencies[]{{51u}};

        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        const editor::EditorExtensionDescriptor descriptors[]{
            CrossOwnerDescriptor(51, "provider", provider),
            CrossOwnerDescriptor(52, "consumer", consumer, consumerDependencies)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        provider.host = &host;
        consumer.host = &host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "declared cross-owner dependency fixture compiles");
        Check(host.Start(&failure), "declared provider/consumer command owners activate");
        Check(provider.registered && consumer.registered && consumer.consumerRouted,
              "dependent owner routes through provider-owned command/context state");
        Check(host.Shutdown(&failure),
              "reverse extension dependency teardown removes the consumer before provider physical cleanup");
        Check(consumer.deactivateCalls == 1u && provider.deactivateCalls == 1u &&
                  consumer.cleanupOrderObserved && provider.cleanupOrderObserved,
              "reverse owner cleanup removes dependent handlers before provider context/command and clears provider active receivers last");
        Check(consumer.queryCalls == 1u && g_uiQuiesceCalls == 1u,
              "cross-owner shutdown invokes no stale handler and still completes before Editor UI quiesce");
    }

    void TestFreshHostCommandTokensStayStale() noexcept
    {
        CommandExtensionFixture first;
        editor::EditorCommandToken staleCommand;
        editor::EditorCommandContextTypeToken staleContextType;
        editor::EditorCommandHandlerToken staleHandler;
        editor::EditorCommandContextToken staleActiveContext;
        editor::EditorExtensionOwner staleOwner;

        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        {
            const editor::EditorExtensionDescriptor descriptors[]{CommandExtensionDescriptor(61, "first-command-owner", first)};
            const editor::EditorShellServiceConfig shellConfig{descriptors};
            WorkspaceFactoryData workspaceFactory{&project};
            app::EngineHost host;
            first.host = &host;
            app::HostFailure failure;
            Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
                  "first fresh-token command owner host compiles");
            Check(host.Start(&failure), "first fresh-token command owner host starts");
            staleCommand = first.commandToken;
            staleContextType = first.contextTypeToken;
            staleHandler = first.handlerToken;
            staleActiveContext = first.activeContextToken;
            staleOwner = first.owner;
            Check(host.Shutdown(&failure), "first fresh-token command owner host shuts down");
        }

        CommandExtensionFixture second;
        const editor::EditorExtensionDescriptor descriptors[]{CommandExtensionDescriptor(61, "second-command-owner", second)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        second.host = &host;
        app::HostFailure failure;
        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "second fresh-token command owner host compiles");
        Check(host.Start(&failure), "second fresh-token command owner host starts");
        Check(second.owner.activationGeneration != staleOwner.activationGeneration &&
                  second.commandToken.index == staleCommand.index && second.commandToken.generation != staleCommand.generation &&
                  second.contextTypeToken.index == staleContextType.index && second.contextTypeToken.generation != staleContextType.generation &&
                  second.handlerToken.index == staleHandler.index && second.handlerToken.generation != staleHandler.generation &&
                  second.activeContextToken.index == staleActiveContext.index &&
                  second.activeContextToken.generation != staleActiveContext.generation,
              "fresh shell generation may reuse slots only with fresh owner and domain-token generations");

        editor::EditorCommandService* const commands = editor::FindEditorCommandService(host);
        if (commands != nullptr)
        {
            editor::EditorCommandFailure commandFailure;
            Check(!editor::testing::EditorExtensionRegistrationTestAdapter::UnregisterOwnedHandler(
                      *commands, staleOwner, staleHandler, &commandFailure) &&
                      commandFailure.code == editor::EditorCommandFailureCode::InvalidToken,
                  "stale handler token from the prior host cannot remove a reused current slot");
            Check(!editor::testing::EditorExtensionRegistrationTestAdapter::UnregisterOwnedCommand(
                      *commands, staleOwner, staleCommand, &commandFailure) &&
                      commandFailure.code == editor::EditorCommandFailureCode::InvalidToken,
                  "stale command token from the prior host cannot remove a reused current slot");
            Check(!editor::testing::EditorExtensionRegistrationTestAdapter::UnregisterOwnedContextType(
                      *commands, staleOwner, staleContextType, &commandFailure) &&
                      commandFailure.code == editor::EditorCommandFailureCode::InvalidToken,
                  "stale context-type token from the prior host cannot remove a reused current slot");
            Check(!commands->PopContext(staleActiveContext, &commandFailure) &&
                      commandFailure.code == editor::EditorCommandFailureCode::InvalidToken,
                  "stale active-context token from the prior host cannot pop a reused current slot");
        }
        Check(host.Shutdown(&failure), "second fresh-token command owner host shuts down");
    }

    void TestSharedOwnedLedgerCapacity() noexcept
    {
        ResetOwnedRegistrationRecording();
        MixedOwnedCapacityFixture fixture;
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        const editor::EditorExtensionDescriptor descriptors[]{MixedOwnedCapacityDescriptor(fixture)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        fixture.host = &host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "mixed owned-ledger capacity fixture compiles");
        Check(!host.Start(&failure),
              "shared owned-ledger capacity exhaustion fails the activating extension without partial host success");
        Check(fixture.commandRegisteredCount == editor::MaximumEditorCommands &&
                  fixture.contextRegisteredCount == editor::MaximumEditorCommandContextTypes - 1u &&
                  fixture.panelRegisteredCount == editor::MaximumEditorPanelTypes - 3u &&
                  fixture.surfaceRegisteredCount == 2u && fixture.activityRegisteredCount == 1u &&
                  fixture.preferenceRegisteredCount == 1u &&
                  fixture.handlerReachedSharedLimit &&
                  fixture.extraHandlerRejectedAtSharedLimit,
              "command, context, panel, surface, Activity and typed-preference records fill the exact shared owned-registration ledger and reject overflow");
        Check(fixture.cleanupCompleteDuringDeactivate && g_activityRegisterCalls == 1u &&
                  g_activityRevokeCalls == 1u && g_activityUnregisterCalls == 1u &&
                  g_activityRevokeBeforeUnregister,
              "shared-ledger capacity rollback logically revokes then physically removes the Activity token before final extension deactivation");
    }

    void TestOwnedSurfaceRegistrationAndTeardown() noexcept
    {
        ResetSurfaceLifecycleRecording();
        SurfaceExtensionFixture fixture;
        const editor::EditorExtensionDescriptor descriptors[]{SurfaceExtensionDescriptor(72, "surface-owner", fixture)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        fixture.host = &host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "owned-surface shell host compiles");
        Check(host.Start(&failure), "owned-surface shell host starts");
        auto* const ui = static_cast<FixtureUiService*>(host.FindCapability(editor::EditorUiCapabilityId));
        Check(ui != nullptr && fixture.registrationSucceeded && fixture.missingCommandRejected &&
                  fixture.missingCommandFailure == editor::EditorUiFailureCode::SurfaceCommandFailure,
              "extension activation registers owned surface entries and rejects a missing command before topology mutation");
        if (ui != nullptr)
        {
            const editor::detail::EditorSurfaceRegistryStatus status = ui->GetSurfaceStatus();
            editor::detail::EditorUiSurfaceToken resolved;
            editor::EditorExtensionOwner owner;
            editor::detail::EditorUiSurfaceKind kind = editor::detail::EditorUiSurfaceKind::MainMenu;
            Check(status.surfaceCount == 3u && status.contributionCount == 20u &&
                      ui->ResolveSurface(fixture.surface, resolved, owner, kind) &&
                      resolved == fixture.surfaceToken && owner == fixture.owner &&
                      kind == editor::detail::EditorUiSurfaceKind::ContextMenu,
                  "owned context root plus same-owner command entries are reachable through the one production surface core");

            struct LayoutMenuObservation final
            {
                bool submenu = false;
                bool command[8]{};
            } layoutMenu;
            const auto observeLayoutMenu = [](const editor::detail::EditorUiContributionView& contribution,
                                              void* const userData) noexcept
            {
                auto& observation = *static_cast<LayoutMenuObservation*>(userData);
                if (contribution.id == editor::detail::EditorMainMenuLayoutSubmenuId)
                {
                    observation.submenu = contribution.kind == editor::detail::EditorUiContributionKind::Submenu &&
                                          contribution.parent == editor::detail::EditorMainMenuWindowAnchorId;
                    return;
                }
                if (contribution.parent != editor::detail::EditorMainMenuLayoutSubmenuId ||
                    contribution.kind != editor::detail::EditorUiContributionKind::Command)
                    return;
                if (contribution.command == editor::detail::SaveCurrentLayoutAsCommandId) observation.command[0] = true;
                else if (contribution.command == editor::detail::SaveLayoutCommandId) observation.command[1] = true;
                else if (contribution.command == editor::detail::SwitchLayoutCommandId) observation.command[2] = true;
                else if (contribution.command == editor::detail::RevertLayoutCommandId) observation.command[3] = true;
                else if (contribution.command == editor::detail::RenameLayoutCommandId) observation.command[4] = true;
                else if (contribution.command == editor::detail::DuplicateLayoutCommandId) observation.command[5] = true;
                else if (contribution.command == editor::detail::DeleteLayoutCommandId) observation.command[6] = true;
                else if (contribution.command == editor::detail::ResetDefaultLayoutCommandId) observation.command[7] = true;
            };
            ui->VisitSurfaceContributions(editor::detail::EditorMainMenuSurfaceId,
                                          observeLayoutMenu, &layoutMenu);
            bool allLayoutCommands = layoutMenu.submenu;
            for (const bool present : layoutMenu.command)
                allLayoutCommands = allLayoutCommands && present;
            Check(allLayoutCommands,
                  "Window / Layout exposes every required P0 personal-layout command through the production surface core");
        }

        Check(host.Shutdown(&failure), "owned-surface shell host shuts down cleanly");
        Check(fixture.deactivateCalls == 1u && fixture.commandUnavailableDuringDeactivate &&
                  fixture.registrationRejectedDuringDeactivate &&
                  fixture.deactivateRegistrationFailure == editor::EditorUiFailureCode::WrongPhase &&
                  !fixture.privateStateAlive,
              "surface cleanup and command cleanup finish before extension deactivate and retained contexts cannot re-register");
        const u32 expected[]{100u, 200u, 200u, 200u, 300u};
        bool ordered = g_surfaceLifecycleEventCount == static_cast<u32>(sizeof(expected) / sizeof(expected[0]));
        if (ordered)
            for (u32 index = 0; index < g_surfaceLifecycleEventCount; ++index)
                ordered = ordered && g_surfaceLifecycleEvents[index] == expected[index];
        Check(g_surfaceRevokeReducedReachableTopology && ordered,
              "surface owner revoke makes topology unreachable before reverse contribution/root physical cleanup");
        Check(g_uiQuiesceCalls == 1u,
              "owned surface teardown completes before Editor UI quiesces");
    }

    void TestPartialActivationSurfaceRollback() noexcept
    {
        ResetSurfaceLifecycleRecording();
        SurfaceExtensionFixture fixture;
        fixture.failAfterRegistration = true;
        const editor::EditorExtensionDescriptor descriptors[]{SurfaceExtensionDescriptor(73, "failing-surface-owner", fixture)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        fixture.host = &host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "partial surface-activation fixture compiles");
        Check(!host.Start(&failure),
              "activation failure after surface registration rolls back host startup");
        Check(fixture.registrationSucceeded && fixture.deactivateCalls == 1u &&
                  fixture.commandUnavailableDuringDeactivate && fixture.registrationRejectedDuringDeactivate &&
                  fixture.deactivateRegistrationFailure == editor::EditorUiFailureCode::WrongPhase &&
                  g_surfaceRevokeReducedReachableTopology,
              "failed activation revokes and physically removes owned surface/command records before extension deactivation");
        const u32 expected[]{100u, 200u, 200u, 200u, 300u};
        bool ordered = g_surfaceLifecycleEventCount == static_cast<u32>(sizeof(expected) / sizeof(expected[0]));
        if (ordered)
            for (u32 index = 0; index < g_surfaceLifecycleEventCount; ++index)
                ordered = ordered && g_surfaceLifecycleEvents[index] == expected[index];
        Check(ordered, "partial surface rollback uses exact reverse contribution/root cleanup after logical revoke");
    }

    void TestCrossOwnerSurfaceReferences() noexcept
    {
        ResetSurfaceLifecycleRecording();
        CrossOwnerSurfaceFixture provider;
        provider.role = CrossOwnerSurfaceRole::Provider;
        CrossOwnerSurfaceFixture consumer;
        consumer.role = CrossOwnerSurfaceRole::Consumer;
        const editor::EditorExtensionId consumerDependencies[]{{74u}};
        const editor::EditorExtensionDescriptor descriptors[]{
            CrossOwnerSurfaceDescriptor(74, "surface-provider", provider),
            CrossOwnerSurfaceDescriptor(75, "surface-consumer", consumer, consumerDependencies)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "cross-owner surface-reference fixture compiles");
        Check(host.Start(&failure), "cross-owner surface-reference fixture starts");
        auto* const ui = static_cast<FixtureUiService*>(host.FindCapability(editor::EditorUiCapabilityId));
        Check(provider.providerRegistered && consumer.crossCommandRejected &&
                  consumer.crossCommandFailure == editor::EditorUiFailureCode::SurfaceCommandFailure &&
                  consumer.privateSurfaceRejected &&
                  consumer.privateSurfaceFailure == editor::EditorUiFailureCode::OwnerMismatch &&
                  consumer.privateAnchorRejected &&
                  consumer.privateAnchorFailure == editor::EditorUiFailureCode::OwnerMismatch,
              "surface registration rejects cross-owner command references, private roots and private anchors");
        if (ui != nullptr)
        {
            const editor::detail::EditorSurfaceRegistryStatus status = ui->GetSurfaceStatus();
            Check(status.surfaceCount == 3u && status.contributionCount == 18u,
                  "cross-owner rejection leaves no partial consumer topology while shell-owned anchors remain usable");
        }
        Check(host.Shutdown(&failure), "cross-owner surface-reference fixture shuts down cleanly");
    }

    void TestOwnedPanelShutdownAndReverseTeardown() noexcept
    {
        ResetPanelLifecycleRecording();
        PanelExtensionFixture provider;
        PanelExtensionFixture consumer;
        const editor::EditorExtensionId consumerDependencies[]{{81u}};
        const editor::EditorExtensionDescriptor descriptors[]{
            PanelExtensionDescriptor(81, "panel-provider", provider),
            PanelExtensionDescriptor(82, "panel-consumer", consumer, consumerDependencies)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        provider.host = &host;
        consumer.host = &host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "owned-panel reverse-teardown host compiles");
        Check(host.Start(&failure), "two owned panel extensions activate");
        Check(provider.registrationSucceeded && provider.instanceCreated && provider.reachableDuringActivation &&
                  provider.unownedRemovalRejected && consumer.registrationSucceeded && consumer.instanceCreated &&
                  consumer.reachableDuringActivation && consumer.unownedRemovalRejected,
              "both owners register exact panel types and live instances while public physical removal stays blocked");

        Check(host.Shutdown(&failure), "owned-panel host shuts down cleanly");
        Check(provider.destroyCalls == 1u && consumer.destroyCalls == 1u &&
                  provider.deactivateCalls == 1u && consumer.deactivateCalls == 1u &&
                  provider.cleanupCompleteDuringDeactivate && consumer.cleanupCompleteDuringDeactivate,
              "reverse teardown destroys each owned instance and exact type before extension deactivate");
        Check(provider.destroySawPrivateStateAlive && consumer.destroySawPrivateStateAlive &&
                  provider.destroySawUiReady && consumer.destroySawUiReady &&
                  provider.registrationRejectedDuringDeactivate && consumer.registrationRejectedDuringDeactivate &&
                  provider.deactivateRegistrationFailure == editor::EditorUiFailureCode::WrongPhase &&
                  consumer.deactivateRegistrationFailure == editor::EditorUiFailureCode::WrongPhase &&
                  provider.visibilityRejectedDuringDeactivate && consumer.visibilityRejectedDuringDeactivate &&
                  provider.deactivateVisibilityFailure == editor::EditorUiFailureCode::OwnerRevoked &&
                  consumer.deactivateVisibilityFailure == editor::EditorUiFailureCode::OwnerRevoked,
              "cleanup runs while private/UI state is alive and retained owner-scoped panel access cannot mutate after logical revoke");
        CheckPanelReverseEvents(
            "declared dependency tears down consumer panel state before provider panel state in exact reverse owner order");
        Check(g_uiQuiesceCalls == 1u,
              "panel cleanup and extension deactivation complete before Editor UI quiesces");
    }

    void TestPartialActivationPanelRollback() noexcept
    {
        ResetPanelLifecycleRecording();
        PanelExtensionFixture fixture;
        fixture.failAfterRegistration = true;
        const editor::EditorExtensionDescriptor descriptors[]{PanelExtensionDescriptor(83, "failing-panel-owner", fixture)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        fixture.host = &host;
        app::HostFailure failure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
              "partial panel-activation fixture compiles");
        Check(!host.Start(&failure), "activation failure after panel creation rolls back host startup");
        Check(fixture.invalidPersistenceRejected &&
                  fixture.invalidPersistenceFailure == editor::EditorUiFailureCode::InvalidDescriptor &&
                  fixture.registrationSucceeded && fixture.instanceCreated && fixture.destroyCalls == 1u &&
                  fixture.deactivateCalls == 1u && fixture.cleanupCompleteDuringDeactivate,
              "invalid persistence registration stays out of the owner ledger and failed activation synchronously destroys only its admitted owned instance/type before deactivate");
        Check(fixture.destroySawPrivateStateAlive && fixture.destroySawUiReady &&
                  fixture.registrationRejectedDuringDeactivate &&
                  fixture.deactivateRegistrationFailure == editor::EditorUiFailureCode::WrongPhase &&
                  fixture.visibilityRejectedDuringDeactivate &&
                  fixture.deactivateVisibilityFailure == editor::EditorUiFailureCode::OwnerRevoked &&
                  !fixture.privateStateAlive,
              "failed activation revokes retained panel access, leaves no reachable callback and releases private state only after physical cleanup");
    }

    void TestPanelFocusBridgeAndLifo() noexcept
    {
        PanelFocusBridgeFixture fixture;
        const editor::EditorExtensionDescriptor descriptors[]{PanelFocusDescriptor(fixture)};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        fixture.host = &host;
        app::HostFailure hostFailure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, hostFailure),
              "focused-panel bridge host compiles");
        Check(host.Start(&hostFailure), "focused-panel bridge host starts");
        auto* const ui = static_cast<FixtureUiService*>(host.FindCapability(editor::EditorUiCapabilityId));
        editor::EditorCommandService* const commands = editor::FindEditorCommandService(host);
        Check(ui != nullptr && commands != nullptr && fixture.registered && fixture.panelCreated && fixture.secondPanelCreated,
              "focused-panel extension registered its command Local context and both panel associations");
        if (ui == nullptr || commands == nullptr)
            return;

        editor::EditorUiFailure uiFailure;
        editor::detail::EditorPanelFocusUpdate focused;
        focused.focus = {1u, fixture.panel, fixture.panelType, fixture.panelInstance, {}};
        focused.receiverData = &fixture.panelStateA;
        Check(ui->PublishPanelFocus(focused, &uiFailure),
              "same-frame focus publication activates the associated panel Local context");
        editor::EditorCommandState state;
        editor::EditorCommandFailure commandFailure;
        Check(commands->Query(fixture.command, state, &commandFailure) &&
                  state.kind == editor::EditorCommandStateKind::Enabled && fixture.queryCalls == 1u &&
                  fixture.lastReceiverData == &fixture.panelStateA,
              "focused Local context routes the command to panel A's live receiver");

        editor::detail::EditorPanelFocusUpdate focusedSecond;
        focusedSecond.focus = {2u, fixture.secondPanel, fixture.secondPanelType, fixture.secondPanelInstance, {}};
        focusedSecond.receiverData = &fixture.panelStateB;
        WrongThreadPanelFocusWorker wrongThreadFocus(*ui, focusedSecond);
        wrongThreadFocus.InitThread();
        wrongThreadFocus.JoinThread();
        Check(!wrongThreadFocus.Published() &&
                  wrongThreadFocus.Failure().code == editor::EditorUiFailureCode::FocusSynchronizationFailure &&
                  ui->GetPanelFocus().panel == fixture.panel && commands->GetStatus().activeContextCount == 1u,
              "worker-thread focus publication is rejected without changing focused panel or Local context state");
        Check(commands->Query(fixture.command, state, &commandFailure) && fixture.queryCalls == 2u &&
                  fixture.lastReceiverData == &fixture.panelStateA,
              "wrong-thread rejection leaves panel A as the active command receiver");

        Check(ui->PublishPanelFocus(focusedSecond, &uiFailure) &&
                  ui->GetPanelFocus().panel == fixture.secondPanel && commands->GetStatus().activeContextCount == 1u,
              "same-frame panel A to panel B focus switch replaces the Local context atomically");
        Check(commands->Query(fixture.command, state, &commandFailure) && fixture.queryCalls == 3u &&
                  fixture.lastReceiverData == &fixture.panelStateB,
              "panel A to panel B focus switch routes immediately to panel B's receiver");

        editor::EditorCommandContextToken controlToken;
        Check(commands->PushContext(fixture.controlContext, &fixture, editor::EditorCommandContextMode::Inherit,
                                    controlToken, &commandFailure),
              "fixture pushes a ControlTool context above the focused panel Local context");
        focused.focus.frame = 3u;
        Check(!ui->PublishPanelFocus(focused, &uiFailure) &&
                  uiFailure.code == editor::EditorUiFailureCode::FocusSynchronizationFailure &&
                  ui->GetPanelFocus().panel == fixture.secondPanel &&
                  commands->GetStatus().activeContextCount == 2u,
              "panel B to panel A focus switch fails closed while a ControlTool token sits above the panel Local token");
        Check(commands->Query(fixture.command, state, &commandFailure) && fixture.queryCalls == 4u &&
                  fixture.lastReceiverData == &fixture.panelStateB,
              "rejected focus switch keeps panel B's receiver reachable and does not install a stale panel A receiver");
        Check(commands->PopContext(controlToken, &commandFailure) &&
                  ui->PublishPanelFocus(focused, &uiFailure) &&
                  ui->GetPanelFocus().panel == fixture.panel && commands->GetStatus().activeContextCount == 1u,
              "panel B to panel A focus switch succeeds after the ControlTool releases strict-LIFO ownership");
        Check(commands->Query(fixture.command, state, &commandFailure) && fixture.queryCalls == 5u &&
                  fixture.lastReceiverData == &fixture.panelStateA,
              "successful retry routes the command to panel A's receiver");

        editor::detail::EditorPanelFocusUpdate cleared;
        cleared.focus.frame = 4u;
        Check(ui->PublishPanelFocus(cleared, &uiFailure) && !ui->GetPanelFocus().HasFocus() &&
                  commands->GetStatus().activeContextCount == 0u,
              "focus clearing removes the panel Local context after strict-LIFO ownership is available");
        focusedSecond.focus.frame = 5u;
        Check(ui->PublishPanelFocus(focusedSecond, &uiFailure) && commands->GetStatus().activeContextCount == 1u,
              "panel Local context can be restored for lifecycle cleanup proof");
        Check(host.Shutdown(&hostFailure), "focused-panel bridge host shuts down cleanly");
        Check(!fixture.privateStateAlive && fixture.deactivateSawNoActiveContext,
              "shell quiesce removes panel Local receiver and focus sink before extension/UI teardown");
    }

    void TestCrossOwnerPanelLocalContextAssociation() noexcept
    {
        CrossOwnerPanelContextFixture fixture;
        editor::EditorExtensionDescriptor descriptors[2];
        MakeCrossOwnerPanelDescriptors(fixture, descriptors);
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure hostFailure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, hostFailure),
              "cross-owner panel-context host compiles");
        Check(host.Start(&hostFailure), "cross-owner panel-context host starts");
        auto* const ui = static_cast<FixtureUiService*>(host.FindCapability(editor::EditorUiCapabilityId));
        Check(ui != nullptr && fixture.providerRegistered && fixture.crossOwnerCreateRejected &&
                  fixture.crossOwnerCreateFailure == editor::EditorUiFailureCode::OwnerMismatch &&
                  fixture.crossOwnerVisibilityRejected &&
                  fixture.crossOwnerVisibilityFailure == editor::EditorUiFailureCode::OwnerMismatch &&
                  fixture.consumerRejected &&
                  fixture.consumerFailure == editor::EditorUiFailureCode::InvalidContextAssociation,
              "owner-scoped panel access rejects another owner's type/handle and panel registration rejects another owner's Local context");
        if (ui != nullptr)
        {
            editor::EditorPanelHandle rejected;
            editor::EditorUiFailure failure;
            Check(!ui->GetPanels().CreatePanel({0xb423u, fixture.consumerPanelType, "rejected cross-owner panel", true},
                                               rejected, &failure) &&
                      !rejected.IsValid() && failure.code == editor::EditorUiFailureCode::InvalidDescriptor,
                  "cross-owner Local-context rejection leaves no partial panel type behind");
        }
        Check(host.Shutdown(&hostFailure), "cross-owner panel-context host shuts down cleanly");
    }

    void TestTypedPreferencePersistence() noexcept
    {
        g_preferenceBool = true;
        g_preferenceSigned = 7;
        g_preferenceRevision = 1;
        g_preferenceValidateCalls = 0;
        g_preferenceApplyCalls = 0;
        g_preferenceFailSecondCapture = false;
        g_preferenceProviderToken = {};
        g_preferenceDeactivateSawRevokedProvider = false;

        const editor::EditorExtensionDescriptor descriptors[]{PreferenceFixtureDescriptor()};
        const editor::EditorShellServiceConfig shellConfig{descriptors};
        vanguard::projects::ProjectDescriptor project;
        project.technicalName.Set("fixture-project");
        WorkspaceFactoryData workspaceFactory{&project};
        app::EngineHost host;
        app::HostFailure hostFailure;

        Check(PrepareShellHost(host, workspaceFactory, shellConfig, hostFailure),
              "typed-preference fixture host compiles");
        Check(host.Start(&hostFailure), "typed-preference fixture host starts");
        editor::EditorShellService* const shell = editor::FindEditorShellService(host);
        Check(shell != nullptr && g_preferenceProviderToken.IsValid(),
              "typed-preference extension registers one owner-scoped provider");
        if (shell == nullptr)
            return;

        auto& persistence =
            static_cast<editor::detail::EditorShellPersistencePrivateService&>(*shell);
        editor::EditorPreferenceFailure preferenceFailure;
        const u64 initialGlobalRevision = persistence.GetTypedPreferenceRevision();
        Check(initialGlobalRevision != 0 &&
                  persistence.RefreshTypedPreferenceRevision(&preferenceFailure) &&
                  persistence.GetTypedPreferenceRevision() == initialGlobalRevision + 1u,
              "first typed-preference refresh installs provider baselines and coalesces provider-set admission into one global revision");

        editor::EditorTypedPreferenceRecord captured[2]{};
        captured[0].extension = {0xdeadbeefu};
        u32 capturedCount = 0;
        Check(!persistence.CaptureTypedPreferences(captured, 1, capturedCount, &preferenceFailure) &&
                  preferenceFailure.code == editor::EditorPreferenceFailureCode::CapacityExceeded &&
                  capturedCount == 2 && captured[0].extension.value == 0xdeadbeefu,
              "typed-preference capture is all-or-nothing and reports the exact required field count");
        Check(persistence.CaptureTypedPreferences(captured, 2, capturedCount, &preferenceFailure) &&
                  capturedCount == 2 && captured[0].extension.value == 0xe600u &&
                  captured[0].providerId == PreferenceFixtureProviderId &&
                  captured[0].fieldId == PreferenceFixtureBoolFieldId &&
                  captured[0].value.type == editor::EditorPreferenceValueType::Bool &&
                  captured[0].value.value.boolean &&
                  captured[1].fieldId == PreferenceFixtureSignedFieldId &&
                  captured[1].value.type == editor::EditorPreferenceValueType::SignedInteger &&
                  captured[1].value.value.signedInteger == 7,
              "typed-preference capture uses copied descriptors and emits canonical stable-key order after activation-local descriptors expire");

        g_preferenceFailSecondCapture = true;
        editor::EditorTypedPreferenceRecord untouched[2]{};
        untouched[0].extension = {0xcafef00du};
        Check(!persistence.CaptureTypedPreferences(untouched, 2, capturedCount, &preferenceFailure) &&
                  preferenceFailure.code == editor::EditorPreferenceFailureCode::CallbackFailure &&
                  capturedCount == 2 && untouched[0].extension.value == 0xcafef00du,
              "provider capture callback failure copies no partial preference prefix to the caller");
        g_preferenceFailSecondCapture = false;

        editor::EditorTypedPreferenceRecord restored[2]{};
        restored[0].extension = {0xe600u};
        restored[0].providerId = PreferenceFixtureProviderId;
        restored[0].fieldId = PreferenceFixtureBoolFieldId;
        restored[0].value.type = editor::EditorPreferenceValueType::Bool;
        restored[0].value.value.boolean = false;
        restored[1].extension = {0xe600u};
        restored[1].providerId = PreferenceFixtureProviderId;
        restored[1].fieldId = PreferenceFixtureSignedFieldId;
        restored[1].value.type = editor::EditorPreferenceValueType::SignedInteger;
        restored[1].value.value.signedInteger = 99;
        editor::EditorTypedPreferenceItemResult results[2]{};
        const u32 appliesBeforeRejectedPreflight = g_preferenceApplyCalls;
        Check(!persistence.ValidateTypedPreferences({restored, 2}, {results, 2}, &preferenceFailure) &&
                  preferenceFailure.code == editor::EditorPreferenceFailureCode::ValidationFailed &&
                  results[0] == editor::EditorTypedPreferenceItemResult::Valid &&
                  results[1] == editor::EditorTypedPreferenceItemResult::Invalid &&
                  g_preferenceApplyCalls == appliesBeforeRejectedPreflight,
              "typed-preference semantic validation rejects one live invalid value without invoking apply");

        restored[1].value.value.signedInteger = -3;
        const u32 validatesBeforeAcceptedPreflight = g_preferenceValidateCalls;
        Check(persistence.ValidateTypedPreferences({restored, 2}, {results, 2}, &preferenceFailure) &&
                  g_preferenceValidateCalls == validatesBeforeAcceptedPreflight + 2u &&
                  g_preferenceApplyCalls == appliesBeforeRejectedPreflight,
              "accepted typed-preference preflight invokes validate for every live field and still performs zero apply mutation");
        Check(persistence.ApplyTypedPreferences({restored, 2}, {results, 2}, &preferenceFailure) &&
                  results[0] == editor::EditorTypedPreferenceItemResult::Valid &&
                  results[1] == editor::EditorTypedPreferenceItemResult::Valid &&
                  g_preferenceBool == false && g_preferenceSigned == -3 &&
                  g_preferenceApplyCalls == appliesBeforeRejectedPreflight + 2u,
              "typed-preference apply commits the exact immediately validated transaction");

        const u64 beforeChangedRefresh = persistence.GetTypedPreferenceRevision();
        Check(persistence.RefreshTypedPreferenceRevision(&preferenceFailure) &&
                  persistence.GetTypedPreferenceRevision() == beforeChangedRefresh + 1u,
              "multiple durable provider field changes coalesce into one Shell typed-preference revision increment");
        const u64 stableGlobalRevision = persistence.GetTypedPreferenceRevision();
        Check(persistence.RefreshTypedPreferenceRevision(&preferenceFailure) &&
                  persistence.GetTypedPreferenceRevision() == stableGlobalRevision,
              "unchanged provider revision leaves the global typed-preference revision stable");

        Check(persistence.ValidateTypedPreferences({restored, 2}, {results, 2}, &preferenceFailure),
              "transaction-mismatch fixture validates a fresh exact transaction");
        editor::EditorTypedPreferenceRecord mutated[2]{restored[0], restored[1]};
        mutated[0].value.value.boolean = true;
        const u32 appliesBeforeMismatch = g_preferenceApplyCalls;
        Check(!persistence.ApplyTypedPreferences({mutated, 2}, {results, 2}, &preferenceFailure) &&
                  preferenceFailure.code == editor::EditorPreferenceFailureCode::TransactionMismatch &&
                  g_preferenceApplyCalls == appliesBeforeMismatch && !g_preferenceBool,
              "typed-preference apply rejects record mutation after validation with zero callback mutation");

        editor::EditorTypedPreferenceRecord missing[2]{};
        missing[0].extension = {0xe600u};
        missing[0].providerId = PreferenceFixtureProviderId;
        missing[0].fieldId = 0xe6ffu;
        missing[0].value.type = editor::EditorPreferenceValueType::Bool;
        missing[1].extension = {0xe600u};
        missing[1].providerId = 0xe6feu;
        missing[1].fieldId = 0xe6fdu;
        missing[1].value.type = editor::EditorPreferenceValueType::SignedInteger;
        const u32 validatesBeforeMissing = g_preferenceValidateCalls;
        const u32 appliesBeforeMissing = g_preferenceApplyCalls;
        Check(persistence.ValidateTypedPreferences({missing, 2}, {results, 2}, &preferenceFailure) &&
                  results[0] == editor::EditorTypedPreferenceItemResult::MissingField &&
                  results[1] == editor::EditorTypedPreferenceItemResult::MissingProvider &&
                  g_preferenceValidateCalls == validatesBeforeMissing &&
                  persistence.ApplyTypedPreferences({missing, 2}, {results, 2}, &preferenceFailure) &&
                  g_preferenceApplyCalls == appliesBeforeMissing,
              "missing exact provider/field keys remain callback-free typed-preference orphans through validate/apply");

        const u64 beforeInvalidOwnerRevision = persistence.GetTypedPreferenceRevision();
        g_preferenceRevision = 0;
        const u32 appliesBeforeInvalidRevisionPreflight = g_preferenceApplyCalls;
        Check(persistence.ValidateTypedPreferences({restored, 2}, {results, 2}, &preferenceFailure) &&
                  results[0] == editor::EditorTypedPreferenceItemResult::Valid &&
                  results[1] == editor::EditorTypedPreferenceItemResult::Valid &&
                  g_preferenceApplyCalls == appliesBeforeInvalidRevisionPreflight,
              "provider revision zero does not turn a semantically valid restored value set into a corrupt-file preflight failure");
        Check(!persistence.RefreshTypedPreferenceRevision(&preferenceFailure) &&
                  preferenceFailure.code == editor::EditorPreferenceFailureCode::CallbackFailure &&
                  persistence.GetTypedPreferenceRevision() == beforeInvalidOwnerRevision,
              "provider revision zero is isolated to revision refresh/write-inhibit handling without advancing the Shell global preference revision");

        Check(host.Shutdown(&hostFailure), "typed-preference fixture host shuts down cleanly");
        Check(g_preferenceDeactivateSawRevokedProvider,
              "provider token is logically unreachable and physically removed before extension deactivation/private-state teardown");
    }

    void TestPreferenceProviderBounds() noexcept
    {
        {
            PreferenceCapacityFixture fixture;
            fixture.mode = PreferenceCapacityMode::Providers;
            const editor::EditorExtensionDescriptor descriptors[]{
                PreferenceCapacityExtension(fixture, 0xe700u, "preference-provider-capacity")};
            const editor::EditorShellServiceConfig shellConfig{descriptors};
            vanguard::projects::ProjectDescriptor project;
            project.technicalName.Set("fixture-project");
            WorkspaceFactoryData workspaceFactory{&project};
            app::EngineHost host;
            app::HostFailure failure;
            Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
                  "preference-provider capacity fixture compiles");
            Check(host.Start(&failure), "preference-provider capacity fixture starts");
            Check(fixture.registeredProviders == editor::MaximumEditorPreferenceProviders &&
                      fixture.registeredFields == editor::MaximumEditorPreferenceProviders &&
                      fixture.duplicateProviderRejected && fixture.overflowRejected,
                  "typed preference registry admits exactly 256 providers, rejects duplicate identity atomically, and rejects provider +1 without a token");
            Check(host.Shutdown(&failure), "preference-provider capacity fixture shuts down cleanly");
        }

        {
            PreferenceCapacityFixture fixture;
            fixture.mode = PreferenceCapacityMode::Fields;
            const editor::EditorExtensionDescriptor descriptors[]{
                PreferenceCapacityExtension(fixture, 0xe701u, "preference-field-capacity")};
            const editor::EditorShellServiceConfig shellConfig{descriptors};
            vanguard::projects::ProjectDescriptor project;
            project.technicalName.Set("fixture-project");
            WorkspaceFactoryData workspaceFactory{&project};
            app::EngineHost host;
            app::HostFailure failure;
            Check(PrepareShellHost(host, workspaceFactory, shellConfig, failure),
                  "preference-field capacity fixture compiles");
            Check(host.Start(&failure), "preference-field capacity fixture starts");
            Check(fixture.invalidDescriptorMatrixRejected &&
                      fixture.registeredFields == editor::MaximumEditorLivePreferenceFields &&
                      fixture.registeredProviders ==
                          editor::MaximumEditorLivePreferenceFields /
                              editor::MaximumEditorPreferenceFieldsPerProvider &&
                      fixture.overflowRejected,
                  "typed preference registration rejects invalid/33-field descriptors, admits exact 32-field providers through 512 live fields, and rejects field +1 atomically");
            Check(host.Shutdown(&failure), "preference-field capacity fixture shuts down cleanly");
        }
    }

    void TestActivityPreferencePolicy() noexcept
    {
        using namespace editor::detail;
        Check(ActivityPresentationExtensionId.value == 0x6564333561637401ull &&
                  ActivityPreferenceProviderId == 0x6564333570726601ull &&
                  ActivityLevelsPreferenceFieldId == 0x656433356c766c01ull &&
                  ActivityCategoryPreferenceFieldId == 0x6564333563617401ull &&
                  ActivityTextPreferenceFieldId == 0x6564333574787401ull,
              "Activity presentation preference provider and field stable identities match the frozen E3.6 contract");

        bool levels[ActivityPreferenceLevelCount]{true, true, true, true, true, true};
        vanguard::i32 category = -1;
        char text[ActivityPreferenceTextBytes]{};
        vanguard::u64 revision = 1;
        editor::EditorPreferenceFieldValue value;
        Check(CaptureActivityPreferenceValue(levels, category, text, ActivityLevelsPreferenceFieldId, value) &&
                  value.type == editor::EditorPreferenceValueType::UnsignedBitMask &&
                  value.value.unsignedBitMask == 0x3fu,
              "Activity level capture maps exactly six visible levels to the low-six-bit durable mask");
        Check(ValidateActivityPreferenceValue(ActivityLevelsPreferenceFieldId, value,
                                              static_cast<u32>(vanguard::diagnostics::Category::Count)),
              "Activity level mask accepts exact low-six-bit maximum");
        value.value.unsignedBitMask = 0x40u;
        Check(!ValidateActivityPreferenceValue(ActivityLevelsPreferenceFieldId, value,
                                               static_cast<u32>(vanguard::diagnostics::Category::Count)),
              "Activity level mask rejects bit six as exact-bound +1 evidence");

        value = {};
        value.type = editor::EditorPreferenceValueType::SignedInteger;
        value.value.signedInteger = -1;
        Check(ValidateActivityPreferenceValue(ActivityCategoryPreferenceFieldId, value,
                                              static_cast<u32>(vanguard::diagnostics::Category::Count)),
              "Activity category accepts the all-categories sentinel");
        value.value.signedInteger = static_cast<vanguard::i64>(vanguard::diagnostics::Category::Count) - 1;
        Check(ValidateActivityPreferenceValue(ActivityCategoryPreferenceFieldId, value,
                                              static_cast<u32>(vanguard::diagnostics::Category::Count)),
              "Activity category accepts the exact final current diagnostics category");
        value.value.signedInteger = static_cast<vanguard::i64>(vanguard::diagnostics::Category::Count);
        Check(!ValidateActivityPreferenceValue(ActivityCategoryPreferenceFieldId, value,
                                               static_cast<u32>(vanguard::diagnostics::Category::Count)),
              "Activity category rejects the current category-count boundary");

        for (u32 index = 0; index < ActivityPreferenceTextBytes - 1u; ++index)
            text[index] = 'x';
        text[ActivityPreferenceTextBytes - 1u] = '\0';
        Check(CaptureActivityPreferenceValue(levels, category, text, ActivityTextPreferenceFieldId, value) &&
                  value.type == editor::EditorPreferenceValueType::Utf8String &&
                  value.value.string.byteCount == ActivityPreferenceTextBytes - 1u &&
                  value.value.string.bytes[ActivityPreferenceTextBytes - 1u] == '\0' &&
                  ValidateActivityPreferenceValue(ActivityTextPreferenceFieldId, value,
                                                  static_cast<u32>(vanguard::diagnostics::Category::Count)),
              "Activity text capture and validation accept exactly 127 UTF-8 payload bytes with local terminator");
        value.value.string.byteCount = ActivityPreferenceTextBytes;
        Check(!ValidateActivityPreferenceValue(ActivityTextPreferenceFieldId, value,
                                               static_cast<u32>(vanguard::diagnostics::Category::Count)),
              "Activity text validation rejects 128 payload bytes as exact-bound +1 evidence");

        value = {};
        value.type = editor::EditorPreferenceValueType::UnsignedBitMask;
        value.value.unsignedBitMask = 0x3fu;
        ApplyActivityPreferenceValue(levels, category, text, revision, ActivityLevelsPreferenceFieldId, value);
        Check(revision == 1,
              "Activity provider apply leaves owner revision unchanged when the durable level value is identical");
        value.value.unsignedBitMask = 0x1fu;
        ApplyActivityPreferenceValue(levels, category, text, revision, ActivityLevelsPreferenceFieldId, value);
        Check(revision == 2 && !levels[5],
              "Activity provider apply advances owner revision exactly once for a real durable level change");

        value = {};
        value.type = editor::EditorPreferenceValueType::Utf8String;
        value.value.string.byteCount = 3;
        std::memcpy(value.value.string.bytes, "new", 3);
        value.value.string.bytes[3] = '\0';
        const u64 beforeTextChange = revision;
        ApplyActivityPreferenceValue(levels, category, text, revision, ActivityTextPreferenceFieldId, value);
        Check(revision == beforeTextChange + 1u && std::strcmp(text, "new") == 0,
              "Activity provider text apply advances owner revision once for a changed durable filter");
        const u64 afterTextChange = revision;
        ApplyActivityPreferenceValue(levels, category, text, revision, ActivityTextPreferenceFieldId, value);
        Check(revision == afterTextChange,
              "Activity provider text reapply is a revision no-op for an identical durable value");

        revision = ~u64{0};
        value.value.string.byteCount = 4;
        std::memcpy(value.value.string.bytes, "next", 4);
        value.value.string.bytes[4] = '\0';
        ApplyActivityPreferenceValue(levels, category, text, revision, ActivityTextPreferenceFieldId, value);
        Check(revision == 0,
              "Activity owner revision exhausts to zero instead of wrapping on a real durable change");
    }

    void TestConsolePersistencePolicy() noexcept
    {
        using namespace editor::detail;
        Check(ConsolePanelStateVersion == 1u && ConsoleContentStateMagic == 0x31534e43u,
              "Console panel persistence begins with a frozen v1 CNS1 content wire");

        ConsoleContentPersistenceState state;
        state.levels[1] = false;
        state.levels[4] = false;
        state.category = static_cast<vanguard::i32>(diagnostics::Category::Rendering);
        std::memcpy(state.text, "shader", 7u);
        state.scrollLock = false;

        vanguard::u8 bytes[editor::MaximumEditorPanelStateBytes]{};
        vanguard::u32 byteCount = 0u;
        Check(CaptureConsoleContentPersistence(
                  state, static_cast<u32>(diagnostics::Category::Count), bytes, sizeof(bytes), byteCount) ==
                  editor::EditorPanelStateResult::Success && byteCount == 17u,
              "Console content persistence emits the compact bounded v1 wire");

        ConsoleContentPersistenceState decoded;
        Check(DecodeConsoleContentPersistence(
                  {bytes, byteCount}, static_cast<u32>(diagnostics::Category::Count), decoded) &&
                  decoded.levels[0] && !decoded.levels[1] && decoded.levels[2] && decoded.levels[3] &&
                  !decoded.levels[4] && decoded.levels[5] && decoded.category == state.category &&
                  std::strcmp(decoded.text, "shader") == 0 && !decoded.scrollLock,
              "Console content persistence roundtrips log-level toggles, category/text filters and scroll lock");

        vanguard::u8 malformed[editor::MaximumEditorPanelStateBytes]{};
        std::memcpy(malformed, bytes, byteCount);
        malformed[0] ^= 0xffu;
        Check(!DecodeConsoleContentPersistence(
                  {malformed, byteCount}, static_cast<u32>(diagnostics::Category::Count), decoded),
              "Console content restore rejects invalid wire magic");
        std::memcpy(malformed, bytes, byteCount);
        malformed[4] = 0x40u;
        Check(!DecodeConsoleContentPersistence(
                  {malformed, byteCount}, static_cast<u32>(diagnostics::Category::Count), decoded),
              "Console content restore rejects level bits outside the six durable diagnostics levels");
        std::memcpy(malformed, bytes, byteCount);
        const vanguard::u32 invalidCategory = static_cast<u32>(diagnostics::Category::Count);
        malformed[5] = static_cast<u8>(invalidCategory);
        malformed[6] = static_cast<u8>(invalidCategory >> 8u);
        malformed[7] = static_cast<u8>(invalidCategory >> 16u);
        malformed[8] = static_cast<u8>(invalidCategory >> 24u);
        Check(!DecodeConsoleContentPersistence(
                  {malformed, byteCount}, static_cast<u32>(diagnostics::Category::Count), decoded),
              "Console content restore rejects an out-of-range category");
        std::memcpy(malformed, bytes, byteCount);
        malformed[9] = 2u;
        Check(!DecodeConsoleContentPersistence(
                  {malformed, byteCount}, static_cast<u32>(diagnostics::Category::Count), decoded),
              "Console content restore rejects non-boolean scroll lock state");
        std::memcpy(malformed, bytes, byteCount);
        malformed[10] = static_cast<u8>(malformed[10] + 1u);
        Check(!DecodeConsoleContentPersistence(
                  {malformed, byteCount}, static_cast<u32>(diagnostics::Category::Count), decoded),
              "Console content restore rejects a declared text length that does not consume the payload exactly");
        std::memcpy(malformed, bytes, byteCount);
        malformed[11] = '\0';
        Check(!DecodeConsoleContentPersistence(
                  {malformed, byteCount}, static_cast<u32>(diagnostics::Category::Count), decoded),
              "Console content restore rejects embedded NUL inside the length-delimited text filter");
        std::memcpy(malformed, bytes, byteCount);
        malformed[byteCount] = 0u;
        Check(!DecodeConsoleContentPersistence(
                  {malformed, byteCount + 1u}, static_cast<u32>(diagnostics::Category::Count), decoded),
              "Console content restore rejects trailing bytes");

        vanguard::u32 insufficientBytes = 0u;
        Check(CaptureConsoleContentPersistence(
                  state, static_cast<u32>(diagnostics::Category::Count), bytes, byteCount - 1u,
                  insufficientBytes) == editor::EditorPanelStateResult::CapacityExceeded &&
                  insufficientBytes == 0u,
              "Console content capture reports CapacityExceeded for a one-byte-short destination");

        ConsoleContentPersistenceState maximum;
        maximum.category = -1;
        maximum.scrollLock = true;
        std::memset(maximum.text, 'x', ActivityPreferenceTextBytes - 1u);
        maximum.text[ActivityPreferenceTextBytes - 1u] = '\0';
        vanguard::u32 maximumBytes = 0u;
        Check(CaptureConsoleContentPersistence(
                  maximum, static_cast<u32>(diagnostics::Category::Count), bytes, sizeof(bytes), maximumBytes) ==
                  editor::EditorPanelStateResult::Success && maximumBytes == 138u &&
                  maximumBytes < editor::MaximumEditorPanelStateBytes &&
                  DecodeConsoleContentPersistence(
                      {bytes, maximumBytes}, static_cast<u32>(diagnostics::Category::Count), decoded) &&
                  std::strlen(decoded.text) == ActivityPreferenceTextBytes - 1u,
              "Console content persistence accepts the exact 127-byte text bound in a tiny provider blob");

        bool panelLevels[ActivityPreferenceLevelCount]{true, true, true, true, true, true};
        vanguard::i32 panelCategory = -1;
        char panelText[ActivityPreferenceTextBytes]{};
        bool panelScrollLock = true;
        bool panelContentRestored = false;
        vanguard::u64 panelRevision = 7u;
        ApplyConsoleContentPersistenceState(
            state, panelLevels, panelCategory, panelText, panelScrollLock, panelContentRestored);
        Check(panelContentRestored && !panelLevels[1] && !panelLevels[4] &&
                  panelCategory == state.category && std::strcmp(panelText, "shader") == 0 &&
                  !panelScrollLock,
              "Console Content restore marks the panel blob authoritative and applies every durable Console field");

        editor::EditorPreferenceFieldValue legacyText;
        legacyText.type = editor::EditorPreferenceValueType::Utf8String;
        legacyText.value.string.byteCount = 6u;
        std::memcpy(legacyText.value.string.bytes, "legacy", 6u);
        legacyText.value.string.bytes[6] = '\0';
        ApplyLegacyConsolePreferenceIfNeeded(
            panelContentRestored, panelLevels, panelCategory, panelText, panelRevision,
            static_cast<u32>(diagnostics::Category::Count), ActivityTextPreferenceFieldId,
            legacyText);
        Check(std::strcmp(panelText, "shader") == 0 && panelRevision == 7u,
              "Console panel Content authority blocks later legacy typed-preference apply without dirtying the owner revision");

        bool legacyLevels[ActivityPreferenceLevelCount]{true, true, true, true, true, true};
        vanguard::i32 legacyCategory = -1;
        char migratedText[ActivityPreferenceTextBytes]{};
        bool legacyPanelContentRestored = false;
        bool legacyScrollLock = true;
        vanguard::u64 legacyRevision = 1u;

        editor::EditorPreferenceFieldValue legacyLevelsValue;
        legacyLevelsValue.type = editor::EditorPreferenceValueType::UnsignedBitMask;
        legacyLevelsValue.value.unsignedBitMask = 0x2du;
        ApplyLegacyConsolePreferenceIfNeeded(
            legacyPanelContentRestored, legacyLevels, legacyCategory, migratedText, legacyRevision,
            static_cast<u32>(diagnostics::Category::Count), ActivityLevelsPreferenceFieldId,
            legacyLevelsValue);

        editor::EditorPreferenceFieldValue legacyCategoryValue;
        legacyCategoryValue.type = editor::EditorPreferenceValueType::SignedInteger;
        legacyCategoryValue.value.signedInteger = static_cast<vanguard::i64>(diagnostics::Category::Rendering);
        ApplyLegacyConsolePreferenceIfNeeded(
            legacyPanelContentRestored, legacyLevels, legacyCategory, migratedText, legacyRevision,
            static_cast<u32>(diagnostics::Category::Count), ActivityCategoryPreferenceFieldId,
            legacyCategoryValue);
        ApplyLegacyConsolePreferenceIfNeeded(
            legacyPanelContentRestored, legacyLevels, legacyCategory, migratedText, legacyRevision,
            static_cast<u32>(diagnostics::Category::Count), ActivityTextPreferenceFieldId,
            legacyText);
        Check(legacyRevision == 4u && EncodeConsoleLevelMask(legacyLevels) == 0x2du &&
                  legacyCategory == static_cast<vanguard::i32>(diagnostics::Category::Rendering) &&
                  std::strcmp(migratedText, "legacy") == 0,
              "Console legacy preference-only restore remains active when no panel Content blob was restored");

        editor::EditorPreferenceFieldValue invalidLegacyCategory;
        invalidLegacyCategory.type = editor::EditorPreferenceValueType::SignedInteger;
        invalidLegacyCategory.value.signedInteger = static_cast<vanguard::i64>(diagnostics::Category::Count);
        const vanguard::u64 beforeInvalidLegacy = legacyRevision;
        const vanguard::i32 beforeInvalidCategory = legacyCategory;
        Check(ValidateLegacyConsolePreferenceForPreflight(
                  ActivityCategoryPreferenceFieldId, invalidLegacyCategory),
              "Console legacy compatibility preflight treats semantically stale category rows as non-fatal shadow state");
        ApplyLegacyConsolePreferenceIfNeeded(
            legacyPanelContentRestored, legacyLevels, legacyCategory, migratedText, legacyRevision,
            static_cast<u32>(diagnostics::Category::Count), ActivityCategoryPreferenceFieldId,
            invalidLegacyCategory);
        Check(legacyCategory == beforeInvalidCategory && legacyRevision == beforeInvalidLegacy,
              "Console legacy compatibility apply ignores semantically invalid shadow rows without mutating live state or revision");

        ConsoleContentPersistenceState migrated;
        for (u32 index = 0u; index < ActivityPreferenceLevelCount; ++index)
            migrated.levels[index] = legacyLevels[index];
        migrated.category = legacyCategory;
        std::memcpy(migrated.text, migratedText, sizeof(migrated.text));
        migrated.scrollLock = legacyScrollLock;
        vanguard::u32 migratedByteCount = 0u;
        ConsoleContentPersistenceState migratedDecoded;
        Check(CaptureConsoleContentPersistence(
                  migrated, static_cast<u32>(diagnostics::Category::Count), bytes, sizeof(bytes),
                  migratedByteCount) == editor::EditorPanelStateResult::Success &&
                  DecodeConsoleContentPersistence(
                      {bytes, migratedByteCount}, static_cast<u32>(diagnostics::Category::Count),
                      migratedDecoded) &&
                  EncodeConsoleLevelMask(migratedDecoded.levels) == 0x2du &&
                  migratedDecoded.category == legacyCategory &&
                  std::strcmp(migratedDecoded.text, "legacy") == 0 && migratedDecoded.scrollLock,
              "Console legacy preference-only state captures into the current CNS1 panel blob on the next save");
    }

    class FileDialogFixturePlatform final : public app::IPlatformHost
    {
    public:
        struct Submission final
        {
            app::PlatformFileDialogCallback callback = nullptr;
            void* userData = nullptr;
            bool allowMultiple = false;
            bool active = false;
        };

        [[nodiscard]] const char* GetName() const noexcept override { return "file-dialog-fixture"; }
        [[nodiscard]] app::PlatformStatus Initialize(const app::PlatformStartupInfo&) noexcept override
        {
            m_initialized = true;
            return app::PlatformStatus::Success();
        }
        [[nodiscard]] app::PlatformPumpResult PumpEvents() noexcept override { return {}; }
        void Shutdown() noexcept override { m_initialized = false; }

        [[nodiscard]] app::PlatformStatus ShowOpenFileDialog(const app::PlatformOpenFileDialogRequest& request,
                                                             const app::PlatformFileDialogCallback callback,
                                                             void* const userData) noexcept override
        {
            if (rejectSubmission)
                return app::PlatformStatus::Failure("fixture rejected file dialog");
            for (u32 index = 0; index < editor::MaximumEditorFileDialogs + 4u; ++index)
            {
                if (m_submissions[index].active)
                    continue;
                m_submissions[index] = {callback, userData, request.allowMultiple, true};
                lastSubmission = index;
                ++submissionCount;
                return app::PlatformStatus::Success();
            }
            return app::PlatformStatus::Failure("fixture submission storage exhausted");
        }

        [[nodiscard]] app::PlatformStatus RevealFileLocation(const char* const utf8FilePath) noexcept override
        {
            if (rejectReveal)
                return app::PlatformStatus::Failure("fixture rejected reveal");
            if (utf8FilePath == nullptr)
                return app::PlatformStatus::Failure("fixture reveal requires a path");
            std::snprintf(lastRevealedPath, sizeof(lastRevealedPath), "%s", utf8FilePath);
            ++revealCount;
            return app::PlatformStatus::Success();
        }

        void CompleteSelected(const u32 slot, const char* const* const paths, const u32 pathCount) noexcept
        {
            Submission submission = Take(slot);
            if (submission.callback == nullptr)
                return;
            const app::PlatformFileDialogResultView result{app::PlatformFileDialogOutcome::Selected, paths, pathCount, nullptr};
            submission.callback(submission.userData, result);
            ++completionCount;
        }

        void CompleteCancelled(const u32 slot) noexcept
        {
            Submission submission = Take(slot);
            if (submission.callback == nullptr)
                return;
            const app::PlatformFileDialogResultView result{app::PlatformFileDialogOutcome::Cancelled, nullptr, 0u, nullptr};
            submission.callback(submission.userData, result);
            ++completionCount;
        }

        void CompleteError(const u32 slot, const char* const error) noexcept
        {
            Submission submission = Take(slot);
            if (submission.callback == nullptr)
                return;
            const app::PlatformFileDialogResultView result{app::PlatformFileDialogOutcome::Error, nullptr, 0u, error};
            submission.callback(submission.userData, result);
            ++completionCount;
        }

        [[nodiscard]] bool AllowMultiple(const u32 slot) const noexcept
        {
            return slot < editor::MaximumEditorFileDialogs + 4u && m_submissions[slot].allowMultiple;
        }

        bool rejectSubmission = false;
        bool rejectReveal = false;
        u32 lastSubmission = ~u32{0};
        u32 submissionCount = 0;
        u32 completionCount = 0;
        u32 revealCount = 0;
        char lastRevealedPath[256]{};

    private:
        [[nodiscard]] Submission Take(const u32 slot) noexcept
        {
            if (slot >= editor::MaximumEditorFileDialogs + 4u || !m_submissions[slot].active)
                return {};
            Submission result = m_submissions[slot];
            m_submissions[slot] = {};
            return result;
        }

        Submission m_submissions[editor::MaximumEditorFileDialogs + 4u]{};
        bool m_initialized = false;
    };

    class FileDialogFixtureWindowService final : public engine::WindowService
    {
    public:
        [[nodiscard]] vanguard::window::WindowManager& GetManager() noexcept override { std::abort(); }
        [[nodiscard]] const vanguard::window::WindowManager& GetManager() const noexcept override { std::abort(); }
        [[nodiscard]] vanguard::window::WindowHandle GetPrimaryWindow() const noexcept override { return {0u, 1u}; }
        [[nodiscard]] bool AttachFileDropSink(engine::IWindowFileDropSink* const sink) noexcept override
        {
            if (sink == nullptr || (m_sink != nullptr && m_sink != sink))
                return false;
            m_sink = sink;
            return true;
        }
        void DetachFileDropSink(engine::IWindowFileDropSink* const sink) noexcept override
        {
            if (m_sink == sink)
                m_sink = nullptr;
        }
        [[nodiscard]] bool Emit(const engine::WindowFileDropEvent& event) noexcept
        {
            return m_sink != nullptr && m_sink->ProcessFileDrop(event);
        }

    private:
        engine::IWindowFileDropSink* m_sink = nullptr;
    };

    FileDialogFixtureWindowService* g_fileDialogFixtureWindows = nullptr;

    app::Service* CreateFileDialogFixtureWindowService(void*) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Editor,
                                                    sizeof(FileDialogFixtureWindowService),
                                                    alignof(FileDialogFixtureWindowService));
        if (!block)
            return nullptr;
        g_fileDialogFixtureWindows = ::new (block.address) FileDialogFixtureWindowService();
        return g_fileDialogFixtureWindows;
    }

    void DestroyFileDialogFixtureWindowService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        g_fileDialogFixtureWindows = nullptr;
        static_cast<FileDialogFixtureWindowService*>(service)->~FileDialogFixtureWindowService();
        memory::MemoryBlock block{service, sizeof(FileDialogFixtureWindowService), memory::PoolId::Editor};
        memory::Free(block);
    }

    [[nodiscard]] bool RegisterFileDialogFixtureWindowService(app::EngineHost& host,
                                                              app::HostFailure* const failure) noexcept
    {
        constexpr app::CapabilityId capabilities[]{engine::WindowCapabilityId};
        app::ServiceDescriptor descriptor;
        descriptor.id = engine::WindowServiceId;
        descriptor.name = "fileDialogFixtureWindow";
        descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        descriptor.scope = app::ServiceScope::EditorWorkspace;
        descriptor.affinity = app::ThreadAffinity::MainThread;
        descriptor.provides = capabilities;
        descriptor.create = CreateFileDialogFixtureWindowService;
        descriptor.destroy = DestroyFileDialogFixtureWindowService;
        return host.RegisterService(editor::EditorModuleId, descriptor, failure);
    }

    class FileDialogSelectionWorker final : public vanguard::concurrency::Thread
    {
    public:
        FileDialogSelectionWorker(FileDialogFixturePlatform& platform, const u32 slot) noexcept
            : Thread("fileDialogCallback"), m_platform(platform), m_slot(slot)
        {
        }

        void ThreadFunction() noexcept override
        {
            const char first[] = "C:/external/first.png";
            const char second[] = "D:/drop/second.dds";
            const char* const paths[]{first, second};
            m_platform.CompleteSelected(m_slot, paths, 2u);
        }

    private:
        FileDialogFixturePlatform& m_platform;
        u32 m_slot = 0;
    };

    void TestEditorFileDialogService() noexcept
    {
        FileDialogFixturePlatform platform;
        app::EngineHost host;
        app::HostFailure hostFailure;
        Check(host.RegisterModule({editor::EditorModuleId, "editor", 1}, &hostFailure) &&
                  RegisterFileDialogFixtureWindowService(host, &hostFailure) &&
                  editor::RegisterEditorFileDialogService(host, &platform, &hostFailure) &&
                  host.Compile(app::ApplicationProfile::Editor, &hostFailure) && host.Start(&hostFailure),
              "P0.2 file-dialog service compiles and starts as an editor service");
        editor::EditorFileDialogService* const service = editor::FindEditorFileDialogService(host);
        Check(service != nullptr, "P0.2 file-dialog capability resolves from the compiled host");
        if (service == nullptr)
            return;

        containers::DynamicArray<containers::String> paths(memory::pools::Editor::GetInstance());
        containers::String error;
        editor::EditorFileDialogFailure failure;
        editor::EditorFileDialogPollResult poll;

        Check(service->RevealFileLocation("C:/project/Assets/source.png", &failure) &&
                  platform.revealCount == 1u &&
                  std::strcmp(platform.lastRevealedPath, "C:/project/Assets/source.png") == 0,
              "P0.5 source-location reveal forwards the exact project-owned path through the reusable platform service");
        platform.rejectReveal = true;
        Check(!service->RevealFileLocation("C:/project/Assets/source.png", &failure) &&
                  failure.code == editor::EditorFileDialogFailureCode::PlatformFailure,
              "P0.5 platform reveal failure remains a non-mutating editor service failure");
        platform.rejectReveal = false;

        Check(g_fileDialogFixtureWindows != nullptr,
              "P0.4 file-dialog service owns the compiled WindowService drop sink");
        if (g_fileDialogFixtureWindows != nullptr)
        {
            const vanguard::window::WindowHandle dropWindow{3u, 7u};
            const vanguard::window::WindowPoint dropPoint{41, 53};
            char borrowedDropPath[] = "C:/external/drop.png";
            Check(g_fileDialogFixtureWindows->Emit(
                      {engine::WindowFileDropEventType::Begin, dropWindow, dropPoint, 100u, nullptr}) &&
                      g_fileDialogFixtureWindows->Emit(
                          {engine::WindowFileDropEventType::File, dropWindow, dropPoint, 101u, borrowedDropPath}),
                  "P0.4 WindowService drop sink accepts begin/file events synchronously");
            borrowedDropPath[0] = 'X';
            Check(g_fileDialogFixtureWindows->Emit(
                      {engine::WindowFileDropEventType::Complete, dropWindow, dropPoint, 102u, nullptr}),
                  "P0.4 WindowService drop sink accepts completion");
            editor::EditorFileDropPollResult drop;
            Check(service->PollFileDrop(drop, paths, &failure) &&
                      drop.state == editor::EditorFileDropState::Ready &&
                      drop.window == dropWindow && drop.position.x == dropPoint.x && drop.position.y == dropPoint.y &&
                      drop.pathCount == 1u && paths.Size() == 1u &&
                      std::strcmp(paths[0].AsChar(), "C:/external/drop.png") == 0,
                  "P0.4 borrowed native drop path is copied before event return and consumed with exact host/position");

            Check(g_fileDialogFixtureWindows->Emit(
                      {engine::WindowFileDropEventType::Begin, dropWindow, dropPoint, 200u, nullptr}),
                  "P0.4 count-limit drop begins");
            for (u32 index = 0u; index < editor::MaximumEditorFileDialogSelections + 1u; ++index)
                Check(g_fileDialogFixtureWindows->Emit(
                          {engine::WindowFileDropEventType::File, dropWindow, dropPoint,
                           201u + index, "C:/external/x.png"}),
                      "P0.4 count-limit drop file is synchronously consumed");
            Check(g_fileDialogFixtureWindows->Emit(
                      {engine::WindowFileDropEventType::Complete, dropWindow, dropPoint, 300u, nullptr}) &&
                      service->PollFileDrop(drop, paths, &failure) &&
                      drop.state == editor::EditorFileDropState::LimitExceeded && paths.Empty(),
                  "P0.4 over-limit dropped-file count returns no partial paths");

            containers::DynamicArray<char> excessiveDropPath(memory::pools::Editor::GetInstance());
            excessiveDropPath.Resize(editor::MaximumEditorFileDialogPathBytes + 1u);
            for (u32 index = 0u; index < editor::MaximumEditorFileDialogPathBytes; ++index)
                excessiveDropPath[index] = 'y';
            excessiveDropPath[editor::MaximumEditorFileDialogPathBytes] = '\0';
            Check(g_fileDialogFixtureWindows->Emit(
                      {engine::WindowFileDropEventType::Begin, dropWindow, dropPoint, 400u, nullptr}) &&
                      g_fileDialogFixtureWindows->Emit(
                          {engine::WindowFileDropEventType::File, dropWindow, dropPoint, 401u,
                           excessiveDropPath.TypedData()}) &&
                      g_fileDialogFixtureWindows->Emit(
                          {engine::WindowFileDropEventType::Complete, dropWindow, dropPoint, 402u, nullptr}) &&
                      service->PollFileDrop(drop, paths, &failure) &&
                      drop.state == editor::EditorFileDropState::LimitExceeded && paths.Empty(),
                  "P0.4 over-limit dropped path bytes return no partial paths");

            Check(g_fileDialogFixtureWindows->Emit(
                      {engine::WindowFileDropEventType::Begin, dropWindow, dropPoint, 500u, nullptr}) &&
                      g_fileDialogFixtureWindows->Emit(
                          {engine::WindowFileDropEventType::File, dropWindow, dropPoint, 501u,
                           "C:/external/stale.png"}) &&
                      g_fileDialogFixtureWindows->Emit(
                          {engine::WindowFileDropEventType::Complete, dropWindow, dropPoint, 502u, nullptr}),
                  "P0.4 stale-drop fixture completes one bounded batch");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            Check(service->PollFileDrop(drop, paths, &failure) &&
                      drop.state == editor::EditorFileDropState::None && paths.Empty(),
                  "P0.4 completed drop expires instead of being consumed by a later unrelated UI frame");
        }

        editor::EditorFileDialogHandle selected;
        Check(service->OpenFiles(true, selected, &failure) && selected.IsValid(),
              "P0.2 multi-select open request is admitted on the main thread");
        const u32 selectedSlot = platform.lastSubmission;
        Check(platform.AllowMultiple(selectedSlot), "P0.2 portable request forwards multi-select intent to the platform");
        Check(service->Poll(selected, poll, paths, error, &failure) && poll.state == editor::EditorFileDialogState::Pending &&
                  paths.Empty() && error.Empty(),
              "P0.2 pending native dialog stays a non-mutating main-thread mailbox");
        FileDialogSelectionWorker selectionWorker(platform, selectedSlot);
        selectionWorker.InitThread();
        selectionWorker.JoinThread();
        Check(service->Poll(selected, poll, paths, error, &failure) && poll.state == editor::EditorFileDialogState::Selected &&
                  poll.pathCount == 2u && paths.Size() == 2u && std::strcmp(paths[0].AsChar(), "C:/external/first.png") == 0 &&
                  std::strcmp(paths[1].AsChar(), "D:/drop/second.dds") == 0 && error.Empty(),
              "P0.2 foreign-thread callback paths are copied before callback return and consumed on the main thread");
        Check(!service->Poll(selected, poll, paths, error, &failure) && failure.code == editor::EditorFileDialogFailureCode::InvalidHandle,
              "P0.2 terminal result consumption invalidates the exact request handle");

        editor::EditorFileDialogHandle cancelled;
        Check(service->OpenFiles(false, cancelled, &failure), "P0.2 cancellation fixture opens");
        platform.CompleteCancelled(platform.lastSubmission);
        Check(service->Poll(cancelled, poll, paths, error, &failure) && poll.state == editor::EditorFileDialogState::Cancelled &&
                  paths.Empty() && error.Empty(),
              "P0.2 user cancellation is terminal and distinct from backend failure");

        editor::EditorFileDialogHandle failed;
        Check(service->OpenFiles(false, failed, &failure), "P0.2 backend-error fixture opens");
        platform.CompleteError(platform.lastSubmission, "native picker failed");
        Check(service->Poll(failed, poll, paths, error, &failure) && poll.state == editor::EditorFileDialogState::BackendError &&
                  paths.Empty() && std::strcmp(error.AsChar(), "native picker failed") == 0,
              "P0.2 backend error remains distinct and copies callback-lifetime diagnostic text");

        editor::EditorFileDialogHandle limited;
        Check(service->OpenFiles(true, limited, &failure), "P0.2 selection-limit fixture opens");
        const char* excessive[editor::MaximumEditorFileDialogSelections + 1u]{};
        for (u32 index = 0; index < editor::MaximumEditorFileDialogSelections + 1u; ++index)
            excessive[index] = "C:/external/x.png";
        platform.CompleteSelected(platform.lastSubmission, excessive, editor::MaximumEditorFileDialogSelections + 1u);
        Check(service->Poll(limited, poll, paths, error, &failure) && poll.state == editor::EditorFileDialogState::LimitExceeded &&
                  paths.Empty(),
              "P0.2 callback storage rejects over-limit multi-selection without partial paths");

        editor::EditorFileDialogHandle byteLimited;
        Check(service->OpenFiles(false, byteLimited, &failure), "P0.2 path-byte-limit fixture opens");
        containers::DynamicArray<char> excessivePath(memory::pools::Editor::GetInstance());
        excessivePath.Resize(editor::MaximumEditorFileDialogPathBytes + 1u);
        for (u32 index = 0; index < editor::MaximumEditorFileDialogPathBytes; ++index)
            excessivePath[index] = 'x';
        excessivePath[editor::MaximumEditorFileDialogPathBytes] = '\0';
        const char* const oversizedPath[]{excessivePath.TypedData()};
        platform.CompleteSelected(platform.lastSubmission, oversizedPath, 1u);
        Check(service->Poll(byteLimited, poll, paths, error, &failure) && poll.state == editor::EditorFileDialogState::LimitExceeded &&
                  paths.Empty(),
              "P0.2 callback storage rejects a path over the total byte budget without partial results");

        platform.rejectSubmission = true;
        editor::EditorFileDialogHandle rejected;
        Check(!service->OpenFiles(false, rejected, &failure) && !rejected.IsValid() &&
                  failure.code == editor::EditorFileDialogFailureCode::PlatformFailure,
              "P0.2 platform submission failure never leaks a live editor request");
        platform.rejectSubmission = false;

        editor::EditorFileDialogHandle pending[editor::MaximumEditorFileDialogs]{};
        u32 pendingSlots[editor::MaximumEditorFileDialogs]{};
        for (u32 index = 0; index < editor::MaximumEditorFileDialogs; ++index)
        {
            Check(service->OpenFiles(false, pending[index], &failure), "P0.2 bounded outstanding request slot is admitted");
            pendingSlots[index] = platform.lastSubmission;
        }
        editor::EditorFileDialogHandle overflow;
        Check(!service->OpenFiles(false, overflow, &failure) && failure.code == editor::EditorFileDialogFailureCode::CapacityExceeded,
              "P0.2 outstanding dialog capacity is hard-bounded");
        Check(service->Release(pending[0], &failure), "P0.2 pending dialog can abandon local ownership without native cancellation");
        platform.CompleteCancelled(pendingSlots[0]);
        Check(platform.completionCount >= 5u,
              "P0.2 late callback after explicit release safely completes its detached mailbox");
        for (u32 index = 1; index < editor::MaximumEditorFileDialogs; ++index)
        {
            Check(service->Release(pending[index], &failure), "P0.2 remaining capacity fixture request releases");
            platform.CompleteCancelled(pendingSlots[index]);
        }

        editor::EditorFileDialogHandle shutdownPending;
        Check(service->OpenFiles(false, shutdownPending, &failure),
              "P0.2 shutdown fixture leaves one native request outstanding");
        const u32 shutdownSlot = platform.lastSubmission;
        Check(host.Shutdown(&hostFailure),
              "P0.2 service shutdown detaches an outstanding native request without waiting for its callback");
        platform.CompleteCancelled(shutdownSlot);
        Check(platform.completionCount >= 1u,
              "P0.2 callback after service destruction safely self-reclaims detached mailbox state");
    }
} // namespace

int main(const int argc, const char* const* const argv)
{
    vanguard::concurrency::InitializeMainThread();
    Check(memory::Initialize(), "memory initialization");
    Check(containers::Initialize(), "containers initialization");

    if (argc == 2 && std::strcmp(argv[1], "--demonstration-extension") == 0)
    {
        TestProductionDemonstrationExtension();
        if (g_failures == 0)
            std::printf("Vanguard production editor demonstration-extension test passed.\n");
        return g_failures == 0 ? 0 : 1;
    }

    TestValidCatalogAndSelection();
    TestDescriptorFailures();
    TestProjectSelectionFailures();
    TestAssetBrowserPersistenceWire();
    TestProductionAssetBrowserBrowsePolicy();
    TestShellServiceSuccessfulActivation();
    TestShellServiceProfileExclusion(app::ApplicationProfile::Runtime, "runtime profile compiles with editor services excluded");
    TestShellServiceProfileExclusion(app::ApplicationProfile::Headless, "headless profile compiles with editor services excluded");
    TestShellRejectsWrongThreadStart();
    TestShellRejectsWrongThreadShutdown();
    TestRequiredCommandServiceDependency();
    TestRequiredActivityServiceDependency();
    TestOwnedActivitySourceRegistrationAndRollback();
    TestOwnedCommandRegistrationAndOrdering();
    TestPartialActivationCommandRollback();
    TestCrossOwnerDependencyCleanup();
    TestFreshHostCommandTokensStayStale();
    TestSharedOwnedLedgerCapacity();
    TestOwnedSurfaceRegistrationAndTeardown();
    TestPartialActivationSurfaceRollback();
    TestCrossOwnerSurfaceReferences();
    TestOwnedPanelShutdownAndReverseTeardown();
    TestPartialActivationPanelRollback();
    TestPanelFocusBridgeAndLifo();
    TestCrossOwnerPanelLocalContextAssociation();
    TestTypedPreferencePersistence();
    TestPreferenceProviderBounds();
    TestActivityPreferencePolicy();
    TestConsolePersistencePolicy();
    TestEditorFileDialogService();
    TestStaticBuiltInOnlyIntegration();
    TestStaticProjectAndDependencySelection();
    TestUnknownProjectSelectionFailsWithoutPluginPathAccess();
    TestEmptyCatalogLifecycle();
    TestSurfaceCommandBridgeLifecycle();
    TestSurfaceCommandBridgeInstallRollback();
    TestProjectSelectionWithMissingCompiledDependency();
    TestIndependentDependencyChainPermutation();

    if (g_failures == 0)
        std::printf("Vanguard editor shell ownership/focus and failure-matrix tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}

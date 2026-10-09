#include <vanguard/editor/editor_panels.hpp>
#include <editor_extension_registration_test_adapter.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace
{
    namespace editor = vanguard::editor;
    namespace detail = vanguard::editor::detail;
    namespace memory = vanguard::memory;
    using vanguard::u8;
    using vanguard::u32;
    using vanguard::u64;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[editorPanelTests] FAILED: %s\n", message);
        ++g_failures;
    }

    struct PanelFixture final
    {
        u32 createCalls = 0;
        u32 destroyCalls = 0;
        u32 drawCalls = 0;
        bool failCreate = false;
    };

    struct FocusInvalidationFixture final
    {
        editor::EditorPanelHandle focused;
        u32 calls = 0;
        bool reject = false;
    };

    struct PersistentPanelFixture final
    {
        editor::EditorPanelInstanceId expectedInstance = 910;
        detail::EditorPanelRegistryCore* registry = nullptr;
        editor::EditorPanelHandle handle;
        u64 revision = 1;
        u8 presentation[8]{1, 2, 3};
        u32 presentationBytes = 3;
        u8 content[8]{4, 5};
        u32 contentBytes = 2;
        u8 restored[16]{};
        u32 restoredBytes = 0;
        u32 restoreCalls = 0;
        u32 migrateCalls = 0;
        u32 destroyCalls = 0;
        editor::EditorPanelStateScope restoredScope = editor::EditorPanelStateScope::Presentation;
        u32 restoredVersion = 0;
        bool noContentState = false;
        bool acceptLegacyVersion = false;
        bool attemptDestroyOnRevision = false;
        bool attemptDestroyOnMigrate = false;
        bool attemptOwnerRevokeOnRevision = false;
        bool reentrantDestroySucceeded = false;
        bool reentrantOwnerRevokeAdmitted = false;
        editor::EditorUiFailureCode reentrantFailure = editor::EditorUiFailureCode::None;
        editor::EditorExtensionOwner owner;
    };

    bool InvalidateFocus(const editor::EditorPanelHandle panel,
                         const detail::EditorPanelInvalidationReason,
                         void* const userData,
                         editor::EditorUiFailure* const failure) noexcept
    {
        auto& fixture = *static_cast<FocusInvalidationFixture*>(userData);
        if (panel != fixture.focused)
            return true;
        ++fixture.calls;
        if (!fixture.reject)
            return true;
        if (failure != nullptr)
        {
            *failure = {};
            failure->code = editor::EditorUiFailureCode::FocusSynchronizationFailure;
            failure->panel = panel;
            failure->message = "fixture focus synchronization rejected";
        }
        return false;
    }

    void* CreatePanelState(editor::EditorPanelInstanceId, void* const data) noexcept
    {
        auto& fixture = *static_cast<PanelFixture*>(data);
        ++fixture.createCalls;
        return fixture.failCreate ? nullptr : &fixture;
    }

    void DestroyPanelState(void* const state, void* const data) noexcept
    {
        auto& fixture = *static_cast<PanelFixture*>(data);
        Check(state == &fixture, "destroy callback receives the factory-produced panel state");
        ++fixture.destroyCalls;
    }

    void DrawPanel(editor::EditorPanelContext&, void*, void* const data) noexcept
    {
        if (data != nullptr)
            ++static_cast<PanelFixture*>(data)->drawCalls;
    }

    void* CreatePersistentPanelState(editor::EditorPanelInstanceId, void* const data) noexcept
    {
        return data;
    }

    void DestroyPersistentPanelState(void*, void* const data) noexcept
    {
        ++static_cast<PersistentPanelFixture*>(data)->destroyCalls;
    }

    void DrawPersistentPanel(editor::EditorPanelContext&, void*, void*) noexcept {}

    u64 GetPersistentPanelRevision(const editor::EditorPanelInstanceId instance, void* const panel, void*) noexcept
    {
        auto& fixture = *static_cast<PersistentPanelFixture*>(panel);
        Check(instance == fixture.expectedInstance, "persistent panel revision callback receives stable instance identity");
        if (fixture.attemptDestroyOnRevision && fixture.registry != nullptr)
        {
            editor::EditorUiFailure failure;
            fixture.reentrantDestroySucceeded = fixture.registry->DestroyPanel(fixture.handle, &failure);
            fixture.reentrantFailure = failure.code;
            fixture.attemptDestroyOnRevision = false;
        }
        if (fixture.attemptOwnerRevokeOnRevision && fixture.registry != nullptr)
        {
            fixture.reentrantOwnerRevokeAdmitted =
                editor::testing::EditorExtensionRegistrationTestAdapter::CanRevokePanelOwner(
                    *fixture.registry, fixture.owner);
            editor::testing::EditorExtensionRegistrationTestAdapter::RevokePanelOwner(
                *fixture.registry, fixture.owner);
            fixture.attemptOwnerRevokeOnRevision = false;
        }
        return fixture.revision;
    }

    editor::EditorPanelStateResult CapturePersistentPanelState(
        const editor::EditorPanelInstanceId instance, void* const panel, const editor::EditorPanelStateScope scope,
        void* const destination,
        const u32 capacity, u32& byteCount, void*) noexcept
    {
        auto& fixture = *static_cast<PersistentPanelFixture*>(panel);
        Check(instance == fixture.expectedInstance, "persistent panel capture callback receives stable instance identity");
        if (scope == editor::EditorPanelStateScope::Content && fixture.noContentState)
        {
            byteCount = 0;
            return editor::EditorPanelStateResult::NoState;
        }
        const u8* source = scope == editor::EditorPanelStateScope::Presentation
                               ? fixture.presentation
                               : fixture.content;
        const u32 sourceBytes = scope == editor::EditorPanelStateScope::Presentation
                                    ? fixture.presentationBytes
                                    : fixture.contentBytes;
        if (sourceBytes > capacity || (sourceBytes != 0 && destination == nullptr))
            return editor::EditorPanelStateResult::CapacityExceeded;
        if (sourceBytes != 0)
            std::memcpy(destination, source, sourceBytes);
        byteCount = sourceBytes;
        return editor::EditorPanelStateResult::Success;
    }

    editor::EditorPanelStateResult RestorePersistentPanelState(
        const editor::EditorPanelInstanceId instance, void* const panel,
        const editor::EditorPanelStateScope scope, const u32 version,
        const vanguard::containers::ArraySpan<const u8> bytes, void*) noexcept
    {
        auto& fixture = *static_cast<PersistentPanelFixture*>(panel);
        Check(instance == fixture.expectedInstance, "persistent panel restore callback receives stable instance identity");
        if ((version != 2 && !(fixture.acceptLegacyVersion && version == 1)) ||
            bytes.Size() > sizeof(fixture.restored))
            return editor::EditorPanelStateResult::InvalidData;
        if (!bytes.Empty())
            std::memcpy(fixture.restored, bytes.Data(), bytes.Size());
        fixture.restoredBytes = bytes.Size();
        fixture.restoredScope = scope;
        fixture.restoredVersion = version;
        ++fixture.restoreCalls;
        return editor::EditorPanelStateResult::Success;
    }

    editor::EditorPanelStateResult MigratePersistentPanelState(
        const editor::EditorPanelInstanceId instance, void* const panel,
        const editor::EditorPanelStateScope, const u32 fromVersion,
        const vanguard::containers::ArraySpan<const u8> source, void* const destination,
        const u32 capacity, u32& byteCount, void*) noexcept
    {
        auto& fixture = *static_cast<PersistentPanelFixture*>(panel);
        Check(instance == fixture.expectedInstance, "persistent panel migration callback receives stable instance identity");
        if (fixture.attemptDestroyOnMigrate && fixture.registry != nullptr)
        {
            editor::EditorUiFailure failure;
            fixture.reentrantDestroySucceeded = fixture.registry->DestroyPanel(fixture.handle, &failure);
            fixture.reentrantFailure = failure.code;
            fixture.attemptDestroyOnMigrate = false;
        }
        if (fromVersion != 1 || source.Size() + 1u > capacity || destination == nullptr)
            return editor::EditorPanelStateResult::UnsupportedVersion;
        if (!source.Empty())
            std::memcpy(destination, source.Data(), source.Size());
        static_cast<u8*>(destination)[source.Size()] = 0x7fu;
        byteCount = source.Size() + 1u;
        ++fixture.migrateCalls;
        return editor::EditorPanelStateResult::Success;
    }

    editor::EditorPanelTypeDesc PersistentType(const editor::EditorPanelTypeId id, const char* const name,
                                               PersistentPanelFixture& fixture) noexcept
    {
        editor::EditorPanelTypeDesc desc;
        desc.id = id;
        desc.name = name;
        desc.create = CreatePersistentPanelState;
        desc.destroy = DestroyPersistentPanelState;
        desc.draw = DrawPersistentPanel;
        desc.factoryData = &fixture;
        desc.policy = editor::EditorPanelTypePolicy::Multiple;
        desc.persistence.stateVersion = 2;
        desc.persistence.capture = CapturePersistentPanelState;
        desc.persistence.restore = RestorePersistentPanelState;
        desc.persistence.migrate = MigratePersistentPanelState;
        desc.persistence.getRevision = GetPersistentPanelRevision;
        return desc;
    }

    editor::EditorPanelTypeDesc MultipleType(const editor::EditorPanelTypeId id, const char* const name,
                                             PanelFixture* const fixture = nullptr) noexcept
    {
        editor::EditorPanelTypeDesc desc;
        desc.id = id;
        desc.name = name;
        desc.draw = DrawPanel;
        if (fixture != nullptr)
        {
            desc.create = CreatePanelState;
            desc.destroy = DestroyPanelState;
            desc.factoryData = fixture;
        }
        desc.policy = editor::EditorPanelTypePolicy::Multiple;
        return desc;
    }

    editor::EditorPanelTypeDesc SingletonType(const editor::EditorPanelTypeId id, const char* const name,
                                              const editor::EditorPanelInstanceId defaultInstance,
                                              PanelFixture* const fixture = nullptr) noexcept
    {
        editor::EditorPanelTypeDesc desc = MultipleType(id, name, fixture);
        desc.policy = editor::EditorPanelTypePolicy::Singleton;
        desc.defaultInstance = defaultInstance;
        return desc;
    }

    struct LocalGenerationSource final
    {
        std::atomic<u32> types;
        std::atomic<u32> instances;

        explicit LocalGenerationSource(const u32 typeSeed = 0, const u32 instanceSeed = 0) noexcept
            : types(typeSeed), instances(instanceSeed)
        {
        }

        [[nodiscard]] detail::EditorPanelGenerationSource Get() noexcept
        {
            return {&types, &instances};
        }
    };

    struct PanelObservation final
    {
        u32 count = 0;
        editor::EditorPanelTypeId type = editor::InvalidEditorPanelTypeId;
        editor::EditorPanelInstanceId id = editor::InvalidEditorPanelInstanceId;
        editor::EditorPanelHandle handle;
        bool open = false;
        bool placeholder = false;
    };

    void ObservePanel(const editor::EditorPanelInfo& panel, void* const data) noexcept
    {
        auto& observation = *static_cast<PanelObservation*>(data);
        ++observation.count;
        observation.type = panel.type;
        observation.id = panel.id;
        observation.handle = panel.handle;
        observation.open = panel.open;
        observation.placeholder = panel.placeholder;
    }

    struct AuthoringObservation final
    {
        u32 ordinary = 0;
        u32 placeholders = 0;
        bool placeholderHasNoCallbacks = true;
    };

    struct OwnedAuthoringObservation final
    {
        editor::EditorPanelInstanceId target = editor::InvalidEditorPanelInstanceId;
        editor::EditorExtensionOwner owner;
        bool found = false;
    };

    void ObserveAuthoring(const detail::EditorPanelAuthoringView& panel, void* const data) noexcept
    {
        auto& observation = *static_cast<AuthoringObservation*>(data);
        if (panel.placeholder)
        {
            ++observation.placeholders;
            observation.placeholderHasNoCallbacks &=
                panel.state == nullptr && panel.draw == nullptr && panel.factoryData == nullptr;
        }
        else
        {
            ++observation.ordinary;
        }
    }

    void ObserveOwnedAuthoring(const detail::EditorPanelAuthoringView& panel, void* const data) noexcept
    {
        auto& observation = *static_cast<OwnedAuthoringObservation*>(data);
        if (panel.id != observation.target)
            return;
        observation.owner = panel.owner;
        observation.found = true;
    }

    void TestDescriptorTokenAndLifetime() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorPanelRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;

        editor::EditorPanelTypeToken invalidToken;
        auto invalid = MultipleType(1, "invalid");
        invalid.draw = nullptr;
        Check(!registry.RegisterType(invalid, invalidToken, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidDescriptor && !invalidToken.IsValid(),
              "invalid panel type fails without a partial type token");

        auto invalidPolicy = MultipleType(2, "invalidPolicy");
        invalidPolicy.defaultInstance = 7;
        Check(!registry.RegisterType(invalidPolicy, invalidToken, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidPolicy,
              "multi-instance panel type rejects a singleton default instance");

        PanelFixture fixture;
        editor::EditorPanelTypeToken firstToken;
        Check(registry.RegisterType(MultipleType(10, "type", &fixture), firstToken, &failure) && firstToken.IsValid(),
              "valid panel type registers with an exact generation token");
        editor::EditorPanelTypeToken duplicateToken;
        Check(!registry.RegisterType(MultipleType(10, "duplicate"), duplicateToken, &failure) &&
                  failure.code == editor::EditorUiFailureCode::DuplicateIdentity && !duplicateToken.IsValid(),
              "duplicate panel type identity is rejected without a token");

        editor::EditorPanelHandle panel;
        Check(registry.CreatePanel({100, 10, "panel", true}, panel, &failure) && panel.IsValid() && fixture.createCalls == 1,
              "registered type creates a panel instance");
        Check(!registry.UnregisterType(firstToken, &failure) && failure.code == editor::EditorUiFailureCode::InUse,
              "type removal fails while a live instance exists");
        Check(registry.DestroyPanel(panel, &failure) && fixture.destroyCalls == 1,
              "destroy ends panel lifetime exactly once");
        Check(registry.UnregisterType(firstToken, &failure), "exact live type token unregisters after instances are gone");

        editor::EditorPanelTypeToken secondToken;
        Check(registry.RegisterType(MultipleType(10, "typeAgain"), secondToken, &failure) &&
                  secondToken.index == firstToken.index && secondToken.generation != firstToken.generation,
              "reused type slot receives a new generation");
        Check(!registry.UnregisterType(firstToken, &failure) && failure.code == editor::EditorUiFailureCode::InvalidHandle,
              "stale type token cannot unregister a later registration");
        Check(registry.UnregisterType(secondToken, &failure), "current exact type token remains valid after stale-token rejection");
    }

    void TestSingletonAndMultiPolicy() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorPanelRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;

        PanelFixture singletonFixture;
        editor::EditorPanelTypeToken singletonToken;
        Check(registry.RegisterType(SingletonType(20, "singleton", 200, &singletonFixture), singletonToken, &failure),
              "singleton type registers with a stable default instance");

        editor::EditorPanelHandle first;
        editor::EditorPanelHandle second;
        Check(registry.CreatePanel({200, 20, "singleton panel", true}, first, &failure) &&
                  registry.CreatePanel({200, 20, "singleton panel", true}, second, &failure) &&
                  first == second && singletonFixture.createCalls == 1,
              "repeated singleton create returns the same live handle and factory state");
        Check(registry.SetOpen(first, false, &failure) && !registry.IsOpen(first),
              "ready-phase singleton close hides without destroying state");
        Check(registry.CreatePanel({200, 20, "singleton panel", true}, second, &failure) &&
                  second == first && registry.IsOpen(first) && singletonFixture.createCalls == 1,
              "singleton create reopens hidden instance without rerunning factory");

        editor::EditorPanelHandle wrongSingleton;
        Check(!registry.CreatePanel({201, 20, "wrong singleton", true}, wrongSingleton, &failure) &&
                  failure.code == editor::EditorUiFailureCode::SingletonViolation && !wrongSingleton.IsValid(),
              "singleton create rejects a non-default stable instance ID");

        Check(registry.DestroyPanel(first, &failure) && singletonFixture.destroyCalls == 1,
              "singleton destroy ends the retained state once");
        editor::EditorPanelHandle recreated;
        Check(registry.CreatePanel({200, 20, "singleton panel", true}, recreated, &failure) &&
                  recreated != first && recreated.generation != first.generation && singletonFixture.createCalls == 2,
              "destroyed singleton recreates with a fresh handle generation");
        Check(!registry.SetOpen(first, false, &failure) && failure.code == editor::EditorUiFailureCode::InvalidHandle,
              "destroyed singleton handle remains stale after recreation");
        Check(registry.DestroyPanel(recreated, &failure) && singletonFixture.destroyCalls == 2 &&
                  registry.UnregisterType(singletonToken, &failure),
              "singleton cleanup destroys the recreated state and unregisters its exact type token");

        editor::EditorPanelTypeToken multiToken;
        Check(registry.RegisterType(MultipleType(21, "multiple"), multiToken, &failure),
              "multi-instance type registers");
        editor::EditorPanelHandle multiA;
        editor::EditorPanelHandle multiB;
        Check(registry.CreatePanel({210, 21, "A", true}, multiA, &failure) &&
                  registry.CreatePanel({211, 21, "B", true}, multiB, &failure) && multiA != multiB,
              "multi-instance type accepts distinct stable instance IDs");
        editor::EditorPanelHandle duplicate;
        Check(!registry.CreatePanel({210, 21, "duplicate", true}, duplicate, &failure) &&
                  failure.code == editor::EditorUiFailureCode::DuplicateIdentity && !duplicate.IsValid(),
              "multi-instance type rejects a duplicate global instance ID");
        Check(registry.DestroyPanel(multiA, &failure) && registry.DestroyPanel(multiB, &failure) &&
                  registry.UnregisterType(multiToken, &failure),
              "multi-instance fixtures clean up through ordinary lifetime operations");
    }

    void TestDeferredVisibilityTransaction() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorPanelRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;
        editor::EditorPanelTypeToken token;
        editor::EditorPanelHandle panel;
        Check(registry.RegisterType(MultipleType(30, "deferred"), token, &failure) &&
                  registry.CreatePanel({300, 30, "panel", true}, panel, &failure),
              "deferred-visibility fixture is created");

        state = editor::EditorUiState::BuildingFrame;
        Check(registry.SetOpen(panel, false, &failure) && registry.IsOpen(panel),
              "in-frame close records a pending request without changing committed visibility");
        int receiverSentinel = 0;
        void* pendingReceiver = &receiverSentinel;
        Check(!registry.ResolveFocusReceiver(panel, pendingReceiver) && pendingReceiver == nullptr,
              "pending in-frame close is immediately unavailable to focus/popup reachability revalidation");
        registry.DiscardPendingVisibility();
        Check(registry.ResolveFocusReceiver(panel, pendingReceiver),
              "discarding the speculative close restores current-frame reachability");
        state = editor::EditorUiState::Ready;
        Check(registry.IsOpen(panel), "aborted frame discards a pending close");

        state = editor::EditorUiState::BuildingFrame;
        Check(registry.SetOpen(panel, false, &failure) && registry.IsOpen(panel),
              "second in-frame close remains deferred");
        registry.CommitPendingVisibility();
        state = editor::EditorUiState::Ready;
        Check(!registry.IsOpen(panel), "successful frame commit applies deferred visibility");
        Check(registry.SetOpen(panel, true, &failure) && registry.IsOpen(panel),
              "ready-phase reopen remains synchronous");

        state = editor::EditorUiState::BuildingFrame;
        editor::EditorPanelHandle rejected;
        Check(!registry.CreatePanel({301, 30, "forbidden", true}, rejected, &failure) &&
                  failure.code == editor::EditorUiFailureCode::WrongPhase && !rejected.IsValid(),
              "topology mutation remains forbidden while authoring a frame");
        state = editor::EditorUiState::Ready;
        Check(registry.DestroyPanel(panel, &failure) && registry.UnregisterType(token, &failure),
              "deferred-visibility fixture cleans up");
    }

    void TestPlaceholderSeam() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorPanelRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;

        editor::EditorPanelHandle placeholder;
        Check(registry.CreatePlaceholder({40, 400, "Missing panel", true}, placeholder, &failure) && placeholder.IsValid(),
              "missing-type placeholder preserves unresolved stable identity");
        editor::EditorPanelTypeToken aliasedType;
        Check(!registry.RegisterType(MultipleType(40, "aliased"), aliasedType, &failure) &&
                  failure.code == editor::EditorUiFailureCode::DuplicateIdentity && !aliasedType.IsValid(),
              "registered type cannot alias a live unresolved placeholder type");
        PanelObservation observation;
        registry.VisitPanels(ObservePanel, &observation);
        Check(observation.count == 1 && observation.type == 40 && observation.id == 400 &&
                  observation.handle == placeholder && observation.open && observation.placeholder,
              "placeholder is bounded registry state with unresolved type and instance IDs");
        AuthoringObservation authoring;
        registry.VisitOpenForAuthoring(ObserveAuthoring, &authoring);
        Check(authoring.placeholders == 1 && authoring.ordinary == 0 && authoring.placeholderHasNoCallbacks,
              "placeholder authoring view exposes no extension state or callbacks");
        Check(registry.SetOpen(placeholder, false, &failure) && !registry.IsOpen(placeholder) &&
                  registry.DestroyPanel(placeholder, &failure),
              "placeholder supports ordinary close and destroy without extension callbacks");

        editor::EditorPanelTypeToken type;
        Check(registry.RegisterType(MultipleType(40, "resolved"), type, &failure), "real type with former unresolved ID registers");
        editor::EditorPanelHandle alias;
        Check(!registry.CreatePlaceholder({40, 401, "alias", true}, alias, &failure) &&
                  failure.code == editor::EditorUiFailureCode::DuplicateIdentity && !alias.IsValid(),
              "placeholder cannot alias a currently registered real type");
        Check(registry.UnregisterType(type, &failure), "placeholder alias fixture cleans up");
    }

    void TestUserStateRevisionAndCapture() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorPanelRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;

        const u64 initialRevision = registry.GetUserStateRevision();
        Check(initialRevision != 0, "panel user-state revision starts at a nonzero baseline");

        editor::EditorPanelTypeToken type;
        Check(registry.RegisterType(MultipleType(90, "current type"), type, &failure) &&
                  registry.GetUserStateRevision() == initialRevision,
              "panel type registration alone does not dirty persistent panel inventory");

        editor::EditorPanelHandle high;
        editor::EditorPanelHandle low;
        Check(registry.CreatePanel({902, 90, "Zulu", true}, high, &failure),
              "user-state fixture creates first ordinary panel");
        const u64 afterFirstCreate = registry.GetUserStateRevision();
        Check(afterFirstCreate == initialRevision + 1u,
              "effective ordinary panel creation advances user-state revision once");
        Check(registry.CreatePanel({901, 90, "Alpha", false}, low, &failure) &&
                  registry.GetUserStateRevision() == afterFirstCreate + 1u,
              "second ordinary panel creation advances user-state revision once");

        editor::EditorPanelHandle placeholder;
        const u64 beforePlaceholder = registry.GetUserStateRevision();
        Check(registry.CreatePlaceholder({99, 999, "Missing", true}, placeholder, &failure) &&
                  registry.GetUserStateRevision() == beforePlaceholder + 1u,
              "placeholder creation advances user-state revision once");

        const u64 beforeNoOp = registry.GetUserStateRevision();
        Check(registry.SetOpen(high, true, &failure) && registry.GetUserStateRevision() == beforeNoOp,
              "no-op ready visibility write does not advance user-state revision");
        Check(registry.SetOpen(high, false, &failure) && registry.GetUserStateRevision() == beforeNoOp + 1u,
              "effective ready visibility write advances user-state revision once");

        state = editor::EditorUiState::BuildingFrame;
        const u64 beforeDeferred = registry.GetUserStateRevision();
        Check(registry.SetOpen(high, true, &failure) &&
                  registry.SetOpen(high, true, &failure) &&
                  registry.GetUserStateRevision() == beforeDeferred,
              "deferred visibility writes do not advance revision before commit");
        registry.CommitPendingVisibility();
        Check(registry.GetUserStateRevision() == beforeDeferred + 1u,
              "committing one effective deferred visibility change advances revision once");
        state = editor::EditorUiState::Ready;

        detail::EditorPanelUserStateRecord sentinel[3]{};
        sentinel[0].instance = 0xdeadbeefull;
        u32 capturedCount = 0;
        Check(!registry.CaptureUserState(sentinel, 2, capturedCount, &failure) &&
                  failure.code == editor::EditorUiFailureCode::CapacityExceeded &&
                  capturedCount == 3 && sentinel[0].instance == 0xdeadbeefull,
              "insufficient panel user-state capture reports exact count and copies no prefix");

        detail::EditorPanelUserStateRecord captured[3]{};
        Check(registry.CaptureUserState(captured, 3, capturedCount, &failure) && capturedCount == 3,
              "bounded panel user-state capture succeeds with exact capacity");
        Check(captured[0].instance == 901 && captured[0].type == 90 && !captured[0].open &&
                  captured[0].fallbackTitleByteCount == 5 &&
                  std::strcmp(captured[0].fallbackTitle, "Alpha") == 0 &&
                  captured[1].instance == 902 && captured[1].type == 90 && captured[1].open &&
                  captured[1].fallbackTitleByteCount == 4 &&
                  std::strcmp(captured[1].fallbackTitle, "Zulu") == 0 &&
                  captured[2].instance == 999 && captured[2].type == 99 && captured[2].open &&
                  std::strcmp(captured[2].fallbackTitle, "Missing") == 0,
              "panel capture is canonical by stable instance and preserves exact current title/open state");

        const u64 beforeDestroy = registry.GetUserStateRevision();
        Check(registry.DestroyPanel(low, &failure) && registry.GetUserStateRevision() == beforeDestroy + 1u,
              "reachable panel destruction advances user-state revision once");
        Check(registry.DestroyPanel(high, &failure) && registry.DestroyPanel(placeholder, &failure) &&
                  registry.UnregisterType(type, &failure),
              "panel user-state fixture cleans up");
    }

    void TestGenerationExhaustionAndFreshCoreStaleness() noexcept
    {
        editor::EditorUiFailure failure;

        {
            editor::EditorUiState state = editor::EditorUiState::Ready;
            LocalGenerationSource generations(~u32{0} - 1u, 0);
            detail::EditorPanelRegistryCore registry(state, generations.Get());
            editor::EditorPanelTypeToken terminal;
            Check(registry.RegisterType(MultipleType(50, "terminal"), terminal, &failure) &&
                      terminal.generation == ~u32{0} && registry.UnregisterType(terminal, &failure),
                  "last nonzero panel type generation is usable exactly once");
            editor::EditorPanelTypeToken exhausted;
            Check(!registry.RegisterType(MultipleType(51, "exhausted"), exhausted, &failure) &&
                      failure.code == editor::EditorUiFailureCode::GenerationExhausted && !exhausted.IsValid(),
                  "panel type generation exhaustion fails instead of wrapping");
            editor::EditorPanelHandle absent;
            Check(!registry.CreatePanel({510, 51, "absent", true}, absent, &failure) && !absent.IsValid(),
                  "generation-exhausted type registration leaves no partial active record");
        }

        {
            editor::EditorUiState state = editor::EditorUiState::Ready;
            LocalGenerationSource generations(0, ~u32{0} - 1u);
            detail::EditorPanelRegistryCore registry(state, generations.Get());
            PanelFixture fixture;
            editor::EditorPanelTypeToken token;
            Check(registry.RegisterType(MultipleType(52, "instanceTerminal", &fixture), token, &failure),
                  "instance-exhaustion fixture type registers");
            editor::EditorPanelHandle terminal;
            Check(registry.CreatePanel({520, 52, "terminal", true}, terminal, &failure) &&
                      terminal.generation == ~u32{0} && fixture.createCalls == 1 &&
                      registry.DestroyPanel(terminal, &failure),
                  "last panel instance generation is usable exactly once");
            const u32 before = fixture.createCalls;
            editor::EditorPanelHandle exhausted;
            Check(!registry.CreatePanel({521, 52, "exhausted", true}, exhausted, &failure) &&
                      failure.code == editor::EditorUiFailureCode::GenerationExhausted && !exhausted.IsValid() &&
                      fixture.createCalls == before,
                  "instance generation exhaustion fails before factory invocation or partial record mutation");
            Check(registry.UnregisterType(token, &failure), "instance-exhaustion fixture type unregisters");
        }

        LocalGenerationSource sharedGenerations;
        editor::EditorPanelTypeToken oldType;
        editor::EditorPanelHandle oldPanel;
        {
            editor::EditorUiState firstState = editor::EditorUiState::Ready;
            detail::EditorPanelRegistryCore first(firstState, sharedGenerations.Get());
            Check(first.RegisterType(MultipleType(53, "freshCore"), oldType, &failure) &&
                      first.CreatePanel({530, 53, "panel", true}, oldPanel, &failure),
                  "first registry core creates stable type and instance identities");
        }
        {
            editor::EditorUiState secondState = editor::EditorUiState::Ready;
            detail::EditorPanelRegistryCore second(secondState, sharedGenerations.Get());
            editor::EditorPanelTypeToken newType;
            editor::EditorPanelHandle newPanel;
            Check(second.RegisterType(MultipleType(53, "freshCore"), newType, &failure) &&
                      second.CreatePanel({530, 53, "panel", true}, newPanel, &failure),
                  "fresh registry core can recreate the same stable IDs");
            Check(newType != oldType && newType.generation != oldType.generation &&
                      newPanel != oldPanel && newPanel.generation != oldPanel.generation,
                  "fresh registry core does not alias stale type tokens or instance handles");
            Check(!second.UnregisterType(oldType, &failure) && failure.code == editor::EditorUiFailureCode::InvalidHandle &&
                      !second.SetOpen(oldPanel, false, &failure) && failure.code == editor::EditorUiFailureCode::InvalidHandle,
                  "stale identities from a prior core cannot mutate the fresh core");
        }

        editor::EditorPanelTypeToken productionOldType;
        editor::EditorPanelHandle productionOldPanel;
        {
            editor::EditorUiState firstState = editor::EditorUiState::Ready;
            detail::EditorPanelRegistryCore first(firstState, detail::GetProductionEditorPanelGenerationSource());
            Check(first.RegisterType(MultipleType(54, "productionFreshCore"), productionOldType, &failure) &&
                      first.CreatePanel({540, 54, "panel", true}, productionOldPanel, &failure),
                  "first production-generation registry core creates stable identities");
        }
        {
            editor::EditorUiState secondState = editor::EditorUiState::Ready;
            detail::EditorPanelRegistryCore second(secondState, detail::GetProductionEditorPanelGenerationSource());
            editor::EditorPanelTypeToken productionNewType;
            editor::EditorPanelHandle productionNewPanel;
            Check(second.RegisterType(MultipleType(54, "productionFreshCore"), productionNewType, &failure) &&
                      second.CreatePanel({540, 54, "panel", true}, productionNewPanel, &failure),
                  "fresh registry core using production generation sources recreates stable IDs");
            Check(productionNewType.generation != productionOldType.generation &&
                      productionNewPanel.generation != productionOldPanel.generation,
                  "production process-static generation sources keep fresh-core identities stale-safe");
        }
    }

    void TestCapacityBounds() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorPanelRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;
        editor::EditorPanelTypeToken types[editor::MaximumEditorPanelTypes]{};

        for (u32 index = 0; index < editor::MaximumEditorPanelTypes; ++index)
            Check(registry.RegisterType(MultipleType(1000u + index, "capacityType"), types[index], &failure),
                  "panel type capacity admits every bounded slot");
        editor::EditorPanelTypeToken excessType;
        Check(!registry.RegisterType(MultipleType(9000, "excessType"), excessType, &failure) &&
                  failure.code == editor::EditorUiFailureCode::CapacityExceeded && !excessType.IsValid(),
              "panel type capacity rejects one extra registration");
        for (u32 index = 1; index < editor::MaximumEditorPanelTypes; ++index)
            Check(registry.UnregisterType(types[index], &failure), "unused capacity type unregisters");

        editor::EditorPanelHandle panels[editor::MaximumEditorPanelInstances]{};
        for (u32 index = 0; index < editor::MaximumEditorPanelInstances; ++index)
            Check(registry.CreatePanel({10000u + index, 1000, "capacityPanel", true}, panels[index], &failure),
                  "panel instance capacity admits every bounded slot");
        editor::EditorPanelHandle excessPanel;
        Check(!registry.CreatePanel({20000, 1000, "excessPanel", true}, excessPanel, &failure) &&
                  failure.code == editor::EditorUiFailureCode::CapacityExceeded && !excessPanel.IsValid(),
              "panel instance capacity rejects one extra instance");
        for (const editor::EditorPanelHandle panel : panels)
            Check(registry.DestroyPanel(panel, &failure), "capacity panel destroys cleanly");
        Check(registry.UnregisterType(types[0], &failure), "capacity fixture final type unregisters");
    }

    class WrongThreadMutationWorker final : public vanguard::concurrency::Thread
    {
    public:
        WrongThreadMutationWorker(detail::EditorPanelRegistryCore& registry, const editor::EditorPanelTypeToken liveType,
                                  const editor::EditorPanelHandle livePanel) noexcept
            : Thread("panelWrongThread"), m_registry(registry), m_liveType(liveType), m_livePanel(livePanel)
        {
        }

        void ThreadFunction() noexcept override
        {
            editor::EditorUiFailure failure;
            editor::EditorPanelTypeToken token;
            m_registerResult = m_registry.RegisterType(MultipleType(61, "wrongThreadType"), token, &failure);
            m_registerFailure = failure.code;
            editor::EditorPanelHandle placeholder;
            m_placeholderResult = m_registry.CreatePlaceholder({62, 612, "wrongThreadPlaceholder", true}, placeholder, &failure);
            m_placeholderFailure = failure.code;
            m_unregisterResult = m_registry.UnregisterType(m_liveType, &failure);
            m_unregisterFailure = failure.code;
            editor::EditorPanelHandle created;
            m_createResult = m_registry.CreatePanel({611, 60, "wrongThreadCreate", true}, created, &failure);
            m_createFailure = failure.code;
            m_destroyResult = m_registry.DestroyPanel(m_livePanel, &failure);
            m_destroyFailure = failure.code;
            m_setOpenResult = m_registry.SetOpen(m_livePanel, false, &failure);
            m_setOpenFailure = failure.code;
        }

        [[nodiscard]] bool AllRejectedAsWrongThread() const noexcept
        {
            return !m_registerResult && !m_placeholderResult && !m_unregisterResult && !m_createResult && !m_destroyResult && !m_setOpenResult &&
                   m_registerFailure == editor::EditorUiFailureCode::WrongThread &&
                   m_placeholderFailure == editor::EditorUiFailureCode::WrongThread &&
                   m_unregisterFailure == editor::EditorUiFailureCode::WrongThread &&
                   m_createFailure == editor::EditorUiFailureCode::WrongThread &&
                   m_destroyFailure == editor::EditorUiFailureCode::WrongThread &&
                   m_setOpenFailure == editor::EditorUiFailureCode::WrongThread;
        }

    private:
        detail::EditorPanelRegistryCore& m_registry;
        editor::EditorPanelTypeToken m_liveType;
        editor::EditorPanelHandle m_livePanel;
        editor::EditorUiFailureCode m_registerFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode m_placeholderFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode m_unregisterFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode m_createFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode m_destroyFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiFailureCode m_setOpenFailure = editor::EditorUiFailureCode::None;
        bool m_registerResult = false;
        bool m_placeholderResult = false;
        bool m_unregisterResult = false;
        bool m_createResult = false;
        bool m_destroyResult = false;
        bool m_setOpenResult = false;
    };

    void TestWrongThreadMutation() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorPanelRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;
        PanelFixture fixture;
        editor::EditorPanelTypeToken liveType;
        editor::EditorPanelHandle livePanel;
        Check(registry.RegisterType(MultipleType(60, "liveType", &fixture), liveType, &failure) &&
                  registry.CreatePanel({610, 60, "livePanel", true}, livePanel, &failure),
              "wrong-thread fixture starts with live type and panel");

        WrongThreadMutationWorker worker(registry, liveType, livePanel);
        worker.InitThread();
        worker.JoinThread();
        Check(worker.AllRejectedAsWrongThread(),
              "all direct panel-registry mutation paths reject worker-thread callers before mutation");
        Check(registry.IsOpen(livePanel) && fixture.destroyCalls == 0,
              "wrong-thread destroy/visibility attempts do not mutate the live instance");

        editor::EditorPanelTypeToken workerType;
        Check(registry.RegisterType(MultipleType(61, "wrongThreadType"), workerType, &failure),
              "wrong-thread type registration left no partial record");
        Check(registry.DestroyPanel(livePanel, &failure) && fixture.destroyCalls == 1 &&
                  registry.UnregisterType(liveType, &failure) && registry.UnregisterType(workerType, &failure),
              "wrong-thread fixture cleans up on the main thread");
    }

    void TestOwnedTypeRevocationAndCleanup() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorPanelRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;
        PanelFixture ownedFixture;
        PanelFixture unownedFixture;
        const editor::EditorExtensionOwner owner{{7001}, 11};
        const editor::EditorExtensionOwner otherOwner{{7002}, 11};

        editor::EditorPanelTypeToken unownedType;
        editor::EditorPanelHandle unownedPanel;
        Check(registry.RegisterType(MultipleType(70, "unowned", &unownedFixture), unownedType, &failure) &&
                  registry.CreatePanel({700, 70, "unowned panel", true}, unownedPanel, &failure),
              "owner fixture preserves an ordinary unowned panel alongside owned state");

        editor::EditorPanelTypeToken ownedType;
        Check(editor::testing::EditorExtensionRegistrationTestAdapter::RegisterOwnedPanelType(
                  registry, owner, MultipleType(71, "owned", &ownedFixture), ownedType, &failure) && ownedType.IsValid(),
              "owned panel type registers through the private owner-stamped seam");
        editor::EditorPanelHandle ownedPanel;
        editor::EditorPanelHandle ownedPanelSecond;
        Check(registry.CreatePanel({710, 71, "owned panel", true}, ownedPanel, &failure) &&
                  registry.CreatePanel({711, 71, "owned panel second", true}, ownedPanelSecond, &failure) &&
                  ownedFixture.createCalls == 2u && registry.IsOpen(ownedPanel) && registry.IsOpen(ownedPanelSecond),
              "instances created from an owned type inherit live owner reachability");
        state = editor::EditorUiState::BuildingFrame;
        OwnedAuthoringObservation authored{710};
        registry.VisitOpenForAuthoring(ObserveOwnedAuthoring, &authored);
        Check(authored.found && authored.owner == owner,
              "private panel authoring view carries the exact inherited extension owner without exposing a public owner lookup");
        state = editor::EditorUiState::Ready;
        Check(!registry.UnregisterType(ownedType, &failure) && failure.code == editor::EditorUiFailureCode::OwnerRevoked,
              "public unowned unregister cannot physically remove an extension-owned type");
        Check(!editor::testing::EditorExtensionRegistrationTestAdapter::UnregisterOwnedPanelType(registry, otherOwner, ownedType, &failure) &&
                  failure.code == editor::EditorUiFailureCode::OwnerRevoked && ownedFixture.destroyCalls == 0u,
              "wrong owner cannot remove another extension's exact type token or destroy its instances");

        const u64 beforeRevokeRevision = registry.GetUserStateRevision();
        editor::testing::EditorExtensionRegistrationTestAdapter::RevokePanelOwner(registry, owner);
        Check(registry.GetUserStateRevision() == beforeRevokeRevision + 2u,
              "owner revoke advances user-state revision once for each logically removed reachable panel");
        PanelObservation visible;
        registry.VisitPanels(ObservePanel, &visible);
        Check(!registry.IsOpen(ownedPanel) && !registry.IsOpen(ownedPanelSecond) && visible.count == 1u && visible.handle == unownedPanel &&
                  ownedFixture.destroyCalls == 0u,
              "logical owner revoke hides owned instance routes without physical destruction and leaves unowned state reachable");
        editor::EditorPanelHandle physicalAlias;
        Check(!registry.CreatePanel({710, 70, "physical alias", true}, physicalAlias, &failure) &&
                  failure.code == editor::EditorUiFailureCode::DuplicateIdentity && !physicalAlias.IsValid(),
              "revoked physical instance identity remains reserved until owned cleanup completes");
        editor::EditorPanelHandle rejected;
        Check(!registry.CreatePanel({712, 71, "late owned panel", true}, rejected, &failure) && !rejected.IsValid(),
              "revoked owned type cannot create another ordinary instance");
        detail::EditorPanelUserStateRecord revokedRestore;
        revokedRestore.type = 71;
        revokedRestore.instance = 712;
        revokedRestore.open = true;
        revokedRestore.fallbackTitleByteCount = 7;
        std::memcpy(revokedRestore.fallbackTitle, "revoked", 7);
        detail::EditorPanelUserStateRestoreStatus revokedRestoreStatus;
        Check(registry.RestoreUserState({&revokedRestore, 1}, revokedRestoreStatus, &failure) &&
                  revokedRestoreStatus.skipped == 1 && revokedRestoreStatus.placeholders == 0 &&
                  revokedRestoreStatus.firstSkippedFailure.code == editor::EditorUiFailureCode::OwnerRevoked,
              "registered but logically revoked type is diagnosed and skipped rather than disguised as a missing-type placeholder");

        const u64 afterLogicalRevoke = registry.GetUserStateRevision();
        Check(editor::testing::EditorExtensionRegistrationTestAdapter::UnregisterOwnedPanelType(registry, owner, ownedType, &failure) &&
                  ownedFixture.destroyCalls == 2u && registry.GetUserStateRevision() == afterLogicalRevoke,
              "privileged exact-token cleanup destroys every owned instance before removing the owned type");
        Check(!editor::testing::EditorExtensionRegistrationTestAdapter::UnregisterOwnedPanelType(registry, owner, ownedType, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidHandle,
              "repeated or stale owned cleanup cannot target a removed type token");
        Check(registry.IsOpen(unownedPanel) && unownedFixture.destroyCalls == 0u,
              "owner cleanup does not disturb unrelated unowned panel state");
        Check(registry.DestroyPanel(unownedPanel, &failure) && registry.UnregisterType(unownedType, &failure),
              "owner fixture ordinary state cleans up through the public path");
    }

    void TestFocusInvalidationTransactions() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        FocusInvalidationFixture focus;
        detail::EditorPanelRegistryCore registry(state, generations.Get(), InvalidateFocus, &focus);
        PanelFixture panelFixture;
        editor::EditorUiFailure failure;
        editor::EditorPanelTypeToken type;
        editor::EditorPanelHandle panel;

        Check(registry.RegisterType(MultipleType(80, "focus", &panelFixture), type, &failure) &&
                  registry.CreatePanel({800, 80, "focused panel", true}, panel, &failure),
              "focus invalidation fixture creates a live panel");
        focus.focused = panel;
        focus.reject = true;
        Check(!registry.SetOpen(panel, false, &failure) &&
                  failure.code == editor::EditorUiFailureCode::FocusSynchronizationFailure &&
                  registry.IsOpen(panel) && panelFixture.destroyCalls == 0u,
              "ready-phase close fails transactionally when focused Local context cannot be removed");
        Check(!registry.DestroyPanel(panel, &failure) &&
                  failure.code == editor::EditorUiFailureCode::FocusSynchronizationFailure &&
                  registry.IsOpen(panel) && panelFixture.destroyCalls == 0u,
              "focused destroy preserves panel state when focus synchronization fails");

        focus.reject = false;
        Check(registry.SetOpen(panel, false, &failure) && !registry.IsOpen(panel),
              "focused close commits after focus synchronization succeeds");
        Check(registry.SetOpen(panel, true, &failure) && registry.DestroyPanel(panel, &failure) &&
                  panelFixture.destroyCalls == 1u && registry.UnregisterType(type, &failure),
              "focus invalidation fixture destroys state only after the focus bridge admits removal");
        Check(focus.calls >= 3u, "focus invalidation callback observes close/destroy lifetime boundaries");
    }

    void TestGenericPersistentStateContract() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorPanelRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;

        editor::EditorPanelTypeDesc incomplete = MultipleType(90, "incomplete persistence");
        incomplete.persistence.stateVersion = 1;
        editor::EditorPanelTypeToken rejectedType;
        Check(!registry.RegisterType(incomplete, rejectedType, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidDescriptor,
              "panel persistence descriptor requires capture/restore/revision callbacks when versioned state is enabled");

        PersistentPanelFixture fixture;
        editor::EditorPanelTypeToken type;
        editor::EditorPanelHandle panel;
        Check(registry.RegisterType(PersistentType(91, "persistent", fixture), type, &failure) &&
                  registry.CreatePanel({910, 91, "persistent panel", true}, panel, &failure),
              "versioned panel persistence provider registers and creates a stable instance");
        fixture.registry = &registry;
        fixture.handle = panel;

        u8 captured[16]{};
        const detail::EditorPanelPersistentStateCapture presentation = registry.CapturePersistentState(
            91, 910, editor::EditorPanelStateScope::Presentation, captured, sizeof(captured), &failure);
        Check(presentation.result == editor::EditorPanelStateResult::Success &&
                  presentation.version == 2 && presentation.byteCount == fixture.presentationBytes &&
                  std::memcmp(captured, fixture.presentation, fixture.presentationBytes) == 0,
              "generic panel capture returns current version plus opaque presentation bytes");

        fixture.noContentState = true;
        const detail::EditorPanelPersistentStateCapture noContent = registry.CapturePersistentState(
            91, 910, editor::EditorPanelStateScope::Content, captured, sizeof(captured), &failure);
        Check(noContent.result == editor::EditorPanelStateResult::NoState &&
                  noContent.version == 2 && noContent.byteCount == 0,
              "provider can independently decline one persistence scope while still reporting its current schema version");
        fixture.noContentState = false;

        const u8 currentBytes[]{9, 8, 7};
        u8 migration[16]{};
        Check(registry.RestorePersistentState(
                  91, 910, editor::EditorPanelStateScope::Content, 2,
                  {currentBytes, static_cast<u32>(sizeof(currentBytes))}, migration, sizeof(migration), &failure) ==
                  editor::EditorPanelStateResult::Success &&
                  fixture.restoreCalls == 1 && fixture.migrateCalls == 0 && fixture.restoredVersion == 2 &&
                  fixture.restoredScope == editor::EditorPanelStateScope::Content &&
                  fixture.restoredBytes == sizeof(currentBytes) &&
                  std::memcmp(fixture.restored, currentBytes, sizeof(currentBytes)) == 0,
              "current-version panel payload restores directly through the live provider");

        const u8 oldBytes[]{6, 5};
        fixture.attemptDestroyOnMigrate = true;
        Check(registry.RestorePersistentState(
                  91, 910, editor::EditorPanelStateScope::Presentation, 1,
                  {oldBytes, static_cast<u32>(sizeof(oldBytes))}, migration, sizeof(migration), &failure) ==
                  editor::EditorPanelStateResult::Success &&
                  fixture.restoreCalls == 2 && fixture.migrateCalls == 1 && fixture.restoredVersion == 2 &&
                  fixture.restoredScope == editor::EditorPanelStateScope::Presentation &&
                  fixture.restoredBytes == sizeof(oldBytes) + 1u && fixture.restored[2] == 0x7fu &&
                  !fixture.reentrantDestroySucceeded &&
                  fixture.reentrantFailure == editor::EditorUiFailureCode::WrongPhase && registry.IsOpen(panel),
              "older panel payload migrates in caller-owned scratch while reentrant topology mutation is rejected");

        const u64 beforeInitialRefresh = registry.GetUserStateRevision();
        fixture.attemptDestroyOnRevision = true;
        fixture.reentrantDestroySucceeded = false;
        fixture.reentrantFailure = editor::EditorUiFailureCode::None;
        Check(registry.RefreshPersistentStateRevisions(&failure) &&
                  registry.GetUserStateRevision() == beforeInitialRefresh + 1u &&
                  !fixture.reentrantDestroySucceeded &&
                  fixture.reentrantFailure == editor::EditorUiFailureCode::WrongPhase && registry.IsOpen(panel),
              "first provider revision sample joins the central revision while reentrant topology mutation is rejected");
        const u64 afterInitialRefresh = registry.GetUserStateRevision();
        Check(registry.RefreshPersistentStateRevisions(&failure) &&
                  registry.GetUserStateRevision() == afterInitialRefresh,
              "unchanged provider revision does not dirty persistence");
        fixture.revision = 2;
        Check(registry.RefreshPersistentStateRevisions(&failure) &&
                  registry.GetUserStateRevision() == afterInitialRefresh + 1u,
              "panel-local persistent-state change advances the existing central dirty revision");
        fixture.revision = 1;
        const u64 beforeRevisionFault = registry.GetUserStateRevision();
        Check(registry.RefreshPersistentStateRevisions(&failure) &&
                  registry.GetUserStateRevision() == beforeRevisionFault &&
                  registry.CapturePersistentState(
                      91, 910, editor::EditorPanelStateScope::Presentation,
                      captured, sizeof(captured), &failure).result == editor::EditorPanelStateResult::CallbackFailure,
              "provider revision regression is isolated to that panel and keeps the central dirty stream writable");
        fixture.revision = 0;
        Check(registry.RefreshPersistentStateRevisions(&failure) &&
                  registry.GetUserStateRevision() == beforeRevisionFault,
              "provider revision zero is a soft unavailable sample rather than a global persistence failure");
        fixture.revision = 3;
        Check(registry.RefreshPersistentStateRevisions(&failure) &&
                  registry.GetUserStateRevision() == beforeRevisionFault + 1u &&
                  registry.CapturePersistentState(
                      91, 910, editor::EditorPanelStateScope::Presentation,
                      captured, sizeof(captured), &failure).result == editor::EditorPanelStateResult::Success,
              "provider can recover monotonically after soft revision faults and resume capture");

        PersistentPanelFixture compatibleFixture;
        compatibleFixture.expectedInstance = 930;
        compatibleFixture.acceptLegacyVersion = true;
        editor::EditorPanelTypeDesc compatibleDesc = PersistentType(93, "compatible persistent", compatibleFixture);
        compatibleDesc.persistence.migrate = nullptr;
        editor::EditorPanelTypeToken compatibleType;
        editor::EditorPanelHandle compatiblePanel;
        Check(registry.RegisterType(compatibleDesc, compatibleType, &failure) &&
                  registry.CreatePanel({930, 93, "compatible persistent panel", true}, compatiblePanel, &failure) &&
                  registry.RestorePersistentState(
                      93, 930, editor::EditorPanelStateScope::Presentation, 1,
                      {oldBytes, static_cast<u32>(sizeof(oldBytes))}, migration, sizeof(migration), &failure) ==
                      editor::EditorPanelStateResult::Success &&
                  compatibleFixture.restoreCalls == 1 && compatibleFixture.migrateCalls == 0 &&
                  compatibleFixture.restoredVersion == 1,
              "older compatible payload is offered directly to RestoreState when no migration callback is registered");
        Check(registry.DestroyPanel(compatiblePanel, &failure) &&
                  registry.UnregisterType(compatibleType, &failure) && compatibleFixture.destroyCalls == 1,
              "compatible no-migrate provider tears down through the ordinary registry lifecycle");

        PersistentPanelFixture ownedFixture;
        ownedFixture.expectedInstance = 940;
        ownedFixture.registry = &registry;
        ownedFixture.owner = {{0x7065727369737401ull}, 7u};
        editor::EditorPanelTypeToken ownedType;
        editor::EditorPanelHandle ownedPanel;
        Check(editor::testing::EditorExtensionRegistrationTestAdapter::RegisterOwnedPanelType(
                  registry, ownedFixture.owner, PersistentType(94, "owned persistent", ownedFixture),
                  ownedType, &failure) &&
                  registry.CreatePanel({940, 94, "owned persistent panel", true}, ownedPanel, &failure),
              "owned persistent-state provider registers through the shell owner seam");
        ownedFixture.handle = ownedPanel;
        Check(editor::testing::EditorExtensionRegistrationTestAdapter::CanRevokePanelOwner(
                  registry, ownedFixture.owner),
              "owned persistent provider can be revoked before entering a provider callback");
        ownedFixture.attemptOwnerRevokeOnRevision = true;
        Check(registry.RefreshPersistentStateRevisions(&failure) &&
                  !ownedFixture.reentrantOwnerRevokeAdmitted && registry.IsOpen(ownedPanel) &&
                  editor::testing::EditorExtensionRegistrationTestAdapter::CanRevokePanelOwner(
                      registry, ownedFixture.owner),
              "owner revoke preflight rejects reentrant teardown transactionally while provider state is borrowed");
        editor::testing::EditorExtensionRegistrationTestAdapter::RevokePanelOwner(registry, ownedFixture.owner);
        Check(!registry.IsOpen(ownedPanel) &&
                  editor::testing::EditorExtensionRegistrationTestAdapter::UnregisterOwnedPanelType(
                      registry, ownedFixture.owner, ownedType, &failure) &&
                  ownedFixture.destroyCalls == 1,
              "owned persistent provider revokes and destroys normally after the callback returns");

        editor::EditorPanelHandle placeholder;
        Check(registry.CreatePlaceholder({92, 920, "missing provider", true}, placeholder, &failure),
              "missing-provider fixture creates an ordinary persisted placeholder");
        const detail::EditorPanelPersistentStateCapture missingCapture = registry.CapturePersistentState(
            92, 920, editor::EditorPanelStateScope::Presentation, captured, sizeof(captured), &failure);
        Check(missingCapture.result == editor::EditorPanelStateResult::ProviderUnavailable &&
                  registry.RestorePersistentState(
                      92, 920, editor::EditorPanelStateScope::Presentation, 1,
                      {oldBytes, static_cast<u32>(sizeof(oldBytes))}, migration, sizeof(migration), &failure) ==
                      editor::EditorPanelStateResult::ProviderUnavailable,
              "missing panel/plugin reports provider-unavailable so the central service can retain opaque bytes unchanged");

        Check(registry.DestroyPanel(placeholder, &failure) && registry.DestroyPanel(panel, &failure) &&
                  registry.UnregisterType(type, &failure) && fixture.destroyCalls == 1,
              "persistent provider fixture tears down through the ordinary registry lifecycle");
    }
} // namespace

int main()
{
    vanguard::concurrency::InitializeMainThread();
    Check(memory::Initialize(), "memory initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    TestDescriptorTokenAndLifetime();
    TestSingletonAndMultiPolicy();
    TestDeferredVisibilityTransaction();
    TestPlaceholderSeam();
    TestUserStateRevisionAndCapture();
    TestGenerationExhaustionAndFreshCoreStaleness();
    TestCapacityBounds();
    TestWrongThreadMutation();
    TestOwnedTypeRevocationAndCleanup();
    TestFocusInvalidationTransactions();
    TestGenericPersistentStateContract();

    if (g_failures == 0)
        std::printf("Vanguard editor bounded panel registry ownership/focus tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}

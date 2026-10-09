#include <vanguard/editor/editor_activity.hpp>
#include <vanguard/editor/editor_activity_service.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/memory/memory.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <thread>

namespace
{
    namespace app = vanguard::application;
    namespace diagnostics = vanguard::diagnostics;
    namespace editor = vanguard::editor;
    namespace detail = vanguard::editor::detail;
    namespace engine = vanguard::engine;
    namespace memory = vanguard::memory;
    using vanguard::u32;
    using vanguard::u8;
    using vanguard::u64;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[editorActivityTests] FAILED: %s\n", message);
        ++g_failures;
    }

    struct FakeDiagnostics final
    {
        diagnostics::SinkCallback callback = nullptr;
        void* userData = nullptr;
        diagnostics::SinkRegistrationToken token;
        u64 nextGeneration = 0;
        u32 registerCalls = 0;
        u32 unregisterCalls = 0;
        bool live = false;
        bool failUnregister = false;
    };

    FakeDiagnostics* g_fakeDiagnostics = nullptr;

    diagnostics::SinkRegistrationResult FakeRegister(const diagnostics::SinkCallback callback, void* const userData,
                                                     diagnostics::SinkRegistrationToken& token) noexcept
    {
        FakeDiagnostics& fake = *g_fakeDiagnostics;
        ++fake.registerCalls;
        if (fake.live || callback == nullptr)
            return diagnostics::SinkRegistrationResult::InternalFailure;
        fake.callback = callback;
        fake.userData = userData;
        fake.token = {0u, ++fake.nextGeneration};
        fake.live = true;
        token = fake.token;
        return diagnostics::SinkRegistrationResult::Success;
    }

    diagnostics::SinkRegistrationResult FakeUnregister(const diagnostics::SinkRegistrationToken token) noexcept
    {
        FakeDiagnostics& fake = *g_fakeDiagnostics;
        ++fake.unregisterCalls;
        if (!fake.live || token.index != fake.token.index || token.generation != fake.token.generation)
            return diagnostics::SinkRegistrationResult::StaleToken;
        if (fake.failUnregister)
            return diagnostics::SinkRegistrationResult::InternalFailure;
        fake.live = false;
        fake.callback = nullptr;
        fake.userData = nullptr;
        return diagnostics::SinkRegistrationResult::Success;
    }

    bool FakeIsRegistered(const diagnostics::SinkRegistrationToken token) noexcept
    {
        const FakeDiagnostics& fake = *g_fakeDiagnostics;
        return fake.live && token.index == fake.token.index && token.generation == fake.token.generation;
    }

    detail::EditorActivityDiagnosticsRegistrationAccess FakeAccess() noexcept
    {
        return {&FakeRegister, &FakeUnregister, &FakeIsRegistered};
    }

    struct BridgeAllocation final
    {
        memory::MemoryBlock block;
        detail::EditorActivityDiagnosticsBridge* bridge = nullptr;

        BridgeAllocation() noexcept
        {
            block = memory::Allocate(memory::PoolId::Tools, sizeof(detail::EditorActivityDiagnosticsBridge),
                                     alignof(detail::EditorActivityDiagnosticsBridge));
            if (block)
                bridge = ::new (block.address) detail::EditorActivityDiagnosticsBridge();
        }

        ~BridgeAllocation()
        {
            if (bridge == nullptr)
                return;
            bridge->~EditorActivityDiagnosticsBridge();
            memory::Free(block);
        }
    };

    void Emit(FakeDiagnostics& fake, const char* const text, const u64 frame = 1) noexcept
    {
        diagnostics::MessageView message;
        message.message = text;
        message.frame = frame;
        message.threadId = 7;
        message.threadContextId = 11;
        message.networkPeerId = 13;
        message.level = diagnostics::Level::Info;
        message.category = diagnostics::Category::Tools;
        fake.callback(message, fake.userData);
    }

    enum class SourceQueryMode : u8
    {
        Success,
        EmptySuccess,
        ZeroGeneration,
        InvalidPresentation,
        FailAfterAdd,
        DuplicateItem,
        InvalidProgress,
        OverflowRows
    };

    struct ActivitySourceFixture final
    {
        detail::EditorActivityCore* core = nullptr;
        editor::EditorExtensionOwner owner{{0x501u}, 1u};
        detail::EditorActivitySourceToken token;
        editor::EditorActivitySourceId source = 0x601u;
        editor::EditorActivityItemId item = 0x701u;
        u64 itemGeneration = 1u;
        SourceQueryMode queryMode = SourceQueryMode::Success;
        editor::EditorActivityCancelResult cancelResult = editor::EditorActivityCancelResult::Accepted;
        const char* label = "activity";
        const char* detailText = "detail";
        u32 rowCount = 1u;
        bool cancellable = true;
        u32 queryCalls = 0;
        u32 cancelCalls = 0;
        bool reenterQuery = false;
        bool reenterCancel = false;
        editor::EditorActivityCancelAdmission reentrantCancelAdmission = editor::EditorActivityCancelAdmission::Queued;
        bool reentrantClear = true;
        bool reentrantRegister = true;
        bool reentrantUnregister = true;
        bool reentrantRevoke = true;
        editor::EditorActivityFailureCode reentrantClearFailure = editor::EditorActivityFailureCode::None;
        editor::EditorActivityFailureCode reentrantRegisterFailure = editor::EditorActivityFailureCode::None;
        editor::EditorActivityFailureCode reentrantUnregisterFailure = editor::EditorActivityFailureCode::None;
        editor::EditorActivityFailureCode reentrantRevokeFailure = editor::EditorActivityFailureCode::None;
        u64 readOnlyGenerationDuringCallback = ~u64{0};
        u64 readOnlyStatusGenerationDuringCallback = ~u64{0};
        u32 readOnlyDiagnosticsRetainedDuringCallback = ~u32{0};
        u32 readOnlyDiagnosticsIngressDuringCallback = ~u32{0};
        u32 readOnlyDiagnosticCaptureCountDuringCallback = ~u32{0};
    };

    bool QueryActivitySource(detail::EditorActivitySourceWriter& writer, void* const userData,
                             editor::EditorActivityFailure* const failure) noexcept;

    void ExerciseSourceReentrancy(ActivitySourceFixture& fixture) noexcept
    {
        if (fixture.core == nullptr)
            return;
        const editor::EditorActivityKey key{fixture.source, fixture.token.generation,
                                            fixture.item, fixture.itemGeneration};
        editor::EditorActivityFailure localFailure;
        fixture.reentrantCancelAdmission = fixture.core->RequestCancel(key, &localFailure);
        fixture.reentrantClear = fixture.core->ClearDiagnostics(&localFailure);
        fixture.reentrantClearFailure = localFailure.code;

        detail::EditorActivitySourceToken untouched{7u, 42u};
        detail::EditorActivitySourceDescriptor nested;
        nested.id = fixture.source + 1000u;
        nested.displayName = "nested";
        nested.query = &QueryActivitySource;
        nested.userData = &fixture;
        fixture.reentrantRegister =
            fixture.core->RegisterSourceForOwner(fixture.owner, nested, untouched, &localFailure);
        fixture.reentrantRegisterFailure = localFailure.code;
        fixture.reentrantUnregister =
            fixture.core->UnregisterSourceForOwner(fixture.owner, fixture.token, &localFailure);
        fixture.reentrantUnregisterFailure = localFailure.code;
        fixture.reentrantRevoke = fixture.core->RevokeSourceOwner(fixture.owner, &localFailure);
        fixture.reentrantRevokeFailure = localFailure.code;

        editor::EditorActivitySourceSnapshot sources[4]{};
        editor::EditorActivityItem items[8]{};
        editor::EditorActivityCapture capture;
        if (fixture.core->CaptureActivities({}, {sources, 4}, {items, 8}, capture, &localFailure))
            fixture.readOnlyGenerationDuringCallback = capture.cursor.activityGeneration;

        editor::EditorActivityDiagnosticRecord diagnostics[8]{};
        editor::EditorActivityDiagnosticCapture diagnosticCapture;
        if (fixture.core->CaptureDiagnostics({}, {diagnostics, 8}, diagnosticCapture, &localFailure))
            fixture.readOnlyDiagnosticCaptureCountDuringCallback = diagnosticCapture.copiedCount;
        const editor::EditorActivityServiceStatus status = fixture.core->GetStatus();
        fixture.readOnlyStatusGenerationDuringCallback = status.activityGeneration;
        fixture.readOnlyDiagnosticsRetainedDuringCallback = status.diagnosticsRetained;
        fixture.readOnlyDiagnosticsIngressDuringCallback = status.diagnosticsIngress;
    }

    bool QueryActivitySource(detail::EditorActivitySourceWriter& writer, void* const userData,
                             editor::EditorActivityFailure* const failure) noexcept
    {
        auto& fixture = *static_cast<ActivitySourceFixture*>(userData);
        ++fixture.queryCalls;
        if (fixture.reenterQuery)
            ExerciseSourceReentrancy(fixture);
        if (fixture.queryMode == SourceQueryMode::EmptySuccess)
            return true;

        detail::EditorActivityItemDescriptor item;
        item.id = fixture.item;
        item.generation = fixture.itemGeneration;
        item.kind = editor::EditorActivityKind::Progress;
        item.state = editor::EditorActivityState::Running;
        item.attention = editor::EditorActivityAttention::Info;
        item.completed = 1u;
        item.total = 4u;
        item.determinate = true;
        item.cancellable = fixture.cancellable;
        item.label = fixture.label;
        item.detail = fixture.detailText;

        if (fixture.queryMode == SourceQueryMode::ZeroGeneration)
            item.generation = 0;
        if (fixture.queryMode == SourceQueryMode::InvalidPresentation)
            item.state = static_cast<editor::EditorActivityState>(0xffu);
        if (fixture.queryMode == SourceQueryMode::InvalidProgress)
            item.total = 0;
        if (!writer.Add(item, failure))
            return fixture.queryMode != SourceQueryMode::FailAfterAdd;

        if (fixture.queryMode == SourceQueryMode::DuplicateItem)
            static_cast<void>(writer.Add(item, failure));
        else if (fixture.queryMode == SourceQueryMode::OverflowRows)
        {
            fixture.rowCount = editor::MaximumEditorActivityRowsPerSource + 1u;
        }
        if (fixture.queryMode != SourceQueryMode::DuplicateItem)
        {
            for (u32 index = 1u; index < fixture.rowCount; ++index)
            {
                item.id = fixture.item + index;
                if (!writer.Add(item, failure))
                    break;
            }
        }
        if (fixture.queryMode == SourceQueryMode::FailAfterAdd)
        {
            if (failure != nullptr)
                *failure = {editor::EditorActivityFailureCode::SourceQueryFailed, fixture.source,
                            fixture.item, "fixture source query failure"};
            return false;
        }
        return true;
    }

    editor::EditorActivityCancelResult CancelActivitySource(const editor::EditorActivityKey,
                                                            void* const userData) noexcept
    {
        auto& fixture = *static_cast<ActivitySourceFixture*>(userData);
        ++fixture.cancelCalls;
        if (fixture.reenterCancel)
            ExerciseSourceReentrancy(fixture);
        return fixture.cancelResult;
    }

    detail::EditorActivitySourceDescriptor MakeSourceDescriptor(ActivitySourceFixture& fixture) noexcept
    {
        detail::EditorActivitySourceDescriptor descriptor;
        descriptor.id = fixture.source;
        descriptor.displayName = "fixture source";
        descriptor.query = &QueryActivitySource;
        descriptor.requestCancel = &CancelActivitySource;
        descriptor.userData = &fixture;
        return descriptor;
    }

    void TestCoreIngressCaptureAndRestart() noexcept
    {
        BridgeAllocation allocation;
        Check(allocation.bridge != nullptr, "activity bridge allocation");
        if (allocation.bridge == nullptr)
            return;

        FakeDiagnostics fake;
        g_fakeDiagnostics = &fake;
        std::atomic<u64> serviceGeneration{0};
        detail::EditorActivityCore first(*allocation.bridge, FakeAccess(), {&serviceGeneration});
        editor::EditorActivityFailure failure;
        Check(first.ReserveServiceGeneration(&failure), "reserve first service generation");
        Check(first.AttachDiagnostics(&failure), "attach process bridge sink");
        Check(fake.registerCalls == 1u && fake.userData == allocation.bridge,
              "diagnostics callback userData is the process-lifetime bridge, not service storage");
        first.Activate();

        Emit(fake, "repeat", 10);
        Emit(fake, "repeat", 11);
        first.Tick({});
        editor::EditorActivityDiagnosticRecord records[8]{};
        editor::EditorActivityDiagnosticCapture capture;
        Check(first.CaptureDiagnostics({}, {records, 8}, capture, &failure), "capture retained diagnostics");
        Check(capture.copiedCount == 1u && records[0].repeatCount == 2u && records[0].firstSequence + 1u == records[0].lastSequence,
              "adjacent identical diagnostics coalesce with contiguous sequence evidence");

        const u64 baseSequence = allocation.bridge->ingressSequence.load(std::memory_order_relaxed);
        const u64 droppedBefore = allocation.bridge->dropped.load(std::memory_order_relaxed);
        std::thread diagnosticsProducer([&]() noexcept {
            for (u32 index = 0; index < editor::MaximumEditorActivityDiagnosticsIngress - 1u; ++index)
                Emit(fake, "filler", 20 + index);
            Emit(fake, "gap-target", 1000);
            Emit(fake, "gap-target", 1001);
        });
        diagnosticsProducer.join();
        const u64 targetSequence = baseSequence + editor::MaximumEditorActivityDiagnosticsIngress;
        Check(allocation.bridge->dropped.load(std::memory_order_relaxed) == droppedBefore + 1u,
              "worker-thread ingress fills the bounded queue, consumes an overflow sequence, and records one drop");
        first.Tick({});
        Check(allocation.bridge->ingress.Size() == editor::MaximumEditorActivityDiagnosticsIngress -
                                                     editor::MaximumEditorActivityDiagnosticsDrainPerTick,
              "main-thread Activity tick drains exactly the bounded budget after cross-thread ingress");
        first.Tick({});
        Emit(fake, "gap-target", 1002);
        first.Tick({});

        editor::EditorActivityCursor gapCursor;
        gapCursor.serviceGeneration = first.GetStatus().serviceGeneration;
        gapCursor.diagnosticSequence = targetSequence;
        editor::EditorActivityDiagnosticRecord gapRecords[4]{};
        editor::EditorActivityDiagnosticCapture gapCapture;
        Check(first.CaptureDiagnostics(gapCursor, {gapRecords, 4}, gapCapture, &failure) && gapCapture.gap && gapCapture.copiedCount >= 1u,
              "capture reports an internal sequence gap caused by ingress loss");
        Check(std::strcmp(gapRecords[0].message, "gap-target") == 0 && gapRecords[0].firstSequence == targetSequence + 2u,
              "identical diagnostics separated by a dropped sequence do not coalesce across the gap");

        char unique[64]{};
        for (u32 index = 0; index < editor::MaximumEditorActivityDiagnosticsRetained + 8u; ++index)
        {
            std::snprintf(unique, sizeof(unique), "unique-%u", index);
            Emit(fake, unique, 2000 + index);
            if ((index + 1u) % editor::MaximumEditorActivityDiagnosticsDrainPerTick == 0)
                first.Tick({});
        }
        for (u32 index = 0; index < 4; ++index)
            first.Tick({});
        const editor::EditorActivityServiceStatus retainedStatus = first.GetStatus();
        Check(retainedStatus.diagnosticsRetained == editor::MaximumEditorActivityDiagnosticsRetained && retainedStatus.diagnosticsEvicted != 0,
              "retained diagnostics remain bounded and expose eviction evidence");

        std::atomic<editor::EditorActivityFailureCode> wrongThreadCode{editor::EditorActivityFailureCode::None};
        std::thread wrongThread([&]() noexcept {
            editor::EditorActivityDiagnosticCapture localCapture;
            editor::EditorActivityFailure localFailure;
            editor::EditorActivityDiagnosticRecord local[1]{};
            static_cast<void>(first.CaptureDiagnostics({}, {local, 1}, localCapture, &localFailure));
            wrongThreadCode.store(localFailure.code, std::memory_order_release);
        });
        wrongThread.join();
        Check(wrongThreadCode.load(std::memory_order_acquire) == editor::EditorActivityFailureCode::WrongThread,
              "consumer capture rejects the wrong thread");

        const editor::EditorActivityKey unsupportedKey{1u, 1u, 2u, 1u};
        Check(first.RequestCancel(unsupportedKey, &failure) == editor::EditorActivityCancelAdmission::Stale &&
                  failure.code == editor::EditorActivityFailureCode::StaleActivity,
              "cancellation rejects an exact key with no published source");
        Check(first.GetStatus().lastFailure.code == editor::EditorActivityFailureCode::StaleActivity &&
                  first.GetStatus().lastFailure.source == unsupportedKey.source &&
                  first.GetStatus().lastFailure.item == unsupportedKey.item,
              "service status retains the latest attributed core failure");

        const editor::EditorActivityFailure statusFailureBeforeWrongThread = first.GetStatus().lastFailure;
        std::atomic<editor::EditorActivityFailureCode> isolatedWrongThreadCode{editor::EditorActivityFailureCode::None};
        std::thread isolatedWrongThread([&]() noexcept {
            editor::EditorActivityFailure localFailure;
            static_cast<void>(first.ClearDiagnostics(&localFailure));
            isolatedWrongThreadCode.store(localFailure.code, std::memory_order_release);
        });
        isolatedWrongThread.join();
        const editor::EditorActivityFailure statusFailureAfterWrongThread = first.GetStatus().lastFailure;
        Check(isolatedWrongThreadCode.load(std::memory_order_acquire) == editor::EditorActivityFailureCode::WrongThread &&
                  statusFailureAfterWrongThread.code == statusFailureBeforeWrongThread.code &&
                  statusFailureAfterWrongThread.source == statusFailureBeforeWrongThread.source &&
                  statusFailureAfterWrongThread.item == statusFailureBeforeWrongThread.item,
              "wrong-thread rejection does not mutate main-thread service status");

        editor::EditorActivityCursor malformedCursor;
        malformedCursor.diagnosticSequence = 1u;
        editor::EditorActivityDiagnosticCapture malformedCapture;
        editor::EditorActivityDiagnosticRecord malformedRecord[1]{};
        const editor::EditorActivityFailure statusFailureBeforeMalformedCapture = first.GetStatus().lastFailure;
        Check(!first.CaptureDiagnostics(malformedCursor, {malformedRecord, 1}, malformedCapture, &failure) &&
                  failure.code == editor::EditorActivityFailureCode::StaleCursor,
              "partial-zero cursors cannot bypass the service-generation check");
        const editor::EditorActivityFailure statusFailureAfterMalformedCapture = first.GetStatus().lastFailure;
        Check(statusFailureAfterMalformedCapture.code == statusFailureBeforeMalformedCapture.code &&
                  statusFailureAfterMalformedCapture.source == statusFailureBeforeMalformedCapture.source &&
                  statusFailureAfterMalformedCapture.item == statusFailureBeforeMalformedCapture.item,
              "read-only capture failures do not overwrite persistent service failure evidence");

        editor::EditorActivityCursor terminalCursor;
        terminalCursor.serviceGeneration = first.GetStatus().serviceGeneration;
        terminalCursor.diagnosticSequence = ~u64{0};
        editor::EditorActivityDiagnosticCapture terminalCapture;
        Check(first.CaptureDiagnostics(terminalCursor, {malformedRecord, 1}, terminalCapture, &failure) &&
                  terminalCapture.copiedCount == 0u && terminalCapture.cursor.diagnosticSequence == ~u64{0},
              "terminal diagnostic cursor does not wrap and replay retained records");

        const editor::EditorActivityCursor firstCursor{first.GetStatus().serviceGeneration, 0, 0};
        Emit(fake, "queued-before-quiesce", 4000);
        first.Quiesce();
        Check(!first.IsFrameActive() && allocation.bridge->activeServiceGeneration.load(std::memory_order_acquire) == 0,
              "quiesce clears frame and bridge admissions before detach");
        fake.failUnregister = true;
        Check(!first.DetachDiagnostics(&failure) && failure.code == editor::EditorActivityFailureCode::DiagnosticsSinkFailure &&
                  allocation.bridge->hasSinkToken && fake.live,
              "failed detach retains exact live bridge token without service pointer lifetime");

        const u32 registerCallsBeforeRestart = fake.registerCalls;
        detail::EditorActivityCore second(*allocation.bridge, FakeAccess(), {&serviceGeneration});
        Check(second.ReserveServiceGeneration(&failure), "reserve restarted service generation");
        Check(second.AttachDiagnostics(&failure) && fake.registerCalls == registerCallsBeforeRestart,
              "restart reuses a proven-live retained bridge sink instead of duplicate registration");
        second.Activate();
        second.Tick({});
        editor::EditorActivityDiagnosticCapture restartedDiagnosticCapture;
        editor::EditorActivityDiagnosticRecord restartedDiagnosticRecord[1]{};
        Check(second.CaptureDiagnostics({}, {restartedDiagnosticRecord, 1}, restartedDiagnosticCapture, &failure) &&
                  restartedDiagnosticCapture.copiedCount == 0u,
              "queued diagnostics from a quiesced prior service generation are discarded on restart");
        editor::EditorActivityCapture activityCapture;
        editor::EditorActivitySourceSnapshot noSources[1]{};
        editor::EditorActivityItem noItems[1]{};
        Check(!second.CaptureActivities(firstCursor, {noSources, 1}, {noItems, 1}, activityCapture, &failure) &&
                  failure.code == editor::EditorActivityFailureCode::StaleCursor,
              "cursor from a prior service instance is explicitly stale");
        fake.failUnregister = false;
        second.Quiesce();
        Check(second.DetachDiagnostics(&failure) && !allocation.bridge->hasSinkToken && !fake.live,
              "later exact detach clears retained bridge token after the fence succeeds");

        std::atomic<u64> exhaustedServiceGeneration{~u64{0}};
        detail::EditorActivityCore exhausted(*allocation.bridge, FakeAccess(), {&exhaustedServiceGeneration});
        Check(!exhausted.ReserveServiceGeneration(&failure) &&
                  failure.code == editor::EditorActivityFailureCode::GenerationExhausted,
              "service generation exhaustion fails instead of wrapping");
        g_fakeDiagnostics = nullptr;
    }

    void TestSourceSnapshotsAndCancellation() noexcept
    {
        BridgeAllocation allocation;
        Check(allocation.bridge != nullptr, "activity source bridge allocation");
        if (allocation.bridge == nullptr)
            return;

        FakeDiagnostics fake;
        g_fakeDiagnostics = &fake;
        std::atomic<u64> serviceGeneration{0};
        std::atomic<u64> sourceGeneration{0};
        detail::EditorActivityCore core(*allocation.bridge, FakeAccess(),
                                        {&serviceGeneration, &sourceGeneration});
        editor::EditorActivityFailure failure;
        Check(core.ReserveServiceGeneration(&failure) && core.AttachDiagnostics(&failure),
              "activity source core initializes");
        core.Activate();

        ActivitySourceFixture fixture;
        fixture.core = &core;
        detail::EditorActivitySourceDescriptor descriptor = MakeSourceDescriptor(fixture);
        Check(core.RegisterSourceForOwner(fixture.owner, descriptor, fixture.token, &failure) &&
                  fixture.token.IsValid(),
              "owned activity source registers with a nonzero generation");

        detail::EditorActivitySourceToken duplicateToken{7u, 42u};
        Check(!core.RegisterSourceForOwner(fixture.owner, descriptor, duplicateToken, &failure) &&
                  failure.code == editor::EditorActivityFailureCode::DuplicateSource &&
                  duplicateToken.index == 7u && duplicateToken.generation == 42u,
              "duplicate source registration fails without mutating the output token");

        core.Tick({});
        editor::EditorActivitySourceSnapshot sources[editor::MaximumEditorActivitySources]{};
        editor::EditorActivityItem items[editor::MaximumEditorActivityRows]{};
        editor::EditorActivityCapture capture;
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  capture.sourceCount == 1u && capture.itemCount == 1u &&
                  !sources[0].stale && sources[0].lastSuccessGeneration == 1u &&
                  items[0].key.sourceGeneration == fixture.token.generation &&
                  items[0].cancellable,
              "successful source query publishes one complete fresh snapshot");

        editor::EditorActivitySourceSnapshot tinySources[1]{{}};
        editor::EditorActivityItem tinyItems[1]{{}};
        tinySources[0].source = 0xfeedu;
        tinyItems[0].key = {0xbeefu, 2u, 0xcafeu, 3u};
        editor::EditorActivityCapture untouchedCapture{{9u, 8u, 7u}, 6u, 5u};
        const editor::EditorActivitySourceSnapshot untouchedSource = tinySources[0];
        const editor::EditorActivityItem untouchedItem = tinyItems[0];
        Check(!core.CaptureActivities({}, {tinySources, 1},
                                      red::ArraySpan<editor::EditorActivityItem>{tinyItems, static_cast<u32>(0)},
                                      untouchedCapture, &failure) &&
                  failure.code == editor::EditorActivityFailureCode::CapacityExceeded &&
                  untouchedCapture.cursor.serviceGeneration == 9u &&
                  tinySources[0].source == untouchedSource.source &&
                  tinyItems[0].key == untouchedItem.key,
              "undersized activity capture fails all-or-nothing without output mutation");

        const editor::EditorActivityCapture beforeCancel = capture;
        const editor::EditorActivityItem beforeCancelItem = items[0];
        fixture.cancelResult = editor::EditorActivityCancelResult::Busy;
        Check(core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Queued &&
                  core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::AlreadyQueued,
              "exact duplicate cancellation admission coalesces in the bounded queue");
        editor::EditorActivityCapture afterAdmission;
        editor::EditorActivitySourceSnapshot afterAdmissionSources[editor::MaximumEditorActivitySources]{};
        editor::EditorActivityItem afterAdmissionItems[editor::MaximumEditorActivityRows]{};
        Check(core.CaptureActivities({}, {afterAdmissionSources, editor::MaximumEditorActivitySources},
                                     {afterAdmissionItems, editor::MaximumEditorActivityRows},
                                     afterAdmission, &failure) &&
                  afterAdmission.cursor.activityGeneration == beforeCancel.cursor.activityGeneration &&
                  afterAdmissionItems[0].cancelQueued == beforeCancelItem.cancelQueued &&
                  afterAdmissionItems[0].cancelResult == beforeCancelItem.cancelResult &&
                  fixture.cancelCalls == 0u,
              "cancel admission mutates only the private queue and leaves same-frame publication identical");

        core.Tick({});
        Check(fixture.cancelCalls == 1u &&
                  core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                         {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  items[0].cancelResult == editor::EditorActivityCancelResult::Busy &&
                  !items[0].cancelQueued,
              "next tick dispatches one Busy cancellation and publishes the bounded result");

        fixture.cancelResult = editor::EditorActivityCancelResult::AlreadyRequested;
        Check(core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Queued,
              "Busy cancellation can be explicitly retried");
        core.Tick({});
        Check(fixture.cancelCalls == 2u &&
                  core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                         {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  items[0].cancelResult == editor::EditorActivityCancelResult::AlreadyRequested &&
                  items[0].cancelQueued &&
                  core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::AlreadyQueued,
              "authoritative AlreadyRequested remains sticky for the exact item generation");

        fixture.itemGeneration = 2u;
        core.Tick({});
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  items[0].key.itemGeneration == 2u && !items[0].cancelQueued &&
                  items[0].cancelResult == editor::EditorActivityCancelResult::None,
              "new item generation does not inherit a prior operation cancellation result");
        Check(core.RequestCancel({fixture.source, fixture.token.generation, fixture.item, 1u}, &failure) ==
                  editor::EditorActivityCancelAdmission::Stale,
              "reused item identity rejects the stale generation");

        fixture.cancelResult = editor::EditorActivityCancelResult::Unsupported;
        Check(core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Queued,
              "unsupported-owner-result fixture queues the exact current key");
        core.Tick({});
        Check(fixture.cancelCalls == 3u &&
                  core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                         {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  items[0].cancelResult == editor::EditorActivityCancelResult::Unsupported &&
                  !items[0].cancelQueued &&
                  core.GetStatus().cancellationFailures != 0,
              "Unsupported cancellation result is published once without an automatic retry loop");

        fixture.cancelResult = editor::EditorActivityCancelResult::Stale;
        Check(core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Queued,
              "Unsupported result permits an explicit retry");
        core.Tick({});
        Check(fixture.cancelCalls == 4u &&
                  core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                         {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  items[0].cancelResult == editor::EditorActivityCancelResult::Stale &&
                  !items[0].cancelQueued,
              "authoritative Stale result is published once and permits an explicit later retry");

        fixture.cancelResult = editor::EditorActivityCancelResult::Accepted;
        Check(core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Queued,
              "Stale owner result permits an explicit retry");
        core.Tick({});
        Check(fixture.cancelCalls == 5u &&
                  core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                         {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  items[0].cancelResult == editor::EditorActivityCancelResult::Accepted &&
                  items[0].cancelQueued,
              "Accepted means the request reached the authoritative owner without inferring terminal state");

        fixture.itemGeneration = 3u;
        fixture.cancelResult = editor::EditorActivityCancelResult::Failed;
        core.Tick({});
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  items[0].key.itemGeneration == 3u && !items[0].cancelQueued &&
                  core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Queued,
              "new generation clears accepted presentation and admits a fresh exact key");
        core.Tick({});
        Check(fixture.cancelCalls == 6u &&
                  core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                         {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  items[0].cancelResult == editor::EditorActivityCancelResult::Failed &&
                  !items[0].cancelQueued,
              "Failed cancellation result is published once without an automatic retry loop");

        const u64 failuresBeforeItemChange = core.GetStatus().cancellationFailures;
        const u32 callsBeforeItemChange = fixture.cancelCalls;
        fixture.cancelResult = editor::EditorActivityCancelResult::Accepted;
        Check(core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Queued,
              "item-generation-change fixture admits the current exact key");
        fixture.itemGeneration = 4u;
        core.Tick({});
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  fixture.cancelCalls == callsBeforeItemChange &&
                  items[0].key.itemGeneration == 4u &&
                  items[0].cancelResult == editor::EditorActivityCancelResult::None &&
                  !items[0].cancelQueued &&
                  core.GetStatus().cancellationFailures == failuresBeforeItemChange + 1u,
              "item generation changing after admission makes next-tick dispatch stale without invoking the owner");

        const editor::EditorActivityKey oldSourceKey = items[0].key;
        const detail::EditorActivitySourceToken oldSourceToken = fixture.token;
        const u32 callsBeforeSourceRestart = fixture.cancelCalls;
        Check(core.RequestCancel(oldSourceKey, &failure) == editor::EditorActivityCancelAdmission::Queued &&
                  core.UnregisterSourceForOwner(fixture.owner, oldSourceToken, &failure),
              "source-restart fixture queues then removes the old exact source generation");
        detail::EditorActivitySourceToken restartedToken;
        Check(core.RegisterSourceForOwner(fixture.owner, descriptor, restartedToken, &failure) &&
                  restartedToken.generation != oldSourceToken.generation,
              "source restart receives a fresh nonzero generation");
        fixture.token = restartedToken;
        core.Tick({});
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  fixture.cancelCalls == callsBeforeSourceRestart &&
                  items[0].key.sourceGeneration == restartedToken.generation &&
                  core.RequestCancel(oldSourceKey, &failure) == editor::EditorActivityCancelAdmission::Stale,
              "source replacement discards queued old-generation cancellation before next-tick dispatch");

        fixture.queryMode = SourceQueryMode::FailAfterAdd;
        fixture.label = "partial-must-not-publish";
        core.Tick({});
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  sources[0].stale && !items[0].cancellable &&
                  std::strcmp(items[0].label, "activity") == 0 &&
                  core.GetStatus().sourceQueryFailures != 0,
              "failed source query retains only the prior rows as stale non-cancellable evidence");

        fixture.queryMode = SourceQueryMode::Success;
        fixture.label = "recovered";
        core.Tick({});
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  !sources[0].stale && capture.itemCount == 1u &&
                  std::strcmp(items[0].label, "recovered") == 0,
              "successful recovery atomically replaces stale rows and remains latest-value only");

        fixture.queryMode = SourceQueryMode::EmptySuccess;
        core.Tick({});
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  capture.sourceCount == 1u && capture.itemCount == 0u &&
                  !sources[0].stale && sources[0].itemCount == 0u,
              "missing row disappears on the next successful source snapshot");
        fixture.queryMode = SourceQueryMode::Success;
        core.Tick({});
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  capture.itemCount == 1u,
              "latest-value source row can reappear without retaining per-frame history");

        const u64 priorGeneration = capture.cursor.activityGeneration;
        Emit(fake, "staged-before-query-callback", 20);
        editor::EditorActivityDiagnosticRecord queryPriorRecords[8]{};
        editor::EditorActivityDiagnosticCapture queryPriorDiagnostics;
        Check(core.CaptureDiagnostics({}, {queryPriorRecords, 8}, queryPriorDiagnostics, &failure),
              "query callback isolation captures the prior diagnostics publication");
        const editor::EditorActivityServiceStatus queryPriorStatus = core.GetStatus();
        fixture.reenterQuery = true;
        core.Tick({});
        fixture.reenterQuery = false;
        Check(fixture.reentrantCancelAdmission == editor::EditorActivityCancelAdmission::ReentrantMutation &&
                  !fixture.reentrantClear &&
                  fixture.reentrantClearFailure == editor::EditorActivityFailureCode::ReentrantMutation &&
                  !fixture.reentrantRegister &&
                  fixture.reentrantRegisterFailure == editor::EditorActivityFailureCode::ReentrantMutation &&
                  !fixture.reentrantUnregister &&
                  fixture.reentrantUnregisterFailure == editor::EditorActivityFailureCode::ReentrantMutation &&
                  !fixture.reentrantRevoke &&
                  fixture.reentrantRevokeFailure == editor::EditorActivityFailureCode::ReentrantMutation &&
                  fixture.readOnlyGenerationDuringCallback == priorGeneration &&
                  fixture.readOnlyStatusGenerationDuringCallback == queryPriorStatus.activityGeneration &&
                  fixture.readOnlyDiagnosticsRetainedDuringCallback == queryPriorStatus.diagnosticsRetained &&
                  fixture.readOnlyDiagnosticsIngressDuringCallback == queryPriorStatus.diagnosticsIngress &&
                  fixture.readOnlyDiagnosticCaptureCountDuringCallback == queryPriorDiagnostics.copiedCount &&
                  core.GetStatus().diagnosticsRetained == queryPriorStatus.diagnosticsRetained + 1u,
              "source query callback rejects every mutation while Activity/diagnostic/status reads see only the prior publication");

        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure),
              "capture current row for cancel callback reentrancy");
        fixture.reenterCancel = true;
        fixture.cancelResult = editor::EditorActivityCancelResult::Busy;
        Check(core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Queued,
              "cancel callback reentrancy fixture queues exact key");
        const u64 priorCancelGeneration = capture.cursor.activityGeneration;
        Emit(fake, "staged-before-cancel-callback", 21);
        editor::EditorActivityDiagnosticRecord cancelPriorRecords[8]{};
        editor::EditorActivityDiagnosticCapture cancelPriorDiagnostics;
        Check(core.CaptureDiagnostics({}, {cancelPriorRecords, 8}, cancelPriorDiagnostics, &failure),
              "cancel callback isolation captures the prior diagnostics publication");
        const editor::EditorActivityServiceStatus cancelPriorStatus = core.GetStatus();
        core.Tick({});
        fixture.reenterCancel = false;
        Check(fixture.reentrantCancelAdmission == editor::EditorActivityCancelAdmission::ReentrantMutation &&
                  fixture.reentrantClearFailure == editor::EditorActivityFailureCode::ReentrantMutation &&
                  fixture.reentrantRegisterFailure == editor::EditorActivityFailureCode::ReentrantMutation &&
                  fixture.reentrantUnregisterFailure == editor::EditorActivityFailureCode::ReentrantMutation &&
                  fixture.reentrantRevokeFailure == editor::EditorActivityFailureCode::ReentrantMutation &&
                  fixture.readOnlyGenerationDuringCallback == priorCancelGeneration &&
                  fixture.readOnlyStatusGenerationDuringCallback == cancelPriorStatus.activityGeneration &&
                  fixture.readOnlyDiagnosticsRetainedDuringCallback == cancelPriorStatus.diagnosticsRetained &&
                  fixture.readOnlyDiagnosticsIngressDuringCallback == cancelPriorStatus.diagnosticsIngress &&
                  fixture.readOnlyDiagnosticCaptureCountDuringCallback == cancelPriorDiagnostics.copiedCount &&
                  core.GetStatus().diagnosticsRetained == cancelPriorStatus.diagnosticsRetained + 1u,
              "cancel callback shares the same mutation guard and prior-publication Activity/diagnostic/status read view");

        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure),
              "capture current row for owner revoke");
        Check(core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Queued,
              "owner revoke fixture queues a request");
        Check(core.RevokeSourceOwner(fixture.owner, &failure) &&
                  core.GetStatus().pendingCancellations == 0u &&
                  core.RequestCancel(items[0].key, &failure) == editor::EditorActivityCancelAdmission::Stale,
              "logical owner revoke makes callbacks unreachable and discards queued requests immediately");
        Check(core.UnregisterSourceForOwner(fixture.owner, fixture.token, &failure) &&
                  !core.UnregisterSourceForOwner(fixture.owner, fixture.token, &failure) &&
                  failure.code == editor::EditorActivityFailureCode::StaleToken,
              "physical cleanup consumes the exact revoked source token and later reuse is stale");

        ActivitySourceFixture perSourceOverflow;
        perSourceOverflow.core = &core;
        perSourceOverflow.owner = {{0x502u}, 1u};
        perSourceOverflow.source = 0x602u;
        perSourceOverflow.item = 0x702u;
        perSourceOverflow.queryMode = SourceQueryMode::OverflowRows;
        detail::EditorActivitySourceToken overflowToken;
        Check(core.RegisterSourceForOwner(perSourceOverflow.owner, MakeSourceDescriptor(perSourceOverflow),
                                          overflowToken, &failure),
              "overflow source registers");
        perSourceOverflow.token = overflowToken;
        core.Tick({});
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  sources[0].stale && sources[0].failure.code == editor::EditorActivityFailureCode::CapacityExceeded &&
                  capture.itemCount == 0u,
              "writer row overflow rejects the transaction without a partial publication");
        Check(core.RevokeSourceOwner(perSourceOverflow.owner, &failure) &&
                  core.UnregisterSourceForOwner(perSourceOverflow.owner, overflowToken, &failure),
              "overflow source cleanup");

        core.Quiesce();
        Check(core.GetStatus().pendingCancellations == 0u && core.DetachDiagnostics(&failure),
              "quiesce closes source/cancel admission and drains no producer callbacks");
        g_fakeDiagnostics = nullptr;
    }

    void TestSourceRegistryBoundsAndGenerations() noexcept
    {
        BridgeAllocation allocation;
        Check(allocation.bridge != nullptr, "activity source registry bridge allocation");
        if (allocation.bridge == nullptr)
            return;

        FakeDiagnostics fake;
        g_fakeDiagnostics = &fake;
        std::atomic<u64> serviceGeneration{0};
        std::atomic<u64> sourceGeneration{0};
        detail::EditorActivityCore core(*allocation.bridge, FakeAccess(),
                                        {&serviceGeneration, &sourceGeneration});
        editor::EditorActivityFailure failure;
        Check(core.ReserveServiceGeneration(&failure) && core.AttachDiagnostics(&failure),
              "source registry bounds core initializes");
        core.Activate();

        ActivitySourceFixture fixtures[editor::MaximumEditorActivitySources]{};
        for (u32 index = 0; index < editor::MaximumEditorActivitySources; ++index)
        {
            fixtures[index].core = &core;
            fixtures[index].source = 0x8100u + index;
            fixtures[index].item = 0x9100u + index;
        }

        Check(core.RegisterSourceForOwner(fixtures[0].owner, MakeSourceDescriptor(fixtures[0]),
                                          fixtures[0].token, &failure),
              "first bounded source registers");
        const detail::EditorActivitySourceToken firstToken = fixtures[0].token;
        Check(core.UnregisterSourceForOwner(fixtures[0].owner, firstToken, &failure),
              "first bounded source unregisters");
        Check(core.RegisterSourceForOwner(fixtures[0].owner, MakeSourceDescriptor(fixtures[0]),
                                          fixtures[0].token, &failure) &&
                  fixtures[0].token.generation > firstToken.generation &&
                  !core.UnregisterSourceForOwner(fixtures[0].owner, firstToken, &failure) &&
                  failure.code == editor::EditorActivityFailureCode::StaleToken,
              "source slot reuse reserves a fresh generation and fences the old token");

        bool allRegistered = true;
        for (u32 index = 1; index < editor::MaximumEditorActivitySources; ++index)
            allRegistered = core.RegisterSourceForOwner(fixtures[index].owner,
                                                        MakeSourceDescriptor(fixtures[index]),
                                                        fixtures[index].token, &failure) &&
                            allRegistered;
        Check(allRegistered && core.GetStatus().liveSources == editor::MaximumEditorActivitySources,
              "source registry admits exactly the locked 64 live sources");

        ActivitySourceFixture overflow;
        overflow.core = &core;
        overflow.source = 0x8fffu;
        overflow.item = 0x9fffu;
        detail::EditorActivitySourceToken untouched{7u, 42u};
        Check(!core.RegisterSourceForOwner(overflow.owner, MakeSourceDescriptor(overflow),
                                           untouched, &failure) &&
                  failure.code == editor::EditorActivityFailureCode::CapacityExceeded &&
                  untouched.index == 7u && untouched.generation == 42u,
              "65th source is rejected without mutating the output token");

        std::atomic<bool> wrongRegister{true};
        std::atomic<bool> wrongUnregister{true};
        std::atomic<bool> wrongRevoke{true};
        std::atomic<editor::EditorActivityFailureCode> wrongRegisterCode{
            editor::EditorActivityFailureCode::None};
        std::atomic<editor::EditorActivityFailureCode> wrongUnregisterCode{
            editor::EditorActivityFailureCode::None};
        std::atomic<editor::EditorActivityFailureCode> wrongRevokeCode{
            editor::EditorActivityFailureCode::None};
        std::thread wrongThread([&]() noexcept {
            editor::EditorActivityFailure localFailure;
            detail::EditorActivitySourceToken localToken{9u, 99u};
            wrongRegister.store(
                core.RegisterSourceForOwner(overflow.owner, MakeSourceDescriptor(overflow),
                                            localToken, &localFailure),
                std::memory_order_release);
            wrongRegisterCode.store(localFailure.code, std::memory_order_release);
            wrongUnregister.store(
                core.UnregisterSourceForOwner(fixtures[0].owner, fixtures[0].token,
                                              &localFailure),
                std::memory_order_release);
            wrongUnregisterCode.store(localFailure.code, std::memory_order_release);
            wrongRevoke.store(core.RevokeSourceOwner(fixtures[0].owner, &localFailure),
                              std::memory_order_release);
            wrongRevokeCode.store(localFailure.code, std::memory_order_release);
        });
        wrongThread.join();
        Check(!wrongRegister.load(std::memory_order_acquire) &&
                  !wrongUnregister.load(std::memory_order_acquire) &&
                  !wrongRevoke.load(std::memory_order_acquire) &&
                  wrongRegisterCode.load(std::memory_order_acquire) ==
                      editor::EditorActivityFailureCode::WrongThread &&
                  wrongUnregisterCode.load(std::memory_order_acquire) ==
                      editor::EditorActivityFailureCode::WrongThread &&
                  wrongRevokeCode.load(std::memory_order_acquire) ==
                      editor::EditorActivityFailureCode::WrongThread &&
                  core.GetStatus().liveSources == editor::MaximumEditorActivitySources,
              "source register/unregister/revoke reject worker-thread mutation without state changes");

        const u64 lastFirstServiceSourceGeneration =
            fixtures[editor::MaximumEditorActivitySources - 1u].token.generation;
        core.Quiesce();
        Check(core.DetachDiagnostics(&failure), "source registry bounds core detaches");

        detail::EditorActivityCore restarted(*allocation.bridge, FakeAccess(),
                                             {&serviceGeneration, &sourceGeneration});
        Check(restarted.ReserveServiceGeneration(&failure) && restarted.AttachDiagnostics(&failure),
              "restarted source registry core initializes");
        restarted.Activate();
        ActivitySourceFixture restartedFixture;
        restartedFixture.core = &restarted;
        restartedFixture.source = 0xa100u;
        restartedFixture.item = 0xa200u;
        Check(restarted.RegisterSourceForOwner(restartedFixture.owner,
                                               MakeSourceDescriptor(restartedFixture),
                                               restartedFixture.token, &failure) &&
                  restartedFixture.token.generation > lastFirstServiceSourceGeneration,
              "source generation remains process-wide across Activity service restart");
        restarted.Quiesce();
        Check(restarted.DetachDiagnostics(&failure), "restarted source registry core detaches");

        sourceGeneration.store(~u64{0}, std::memory_order_relaxed);
        detail::EditorActivityCore exhausted(*allocation.bridge, FakeAccess(),
                                             {&serviceGeneration, &sourceGeneration});
        Check(exhausted.ReserveServiceGeneration(&failure) && exhausted.AttachDiagnostics(&failure),
              "source-generation exhaustion core initializes");
        exhausted.Activate();
        ActivitySourceFixture exhaustedFixture;
        exhaustedFixture.core = &exhausted;
        exhaustedFixture.source = 0xa300u;
        exhaustedFixture.item = 0xa400u;
        detail::EditorActivitySourceToken exhaustedToken{11u, 77u};
        Check(!exhausted.RegisterSourceForOwner(exhaustedFixture.owner,
                                                MakeSourceDescriptor(exhaustedFixture),
                                                exhaustedToken, &failure) &&
                  failure.code == editor::EditorActivityFailureCode::GenerationExhausted &&
                  exhaustedToken.index == 11u && exhaustedToken.generation == 77u &&
                  exhausted.GetStatus().liveSources == 0u,
              "source generation exhaustion fails closed without wrapping or partial registration");
        exhausted.Quiesce();
        Check(exhausted.DetachDiagnostics(&failure), "source-generation exhaustion core detaches");
        g_fakeDiagnostics = nullptr;
    }

    void TestSourceWriterValidation() noexcept
    {
        BridgeAllocation allocation;
        Check(allocation.bridge != nullptr, "activity writer validation bridge allocation");
        if (allocation.bridge == nullptr)
            return;

        FakeDiagnostics fake;
        g_fakeDiagnostics = &fake;
        std::atomic<u64> serviceGeneration{0};
        std::atomic<u64> sourceGeneration{0};
        detail::EditorActivityCore core(*allocation.bridge, FakeAccess(),
                                        {&serviceGeneration, &sourceGeneration});
        editor::EditorActivityFailure failure;
        Check(core.ReserveServiceGeneration(&failure) && core.AttachDiagnostics(&failure),
              "writer validation core initializes");
        core.Activate();

        ActivitySourceFixture sourceNameFixture;
        sourceNameFixture.core = &core;
        char overlongSourceName[editor::MaximumEditorActivityLabelBytes + 1u]{};
        for (u32 index = 0; index < editor::MaximumEditorActivityLabelBytes; ++index)
            overlongSourceName[index] = 's';
        detail::EditorActivitySourceDescriptor overlongSource =
            MakeSourceDescriptor(sourceNameFixture);
        overlongSource.displayName = overlongSourceName;
        detail::EditorActivitySourceToken untouched{5u, 55u};
        Check(!core.RegisterSourceForOwner(sourceNameFixture.owner, overlongSource,
                                           untouched, &failure) &&
                  failure.code == editor::EditorActivityFailureCode::InvalidDescriptor &&
                  untouched.index == 5u && untouched.generation == 55u,
              "overlong source display name is rejected before registration");

        char overlongLabel[editor::MaximumEditorActivityLabelBytes + 1u]{};
        for (u32 index = 0; index < editor::MaximumEditorActivityLabelBytes; ++index)
            overlongLabel[index] = 'l';
        char overlongDetail[editor::MaximumEditorActivityDetailBytes + 1u]{};
        for (u32 index = 0; index < editor::MaximumEditorActivityDetailBytes; ++index)
            overlongDetail[index] = 'd';

        auto verifyRejectedSnapshot =
            [&](const SourceQueryMode mode, const char* const label,
                const char* const detailText,
                const editor::EditorActivityFailureCode expected,
                const u64 ordinal) noexcept {
                ActivitySourceFixture fixture;
                fixture.core = &core;
                fixture.owner = {{0xb000u + ordinal}, 1u};
                fixture.source = 0xb100u + ordinal;
                fixture.item = 0xb200u + ordinal;
                fixture.queryMode = mode;
                fixture.label = label;
                fixture.detailText = detailText;
                Check(core.RegisterSourceForOwner(fixture.owner, MakeSourceDescriptor(fixture),
                                                  fixture.token, &failure),
                      "invalid-writer fixture source registers");
                core.Tick({});
                editor::EditorActivitySourceSnapshot sources[editor::MaximumEditorActivitySources]{};
                editor::EditorActivityItem items[editor::MaximumEditorActivityRows]{};
                editor::EditorActivityCapture capture;
                Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                             {items, editor::MaximumEditorActivityRows},
                                             capture, &failure) &&
                          capture.sourceCount == 1u && capture.itemCount == 0u &&
                          sources[0].stale && sources[0].failure.code == expected,
                      "invalid writer transaction publishes only a stale zero-row failure snapshot");
                Check(core.RevokeSourceOwner(fixture.owner, &failure) &&
                          core.UnregisterSourceForOwner(fixture.owner, fixture.token, &failure),
                      "invalid writer fixture cleans up through revoke then exact token removal");
            };

        verifyRejectedSnapshot(SourceQueryMode::ZeroGeneration, "zero-generation", "detail",
                               editor::EditorActivityFailureCode::InvalidDescriptor, 1u);
        verifyRejectedSnapshot(SourceQueryMode::InvalidPresentation, "invalid-presentation", "detail",
                               editor::EditorActivityFailureCode::InvalidDescriptor, 2u);
        verifyRejectedSnapshot(SourceQueryMode::InvalidProgress, "invalid-progress", "detail",
                               editor::EditorActivityFailureCode::InvalidDescriptor, 3u);
        verifyRejectedSnapshot(SourceQueryMode::DuplicateItem, "duplicate-item", "detail",
                               editor::EditorActivityFailureCode::DuplicateItem, 4u);
        verifyRejectedSnapshot(SourceQueryMode::Success, overlongLabel, "detail",
                               editor::EditorActivityFailureCode::InvalidDescriptor, 5u);
        verifyRejectedSnapshot(SourceQueryMode::Success, "overlong-detail", overlongDetail,
                               editor::EditorActivityFailureCode::InvalidDescriptor, 6u);
        verifyRejectedSnapshot(SourceQueryMode::OverflowRows, "row-overflow", "detail",
                               editor::EditorActivityFailureCode::CapacityExceeded, 7u);

        ActivitySourceFixture missingCancel;
        missingCancel.core = &core;
        missingCancel.owner = {{0xb080u}, 1u};
        missingCancel.source = 0xb180u;
        missingCancel.item = 0xb280u;
        detail::EditorActivitySourceDescriptor missingCancelDescriptor =
            MakeSourceDescriptor(missingCancel);
        missingCancelDescriptor.requestCancel = nullptr;
        Check(core.RegisterSourceForOwner(missingCancel.owner, missingCancelDescriptor,
                                          missingCancel.token, &failure),
              "source without a cancel callback registers");
        core.Tick({});
        editor::EditorActivitySourceSnapshot validationSources[editor::MaximumEditorActivitySources]{};
        editor::EditorActivityItem validationItems[editor::MaximumEditorActivityRows]{};
        editor::EditorActivityCapture validationCapture;
        Check(core.CaptureActivities({}, {validationSources, editor::MaximumEditorActivitySources},
                                     {validationItems, editor::MaximumEditorActivityRows},
                                     validationCapture, &failure) &&
                  validationCapture.itemCount == 0u && validationSources[0].stale &&
                  validationSources[0].failure.code == editor::EditorActivityFailureCode::InvalidDescriptor,
              "writer rejects a cancellable row when the source has no cancellation callback");
        Check(core.RevokeSourceOwner(missingCancel.owner, &failure) &&
                  core.UnregisterSourceForOwner(missingCancel.owner, missingCancel.token, &failure),
              "missing-cancel writer fixture cleans up");

        ActivitySourceFixture nonCancellable;
        nonCancellable.core = &core;
        nonCancellable.owner = {{0xb081u}, 1u};
        nonCancellable.source = 0xb181u;
        nonCancellable.item = 0xb281u;
        nonCancellable.cancellable = false;
        Check(core.RegisterSourceForOwner(nonCancellable.owner, MakeSourceDescriptor(nonCancellable),
                                          nonCancellable.token, &failure),
              "non-cancellable source fixture registers");
        core.Tick({});
        Check(core.CaptureActivities({}, {validationSources, editor::MaximumEditorActivitySources},
                                     {validationItems, editor::MaximumEditorActivityRows},
                                     validationCapture, &failure) &&
                  validationCapture.itemCount == 1u && !validationItems[0].cancellable &&
                  core.RequestCancel(validationItems[0].key, &failure) ==
                      editor::EditorActivityCancelAdmission::Unsupported &&
                  failure.code == editor::EditorActivityFailureCode::CancellationUnsupported,
              "RequestCancel rejects a fresh exact key whose current producer snapshot is non-cancellable");
        Check(core.RevokeSourceOwner(nonCancellable.owner, &failure) &&
                  core.UnregisterSourceForOwner(nonCancellable.owner, nonCancellable.token, &failure),
              "non-cancellable source fixture cleans up");

        core.Quiesce();
        Check(core.DetachDiagnostics(&failure), "writer validation core detaches");
        g_fakeDiagnostics = nullptr;
    }

    void TestConservativeRowReservationPreservesLaterStaleRows() noexcept
    {
        BridgeAllocation allocation;
        Check(allocation.bridge != nullptr, "activity conservative-row-reservation bridge allocation");
        if (allocation.bridge == nullptr)
            return;

        FakeDiagnostics fake;
        g_fakeDiagnostics = &fake;
        std::atomic<u64> serviceGeneration{0};
        std::atomic<u64> sourceGeneration{0};
        detail::EditorActivityCore core(*allocation.bridge, FakeAccess(),
                                        {&serviceGeneration, &sourceGeneration});
        editor::EditorActivityFailure failure;
        Check(core.ReserveServiceGeneration(&failure) && core.AttachDiagnostics(&failure),
              "conservative-row-reservation core initializes");
        core.Activate();

        ActivitySourceFixture fixtures[9]{};
        bool registered = true;
        for (u32 index = 0; index < 9u; ++index)
        {
            fixtures[index].core = &core;
            fixtures[index].source = 0xbf00u + index;
            fixtures[index].item = 0xc000u + index * editor::MaximumEditorActivityRowsPerSource;
            fixtures[index].rowCount = index == 0u
                ? 1u
                : (index < 8u ? editor::MaximumEditorActivityRowsPerSource
                              : editor::MaximumEditorActivityRowsPerSource - 1u);
            registered = core.RegisterSourceForOwner(fixtures[index].owner,
                                                     MakeSourceDescriptor(fixtures[index]),
                                                     fixtures[index].token, &failure) &&
                         registered;
        }
        Check(registered, "conservative-row-reservation sources register");

        core.Tick({});
        editor::EditorActivitySourceSnapshot sources[editor::MaximumEditorActivitySources]{};
        editor::EditorActivityItem items[editor::MaximumEditorActivityRows]{};
        editor::EditorActivityCapture capture;
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  capture.sourceCount == 9u && capture.itemCount == editor::MaximumEditorActivityRows &&
                  !sources[0].stale && sources[0].itemCount == 1u &&
                  !sources[8].stale &&
                  sources[8].itemCount == editor::MaximumEditorActivityRowsPerSource - 1u,
              "first conservative-row-reservation tick publishes a full 256-row prior snapshot");

        fixtures[0].rowCount = editor::MaximumEditorActivityRowsPerSource;
        fixtures[8].queryMode = SourceQueryMode::FailAfterAdd;
        core.Tick({});

        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  capture.sourceCount == 9u && capture.itemCount == editor::MaximumEditorActivityRows &&
                  sources[0].stale && sources[0].itemCount == 1u &&
                  sources[0].failure.code == editor::EditorActivityFailureCode::CapacityExceeded &&
                  sources[8].stale &&
                  sources[8].itemCount == editor::MaximumEditorActivityRowsPerSource - 1u &&
                  sources[8].failure.code == editor::EditorActivityFailureCode::SourceQueryFailed,
              "earlier growth is bounded so a later failed source retains its complete prior snapshot");

        bool laterStaleRowsPreserved = sources[8].firstItem + sources[8].itemCount <= capture.itemCount;
        for (u32 index = 0; laterStaleRowsPreserved && index < sources[8].itemCount; ++index)
        {
            const editor::EditorActivityItem& item = items[sources[8].firstItem + index];
            laterStaleRowsPreserved = !item.cancellable && !item.cancelQueued &&
                                     item.key.source == fixtures[8].source &&
                                     item.key.sourceGeneration == fixtures[8].token.generation;
        }
        Check(laterStaleRowsPreserved,
              "later source prior rows are retained completely as stale non-cancellable evidence");

        core.Quiesce();
        Check(core.DetachDiagnostics(&failure), "conservative-row-reservation core detaches");
        g_fakeDiagnostics = nullptr;
    }

    void TestTotalRowsAndCancelQueueBounds() noexcept
    {
        BridgeAllocation allocation;
        Check(allocation.bridge != nullptr, "activity total-row bridge allocation");
        if (allocation.bridge == nullptr)
            return;

        FakeDiagnostics fake;
        g_fakeDiagnostics = &fake;
        std::atomic<u64> serviceGeneration{0};
        std::atomic<u64> sourceGeneration{0};
        detail::EditorActivityCore core(*allocation.bridge, FakeAccess(),
                                        {&serviceGeneration, &sourceGeneration});
        editor::EditorActivityFailure failure;
        Check(core.ReserveServiceGeneration(&failure) && core.AttachDiagnostics(&failure),
              "total-row/cancel-queue core initializes");
        core.Activate();

        ActivitySourceFixture fixtures[9]{};
        bool registered = true;
        for (u32 index = 0; index < 9u; ++index)
        {
            fixtures[index].core = &core;
            fixtures[index].source = 0xc100u + index;
            fixtures[index].item = 0xc200u + index * editor::MaximumEditorActivityRowsPerSource;
            fixtures[index].rowCount =
                index < 8u ? editor::MaximumEditorActivityRowsPerSource : 1u;
            registered = core.RegisterSourceForOwner(fixtures[index].owner,
                                                     MakeSourceDescriptor(fixtures[index]),
                                                     fixtures[index].token, &failure) &&
                         registered;
        }
        Check(registered, "nine total-row transaction sources register");
        core.Tick({});

        editor::EditorActivitySourceSnapshot sources[editor::MaximumEditorActivitySources]{};
        editor::EditorActivityItem items[editor::MaximumEditorActivityRows]{};
        editor::EditorActivityCapture capture;
        Check(core.CaptureActivities({}, {sources, editor::MaximumEditorActivitySources},
                                     {items, editor::MaximumEditorActivityRows}, capture, &failure) &&
                  capture.sourceCount == 9u &&
                  capture.itemCount == editor::MaximumEditorActivityRows &&
                  !sources[7].stale && sources[7].itemCount ==
                      editor::MaximumEditorActivityRowsPerSource &&
                  sources[8].stale && sources[8].itemCount == 0u &&
                  sources[8].failure.code == editor::EditorActivityFailureCode::CapacityExceeded,
              "257th transaction row fails in the writer and publishes no partial ninth-source rows");

        bool firstThirtyTwoQueued = true;
        for (u32 index = 0; index < editor::MaximumEditorActivityCancelRequests; ++index)
            firstThirtyTwoQueued =
                core.RequestCancel(items[index].key, &failure) ==
                    editor::EditorActivityCancelAdmission::Queued &&
                firstThirtyTwoQueued;
        Check(firstThirtyTwoQueued &&
                  core.GetStatus().pendingCancellations ==
                      editor::MaximumEditorActivityCancelRequests &&
                  core.RequestCancel(items[editor::MaximumEditorActivityCancelRequests].key,
                                     &failure) ==
                      editor::EditorActivityCancelAdmission::CapacityExceeded &&
                  failure.code == editor::EditorActivityFailureCode::CapacityExceeded,
              "cancellation admission is bounded to exactly 32 queued exact keys");

        u32 cancelCallsBeforeQuiesce = 0;
        for (const ActivitySourceFixture& fixture : fixtures)
            cancelCallsBeforeQuiesce += fixture.cancelCalls;
        Check(cancelCallsBeforeQuiesce == 0u,
              "queued cancellation requests invoke no source callback before the next Activity tick");
        core.Quiesce();
        core.Tick({});
        u32 cancelCallsAfterQuiesce = 0;
        for (const ActivitySourceFixture& fixture : fixtures)
            cancelCallsAfterQuiesce += fixture.cancelCalls;
        Check(core.GetStatus().pendingCancellations == 0u &&
                  cancelCallsAfterQuiesce == cancelCallsBeforeQuiesce,
              "quiesce discards queued cancellations and no post-quiesce tick can reach revoked source callbacks");
        Check(core.DetachDiagnostics(&failure), "total-row/cancel-queue core detaches");
        g_fakeDiagnostics = nullptr;
    }

    class FixtureFramePipelineService final : public engine::FramePipelineService
    {
    public:
        bool RegisterParticipant(const engine::FrameParticipantDescriptor& descriptor, engine::FrameFailure*) noexcept override
        {
            ++registrations;
            participant = descriptor.id;
            phase = descriptor.phase;
            dependencyCount = descriptor.after.Size();
            dependency = dependencyCount != 0 ? descriptor.after[0] : engine::InvalidFrameParticipantId;
            callback = descriptor.execute;
            userData = descriptor.userData;
            return true;
        }
        bool Configure(const engine::FramePipelineConfig&, engine::FrameFailure*) noexcept override { return true; }
        bool Compile(engine::FrameFailure*) noexcept override { return true; }
        bool RunFrame(engine::FrameFailure*) noexcept override { return true; }
        bool SetPaused(bool) noexcept override { return true; }
        bool SetTimeScale(vanguard::f32) noexcept override { return true; }
        engine::FramePipelineState GetState() const noexcept override { return engine::FramePipelineState::Building; }
        engine::FramePipelineStats GetStats() const noexcept override { return {}; }
        void VisitParticipantStats(engine::FrameParticipantVisitor, void*) const noexcept override {}
        const engine::FrameFailure& GetLastFailure() const noexcept override { return failure; }

        u32 registrations = 0;
        engine::FrameParticipantId participant = engine::InvalidFrameParticipantId;
        engine::FramePhase phase = engine::FramePhase::Simulation;
        u32 dependencyCount = 0;
        engine::FrameParticipantId dependency = engine::InvalidFrameParticipantId;
        engine::ExecuteFrameParticipant callback = nullptr;
        void* userData = nullptr;
        engine::FrameFailure failure;
    };

    FixtureFramePipelineService g_framePipeline;

    app::Service* CreateFixtureFramePipeline(void*) noexcept { return &g_framePipeline; }
    void DestroyFixtureFramePipeline(app::Service*, void*) noexcept {}

    void TestRealServiceCompositionShape() noexcept
    {
        app::EngineHost host;
        app::HostFailure failure;
        Check(host.RegisterModule({engine::EngineModuleId, "engineFixture", 1}, &failure), "register fixture engine module");
        Check(host.RegisterModule({editor::EditorModuleId, "editorFixture", 1}, &failure), "register fixture editor module");

        constexpr app::CapabilityId frameCapabilities[]{engine::FramePipelineCapabilityId};
        app::ServiceDescriptor frameDescriptor;
        frameDescriptor.id = engine::FramePipelineServiceId;
        frameDescriptor.name = "frameFixture";
        frameDescriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        frameDescriptor.scope = app::ServiceScope::Engine;
        frameDescriptor.affinity = app::ThreadAffinity::MainThread;
        frameDescriptor.provides = {frameCapabilities, 1};
        frameDescriptor.create = &CreateFixtureFramePipeline;
        frameDescriptor.destroy = &DestroyFixtureFramePipeline;
        Check(host.RegisterService(engine::EngineModuleId, frameDescriptor, &failure), "register frame capability fixture");
        Check(editor::RegisterEditorActivityService(host, &failure), "register production editor Activity service descriptor");
        Check(host.Compile(app::ApplicationProfile::Editor, &failure), "compile editor Activity service fixture");
        Check(host.Start(&failure), "start editor Activity service fixture");
        editor::EditorActivityService* const activity = editor::FindEditorActivityService(host);
        Check(activity != nullptr && activity->GetStatus().state == editor::EditorActivityServiceState::Ready,
              "real Activity service is discoverable and ready");
        Check(g_framePipeline.registrations == 1u && g_framePipeline.participant == editor::EditorActivityFrameParticipantId &&
                  g_framePipeline.phase == engine::FramePhase::BeginFrame && g_framePipeline.dependencyCount == 1u &&
                  g_framePipeline.dependency == engine::InputFrameParticipantId && g_framePipeline.callback != nullptr &&
                  g_framePipeline.userData != nullptr,
              "real Activity participant is BeginFrame after Input with production callback/userData");
        Check(host.Shutdown(&failure), "shutdown editor Activity service fixture");
    }
} // namespace

int main()
{
    vanguard::concurrency::InitializeMainThread();
    Check(memory::Initialize(), "memory initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    TestCoreIngressCaptureAndRestart();
    TestSourceSnapshotsAndCancellation();
    TestSourceRegistryBoundsAndGenerations();
    TestSourceWriterValidation();
    TestConservativeRowReservationPreservesLaterStaleRows();
    TestTotalRowsAndCancelQueueBounds();
    TestRealServiceCompositionShape();

    if (g_failures == 0)
        std::printf("Vanguard editor activity core tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}

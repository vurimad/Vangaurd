#include <vanguard/memory/memory.hpp>
#include <vanguard/window/window.hpp>

#include <cstdio>

namespace
{
    namespace win = vanguard::window;
    vanguard::u32 g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition) return;
        std::printf("[windowTests] FAILED: %s\n", message);
        ++g_failures;
    }

    class FakeWindowBackend final : public win::IWindowBackend
    {
    public:
        struct Window
        {
            win::BackendWindowId id;
            win::BackendWindowState state;
            char title[win::MaximumWindowTitleBytes]{};
            bool alive = false;
        };

        FakeWindowBackend() noexcept
        {
            displays[0].id = {101};
            displays[0].fingerprint = 0x1111;
            displays[0].bounds = {{0, 0}, {2560, 1440}};
            displays[0].workArea = {{0, 0}, {2560, 1400}};
            displays[0].desktopPixelExtent = {2560, 1440};
            displays[0].desktopRefreshRate = {144, 1};
            displays[0].contentScale = 1.0f;
            displays[0].primary = true;
            displays[0].hdrCapable = true;
            displays[1].id = {202};
            displays[1].fingerprint = 0x2222;
            displays[1].bounds = {{-1920, 0}, {1920, 1080}};
            displays[1].workArea = {{-1920, 0}, {1920, 1040}};
            displays[1].desktopPixelExtent = {1920, 1080};
            displays[1].desktopRefreshRate = {60, 1};
            displays[1].contentScale = 1.25f;
            displayCount = 2;
        }

        [[nodiscard]] win::BackendStatus EnumerateDisplays(win::BackendDisplaySnapshot* const destination,
                                                            const vanguard::u32 capacity,
                                                            vanguard::u32& count) noexcept override
        {
            if (failNext) return ConsumeFailure();
            if (destination == nullptr || capacity < displayCount)
                return win::BackendStatus::Failure(1, "fake display destination is too small");
            for (vanguard::u32 index = 0; index < displayCount; ++index) destination[index] = displays[index];
            count = displayCount;
            return {};
        }

        [[nodiscard]] win::BackendStatus Create(const win::BackendWindowDescriptor& descriptor,
                                                win::BackendWindowId& window,
                                                win::BackendWindowState& state) noexcept override
        {
            if (failNext) return ConsumeFailure();
            for (Window& candidate : windows)
            {
                if (candidate.alive) continue;
                candidate.alive = true;
                candidate.id = {nextWindowId++};
                candidate.state.placement.position = descriptor.placement.position;
                candidate.state.placement.logicalExtent = descriptor.placement.logicalExtent;
                candidate.state.placement.display = descriptor.placement.display;
                candidate.state.placement.mode = descriptor.placement.mode;
                candidate.state.placement.visible = descriptor.placement.visible;
                candidate.state.pixelExtent = descriptor.placement.logicalExtent;
                candidate.state.safeArea = {{0, 0}, descriptor.placement.logicalExtent};
                candidate.state.contentScale = 1.0f;
                CopyTitle(descriptor.title, candidate.title);
                window = candidate.id;
                state = candidate.state;
                return {};
            }
            return win::BackendStatus::Failure(2, "fake window capacity exhausted");
        }

        [[nodiscard]] win::BackendStatus ApplyWindowState(const win::BackendWindowId window,
                                                          const win::BackendWindowRequest& request,
                                                          win::BackendWindowState& state) noexcept override
        {
            if (failNext) return ConsumeFailure();
            Window* const record = Find(window);
            if (record == nullptr) return win::BackendStatus::Failure(3, "fake window is unavailable");
            if (win::HasField(request.fields, win::WindowStateField::Position))
                record->state.placement.position = request.placement.position;
            if (win::HasField(request.fields, win::WindowStateField::LogicalExtent))
            {
                record->state.placement.logicalExtent = request.placement.logicalExtent;
                record->state.pixelExtent = request.placement.logicalExtent;
                record->state.safeArea = {{0, 0}, request.placement.logicalExtent};
            }
            if (win::HasField(request.fields, win::WindowStateField::Display))
                record->state.placement.display = request.placement.display;
            if (win::HasField(request.fields, win::WindowStateField::Mode))
                record->state.placement.mode = request.placement.mode;
            if (win::HasField(request.fields, win::WindowStateField::Visibility))
                record->state.placement.visible = request.placement.visible;
            state = record->state;
            return {};
        }

        [[nodiscard]] win::BackendStatus SetWindowTitle(const win::BackendWindowId window,
                                                        const char* const title) noexcept override
        {
            if (failNext) return ConsumeFailure();
            Window* const record = Find(window);
            if (record == nullptr) return win::BackendStatus::Failure(4, "fake window is unavailable");
            CopyTitle(title, record->title);
            return {};
        }

        [[nodiscard]] win::BackendStatus DestroyWindow(const win::BackendWindowId window) noexcept override
        {
            if (failNext) return ConsumeFailure();
            Window* const record = Find(window);
            if (record == nullptr) return win::BackendStatus::Failure(5, "fake window is unavailable");
            record->alive = false;
            return {};
        }

        [[nodiscard]] win::BackendWindowEvent Event(const win::BackendWindowId window,
                                                    const win::BackendEventType type,
                                                    const vanguard::u64 timestamp = 1) noexcept
        {
            win::BackendWindowEvent event;
            event.type = type;
            event.window = window;
            event.timestampNanoseconds = timestamp;
            Window* const record = Find(window);
            if (record != nullptr) event.state = record->state;
            return event;
        }

        [[nodiscard]] win::BackendWindowId IdAt(const vanguard::u32 index) const noexcept
        {
            return index < win::MaximumWindows ? windows[index].id : win::BackendWindowId{};
        }

        void FailNext() noexcept { failNext = true; }

        win::BackendDisplaySnapshot displays[win::MaximumDisplays]{};
        vanguard::u32 displayCount = 0;

    private:
        static void CopyTitle(const char* source, char* destination) noexcept
        {
            vanguard::u32 index = 0;
            if (source != nullptr)
                while (index + 1u < win::MaximumWindowTitleBytes && source[index] != '\0')
                {
                    destination[index] = source[index];
                    ++index;
                }
            destination[index] = '\0';
        }

        [[nodiscard]] Window* Find(const win::BackendWindowId id) noexcept
        {
            for (Window& record : windows) if (record.alive && record.id == id) return &record;
            return nullptr;
        }

        [[nodiscard]] win::BackendStatus ConsumeFailure() noexcept
        {
            failNext = false;
            return win::BackendStatus::Failure(99, "requested fake backend failure");
        }

        Window windows[win::MaximumWindows]{};
        vanguard::u64 nextWindowId = 1;
        bool failNext = false;
    };
}

int main()
{
    Check(vanguard::memory::Initialize(), "memory initialization");

    FakeWindowBackend backend;
    win::WindowManager manager;
    win::Failure failure;
    Check(manager.Initialize(backend, &failure), "WindowManager initialization");
    Check(manager.IsInitialized() && manager.GetStats().activeDisplays == 2 &&
              manager.PrimaryDisplay().IsValid(),
          "initial display topology publication");

    win::WindowEventCursor firstConsumer;
    win::WindowEventCursor secondConsumer;
    Check(manager.CreateEventCursor(win::EventCursorOrigin::OldestAvailable, firstConsumer) &&
              manager.CreateEventCursor(win::EventCursorOrigin::OldestAvailable, secondConsumer),
          "independent event cursor creation");

    win::WindowDescriptor invalidDescriptor;
    invalidDescriptor.title = "";
    win::WindowHandle invalidWindow;
    Check(!manager.Create(invalidDescriptor, invalidWindow, &failure) &&
              failure.code == win::FailureCode::InvalidDescriptor,
          "invalid window descriptor rejection");

    win::WindowDescriptor primaryDescriptor;
    primaryDescriptor.title = "Vanguard";
    primaryDescriptor.placement.position = {-640, 80};
    primaryDescriptor.placement.logicalExtent = {1600, 900};
    primaryDescriptor.flags = win::WindowFlag::Resizable | win::WindowFlag::HighPixelDensity |
                              win::WindowFlag::AcceptFileDrop;
    win::WindowHandle primary;
    Check(manager.Create(primaryDescriptor, primary, &failure), "primary window creation");

    win::WindowSnapshot primarySnapshot;
    Check(manager.Snapshot(primary, primarySnapshot) && primarySnapshot.lifecycle == win::WindowLifecycleState::Alive &&
              primarySnapshot.requested.position == win::WindowPoint{-640, 80} &&
              primarySnapshot.nativeState.pixelExtent == win::WindowExtent{1600, 900} &&
              primarySnapshot.stateRevision == 1 && primarySnapshot.pixelExtentRevision == 1 &&
              primarySnapshot.surfaceRevision == 1,
          "created window snapshot and revision contract");

    win::WindowDescriptor childDescriptor;
    childDescriptor.title = "Inspector";
    childDescriptor.role = win::WindowRole::Utility;
    childDescriptor.relationship = win::WindowRelationship::Owned;
    childDescriptor.parent = primary;
    childDescriptor.placement.logicalExtent = {640, 480};
    win::WindowHandle child;
    Check(manager.Create(childDescriptor, child, &failure), "owned window creation");
    Check(!manager.DestroyWindow(primary, &failure) && failure.code == win::FailureCode::ParentHasChildren,
          "parent destruction is rejected while children remain alive");

    win::WindowStateRequest stateRequest;
    stateRequest.fields = win::WindowStateField::Position | win::WindowStateField::LogicalExtent;
    stateRequest.placement.position = {-1200, 160};
    stateRequest.placement.logicalExtent = {1280, 720};
    Check(manager.RequestState(primary, stateRequest, &failure) && manager.Snapshot(primary, primarySnapshot) &&
              primarySnapshot.requested.position == stateRequest.placement.position &&
              primarySnapshot.nativeState.placement.logicalExtent == stateRequest.placement.logicalExtent &&
              primarySnapshot.pixelExtentRevision == 2,
          "requested and native window state transaction");

    backend.FailNext();
    stateRequest.placement.logicalExtent = {1920, 1080};
    const win::WindowPlacement stateBeforeFailure = primarySnapshot.requested;
    Check(!manager.RequestState(primary, stateRequest, &failure) &&
              failure.code == win::FailureCode::BackendFailure && manager.Snapshot(primary, primarySnapshot) &&
              primarySnapshot.requested.logicalExtent == stateBeforeFailure.logicalExtent,
          "failed backend state request preserves the previous valid state");
    Check(manager.SetTitle(primary, "Vanguard Runtime", &failure) && manager.Snapshot(primary, primarySnapshot) &&
              primarySnapshot.title[9] == 'R',
          "window title is copied into manager-owned state");

    const win::BackendWindowId primaryBackend = backend.IdAt(0);
    win::BackendWindowEvent pixelEvent = backend.Event(primaryBackend, win::BackendEventType::PixelExtentChanged, 42);
    pixelEvent.state.pixelExtent = {2560, 1440};
    pixelEvent.state.contentScale = 2.0f;
    Check(manager.ProcessBackendEvent(pixelEvent, &failure) && manager.Snapshot(primary, primarySnapshot) &&
              primarySnapshot.nativeState.pixelExtent == win::WindowExtent{2560, 1440} &&
              primarySnapshot.pixelExtentRevision == 3,
          "backend pixel extent change updates the authoritative snapshot");

    Check(manager.ProcessBackendEvent(backend.Event(primaryBackend, win::BackendEventType::CloseRequested, 50),
                                      &failure) &&
              manager.Snapshot(primary, primarySnapshot) && primarySnapshot.closeRequestSerial == 1 &&
              manager.ResolveCloseRequest(primary, 1, win::CloseDecision::Defer, &failure) &&
              !manager.ResolveCloseRequest(primary, 2, win::CloseDecision::Reject, &failure) &&
              failure.code == win::FailureCode::InvalidCloseSerial &&
              manager.ResolveCloseRequest(primary, 1, win::CloseDecision::Reject, &failure),
          "close requests are serial-bound and explicitly deferrable or rejectable");
    Check(manager.ProcessBackendEvent(backend.Event(primaryBackend, win::BackendEventType::CloseRequested, 60),
                                      &failure) && manager.Snapshot(primary, primarySnapshot) &&
              primarySnapshot.closeRequestSerial == 2 &&
              !manager.ResolveCloseRequest(primary, 2, win::CloseDecision::Accept, &failure) &&
              failure.code == win::FailureCode::ParentHasChildren,
          "accepted parent close still respects ownership ordering");
    Check(manager.DestroyWindow(child, &failure) &&
              manager.ResolveCloseRequest(primary, 2, win::CloseDecision::Accept, &failure) &&
              !manager.Snapshot(primary, primarySnapshot) &&
              !manager.DestroyWindow(primary, &failure) && failure.code == win::FailureCode::InvalidHandle,
          "ordered destruction invalidates generational window handles");

    win::DisplaySnapshot secondary;
    win::DisplaySnapshot displaySnapshots[win::MaximumDisplays]{};
    const vanguard::u32 displaysBeforeRemoval = manager.VisitDisplays(displaySnapshots, win::MaximumDisplays);
    for (vanguard::u32 index = 0; index < displaysBeforeRemoval; ++index)
        if (!displaySnapshots[index].primary) secondary = displaySnapshots[index];
    backend.displayCount = 1;
    Check(manager.RefreshDisplays(&failure) && secondary.handle.IsValid() &&
              !manager.Snapshot(secondary.handle, secondary),
          "display removal invalidates its generational handle");
    backend.displays[1].id = {303};
    backend.displays[1].fingerprint = 0x3333;
    backend.displays[1].bounds = {{2560, 0}, {3840, 2160}};
    backend.displays[1].workArea = {{2560, 0}, {3840, 2100}};
    backend.displays[1].desktopPixelExtent = {3840, 2160};
    backend.displays[1].desktopRefreshRate = {120, 1};
    backend.displays[1].contentScale = 1.5f;
    backend.displayCount = 2;
    Check(manager.RefreshDisplays(&failure) && manager.GetStats().activeDisplays == 2 &&
              manager.GetStats().topologyRevision >= 3,
          "display slot reuse receives a new generation and topology revision");

    for (vanguard::u32 index = 0; index < win::MaximumDisplays; ++index)
    {
        backend.displays[index].id = {1000u + index};
        backend.displays[index].fingerprint = 0x10000u + index;
        backend.displays[index].bounds = {{static_cast<vanguard::i32>(index * 1280u), 0}, {1280, 720}};
        backend.displays[index].workArea = backend.displays[index].bounds;
        backend.displays[index].desktopPixelExtent = {1280, 720};
        backend.displays[index].desktopRefreshRate = {60, 1};
        backend.displays[index].contentScale = 1.0f;
        backend.displays[index].primary = index == 0;
    }
    backend.displayCount = win::MaximumDisplays;
    Check(manager.RefreshDisplays(&failure) && manager.GetStats().activeDisplays == win::MaximumDisplays,
          "display registry accepts a complete capacity topology");
    win::DisplaySnapshot replacedDisplay;
    static_cast<void>(manager.Snapshot(manager.PrimaryDisplay(), replacedDisplay));
    for (vanguard::u32 index = 0; index < win::MaximumDisplays; ++index)
    {
        backend.displays[index].id = {2000u + index};
        backend.displays[index].fingerprint = 0x20000u + index;
    }
    Check(manager.RefreshDisplays(&failure) && manager.GetStats().activeDisplays == win::MaximumDisplays &&
              !manager.Snapshot(replacedDisplay.handle, replacedDisplay),
          "full-capacity display replacement is planned transactionally and invalidates old generations");
    backend.displayCount = 2;
    backend.displays[0].id = {101};
    backend.displays[0].fingerprint = 0x1111;
    backend.displays[0].primary = true;
    backend.displays[1].id = {303};
    backend.displays[1].fingerprint = 0x3333;
    backend.displays[1].primary = false;
    Check(manager.RefreshDisplays(&failure) && manager.GetStats().activeDisplays == 2,
          "display topology contracts back to the active set");

    win::WindowDescriptor journalDescriptor;
    journalDescriptor.title = "Journal";
    win::WindowHandle journalWindow;
    Check(manager.Create(journalDescriptor, journalWindow, &failure), "event journal fixture creation");
    const win::BackendWindowId journalBackend = backend.IdAt(0);
    win::WindowEventCursor slowConsumer;
    Check(manager.CreateEventCursor(win::EventCursorOrigin::OldestAvailable, slowConsumer),
          "slow event consumer cursor creation");
    for (vanguard::u32 index = 0; index < win::WindowEventJournalCapacity + 8u; ++index)
        Check(manager.ProcessBackendEvent(backend.Event(journalBackend, win::BackendEventType::Exposed, index), &failure),
              "event journal stress publication");
    win::WindowEvent readBuffer[32]{};
    const win::WindowEventReadResult slowRead = manager.ReadEvents(slowConsumer, {readBuffer, 32});
    Check(slowRead.count == 32 && slowRead.lostEvents != 0 && manager.GetStats().overwrittenEvents != 0,
          "bounded event journal reports slow-consumer loss and supports snapshot recovery");
    const win::WindowEventReadResult firstRead = manager.ReadEvents(firstConsumer, {readBuffer, 32});
    const win::WindowEventReadResult secondRead = manager.ReadEvents(secondConsumer, {readBuffer, 32});
    Check(firstRead.count == secondRead.count && firstRead.newestSequence == secondRead.newestSequence,
          "event cursors consume the journal independently");

    Check(!manager.Shutdown(&failure) && failure.code == win::FailureCode::WindowsRemainAlive,
          "shutdown rejects live windows");
    Check(manager.DestroyWindow(journalWindow, &failure) && manager.Shutdown(&failure) && !manager.IsInitialized(),
          "explicit window teardown permits clean shutdown");

    if (g_failures == 0) std::printf("[windowTests] all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}

#include <vanguard/concurrency/thread.hpp>
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
            if (reenterOnCreate && manager != nullptr)
            {
                reenterOnCreate = false;
                win::Failure failure{};
                win::BackendWindowEvent event{};
                reentryRejected = !manager->ProcessBackendEvent(event, &failure) &&
                                  failure.code == win::FailureCode::BackendReentry;
            }
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

        [[nodiscard]] win::BackendStatus ResolvePresentationSurface(
            const win::BackendWindowId window, win::NativePresentationSurface& surface) noexcept override
        {
            surface = {};
            if (failNext) return ConsumeFailure();
            Window* const record = Find(window);
            if (record == nullptr) return win::BackendStatus::Failure(5, "fake window is unavailable");
            surface = {win::NativePresentationSurfaceKind::Win32, record, nullptr};
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
            win::BackendWindowEvent event{};
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

        win::WindowManager* manager = nullptr;
        bool reenterOnCreate = false;
        bool reentryRejected = false;

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

    class WrongThreadRejectWorker final : public vanguard::concurrency::Thread
    {
    public:
        explicit WrongThreadRejectWorker(win::WindowManager& manager) noexcept
            : Thread("windowRejectWorker"), m_manager(manager)
        {
        }

        void ThreadFunction() noexcept override
        {
            for (vanguard::u32 index = 0; index < 1000; ++index)
            {
                win::Failure failure{};
                static_cast<void>(m_manager.SetTitle({}, "wrong thread", &failure));
                if (failure.code != win::FailureCode::WrongThread) m_failed = true;
            }
        }

        [[nodiscard]] bool Failed() const noexcept { return m_failed; }

    private:
        win::WindowManager& m_manager;
        bool m_failed = false;
    };

    class PresentationOwnershipWorker final : public vanguard::concurrency::Thread
    {
    public:
        PresentationOwnershipWorker(win::WindowManager& manager, const win::WindowHandle window,
                                    win::PresentationAttachmentHandle& attachment, const bool detach) noexcept
            : Thread("presentationOwner"), m_manager(manager), m_window(window), m_attachment(attachment),
              m_detach(detach)
        {
        }

        void ThreadFunction() noexcept override
        {
            win::Failure failure{};
            m_succeeded = m_detach ? m_manager.DetachPresentation(m_attachment, &failure)
                                   : m_manager.AttachPresentation(m_window, m_attachment, &failure);
            m_failureCode = failure.code;
        }

        [[nodiscard]] bool Succeeded() const noexcept { return m_succeeded; }
        [[nodiscard]] win::FailureCode FailureCode() const noexcept { return m_failureCode; }

    private:
        win::WindowManager& m_manager;
        win::WindowHandle m_window;
        win::PresentationAttachmentHandle& m_attachment;
        bool m_detach = false;
        bool m_succeeded = false;
        win::FailureCode m_failureCode = win::FailureCode::None;
    };
}

int main()
{
    vanguard::concurrency::InitializeMainThread();
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
    invalidDescriptor.title = "Invalid enum";
    invalidDescriptor.role = static_cast<win::WindowRole>(0xffu);
    Check(!manager.Create(invalidDescriptor, invalidWindow, &failure) &&
              failure.code == win::FailureCode::InvalidDescriptor,
          "out-of-range descriptor enums are rejected");

    win::WindowDescriptor headlessDescriptor{};
    headlessDescriptor.title = "Headless utility";
    headlessDescriptor.surfaceKind = win::PresentationSurfaceKind::None;
    win::WindowHandle headlessWindow{};
    win::PresentationAttachmentHandle headlessPresentation{};
    Check(manager.Create(headlessDescriptor, headlessWindow, &failure) &&
              !manager.AttachPresentation(headlessWindow, headlessPresentation, &failure) &&
              failure.code == win::FailureCode::PresentationUnavailable &&
              manager.DestroyWindow(headlessWindow, &failure),
          "non-presentable utility windows reject renderer attachment explicitly");

    win::WindowDescriptor primaryDescriptor;
    primaryDescriptor.title = "Vanguard";
    primaryDescriptor.placement.position = {-640, 80};
    primaryDescriptor.placement.logicalExtent = {1600, 900};
    primaryDescriptor.flags = win::WindowFlag::Resizable | win::WindowFlag::HighPixelDensity |
                               win::WindowFlag::AcceptFileDrop;
    win::WindowHandle primary;
    backend.manager = &manager;
    backend.reenterOnCreate = true;
    Check(manager.Create(primaryDescriptor, primary, &failure), "primary window creation");
    Check(backend.reentryRejected, "synchronous backend re-entry is rejected without deadlocking");

    const win::ManagerStats rejectsBeforeWorkers = manager.GetStats();
    WrongThreadRejectWorker rejectWorkers[] = {WrongThreadRejectWorker(manager), WrongThreadRejectWorker(manager),
                                                WrongThreadRejectWorker(manager), WrongThreadRejectWorker(manager)};
    for (WrongThreadRejectWorker& worker : rejectWorkers) worker.InitThread();
    for (WrongThreadRejectWorker& worker : rejectWorkers) worker.JoinThread();
    bool rejectWorkersSucceeded = true;
    for (const WrongThreadRejectWorker& worker : rejectWorkers)
        rejectWorkersSucceeded = rejectWorkersSucceeded && !worker.Failed();
    Check(rejectWorkersSucceeded &&
              manager.GetStats().rejectedOperations == rejectsBeforeWorkers.rejectedOperations + 4000u,
          "concurrent wrong-thread rejections are counted atomically");

    win::WindowSnapshot primarySnapshot;
    Check(manager.Snapshot(primary, primarySnapshot) && primarySnapshot.lifecycle == win::WindowLifecycleState::Alive &&
              primarySnapshot.requested.position == win::WindowPoint{-640, 80} &&
              primarySnapshot.nativeState.pixelExtent == win::WindowExtent{1600, 900} &&
              primarySnapshot.stateRevision == 1 && primarySnapshot.pixelExtentRevision == 1 &&
              primarySnapshot.surfaceRevision == 1,
          "created window snapshot and revision contract");

    win::PresentationAttachmentHandle presentation{};
    PresentationOwnershipWorker presentationAttachWorker(manager, primary, presentation, false);
    presentationAttachWorker.InitThread();
    presentationAttachWorker.JoinThread();
    Check(presentationAttachWorker.Succeeded() &&
              presentationAttachWorker.FailureCode() == win::FailureCode::None && presentation.IsValid() &&
              manager.Snapshot(primary, primarySnapshot) && primarySnapshot.presentation == presentation &&
              manager.GetStats().activePresentationAttachments == 1,
          "renderer-thread presentation attachment receives independent generational identity");
    win::PresentationAttachmentSnapshot presentationSnapshot{};
    Check(manager.Snapshot(presentation, presentationSnapshot) && presentationSnapshot.window == primary &&
              win::HasRequirement(presentationSnapshot.requirements,
                                  win::PresentationRequirement::SurfaceReconfigure) &&
              win::HasRequirement(presentationSnapshot.requirements,
                                  win::PresentationRequirement::PixelExtentResize),
          "new presentation attachment requests initial surface creation and pixel extent setup");
    win::NativePresentationSurface nativeSurface{};
    Check(manager.ResolvePresentationSurface(presentation, nativeSurface, &failure) && nativeSurface.IsValid() &&
              nativeSurface.kind == win::NativePresentationSurfaceKind::Win32,
          "presentation attachment resolves its backend-owned native surface without exposing backend window identity");
    win::PresentationAttachmentHandle duplicatePresentation{};
    Check(!manager.AttachPresentation(primary, duplicatePresentation, &failure) &&
              failure.code == win::FailureCode::PresentationAlreadyAttached,
          "a native window accepts only one presentation owner");
    Check(!manager.AcknowledgePresentation(presentation, {}, &failure) &&
              failure.code == win::FailureCode::InvalidPresentationRevision,
          "zero presentation acknowledgements are rejected");
    Check(manager.AcknowledgePresentation(
              presentation,
              {presentationSnapshot.requiredPixelExtentRevision, presentationSnapshot.requiredSurfaceRevision},
              &failure) && manager.Snapshot(presentation, presentationSnapshot) &&
              presentationSnapshot.requirements == win::PresentationRequirement::None,
          "renderer acknowledgement clears exactly the initial presentation work it completed");

    win::WindowDescriptor childDescriptor;
    childDescriptor.title = "Inspector";
    childDescriptor.role = win::WindowRole::Utility;
    childDescriptor.relationship = win::WindowRelationship::Owned;
    childDescriptor.parent = primary;
    childDescriptor.placement.logicalExtent = {640, 480};
    win::WindowHandle child;
    Check(manager.Create(childDescriptor, child, &failure), "owned window creation");
    Check(!manager.DestroyWindow(primary, &failure) &&
              failure.code == win::FailureCode::PresentationStillAttached,
          "native window destruction is rejected while renderer presentation resources remain attached");

    win::WindowStateRequest stateRequest;
    stateRequest.fields = win::WindowStateField::Position | win::WindowStateField::LogicalExtent;
    stateRequest.placement.position = {-1200, 160};
    stateRequest.placement.logicalExtent = {1280, 720};
    Check(manager.RequestState(primary, stateRequest, &failure) && manager.Snapshot(primary, primarySnapshot) &&
              primarySnapshot.requested.position == stateRequest.placement.position &&
              primarySnapshot.nativeState.placement.logicalExtent == stateRequest.placement.logicalExtent &&
              primarySnapshot.pixelExtentRevision == 2,
          "requested and native window state transaction");
    Check(manager.Snapshot(presentation, presentationSnapshot) &&
              win::HasRequirement(presentationSnapshot.requirements,
                                  win::PresentationRequirement::PixelExtentResize) &&
              !win::HasRequirement(presentationSnapshot.requirements,
                                   win::PresentationRequirement::SurfaceReconfigure),
          "pixel-only changes request resize work without forcing surface reconfiguration");
    const vanguard::u64 observedPixelRevision = presentationSnapshot.requiredPixelExtentRevision;

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
    Check(manager.AcknowledgePresentation(presentation, {observedPixelRevision, 0}, &failure) &&
              manager.Snapshot(presentation, presentationSnapshot) &&
              presentationSnapshot.acknowledgedPixelExtentRevision == observedPixelRevision &&
              win::HasRequirement(presentationSnapshot.requirements,
                                  win::PresentationRequirement::PixelExtentResize),
          "an acknowledgement for completed older work remains pending when the window changed concurrently");
    Check(manager.AcknowledgePresentation(
              presentation, {presentationSnapshot.requiredPixelExtentRevision, 0}, &failure),
          "renderer can subsequently catch up to the newest pixel extent revision");

    win::BackendWindowEvent compositeEvent = pixelEvent;
    compositeEvent.type = win::BackendEventType::Resized;
    compositeEvent.state.placement.position = {-900, 120};
    compositeEvent.state.placement.logicalExtent = {1440, 810};
    compositeEvent.state.placement.mode = win::WindowMode::BorderlessFullscreen;
    compositeEvent.state.placement.visible = false;
    compositeEvent.state.pixelExtent = {2880, 1620};
    compositeEvent.state.safeArea = {{0, 0}, {1440, 810}};
    Check(manager.ProcessBackendEvent(compositeEvent, &failure) && manager.Snapshot(primary, primarySnapshot) &&
              primarySnapshot.requested.position == compositeEvent.state.placement.position &&
              primarySnapshot.requested.logicalExtent == compositeEvent.state.placement.logicalExtent &&
              primarySnapshot.requested.mode == win::WindowMode::BorderlessFullscreen &&
              !primarySnapshot.requested.visible,
          "backend events reconcile every externally authoritative placement field");
    Check(manager.Snapshot(presentation, presentationSnapshot) &&
              win::HasRequirement(presentationSnapshot.requirements,
                                  win::PresentationRequirement::SurfaceReconfigure) &&
              win::HasRequirement(presentationSnapshot.requirements,
                                  win::PresentationRequirement::PixelExtentResize) &&
              win::HasRequirement(presentationSnapshot.requirements, win::PresentationRequirement::Suspended),
          "mode, extent, and visibility changes expose precise render-safe presentation requirements");
    Check(!manager.AcknowledgePresentation(
              presentation,
              {presentationSnapshot.requiredPixelExtentRevision + 1u,
               presentationSnapshot.requiredSurfaceRevision},
              &failure) && failure.code == win::FailureCode::InvalidPresentationRevision,
          "presentation cannot acknowledge window revisions that do not exist");
    Check(manager.AcknowledgePresentation(
              presentation,
              {presentationSnapshot.requiredPixelExtentRevision, presentationSnapshot.requiredSurfaceRevision},
              &failure) && manager.Snapshot(presentation, presentationSnapshot) &&
              presentationSnapshot.requirements == win::PresentationRequirement::Suspended,
          "completed reconfiguration leaves only the independently derived suspension state");
    Check(!manager.AcknowledgePresentation(
              presentation, {presentationSnapshot.acknowledgedPixelExtentRevision - 1u, 0}, &failure) &&
              failure.code == win::FailureCode::InvalidPresentationRevision,
          "presentation acknowledgements cannot regress completed GPU work");

    win::BackendWindowEvent invalidStateEvent = compositeEvent;
    invalidStateEvent.state.pixelExtent = {};
    const vanguard::u64 revisionBeforeInvalidState = primarySnapshot.stateRevision;
    Check(!manager.ProcessBackendEvent(invalidStateEvent, &failure) &&
              failure.code == win::FailureCode::BackendFailure && manager.Snapshot(primary, primarySnapshot) &&
              primarySnapshot.stateRevision == revisionBeforeInvalidState,
          "invalid backend pixel extents are rejected without mutating window state");

    win::BackendWindowEvent invalidTypeEvent{};
    invalidTypeEvent.type = static_cast<win::BackendEventType>(0xffu);
    invalidTypeEvent.window = primaryBackend;
    Check(!manager.ProcessBackendEvent(invalidTypeEvent, &failure) &&
              failure.code == win::FailureCode::BackendFailure,
          "unknown backend event types are rejected explicitly");

    win::PresentationAttachmentSnapshot presentationSnapshots[win::MaximumPresentationAttachments]{};
    Check(manager.VisitPresentationAttachments(presentationSnapshots, win::MaximumPresentationAttachments) == 1 &&
              presentationSnapshots[0].handle == presentation,
          "presentation attachments are inspectable without exposing renderer objects");
    PresentationOwnershipWorker presentationDetachWorker(manager, primary, presentation, true);
    presentationDetachWorker.InitThread();
    presentationDetachWorker.JoinThread();
    Check(presentationDetachWorker.Succeeded() &&
              presentationDetachWorker.FailureCode() == win::FailureCode::None &&
              !manager.Snapshot(presentation, presentationSnapshot) &&
              manager.GetStats().activePresentationAttachments == 0 &&
              manager.Snapshot(primary, primarySnapshot) && !primarySnapshot.presentation.IsValid(),
          "renderer-thread detach invalidates its generation and releases native window destruction ordering");
    Check(!manager.DetachPresentation(presentation, &failure) && failure.code == win::FailureCode::InvalidHandle,
          "stale presentation attachment handles are rejected");
    Check(!manager.DestroyWindow(primary, &failure) && failure.code == win::FailureCode::ParentHasChildren,
          "after presentation detach, parent destruction still respects owned-window ordering");

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
    Check(manager.DestroyWindow(child, &failure), "child destruction permits parent close progression");
    backend.FailNext();
    Check(!manager.ResolveCloseRequest(primary, 2, win::CloseDecision::Accept, &failure) &&
              failure.code == win::FailureCode::BackendFailure && manager.Snapshot(primary, primarySnapshot) &&
              primarySnapshot.lifecycle == win::WindowLifecycleState::CloseRequested,
          "failed native destruction restores the exact previous lifecycle");
    Check(manager.ResolveCloseRequest(primary, 2, win::CloseDecision::Accept, &failure) &&
              !manager.Snapshot(primary, primarySnapshot) &&
              !manager.DestroyWindow(primary, &failure) && failure.code == win::FailureCode::InvalidHandle,
          "ordered destruction invalidates generational window handles");

    win::DisplaySnapshot secondary;
    win::DisplaySnapshot displaySnapshots[win::MaximumDisplays]{};
    const vanguard::u32 displaysBeforeRemoval = manager.VisitDisplays(displaySnapshots, win::MaximumDisplays);
    for (vanguard::u32 index = 0; index < displaysBeforeRemoval; ++index)
        if (!displaySnapshots[index].primary) secondary = displaySnapshots[index];
    win::WindowDescriptor topologyWindowDescriptor{};
    topologyWindowDescriptor.title = "Topology";
    topologyWindowDescriptor.placement.display = secondary.handle;
    win::WindowHandle topologyWindow{};
    Check(manager.Create(topologyWindowDescriptor, topologyWindow, &failure),
          "display topology fixture window creation");
    win::PresentationAttachmentHandle topologyPresentation{};
    Check(manager.AttachPresentation(topologyWindow, topologyPresentation, &failure) &&
              manager.Snapshot(topologyPresentation, presentationSnapshot) &&
              manager.AcknowledgePresentation(
                  topologyPresentation,
                  {presentationSnapshot.requiredPixelExtentRevision, presentationSnapshot.requiredSurfaceRevision},
                  &failure),
          "topology fixture presentation initialization");
    const win::BackendWindowId topologyBackend = backend.IdAt(0);
    backend.displayCount = 1;
    Check(manager.RefreshDisplays(&failure) && secondary.handle.IsValid() &&
              !manager.Snapshot(secondary.handle, secondary) &&
              manager.Snapshot(topologyWindow, primarySnapshot) &&
              !primarySnapshot.requested.display.IsValid() &&
              !primarySnapshot.nativeState.placement.display.IsValid() &&
              manager.Snapshot(topologyPresentation, presentationSnapshot) &&
              win::HasRequirement(presentationSnapshot.requirements,
                                  win::PresentationRequirement::SurfaceReconfigure),
          "display removal invalidates its handle and explicitly marks affected window placement unavailable");

    win::BackendWindowEvent topologyEvent = backend.Event(topologyBackend, win::BackendEventType::DisplayChanged, 70);
    topologyEvent.state.placement.display = {101};
    Check(manager.ProcessBackendEvent(topologyEvent, &failure) && manager.Snapshot(topologyWindow, primarySnapshot) &&
              primarySnapshot.requested.display == manager.PrimaryDisplay(),
          "a later authoritative backend event reconciles an invalidated window placement");
    Check(manager.Snapshot(topologyPresentation, presentationSnapshot) &&
              manager.AcknowledgePresentation(
                  topologyPresentation,
                  {presentationSnapshot.requiredPixelExtentRevision, presentationSnapshot.requiredSurfaceRevision},
                  &failure),
          "renderer acknowledges presentation work after display reconciliation");
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

    const win::DisplayHandle primaryBeforeTopologyUpdate = manager.PrimaryDisplay();
    backend.displays[0].workArea.extent.height = 1380;
    Check(manager.RefreshDisplays(&failure) && manager.PrimaryDisplay() == primaryBeforeTopologyUpdate,
          "mutable display topology updates preserve physical display identity");

    const win::DisplayHandle primaryBeforeFingerprintReplacement = manager.PrimaryDisplay();
    backend.displays[0].fingerprint = 0x1112;
    Check(manager.RefreshDisplays(&failure) && manager.PrimaryDisplay().IsValid() &&
              !(manager.PrimaryDisplay() == primaryBeforeFingerprintReplacement) &&
              !manager.Snapshot(primaryBeforeFingerprintReplacement, secondary) &&
              manager.Snapshot(topologyWindow, primarySnapshot) && !primarySnapshot.requested.display.IsValid(),
          "a changed display fingerprint is removal plus addition rather than silent identity reuse");
    topologyEvent.state.placement.display = {101};
    Check(manager.ProcessBackendEvent(topologyEvent, &failure) &&
              !manager.DestroyWindow(topologyWindow, &failure) &&
              failure.code == win::FailureCode::PresentationStillAttached &&
              manager.DetachPresentation(topologyPresentation, &failure) &&
              manager.DestroyWindow(topologyWindow, &failure),
          "fingerprint-replaced presentation detaches before native window teardown");

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

    win::WindowEventCursor aheadCursor = slowConsumer;
    aheadCursor.nextSequence += 1000000u;
    const win::WindowEventReadResult aheadRead = manager.ReadEvents(aheadCursor, {readBuffer, 32});
    Check(aheadRead.invalidCursor && aheadRead.count == 0 && aheadRead.lostEvents == 0,
          "an event cursor ahead of the journal is rejected and recovered without unsigned underflow");
    win::WindowEventCursor foreignCursor = slowConsumer;
    ++foreignCursor.journalIdentity;
    const win::WindowEventReadResult foreignRead = manager.ReadEvents(foreignCursor, {readBuffer, 32});
    Check(foreignRead.invalidCursor && foreignRead.count == 0,
          "event cursors from another manager lifetime are rejected explicitly");
    win::WindowEventCursor invalidOriginCursor{};
    Check(!manager.CreateEventCursor(static_cast<win::EventCursorOrigin>(0xffu), invalidOriginCursor),
          "invalid event cursor origins are rejected");

    Check(!manager.Shutdown(&failure) && failure.code == win::FailureCode::WindowsRemainAlive,
          "shutdown rejects live windows");
    Check(manager.DestroyWindow(journalWindow, &failure) && manager.Shutdown(&failure) && !manager.IsInitialized(),
          "explicit window teardown permits clean shutdown");

    if (g_failures == 0) std::printf("[windowTests] all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}

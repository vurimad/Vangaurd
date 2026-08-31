#include <vanguard/window/window_manager.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace
{
    namespace win = vanguard::window;

    vanguard::concurrency::Atomic<vanguard::u64> g_nextJournalIdentity{0};

    class BackendCallGuard final
    {
    public:
        explicit BackendCallGuard(vanguard::concurrency::Atomic<bool>& active) noexcept : m_active(active)
        {
            VG_ASSERT_MSG(!m_active.Exchange(true), "window backend commands must not nest or re-enter the manager");
        }

        ~BackendCallGuard()
        {
            m_active.SetValue(false);
        }

        BackendCallGuard(const BackendCallGuard&) = delete;
        BackendCallGuard& operator=(const BackendCallGuard&) = delete;

    private:
        vanguard::concurrency::Atomic<bool>& m_active;
    };

    [[nodiscard]] bool IsValidRole(const win::WindowRole value) noexcept
    {
        switch (value)
        {
        case win::WindowRole::Primary:
        case win::WindowRole::EditorMain:
        case win::WindowRole::EditorViewport:
        case win::WindowRole::GamePreview:
        case win::WindowRole::Tool:
        case win::WindowRole::Utility:
        case win::WindowRole::Embedded:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsValidRelationship(const win::WindowRelationship value) noexcept
    {
        switch (value)
        {
        case win::WindowRelationship::Independent:
        case win::WindowRelationship::Owned:
        case win::WindowRelationship::Modal:
        case win::WindowRelationship::Embedded:
        case win::WindowRelationship::ImGuiViewport:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsValidMode(const win::WindowMode value) noexcept
    {
        switch (value)
        {
        case win::WindowMode::Windowed:
        case win::WindowMode::BorderlessFullscreen:
        case win::WindowMode::ExclusiveFullscreen:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsValidInitialPlacement(const win::InitialWindowPlacement value) noexcept
    {
        switch (value)
        {
        case win::InitialWindowPlacement::Explicit:
        case win::InitialWindowPlacement::CenteredOnDisplay:
        case win::InitialWindowPlacement::PlatformDefault:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsValidSurfaceKind(const win::PresentationSurfaceKind value) noexcept
    {
        switch (value)
        {
        case win::PresentationSurfaceKind::None:
        case win::PresentationSurfaceKind::PlatformNative:
        case win::PresentationSurfaceKind::Vulkan:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsValidBackendEventType(const win::BackendEventType value) noexcept
    {
        switch (value)
        {
        case win::BackendEventType::Shown:
        case win::BackendEventType::Hidden:
        case win::BackendEventType::Exposed:
        case win::BackendEventType::Moved:
        case win::BackendEventType::Resized:
        case win::BackendEventType::PixelExtentChanged:
        case win::BackendEventType::Minimized:
        case win::BackendEventType::Maximized:
        case win::BackendEventType::Restored:
        case win::BackendEventType::FocusGained:
        case win::BackendEventType::FocusLost:
        case win::BackendEventType::MouseEntered:
        case win::BackendEventType::MouseLeft:
        case win::BackendEventType::CloseRequested:
        case win::BackendEventType::DisplayChanged:
        case win::BackendEventType::ContentScaleChanged:
        case win::BackendEventType::SafeAreaChanged:
        case win::BackendEventType::OcclusionChanged:
        case win::BackendEventType::HdrStateChanged:
        case win::BackendEventType::Failure:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsValidFlags(const win::WindowFlag value) noexcept
    {
        constexpr vanguard::u32 valid = static_cast<vanguard::u32>(win::WindowFlag::Resizable) | static_cast<vanguard::u32>(win::WindowFlag::Borderless) |
                                        static_cast<vanguard::u32>(win::WindowFlag::AlwaysOnTop) | static_cast<vanguard::u32>(win::WindowFlag::Utility) |
                                        static_cast<vanguard::u32>(win::WindowFlag::SkipTaskbar) | static_cast<vanguard::u32>(win::WindowFlag::Transparent) |
                                        static_cast<vanguard::u32>(win::WindowFlag::AcceptFileDrop) |
                                        static_cast<vanguard::u32>(win::WindowFlag::HighPixelDensity);
        return (static_cast<vanguard::u32>(value) & ~valid) == 0;
    }

    [[nodiscard]] bool IsValidScale(const vanguard::f32 value) noexcept
    {
        return value > 0.0f && value <= 16.0f;
    }

    [[nodiscard]] bool IsValidConstraints(const win::WindowConstraints& constraints) noexcept
    {
        return constraints.minimum.IsValid() && constraints.maximum.IsValid() && constraints.minimum.width <= constraints.maximum.width &&
               constraints.minimum.height <= constraints.maximum.height;
    }

    [[nodiscard]] bool IsWithinConstraints(const win::WindowExtent extent, const win::WindowConstraints& constraints) noexcept
    {
        return extent.IsValid() && extent.width >= constraints.minimum.width && extent.height >= constraints.minimum.height &&
               extent.width <= constraints.maximum.width && extent.height <= constraints.maximum.height;
    }

    [[nodiscard]] bool CopyString(const char* const source, char* const destination, const vanguard::u32 capacity) noexcept
    {
        if (source == nullptr || source[0] == '\0' || destination == nullptr || capacity == 0)
            return false;
        vanguard::u32 length = 0;
        while (length + 1u < capacity && source[length] != '\0')
        {
            destination[length] = source[length];
            ++length;
        }
        if (source[length] != '\0')
            return false;
        destination[length] = '\0';
        return true;
    }

    [[nodiscard]] bool StringsEqual(const char* left, const char* right) noexcept
    {
        if (left == nullptr || right == nullptr)
            return left == right;
        while (*left != '\0' && *left == *right)
        {
            ++left;
            ++right;
        }
        return *left == '\0' && *right == '\0';
    }

    [[nodiscard]] bool DisplayDataEqual(const win::DisplaySnapshot& left, const win::BackendDisplaySnapshot& right) noexcept
    {
        return left.fingerprint == right.fingerprint && left.bounds == right.bounds && left.workArea == right.workArea &&
               left.desktopPixelExtent == right.desktopPixelExtent && left.desktopRefreshRate == right.desktopRefreshRate &&
               left.contentScale == right.contentScale && left.primary == right.primary && left.hdrCapable == right.hdrCapable;
    }
} // namespace

namespace vanguard::window
{
    struct WindowManager::Impl
    {
        struct WindowRecord
        {
            WindowSnapshot snapshot;
            BackendWindowId backend;
            u32 generation = 1;
            bool occupied = false;
        };

        struct DisplayRecord
        {
            DisplaySnapshot snapshot;
            BackendDisplayId backend;
            u32 generation = 1;
            bool occupied = false;
        };

        struct PresentationRecord
        {
            WindowHandle window;
            u64 acknowledgedPixelExtentRevision = 0;
            u64 acknowledgedSurfaceRevision = 0;
            u32 generation = 1;
            bool occupied = false;
        };

        mutable concurrency::RWLock lock;
        concurrency::ThreadId ownerThread;
        concurrency::Atomic<bool> backendCallActive{false};
        concurrency::Atomic<u64> rejectedOperations{0};
        IWindowBackend* backend = nullptr;
        WindowRecord windows[MaximumWindows]{};
        DisplayRecord displays[MaximumDisplays]{};
        PresentationRecord presentations[MaximumPresentationAttachments]{};
        WindowEvent events[WindowEventJournalCapacity]{};
        ManagerStats stats;
        u64 nextEventSequence = 1;
        u64 oldestEventSequence = 1;
        u32 storedEventCount = 0;
        u64 journalIdentity = 0;

        [[nodiscard]] bool IsOwnerThread() const noexcept
        {
            return ownerThread == concurrency::ThreadId::GetCurrentThread();
        }

        [[nodiscard]] bool IsBackendReentry() const noexcept
        {
            return IsOwnerThread() && backendCallActive.GetValue();
        }

        void Reject(Failure* const failure, const FailureCode code, const char* const message, const WindowHandle window = {}, const DisplayHandle display = {},
                    const i32 backendCode = 0) noexcept
        {
            static_cast<void>(rejectedOperations.Increment());
            if (failure != nullptr)
                *failure = {code, window, display, backendCode, message};
        }

        [[nodiscard]] WindowRecord* FindWindow(const WindowHandle handle) noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            WindowRecord& record = windows[handle.index];
            return record.occupied && record.generation == handle.generation ? &record : nullptr;
        }

        [[nodiscard]] const WindowRecord* FindWindow(const WindowHandle handle) const noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            const WindowRecord& record = windows[handle.index];
            return record.occupied && record.generation == handle.generation ? &record : nullptr;
        }

        [[nodiscard]] WindowRecord* FindWindow(const BackendWindowId id) noexcept
        {
            if (!id.IsValid())
                return nullptr;
            for (WindowRecord& record : windows)
                if (record.occupied && record.backend == id)
                    return &record;
            return nullptr;
        }

        [[nodiscard]] DisplayRecord* FindDisplay(const DisplayHandle handle) noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            DisplayRecord& record = displays[handle.index];
            return record.occupied && record.generation == handle.generation ? &record : nullptr;
        }

        [[nodiscard]] const DisplayRecord* FindDisplay(const DisplayHandle handle) const noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            const DisplayRecord& record = displays[handle.index];
            return record.occupied && record.generation == handle.generation ? &record : nullptr;
        }

        [[nodiscard]] DisplayRecord* FindDisplay(const BackendDisplayId id) noexcept
        {
            if (!id.IsValid())
                return nullptr;
            for (DisplayRecord& record : displays)
                if (record.occupied && record.backend == id)
                    return &record;
            return nullptr;
        }

        [[nodiscard]] const DisplayRecord* FindDisplay(const BackendDisplayId id) const noexcept
        {
            if (!id.IsValid())
                return nullptr;
            for (const DisplayRecord& record : displays)
                if (record.occupied && record.backend == id)
                    return &record;
            return nullptr;
        }

        [[nodiscard]] PresentationRecord* FindPresentation(const PresentationAttachmentHandle handle) noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            PresentationRecord& record = presentations[handle.index];
            return record.occupied && record.generation == handle.generation ? &record : nullptr;
        }

        [[nodiscard]] const PresentationRecord* FindPresentation(const PresentationAttachmentHandle handle) const noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            const PresentationRecord& record = presentations[handle.index];
            return record.occupied && record.generation == handle.generation ? &record : nullptr;
        }

        [[nodiscard]] bool BuildPresentationSnapshot(const PresentationRecord& presentation, PresentationAttachmentSnapshot& snapshot) const noexcept
        {
            const WindowRecord* const window = FindWindow(presentation.window);
            if (window == nullptr)
                return false;
            snapshot = {};
            snapshot.handle = {static_cast<u32>(&presentation - presentations), presentation.generation};
            snapshot.window = presentation.window;
            snapshot.surfaceKind = window->snapshot.surfaceKind;
            snapshot.pixelExtent = window->snapshot.nativeState.pixelExtent;
            snapshot.display = window->snapshot.nativeState.placement.display;
            snapshot.mode = window->snapshot.nativeState.placement.mode;
            snapshot.windowStateRevision = window->snapshot.stateRevision;
            snapshot.requiredPixelExtentRevision = window->snapshot.pixelExtentRevision;
            snapshot.requiredSurfaceRevision = window->snapshot.surfaceRevision;
            snapshot.acknowledgedPixelExtentRevision = presentation.acknowledgedPixelExtentRevision;
            snapshot.acknowledgedSurfaceRevision = presentation.acknowledgedSurfaceRevision;
            snapshot.sdrWhiteLevel = window->snapshot.nativeState.sdrWhiteLevel;
            snapshot.hdrHeadroom = window->snapshot.nativeState.hdrHeadroom;
            snapshot.visible = window->snapshot.nativeState.placement.visible;
            snapshot.minimized = window->snapshot.nativeState.minimized;
            snapshot.occluded = window->snapshot.nativeState.occluded;
            snapshot.hdrCapable = window->snapshot.nativeState.hdrCapable;
            if (snapshot.acknowledgedSurfaceRevision < snapshot.requiredSurfaceRevision)
                snapshot.requirements = snapshot.requirements | PresentationRequirement::SurfaceReconfigure;
            if (snapshot.acknowledgedPixelExtentRevision < snapshot.requiredPixelExtentRevision)
                snapshot.requirements = snapshot.requirements | PresentationRequirement::PixelExtentResize;
            if (!snapshot.visible || snapshot.minimized)
                snapshot.requirements = snapshot.requirements | PresentationRequirement::Suspended;
            if (snapshot.occluded)
                snapshot.requirements = snapshot.requirements | PresentationRequirement::Occluded;
            return true;
        }

        [[nodiscard]] DisplayRecord* PrimaryDisplayRecord() noexcept
        {
            for (DisplayRecord& record : displays)
                if (record.occupied && record.snapshot.primary)
                    return &record;
            for (DisplayRecord& record : displays)
                if (record.occupied)
                    return &record;
            return nullptr;
        }

        void Publish(WindowEvent event) noexcept
        {
            event.sequence = nextEventSequence++;
            events[(event.sequence - 1u) % WindowEventJournalCapacity] = event;
            if (storedEventCount < WindowEventJournalCapacity)
            {
                ++storedEventCount;
            }
            else
            {
                ++oldestEventSequence;
                ++stats.overwrittenEvents;
            }
            ++stats.publishedEvents;
        }

        void PublishWindowEvent(const WindowEventType type, const WindowRecord& record, const u64 timestampNanoseconds = 0, const i32 backendCode = 0) noexcept
        {
            WindowEvent event{};
            event.type = type;
            event.window = record.snapshot.handle;
            event.display = record.snapshot.nativeState.placement.display;
            event.timestampNanoseconds = timestampNanoseconds;
            event.closeRequestSerial = record.snapshot.closeRequestSerial;
            event.stateRevision = record.snapshot.stateRevision;
            event.position = record.snapshot.nativeState.placement.position;
            event.logicalExtent = record.snapshot.nativeState.placement.logicalExtent;
            event.pixelExtent = record.snapshot.nativeState.pixelExtent;
            event.contentScale = record.snapshot.nativeState.contentScale;
            event.backendCode = backendCode;
            Publish(event);
        }

        void PublishDisplayEvent(const WindowEventType type, const DisplayHandle display) noexcept
        {
            WindowEvent event{};
            event.type = type;
            event.display = display;
            Publish(event);
        }

        [[nodiscard]] bool MapBackendState(const BackendWindowState& source, WindowNativeState& destination, Failure* const failure,
                                           const WindowHandle window) noexcept
        {
            DisplayRecord* const display = FindDisplay(source.placement.display);
            if (display == nullptr)
            {
                Reject(failure, FailureCode::DisplayUnavailable, "window backend reported an unknown display", window);
                return false;
            }
            if (!source.placement.logicalExtent.IsValid() || !source.pixelExtent.IsValid() || !source.safeArea.extent.IsValid() ||
                !IsValidScale(source.contentScale) || !IsValidMode(source.placement.mode))
            {
                Reject(failure, FailureCode::BackendFailure, "window backend reported invalid native state", window);
                return false;
            }
            destination.placement.position = source.placement.position;
            destination.placement.logicalExtent = source.placement.logicalExtent;
            destination.placement.display = display->snapshot.handle;
            destination.placement.mode = source.placement.mode;
            destination.placement.visible = source.placement.visible;
            destination.pixelExtent = source.pixelExtent;
            destination.safeArea = source.safeArea;
            destination.contentScale = source.contentScale;
            destination.sdrWhiteLevel = source.sdrWhiteLevel;
            destination.hdrHeadroom = source.hdrHeadroom;
            destination.focused = source.focused;
            destination.mouseFocus = source.mouseFocus;
            destination.minimized = source.minimized;
            destination.maximized = source.maximized;
            destination.occluded = source.occluded;
            destination.hdrCapable = source.hdrCapable;
            return true;
        }

        void PublishStateDifferences(WindowRecord& record, const WindowNativeState& previous, const u64 timestampNanoseconds) noexcept
        {
            const WindowNativeState& current = record.snapshot.nativeState;
            if (!(previous.placement.display == current.placement.display) || previous.placement.mode != current.placement.mode ||
                previous.hdrCapable != current.hdrCapable || previous.sdrWhiteLevel != current.sdrWhiteLevel || previous.hdrHeadroom != current.hdrHeadroom)
                ++record.snapshot.surfaceRevision;
            if (!(previous.placement.position == current.placement.position))
                PublishWindowEvent(WindowEventType::Moved, record, timestampNanoseconds);
            if (!(previous.placement.logicalExtent == current.placement.logicalExtent))
                PublishWindowEvent(WindowEventType::Resized, record, timestampNanoseconds);
            if (!(previous.pixelExtent == current.pixelExtent))
            {
                ++record.snapshot.pixelExtentRevision;
                PublishWindowEvent(WindowEventType::PixelExtentChanged, record, timestampNanoseconds);
            }
            if (!(previous.placement.display == current.placement.display))
                PublishWindowEvent(WindowEventType::DisplayChanged, record, timestampNanoseconds);
            if (previous.placement.mode != current.placement.mode)
                PublishWindowEvent(WindowEventType::ModeChanged, record, timestampNanoseconds);
            if (previous.placement.visible != current.placement.visible)
                PublishWindowEvent(current.placement.visible ? WindowEventType::Shown : WindowEventType::Hidden, record, timestampNanoseconds);
            if (previous.contentScale != current.contentScale)
                PublishWindowEvent(WindowEventType::ContentScaleChanged, record, timestampNanoseconds);
            if (!(previous.safeArea == current.safeArea))
                PublishWindowEvent(WindowEventType::SafeAreaChanged, record, timestampNanoseconds);
            if (previous.focused != current.focused)
                PublishWindowEvent(current.focused ? WindowEventType::FocusGained : WindowEventType::FocusLost, record, timestampNanoseconds);
            if (previous.mouseFocus != current.mouseFocus)
                PublishWindowEvent(current.mouseFocus ? WindowEventType::MouseEntered : WindowEventType::MouseLeft, record, timestampNanoseconds);
            if (previous.minimized != current.minimized && current.minimized)
                PublishWindowEvent(WindowEventType::Minimized, record, timestampNanoseconds);
            if (previous.maximized != current.maximized && current.maximized)
                PublishWindowEvent(WindowEventType::Maximized, record, timestampNanoseconds);
            if ((previous.minimized || previous.maximized) && !current.minimized && !current.maximized)
                PublishWindowEvent(WindowEventType::Restored, record, timestampNanoseconds);
            if (previous.occluded != current.occluded)
                PublishWindowEvent(WindowEventType::OcclusionChanged, record, timestampNanoseconds);
            if (previous.hdrCapable != current.hdrCapable)
                PublishWindowEvent(WindowEventType::HdrStateChanged, record, timestampNanoseconds);
        }

        [[nodiscard]] bool HasChildren(const WindowHandle parent) const noexcept
        {
            for (const WindowRecord& record : windows)
                if (record.occupied && record.snapshot.parent == parent)
                    return true;
            return false;
        }

        void FinalizeDestroy(WindowRecord& record, const u64 timestampNanoseconds) noexcept
        {
            record.snapshot.lifecycle = WindowLifecycleState::DestroyPending;
            ++record.snapshot.stateRevision;
            PublishWindowEvent(WindowEventType::Destroyed, record, timestampNanoseconds);
            record.occupied = false;
            record.backend = {};
            record.snapshot = {};
            ++record.generation;
            if (record.generation == 0)
                record.generation = 1;
            --stats.activeWindows;
        }

        [[nodiscard]] bool DestroyRecord(WindowRecord& record, Failure* const failure, const bool closeAccepted) noexcept
        {
            const WindowHandle handle = record.snapshot.handle;
            if (record.snapshot.presentation.IsValid())
            {
                Reject(failure, FailureCode::PresentationStillAttached, "presentation resources must be detached before destroying their native window",
                       handle);
                return false;
            }
            if (HasChildren(handle))
            {
                Reject(failure, FailureCode::ParentHasChildren, "owned or embedded windows must be destroyed before their parent", handle);
                return false;
            }
            const WindowLifecycleState previousLifecycle = record.snapshot.lifecycle;
            record.snapshot.lifecycle = WindowLifecycleState::Retiring;
            ++record.snapshot.stateRevision;
            const BackendCallGuard backendCall(backendCallActive);
            const BackendStatus status = backend->DestroyWindow(record.backend);
            if (!status)
            {
                record.snapshot.lifecycle = previousLifecycle;
                ++record.snapshot.stateRevision;
                Reject(failure, FailureCode::BackendFailure, status.message != nullptr ? status.message : "window backend destruction failed", handle, {},
                       status.code);
                PublishWindowEvent(WindowEventType::BackendFailure, record, 0, status.code);
                return false;
            }
            if (closeAccepted)
                PublishWindowEvent(WindowEventType::CloseAccepted, record);
            FinalizeDestroy(record, 0);
            return true;
        }
    };

    WindowManager::~WindowManager()
    {
        if (m_impl != nullptr)
            VG_FATAL("WindowManager requires explicit successful Shutdown");
    }

    bool WindowManager::Initialize(IWindowBackend& backend, Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl != nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::AlreadyInitialized, {}, {}, 0, "WindowManager is already initialized"};
            return false;
        }
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Window, sizeof(Impl), alignof(Impl));
        if (!block)
        {
            if (failure != nullptr)
                *failure = {FailureCode::CapacityExceeded, {}, {}, 0, "WindowManager storage allocation failed"};
            return false;
        }
        m_impl = ::new (block.address) Impl();
        m_impl->backend = &backend;
        m_impl->ownerThread = concurrency::ThreadId::GetCurrentThread();
        m_impl->journalIdentity = g_nextJournalIdentity.Increment();
        if (m_impl->journalIdentity == 0)
            m_impl->journalIdentity = g_nextJournalIdentity.Increment();
        if (!RefreshDisplays(failure))
        {
            m_impl->~Impl();
            memory::MemoryBlock failedBlock{m_impl, sizeof(Impl), memory::PoolId::Window};
            memory::Free(failedBlock);
            m_impl = nullptr;
            return false;
        }
        return true;
    }

    bool WindowManager::Shutdown(Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
            return true;
        if (!m_impl->IsOwnerThread())
        {
            m_impl->Reject(failure, FailureCode::WrongThread, "WindowManager shutdown requires its owner thread");
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not re-enter WindowManager shutdown");
            return false;
        }
        m_impl->lock.Acquire();
        if (m_impl->stats.activeWindows != 0)
        {
            m_impl->Reject(failure, FailureCode::WindowsRemainAlive, "all windows must be explicitly destroyed before WindowManager shutdown");
            m_impl->lock.Release();
            return false;
        }
        m_impl->lock.Release();
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Window};
        memory::Free(block);
        m_impl = nullptr;
        return true;
    }

    bool WindowManager::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool WindowManager::RefreshDisplays(Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, {}, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (!m_impl->IsOwnerThread())
        {
            m_impl->Reject(failure, FailureCode::WrongThread, "display refresh requires the owner thread");
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not re-enter display refresh");
            return false;
        }
        BackendDisplaySnapshot backendDisplays[MaximumDisplays]{};
        u32 count = 0;
        BackendStatus status{};
        {
            const BackendCallGuard backendCall(m_impl->backendCallActive);
            status = m_impl->backend->EnumerateDisplays(backendDisplays, MaximumDisplays, count);
        }
        if (!status || count > MaximumDisplays)
        {
            m_impl->Reject(failure, status ? FailureCode::CapacityExceeded : FailureCode::BackendFailure,
                           status.message != nullptr ? status.message : "display enumeration exceeded capacity", {}, {}, status.code);
            return false;
        }

        u32 primaryCount = 0;
        for (u32 sourceIndex = 0; sourceIndex < count; ++sourceIndex)
        {
            const BackendDisplaySnapshot& source = backendDisplays[sourceIndex];
            if (!source.id.IsValid() || source.fingerprint == 0 || !source.bounds.extent.IsValid() || !source.workArea.extent.IsValid() ||
                !source.desktopPixelExtent.IsValid() || !IsValidScale(source.contentScale) || source.desktopRefreshRate.denominator == 0)
            {
                m_impl->Reject(failure, FailureCode::BackendFailure, "window backend returned an invalid display snapshot", {}, {}, status.code);
                return false;
            }
            if (source.primary)
                ++primaryCount;
            for (u32 previous = 0; previous < sourceIndex; ++previous)
            {
                if (!(backendDisplays[previous].id == source.id))
                    continue;
                m_impl->Reject(failure, FailureCode::BackendFailure, "window backend returned duplicate display identities", {}, {}, status.code);
                return false;
            }
        }
        if (primaryCount > 1)
        {
            m_impl->Reject(failure, FailureCode::BackendFailure, "window backend returned more than one primary display", {}, {}, status.code);
            return false;
        }

        m_impl->lock.Acquire();
        u32 sourceSlots[MaximumDisplays]{};
        bool claimedSlots[MaximumDisplays]{};
        for (u32 index = 0; index < MaximumDisplays; ++index)
            sourceSlots[index] = ~u32{0};

        // Preserve generations for displays that remain present, then plan unmatched displays into any unclaimed slot.
        // Planning completes before registry mutation, so a total topology replacement remains atomic even at capacity.
        for (u32 sourceIndex = 0; sourceIndex < count; ++sourceIndex)
        {
            Impl::DisplayRecord* const existing = m_impl->FindDisplay(backendDisplays[sourceIndex].id);
            if (existing == nullptr || existing->snapshot.fingerprint != backendDisplays[sourceIndex].fingerprint)
                continue;
            const u32 slot = static_cast<u32>(existing - m_impl->displays);
            sourceSlots[sourceIndex] = slot;
            claimedSlots[slot] = true;
        }
        for (u32 sourceIndex = 0; sourceIndex < count; ++sourceIndex)
        {
            if (sourceSlots[sourceIndex] != ~u32{0})
                continue;
            for (u32 slot = 0; slot < MaximumDisplays; ++slot)
            {
                if (claimedSlots[slot])
                    continue;
                sourceSlots[sourceIndex] = slot;
                claimedSlots[slot] = true;
                break;
            }
            if (sourceSlots[sourceIndex] == ~u32{0})
            {
                m_impl->Reject(failure, FailureCode::CapacityExceeded, "display registry capacity is exhausted");
                m_impl->lock.Release();
                return false;
            }
        }

        bool changed = false;
        for (u32 slot = 0; slot < MaximumDisplays; ++slot)
        {
            Impl::DisplayRecord& record = m_impl->displays[slot];
            if (!record.occupied)
                continue;
            u32 assignedSource = ~u32{0};
            for (u32 sourceIndex = 0; sourceIndex < count; ++sourceIndex)
                if (sourceSlots[sourceIndex] == slot)
                {
                    assignedSource = sourceIndex;
                    break;
                }
            if (assignedSource != ~u32{0} && record.backend == backendDisplays[assignedSource].id &&
                record.snapshot.fingerprint == backendDisplays[assignedSource].fingerprint)
                continue;
            const DisplayHandle removed = record.snapshot.handle;
            m_impl->PublishDisplayEvent(WindowEventType::DisplayRemoved, removed);
            record.occupied = false;
            record.backend = {};
            record.snapshot = {};
            ++record.generation;
            if (record.generation == 0)
                record.generation = 1;
            --m_impl->stats.activeDisplays;
            changed = true;
        }

        for (u32 sourceIndex = 0; sourceIndex < count; ++sourceIndex)
        {
            const BackendDisplaySnapshot& source = backendDisplays[sourceIndex];
            Impl::DisplayRecord& record = m_impl->displays[sourceSlots[sourceIndex]];
            if (!record.occupied)
            {
                record.occupied = true;
                record.backend = source.id;
                record.snapshot.handle = {sourceSlots[sourceIndex], record.generation};
                ++m_impl->stats.activeDisplays;
                changed = true;
                m_impl->PublishDisplayEvent(WindowEventType::DisplayAdded, record.snapshot.handle);
            }
            else if (!DisplayDataEqual(record.snapshot, source))
            {
                changed = true;
                m_impl->PublishDisplayEvent(WindowEventType::DisplayUpdated, record.snapshot.handle);
            }
            record.snapshot.fingerprint = source.fingerprint;
            record.snapshot.bounds = source.bounds;
            record.snapshot.workArea = source.workArea;
            record.snapshot.desktopPixelExtent = source.desktopPixelExtent;
            record.snapshot.desktopRefreshRate = source.desktopRefreshRate;
            record.snapshot.contentScale = source.contentScale;
            record.snapshot.primary = source.primary;
            record.snapshot.hdrCapable = source.hdrCapable;
        }
        if (changed)
            ++m_impl->stats.topologyRevision;
        for (Impl::DisplayRecord& record : m_impl->displays)
            if (record.occupied)
                record.snapshot.topologyRevision = m_impl->stats.topologyRevision;

        // Windows referencing a removed or physically replaced display remain alive, but their placement is explicitly
        // unavailable until the platform supplies a fresh authoritative state event for the new topology.
        for (Impl::WindowRecord& record : m_impl->windows)
        {
            if (!record.occupied)
                continue;
            const bool requestedUnavailable = record.snapshot.requested.display.IsValid() && m_impl->FindDisplay(record.snapshot.requested.display) == nullptr;
            const bool nativeUnavailable =
                record.snapshot.nativeState.placement.display.IsValid() && m_impl->FindDisplay(record.snapshot.nativeState.placement.display) == nullptr;
            if (!requestedUnavailable && !nativeUnavailable)
                continue;
            record.snapshot.requested.display = {};
            record.snapshot.nativeState.placement.display = {};
            ++record.snapshot.stateRevision;
            ++record.snapshot.surfaceRevision;
            m_impl->PublishWindowEvent(WindowEventType::DisplayInvalidated, record);
        }
        m_impl->lock.Release();
        return true;
    }

    bool WindowManager::Create(const WindowDescriptor& descriptor, WindowHandle& window, Failure* const failure) noexcept
    {
        window = {};
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, {}, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (!m_impl->IsOwnerThread())
        {
            m_impl->Reject(failure, FailureCode::WrongThread, "window creation requires the owner thread");
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not re-enter window creation");
            return false;
        }
        char title[MaximumWindowTitleBytes]{};
        if (!CopyString(descriptor.title, title, MaximumWindowTitleBytes) || !IsValidRole(descriptor.role) || !IsValidRelationship(descriptor.relationship) ||
            !IsValidMode(descriptor.placement.mode) || !IsValidInitialPlacement(descriptor.initialPlacement) || !IsValidSurfaceKind(descriptor.surfaceKind) ||
            !IsValidConstraints(descriptor.constraints) || !IsWithinConstraints(descriptor.placement.logicalExtent, descriptor.constraints) ||
            !IsValidFlags(descriptor.flags))
        {
            m_impl->Reject(failure, FailureCode::InvalidDescriptor, "window descriptor is invalid");
            return false;
        }

        m_impl->lock.Acquire();
        Impl::DisplayRecord* display =
            descriptor.placement.display.IsValid() ? m_impl->FindDisplay(descriptor.placement.display) : m_impl->PrimaryDisplayRecord();
        Impl::WindowRecord* parent = descriptor.parent.IsValid() ? m_impl->FindWindow(descriptor.parent) : nullptr;
        if (display == nullptr)
        {
            m_impl->Reject(failure, FailureCode::DisplayUnavailable, "requested window display is unavailable", {}, descriptor.placement.display);
            m_impl->lock.Release();
            return false;
        }
        if (descriptor.parent.IsValid() && parent == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "requested parent window is unavailable", descriptor.parent);
            m_impl->lock.Release();
            return false;
        }
        if ((descriptor.relationship != WindowRelationship::Independent && parent == nullptr) ||
            (descriptor.relationship == WindowRelationship::Independent && parent != nullptr))
        {
            m_impl->Reject(failure, FailureCode::InvalidDescriptor, "window parent and relationship are inconsistent");
            m_impl->lock.Release();
            return false;
        }
        Impl::WindowRecord* record = nullptr;
        for (Impl::WindowRecord& candidate : m_impl->windows)
        {
            if (candidate.occupied)
                continue;
            record = &candidate;
            break;
        }
        if (record == nullptr)
        {
            m_impl->Reject(failure, FailureCode::CapacityExceeded, "window registry capacity is exhausted");
            m_impl->lock.Release();
            return false;
        }

        BackendWindowDescriptor backendDescriptor{};
        backendDescriptor.title = title;
        backendDescriptor.parent = parent != nullptr ? parent->backend : BackendWindowId{};
        backendDescriptor.role = descriptor.role;
        backendDescriptor.relationship = descriptor.relationship;
        backendDescriptor.placement.position = descriptor.placement.position;
        backendDescriptor.placement.logicalExtent = descriptor.placement.logicalExtent;
        backendDescriptor.placement.display = display->backend;
        backendDescriptor.placement.mode = descriptor.placement.mode;
        backendDescriptor.placement.visible = descriptor.placement.visible;
        backendDescriptor.initialPlacement = descriptor.initialPlacement;
        backendDescriptor.constraints = descriptor.constraints;
        backendDescriptor.surfaceKind = descriptor.surfaceKind;
        backendDescriptor.flags = descriptor.flags;
        BackendWindowId backendWindow{};
        BackendWindowState backendState{};
        record->occupied = true;
        const u32 recordIndex = static_cast<u32>(record - m_impl->windows);
        record->snapshot.handle = {recordIndex, record->generation};
        record->snapshot.lifecycle = WindowLifecycleState::Creating;
        BackendStatus status{};
        {
            const BackendCallGuard backendCall(m_impl->backendCallActive);
            status = m_impl->backend->Create(backendDescriptor, backendWindow, backendState);
        }
        if (!status || !backendWindow.IsValid())
        {
            record->occupied = false;
            record->snapshot = {};
            m_impl->Reject(failure, FailureCode::BackendFailure, status.message != nullptr ? status.message : "window backend creation failed", {},
                           display->snapshot.handle, status.code);
            m_impl->lock.Release();
            return false;
        }
        record->backend = backendWindow;
        record->snapshot.parent = descriptor.parent;
        record->snapshot.role = descriptor.role;
        record->snapshot.relationship = descriptor.relationship;
        record->snapshot.surfaceKind = descriptor.surfaceKind;
        record->snapshot.flags = descriptor.flags;
        record->snapshot.constraints = descriptor.constraints;
        record->snapshot.requested = descriptor.placement;
        record->snapshot.requested.display = display->snapshot.handle;
        if (!m_impl->MapBackendState(backendState, record->snapshot.nativeState, failure, record->snapshot.handle))
        {
            {
                const BackendCallGuard backendCall(m_impl->backendCallActive);
                static_cast<void>(m_impl->backend->DestroyWindow(backendWindow));
            }
            record->occupied = false;
            record->backend = {};
            record->snapshot = {};
            m_impl->lock.Release();
            return false;
        }
        if (descriptor.initialPlacement != InitialWindowPlacement::Explicit)
            record->snapshot.requested.position = record->snapshot.nativeState.placement.position;
        record->snapshot.lifecycle = WindowLifecycleState::Alive;
        record->snapshot.stateRevision = 1;
        record->snapshot.pixelExtentRevision = 1;
        record->snapshot.surfaceRevision = 1;
        static_cast<void>(CopyString(title, record->snapshot.title, MaximumWindowTitleBytes));
        ++m_impl->stats.activeWindows;
        window = record->snapshot.handle;
        m_impl->PublishWindowEvent(WindowEventType::Created, *record);
        m_impl->lock.Release();
        return true;
    }

    bool WindowManager::RequestState(const WindowHandle window, const WindowStateRequest& request, Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, window, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (!m_impl->IsOwnerThread())
        {
            m_impl->Reject(failure, FailureCode::WrongThread, "window state mutation requires the owner thread", window);
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not re-enter state mutation", window);
            return false;
        }
        constexpr u32 validFields = static_cast<u32>(WindowStateField::Position) | static_cast<u32>(WindowStateField::LogicalExtent) |
                                    static_cast<u32>(WindowStateField::Display) | static_cast<u32>(WindowStateField::Mode) |
                                    static_cast<u32>(WindowStateField::Visibility);
        if (request.fields == WindowStateField::None || (static_cast<u32>(request.fields) & ~validFields) != 0)
        {
            m_impl->Reject(failure, FailureCode::InvalidDescriptor, "window state request fields are invalid", window);
            return false;
        }

        m_impl->lock.Acquire();
        Impl::WindowRecord* const record = m_impl->FindWindow(window);
        if (record == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "window handle is stale or invalid", window);
            m_impl->lock.Release();
            return false;
        }
        if (record->snapshot.lifecycle != WindowLifecycleState::Alive)
        {
            m_impl->Reject(failure, FailureCode::InvalidState, "window does not accept state changes", window);
            m_impl->lock.Release();
            return false;
        }
        WindowPlacement requested = record->snapshot.requested;
        if (HasField(request.fields, WindowStateField::Position))
            requested.position = request.placement.position;
        if (HasField(request.fields, WindowStateField::LogicalExtent))
            requested.logicalExtent = request.placement.logicalExtent;
        if (HasField(request.fields, WindowStateField::Display))
            requested.display = request.placement.display;
        if (HasField(request.fields, WindowStateField::Mode))
            requested.mode = request.placement.mode;
        if (HasField(request.fields, WindowStateField::Visibility))
            requested.visible = request.placement.visible;
        if (!IsWithinConstraints(requested.logicalExtent, record->snapshot.constraints) || !IsValidMode(requested.mode))
        {
            m_impl->Reject(failure, FailureCode::InvalidDescriptor, "requested window state is invalid", window);
            m_impl->lock.Release();
            return false;
        }
        Impl::DisplayRecord* const display = m_impl->FindDisplay(requested.display);
        if (display == nullptr)
        {
            m_impl->Reject(failure, FailureCode::DisplayUnavailable, "requested display is stale or unavailable", window, requested.display);
            m_impl->lock.Release();
            return false;
        }
        BackendWindowRequest backendRequest{};
        backendRequest.fields = request.fields;
        backendRequest.placement.position = requested.position;
        backendRequest.placement.logicalExtent = requested.logicalExtent;
        backendRequest.placement.display = display->backend;
        backendRequest.placement.mode = requested.mode;
        backendRequest.placement.visible = requested.visible;
        BackendWindowState backendState{};
        BackendStatus status{};
        {
            const BackendCallGuard backendCall(m_impl->backendCallActive);
            status = m_impl->backend->ApplyWindowState(record->backend, backendRequest, backendState);
        }
        if (!status)
        {
            m_impl->Reject(failure, FailureCode::BackendFailure, status.message != nullptr ? status.message : "window backend rejected the state request",
                           window, requested.display, status.code);
            m_impl->lock.Release();
            return false;
        }
        WindowNativeState mapped{};
        if (!m_impl->MapBackendState(backendState, mapped, failure, window))
        {
            m_impl->lock.Release();
            return false;
        }
        const WindowNativeState previous = record->snapshot.nativeState;
        record->snapshot.requested = requested;
        record->snapshot.nativeState = mapped;
        ++record->snapshot.stateRevision;
        m_impl->PublishStateDifferences(*record, previous, 0);
        m_impl->lock.Release();
        return true;
    }

    bool WindowManager::SetTitle(const WindowHandle window, const char* const title, Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, window, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (!m_impl->IsOwnerThread())
        {
            m_impl->Reject(failure, FailureCode::WrongThread, "window title mutation requires the owner thread", window);
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not re-enter title mutation", window);
            return false;
        }
        char copied[MaximumWindowTitleBytes]{};
        if (!CopyString(title, copied, MaximumWindowTitleBytes))
        {
            m_impl->Reject(failure, FailureCode::InvalidDescriptor, "window title is empty or too long", window);
            return false;
        }
        m_impl->lock.Acquire();
        Impl::WindowRecord* const record = m_impl->FindWindow(window);
        if (record == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "window handle is stale or invalid", window);
            m_impl->lock.Release();
            return false;
        }
        if (record->snapshot.lifecycle != WindowLifecycleState::Alive || StringsEqual(record->snapshot.title, copied))
        {
            const bool unchanged = StringsEqual(record->snapshot.title, copied);
            if (!unchanged)
                m_impl->Reject(failure, FailureCode::InvalidState, "window does not accept title changes", window);
            m_impl->lock.Release();
            return unchanged;
        }
        BackendStatus status{};
        {
            const BackendCallGuard backendCall(m_impl->backendCallActive);
            status = m_impl->backend->SetWindowTitle(record->backend, copied);
        }
        if (!status)
        {
            m_impl->Reject(failure, FailureCode::BackendFailure, status.message != nullptr ? status.message : "window backend rejected the title", window, {},
                           status.code);
            m_impl->lock.Release();
            return false;
        }
        static_cast<void>(CopyString(copied, record->snapshot.title, MaximumWindowTitleBytes));
        ++record->snapshot.stateRevision;
        m_impl->lock.Release();
        return true;
    }

    bool WindowManager::ResolveCloseRequest(const WindowHandle window, const u64 closeRequestSerial, const CloseDecision decision,
                                            Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, window, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (!m_impl->IsOwnerThread())
        {
            m_impl->Reject(failure, FailureCode::WrongThread, "close resolution requires the owner thread", window);
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not re-enter close resolution", window);
            return false;
        }
        m_impl->lock.Acquire();
        Impl::WindowRecord* const record = m_impl->FindWindow(window);
        if (record == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "window handle is stale or invalid", window);
            m_impl->lock.Release();
            return false;
        }
        if (record->snapshot.lifecycle != WindowLifecycleState::CloseRequested)
        {
            m_impl->Reject(failure, FailureCode::InvalidState, "window has no unresolved close request", window);
            m_impl->lock.Release();
            return false;
        }
        if (closeRequestSerial == 0 || closeRequestSerial != record->snapshot.closeRequestSerial)
        {
            m_impl->Reject(failure, FailureCode::InvalidCloseSerial, "close request serial is stale", window);
            m_impl->lock.Release();
            return false;
        }
        if (decision == CloseDecision::Defer)
        {
            m_impl->lock.Release();
            return true;
        }
        if (decision == CloseDecision::Reject)
        {
            record->snapshot.lifecycle = WindowLifecycleState::Alive;
            ++record->snapshot.stateRevision;
            m_impl->PublishWindowEvent(WindowEventType::CloseRejected, *record);
            m_impl->lock.Release();
            return true;
        }
        if (decision != CloseDecision::Accept)
        {
            m_impl->Reject(failure, FailureCode::InvalidDescriptor, "close decision is invalid", window);
            m_impl->lock.Release();
            return false;
        }
        const bool result = m_impl->DestroyRecord(*record, failure, true);
        m_impl->lock.Release();
        return result;
    }

    bool WindowManager::DestroyWindow(const WindowHandle window, Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, window, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (!m_impl->IsOwnerThread())
        {
            m_impl->Reject(failure, FailureCode::WrongThread, "window destruction requires the owner thread", window);
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not re-enter window destruction", window);
            return false;
        }
        m_impl->lock.Acquire();
        Impl::WindowRecord* const record = m_impl->FindWindow(window);
        if (record == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "window handle is stale or invalid", window);
            m_impl->lock.Release();
            return false;
        }
        const bool result = m_impl->DestroyRecord(*record, failure, false);
        m_impl->lock.Release();
        return result;
    }

    bool WindowManager::AttachPresentation(const WindowHandle window, PresentationAttachmentHandle& attachment, Failure* const failure) noexcept
    {
        attachment = {};
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, window, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not attach presentation resources", window);
            return false;
        }
        m_impl->lock.Acquire();
        Impl::WindowRecord* const windowRecord = m_impl->FindWindow(window);
        if (windowRecord == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "presentation attachment references a stale or invalid window", window);
            m_impl->lock.Release();
            return false;
        }
        if (windowRecord->snapshot.lifecycle != WindowLifecycleState::Alive)
        {
            m_impl->Reject(failure, FailureCode::InvalidState, "presentation resources can only attach to a live window", window);
            m_impl->lock.Release();
            return false;
        }
        if (windowRecord->snapshot.surfaceKind == PresentationSurfaceKind::None)
        {
            m_impl->Reject(failure, FailureCode::PresentationUnavailable, "window was created without a presentation-capable surface", window);
            m_impl->lock.Release();
            return false;
        }
        if (windowRecord->snapshot.presentation.IsValid())
        {
            m_impl->Reject(failure, FailureCode::PresentationAlreadyAttached, "window already owns a presentation attachment", window);
            m_impl->lock.Release();
            return false;
        }
        Impl::PresentationRecord* presentation = nullptr;
        for (Impl::PresentationRecord& candidate : m_impl->presentations)
        {
            if (candidate.occupied)
                continue;
            presentation = &candidate;
            break;
        }
        if (presentation == nullptr)
        {
            m_impl->Reject(failure, FailureCode::CapacityExceeded, "presentation attachment registry capacity is exhausted", window);
            m_impl->lock.Release();
            return false;
        }
        const u32 index = static_cast<u32>(presentation - m_impl->presentations);
        presentation->occupied = true;
        presentation->window = window;
        presentation->acknowledgedPixelExtentRevision = 0;
        presentation->acknowledgedSurfaceRevision = 0;
        attachment = {index, presentation->generation};
        windowRecord->snapshot.presentation = attachment;
        ++windowRecord->snapshot.stateRevision;
        ++m_impl->stats.activePresentationAttachments;
        m_impl->lock.Release();
        return true;
    }

    bool WindowManager::DetachPresentation(const PresentationAttachmentHandle attachment, Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, {}, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not detach presentation resources");
            return false;
        }
        m_impl->lock.Acquire();
        Impl::PresentationRecord* const presentation = m_impl->FindPresentation(attachment);
        if (presentation == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "presentation attachment handle is stale or invalid");
            m_impl->lock.Release();
            return false;
        }
        Impl::WindowRecord* const windowRecord = m_impl->FindWindow(presentation->window);
        if (windowRecord == nullptr || !(windowRecord->snapshot.presentation == attachment))
        {
            m_impl->Reject(failure, FailureCode::InvalidState, "presentation attachment registry is inconsistent");
            m_impl->lock.Release();
            return false;
        }
        windowRecord->snapshot.presentation = {};
        ++windowRecord->snapshot.stateRevision;
        presentation->occupied = false;
        presentation->window = {};
        presentation->acknowledgedPixelExtentRevision = 0;
        presentation->acknowledgedSurfaceRevision = 0;
        ++presentation->generation;
        if (presentation->generation == 0)
            presentation->generation = 1;
        --m_impl->stats.activePresentationAttachments;
        m_impl->lock.Release();
        return true;
    }

    bool WindowManager::AcknowledgePresentation(const PresentationAttachmentHandle attachment, const PresentationAcknowledgement& acknowledgement,
                                                Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, {}, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not acknowledge presentation work");
            return false;
        }
        m_impl->lock.Acquire();
        Impl::PresentationRecord* const presentation = m_impl->FindPresentation(attachment);
        if (presentation == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "presentation attachment handle is stale or invalid");
            m_impl->lock.Release();
            return false;
        }
        const Impl::WindowRecord* const windowRecord = m_impl->FindWindow(presentation->window);
        const bool pixelRevisionInvalid = acknowledgement.pixelExtentRevision != 0 &&
                                          (acknowledgement.pixelExtentRevision < presentation->acknowledgedPixelExtentRevision || windowRecord == nullptr ||
                                           acknowledgement.pixelExtentRevision > windowRecord->snapshot.pixelExtentRevision);
        const bool surfaceRevisionInvalid =
            acknowledgement.surfaceRevision != 0 && (acknowledgement.surfaceRevision < presentation->acknowledgedSurfaceRevision || windowRecord == nullptr ||
                                                     acknowledgement.surfaceRevision > windowRecord->snapshot.surfaceRevision);
        if ((acknowledgement.pixelExtentRevision == 0 && acknowledgement.surfaceRevision == 0) || pixelRevisionInvalid || surfaceRevisionInvalid)
        {
            m_impl->Reject(failure, FailureCode::InvalidPresentationRevision,
                           "presentation acknowledgement is zero, regressive, or newer than its window state", presentation->window);
            m_impl->lock.Release();
            return false;
        }
        if (acknowledgement.pixelExtentRevision != 0)
            presentation->acknowledgedPixelExtentRevision = acknowledgement.pixelExtentRevision;
        if (acknowledgement.surfaceRevision != 0)
            presentation->acknowledgedSurfaceRevision = acknowledgement.surfaceRevision;
        m_impl->lock.Release();
        return true;
    }

    bool WindowManager::ResolvePresentationSurface(const PresentationAttachmentHandle attachment, NativePresentationSurface& surface,
                                                   Failure* const failure) noexcept
    {
        surface = {};
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, {}, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (!m_impl->IsOwnerThread())
        {
            m_impl->Reject(failure, FailureCode::WrongThread, "presentation-surface resolution requires the owner thread");
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backend commands must not re-enter presentation-surface resolution");
            return false;
        }

        m_impl->lock.Acquire();
        const Impl::PresentationRecord* const presentation = m_impl->FindPresentation(attachment);
        const Impl::WindowRecord* const windowRecord = presentation != nullptr ? m_impl->FindWindow(presentation->window) : nullptr;
        if (presentation == nullptr || windowRecord == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "presentation attachment handle is stale or invalid");
            m_impl->lock.Release();
            return false;
        }
        BackendStatus status;
        {
            const BackendCallGuard backendCall(m_impl->backendCallActive);
            status = m_impl->backend->ResolvePresentationSurface(windowRecord->backend, surface);
        }
        if (!status || !surface.IsValid())
        {
            surface = {};
            m_impl->Reject(failure, FailureCode::PresentationUnavailable,
                           status.message != nullptr ? status.message : "native presentation surface is unavailable", presentation->window, {}, status.code);
            m_impl->lock.Release();
            return false;
        }
        m_impl->lock.Release();
        return true;
    }

    bool WindowManager::ProcessBackendEvent(const BackendWindowEvent& event, Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                *failure = {FailureCode::NotInitialized, {}, {}, 0, "WindowManager is not initialized"};
            return false;
        }
        if (!m_impl->IsOwnerThread())
        {
            m_impl->Reject(failure, FailureCode::WrongThread, "backend events require the owner thread");
            return false;
        }
        if (m_impl->IsBackendReentry())
        {
            m_impl->Reject(failure, FailureCode::BackendReentry, "window backends must queue events until the active command returns");
            return false;
        }
        if (!IsValidBackendEventType(event.type))
        {
            m_impl->Reject(failure, FailureCode::BackendFailure, "window backend emitted an invalid event type", {}, {}, event.backendCode);
            return false;
        }
        m_impl->lock.Acquire();
        Impl::WindowRecord* const record = m_impl->FindWindow(event.window);
        if (record == nullptr)
        {
            m_impl->Reject(failure, FailureCode::InvalidHandle, "backend event references an unknown native window", {}, {}, event.backendCode);
            m_impl->lock.Release();
            return false;
        }
        if (event.type == BackendEventType::Failure)
        {
            record->snapshot.lifecycle = WindowLifecycleState::Failed;
            ++record->snapshot.stateRevision;
            m_impl->PublishWindowEvent(WindowEventType::BackendFailure, *record, event.timestampNanoseconds, event.backendCode);
            m_impl->lock.Release();
            return true;
        }
        if (event.type == BackendEventType::CloseRequested)
        {
            if (record->snapshot.lifecycle == WindowLifecycleState::Alive)
            {
                record->snapshot.lifecycle = WindowLifecycleState::CloseRequested;
                ++record->snapshot.closeRequestSerial;
                if (record->snapshot.closeRequestSerial == 0)
                    ++record->snapshot.closeRequestSerial;
                ++record->snapshot.stateRevision;
                m_impl->PublishWindowEvent(WindowEventType::CloseRequested, *record, event.timestampNanoseconds);
            }
            m_impl->lock.Release();
            return true;
        }
        if (record->snapshot.lifecycle != WindowLifecycleState::Alive && record->snapshot.lifecycle != WindowLifecycleState::CloseRequested)
        {
            m_impl->Reject(failure, FailureCode::InvalidState, "backend event reached a retiring window", record->snapshot.handle);
            m_impl->lock.Release();
            return false;
        }
        WindowNativeState mapped{};
        if (!m_impl->MapBackendState(event.state, mapped, failure, record->snapshot.handle))
        {
            m_impl->lock.Release();
            return false;
        }
        const WindowNativeState previous = record->snapshot.nativeState;
        record->snapshot.nativeState = mapped;
        record->snapshot.requested.position = mapped.placement.position;
        record->snapshot.requested.logicalExtent = mapped.placement.logicalExtent;
        record->snapshot.requested.display = mapped.placement.display;
        record->snapshot.requested.mode = mapped.placement.mode;
        record->snapshot.requested.visible = mapped.placement.visible;
        ++record->snapshot.stateRevision;
        m_impl->PublishStateDifferences(*record, previous, event.timestampNanoseconds);
        if (event.type == BackendEventType::Exposed)
            m_impl->PublishWindowEvent(WindowEventType::Exposed, *record, event.timestampNanoseconds);
        m_impl->lock.Release();
        return true;
    }

    bool WindowManager::GetSnapshot(const WindowHandle window, WindowSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr || m_impl->IsBackendReentry())
            return false;
        m_impl->lock.AcquireShared();
        const Impl::WindowRecord* const record = m_impl->FindWindow(window);
        if (record != nullptr)
            snapshot = record->snapshot;
        m_impl->lock.ReleaseShared();
        return record != nullptr;
    }

    bool WindowManager::GetSnapshot(const DisplayHandle display, DisplaySnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr || m_impl->IsBackendReentry())
            return false;
        m_impl->lock.AcquireShared();
        const Impl::DisplayRecord* const record = m_impl->FindDisplay(display);
        if (record != nullptr)
            snapshot = record->snapshot;
        m_impl->lock.ReleaseShared();
        return record != nullptr;
    }

    bool WindowManager::GetSnapshot(const PresentationAttachmentHandle attachment, PresentationAttachmentSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr || m_impl->IsBackendReentry())
            return false;
        m_impl->lock.AcquireShared();
        const Impl::PresentationRecord* const record = m_impl->FindPresentation(attachment);
        const bool result = record != nullptr && m_impl->BuildPresentationSnapshot(*record, snapshot);
        m_impl->lock.ReleaseShared();
        return result;
    }

    WindowHandle WindowManager::ResolveBackendWindow(const BackendWindowId window) const noexcept
    {
        if (m_impl == nullptr || m_impl->IsBackendReentry() || !window.IsValid())
            return {};
        m_impl->lock.AcquireShared();
        WindowHandle handle;
        for (const Impl::WindowRecord& record : m_impl->windows)
        {
            if (!record.occupied || record.backend != window)
                continue;
            handle = record.snapshot.handle;
            break;
        }
        m_impl->lock.ReleaseShared();
        return handle;
    }

    DisplayHandle WindowManager::GetPrimaryDisplay() const noexcept
    {
        if (m_impl == nullptr || m_impl->IsBackendReentry())
            return {};
        m_impl->lock.AcquireShared();
        DisplayHandle display;
        for (const Impl::DisplayRecord& record : m_impl->displays)
        {
            if (record.occupied && record.snapshot.primary)
            {
                display = record.snapshot.handle;
                break;
            }
        }
        if (!display.IsValid())
            for (const Impl::DisplayRecord& record : m_impl->displays)
                if (record.occupied)
                {
                    display = record.snapshot.handle;
                    break;
                }
        m_impl->lock.ReleaseShared();
        return display;
    }

    u32 WindowManager::VisitWindows(WindowSnapshot* const snapshots, const u32 capacity) const noexcept
    {
        if (m_impl == nullptr || m_impl->IsBackendReentry() || snapshots == nullptr || capacity == 0)
            return 0;
        m_impl->lock.AcquireShared();
        u32 count = 0;
        for (const Impl::WindowRecord& record : m_impl->windows)
        {
            if (count == capacity)
                break;
            if (!record.occupied)
                continue;
            snapshots[count++] = record.snapshot;
        }
        m_impl->lock.ReleaseShared();
        return count;
    }

    u32 WindowManager::VisitDisplays(DisplaySnapshot* const snapshots, const u32 capacity) const noexcept
    {
        if (m_impl == nullptr || m_impl->IsBackendReentry() || snapshots == nullptr || capacity == 0)
            return 0;
        m_impl->lock.AcquireShared();
        u32 count = 0;
        for (const Impl::DisplayRecord& record : m_impl->displays)
        {
            if (count == capacity)
                break;
            if (!record.occupied)
                continue;
            snapshots[count++] = record.snapshot;
        }
        m_impl->lock.ReleaseShared();
        return count;
    }

    u32 WindowManager::VisitPresentationAttachments(PresentationAttachmentSnapshot* const snapshots, const u32 capacity) const noexcept
    {
        if (m_impl == nullptr || m_impl->IsBackendReentry() || snapshots == nullptr || capacity == 0)
            return 0;
        m_impl->lock.AcquireShared();
        u32 count = 0;
        for (const Impl::PresentationRecord& record : m_impl->presentations)
        {
            if (count == capacity)
                break;
            if (!record.occupied)
                continue;
            PresentationAttachmentSnapshot snapshot{};
            if (m_impl->BuildPresentationSnapshot(record, snapshot))
                snapshots[count++] = snapshot;
        }
        m_impl->lock.ReleaseShared();
        return count;
    }

    bool WindowManager::CreateEventCursor(const EventCursorOrigin origin, WindowEventCursor& cursor) const noexcept
    {
        cursor = {};
        if (m_impl == nullptr || m_impl->IsBackendReentry())
            return false;
        switch (origin)
        {
        case EventCursorOrigin::OldestAvailable:
        case EventCursorOrigin::NextEvent:
            break;
        default:
            return false;
        }
        m_impl->lock.AcquireShared();
        cursor.journalIdentity = m_impl->journalIdentity;
        cursor.nextSequence = origin == EventCursorOrigin::OldestAvailable ? m_impl->oldestEventSequence : m_impl->nextEventSequence;
        m_impl->lock.ReleaseShared();
        return true;
    }

    WindowEventReadResult WindowManager::ReadEvents(WindowEventCursor& cursor, const containers::ArraySpan<WindowEvent> events) const noexcept
    {
        WindowEventReadResult result{};
        if (m_impl == nullptr || m_impl->IsBackendReentry() || cursor.journalIdentity == 0 || cursor.nextSequence == 0 || events.Data() == nullptr ||
            events.Size() == 0)
            return result;
        m_impl->lock.AcquireShared();
        if (cursor.journalIdentity != m_impl->journalIdentity || cursor.nextSequence > m_impl->nextEventSequence)
        {
            result.invalidCursor = true;
            cursor.journalIdentity = m_impl->journalIdentity;
            cursor.nextSequence = m_impl->nextEventSequence;
            m_impl->lock.ReleaseShared();
            return result;
        }
        if (cursor.nextSequence < m_impl->oldestEventSequence)
        {
            result.lostEvents = m_impl->oldestEventSequence - cursor.nextSequence;
            cursor.nextSequence = m_impl->oldestEventSequence;
        }
        const u64 available = m_impl->nextEventSequence - cursor.nextSequence;
        const u32 count = available < events.Size() ? static_cast<u32>(available) : events.Size();
        for (u32 index = 0; index < count; ++index)
        {
            const u64 sequence = cursor.nextSequence + index;
            events[index] = m_impl->events[(sequence - 1u) % WindowEventJournalCapacity];
        }
        cursor.nextSequence += count;
        result.count = count;
        result.newestSequence = count != 0 ? cursor.nextSequence - 1u : 0;
        m_impl->lock.ReleaseShared();
        return result;
    }

    ManagerStats WindowManager::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        if (m_impl->IsBackendReentry())
        {
            ManagerStats stats{};
            stats.rejectedOperations = m_impl->rejectedOperations.GetValue();
            return stats;
        }
        m_impl->lock.AcquireShared();
        ManagerStats stats = m_impl->stats;
        stats.rejectedOperations = m_impl->rejectedOperations.GetValue();
        m_impl->lock.ReleaseShared();
        return stats;
    }
} // namespace vanguard::window

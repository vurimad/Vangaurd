#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::window
{
    inline constexpr u32 MaximumWindows = 64;
    inline constexpr u32 MaximumDisplays = 32;
    inline constexpr u32 MaximumPresentationAttachments = MaximumWindows;
    inline constexpr u32 MaximumWindowTitleBytes = 256;
    inline constexpr u32 WindowEventJournalCapacity = 2048;

    struct WindowHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumWindows && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const WindowHandle&, const WindowHandle&) noexcept = default;
    };

    struct DisplayHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumDisplays && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const DisplayHandle&, const DisplayHandle&) noexcept = default;
    };

    struct PresentationAttachmentHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumPresentationAttachments && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const PresentationAttachmentHandle&, const PresentationAttachmentHandle&) noexcept = default;
    };

    inline constexpr WindowHandle InvalidWindowHandle{};
    inline constexpr DisplayHandle InvalidDisplayHandle{};
    inline constexpr PresentationAttachmentHandle InvalidPresentationAttachmentHandle{};

    struct WindowPoint
    {
        i32 x = 0;
        i32 y = 0;

        [[nodiscard]] friend constexpr bool operator==(const WindowPoint&, const WindowPoint&) noexcept = default;
    };

    enum class CursorShape : u8
    {
        Arrow,
        TextInput,
        Move,
        ResizeNorthSouth,
        ResizeEastWest,
        ResizeNorthEastSouthWest,
        ResizeNorthWestSouthEast,
        Pointer,
        Wait,
        Progress,
        NotAllowed,
        Count
    };

    struct TextInputRequest
    {
        WindowPoint position;
        u32 lineHeight = 0;
        bool enabled = false;
        bool showIme = false;
    };

    struct WindowExtent
    {
        u32 width = 0;
        u32 height = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return width != 0 && height != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const WindowExtent&, const WindowExtent&) noexcept = default;
    };

    struct WindowRect
    {
        WindowPoint origin;
        WindowExtent extent;

        [[nodiscard]] friend constexpr bool operator==(const WindowRect&, const WindowRect&) noexcept = default;
    };

    struct RefreshRate
    {
        u32 numerator = 0;
        u32 denominator = 1;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return numerator != 0 && denominator != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const RefreshRate&, const RefreshRate&) noexcept = default;
    };

    enum class WindowRole : u8
    {
        Primary,
        EditorMain,
        EditorViewport,
        GamePreview,
        Tool,
        Utility,
        Embedded
    };

    enum class WindowRelationship : u8
    {
        Independent,
        Owned,
        Modal,
        Embedded,
        EditorPlatformViewport
    };

    enum class WindowMode : u8
    {
        Windowed,
        BorderlessFullscreen,
        ExclusiveFullscreen
    };

    // Controls how the native backend interprets the initial window position. The policy is
    // consumed during creation; all subsequent position requests use explicit desktop coordinates.
    enum class InitialWindowPlacement : u8
    {
        Explicit,
        CenteredOnDisplay,
        PlatformDefault
    };

    enum class PresentationSurfaceKind : u8
    {
        None,           // Headless or non-rendered utility window
        PlatformNative, // Platform-specific surface (e.g., Win32, X11, Wayland, macOS)
        Vulkan          // SDL window created with SDL_WINDOW_VULKAN
    };

    enum class PresentationRequirement : u32
    {
        None = 0,
        SurfaceReconfigure = 1u << 0u,
        PixelExtentResize = 1u << 1u,
        Suspended = 1u << 2u,
        Occluded = 1u << 3u
    };

    [[nodiscard]] constexpr PresentationRequirement operator|(const PresentationRequirement left, const PresentationRequirement right) noexcept
    {
        return static_cast<PresentationRequirement>(static_cast<u32>(left) | static_cast<u32>(right));
    }
    [[nodiscard]] constexpr bool HasRequirement(const PresentationRequirement requirements, const PresentationRequirement requirement) noexcept
    {
        return (static_cast<u32>(requirements) & static_cast<u32>(requirement)) != 0;
    }

    enum class WindowLifecycleState : u8
    {
        Vacant,
        Creating,
        Alive,
        CloseRequested,
        Retiring,
        DestroyPending,
        Failed
    };

    enum class WindowFlag : u32
    {
        None = 0,
        Resizable = 1u << 0u,
        Borderless = 1u << 1u,
        AlwaysOnTop = 1u << 2u,
        Utility = 1u << 3u,
        SkipTaskbar = 1u << 4u,
        Transparent = 1u << 5u,
        AcceptFileDrop = 1u << 6u,
        HighPixelDensity = 1u << 7u
    };

    [[nodiscard]] constexpr WindowFlag operator|(const WindowFlag left, const WindowFlag right) noexcept
    {
        return static_cast<WindowFlag>(static_cast<u32>(left) | static_cast<u32>(right));
    }
    [[nodiscard]] constexpr bool HasFlag(const WindowFlag flags, const WindowFlag flag) noexcept
    {
        return (static_cast<u32>(flags) & static_cast<u32>(flag)) != 0;
    }

    struct WindowConstraints
    {
        WindowExtent minimum{1, 1};
        WindowExtent maximum{16384, 16384};
    };

    struct WindowPlacement
    {
        WindowPoint position;
        WindowExtent logicalExtent{1280, 720};
        DisplayHandle display;
        WindowMode mode = WindowMode::Windowed;
        bool visible = true;
    };

    struct WindowNativeState
    {
        WindowPlacement placement;
        WindowExtent pixelExtent{1280, 720};
        WindowRect safeArea{{0, 0}, {1280, 720}};
        f32 contentScale = 1.0f;
        f32 sdrWhiteLevel = 1.0f;
        f32 hdrHeadroom = 1.0f;
        bool focused = false;
        bool mouseFocus = false;
        bool minimized = false;
        bool maximized = false;
        bool occluded = false;
        bool hdrCapable = false;
    };

    struct WindowDescriptor
    {
        const char* title = nullptr;
        WindowRole role = WindowRole::Primary;
        WindowRelationship relationship = WindowRelationship::Independent;
        WindowHandle parent;
        WindowPlacement placement;
        InitialWindowPlacement initialPlacement = InitialWindowPlacement::Explicit;
        WindowConstraints constraints;
        PresentationSurfaceKind surfaceKind = PresentationSurfaceKind::PlatformNative;
        WindowFlag flags = WindowFlag::Resizable | WindowFlag::HighPixelDensity;
    };

    enum class WindowStateField : u32
    {
        None = 0,
        Position = 1u << 0u,
        LogicalExtent = 1u << 1u,
        Display = 1u << 2u,
        Mode = 1u << 3u,
        Visibility = 1u << 4u
    };

    [[nodiscard]] constexpr WindowStateField operator|(const WindowStateField left, const WindowStateField right) noexcept
    {
        return static_cast<WindowStateField>(static_cast<u32>(left) | static_cast<u32>(right));
    }
    [[nodiscard]] constexpr bool HasField(const WindowStateField fields, const WindowStateField field) noexcept
    {
        return (static_cast<u32>(fields) & static_cast<u32>(field)) != 0;
    }

    struct WindowStateRequest
    {
        WindowStateField fields = WindowStateField::None;
        WindowPlacement placement;
        // Only applies when showing a window; false preserves the current keyboard focus and mouse capture.
        bool activateWhenShown = true;
    };

    struct WindowSnapshot
    {
        WindowHandle handle;
        PresentationAttachmentHandle presentation;
        WindowHandle parent;
        WindowRole role = WindowRole::Primary;
        WindowRelationship relationship = WindowRelationship::Independent;
        WindowLifecycleState lifecycle = WindowLifecycleState::Vacant;
        PresentationSurfaceKind surfaceKind = PresentationSurfaceKind::None;
        WindowFlag flags = WindowFlag::None;
        WindowConstraints constraints;
        WindowPlacement requested;
        WindowNativeState nativeState;
        u64 stateRevision = 0;
        u64 pixelExtentRevision = 0;
        u64 surfaceRevision = 0;
        u64 closeRequestSerial = 0;
        char title[MaximumWindowTitleBytes]{};
    };

    struct PresentationAttachmentSnapshot
    {
        PresentationAttachmentHandle handle;
        WindowHandle window;
        PresentationSurfaceKind surfaceKind = PresentationSurfaceKind::None;
        PresentationRequirement requirements = PresentationRequirement::None;
        WindowExtent pixelExtent;
        DisplayHandle display;
        WindowMode mode = WindowMode::Windowed;
        u64 windowStateRevision = 0;
        u64 requiredPixelExtentRevision = 0;
        u64 requiredSurfaceRevision = 0;
        u64 acknowledgedPixelExtentRevision = 0;
        u64 acknowledgedSurfaceRevision = 0;
        f32 sdrWhiteLevel = 1.0f;
        f32 hdrHeadroom = 1.0f;
        bool visible = false;
        bool minimized = false;
        bool occluded = false;
        bool hdrCapable = false;
    };

    struct PresentationAcknowledgement
    {
        u64 pixelExtentRevision = 0;
        u64 surfaceRevision = 0;
    };

    struct DisplaySnapshot
    {
        DisplayHandle handle;
        u64 fingerprint = 0;
        WindowRect bounds;
        WindowRect workArea;
        WindowExtent desktopPixelExtent;
        RefreshRate desktopRefreshRate;
        f32 contentScale = 1.0f;
        u64 topologyRevision = 0;
        bool primary = false;
        bool hdrCapable = false;
    };

    enum class CloseDecision : u8
    {
        Defer,
        Reject,
        Accept
    };

    enum class EventCursorOrigin : u8
    {
        OldestAvailable,
        NextEvent
    };

    struct WindowEventCursor
    {
        u64 journalIdentity = 0;
        u64 nextSequence = 0;
    };

    enum class WindowEventType : u8
    {
        Created,
        Destroyed,
        CloseRequested,
        CloseAccepted,
        CloseRejected,
        Shown,
        Hidden,
        Exposed,
        Moved,
        Resized,
        PixelExtentChanged,
        Minimized,
        Maximized,
        Restored,
        FocusGained,
        FocusLost,
        MouseEntered,
        MouseLeft,
        DisplayChanged,
        ModeChanged,
        ContentScaleChanged,
        SafeAreaChanged,
        OcclusionChanged,
        HdrStateChanged,
        DisplayAdded,
        DisplayRemoved,
        DisplayUpdated,
        DisplayInvalidated,
        BackendFailure
    };

    struct WindowEvent
    {
        WindowEventType type = WindowEventType::Created;
        WindowHandle window;
        DisplayHandle display;
        u64 sequence = 0;
        u64 timestampNanoseconds = 0;
        u64 closeRequestSerial = 0;
        u64 stateRevision = 0;
        WindowPoint position;
        WindowExtent logicalExtent;
        WindowExtent pixelExtent;
        f32 contentScale = 1.0f;
        i32 backendCode = 0;
    };

    struct WindowEventReadResult
    {
        u32 count = 0;
        u64 lostEvents = 0;
        u64 newestSequence = 0;
        bool invalidCursor = false;
    };

    enum class FailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDescriptor,
        InvalidHandle,
        InvalidState,
        InvalidCloseSerial,
        CapacityExceeded,
        ParentHasChildren,
        DisplayUnavailable,
        BackendFailure,
        BackendReentry,
        PresentationUnavailable,
        PresentationAlreadyAttached,
        PresentationStillAttached,
        InvalidPresentationRevision,
        EventsUnavailable,
        WindowsRemainAlive
    };

    struct Failure
    {
        FailureCode code = FailureCode::None;
        WindowHandle window;
        DisplayHandle display;
        i32 backendCode = 0;
        const char* message = nullptr;
    };

    struct ManagerStats
    {
        u32 activeWindows = 0;
        u32 activeDisplays = 0;
        u32 activePresentationAttachments = 0;
        u64 topologyRevision = 0;
        u64 publishedEvents = 0;
        u64 overwrittenEvents = 0;
        u64 rejectedOperations = 0;
    };
} // namespace vanguard::window

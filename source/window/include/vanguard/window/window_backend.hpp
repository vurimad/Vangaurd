#pragma once

#include <vanguard/window/window_types.hpp>

namespace vanguard::window
{
    struct BackendWindowId
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const BackendWindowId&, const BackendWindowId&) noexcept = default;
    };

    struct BackendDisplayId
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const BackendDisplayId&, const BackendDisplayId&) noexcept = default;
    };

    struct BackendDisplaySnapshot
    {
        BackendDisplayId id;
        u64 fingerprint = 0;
        WindowRect bounds;
        WindowRect workArea;
        WindowExtent desktopPixelExtent;
        RefreshRate desktopRefreshRate;
        f32 contentScale = 1.0f;
        bool primary = false;
        bool hdrCapable = false;
    };

    struct BackendWindowPlacement
    {
        WindowPoint position;
        WindowExtent logicalExtent;
        BackendDisplayId display;
        WindowMode mode = WindowMode::Windowed;
        bool visible = true;
    };

    struct BackendWindowState
    {
        BackendWindowPlacement placement;
        WindowExtent pixelExtent;
        WindowRect safeArea;
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

    struct BackendWindowDescriptor
    {
        const char* title = nullptr;
        BackendWindowId parent;
        WindowRole role = WindowRole::Primary;
        WindowRelationship relationship = WindowRelationship::Independent;
        BackendWindowPlacement placement;
        InitialWindowPlacement initialPlacement = InitialWindowPlacement::Explicit;
        WindowConstraints constraints;
        PresentationSurfaceKind surfaceKind = PresentationSurfaceKind::PlatformNative;
        WindowFlag flags = WindowFlag::None;
    };

    struct BackendWindowRequest
    {
        WindowStateField fields = WindowStateField::None;
        BackendWindowPlacement placement;
    };

    enum class BackendEventType : u8
    {
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
        CloseRequested,
        DisplayChanged,
        ContentScaleChanged,
        SafeAreaChanged,
        OcclusionChanged,
        HdrStateChanged,
        Failure
    };

    struct BackendWindowEvent
    {
        BackendEventType type = BackendEventType::Exposed;
        BackendWindowId window;
        BackendWindowState state;
        u64 timestampNanoseconds = 0;
        i32 backendCode = 0;
    };

    struct BackendStatus
    {
        bool success = true;
        i32 code = 0;
        const char* message = nullptr;

        [[nodiscard]] static constexpr BackendStatus Success() noexcept
        {
            return {};
        }
        [[nodiscard]] static constexpr BackendStatus Failure(const i32 code, const char* const message) noexcept
        {
            return {false, code, message};
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return success;
        }
    };

    enum class NativePresentationSurfaceKind : u8
    {
        None,
        Win32,
        Xlib,
        Xcb,
        Wayland,
        Cocoa
    };

    struct NativePresentationSurface
    {
        NativePresentationSurfaceKind kind = NativePresentationSurfaceKind::None;
        void* window = nullptr;
        void* display = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return kind != NativePresentationSurfaceKind::None && window != nullptr;
        }
    };

    class IWindowBackend
    {
    public:
        virtual ~IWindowBackend() = default;
        IWindowBackend(const IWindowBackend&) = delete;
        IWindowBackend& operator=(const IWindowBackend&) = delete;

        // Backend commands are synchronous and must not call WindowManager or deliver native events while they run.
        // Native events are queued by the platform layer and delivered later through IWindowEventSink, in pump order.
        // A successful DestroyWindow call means native destruction is complete; no later destruction event is emitted.
        [[nodiscard]] virtual BackendStatus EnumerateDisplays(BackendDisplaySnapshot* displays, u32 capacity, u32& count) noexcept = 0;
        [[nodiscard]] virtual BackendStatus Create(const BackendWindowDescriptor& descriptor, BackendWindowId& window, BackendWindowState& state) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ApplyWindowState(BackendWindowId window, const BackendWindowRequest& request,
                                                             BackendWindowState& state) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetWindowTitle(BackendWindowId window, const char* title) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ResolvePresentationSurface(BackendWindowId window, NativePresentationSurface& surface) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DestroyWindow(BackendWindowId window) noexcept = 0;

    protected:
        IWindowBackend() noexcept = default;
    };

    enum class WindowEventSinkAction : u8
    {
        Continue,
        RequestApplicationExit
    };

    struct WindowEventSinkResult
    {
        BackendStatus status;
        WindowEventSinkAction action = WindowEventSinkAction::Continue;

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return static_cast<bool>(status);
        }
    };

    // Platform event pumps use this narrow bridge to deliver native window changes without owning WindowManager
    // or depending on engine service composition.
    class IWindowEventSink
    {
    public:
        virtual ~IWindowEventSink() = default;
        IWindowEventSink(const IWindowEventSink&) = delete;
        IWindowEventSink& operator=(const IWindowEventSink&) = delete;

        [[nodiscard]] virtual BackendStatus RefreshDisplayTopology() noexcept = 0;
        [[nodiscard]] virtual WindowEventSinkResult ProcessWindowEvent(const BackendWindowEvent& event) noexcept = 0;
        [[nodiscard]] virtual WindowHandle ResolveWindow(BackendWindowId backendWindow) const noexcept = 0;

    protected:
        IWindowEventSink() noexcept = default;
    };
} // namespace vanguard::window

#include <vanguard/window/sdl/sdl_window_backend.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>

#include <SDL3/SDL.h>

#include <cstring>
#include <new>

namespace
{
    using namespace vanguard;
    namespace win = vanguard::window;

    [[nodiscard]] win::BackendStatus SdlFailure(const char* const fallback) noexcept
    {
        const char* const error = SDL_GetError();
        return win::BackendStatus::Failure(-1, error != nullptr && error[0] != '\0' ? error : fallback);
    }

    [[nodiscard]] u64 HashBytes(u64 hash, const void* const data, const u32 size) noexcept
    {
        constexpr u64 prime = 1099511628211ull;
        const auto* const bytes = static_cast<const u8*>(data);
        for (u32 index = 0; index < size; ++index)
            hash = (hash ^ bytes[index]) * prime;
        return hash;
    }

    [[nodiscard]] u64 DisplayFingerprint(const SDL_DisplayID display) noexcept
    {
        u64 hash = 14695981039346656037ull;
        hash = HashBytes(hash, &display, sizeof(display));
        const char* const name = SDL_GetDisplayName(display);
        if (name != nullptr)
            for (const char* cursor = name; *cursor != '\0'; ++cursor)
                hash = (hash ^ static_cast<u8>(*cursor)) * 1099511628211ull;
        return hash != 0 ? hash : 1;
    }

    [[nodiscard]] bool IsWindowEvent(const u32 type) noexcept
    {
        return type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST;
    }

    [[nodiscard]] bool IsDisplayTopologyEvent(const u32 type) noexcept
    {
        return type >= SDL_EVENT_DISPLAY_FIRST && type <= SDL_EVENT_DISPLAY_LAST;
    }

    [[nodiscard]] bool TranslateEventType(const u32 source, win::BackendEventType& destination) noexcept
    {
        switch (source)
        {
        case SDL_EVENT_WINDOW_SHOWN:
            destination = win::BackendEventType::Shown;
            return true;
        case SDL_EVENT_WINDOW_HIDDEN:
            destination = win::BackendEventType::Hidden;
            return true;
        case SDL_EVENT_WINDOW_EXPOSED:
            destination = win::BackendEventType::Exposed;
            return true;
        case SDL_EVENT_WINDOW_MOVED:
            destination = win::BackendEventType::Moved;
            return true;
        case SDL_EVENT_WINDOW_RESIZED:
            destination = win::BackendEventType::Resized;
            return true;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            destination = win::BackendEventType::PixelExtentChanged;
            return true;
        case SDL_EVENT_WINDOW_MINIMIZED:
            destination = win::BackendEventType::Minimized;
            return true;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            destination = win::BackendEventType::Maximized;
            return true;
        case SDL_EVENT_WINDOW_RESTORED:
            destination = win::BackendEventType::Restored;
            return true;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            destination = win::BackendEventType::FocusGained;
            return true;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            destination = win::BackendEventType::FocusLost;
            return true;
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            destination = win::BackendEventType::MouseEntered;
            return true;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            destination = win::BackendEventType::MouseLeft;
            return true;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            destination = win::BackendEventType::CloseRequested;
            return true;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            destination = win::BackendEventType::DisplayChanged;
            return true;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            destination = win::BackendEventType::ContentScaleChanged;
            return true;
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            destination = win::BackendEventType::SafeAreaChanged;
            return true;
        case SDL_EVENT_WINDOW_OCCLUDED:
            destination = win::BackendEventType::OcclusionChanged;
            return true;
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
            destination = win::BackendEventType::HdrStateChanged;
            return true;
        default:
            return false;
        }
    }
} // namespace

namespace vanguard::window::sdl
{
    struct SdlWindowBackend::Impl
    {
        struct Record
        {
            SDL_Window* native = nullptr;
            BackendWindowId id;
            WindowMode mode = WindowMode::Windowed;
        };

        Record records[MaximumWindows]{};
        SDL_Cursor* cursors[static_cast<u32>(CursorShape::Count)]{};
        SDL_Window* textInputWindow = nullptr;
        u32 windowCount = 0;

        [[nodiscard]] Record* Find(const BackendWindowId id) noexcept
        {
            if (!id.IsValid())
                return nullptr;
            for (Record& record : records)
                if (record.native != nullptr && record.id == id)
                    return &record;
            return nullptr;
        }

        [[nodiscard]] Record* Find(const SDL_WindowID id) noexcept
        {
            return Find(BackendWindowId{static_cast<u64>(id)});
        }

        [[nodiscard]] Record* AllocateRecord() noexcept
        {
            for (Record& record : records)
                if (record.native == nullptr)
                    return &record;
            return nullptr;
        }

        [[nodiscard]] BackendStatus QueryState(Record& record, BackendWindowState& state) noexcept
        {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            int pixelWidth = 0;
            int pixelHeight = 0;
            SDL_Rect safe{};
            if (!SDL_GetWindowPosition(record.native, &x, &y) || !SDL_GetWindowSize(record.native, &width, &height) ||
                !SDL_GetWindowSizeInPixels(record.native, &pixelWidth, &pixelHeight) || !SDL_GetWindowSafeArea(record.native, &safe))
                return SdlFailure("failed to query SDL window state");

            const SDL_DisplayID display = SDL_GetDisplayForWindow(record.native);
            if (display == 0)
                return SdlFailure("SDL could not identify the window display");
            const SDL_WindowFlags flags = SDL_GetWindowFlags(record.native);
            state = {};
            state.placement.position = {x, y};
            state.placement.logicalExtent = {static_cast<u32>(width), static_cast<u32>(height)};
            state.placement.display = {static_cast<u64>(display)};
            state.placement.mode = record.mode;
            state.placement.visible = (flags & SDL_WINDOW_HIDDEN) == 0;
            state.pixelExtent = {static_cast<u32>(pixelWidth), static_cast<u32>(pixelHeight)};
            state.safeArea = {{safe.x, safe.y}, {static_cast<u32>(safe.w), static_cast<u32>(safe.h)}};
            state.contentScale = SDL_GetWindowDisplayScale(record.native);
            state.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
            state.mouseFocus = (flags & SDL_WINDOW_MOUSE_FOCUS) != 0;
            state.minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
            state.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
            state.occluded = (flags & SDL_WINDOW_OCCLUDED) != 0;
            const SDL_PropertiesID properties = SDL_GetWindowProperties(record.native);
            state.sdrWhiteLevel = SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f);
            state.hdrHeadroom = SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f);
            state.hdrCapable = SDL_GetBooleanProperty(properties, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
            return BackendStatus::Success();
        }

        [[nodiscard]] BackendStatus SetMode(Record& record, const BackendWindowPlacement& placement) noexcept
        {
            if (placement.mode == WindowMode::Windowed)
            {
                if (!SDL_SetWindowFullscreen(record.native, false))
                    return SdlFailure("failed to leave SDL fullscreen");
                if (!SDL_SetWindowFullscreenMode(record.native, nullptr))
                    return SdlFailure("failed to clear SDL fullscreen mode");
            }
            else if (placement.mode == WindowMode::BorderlessFullscreen)
            {
                if (!SDL_SetWindowFullscreenMode(record.native, nullptr))
                    return SdlFailure("failed to select desktop fullscreen mode");
                if (!SDL_SetWindowFullscreen(record.native, true))
                    return SdlFailure("failed to enter SDL borderless fullscreen");
            }
            else
            {
                SDL_DisplayMode closest{};
                if (!SDL_GetClosestFullscreenDisplayMode(static_cast<SDL_DisplayID>(placement.display.value), static_cast<int>(placement.logicalExtent.width),
                                                         static_cast<int>(placement.logicalExtent.height), 0.0f, true, &closest))
                    return SdlFailure("no matching SDL exclusive fullscreen mode is available");
                if (!SDL_SetWindowFullscreenMode(record.native, &closest))
                    return SdlFailure("failed to select SDL exclusive fullscreen mode");
                if (!SDL_SetWindowFullscreen(record.native, true))
                    return SdlFailure("failed to enter SDL exclusive fullscreen");
            }
            record.mode = placement.mode;
            return BackendStatus::Success();
        }

        [[nodiscard]] SDL_Cursor* ResolveCursor(const CursorShape shape) noexcept
        {
            const u32 index = static_cast<u32>(shape);
            if (index >= static_cast<u32>(CursorShape::Count))
                return nullptr;
            if (cursors[index] != nullptr)
                return cursors[index];
            SDL_SystemCursor systemCursor = SDL_SYSTEM_CURSOR_DEFAULT;
            switch (shape)
            {
            case CursorShape::Arrow: systemCursor = SDL_SYSTEM_CURSOR_DEFAULT; break;
            case CursorShape::TextInput: systemCursor = SDL_SYSTEM_CURSOR_TEXT; break;
            case CursorShape::Move: systemCursor = SDL_SYSTEM_CURSOR_MOVE; break;
            case CursorShape::ResizeNorthSouth: systemCursor = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
            case CursorShape::ResizeEastWest: systemCursor = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
            case CursorShape::ResizeNorthEastSouthWest: systemCursor = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
            case CursorShape::ResizeNorthWestSouthEast: systemCursor = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
            case CursorShape::Pointer: systemCursor = SDL_SYSTEM_CURSOR_POINTER; break;
            case CursorShape::Wait: systemCursor = SDL_SYSTEM_CURSOR_WAIT; break;
            case CursorShape::Progress: systemCursor = SDL_SYSTEM_CURSOR_PROGRESS; break;
            case CursorShape::NotAllowed: systemCursor = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
            default: return nullptr;
            }
            cursors[index] = SDL_CreateSystemCursor(systemCursor);
            return cursors[index];
        }
    };

    SdlWindowBackend::~SdlWindowBackend()
    {
        VG_ASSERT_MSG(m_impl == nullptr, "SdlWindowBackend requires explicit successful Shutdown");
        if (m_impl == nullptr)
            return;
        for (Impl::Record& record : m_impl->records)
            if (record.native != nullptr)
                SDL_DestroyWindow(record.native);
        for (SDL_Cursor* const cursor : m_impl->cursors)
            if (cursor != nullptr)
                SDL_DestroyCursor(cursor);
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Window};
        memory::Free(block);
        m_impl = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    BackendStatus SdlWindowBackend::Initialize() noexcept
    {
        if (m_impl != nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is already initialized");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            return SdlFailure("failed to initialize SDL video");
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Window, sizeof(Impl), alignof(Impl));
        if (!block)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return BackendStatus::Failure(-1, "failed to allocate SDL window backend storage");
        }
        m_impl = ::new (block.address) Impl();
        return BackendStatus::Success();
    }

    BackendStatus SdlWindowBackend::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return BackendStatus::Success();
        if (m_impl->windowCount != 0)
            return BackendStatus::Failure(-1, "SDL window backend still owns live windows");
        for (SDL_Cursor* const cursor : m_impl->cursors)
            if (cursor != nullptr)
                SDL_DestroyCursor(cursor);
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Window};
        memory::Free(block);
        m_impl = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return BackendStatus::Success();
    }

    bool SdlWindowBackend::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    EventTranslation SdlWindowBackend::ProcessEvent(const SDL_Event& source, BackendWindowEvent& destination) noexcept
    {
        destination = {};
        EventTranslation result;
        result.displayTopologyChanged = IsDisplayTopologyEvent(source.type);
        if (m_impl == nullptr || !IsWindowEvent(source.type))
            return result;
        Impl::Record* const record = m_impl->Find(source.window.windowID);
        if (record == nullptr || !TranslateEventType(source.type, destination.type))
            return result;
        destination.window = record->id;
        destination.timestampNanoseconds = source.window.timestamp;
        if (destination.type != BackendEventType::CloseRequested)
        {
            const BackendStatus status = m_impl->QueryState(*record, destination.state);
            if (!status)
            {
                destination.type = BackendEventType::Failure;
                destination.backendCode = status.code;
            }
        }
        result.windowEvent = true;
        return result;
    }

    BackendWindowId SdlWindowBackend::ResolveNativeWindow(const u32 nativeWindowId) const noexcept
    {
        if (m_impl == nullptr)
            return {};
        const Impl::Record* const record = m_impl->Find(static_cast<SDL_WindowID>(nativeWindowId));
        return record != nullptr ? record->id : BackendWindowId{};
    }

    BackendStatus SdlWindowBackend::EnumerateDisplays(BackendDisplaySnapshot* const displays, const u32 capacity, u32& count) noexcept
    {
        count = 0;
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        int nativeCount = 0;
        SDL_DisplayID* const nativeDisplays = SDL_GetDisplays(&nativeCount);
        if (nativeDisplays == nullptr)
            return SdlFailure("failed to enumerate SDL displays");
        count = nativeCount > 0 ? static_cast<u32>(nativeCount) : 0;
        if (displays == nullptr || count > capacity)
        {
            SDL_free(nativeDisplays);
            return BackendStatus::Failure(-1, "SDL display topology exceeds the supplied capacity");
        }
        const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
        for (u32 index = 0; index < count; ++index)
        {
            const SDL_DisplayID id = nativeDisplays[index];
            SDL_Rect bounds{};
            SDL_Rect workArea{};
            const SDL_DisplayMode* const mode = SDL_GetDesktopDisplayMode(id);
            if (!SDL_GetDisplayBounds(id, &bounds) || !SDL_GetDisplayUsableBounds(id, &workArea) || mode == nullptr)
            {
                SDL_free(nativeDisplays);
                count = 0;
                return SdlFailure("failed to query SDL display topology");
            }
            BackendDisplaySnapshot& snapshot = displays[index];
            snapshot = {};
            snapshot.id = {static_cast<u64>(id)};
            snapshot.fingerprint = DisplayFingerprint(id);
            snapshot.bounds = {{bounds.x, bounds.y}, {static_cast<u32>(bounds.w), static_cast<u32>(bounds.h)}};
            snapshot.workArea = {{workArea.x, workArea.y}, {static_cast<u32>(workArea.w), static_cast<u32>(workArea.h)}};
            snapshot.desktopPixelExtent = {static_cast<u32>(mode->w * mode->pixel_density), static_cast<u32>(mode->h * mode->pixel_density)};
            snapshot.desktopRefreshRate = {static_cast<u32>(mode->refresh_rate_numerator),
                                           mode->refresh_rate_denominator > 0 ? static_cast<u32>(mode->refresh_rate_denominator) : 1u};
            snapshot.contentScale = SDL_GetDisplayContentScale(id);
            snapshot.primary = id == primary;
            snapshot.hdrCapable = SDL_GetBooleanProperty(SDL_GetDisplayProperties(id), SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN, false);
        }
        SDL_free(nativeDisplays);
        return count != 0 ? BackendStatus::Success() : BackendStatus::Failure(-1, "SDL reported no displays");
    }

    BackendStatus SdlWindowBackend::Create(const BackendWindowDescriptor& descriptor, BackendWindowId& window, BackendWindowState& state) noexcept
    {
        window = {};
        state = {};
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        Impl::Record* const record = m_impl->AllocateRecord();
        if (record == nullptr)
            return BackendStatus::Failure(-1, "SDL window registry capacity is exhausted");
        SDL_WindowFlags flags = SDL_WINDOW_HIDDEN;
        if (HasFlag(descriptor.flags, WindowFlag::Resizable))
            flags |= SDL_WINDOW_RESIZABLE;
        if (HasFlag(descriptor.flags, WindowFlag::Borderless))
            flags |= SDL_WINDOW_BORDERLESS;
        if (HasFlag(descriptor.flags, WindowFlag::AlwaysOnTop))
            flags |= SDL_WINDOW_ALWAYS_ON_TOP;
        if (HasFlag(descriptor.flags, WindowFlag::Utility) || HasFlag(descriptor.flags, WindowFlag::SkipTaskbar))
            flags |= SDL_WINDOW_UTILITY;
        if (HasFlag(descriptor.flags, WindowFlag::Transparent))
            flags |= SDL_WINDOW_TRANSPARENT;
        if (HasFlag(descriptor.flags, WindowFlag::HighPixelDensity))
            flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (descriptor.surfaceKind == PresentationSurfaceKind::Vulkan)
            flags |= SDL_WINDOW_VULKAN;
        SDL_Window* const parent =
            descriptor.parent.IsValid() && m_impl->Find(descriptor.parent) != nullptr ? m_impl->Find(descriptor.parent)->native : nullptr;
        if (descriptor.parent.IsValid() && parent == nullptr)
            return BackendStatus::Failure(-1, "SDL parent window is invalid");

        SDL_PropertiesID properties = SDL_CreateProperties();
        if (properties == 0)
            return SdlFailure("failed to allocate SDL window properties");
        Sint64 initialX = descriptor.placement.position.x;
        Sint64 initialY = descriptor.placement.position.y;
        const SDL_DisplayID initialDisplay = static_cast<SDL_DisplayID>(descriptor.placement.display.value);
        switch (descriptor.initialPlacement)
        {
        case InitialWindowPlacement::Explicit:
            break;
        case InitialWindowPlacement::CenteredOnDisplay:
            initialX = SDL_WINDOWPOS_CENTERED_DISPLAY(initialDisplay);
            initialY = SDL_WINDOWPOS_CENTERED_DISPLAY(initialDisplay);
            break;
        case InitialWindowPlacement::PlatformDefault:
            initialX = SDL_WINDOWPOS_UNDEFINED_DISPLAY(initialDisplay);
            initialY = SDL_WINDOWPOS_UNDEFINED_DISPLAY(initialDisplay);
            break;
        default:
            SDL_DestroyProperties(properties);
            return BackendStatus::Failure(-1, "SDL window initial placement policy is invalid");
        }
        bool propertiesValid = SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, descriptor.title) &&
                               SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, descriptor.placement.logicalExtent.width) &&
                               SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, descriptor.placement.logicalExtent.height) &&
                               SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, initialX) &&
                               SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, initialY) &&
                               SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
        if (parent != nullptr)
            propertiesValid = propertiesValid && SDL_SetPointerProperty(properties, SDL_PROP_WINDOW_CREATE_PARENT_POINTER, parent);
        SDL_Window* native = propertiesValid ? SDL_CreateWindowWithProperties(properties) : nullptr;
        SDL_DestroyProperties(properties);
        if (native == nullptr)
            return SdlFailure("failed to create SDL window");

        record->native = native;
        record->id = {static_cast<u64>(SDL_GetWindowID(native))};
        record->mode = WindowMode::Windowed;
        if (!record->id.IsValid() || !SDL_SetWindowMinimumSize(native, descriptor.constraints.minimum.width, descriptor.constraints.minimum.height) ||
            !SDL_SetWindowMaximumSize(native, descriptor.constraints.maximum.width, descriptor.constraints.maximum.height))
        {
            SDL_DestroyWindow(native);
            *record = {};
            return SdlFailure("failed to configure SDL window constraints");
        }
        if (descriptor.relationship == WindowRelationship::Modal && !SDL_SetWindowModal(native, true))
        {
            SDL_DestroyWindow(native);
            *record = {};
            return SdlFailure("failed to configure SDL modal ownership");
        }
        BackendStatus status = m_impl->SetMode(*record, descriptor.placement);
        if (status && descriptor.placement.visible && !SDL_ShowWindow(native))
            status = SdlFailure("failed to show SDL window");
        if (status)
            status = m_impl->QueryState(*record, state);
        if (!status)
        {
            SDL_DestroyWindow(native);
            *record = {};
            return status;
        }
        ++m_impl->windowCount;
        window = record->id;
        return BackendStatus::Success();
    }

    BackendStatus SdlWindowBackend::ApplyWindowState(const BackendWindowId window, const BackendWindowRequest& request, BackendWindowState& state) noexcept
    {
        state = {};
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        Impl::Record* const record = m_impl->Find(window);
        if (record == nullptr)
            return BackendStatus::Failure(-1, "SDL window handle is invalid");
        if (HasField(request.fields, WindowStateField::Mode))
        {
            const BackendStatus status = m_impl->SetMode(*record, request.placement);
            if (!status)
                return status;
        }
        if (request.placement.mode == WindowMode::Windowed)
        {
            if (HasField(request.fields, WindowStateField::Position) &&
                !SDL_SetWindowPosition(record->native, request.placement.position.x, request.placement.position.y))
                return SdlFailure("failed to move SDL window");
            if (HasField(request.fields, WindowStateField::Display) && !HasField(request.fields, WindowStateField::Position) &&
                !SDL_SetWindowPosition(record->native,
                                       static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(static_cast<SDL_DisplayID>(request.placement.display.value))),
                                       static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(static_cast<SDL_DisplayID>(request.placement.display.value)))))
                return SdlFailure("failed to move SDL window to its requested display");
            if (HasField(request.fields, WindowStateField::LogicalExtent) &&
                !SDL_SetWindowSize(record->native, request.placement.logicalExtent.width, request.placement.logicalExtent.height))
                return SdlFailure("failed to resize SDL window");
        }
        if (HasField(request.fields, WindowStateField::Visibility))
        {
            // Showing a drag-created window must not take focus away from its capture owner.
            const bool suppressActivation = request.placement.visible && !request.activateWhenShown;
            const bool previousActivation = SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, true);
            if (suppressActivation && !SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0"))
                return BackendStatus::Failure(-1, "SDL rejected showing the window without activation");
            const bool changed = request.placement.visible ? SDL_ShowWindow(record->native) : SDL_HideWindow(record->native);
            if (suppressActivation)
                static_cast<void>(SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, previousActivation ? "1" : "0"));
            if (!changed)
                return SdlFailure("failed to change SDL window visibility");
        }
        return m_impl->QueryState(*record, state);
    }

    BackendStatus SdlWindowBackend::SetWindowTitle(const BackendWindowId window, const char* const title) noexcept
    {
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        Impl::Record* const record = m_impl->Find(window);
        if (record == nullptr)
            return BackendStatus::Failure(-1, "SDL window handle is invalid");
        return SDL_SetWindowTitle(record->native, title) ? BackendStatus::Success() : SdlFailure("failed to set SDL window title");
    }

    BackendStatus SdlWindowBackend::SetWindowOpacity(const BackendWindowId window, const f32 opacity) noexcept
    {
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        if (!(opacity >= 0.0f && opacity <= 1.0f))
            return BackendStatus::Failure(-1, "window opacity must be finite and between zero and one");
        Impl::Record* const record = m_impl->Find(window);
        if (record == nullptr)
            return BackendStatus::Failure(-1, "SDL window is unavailable");
        return SDL_SetWindowOpacity(record->native, opacity) ? BackendStatus::Success() : SdlFailure("failed to set SDL window opacity");
    }

    BackendStatus SdlWindowBackend::RequestWindowFocus(const BackendWindowId window) noexcept
    {
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        Impl::Record* const record = m_impl->Find(window);
        if (record == nullptr)
            return BackendStatus::Failure(-1, "SDL window handle is invalid");
        return SDL_RaiseWindow(record->native) ? BackendStatus::Success() : SdlFailure("failed to focus SDL window");
    }

    BackendStatus SdlWindowBackend::SetCursor(const CursorShape shape, const bool visible) noexcept
    {
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        if (!visible)
            return SDL_HideCursor() ? BackendStatus::Success() : SdlFailure("failed to hide SDL cursor");
        SDL_Cursor* const cursor = m_impl->ResolveCursor(shape);
        if (cursor == nullptr || !SDL_SetCursor(cursor) || !SDL_ShowCursor())
            return SdlFailure("failed to set SDL cursor");
        return BackendStatus::Success();
    }

    BackendStatus SdlWindowBackend::ReadClipboardText(char* const destination, const u32 capacity, u32& requiredCapacity) noexcept
    {
        requiredCapacity = 0;
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        char* const text = SDL_GetClipboardText();
        if (text == nullptr)
            return SdlFailure("failed to read SDL clipboard text");
        const size_t size = std::strlen(text) + 1u;
        if (size > static_cast<size_t>(~u32{0}))
        {
            SDL_free(text);
            return BackendStatus::Failure(-1, "SDL clipboard text exceeds Vanguard capacity");
        }
        requiredCapacity = static_cast<u32>(size);
        if (destination == nullptr)
        {
            SDL_free(text);
            return BackendStatus::Success();
        }
        if (capacity < requiredCapacity)
        {
            SDL_free(text);
            return BackendStatus::Failure(-1, "clipboard destination is too small");
        }
        std::memcpy(destination, text, size);
        SDL_free(text);
        return BackendStatus::Success();
    }

    BackendStatus SdlWindowBackend::WriteClipboardText(const char* const text) noexcept
    {
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        return SDL_SetClipboardText(text) ? BackendStatus::Success() : SdlFailure("failed to write SDL clipboard text");
    }

    BackendStatus SdlWindowBackend::SetTextInput(const BackendWindowId window, const TextInputRequest& request) noexcept
    {
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        Impl::Record* const record = m_impl->Find(window);
        if (record == nullptr)
            return BackendStatus::Failure(-1, "SDL window handle is invalid");
        if (m_impl->textInputWindow != nullptr && (m_impl->textInputWindow != record->native || !request.enabled))
        {
            if (!SDL_StopTextInput(m_impl->textInputWindow))
                return SdlFailure("failed to stop SDL text input");
            m_impl->textInputWindow = nullptr;
        }
        if (!request.enabled)
            return BackendStatus::Success();
        if (request.showIme)
        {
            const SDL_Rect area{request.position.x, request.position.y, 1, static_cast<int>(request.lineHeight)};
            if (!SDL_SetTextInputArea(record->native, &area, 0))
                return SdlFailure("failed to set SDL text-input area");
        }
        if (!SDL_TextInputActive(record->native) && !SDL_StartTextInput(record->native))
            return SdlFailure("failed to start SDL text input");
        m_impl->textInputWindow = record->native;
        return BackendStatus::Success();
    }

    BackendStatus SdlWindowBackend::ResolvePresentationSurface(const BackendWindowId window, NativePresentationSurface& surface) noexcept
    {
        surface = {};
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        Impl::Record* const record = m_impl->Find(window);
        if (record == nullptr)
            return BackendStatus::Failure(-1, "SDL window handle is invalid");
#if defined(_WIN32)
        void* const nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(record->native), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (nativeWindow == nullptr)
            return SdlFailure("SDL did not expose a Win32 presentation surface");
        surface = {NativePresentationSurfaceKind::Win32, nativeWindow, nullptr};
        return BackendStatus::Success();
#else
        return BackendStatus::Failure(-1, "the current SDL platform has no presentation-surface adapter");
#endif
    }

    BackendStatus SdlWindowBackend::DestroyWindow(const BackendWindowId window) noexcept
    {
        if (m_impl == nullptr)
            return BackendStatus::Failure(-1, "SDL window backend is not initialized");
        Impl::Record* const record = m_impl->Find(window);
        if (record == nullptr)
            return BackendStatus::Failure(-1, "SDL window handle is invalid");
        if (m_impl->textInputWindow == record->native)
        {
            static_cast<void>(SDL_StopTextInput(record->native));
            m_impl->textInputWindow = nullptr;
        }
        SDL_DestroyWindow(record->native);
        *record = {};
        --m_impl->windowCount;
        return BackendStatus::Success();
    }
} // namespace vanguard::window::sdl

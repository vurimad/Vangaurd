#include <vanguard/platform/windows/windows_platform_host.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/input/input.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/window/sdl/sdl_window_backend.hpp>

#include <SDL3/SDL.h>
#include <Windows.h>

#include <new>

namespace
{
    using SetProcessDpiAwarenessContextFunction = BOOL(WINAPI*)(HANDLE);

    vanguard::concurrency::Atomic<bool> g_consoleExitRequested;
    vanguard::concurrency::Atomic<bool> g_platformHostActive;

    class SdlInputBackend final : public vanguard::input::IInputBackend
    {
    public:
        ~SdlInputBackend() override
        {
            for (GamepadRecord& record : m_gamepads)
                if (record.handle != nullptr)
                    SDL_CloseGamepad(record.handle);
        }

        [[nodiscard]] vanguard::input::BackendDrainResult Drain(vanguard::input::RawEvent* const destination, const vanguard::u32 capacity) noexcept override
        {
            vanguard::input::BackendDrainResult result;
            result.droppedSinceLastDrain = m_dropped;
            result.resetRequested = m_resetRequested;
            if (destination != nullptr)
            {
                while (result.count < capacity && m_read != m_write)
                {
                    destination[result.count++] = m_events[m_read];
                    m_read = (m_read + 1u) % QueueCapacity;
                }
            }
            m_dropped = 0;
            m_resetRequested = false;
            return result;
        }

        [[nodiscard]] bool SetRumble(const vanguard::input::DeviceId device, const vanguard::f32 lowFrequency, const vanguard::f32 highFrequency,
                                     const vanguard::u32 durationMilliseconds) noexcept override
        {
            GamepadRecord* const record = FindByDevice(device);
            if (record == nullptr || record->handle == nullptr)
                return false;
            const auto low = static_cast<vanguard::u16>(lowFrequency * 65535.0f);
            const auto high = static_cast<vanguard::u16>(highFrequency * 65535.0f);
            return SDL_RumbleGamepad(record->handle, low, high, durationMilliseconds);
        }

        void RequestDeviceRefresh() noexcept override
        {
            m_refreshRequested = true;
        }

        void RefreshIfRequested() noexcept
        {
            if (!m_refreshRequested)
                return;
            m_refreshRequested = false;
            int count = 0;
            SDL_JoystickID* const devices = SDL_GetGamepads(&count);
            if (devices == nullptr)
                return;
            for (int index = 0; index < count; ++index)
                if (FindByInstance(devices[index]) == nullptr)
                    ConnectGamepad(devices[index], SDL_GetTicksNS());
            for (GamepadRecord& record : m_gamepads)
            {
                if (record.handle == nullptr)
                    continue;
                bool present = false;
                for (int index = 0; index < count; ++index)
                    if (devices[index] == record.instance)
                    {
                        present = true;
                        break;
                    }
                if (!present)
                    DisconnectGamepad(record.instance, SDL_GetTicksNS());
            }
            SDL_free(devices);
        }

        void Process(const SDL_Event& event, const vanguard::window::WindowHandle window) noexcept
        {
            using namespace vanguard::input;
            RawEvent translated;
            switch (event.type)
            {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (event.key.scancode > SDL_SCANCODE_UNKNOWN && event.key.scancode < SDL_SCANCODE_COUNT)
                {
                    translated.type = EventType::KeyChanged;
                    translated.deviceType = DeviceType::Keyboard;
                    translated.device = MakeSimpleDevice(DeviceType::Keyboard, event.key.which);
                    translated.timestampNanoseconds = event.key.timestamp;
                    translated.window = window;
                    translated.data.key.key = static_cast<Key>(event.key.scancode);
                    translated.data.key.pressed = event.key.down;
                    translated.data.key.repeated = event.key.repeat;
                    Push(translated);
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                translated.type = EventType::TextInput;
                translated.deviceType = DeviceType::Keyboard;
                translated.device = MakeSimpleDevice(DeviceType::Keyboard, 0);
                translated.timestampNanoseconds = event.text.timestamp;
                translated.window = window;
                if (event.text.text != nullptr)
                {
                    while (translated.data.text.length + 1u < MaximumTextInputBytes && event.text.text[translated.data.text.length] != '\0')
                    {
                        translated.data.text.utf8[translated.data.text.length] = event.text.text[translated.data.text.length];
                        ++translated.data.text.length;
                    }
                    translated.data.text.utf8[translated.data.text.length] = '\0';
                    Push(translated);
                }
                break;
            case SDL_EVENT_KEYBOARD_ADDED:
            case SDL_EVENT_KEYBOARD_REMOVED:
                translated.type = event.type == SDL_EVENT_KEYBOARD_ADDED ? EventType::DeviceConnected : EventType::DeviceDisconnected;
                translated.deviceType = DeviceType::Keyboard;
                translated.device = MakeSimpleDevice(DeviceType::Keyboard, event.kdevice.which);
                translated.timestampNanoseconds = event.kdevice.timestamp;
                Push(translated);
                break;
            case SDL_EVENT_MOUSE_ADDED:
            case SDL_EVENT_MOUSE_REMOVED:
                translated.type = event.type == SDL_EVENT_MOUSE_ADDED ? EventType::DeviceConnected : EventType::DeviceDisconnected;
                translated.deviceType = DeviceType::Mouse;
                translated.device = MakeSimpleDevice(DeviceType::Mouse, event.mdevice.which);
                translated.timestampNanoseconds = event.mdevice.timestamp;
                Push(translated);
                break;
            case SDL_EVENT_MOUSE_MOTION:
            {
                translated.type = EventType::MouseMoved;
                translated.deviceType = DeviceType::Mouse;
                translated.device = MakeSimpleDevice(DeviceType::Mouse, event.motion.which);
                translated.timestampNanoseconds = event.motion.timestamp;
                translated.window = window;
                translated.data.mouseMotion = {event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel};
                SDL_Window* const nativeWindow = SDL_GetWindowFromID(event.motion.windowID);
                int originX = 0;
                int originY = 0;
                if (nativeWindow != nullptr && SDL_GetWindowPosition(nativeWindow, &originX, &originY))
                {
                    translated.data.mouseMotion.desktopX = event.motion.x + static_cast<vanguard::f32>(originX);
                    translated.data.mouseMotion.desktopY = event.motion.y + static_cast<vanguard::f32>(originY);
                    translated.data.mouseMotion.hasDesktopPosition = true;
                }
                Push(translated);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (TranslateMouseButton(event.button.button, translated.data.mouseButton.button))
                {
                    translated.type = EventType::MouseButtonChanged;
                    translated.deviceType = DeviceType::Mouse;
                    translated.device = MakeSimpleDevice(DeviceType::Mouse, event.button.which);
                    translated.timestampNanoseconds = event.button.timestamp;
                    translated.window = window;
                    translated.data.mouseButton.pressed = event.button.down;
                    translated.data.mouseButton.clicks = event.button.clicks;
                    Push(translated);
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                translated.type = EventType::MouseWheel;
                translated.deviceType = DeviceType::Mouse;
                translated.device = MakeSimpleDevice(DeviceType::Mouse, event.wheel.which);
                translated.timestampNanoseconds = event.wheel.timestamp;
                translated.window = window;
                translated.data.wheel.x = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.x : event.wheel.x;
                translated.data.wheel.y = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
                Push(translated);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                ConnectGamepad(event.gdevice.which, event.gdevice.timestamp);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                DisconnectGamepad(event.gdevice.which, event.gdevice.timestamp);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                GamepadRecord* const record = FindByInstance(event.gbutton.which);
                if (record != nullptr && event.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT)
                {
                    translated.type = EventType::GamepadButtonChanged;
                    translated.deviceType = DeviceType::Gamepad;
                    translated.device = record->device;
                    translated.timestampNanoseconds = event.gbutton.timestamp;
                    translated.data.gamepadButton.button = static_cast<GamepadButton>(event.gbutton.button);
                    translated.data.gamepadButton.pressed = event.gbutton.down;
                    Push(translated);
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            {
                GamepadRecord* const record = FindByInstance(event.gaxis.which);
                if (record != nullptr && event.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT)
                {
                    translated.type = EventType::GamepadAxisChanged;
                    translated.deviceType = DeviceType::Gamepad;
                    translated.device = record->device;
                    translated.timestampNanoseconds = event.gaxis.timestamp;
                    translated.data.gamepadAxis.axis = static_cast<GamepadAxis>(event.gaxis.axis);
                    translated.data.gamepadAxis.value = event.gaxis.axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                                                            ? static_cast<vanguard::f32>(event.gaxis.value) / 32767.0f
                                                            : (event.gaxis.value < 0 ? static_cast<vanguard::f32>(event.gaxis.value) / 32768.0f
                                                                                     : static_cast<vanguard::f32>(event.gaxis.value) / 32767.0f);
                    Push(translated);
                }
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                translated.type = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ? EventType::FocusGained : EventType::FocusLost;
                translated.timestampNanoseconds = event.window.timestamp;
                translated.window = window;
                Push(translated);
                break;
            default:
                break;
            }
        }

    private:
        static constexpr vanguard::u32 QueueCapacity = 4096;
        struct GamepadRecord
        {
            SDL_Gamepad* handle = nullptr;
            SDL_JoystickID instance = 0;
            vanguard::input::DeviceId device = vanguard::input::InvalidDeviceId;
            vanguard::u32 generation = 0;
        };

        [[nodiscard]] static vanguard::input::DeviceId MakeSimpleDevice(const vanguard::input::DeviceType type, const vanguard::u32 instance) noexcept
        {
            return (static_cast<vanguard::u64>(type) << 56u) | static_cast<vanguard::u64>(instance + 1u);
        }
        [[nodiscard]] static bool TranslateMouseButton(const vanguard::u8 source, vanguard::input::MouseButton& destination) noexcept
        {
            switch (source)
            {
            case SDL_BUTTON_LEFT:
                destination = vanguard::input::MouseButton::Left;
                return true;
            case SDL_BUTTON_MIDDLE:
                destination = vanguard::input::MouseButton::Middle;
                return true;
            case SDL_BUTTON_RIGHT:
                destination = vanguard::input::MouseButton::Right;
                return true;
            case SDL_BUTTON_X1:
                destination = vanguard::input::MouseButton::Extra1;
                return true;
            case SDL_BUTTON_X2:
                destination = vanguard::input::MouseButton::Extra2;
                return true;
            default:
                return false;
            }
        }
        void Push(const vanguard::input::RawEvent& event) noexcept
        {
            const vanguard::u32 next = (m_write + 1u) % QueueCapacity;
            if (next == m_read)
            {
                ++m_dropped;
                m_resetRequested = true;
                return;
            }
            m_events[m_write] = event;
            m_write = next;
        }
        [[nodiscard]] GamepadRecord* FindByInstance(const SDL_JoystickID instance) noexcept
        {
            for (GamepadRecord& record : m_gamepads)
                if (record.handle != nullptr && record.instance == instance)
                    return &record;
            return nullptr;
        }
        [[nodiscard]] GamepadRecord* FindByDevice(const vanguard::input::DeviceId device) noexcept
        {
            for (GamepadRecord& record : m_gamepads)
                if (record.handle != nullptr && record.device == device)
                    return &record;
            return nullptr;
        }
        void ConnectGamepad(const SDL_JoystickID instance, const vanguard::u64 timestamp) noexcept
        {
            if (FindByInstance(instance) != nullptr)
                return;
            for (vanguard::u32 index = 0; index < vanguard::input::MaximumGamepads; ++index)
            {
                GamepadRecord& record = m_gamepads[index];
                if (record.handle != nullptr)
                    continue;
                SDL_Gamepad* const handle = SDL_OpenGamepad(instance);
                if (handle == nullptr)
                    return;
                ++record.generation;
                if (record.generation == 0)
                    ++record.generation;
                record.handle = handle;
                record.instance = instance;
                record.device = (static_cast<vanguard::u64>(record.generation) << 32u) | (index + 1u);
                vanguard::input::RawEvent event;
                event.type = vanguard::input::EventType::DeviceConnected;
                event.deviceType = vanguard::input::DeviceType::Gamepad;
                event.device = record.device;
                event.timestampNanoseconds = timestamp;
                Push(event);
                return;
            }
        }
        void DisconnectGamepad(const SDL_JoystickID instance, const vanguard::u64 timestamp) noexcept
        {
            GamepadRecord* const record = FindByInstance(instance);
            if (record == nullptr)
                return;
            vanguard::input::RawEvent event;
            event.type = vanguard::input::EventType::DeviceDisconnected;
            event.deviceType = vanguard::input::DeviceType::Gamepad;
            event.device = record->device;
            event.timestampNanoseconds = timestamp;
            Push(event);
            SDL_CloseGamepad(record->handle);
            record->handle = nullptr;
            record->instance = 0;
            record->device = vanguard::input::InvalidDeviceId;
        }

        vanguard::input::RawEvent m_events[QueueCapacity]{};
        GamepadRecord m_gamepads[vanguard::input::MaximumGamepads]{};
        vanguard::u32 m_read = 0;
        vanguard::u32 m_write = 0;
        vanguard::u32 m_dropped = 0;
        bool m_resetRequested = false;
        bool m_refreshRequested = true;
    };

    void ConfigureDpiAwareness() noexcept
    {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is the stable pseudo-handle value -4. Resolve the Windows 10 API
        // dynamically so Vanguard can retain an older minimum-OS build contract and still use the modern mode.
        const HMODULE user32Module = ::GetModuleHandleW(L"User32.dll");
        const auto setProcessDpiAwarenessContext =
            user32Module != nullptr ? reinterpret_cast<SetProcessDpiAwarenessContextFunction>(::GetProcAddress(user32Module, "SetProcessDpiAwarenessContext"))
                                    : nullptr;
        if (setProcessDpiAwarenessContext != nullptr && setProcessDpiAwarenessContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))) != FALSE)
            return;
        static_cast<void>(::SetProcessDPIAware());
    }

    BOOL WINAPI ConsoleControlHandler(const DWORD controlType) noexcept
    {
        switch (controlType)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_consoleExitRequested.SetValue(true);
            return TRUE;
        default:
            return FALSE;
        }
    }
} // namespace

namespace vanguard::platform::windows
{
    WindowsPlatformHost::~WindowsPlatformHost()
    {
        Shutdown();
    }

    const char* WindowsPlatformHost::GetName() const noexcept
    {
        return "Windows";
    }

    application::PlatformStatus WindowsPlatformHost::Initialize(const application::PlatformStartupInfo& startup) noexcept
    {
        if (m_initialized)
            return application::PlatformStatus::Failure("Windows platform host is already initialized");
        if (g_platformHostActive.CompareExchange(true, false))
            return application::PlatformStatus::Failure("another Windows platform host is already active");

        // This must happen before any product service creates an HWND. Failure is non-fatal when process metadata or
        // an embedding host has already selected the awareness mode.
        ConfigureDpiAwareness();

        // An editor activation click also operates the clicked tab/control, matching normal multi-window tool interaction.
        if (application::HasProfile(startup.profile, application::ApplicationProfile::Editor))
            static_cast<void>(SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1"));

        g_consoleExitRequested.SetValue(false);
        if (::SetConsoleCtrlHandler(ConsoleControlHandler, TRUE) == FALSE)
        {
            g_platformHostActive.SetValue(false);
            return application::PlatformStatus::Failure("failed to install the Windows process control handler");
        }
        if (!SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
        {
            static_cast<void>(::SetConsoleCtrlHandler(ConsoleControlHandler, FALSE));
            g_platformHostActive.SetValue(false);
            return application::PlatformStatus::Failure("failed to initialize SDL event and gamepad subsystems");
        }
        vanguard::memory::MemoryBlock inputBlock =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Input, sizeof(SdlInputBackend), alignof(SdlInputBackend));
        if (!inputBlock)
        {
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
            static_cast<void>(::SetConsoleCtrlHandler(ConsoleControlHandler, FALSE));
            g_platformHostActive.SetValue(false);
            return application::PlatformStatus::Failure("failed to allocate the SDL input backend");
        }
        m_inputBackend = ::new (inputBlock.address) SdlInputBackend();

        vanguard::memory::MemoryBlock windowBlock = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Window, sizeof(vanguard::window::sdl::SdlWindowBackend), alignof(vanguard::window::sdl::SdlWindowBackend));
        if (!windowBlock)
        {
            static_cast<SdlInputBackend*>(m_inputBackend)->~SdlInputBackend();
            vanguard::memory::Free(inputBlock);
            m_inputBackend = nullptr;
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
            static_cast<void>(::SetConsoleCtrlHandler(ConsoleControlHandler, FALSE));
            g_platformHostActive.SetValue(false);
            return application::PlatformStatus::Failure("failed to allocate the SDL window backend");
        }
        m_windowBackend = ::new (windowBlock.address) vanguard::window::sdl::SdlWindowBackend();
        const vanguard::window::BackendStatus windowStatus = m_windowBackend->Initialize();
        if (!windowStatus)
        {
            m_windowBackend->~SdlWindowBackend();
            vanguard::memory::Free(windowBlock);
            m_windowBackend = nullptr;
            static_cast<SdlInputBackend*>(m_inputBackend)->~SdlInputBackend();
            vanguard::memory::Free(inputBlock);
            m_inputBackend = nullptr;
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
            static_cast<void>(::SetConsoleCtrlHandler(ConsoleControlHandler, FALSE));
            g_platformHostActive.SetValue(false);
            return application::PlatformStatus::Failure(windowStatus.message != nullptr ? windowStatus.message : "failed to initialize the SDL window backend");
        }
        m_initialized = true;
        return application::PlatformStatus::Success();
    }

    application::PlatformPumpResult WindowsPlatformHost::PumpEvents() noexcept
    {
        if (!m_initialized)
            return {application::PlatformPumpAction::Failure, -1, "Windows platform host is not initialized"};
        if (g_consoleExitRequested.GetValue())
            return {application::PlatformPumpAction::ExitRequested, 0, nullptr};

        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                return {application::PlatformPumpAction::ExitRequested, 0, nullptr};

            vanguard::window::WindowHandle inputWindow;
            vanguard::window::BackendWindowEvent windowEvent;
            const vanguard::window::sdl::EventTranslation translation = m_windowBackend->ProcessEvent(event, windowEvent);
            if (translation.displayTopologyChanged && m_windowEventSink != nullptr)
            {
                const vanguard::window::BackendStatus status = m_windowEventSink->RefreshDisplayTopology();
                if (!status)
                    return {application::PlatformPumpAction::Failure, status.code,
                            status.message != nullptr ? status.message : "window display topology refresh failed"};
            }
            if (translation.windowEvent && m_windowEventSink != nullptr)
            {
                const vanguard::window::WindowEventSinkResult result = m_windowEventSink->ProcessWindowEvent(windowEvent);
                if (!result)
                    return {application::PlatformPumpAction::Failure, result.status.code,
                            result.status.message != nullptr ? result.status.message : "window event processing failed"};
                if (result.action == vanguard::window::WindowEventSinkAction::RequestApplicationExit)
                    return {application::PlatformPumpAction::ExitRequested, 0, nullptr};
            }
            if (m_windowEventSink != nullptr)
            {
                SDL_Window* const nativeWindow = SDL_GetWindowFromEvent(&event);
                if (nativeWindow != nullptr)
                {
                    const vanguard::window::BackendWindowId backendWindow = m_windowBackend->ResolveNativeWindow(SDL_GetWindowID(nativeWindow));
                    inputWindow = m_windowEventSink->ResolveWindow(backendWindow);
                }
            }
            static_cast<SdlInputBackend*>(m_inputBackend)->Process(event, inputWindow);
        }
        static_cast<SdlInputBackend*>(m_inputBackend)->RefreshIfRequested();
        return {};
    }

    input::IInputBackend* WindowsPlatformHost::GetInputBackend() noexcept
    {
        return m_inputBackend;
    }

    window::IWindowBackend* WindowsPlatformHost::GetWindowBackend() noexcept
    {
        return m_windowBackend;
    }

    bool WindowsPlatformHost::AttachWindowEventSink(window::IWindowEventSink* const sink) noexcept
    {
        if (!m_initialized || sink == nullptr || m_windowEventSink != nullptr)
            return false;
        m_windowEventSink = sink;
        return true;
    }

    void WindowsPlatformHost::DetachWindowEventSink(window::IWindowEventSink* const sink) noexcept
    {
        if (m_windowEventSink == sink)
            m_windowEventSink = nullptr;
    }

    void WindowsPlatformHost::Shutdown() noexcept
    {
        if (!m_initialized)
            return;
        m_windowEventSink = nullptr;
        if (m_inputBackend != nullptr)
        {
            static_cast<SdlInputBackend*>(m_inputBackend)->~SdlInputBackend();
            vanguard::memory::MemoryBlock block{m_inputBackend, sizeof(SdlInputBackend), vanguard::memory::PoolId::Input};
            vanguard::memory::Free(block);
            m_inputBackend = nullptr;
        }
        if (m_windowBackend != nullptr)
        {
            static_cast<void>(m_windowBackend->Shutdown());
            m_windowBackend->~SdlWindowBackend();
            vanguard::memory::MemoryBlock block{m_windowBackend, sizeof(vanguard::window::sdl::SdlWindowBackend), vanguard::memory::PoolId::Window};
            vanguard::memory::Free(block);
            m_windowBackend = nullptr;
        }
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
        static_cast<void>(::SetConsoleCtrlHandler(ConsoleControlHandler, FALSE));
        g_consoleExitRequested.SetValue(false);
        g_platformHostActive.SetValue(false);
        m_initialized = false;
    }
} // namespace vanguard::platform::windows

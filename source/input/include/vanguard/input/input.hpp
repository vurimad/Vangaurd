#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/window/window_types.hpp>

namespace vanguard::input
{
    using DeviceId = u64;

    inline constexpr DeviceId InvalidDeviceId = 0;
    inline constexpr u32 MaximumKeyboardKeys = 512;
    inline constexpr u32 KeyboardStateWordCount = MaximumKeyboardKeys / 64;
    inline constexpr u32 MaximumGamepads = 8;
    inline constexpr u32 MaximumBackendEventsPerFrame = 1024;
    inline constexpr u32 MaximumInputEventsPerFrame = 2048;
    inline constexpr u32 MaximumTextInputBytes = 32;

    /// Platform-stable physical keyboard positions. Values follow the USB HID keyboard usage page where possible.
    enum class Key : u16
    {
        Unknown = 0,
        A = 4,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,
        Digit1 = 30,
        Digit2,
        Digit3,
        Digit4,
        Digit5,
        Digit6,
        Digit7,
        Digit8,
        Digit9,
        Digit0,
        Enter = 40,
        Escape,
        Backspace,
        Tab,
        Space,
        Minus,
        Equals,
        LeftBracket,
        RightBracket,
        Backslash,
        NonUsHash,
        Semicolon,
        Apostrophe,
        Grave,
        Comma,
        Period,
        Slash,
        CapsLock,
        F1 = 58,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        PrintScreen = 70,
        ScrollLock,
        Pause,
        Insert,
        Home,
        PageUp,
        Delete,
        End,
        PageDown,
        Right,
        Left,
        Down,
        Up,
        NumLock,
        KeypadDivide,
        KeypadMultiply,
        KeypadMinus,
        KeypadPlus,
        KeypadEnter,
        Keypad1,
        Keypad2,
        Keypad3,
        Keypad4,
        Keypad5,
        Keypad6,
        Keypad7,
        Keypad8,
        Keypad9,
        Keypad0,
        KeypadPeriod,
        NonUsBackslash,
        Application,
        Power,
        KeypadEquals,
        F13,
        F14,
        F15,
        F16,
        F17,
        F18,
        F19,
        F20,
        F21,
        F22,
        F23,
        F24,
        Execute,
        Help,
        Menu,
        Select,
        Stop,
        Again,
        Undo,
        Cut,
        Copy,
        Paste,
        Find,
        Mute,
        VolumeUp,
        VolumeDown,
        KeypadComma = 133,
        KeypadEqualsAs400,
        International1,
        International2,
        International3,
        International4,
        International5,
        International6,
        International7,
        International8,
        International9,
        Language1,
        Language2,
        Language3,
        Language4,
        Language5,
        Language6,
        Language7,
        Language8,
        Language9,
        AltErase,
        SysReq,
        Cancel,
        Clear,
        Prior,
        Return2,
        Separator,
        Out,
        Oper,
        ClearAgain,
        CrSel,
        ExSel,
        Keypad00 = 176,
        Keypad000,
        ThousandsSeparator,
        DecimalSeparator,
        CurrencyUnit,
        CurrencySubunit,
        KeypadLeftParenthesis,
        KeypadRightParenthesis,
        KeypadLeftBrace,
        KeypadRightBrace,
        KeypadTab,
        KeypadBackspace,
        KeypadA,
        KeypadB,
        KeypadC,
        KeypadD,
        KeypadE,
        KeypadF,
        KeypadXor,
        KeypadPower,
        KeypadPercent,
        KeypadLess,
        KeypadGreater,
        KeypadAmpersand,
        KeypadDoubleAmpersand,
        KeypadVerticalBar,
        KeypadDoubleVerticalBar,
        KeypadColon,
        KeypadHash,
        KeypadSpace,
        KeypadAt,
        KeypadExclamation,
        KeypadMemoryStore,
        KeypadMemoryRecall,
        KeypadMemoryClear,
        KeypadMemoryAdd,
        KeypadMemorySubtract,
        KeypadMemoryMultiply,
        KeypadMemoryDivide,
        KeypadPlusMinus,
        KeypadClear,
        KeypadClearEntry,
        KeypadBinary,
        KeypadOctal,
        KeypadDecimal,
        KeypadHexadecimal,
        LeftControl = 224,
        LeftShift,
        LeftAlt,
        LeftGui,
        RightControl,
        RightShift,
        RightAlt,
        RightGui,
        Mode = 257,
        Sleep,
        Wake,
        ChannelIncrement,
        ChannelDecrement,
        MediaPlay,
        MediaPause,
        MediaRecord,
        MediaFastForward,
        MediaRewind,
        MediaNextTrack,
        MediaPreviousTrack,
        MediaStop,
        MediaEject,
        MediaPlayPause,
        MediaSelect,
        ApplicationControlNew,
        ApplicationControlOpen,
        ApplicationControlClose,
        ApplicationControlExit,
        ApplicationControlSave,
        ApplicationControlPrint,
        ApplicationControlProperties,
        ApplicationControlSearch,
        ApplicationControlHome,
        ApplicationControlBack,
        ApplicationControlForward,
        ApplicationControlStop,
        ApplicationControlRefresh,
        ApplicationControlBookmarks,
        SoftLeft,
        SoftRight,
        Call,
        EndCall
    };

    enum class MouseButton : u8
    {
        Left,
        Middle,
        Right,
        Extra1,
        Extra2,
        Count
    };
    enum class GamepadButton : u8
    {
        South,
        East,
        West,
        North,
        Back,
        Guide,
        Start,
        LeftStick,
        RightStick,
        LeftShoulder,
        RightShoulder,
        DpadUp,
        DpadDown,
        DpadLeft,
        DpadRight,
        Misc1,
        RightPaddle1,
        LeftPaddle1,
        RightPaddle2,
        LeftPaddle2,
        Touchpad,
        Misc2,
        Misc3,
        Misc4,
        Misc5,
        Misc6,
        Count
    };
    enum class GamepadAxis : u8
    {
        LeftX,
        LeftY,
        RightX,
        RightY,
        LeftTrigger,
        RightTrigger,
        Count
    };
    enum class DeviceType : u8
    {
        Unknown,
        Keyboard,
        Mouse,
        Gamepad
    };
    enum class EventType : u8
    {
        KeyChanged,
        MouseButtonChanged,
        MouseMoved,
        MouseWheel,
        TextInput,
        GamepadButtonChanged,
        GamepadAxisChanged,
        DeviceConnected,
        DeviceDisconnected,
        FocusGained,
        FocusLost
    };

    struct RawEvent
    {
        EventType type = EventType::KeyChanged;
        DeviceType deviceType = DeviceType::Unknown;
        DeviceId device = InvalidDeviceId;
        u64 timestampNanoseconds = 0;
        window::WindowHandle window;
        union
        {
            struct
            {
                Key key;
                bool pressed;
                bool repeated;
            } key;
            struct
            {
                MouseButton button;
                bool pressed;
                u8 clicks;
            } mouseButton;
            struct
            {
                f32 x;
                f32 y;
                f32 deltaX;
                f32 deltaY;
            } mouseMotion;
            struct
            {
                f32 x;
                f32 y;
            } wheel;
            struct
            {
                GamepadButton button;
                bool pressed;
            } gamepadButton;
            struct
            {
                GamepadAxis axis;
                f32 value;
            } gamepadAxis;
            struct
            {
                char utf8[MaximumTextInputBytes];
                u8 length;
            } text;
        } data{};
    };

    struct BackendDrainResult
    {
        u32 count = 0;
        u32 droppedSinceLastDrain = 0;
        bool resetRequested = false;
    };

    class IInputBackend
    {
    public:
        virtual ~IInputBackend() = default;
        IInputBackend(const IInputBackend&) = delete;
        IInputBackend& operator=(const IInputBackend&) = delete;

        [[nodiscard]] virtual BackendDrainResult Drain(RawEvent* destination, u32 capacity) noexcept = 0;
        [[nodiscard]] virtual bool SetRumble(DeviceId device, f32 lowFrequency, f32 highFrequency, u32 durationMilliseconds) noexcept = 0;
        virtual void RequestDeviceRefresh() noexcept = 0;

    protected:
        IInputBackend() noexcept = default;
    };

    struct KeyboardState
    {
        u64 down[KeyboardStateWordCount]{};
        u64 pressed[KeyboardStateWordCount]{};
        u64 released[KeyboardStateWordCount]{};

        [[nodiscard]] bool IsDown(Key key) const noexcept;
        [[nodiscard]] bool WasPressed(Key key) const noexcept;
        [[nodiscard]] bool WasReleased(Key key) const noexcept;
    };

    struct MouseState
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 deltaX = 0.0f;
        f32 deltaY = 0.0f;
        f32 wheelX = 0.0f;
        f32 wheelY = 0.0f;
        u8 down = 0;
        u8 pressed = 0;
        u8 released = 0;

        [[nodiscard]] bool IsDown(MouseButton button) const noexcept;
        [[nodiscard]] bool WasPressed(MouseButton button) const noexcept;
        [[nodiscard]] bool WasReleased(MouseButton button) const noexcept;
    };

    struct GamepadState
    {
        DeviceId device = InvalidDeviceId;
        u64 down = 0;
        u64 pressed = 0;
        u64 released = 0;
        f32 axes[static_cast<u32>(GamepadAxis::Count)]{};
        bool connected = false;

        [[nodiscard]] bool IsDown(GamepadButton button) const noexcept;
        [[nodiscard]] bool WasPressed(GamepadButton button) const noexcept;
        [[nodiscard]] bool WasReleased(GamepadButton button) const noexcept;
        [[nodiscard]] f32 GetAxis(GamepadAxis axis) const noexcept;
    };

    struct FrameSnapshot
    {
        KeyboardState keyboard;
        MouseState mouse;
        GamepadState gamepads[MaximumGamepads]{};
        DeviceType lastActiveDeviceType = DeviceType::Unknown;
        DeviceId lastActiveDevice = InvalidDeviceId;
        u64 frame = 0;
        bool focused = true;
    };

    struct InputStats
    {
        u64 frames = 0;
        u64 events = 0;
        u64 droppedBackendEvents = 0;
        u64 stateResets = 0;
        u32 connectedGamepads = 0;
        u32 lastFrameEvents = 0;
    };
} // namespace vanguard::input

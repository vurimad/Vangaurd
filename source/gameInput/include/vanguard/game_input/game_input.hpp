#pragma once

#include <vanguard/input/input.hpp>

namespace vanguard::game_input
{
    using ActionId = u64;
    using ContextId = u64;
    using BindingId = u64;
    using CurveId = u64;
    using ListenerId = u64;

    inline constexpr ActionId InvalidActionId = 0;
    inline constexpr ContextId InvalidContextId = 0;
    inline constexpr BindingId InvalidBindingId = 0;
    inline constexpr CurveId InvalidCurveId = 0;
    inline constexpr ListenerId InvalidListenerId = 0;
    inline constexpr u32 MaximumContexts = 64;
    inline constexpr u32 MaximumActions = 256;
    inline constexpr u32 MaximumBindings = 1024;
    inline constexpr u32 MaximumContextStackDepth = 16;
    inline constexpr u32 MaximumBindingModifiers = 4;
    inline constexpr u32 MaximumResponseCurvePoints = 8;
    inline constexpr u32 MaximumActionEventsPerFrame = 1024;
    inline constexpr u32 MaximumListeners = 128;
    inline constexpr u32 MaximumRebindConflicts = 16;
    inline constexpr u32 MaximumNameBytes = 64;

    [[nodiscard]] constexpr u64 MakeId(const char* text) noexcept
    {
        u64 hash = 14695981039346656037ull;
        if (text == nullptr) return 0;
        while (*text != '\0') { hash ^= static_cast<u8>(*text++); hash *= 1099511628211ull; }
        return hash != 0 ? hash : 1;
    }

    enum class ContextLayer : u8 { Player, UserInterface, Debug, Count };
    enum class ActionValueType : u8 { Button, Axis1D, Axis2D };
    enum class AxisComponent : u8 { Scalar, X, Y };
    enum class ControlType : u8
    {
        Key, MouseButton, MouseDeltaX, MouseDeltaY, MouseWheelX, MouseWheelY, GamepadButton, GamepadAxis
    };
    enum class ActionEventType : u8
    {
        Pressed, Released, Tap, MultiTapStarted, MultiTapCompleted, HoldProgress, HoldComplete, Repeat,
        TogglePressed, ToggleReleased, AxisChanged
    };
    enum class ListenerResult : u8 { Continue, ConsumeAction, ConsumeControl };
    enum class RebindPolicy : u8 { RejectConflicts, AllowShared };
    enum class Result : u8
    {
        Success, InvalidArgument, InvalidState, LimitExceeded, DuplicateId, UnknownContext, UnknownAction,
        UnknownBinding, InvalidControl, InvalidMapping, Conflict, NotCompiled, EventOverflow, ListenerFailure
    };

    struct Control
    {
        ControlType type = ControlType::Key;
        u16 code = 0;
        input::DeviceId device = input::InvalidDeviceId;

        [[nodiscard]] static constexpr Control Keyboard(const input::Key key) noexcept
        {
            return {ControlType::Key, static_cast<u16>(key), input::InvalidDeviceId};
        }
        [[nodiscard]] static constexpr Control Mouse(const input::MouseButton button) noexcept
        {
            return {ControlType::MouseButton, static_cast<u16>(button), input::InvalidDeviceId};
        }
        [[nodiscard]] static constexpr Control Gamepad(const input::GamepadButton button,
                                                       const input::DeviceId device = input::InvalidDeviceId) noexcept
        {
            return {ControlType::GamepadButton, static_cast<u16>(button), device};
        }
        [[nodiscard]] static constexpr Control Gamepad(const input::GamepadAxis axis,
                                                       const input::DeviceId device = input::InvalidDeviceId) noexcept
        {
            return {ControlType::GamepadAxis, static_cast<u16>(axis), device};
        }
        [[nodiscard]] constexpr bool operator==(const Control& other) const noexcept
        {
            return type == other.type && code == other.code && device == other.device;
        }
    };

    struct ContextDescriptor
    {
        ContextId id = InvalidContextId;
        const char* name = nullptr;
        ContextLayer layer = ContextLayer::Player;
        i16 priority = 0;
    };

    struct ResponseCurvePoint { f32 input = 0.0f; f32 output = 0.0f; };
    struct ResponseCurve
    {
        ResponseCurvePoint points[MaximumResponseCurvePoints]{};
        u8 count = 0;
    };

    struct ActionDescriptor
    {
        ActionId id = InvalidActionId;
        const char* name = nullptr;
        ActionValueType valueType = ActionValueType::Button;
        i16 priority = 0;
        f32 holdSeconds = 0.0f;
        f32 tapMaximumSeconds = 0.0f;
        f32 multiTapMaximumDownSeconds = 0.0f;
        f32 multiTapMaximumGapSeconds = 0.0f;
        f32 repeatDelaySeconds = 0.0f;
        f32 repeatIntervalSeconds = 0.0f;
        f32 radialDeadzoneInner = 0.0f;
        f32 radialDeadzoneOuter = 1.0f;
        f32 sensitivity = 1.0f;
        ResponseCurve responseCurve;
        u8 multiTapCount = 0;
        bool toggle = false;
        bool consumeControl = false;
    };

    struct BindingDescriptor
    {
        BindingId id = InvalidBindingId;
        ContextId context = InvalidContextId;
        ActionId action = InvalidActionId;
        Control control;
        AxisComponent component = AxisComponent::Scalar;
        f32 scale = 1.0f;
        f32 pressThreshold = 0.5f;
        f32 releaseThreshold = 0.4f;
        containers::ArraySpan<const Control> modifiers;
        bool overridable = false;
    };

    struct ActionState
    {
        ActionId action = InvalidActionId;
        f32 value = 0.0f;
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 durationSeconds = 0.0f;
        f32 holdProgress = 0.0f;
        u32 pressCount = 0;
        u32 releaseCount = 0;
        bool down = false;
        bool holdComplete = false;
        bool toggled = false;
    };

    struct ActionEvent
    {
        ActionId action = InvalidActionId;
        ContextId context = InvalidContextId;
        BindingId binding = InvalidBindingId;
        ActionEventType type = ActionEventType::Pressed;
        Control source;
        f32 value = 0.0f;
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 durationSeconds = 0.0f;
        u64 frame = 0;
        bool consumed = false;
    };

    using ActionListener = ListenerResult (*)(const ActionEvent& event, void* userData) noexcept;
    struct ListenerDescriptor
    {
        ActionId action = InvalidActionId;
        i16 priority = 0;
        ActionListener callback = nullptr;
        void* userData = nullptr;
    };

    struct Conflict
    {
        BindingId binding = InvalidBindingId;
        ContextId context = InvalidContextId;
        ActionId action = InvalidActionId;
    };
    struct ConflictReport
    {
        Conflict conflicts[MaximumRebindConflicts]{};
        u32 count = 0;
        bool truncated = false;
    };

    struct Stats
    {
        u64 frames = 0;
        u64 events = 0;
        u64 consumedEvents = 0;
        u64 rebinds = 0;
        u32 contexts = 0;
        u32 actions = 0;
        u32 bindings = 0;
        u32 listeners = 0;
        u32 activeContexts = 0;
        u32 lastFrameEvents = 0;
        bool compiled = false;
    };

    class ActionMap final
    {
    public:
        struct Impl;

        ActionMap() noexcept;
        ~ActionMap();
        ActionMap(const ActionMap&) = delete;
        ActionMap& operator=(const ActionMap&) = delete;
        ActionMap(ActionMap&& other) noexcept;
        ActionMap& operator=(ActionMap&& other) noexcept;

        [[nodiscard]] Result RegisterContext(const ContextDescriptor& descriptor) noexcept;
        [[nodiscard]] Result RegisterAction(const ActionDescriptor& descriptor) noexcept;
        [[nodiscard]] Result RegisterBinding(const BindingDescriptor& descriptor) noexcept;
        [[nodiscard]] Result Compile() noexcept;

        [[nodiscard]] Result PushContext(ContextId context) noexcept;
        [[nodiscard]] Result PopContext(ContextLayer layer, ContextId expected = InvalidContextId) noexcept;
        [[nodiscard]] Result ResetContext(ContextId context) noexcept;
        [[nodiscard]] Result RemoveContext(ContextId context) noexcept;
        [[nodiscard]] ContextId CurrentContext(ContextLayer layer) const noexcept;
        [[nodiscard]] bool SetLayerActive(ContextLayer layer, bool active) noexcept;
        [[nodiscard]] bool IsLayerActive(ContextLayer layer) const noexcept;

        [[nodiscard]] Result CheckRebind(BindingId binding, const Control& replacement,
                                         ConflictReport& conflicts) const noexcept;
        [[nodiscard]] Result Rebind(BindingId binding, const Control& replacement, RebindPolicy policy,
                                    ConflictReport* conflicts = nullptr) noexcept;
        [[nodiscard]] Result ResetBinding(BindingId binding) noexcept;

        [[nodiscard]] ListenerId RegisterListener(const ListenerDescriptor& descriptor) noexcept;
        [[nodiscard]] bool UnregisterListener(ListenerId listener) noexcept;

        [[nodiscard]] Result Update(const input::FrameSnapshot& snapshot,
                                    containers::ArraySpan<const input::RawEvent> physicalEvents,
                                    f32 deltaSeconds) noexcept;
        [[nodiscard]] const ActionState* FindAction(ActionId action) const noexcept;
        [[nodiscard]] containers::ArraySpan<const ActionEvent> Events() const noexcept;
        [[nodiscard]] Stats GetStats() const noexcept;
        [[nodiscard]] Result LastResult() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
}

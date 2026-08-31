#include <vanguard/game_input/game_input.hpp>

#include <vanguard/memory/memory.hpp>

#include <cmath>
#include <new>

namespace
{
    namespace gi = vanguard::game_input;
    namespace input = vanguard::input;

    [[nodiscard]] bool Finite(const vanguard::f32 value) noexcept
    {
        return value == value && value <= 3.402823466e+38f && value >= -3.402823466e+38f;
    }
    [[nodiscard]] vanguard::f32 Abs(const vanguard::f32 value) noexcept
    {
        return value < 0.0f ? -value : value;
    }
    [[nodiscard]] vanguard::f32 Clamp(const vanguard::f32 value, const vanguard::f32 minimum, const vanguard::f32 maximum) noexcept
    {
        return value < minimum ? minimum : (value > maximum ? maximum : value);
    }
    [[nodiscard]] bool ValidControl(const gi::Control& control) noexcept
    {
        switch (control.type)
        {
        case gi::ControlType::Key:
            return control.code > 0 && control.code < input::MaximumKeyboardKeys;
        case gi::ControlType::MouseButton:
            return control.code < static_cast<vanguard::u16>(input::MouseButton::Count);
        case gi::ControlType::MouseDeltaX:
        case gi::ControlType::MouseDeltaY:
        case gi::ControlType::MouseWheelX:
        case gi::ControlType::MouseWheelY:
            return control.code == 0;
        case gi::ControlType::GamepadButton:
            return control.code < static_cast<vanguard::u16>(input::GamepadButton::Count);
        case gi::ControlType::GamepadAxis:
            return control.code < static_cast<vanguard::u16>(input::GamepadAxis::Count);
        }
        return false;
    }
    [[nodiscard]] vanguard::u32 LayerRank(const gi::ContextLayer layer) noexcept
    {
        switch (layer)
        {
        case gi::ContextLayer::Debug:
            return 3;
        case gi::ContextLayer::UserInterface:
            return 2;
        case gi::ContextLayer::Player:
            return 1;
        default:
            return 0;
        }
    }
    [[nodiscard]] bool CopyName(char (&destination)[gi::MaximumNameBytes], const char* source) noexcept
    {
        if (source == nullptr || source[0] == '\0')
            return false;
        vanguard::u32 index = 0;
        while (source[index] != '\0' && index + 1u < gi::MaximumNameBytes)
        {
            destination[index] = source[index];
            ++index;
        }
        if (source[index] != '\0')
            return false;
        destination[index] = '\0';
        return true;
    }
} // namespace

namespace vanguard::game_input
{
    struct ActionMap::Impl
    {
        struct ContextRecord
        {
            ContextDescriptor descriptor;
            char name[MaximumNameBytes]{};
        };
        struct ActionRecord
        {
            ActionDescriptor descriptor;
            char name[MaximumNameBytes]{};
            ActionState state;
            ContextId activeContext = InvalidContextId;
            BindingId sourceBinding = InvalidBindingId;
            Control sourceControl;
            f32 nextRepeatSeconds = 0.0f;
            f32 multiTapGapSeconds = 0.0f;
            u8 multiTapProgress = 0;
        };
        struct BindingRecord
        {
            BindingDescriptor descriptor;
            Control modifiers[MaximumBindingModifiers]{};
            Control defaultControl;
            u8 modifierCount = 0;
            bool thresholdDown = false;
        };
        struct Group
        {
            u16 contextIndex = 0;
            u16 actionIndex = 0;
            u16 firstBinding = 0;
            u16 bindingCount = 0;
        };
        struct ListenerRecord
        {
            ListenerId id = InvalidListenerId;
            ListenerDescriptor descriptor;
            u64 sequence = 0;
        };
        struct Stack
        {
            ContextId contexts[MaximumContextStackDepth]{};
            u8 count = 0;
            bool active = true;
        };

        ContextRecord contexts[MaximumContexts]{};
        ActionRecord actions[MaximumActions]{};
        BindingRecord bindings[MaximumBindings]{};
        Group groups[MaximumBindings]{};
        ListenerRecord listeners[MaximumListeners]{};
        Stack stacks[static_cast<u32>(ContextLayer::Count)]{};
        ActionEvent events[MaximumActionEventsPerFrame]{};
        u16 bindingOrder[MaximumBindings]{};
        u16 activeGroups[MaximumBindings]{};
        Control consumedControls[MaximumBindings]{};
        Stats stats;
        Result lastResult = Result::Success;
        u64 listenerSequence = 0;
        u32 contextCount = 0;
        u32 actionCount = 0;
        u32 bindingCount = 0;
        u32 groupCount = 0;
        u32 listenerCount = 0;
        u32 eventCount = 0;
        u32 consumedControlCount = 0;
        bool compiled = false;
        bool evaluating = false;
        bool dispatching = false;

        [[nodiscard]] i32 FindContext(const ContextId id) const noexcept
        {
            for (u32 index = 0; index < contextCount; ++index)
                if (contexts[index].descriptor.id == id)
                    return static_cast<i32>(index);
            return -1;
        }
        [[nodiscard]] i32 FindAction(const ActionId id) const noexcept
        {
            for (u32 index = 0; index < actionCount; ++index)
                if (actions[index].descriptor.id == id)
                    return static_cast<i32>(index);
            return -1;
        }
        [[nodiscard]] i32 FindBinding(const BindingId id) const noexcept
        {
            for (u32 index = 0; index < bindingCount; ++index)
                if (bindings[index].descriptor.id == id)
                    return static_cast<i32>(index);
            return -1;
        }
        [[nodiscard]] bool IsContextActive(const u32 contextIndex) const noexcept
        {
            const ContextDescriptor& context = contexts[contextIndex].descriptor;
            const Stack& stack = stacks[static_cast<u32>(context.layer)];
            return stack.active && stack.count != 0 && stack.contexts[stack.count - 1u] == context.id;
        }
        [[nodiscard]] const input::GamepadState* SelectGamepad(const input::FrameSnapshot& snapshot, const input::DeviceId requested) const noexcept
        {
            if (requested != input::InvalidDeviceId)
                for (const input::GamepadState& gamepad : snapshot.gamepads)
                    if (gamepad.connected && gamepad.device == requested)
                        return &gamepad;
            if (snapshot.lastActiveDeviceType == input::DeviceType::Gamepad)
                for (const input::GamepadState& gamepad : snapshot.gamepads)
                    if (gamepad.connected && gamepad.device == snapshot.lastActiveDevice)
                        return &gamepad;
            for (const input::GamepadState& gamepad : snapshot.gamepads)
                if (gamepad.connected)
                    return &gamepad;
            return nullptr;
        }
        [[nodiscard]] f32 Value(const Control& control, const input::FrameSnapshot& snapshot) const noexcept
        {
            switch (control.type)
            {
            case ControlType::Key:
                return snapshot.keyboard.IsDown(static_cast<input::Key>(control.code)) ? 1.0f : 0.0f;
            case ControlType::MouseButton:
                return snapshot.mouse.IsDown(static_cast<input::MouseButton>(control.code)) ? 1.0f : 0.0f;
            case ControlType::MouseDeltaX:
                return snapshot.mouse.deltaX;
            case ControlType::MouseDeltaY:
                return snapshot.mouse.deltaY;
            case ControlType::MouseWheelX:
                return snapshot.mouse.wheelX;
            case ControlType::MouseWheelY:
                return snapshot.mouse.wheelY;
            case ControlType::GamepadButton:
            {
                const input::GamepadState* gamepad = SelectGamepad(snapshot, control.device);
                return gamepad != nullptr && gamepad->IsDown(static_cast<input::GamepadButton>(control.code)) ? 1.0f : 0.0f;
            }
            case ControlType::GamepadAxis:
            {
                const input::GamepadState* gamepad = SelectGamepad(snapshot, control.device);
                return gamepad != nullptr ? gamepad->GetAxis(static_cast<input::GamepadAxis>(control.code)) : 0.0f;
            }
            }
            return 0.0f;
        }
        [[nodiscard]] bool ModifiersDown(const BindingRecord& binding, const input::FrameSnapshot& snapshot) const noexcept
        {
            for (u32 index = 0; index < binding.modifierCount; ++index)
                if (Value(binding.modifiers[index], snapshot) <= 0.5f)
                    return false;
            return true;
        }
        [[nodiscard]] bool SameSource(const Control& binding, const input::RawEvent& event) const noexcept
        {
            if (binding.device != input::InvalidDeviceId && event.device != binding.device)
                return false;
            switch (binding.type)
            {
            case ControlType::Key:
                return event.type == input::EventType::KeyChanged && binding.code == static_cast<u16>(event.data.key.key);
            case ControlType::MouseButton:
                return event.type == input::EventType::MouseButtonChanged && binding.code == static_cast<u16>(event.data.mouseButton.button);
            case ControlType::GamepadButton:
                return event.type == input::EventType::GamepadButtonChanged && binding.code == static_cast<u16>(event.data.gamepadButton.button);
            case ControlType::GamepadAxis:
                return event.type == input::EventType::GamepadAxisChanged && binding.code == static_cast<u16>(event.data.gamepadAxis.axis);
            case ControlType::MouseDeltaX:
                return event.type == input::EventType::MouseMoved;
            case ControlType::MouseDeltaY:
                return event.type == input::EventType::MouseMoved;
            case ControlType::MouseWheelX:
                return event.type == input::EventType::MouseWheel;
            case ControlType::MouseWheelY:
                return event.type == input::EventType::MouseWheel;
            }
            return false;
        }
        void CountTransitions(const BindingRecord& binding, const containers::ArraySpan<const input::RawEvent> physical, u32& presses,
                              u32& releases) const noexcept
        {
            for (const input::RawEvent& event : physical)
            {
                if (!SameSource(binding.descriptor.control, event))
                    continue;
                if (event.type == input::EventType::KeyChanged)
                {
                    if (event.data.key.repeated)
                        continue;
                    event.data.key.pressed ? ++presses : ++releases;
                }
                else if (event.type == input::EventType::MouseButtonChanged)
                    event.data.mouseButton.pressed ? ++presses : ++releases;
                else if (event.type == input::EventType::GamepadButtonChanged)
                    event.data.gamepadButton.pressed ? ++presses : ++releases;
            }
        }
        [[nodiscard]] bool IsConsumed(const Control& control) const noexcept
        {
            for (u32 index = 0; index < consumedControlCount; ++index)
                if (consumedControls[index] == control)
                    return true;
            return false;
        }
        void Consume(const Control& control) noexcept
        {
            if (!IsConsumed(control) && consumedControlCount < MaximumBindings)
                consumedControls[consumedControlCount++] = control;
        }
        [[nodiscard]] f32 Curve(const ResponseCurve& curve, const f32 value) const noexcept
        {
            if (curve.count == 0)
                return value;
            const f32 sign = value < 0.0f ? -1.0f : 1.0f;
            const f32 magnitude = Abs(value);
            if (magnitude <= curve.points[0].input)
                return sign * curve.points[0].output;
            for (u32 index = 1; index < curve.count; ++index)
            {
                if (magnitude > curve.points[index].input)
                    continue;
                const ResponseCurvePoint& left = curve.points[index - 1u];
                const ResponseCurvePoint& right = curve.points[index];
                const f32 range = right.input - left.input;
                const f32 alpha = range > 0.0f ? (magnitude - left.input) / range : 0.0f;
                return sign * (left.output + (right.output - left.output) * alpha);
            }
            return sign * curve.points[curve.count - 1u].output;
        }
        void Shape(ActionRecord& action, f32& x, f32& y) const noexcept
        {
            const ActionDescriptor& descriptor = action.descriptor;
            if (descriptor.valueType == ActionValueType::Axis2D)
            {
                const f32 magnitude = ::sqrtf(x * x + y * y);
                if (magnitude <= descriptor.radialDeadzoneInner)
                {
                    x = 0.0f;
                    y = 0.0f;
                    return;
                }
                const f32 legalRange = descriptor.radialDeadzoneOuter - descriptor.radialDeadzoneInner;
                const f32 normalized = legalRange > 0.0f ? Clamp((magnitude - descriptor.radialDeadzoneInner) / legalRange, 0.0f, 1.0f) : 1.0f;
                const f32 curved = Curve(descriptor.responseCurve, normalized) * descriptor.sensitivity;
                const f32 scale = magnitude > 0.0f ? curved / magnitude : 0.0f;
                x = Clamp(x * scale, -1.0f, 1.0f);
                y = Clamp(y * scale, -1.0f, 1.0f);
            }
            else
            {
                x = Clamp(Curve(descriptor.responseCurve, x) * descriptor.sensitivity, -1.0f, 1.0f);
            }
        }
        [[nodiscard]] bool Emit(const ActionRecord& action, const ContextId context, const BindingId binding, const ActionEventType type, const Control source,
                                const f32 value, const f32 x, const f32 y, const f32 duration) noexcept
        {
            if (eventCount >= MaximumActionEventsPerFrame)
            {
                lastResult = Result::EventOverflow;
                return false;
            }
            ActionEvent& event = events[eventCount++];
            event.action = action.descriptor.id;
            event.context = context;
            event.binding = binding;
            event.type = type;
            event.source = source;
            event.value = value;
            event.x = x;
            event.y = y;
            event.durationSeconds = duration;
            event.frame = stats.frames + 1u;
            return true;
        }
        [[nodiscard]] bool HigherPriority(const Group& left, const Group& right) const noexcept
        {
            const ContextDescriptor& lc = contexts[left.contextIndex].descriptor;
            const ContextDescriptor& rc = contexts[right.contextIndex].descriptor;
            if (LayerRank(lc.layer) != LayerRank(rc.layer))
                return LayerRank(lc.layer) > LayerRank(rc.layer);
            if (lc.priority != rc.priority)
                return lc.priority > rc.priority;
            const ActionDescriptor& la = actions[left.actionIndex].descriptor;
            const ActionDescriptor& ra = actions[right.actionIndex].descriptor;
            if (la.priority != ra.priority)
                return la.priority > ra.priority;
            if (la.id != ra.id)
                return la.id < ra.id;
            return lc.id < rc.id;
        }
        [[nodiscard]] bool EvaluateGroup(const Group& group, const input::FrameSnapshot& snapshot, const containers::ArraySpan<const input::RawEvent> physical,
                                         const f32 deltaSeconds) noexcept
        {
            ActionRecord& action = actions[group.actionIndex];
            ActionState& state = action.state;
            const ContextId contextId = contexts[group.contextIndex].descriptor.id;
            if (action.activeContext != InvalidContextId && action.activeContext != contextId)
            {
                if (action.descriptor.valueType == ActionValueType::Button && state.down)
                    if (!Emit(action, action.activeContext, action.sourceBinding, ActionEventType::Released, action.sourceControl, 0.0f, 0.0f, 0.0f,
                              state.durationSeconds))
                        return false;
                if (action.descriptor.valueType != ActionValueType::Button && (Abs(state.x) > 0.00001f || Abs(state.y) > 0.00001f))
                    if (!Emit(action, action.activeContext, action.sourceBinding, ActionEventType::AxisChanged, action.sourceControl, 0.0f, 0.0f, 0.0f, 0.0f))
                        return false;
                state.down = false;
                state.value = state.x = state.y = state.durationSeconds = state.holdProgress = 0.0f;
                state.holdComplete = false;
                action.multiTapProgress = 0;
                action.multiTapGapSeconds = 0.0f;
            }
            action.activeContext = contextId;
            const bool previousDown = state.down;
            const f32 previousValue = state.value;
            const f32 previousX = state.x;
            const f32 previousY = state.y;
            f32 x = 0.0f, y = 0.0f;
            bool down = false;
            u32 presses = 0, releases = 0;
            BindingId sourceBinding = InvalidBindingId;
            Control source{};
            for (u32 offset = 0; offset < group.bindingCount; ++offset)
            {
                BindingRecord& binding = bindings[bindingOrder[group.firstBinding + offset]];
                if (IsConsumed(binding.descriptor.control) || !ModifiersDown(binding, snapshot))
                    continue;
                f32 value = Value(binding.descriptor.control, snapshot) * binding.descriptor.scale;
                if (action.descriptor.valueType == ActionValueType::Button)
                {
                    if (binding.descriptor.control.type == ControlType::GamepadAxis)
                    {
                        const f32 magnitude = Abs(value);
                        binding.thresholdDown =
                            binding.thresholdDown ? magnitude > binding.descriptor.releaseThreshold : magnitude >= binding.descriptor.pressThreshold;
                        value = binding.thresholdDown ? 1.0f : 0.0f;
                    }
                    down |= value > 0.5f;
                    CountTransitions(binding, physical, presses, releases);
                }
                else if (binding.descriptor.component == AxisComponent::Y)
                    y += value;
                else
                    x += value;
                if ((Abs(value) > 0.00001f || presses != 0 || releases != 0) && sourceBinding == InvalidBindingId)
                {
                    sourceBinding = binding.descriptor.id;
                    source = binding.descriptor.control;
                }
            }
            if (sourceBinding != InvalidBindingId)
            {
                action.sourceBinding = sourceBinding;
                action.sourceControl = source;
            }

            Shape(action, x, y);
            state.pressCount = presses;
            state.releaseCount = releases;
            state.value = action.descriptor.valueType == ActionValueType::Axis2D ? ::sqrtf(x * x + y * y) : x;
            state.x = x;
            state.y = y;
            if (action.descriptor.valueType != ActionValueType::Button)
            {
                if (Abs(previousX - x) > 0.00001f || Abs(previousY - y) > 0.00001f || Abs(previousValue - state.value) > 0.00001f)
                    if (!Emit(action, contextId, sourceBinding, ActionEventType::AxisChanged, source, state.value, x, y, 0.0f))
                        return false;
                if (action.descriptor.consumeControl && (Abs(x) > 0.00001f || Abs(y) > 0.00001f))
                    for (u32 offset = 0; offset < group.bindingCount; ++offset)
                        Consume(bindings[bindingOrder[group.firstBinding + offset]].descriptor.control);
                return true;
            }

            if (presses == 0 && down && !previousDown)
                presses = 1;
            if (releases == 0 && !down && previousDown)
                releases = 1;
            state.down = down;
            if (presses != 0)
            {
                if (action.descriptor.multiTapCount != 0)
                {
                    if (action.multiTapProgress != 0 && action.multiTapGapSeconds > action.descriptor.multiTapMaximumGapSeconds)
                        action.multiTapProgress = 0;
                    if (action.multiTapProgress < action.descriptor.multiTapCount)
                        ++action.multiTapProgress;
                    action.multiTapGapSeconds = 0.0f;
                    if (action.multiTapProgress == action.descriptor.multiTapCount)
                        if (!Emit(action, contextId, sourceBinding, ActionEventType::MultiTapStarted, source, 1.0f, 0.0f, 0.0f, 0.0f))
                            return false;
                }
                state.durationSeconds = 0.0f;
                state.holdComplete = false;
                action.nextRepeatSeconds = action.descriptor.repeatDelaySeconds;
                if (!Emit(action, contextId, sourceBinding, ActionEventType::Pressed, source, 1.0f, 0.0f, 0.0f, 0.0f))
                    return false;
                if (action.descriptor.toggle && (presses & 1u) != 0)
                {
                    state.toggled = !state.toggled;
                    if (!Emit(action, contextId, sourceBinding, state.toggled ? ActionEventType::TogglePressed : ActionEventType::ToggleReleased, source,
                              state.toggled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f))
                        return false;
                }
            }
            else if (down)
                state.durationSeconds += deltaSeconds;

            if (down && action.descriptor.holdSeconds > 0.0f)
            {
                state.holdProgress = Clamp(state.durationSeconds / action.descriptor.holdSeconds, 0.0f, 1.0f);
                if (!Emit(action, contextId, sourceBinding, ActionEventType::HoldProgress, source, state.holdProgress, 0.0f, 0.0f, state.durationSeconds))
                    return false;
                if (!state.holdComplete && state.durationSeconds >= action.descriptor.holdSeconds)
                {
                    state.holdComplete = true;
                    if (!Emit(action, contextId, sourceBinding, ActionEventType::HoldComplete, source, 1.0f, 0.0f, 0.0f, state.durationSeconds))
                        return false;
                }
            }
            else if (!down)
                state.holdProgress = 0.0f;

            if (down && action.descriptor.repeatIntervalSeconds > 0.0f && action.nextRepeatSeconds > 0.0f)
            {
                while (state.durationSeconds >= action.nextRepeatSeconds)
                {
                    if (!Emit(action, contextId, sourceBinding, ActionEventType::Repeat, source, 1.0f, 0.0f, 0.0f, state.durationSeconds))
                        return false;
                    action.nextRepeatSeconds += action.descriptor.repeatIntervalSeconds;
                }
            }
            if (releases != 0)
            {
                if (!Emit(action, contextId, sourceBinding, ActionEventType::Released, source, 0.0f, 0.0f, 0.0f, state.durationSeconds))
                    return false;
                if (action.descriptor.tapMaximumSeconds > 0.0f && state.durationSeconds <= action.descriptor.tapMaximumSeconds)
                    if (!Emit(action, contextId, sourceBinding, ActionEventType::Tap, source, 1.0f, 0.0f, 0.0f, state.durationSeconds))
                        return false;
                if (action.descriptor.multiTapCount != 0)
                {
                    if (state.durationSeconds > action.descriptor.multiTapMaximumDownSeconds)
                        action.multiTapProgress = 0;
                    else if (action.multiTapProgress == action.descriptor.multiTapCount)
                    {
                        if (!Emit(action, contextId, sourceBinding, ActionEventType::MultiTapCompleted, source, 1.0f, 0.0f, 0.0f, state.durationSeconds))
                            return false;
                        action.multiTapProgress = 0;
                    }
                    action.multiTapGapSeconds = 0.0f;
                }
                if (!down)
                {
                    state.durationSeconds = 0.0f;
                    state.holdProgress = 0.0f;
                    state.holdComplete = false;
                }
            }
            if (!down && action.multiTapProgress != 0)
            {
                action.multiTapGapSeconds += deltaSeconds;
                if (action.multiTapGapSeconds > action.descriptor.multiTapMaximumGapSeconds)
                {
                    action.multiTapProgress = 0;
                    action.multiTapGapSeconds = 0.0f;
                }
            }
            if (action.descriptor.consumeControl && (down || presses != 0 || releases != 0))
                for (u32 offset = 0; offset < group.bindingCount; ++offset)
                    Consume(bindings[bindingOrder[group.firstBinding + offset]].descriptor.control);
            return true;
        }
        [[nodiscard]] bool Dispatch() noexcept
        {
            dispatching = true;
            Control listenerConsumed[MaximumActionEventsPerFrame]{};
            u32 listenerConsumedCount = 0;
            for (u32 eventIndex = 0; eventIndex < eventCount; ++eventIndex)
            {
                ActionEvent& event = events[eventIndex];
                for (u32 index = 0; index < listenerConsumedCount; ++index)
                    if (event.source == listenerConsumed[index])
                    {
                        event.consumed = true;
                        break;
                    }
                if (event.consumed)
                {
                    ++stats.consumedEvents;
                    continue;
                }
                for (u32 listenerIndex = 0; listenerIndex < listenerCount; ++listenerIndex)
                {
                    ListenerRecord& listener = listeners[listenerIndex];
                    if (listener.descriptor.action != InvalidActionId && listener.descriptor.action != event.action)
                        continue;
                    const ListenerResult result = listener.descriptor.callback(event, listener.descriptor.userData);
                    if (result == ListenerResult::Continue)
                        continue;
                    event.consumed = true;
                    ++stats.consumedEvents;
                    if (result == ListenerResult::ConsumeControl && ValidControl(event.source) && listenerConsumedCount < MaximumActionEventsPerFrame)
                        listenerConsumed[listenerConsumedCount++] = event.source;
                    break;
                }
            }
            dispatching = false;
            return true;
        }
    };

    ActionMap::ActionMap() noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Input, sizeof(Impl), alignof(Impl));
        if (block)
            m_impl = ::new (block.address) Impl();
    }
    ActionMap::~ActionMap()
    {
        if (m_impl == nullptr)
            return;
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Input};
        memory::Free(block);
    }
    ActionMap::ActionMap(ActionMap&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }
    ActionMap& ActionMap::operator=(ActionMap&& other) noexcept
    {
        if (this == &other)
            return *this;
        if (m_impl != nullptr)
        {
            m_impl->~Impl();
            memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Input};
            memory::Free(block);
        }
        m_impl = other.m_impl;
        other.m_impl = nullptr;
        return *this;
    }

    Result ActionMap::RegisterContext(const ContextDescriptor& descriptor) noexcept
    {
        if (m_impl == nullptr)
            return Result::InvalidState;
        if (m_impl->compiled || m_impl->evaluating || m_impl->dispatching)
            return m_impl->lastResult = Result::InvalidState;
        if (descriptor.id == InvalidContextId || descriptor.name == nullptr || descriptor.name[0] == '\0' ||
            static_cast<u32>(descriptor.layer) >= static_cast<u32>(ContextLayer::Count))
            return m_impl->lastResult = Result::InvalidArgument;
        if (m_impl->FindContext(descriptor.id) >= 0)
            return m_impl->lastResult = Result::DuplicateId;
        if (m_impl->contextCount >= MaximumContexts)
            return m_impl->lastResult = Result::LimitExceeded;
        Impl::ContextRecord& record = m_impl->contexts[m_impl->contextCount];
        if (!CopyName(record.name, descriptor.name))
            return m_impl->lastResult = Result::InvalidArgument;
        record.descriptor = descriptor;
        record.descriptor.name = record.name;
        ++m_impl->contextCount;
        return m_impl->lastResult = Result::Success;
    }
    Result ActionMap::RegisterAction(const ActionDescriptor& descriptor) noexcept
    {
        if (m_impl == nullptr)
            return Result::InvalidState;
        if (m_impl->compiled || m_impl->evaluating || m_impl->dispatching)
            return m_impl->lastResult = Result::InvalidState;
        if (descriptor.id == InvalidActionId || descriptor.name == nullptr || descriptor.name[0] == '\0' ||
            static_cast<u32>(descriptor.valueType) > static_cast<u32>(ActionValueType::Axis2D) || !Finite(descriptor.holdSeconds) ||
            descriptor.holdSeconds < 0.0f || !Finite(descriptor.tapMaximumSeconds) || descriptor.tapMaximumSeconds < 0.0f ||
            !Finite(descriptor.multiTapMaximumDownSeconds) || descriptor.multiTapMaximumDownSeconds < 0.0f || !Finite(descriptor.multiTapMaximumGapSeconds) ||
            descriptor.multiTapMaximumGapSeconds < 0.0f || !Finite(descriptor.repeatDelaySeconds) || descriptor.repeatDelaySeconds < 0.0f ||
            !Finite(descriptor.repeatIntervalSeconds) || descriptor.repeatIntervalSeconds < 0.0f || !Finite(descriptor.radialDeadzoneInner) ||
            !Finite(descriptor.radialDeadzoneOuter) || descriptor.radialDeadzoneInner < 0.0f || descriptor.radialDeadzoneOuter > 1.0f ||
            descriptor.radialDeadzoneInner >= descriptor.radialDeadzoneOuter ||
            (descriptor.multiTapCount != 0 &&
             (descriptor.multiTapCount < 2 || descriptor.multiTapMaximumDownSeconds <= 0.0f || descriptor.multiTapMaximumGapSeconds <= 0.0f)) ||
            !Finite(descriptor.sensitivity) || descriptor.sensitivity <= 0.0f || descriptor.responseCurve.count > MaximumResponseCurvePoints)
            return m_impl->lastResult = Result::InvalidArgument;
        if ((descriptor.repeatDelaySeconds == 0.0f) != (descriptor.repeatIntervalSeconds == 0.0f))
            return m_impl->lastResult = Result::InvalidArgument;
        for (u32 index = 0; index < descriptor.responseCurve.count; ++index)
        {
            const ResponseCurvePoint point = descriptor.responseCurve.points[index];
            if (!Finite(point.input) || !Finite(point.output) || point.input < 0.0f || point.input > 1.0f || point.output < 0.0f || point.output > 1.0f ||
                (index != 0 && point.input <= descriptor.responseCurve.points[index - 1u].input))
                return m_impl->lastResult = Result::InvalidArgument;
        }
        if (m_impl->FindAction(descriptor.id) >= 0)
            return m_impl->lastResult = Result::DuplicateId;
        if (m_impl->actionCount >= MaximumActions)
            return m_impl->lastResult = Result::LimitExceeded;
        Impl::ActionRecord& record = m_impl->actions[m_impl->actionCount];
        if (!CopyName(record.name, descriptor.name))
            return m_impl->lastResult = Result::InvalidArgument;
        record.descriptor = descriptor;
        record.descriptor.name = record.name;
        record.state.action = descriptor.id;
        ++m_impl->actionCount;
        return m_impl->lastResult = Result::Success;
    }
    Result ActionMap::RegisterBinding(const BindingDescriptor& descriptor) noexcept
    {
        if (m_impl == nullptr)
            return Result::InvalidState;
        if (m_impl->compiled || m_impl->evaluating || m_impl->dispatching)
            return m_impl->lastResult = Result::InvalidState;
        if (descriptor.id == InvalidBindingId || !ValidControl(descriptor.control) || !Finite(descriptor.scale) || descriptor.scale == 0.0f ||
            !Finite(descriptor.pressThreshold) || !Finite(descriptor.releaseThreshold) || descriptor.pressThreshold <= 0.0f ||
            descriptor.pressThreshold > 1.0f || descriptor.releaseThreshold < 0.0f || descriptor.releaseThreshold >= descriptor.pressThreshold ||
            descriptor.modifiers.Size() > MaximumBindingModifiers)
            return m_impl->lastResult = Result::InvalidArgument;
        const i32 context = m_impl->FindContext(descriptor.context);
        const i32 action = m_impl->FindAction(descriptor.action);
        if (context < 0)
            return m_impl->lastResult = Result::UnknownContext;
        if (action < 0)
            return m_impl->lastResult = Result::UnknownAction;
        if (m_impl->FindBinding(descriptor.id) >= 0)
            return m_impl->lastResult = Result::DuplicateId;
        if (m_impl->bindingCount >= MaximumBindings)
            return m_impl->lastResult = Result::LimitExceeded;
        const ActionValueType valueType = m_impl->actions[action].descriptor.valueType;
        if ((valueType == ActionValueType::Axis2D && descriptor.component == AxisComponent::Scalar) ||
            (valueType != ActionValueType::Axis2D && descriptor.component != AxisComponent::Scalar))
            return m_impl->lastResult = Result::InvalidMapping;
        Impl::BindingRecord& record = m_impl->bindings[m_impl->bindingCount++];
        record.descriptor = descriptor;
        record.descriptor.modifiers = {};
        record.defaultControl = descriptor.control;
        record.modifierCount = static_cast<u8>(descriptor.modifiers.Size());
        for (u32 index = 0; index < record.modifierCount; ++index)
        {
            if (!ValidControl(descriptor.modifiers[index]) || descriptor.modifiers[index].type == ControlType::GamepadAxis)
            {
                --m_impl->bindingCount;
                return m_impl->lastResult = Result::InvalidControl;
            }
            record.modifiers[index] = descriptor.modifiers[index];
        }
        return m_impl->lastResult = Result::Success;
    }
    Result ActionMap::Compile() noexcept
    {
        if (m_impl == nullptr)
            return Result::InvalidState;
        if (m_impl->compiled)
            return Result::Success;
        if (m_impl->evaluating || m_impl->dispatching)
            return m_impl->lastResult = Result::InvalidState;
        for (u32 index = 0; index < m_impl->bindingCount; ++index)
            m_impl->bindingOrder[index] = static_cast<u16>(index);
        for (u32 index = 1; index < m_impl->bindingCount; ++index)
        {
            const u16 value = m_impl->bindingOrder[index];
            u32 destination = index;
            while (destination != 0)
            {
                const Impl::BindingRecord& left = m_impl->bindings[value];
                const Impl::BindingRecord& right = m_impl->bindings[m_impl->bindingOrder[destination - 1u]];
                if (left.descriptor.context > right.descriptor.context ||
                    (left.descriptor.context == right.descriptor.context && left.descriptor.action >= right.descriptor.action))
                    break;
                m_impl->bindingOrder[destination] = m_impl->bindingOrder[destination - 1u];
                --destination;
            }
            m_impl->bindingOrder[destination] = value;
        }
        m_impl->groupCount = 0;
        for (u32 offset = 0; offset < m_impl->bindingCount;)
        {
            const Impl::BindingRecord& first = m_impl->bindings[m_impl->bindingOrder[offset]];
            Impl::Group& group = m_impl->groups[m_impl->groupCount++];
            group.contextIndex = static_cast<u16>(m_impl->FindContext(first.descriptor.context));
            group.actionIndex = static_cast<u16>(m_impl->FindAction(first.descriptor.action));
            group.firstBinding = static_cast<u16>(offset);
            do
            {
                ++offset;
                ++group.bindingCount;
            } while (offset < m_impl->bindingCount && m_impl->bindings[m_impl->bindingOrder[offset]].descriptor.context == first.descriptor.context &&
                     m_impl->bindings[m_impl->bindingOrder[offset]].descriptor.action == first.descriptor.action);
        }
        m_impl->compiled = true;
        m_impl->stats = {0, 0, 0, 0, m_impl->contextCount, m_impl->actionCount, m_impl->bindingCount, m_impl->listenerCount, 0, 0, true};
        return m_impl->lastResult = Result::Success;
    }

    Result ActionMap::PushContext(const ContextId context) noexcept
    {
        if (m_impl == nullptr || m_impl->evaluating || m_impl->dispatching)
            return Result::InvalidState;
        const i32 found = m_impl->FindContext(context);
        if (found < 0)
            return m_impl->lastResult = Result::UnknownContext;
        Impl::Stack& stack = m_impl->stacks[static_cast<u32>(m_impl->contexts[found].descriptor.layer)];
        if (stack.count >= MaximumContextStackDepth)
            return m_impl->lastResult = Result::LimitExceeded;
        for (u32 index = 0; index < stack.count; ++index)
            if (stack.contexts[index] == context)
                return m_impl->lastResult = Result::DuplicateId;
        stack.contexts[stack.count++] = context;
        return m_impl->lastResult = Result::Success;
    }
    Result ActionMap::PopContext(const ContextLayer layer, const ContextId expected) noexcept
    {
        if (m_impl == nullptr || m_impl->evaluating || m_impl->dispatching || static_cast<u32>(layer) >= static_cast<u32>(ContextLayer::Count))
            return Result::InvalidState;
        Impl::Stack& stack = m_impl->stacks[static_cast<u32>(layer)];
        if (stack.count == 0 || (expected != InvalidContextId && stack.contexts[stack.count - 1u] != expected))
            return m_impl->lastResult = Result::UnknownContext;
        --stack.count;
        return m_impl->lastResult = Result::Success;
    }
    Result ActionMap::ResetContext(const ContextId context) noexcept
    {
        if (m_impl == nullptr || m_impl->evaluating || m_impl->dispatching)
            return Result::InvalidState;
        const i32 found = m_impl->FindContext(context);
        if (found < 0)
            return m_impl->lastResult = Result::UnknownContext;
        Impl::Stack& stack = m_impl->stacks[static_cast<u32>(m_impl->contexts[found].descriptor.layer)];
        stack.count = 1;
        stack.contexts[0] = context;
        return m_impl->lastResult = Result::Success;
    }
    Result ActionMap::RemoveContext(const ContextId context) noexcept
    {
        if (m_impl == nullptr || m_impl->evaluating || m_impl->dispatching)
            return Result::InvalidState;
        const i32 found = m_impl->FindContext(context);
        if (found < 0)
            return m_impl->lastResult = Result::UnknownContext;
        Impl::Stack& stack = m_impl->stacks[static_cast<u32>(m_impl->contexts[found].descriptor.layer)];
        for (u32 index = 0; index < stack.count; ++index)
            if (stack.contexts[index] == context)
            {
                for (u32 next = index + 1u; next < stack.count; ++next)
                    stack.contexts[next - 1u] = stack.contexts[next];
                --stack.count;
                return m_impl->lastResult = Result::Success;
            }
        return m_impl->lastResult = Result::UnknownContext;
    }
    ContextId ActionMap::GetCurrentContext(const ContextLayer layer) const noexcept
    {
        if (m_impl == nullptr || static_cast<u32>(layer) >= static_cast<u32>(ContextLayer::Count))
            return InvalidContextId;
        const Impl::Stack& stack = m_impl->stacks[static_cast<u32>(layer)];
        return stack.count != 0 ? stack.contexts[stack.count - 1u] : InvalidContextId;
    }
    bool ActionMap::SetLayerActive(const ContextLayer layer, const bool active) noexcept
    {
        if (m_impl == nullptr || m_impl->evaluating || m_impl->dispatching || static_cast<u32>(layer) >= static_cast<u32>(ContextLayer::Count))
            return false;
        m_impl->stacks[static_cast<u32>(layer)].active = active;
        return true;
    }
    bool ActionMap::IsLayerActive(const ContextLayer layer) const noexcept
    {
        return m_impl != nullptr && static_cast<u32>(layer) < static_cast<u32>(ContextLayer::Count) && m_impl->stacks[static_cast<u32>(layer)].active;
    }

    Result ActionMap::CheckRebind(const BindingId binding, const Control& replacement, ConflictReport& conflicts) const noexcept
    {
        conflicts = {};
        if (m_impl == nullptr || !ValidControl(replacement))
            return Result::InvalidControl;
        const i32 targetIndex = m_impl->FindBinding(binding);
        if (targetIndex < 0)
            return Result::UnknownBinding;
        const Impl::BindingRecord& target = m_impl->bindings[targetIndex];
        if (!target.descriptor.overridable)
            return Result::InvalidState;
        for (u32 index = 0; index < m_impl->bindingCount; ++index)
        {
            if (static_cast<i32>(index) == targetIndex)
                continue;
            const Impl::BindingRecord& candidate = m_impl->bindings[index];
            if (candidate.descriptor.context != target.descriptor.context || !(candidate.descriptor.control == replacement) ||
                candidate.modifierCount != target.modifierCount)
                continue;
            bool sameModifiers = true;
            for (u32 modifier = 0; modifier < target.modifierCount; ++modifier)
                if (!(candidate.modifiers[modifier] == target.modifiers[modifier]))
                {
                    sameModifiers = false;
                    break;
                }
            if (!sameModifiers)
                continue;
            if (conflicts.count < MaximumRebindConflicts)
                conflicts.conflicts[conflicts.count++] = {candidate.descriptor.id, candidate.descriptor.context, candidate.descriptor.action};
            else
                conflicts.truncated = true;
        }
        return conflicts.count != 0 || conflicts.truncated ? Result::Conflict : Result::Success;
    }
    Result ActionMap::Rebind(const BindingId binding, const Control& replacement, const RebindPolicy policy, ConflictReport* const conflicts) noexcept
    {
        if (m_impl == nullptr || m_impl->evaluating || m_impl->dispatching)
            return Result::InvalidState;
        ConflictReport local;
        const Result checked = CheckRebind(binding, replacement, local);
        if (conflicts != nullptr)
            *conflicts = local;
        if (checked != Result::Success && !(checked == Result::Conflict && policy == RebindPolicy::AllowShared))
            return m_impl->lastResult = checked;
        const i32 found = m_impl->FindBinding(binding);
        m_impl->bindings[found].descriptor.control = replacement;
        ++m_impl->stats.rebinds;
        return m_impl->lastResult = Result::Success;
    }
    Result ActionMap::ResetBinding(const BindingId binding) noexcept
    {
        if (m_impl == nullptr || m_impl->evaluating || m_impl->dispatching)
            return Result::InvalidState;
        const i32 found = m_impl->FindBinding(binding);
        if (found < 0)
            return m_impl->lastResult = Result::UnknownBinding;
        if (!m_impl->bindings[found].descriptor.overridable)
            return m_impl->lastResult = Result::InvalidState;
        m_impl->bindings[found].descriptor.control = m_impl->bindings[found].defaultControl;
        return m_impl->lastResult = Result::Success;
    }

    ListenerId ActionMap::RegisterListener(const ListenerDescriptor& descriptor) noexcept
    {
        if (m_impl == nullptr || m_impl->evaluating || m_impl->dispatching || descriptor.callback == nullptr ||
            (descriptor.action != InvalidActionId && m_impl->FindAction(descriptor.action) < 0) || m_impl->listenerCount >= MaximumListeners)
            return InvalidListenerId;
        Impl::ListenerRecord record;
        record.sequence = ++m_impl->listenerSequence;
        record.id = record.sequence != 0 ? record.sequence : ++m_impl->listenerSequence;
        record.descriptor = descriptor;
        u32 position = m_impl->listenerCount;
        while (position != 0 && m_impl->listeners[position - 1u].descriptor.priority < descriptor.priority)
        {
            m_impl->listeners[position] = m_impl->listeners[position - 1u];
            --position;
        }
        m_impl->listeners[position] = record;
        ++m_impl->listenerCount;
        m_impl->stats.listeners = m_impl->listenerCount;
        return record.id;
    }
    bool ActionMap::UnregisterListener(const ListenerId listener) noexcept
    {
        if (m_impl == nullptr || m_impl->evaluating || m_impl->dispatching || listener == InvalidListenerId)
            return false;
        for (u32 index = 0; index < m_impl->listenerCount; ++index)
            if (m_impl->listeners[index].id == listener)
            {
                for (u32 next = index + 1u; next < m_impl->listenerCount; ++next)
                    m_impl->listeners[next - 1u] = m_impl->listeners[next];
                --m_impl->listenerCount;
                m_impl->stats.listeners = m_impl->listenerCount;
                return true;
            }
        return false;
    }

    Result ActionMap::Update(const input::FrameSnapshot& snapshot, const containers::ArraySpan<const input::RawEvent> physicalEvents,
                             const f32 deltaSeconds) noexcept
    {
        if (m_impl == nullptr || !m_impl->compiled)
            return m_impl != nullptr ? m_impl->lastResult = Result::NotCompiled : Result::InvalidState;
        if (m_impl->evaluating || m_impl->dispatching || !Finite(deltaSeconds) || deltaSeconds < 0.0f)
            return m_impl->lastResult = Result::InvalidState;
        m_impl->evaluating = true;
        m_impl->eventCount = 0;
        m_impl->consumedControlCount = 0;
        bool evaluated[MaximumActions]{};
        u32 activeCount = 0;
        for (u32 groupIndex = 0; groupIndex < m_impl->groupCount; ++groupIndex)
            if (m_impl->IsContextActive(m_impl->groups[groupIndex].contextIndex))
                m_impl->activeGroups[activeCount++] = static_cast<u16>(groupIndex);
        for (u32 index = 1; index < activeCount; ++index)
        {
            const u16 value = m_impl->activeGroups[index];
            u32 destination = index;
            while (destination != 0 && m_impl->HigherPriority(m_impl->groups[value], m_impl->groups[m_impl->activeGroups[destination - 1u]]))
            {
                m_impl->activeGroups[destination] = m_impl->activeGroups[destination - 1u];
                --destination;
            }
            m_impl->activeGroups[destination] = value;
        }
        m_impl->stats.activeContexts = 0;
        for (const Impl::Stack& stack : m_impl->stacks)
            if (stack.active && stack.count != 0)
                ++m_impl->stats.activeContexts;
        for (u32 index = 0; index < activeCount; ++index)
        {
            const Impl::Group& group = m_impl->groups[m_impl->activeGroups[index]];
            if (evaluated[group.actionIndex])
                continue;
            evaluated[group.actionIndex] = true;
            if (!m_impl->EvaluateGroup(group, snapshot, physicalEvents, deltaSeconds))
            {
                m_impl->evaluating = false;
                return m_impl->lastResult;
            }
        }
        for (u32 actionIndex = 0; actionIndex < m_impl->actionCount; ++actionIndex)
            if (!evaluated[actionIndex])
            {
                Impl::ActionRecord& action = m_impl->actions[actionIndex];
                action.state.pressCount = action.state.releaseCount = 0;
                if (action.state.down)
                {
                    if (!m_impl->Emit(action, action.activeContext, action.sourceBinding, ActionEventType::Released, action.sourceControl, 0.0f, 0.0f, 0.0f,
                                      action.state.durationSeconds))
                    {
                        m_impl->evaluating = false;
                        return m_impl->lastResult;
                    }
                    action.state.down = false;
                    action.state.durationSeconds = 0.0f;
                    action.state.holdProgress = 0.0f;
                    action.state.holdComplete = false;
                }
                else if (action.descriptor.valueType != ActionValueType::Button && (Abs(action.state.x) > 0.00001f || Abs(action.state.y) > 0.00001f))
                {
                    if (!m_impl->Emit(action, action.activeContext, action.sourceBinding, ActionEventType::AxisChanged, action.sourceControl, 0.0f, 0.0f, 0.0f,
                                      0.0f))
                    {
                        m_impl->evaluating = false;
                        return m_impl->lastResult;
                    }
                }
                action.state.value = action.state.x = action.state.y = 0.0f;
                action.multiTapProgress = 0;
                action.multiTapGapSeconds = 0.0f;
                action.activeContext = InvalidContextId;
            }
        m_impl->evaluating = false;
        if (!m_impl->Dispatch())
            return m_impl->lastResult = Result::ListenerFailure;
        ++m_impl->stats.frames;
        m_impl->stats.events += m_impl->eventCount;
        m_impl->stats.lastFrameEvents = m_impl->eventCount;
        return m_impl->lastResult = Result::Success;
    }
    const ActionState* ActionMap::FindAction(const ActionId action) const noexcept
    {
        if (m_impl == nullptr)
            return nullptr;
        const i32 found = m_impl->FindAction(action);
        return found >= 0 ? &m_impl->actions[found].state : nullptr;
    }
    containers::ArraySpan<const ActionEvent> ActionMap::GetEvents() const noexcept
    {
        return m_impl != nullptr ? containers::ArraySpan<const ActionEvent>{m_impl->events, m_impl->eventCount} : containers::ArraySpan<const ActionEvent>{};
    }
    Stats ActionMap::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : Stats{};
    }
    Result ActionMap::GetLastResult() const noexcept
    {
        return m_impl != nullptr ? m_impl->lastResult : Result::InvalidState;
    }
} // namespace vanguard::game_input

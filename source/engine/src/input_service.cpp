#include <vanguard/engine/input_service.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace input = vanguard::input;

    [[nodiscard]] vanguard::u32 BitIndex(const input::Key key) noexcept { return static_cast<vanguard::u32>(key); }
    void ClearWords(vanguard::u64* const words, const vanguard::u32 count) noexcept
    {
        for (vanguard::u32 index = 0; index < count; ++index) words[index] = 0;
    }
    void SetWordBit(vanguard::u64* const words, const vanguard::u32 bit, const bool value) noexcept
    {
        const vanguard::u64 mask = 1ull << (bit & 63u);
        if (value) words[bit >> 6u] |= mask;
        else words[bit >> 6u] &= ~mask;
    }
    [[nodiscard]] bool WordBit(const vanguard::u64* const words, const vanguard::u32 bit) noexcept
    {
        return (words[bit >> 6u] & (1ull << (bit & 63u))) != 0;
    }
    [[nodiscard]] vanguard::f32 ClampUnit(const vanguard::f32 value) noexcept
    {
        if (value != value) return 0.0f;
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

    class ManagedInputService final : public engine::InputService
    {
    public:
        explicit ManagedInputService(input::IInputBackend* const backend) noexcept : m_backend(backend) {}

        [[nodiscard]] const input::FrameSnapshot& Snapshot() const noexcept override { return m_snapshot; }
        [[nodiscard]] vanguard::containers::ArraySpan<const input::RawEvent> Events() const noexcept override
        {
            return {m_events, m_eventCount};
        }
        [[nodiscard]] const input::GamepadState* FindGamepad(const input::DeviceId device) const noexcept override
        {
            for (const input::GamepadState& gamepad : m_snapshot.gamepads)
                if (gamepad.connected && gamepad.device == device) return &gamepad;
            return nullptr;
        }
        [[nodiscard]] bool SetRumble(const input::DeviceId device, const vanguard::f32 lowFrequency,
                                     const vanguard::f32 highFrequency,
                                     const vanguard::u32 durationMilliseconds) noexcept override
        {
            return m_backend != nullptr && FindGamepad(device) != nullptr &&
                   m_backend->SetRumble(device, ClampUnit(lowFrequency), ClampUnit(highFrequency), durationMilliseconds);
        }
        void RequestReset() noexcept override { m_resetRequested = true; }
        void RequestDeviceRefresh() noexcept override
        {
            if (m_backend != nullptr) m_backend->RequestDeviceRefresh();
        }
        [[nodiscard]] input::InputStats GetStats() const noexcept override { return m_stats; }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_framePipeline = engine::FindFramePipelineService(context);
            if (m_backend == nullptr || m_framePipeline == nullptr)
                return app::LifecycleStatus::Failure("Input service dependencies are unavailable");
            engine::FrameParticipantDescriptor descriptor;
            descriptor.id = engine::InputFrameParticipantId;
            descriptor.name = "input";
            descriptor.phase = engine::FramePhase::Input;
            descriptor.profiles = app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor |
                                  app::ApplicationProfile::Tool | app::ApplicationProfile::Test;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            engine::FrameFailure failure;
            if (!m_framePipeline->RegisterParticipant(descriptor, &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message
                                                                               : "Input frame registration failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_backend != nullptr)
                for (const input::GamepadState& gamepad : m_snapshot.gamepads)
                    if (gamepad.connected) static_cast<void>(m_backend->SetRumble(gamepad.device, 0.0f, 0.0f, 0));
            m_backend = nullptr;
            m_framePipeline = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext& context,
                                                            void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedInputService*>(userData);
            if (service == nullptr || !service->Update(context.frame))
                return engine::FrameParticipantStatus::Failure("Input frame collection failed");
            return engine::FrameParticipantStatus::Success();
        }

        [[nodiscard]] bool Update(const vanguard::u64 frame) noexcept
        {
            ClearTransientState();
            const input::BackendDrainResult drain = m_backend->Drain(m_backendEvents, input::MaximumBackendEventsPerFrame);
            if (drain.count > input::MaximumBackendEventsPerFrame) return false;
            m_stats.droppedBackendEvents += drain.droppedSinceLastDrain;
            if (m_resetRequested || drain.resetRequested || drain.droppedSinceLastDrain != 0)
            {
                ResetHeldState(true);
                m_resetRequested = false;
            }
            for (vanguard::u32 index = 0; index < drain.count; ++index)
            {
                if (!Publish(m_backendEvents[index])) return false;
                Apply(m_backendEvents[index]);
            }
            m_snapshot.frame = frame;
            ++m_stats.frames;
            m_stats.events += m_eventCount;
            m_stats.lastFrameEvents = m_eventCount;
            m_stats.connectedGamepads = 0;
            for (const input::GamepadState& gamepad : m_snapshot.gamepads)
                if (gamepad.connected) ++m_stats.connectedGamepads;
            return true;
        }

        void ClearTransientState() noexcept
        {
            ClearWords(m_snapshot.keyboard.pressed, input::KeyboardStateWordCount);
            ClearWords(m_snapshot.keyboard.released, input::KeyboardStateWordCount);
            m_snapshot.mouse.pressed = 0;
            m_snapshot.mouse.released = 0;
            m_snapshot.mouse.deltaX = 0.0f;
            m_snapshot.mouse.deltaY = 0.0f;
            m_snapshot.mouse.wheelX = 0.0f;
            m_snapshot.mouse.wheelY = 0.0f;
            for (input::GamepadState& gamepad : m_snapshot.gamepads)
            {
                gamepad.pressed = 0;
                gamepad.released = 0;
            }
            m_eventCount = 0;
        }

        [[nodiscard]] bool Publish(const input::RawEvent& event) noexcept
        {
            if (m_eventCount >= input::MaximumInputEventsPerFrame) return false;
            m_events[m_eventCount++] = event;
            return true;
        }

        void ResetGamepad(input::GamepadState& gamepad, const bool publish) noexcept
        {
            for (vanguard::u32 button = 0; button < static_cast<vanguard::u32>(input::GamepadButton::Count); ++button)
            {
                const vanguard::u64 mask = 1ull << button;
                if ((gamepad.down & mask) == 0) continue;
                gamepad.released |= mask;
                if (publish)
                {
                    input::RawEvent event;
                    event.type = input::EventType::GamepadButtonChanged;
                    event.deviceType = input::DeviceType::Gamepad;
                    event.device = gamepad.device;
                    event.data.gamepadButton = {static_cast<input::GamepadButton>(button), false};
                    static_cast<void>(Publish(event));
                }
            }
            gamepad.down = 0;
            for (vanguard::u32 axisIndex = 0; axisIndex < static_cast<vanguard::u32>(input::GamepadAxis::Count); ++axisIndex)
            {
                if (gamepad.axes[axisIndex] != 0.0f && publish)
                {
                    input::RawEvent event;
                    event.type = input::EventType::GamepadAxisChanged;
                    event.deviceType = input::DeviceType::Gamepad;
                    event.device = gamepad.device;
                    event.data.gamepadAxis = {static_cast<input::GamepadAxis>(axisIndex), 0.0f};
                    static_cast<void>(Publish(event));
                }
                gamepad.axes[axisIndex] = 0.0f;
            }
        }

        void ResetHeldState(const bool publish) noexcept
        {
            for (vanguard::u32 key = 1; key < input::MaximumKeyboardKeys; ++key)
            {
                if (!WordBit(m_snapshot.keyboard.down, key)) continue;
                SetWordBit(m_snapshot.keyboard.released, key, true);
                SetWordBit(m_snapshot.keyboard.down, key, false);
                if (publish)
                {
                    input::RawEvent event;
                    event.type = input::EventType::KeyChanged;
                    event.deviceType = input::DeviceType::Keyboard;
                    event.data.key = {static_cast<input::Key>(key), false, false};
                    static_cast<void>(Publish(event));
                }
            }
            for (vanguard::u32 button = 0; button < static_cast<vanguard::u32>(input::MouseButton::Count); ++button)
            {
                const auto mask = static_cast<vanguard::u8>(1u << button);
                if ((m_snapshot.mouse.down & mask) == 0) continue;
                m_snapshot.mouse.released |= mask;
                if (publish)
                {
                    input::RawEvent event;
                    event.type = input::EventType::MouseButtonChanged;
                    event.deviceType = input::DeviceType::Mouse;
                    event.data.mouseButton = {static_cast<input::MouseButton>(button), false, 0};
                    static_cast<void>(Publish(event));
                }
            }
            m_snapshot.mouse.down = 0;
            for (input::GamepadState& gamepad : m_snapshot.gamepads)
                if (gamepad.connected) ResetGamepad(gamepad, publish);
            ++m_stats.stateResets;
        }

        input::GamepadState* FindOrCreateGamepad(const input::DeviceId device) noexcept
        {
            for (input::GamepadState& gamepad : m_snapshot.gamepads)
                if (gamepad.connected && gamepad.device == device) return &gamepad;
            for (input::GamepadState& gamepad : m_snapshot.gamepads)
                if (!gamepad.connected)
                {
                    gamepad = {};
                    gamepad.device = device;
                    gamepad.connected = true;
                    return &gamepad;
                }
            return nullptr;
        }

        void MarkActive(const input::RawEvent& event) noexcept
        {
            m_snapshot.lastActiveDeviceType = event.deviceType;
            m_snapshot.lastActiveDevice = event.device;
        }

        void Apply(const input::RawEvent& event) noexcept
        {
            switch (event.type)
            {
            case input::EventType::KeyChanged:
            {
                const vanguard::u32 bit = BitIndex(event.data.key.key);
                if (bit >= input::MaximumKeyboardKeys || event.data.key.key == input::Key::Unknown) break;
                const bool wasDown = WordBit(m_snapshot.keyboard.down, bit);
                SetWordBit(m_snapshot.keyboard.down, bit, event.data.key.pressed);
                if (event.data.key.pressed && !wasDown) SetWordBit(m_snapshot.keyboard.pressed, bit, true);
                if (!event.data.key.pressed && wasDown) SetWordBit(m_snapshot.keyboard.released, bit, true);
                if (!event.data.key.repeated) MarkActive(event);
                break;
            }
            case input::EventType::MouseButtonChanged:
            {
                const vanguard::u8 mask = static_cast<vanguard::u8>(1u << static_cast<vanguard::u32>(event.data.mouseButton.button));
                const bool wasDown = (m_snapshot.mouse.down & mask) != 0;
                if (event.data.mouseButton.pressed) m_snapshot.mouse.down |= mask; else m_snapshot.mouse.down &= static_cast<vanguard::u8>(~mask);
                if (event.data.mouseButton.pressed && !wasDown) m_snapshot.mouse.pressed |= mask;
                if (!event.data.mouseButton.pressed && wasDown) m_snapshot.mouse.released |= mask;
                MarkActive(event);
                break;
            }
            case input::EventType::MouseMoved:
                m_snapshot.mouse.x = event.data.mouseMotion.x;
                m_snapshot.mouse.y = event.data.mouseMotion.y;
                m_snapshot.mouse.deltaX += event.data.mouseMotion.deltaX;
                m_snapshot.mouse.deltaY += event.data.mouseMotion.deltaY;
                if (event.data.mouseMotion.deltaX != 0.0f || event.data.mouseMotion.deltaY != 0.0f) MarkActive(event);
                break;
            case input::EventType::MouseWheel:
                m_snapshot.mouse.wheelX += event.data.wheel.x;
                m_snapshot.mouse.wheelY += event.data.wheel.y;
                MarkActive(event);
                break;
            case input::EventType::GamepadButtonChanged:
            {
                input::GamepadState* const gamepad = FindOrCreateGamepad(event.device);
                if (gamepad == nullptr) break;
                const vanguard::u64 mask = 1ull << static_cast<vanguard::u32>(event.data.gamepadButton.button);
                const bool wasDown = (gamepad->down & mask) != 0;
                if (event.data.gamepadButton.pressed) gamepad->down |= mask; else gamepad->down &= ~mask;
                if (event.data.gamepadButton.pressed && !wasDown) gamepad->pressed |= mask;
                if (!event.data.gamepadButton.pressed && wasDown) gamepad->released |= mask;
                MarkActive(event);
                break;
            }
            case input::EventType::GamepadAxisChanged:
            {
                input::GamepadState* const gamepad = FindOrCreateGamepad(event.device);
                if (gamepad == nullptr) break;
                gamepad->axes[static_cast<vanguard::u32>(event.data.gamepadAxis.axis)] = event.data.gamepadAxis.value;
                if (event.data.gamepadAxis.value > 0.05f || event.data.gamepadAxis.value < -0.05f) MarkActive(event);
                break;
            }
            case input::EventType::DeviceConnected:
                if (event.deviceType == input::DeviceType::Gamepad) static_cast<void>(FindOrCreateGamepad(event.device));
                break;
            case input::EventType::DeviceDisconnected:
                if (event.deviceType == input::DeviceType::Gamepad)
                    for (input::GamepadState& gamepad : m_snapshot.gamepads)
                        if (gamepad.connected && gamepad.device == event.device)
                        {
                            ResetGamepad(gamepad, true);
                            gamepad.connected = false;
                            break;
                        }
                break;
            case input::EventType::FocusGained: m_snapshot.focused = true; break;
            case input::EventType::FocusLost: m_snapshot.focused = false; ResetHeldState(true); break;
            case input::EventType::TextInput: MarkActive(event); break;
            }
        }

        input::IInputBackend* m_backend = nullptr;
        engine::FramePipelineService* m_framePipeline = nullptr;
        input::FrameSnapshot m_snapshot;
        input::RawEvent m_backendEvents[input::MaximumBackendEventsPerFrame]{};
        input::RawEvent m_events[input::MaximumInputEventsPerFrame]{};
        input::InputStats m_stats;
        vanguard::u32 m_eventCount = 0;
        bool m_resetRequested = false;
    };

    app::Service* CreateInputService(void* const userData) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Input, sizeof(ManagedInputService), alignof(ManagedInputService));
        return block ? ::new (block.address) ManagedInputService(static_cast<input::IInputBackend*>(userData)) : nullptr;
    }

    void DestroyInputService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedInputService*>(service)->~ManagedInputService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedInputService), vanguard::memory::PoolId::Input};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterInputService(application::EngineHost& host, input::IInputBackend* const backend,
                              application::HostFailure* const failure) noexcept
    {
        if (backend == nullptr)
        {
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = application::HostFailureCode::InvalidArgument;
                failure->service = InputServiceId;
                failure->message = "Input service requires a platform input backend";
            }
            return false;
        }
        constexpr application::ServiceDependency dependencies[]{
            {FramePipelineServiceId, application::DependencyKind::Required},
            {WindowServiceId, application::DependencyKind::StartAfter}};
        constexpr application::CapabilityId capabilities[]{InputCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = InputServiceId;
        descriptor.name = "input";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor |
                              application::ApplicationProfile::Tool | application::ApplicationProfile::Test;
        descriptor.scope = application::ServiceScope::Engine;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 2};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateInputService;
        descriptor.destroy = DestroyInputService;
        descriptor.userData = backend;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    InputService* FindInputService(application::EngineHost& host) noexcept
    {
        return static_cast<InputService*>(host.FindCapability(InputCapabilityId));
    }
    InputService* FindInputService(application::ServiceContext& context) noexcept
    {
        return static_cast<InputService*>(context.FindCapability(InputCapabilityId));
    }
}

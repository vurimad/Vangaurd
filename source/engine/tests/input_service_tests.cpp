#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/game_input_service.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstdio>

namespace
{
    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition) return;
        std::fprintf(stderr, "[inputServiceTests] FAILED: %s\n", message);
        ++g_failures;
    }

    class FakeBackend final : public vanguard::input::IInputBackend
    {
    public:
        void Push(const vanguard::input::RawEvent& event) noexcept
        {
            if (count < 32) events[count++] = event;
        }
        [[nodiscard]] vanguard::input::BackendDrainResult Drain(vanguard::input::RawEvent* destination,
                                                                 const vanguard::u32 capacity) noexcept override
        {
            vanguard::input::BackendDrainResult result;
            result.count = count < capacity ? count : capacity;
            result.droppedSinceLastDrain = dropped;
            result.resetRequested = reset;
            for (vanguard::u32 index = 0; index < result.count; ++index) destination[index] = events[index];
            count = 0;
            dropped = 0;
            reset = false;
            return result;
        }
        [[nodiscard]] bool SetRumble(const vanguard::input::DeviceId device, const vanguard::f32 low,
                                     const vanguard::f32 high, const vanguard::u32 milliseconds) noexcept override
        {
            rumbleDevice = device;
            rumbleLow = low;
            rumbleHigh = high;
            rumbleMilliseconds = milliseconds;
            return true;
        }
        void RequestDeviceRefresh() noexcept override { refreshRequested = true; }

        vanguard::input::RawEvent events[32]{};
        vanguard::input::DeviceId rumbleDevice = 0;
        vanguard::f32 rumbleLow = 0.0f;
        vanguard::f32 rumbleHigh = 0.0f;
        vanguard::u32 rumbleMilliseconds = 0;
        vanguard::u32 count = 0;
        vanguard::u32 dropped = 0;
        bool reset = false;
        bool refreshRequested = false;
    };

    struct FakeClock { vanguard::u64 ticks = 1000; };
    [[nodiscard]] vanguard::u64 ReadClock(void* const userData) noexcept
    {
        auto* const clock = static_cast<FakeClock*>(userData);
        clock->ticks += 16;
        return clock->ticks;
    }

    [[nodiscard]] vanguard::input::RawEvent KeyEvent(const vanguard::input::Key key, const bool down) noexcept
    {
        vanguard::input::RawEvent event;
        event.type = vanguard::input::EventType::KeyChanged;
        event.deviceType = vanguard::input::DeviceType::Keyboard;
        event.device = 1;
        event.data.key = {key, down, false};
        return event;
    }
}

int main()
{
    Check(vanguard::memory::Initialize(), "memory initialization");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "inputServiceTests"),
          "diagnostics initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    FakeBackend backend;
    vanguard::application::EngineHost host;
    vanguard::application::HostFailure hostFailure;
    Check(vanguard::engine::RegisterEngineModule(host, &hostFailure), "engine module registration");
    Check(vanguard::engine::RegisterIoService(host, &hostFailure), "I/O registration");
    Check(vanguard::engine::RegisterFilesystemService(host, &hostFailure), "filesystem registration");
    Check(vanguard::engine::RegisterJobsService(host, &hostFailure), "Jobs registration");
    Check(vanguard::engine::RegisterFramePipelineService(host, &hostFailure), "frame pipeline registration");
    Check(vanguard::engine::RegisterInputService(host, &backend, &hostFailure), "input registration");
    Check(vanguard::engine::RegisterResourcesService(host, &hostFailure), "resources registration");
    Check(vanguard::engine::RegisterResourceStreamingService(host, &hostFailure), "resource streaming registration");
    Check(vanguard::engine::RegisterGameInputService(host, &hostFailure), "game input registration");
    Check(host.Compile(vanguard::application::ApplicationProfile::Test, &hostFailure), "service graph compilation");
    Check(host.Start(&hostFailure), "service graph start");

    auto* const pipeline = vanguard::engine::FindFramePipelineService(host);
    auto* const input = vanguard::engine::FindInputService(host);
    auto* const gameInput = vanguard::engine::FindGameInputService(host);
    auto* const resourceStreaming = vanguard::engine::FindResourceStreamingService(host);
    constexpr vanguard::game_input::ContextId gameplay = vanguard::game_input::MakeId("test.gameplay");
    constexpr vanguard::game_input::ActionId moveForward = vanguard::game_input::MakeId("test.moveForward");
    Check(gameInput != nullptr, "game input capability");
    Check(resourceStreaming != nullptr && resourceStreaming->Streamer().GetStats().registeredDecoders == 1,
          "cooked game input resource decoder registration");
    Check(gameInput->Mappings().RegisterContext({gameplay, "gameplay"}) == vanguard::game_input::Result::Success,
          "game input context");
    Check(gameInput->Mappings().RegisterAction({moveForward, "moveForward"}) == vanguard::game_input::Result::Success,
          "game input action");
    Check(gameInput->Mappings().RegisterBinding({vanguard::game_input::MakeId("test.moveForward.w"), gameplay,
          moveForward, vanguard::game_input::Control::Keyboard(vanguard::input::Key::W)}) ==
          vanguard::game_input::Result::Success, "game input binding");
    Check(gameInput->Mappings().PushContext(gameplay) == vanguard::game_input::Result::Success,
          "game input context activation");
    FakeClock clock;
    vanguard::engine::FramePipelineConfig config;
    config.clock = {ReadClock, 1000, &clock};
    config.pacing = vanguard::engine::FramePacingMode::Disabled;
    Check(pipeline != nullptr && pipeline->Configure(config), "frame configuration");
    Check(pipeline != nullptr && pipeline->Compile(), "frame compilation");

    backend.Push(KeyEvent(vanguard::input::Key::W, true));
    Check(pipeline->RunFrame(), "key press frame");
    Check(input->Snapshot().keyboard.IsDown(vanguard::input::Key::W) &&
          input->Snapshot().keyboard.WasPressed(vanguard::input::Key::W) &&
          !input->Snapshot().keyboard.WasReleased(vanguard::input::Key::W), "key press snapshot");
    Check(input->Events().Size() == 1, "buffered event publication");
    Check(gameInput->Mappings().FindAction(moveForward) != nullptr &&
          gameInput->Mappings().FindAction(moveForward)->down, "game input runs after physical input");

    Check(pipeline->RunFrame(), "held key frame");
    Check(input->Snapshot().keyboard.IsDown(vanguard::input::Key::W) &&
          !input->Snapshot().keyboard.WasPressed(vanguard::input::Key::W), "transitions clear while held");

    vanguard::input::RawEvent connected;
    connected.type = vanguard::input::EventType::DeviceConnected;
    connected.deviceType = vanguard::input::DeviceType::Gamepad;
    connected.device = 0x100000001ull;
    backend.Push(connected);
    vanguard::input::RawEvent axis;
    axis.type = vanguard::input::EventType::GamepadAxisChanged;
    axis.deviceType = vanguard::input::DeviceType::Gamepad;
    axis.device = connected.device;
    axis.data.gamepadAxis = {vanguard::input::GamepadAxis::LeftX, 0.75f};
    backend.Push(axis);
    Check(pipeline->RunFrame(), "gamepad frame");
    Check(input->FindGamepad(connected.device) != nullptr &&
          input->FindGamepad(connected.device)->Axis(vanguard::input::GamepadAxis::LeftX) == 0.75f,
          "gamepad connection and axis state");
    Check(input->SetRumble(connected.device, -1.0f, 2.0f, 250) && backend.rumbleLow == 0.0f &&
          backend.rumbleHigh == 1.0f && backend.rumbleMilliseconds == 250, "safe clamped rumble output");

    vanguard::input::RawEvent focusLost;
    focusLost.type = vanguard::input::EventType::FocusLost;
    backend.Push(focusLost);
    Check(pipeline->RunFrame(), "focus loss frame");
    Check(!input->Snapshot().focused && !input->Snapshot().keyboard.IsDown(vanguard::input::Key::W) &&
          input->Snapshot().keyboard.WasReleased(vanguard::input::Key::W), "focus loss releases held controls");

    backend.Push(KeyEvent(vanguard::input::Key::A, true));
    backend.dropped = 4;
    Check(pipeline->RunFrame(), "overflow recovery frame");
    Check(input->Snapshot().keyboard.IsDown(vanguard::input::Key::A) &&
          input->GetStats().droppedBackendEvents == 4 && input->GetStats().stateResets >= 2,
          "overflow reset and accounting");

    input->RequestDeviceRefresh();
    Check(backend.refreshRequested, "device refresh forwarding");
    Check(host.Shutdown(&hostFailure), "service graph shutdown");
    vanguard::diagnostics::Shutdown();
    if (g_failures == 0) std::printf("[inputServiceTests] all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}

#include <vanguard/game_input/game_input.hpp>
#include <vanguard/game_input/mapping_resource.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cstdio>

namespace
{
    using namespace vanguard;
    namespace gi = game_input;

    u32 failures = 0;
    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
    void SetKey(input::FrameSnapshot& snapshot, const input::Key key, const bool down) noexcept
    {
        const u32 value = static_cast<u32>(key);
        const u64 mask = 1ull << (value & 63u);
        if (down)
            snapshot.keyboard.down[value >> 6u] |= mask;
        else
            snapshot.keyboard.down[value >> 6u] &= ~mask;
    }
    input::RawEvent KeyEvent(const input::Key key, const bool pressed) noexcept
    {
        input::RawEvent event;
        event.type = input::EventType::KeyChanged;
        event.deviceType = input::DeviceType::Keyboard;
        event.data.key = {key, pressed, false};
        return event;
    }
    bool HasEvent(const gi::ActionMap& map, const gi::ActionEventType type) noexcept
    {
        for (const gi::ActionEvent& event : map.GetEvents())
            if (event.type == type)
                return true;
        return false;
    }
} // namespace

int main()
{
    Check(vanguard::memory::Initialize(), "memory initialization");
    {
        constexpr gi::ContextId gameplay = gi::MakeId("gameplay");
        constexpr gi::ContextId menu = gi::MakeId("menu");
        constexpr gi::ActionId activate = gi::MakeId("activate");
        constexpr gi::ActionId navigate = gi::MakeId("navigate");
        constexpr gi::BindingId activateBinding = gi::MakeId("activate.keyboard");
        constexpr gi::BindingId navigateBinding = gi::MakeId("navigate.keyboard");

        gi::ActionMap map;
        Check(map.RegisterContext({gameplay, "gameplay", gi::ContextLayer::Player, 0}) == gi::Result::Success, "gameplay context registration");
        Check(map.RegisterContext({menu, "menu", gi::ContextLayer::UserInterface, 0}) == gi::Result::Success, "menu context registration");
        gi::ActionDescriptor action{activate, "activate"};
        action.holdSeconds = 0.25f;
        action.tapMaximumSeconds = 0.2f;
        action.repeatDelaySeconds = 0.2f;
        action.repeatIntervalSeconds = 0.1f;
        action.multiTapCount = 2;
        action.multiTapMaximumDownSeconds = 0.2f;
        action.multiTapMaximumGapSeconds = 0.25f;
        Check(map.RegisterAction(action) == gi::Result::Success, "button action registration");
        Check(map.RegisterAction({navigate, "navigate", gi::ActionValueType::Axis1D}) == gi::Result::Success, "axis action registration");
        Check(map.RegisterBinding({activateBinding, gameplay, activate, gi::Control::Keyboard(input::Key::Space)}) == gi::Result::Success,
              "button binding registration");
        gi::BindingDescriptor axis{navigateBinding, menu, navigate, gi::Control::Keyboard(input::Key::D)};
        Check(map.RegisterBinding(axis) == gi::Result::Success, "axis binding registration");
        Check(map.PushContext(gameplay) == gi::Result::Success && map.PushContext(menu) == gi::Result::Success, "context activation");
        Check(map.Compile() == gi::Result::Success, "mapping compilation");

        input::FrameSnapshot snapshot;
        input::RawEvent event = KeyEvent(input::Key::Space, true);
        SetKey(snapshot, input::Key::Space, true);
        Check(map.Update(snapshot, {&event, 1}, 0.0f) == gi::Result::Success && HasEvent(map, gi::ActionEventType::Pressed), "press event");
        Check(map.Update(snapshot, {}, 0.26f) == gi::Result::Success && HasEvent(map, gi::ActionEventType::HoldComplete) &&
                  HasEvent(map, gi::ActionEventType::Repeat),
              "hold and repeat events");
        event = KeyEvent(input::Key::Space, false);
        SetKey(snapshot, input::Key::Space, false);
        Check(map.Update(snapshot, {&event, 1}, 0.0f) == gi::Result::Success && HasEvent(map, gi::ActionEventType::Released), "release event");

        for (u32 tap = 0; tap < 2; ++tap)
        {
            event = KeyEvent(input::Key::Space, true);
            SetKey(snapshot, input::Key::Space, true);
            Check(map.Update(snapshot, {&event, 1}, 0.05f) == gi::Result::Success, "multi-tap press");
            event = KeyEvent(input::Key::Space, false);
            SetKey(snapshot, input::Key::Space, false);
            Check(map.Update(snapshot, {&event, 1}, 0.05f) == gi::Result::Success, "multi-tap release");
        }
        Check(HasEvent(map, gi::ActionEventType::MultiTapCompleted), "multi-tap completion");

        SetKey(snapshot, input::Key::D, true);
        Check(map.Update(snapshot, {}, 0.016f) == gi::Result::Success && map.FindAction(navigate) != nullptr && map.FindAction(navigate)->x == 1.0f,
              "active UI axis mapping");
        Check(map.PopContext(gi::ContextLayer::UserInterface, menu) == gi::Result::Success && map.Update(snapshot, {}, 0.016f) == gi::Result::Success &&
                  HasEvent(map, gi::ActionEventType::AxisChanged) && map.FindAction(navigate)->x == 0.0f,
              "context removal publishes axis reset");

        gi::ContextDescriptor storedContexts[]{{menu, "menu", gi::ContextLayer::UserInterface, 10}, {gameplay, "gameplay", gi::ContextLayer::Player, 0}};
        gi::ActionDescriptor storedActions[]{{navigate, "navigate", gi::ActionValueType::Axis1D}, action};
        gi::BindingDescriptor storedBindings[]{axis, {activateBinding, gameplay, activate, gi::Control::Keyboard(input::Key::Space)}};
        gi::ContextId initialContexts[]{gameplay, menu};
        gi::MappingBuildDescription mappingDescription{{storedContexts, 2}, {storedActions, 2}, {storedBindings, 2}, {initialContexts, 2}};
        containers::DynamicArray<u8> cooked(memory::pools::Input::GetInstance());
        filesystem::MemoryFileWriter mappingWriter(cooked);
        Check(gi::CookMapping(mappingDescription, mappingWriter) == gi::MappingResult::Success, "cook deterministic mapping resource");
        gi::ContextDescriptor reversedContexts[]{storedContexts[1], storedContexts[0]};
        gi::ActionDescriptor reversedActions[]{storedActions[1], storedActions[0]};
        gi::BindingDescriptor reversedBindings[]{storedBindings[1], storedBindings[0]};
        gi::MappingBuildDescription reversedDescription{{reversedContexts, 2}, {reversedActions, 2}, {reversedBindings, 2}, {initialContexts, 2}};
        containers::DynamicArray<u8> recooked(memory::pools::Input::GetInstance());
        filesystem::MemoryFileWriter recookedWriter(recooked);
        Check(gi::CookMapping(reversedDescription, recookedWriter) == gi::MappingResult::Success && cooked.Size() == recooked.Size(),
              "recook reordered source mapping");
        bool byteExact = cooked.Size() == recooked.Size();
        for (u32 index = 0; byteExact && index < cooked.Size(); ++index)
            byteExact = cooked[index] == recooked[index];
        Check(byteExact, "mapping cook is source-order independent and byte deterministic");
        filesystem::MemoryFileReader mappingReader(cooked, 0);
        gi::MappingFile mappingFile;
        Check(mappingFile.Open(mappingReader) == gi::MappingResult::Success && mappingFile.GetContexts().Size() == 2 && mappingFile.GetActions().Size() == 2 &&
                  mappingFile.Bindings().Size() == 2,
              "open and validate cooked mapping resource");
        gi::ActionMap installed;
        Check(mappingFile.Install(installed) == gi::MappingResult::Success && installed.GetStats().compiled &&
                  installed.GetCurrentContext(gi::ContextLayer::UserInterface) == menu,
              "install mapping resource into runtime action map");
        if (cooked.Size() > vanguard::serialization::DocumentHeader::WireSize + 4u)
        {
            cooked[vanguard::serialization::DocumentHeader::WireSize + 4u] ^= 0x80u;
            filesystem::MemoryFileReader corruptReader(cooked, 0);
            gi::MappingFile corrupt;
            Check(corrupt.Open(corruptReader) == gi::MappingResult::IntegrityFailure, "reject corrupted mapping resource");
        }
    }
    std::printf(failures == 0 ? "gameInput tests passed\n" : "gameInput tests failed: %u\n", failures);
    return failures == 0 ? 0 : 1;
}

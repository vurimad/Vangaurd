#include <vanguard/input/input.hpp>

namespace
{
    [[nodiscard]] bool TestBit(const vanguard::u64* const words, const vanguard::u32 bit,
                               const vanguard::u32 limit) noexcept
    {
        return bit < limit && (words[bit >> 6u] & (1ull << (bit & 63u))) != 0;
    }
}

namespace vanguard::input
{
    bool KeyboardState::IsDown(const Key key) const noexcept { return TestBit(down, static_cast<u32>(key), MaximumKeyboardKeys); }
    bool KeyboardState::WasPressed(const Key key) const noexcept { return TestBit(pressed, static_cast<u32>(key), MaximumKeyboardKeys); }
    bool KeyboardState::WasReleased(const Key key) const noexcept { return TestBit(released, static_cast<u32>(key), MaximumKeyboardKeys); }

    bool MouseState::IsDown(const MouseButton button) const noexcept { return (down & (1u << static_cast<u32>(button))) != 0; }
    bool MouseState::WasPressed(const MouseButton button) const noexcept { return (pressed & (1u << static_cast<u32>(button))) != 0; }
    bool MouseState::WasReleased(const MouseButton button) const noexcept { return (released & (1u << static_cast<u32>(button))) != 0; }

    bool GamepadState::IsDown(const GamepadButton button) const noexcept { return TestBit(&down, static_cast<u32>(button), 64); }
    bool GamepadState::WasPressed(const GamepadButton button) const noexcept { return TestBit(&pressed, static_cast<u32>(button), 64); }
    bool GamepadState::WasReleased(const GamepadButton button) const noexcept { return TestBit(&released, static_cast<u32>(button), 64); }
    f32 GamepadState::Axis(const GamepadAxis axis) const noexcept
    {
        const u32 index = static_cast<u32>(axis);
        return index < static_cast<u32>(GamepadAxis::Count) ? axes[index] : 0.0f;
    }
}

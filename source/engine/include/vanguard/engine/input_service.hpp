#pragma once

#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/input/input.hpp>

namespace vanguard::engine
{
    inline constexpr FrameParticipantId InputFrameParticipantId = 0x696e707574667201ull;

    class InputService : public application::Service
    {
    public:
        ~InputService() override = default;

        [[nodiscard]] virtual const input::FrameSnapshot& GetSnapshot() const noexcept = 0;
        [[nodiscard]] virtual containers::ArraySpan<const input::RawEvent> GetEvents() const noexcept = 0;
        [[nodiscard]] virtual const input::GamepadState* FindGamepad(input::DeviceId device) const noexcept = 0;
        [[nodiscard]] virtual bool SetRumble(input::DeviceId device, f32 lowFrequency, f32 highFrequency, u32 durationMilliseconds) noexcept = 0;
        virtual void RequestReset() noexcept = 0;
        virtual void RequestDeviceRefresh() noexcept = 0;
        [[nodiscard]] virtual input::InputStats GetStats() const noexcept = 0;

    protected:
        InputService() noexcept = default;
    };

    [[nodiscard]] InputService* FindInputService(application::EngineHost& host) noexcept;
    [[nodiscard]] InputService* FindInputService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

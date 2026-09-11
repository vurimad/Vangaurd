#pragma once

#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/game_input/mapping_resource.hpp>

namespace vanguard::engine
{
    inline constexpr FrameParticipantId GameInputFrameParticipantId = 0x67616d65696e6672ull;

    class GameInputService : public application::Service
    {
    public:
        ~GameInputService() override = default;

        [[nodiscard]] virtual game_input::ActionMap& GetMappings() noexcept = 0;
        [[nodiscard]] virtual const game_input::ActionMap& GetMappings() const noexcept = 0;
        /// Atomically replaces the current map with a validated cooked mapping.
        [[nodiscard]] virtual game_input::MappingResult Install(const game_input::MappingFile& mapping) noexcept = 0;
        /// Replaces the map with an empty compiled map, including its listeners.
        /// Call after the owning world's components have detached.
        [[nodiscard]] virtual bool Clear() noexcept = 0;

    protected:
        GameInputService() noexcept = default;
    };

    [[nodiscard]] GameInputService* FindGameInputService(application::EngineHost& host) noexcept;
    [[nodiscard]] GameInputService* FindGameInputService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

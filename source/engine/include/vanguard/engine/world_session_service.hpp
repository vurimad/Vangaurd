#pragma once

#include <vanguard/engine/game_world_service.hpp>

namespace vanguard::engine
{
    enum class WorldSessionStatus : u8
    {
        Idle,
        LoadingWorld,
        Running,
        Stopping,
        Failed
    };

    enum class WorldSessionInputMode : u8
    {
        Unspecified,
        None,
        Mapping
    };

    enum class WorldSessionFailureCode : u8
    {
        None,
        InvalidState,
        InvalidRequest,
        InputRequestFailure,
        InputLoadFailure,
        InputInstallFailure,
        InputClearFailure,
        WorldRequestFailure,
        WorldLoadFailure,
        GameWorldStartFailure,
        GameWorldStopFailure
    };

    struct WorldSessionFailure
    {
        WorldSessionFailureCode code = WorldSessionFailureCode::None;
        resources::Failure resourceFailure = resources::Failure::None;
        const char* message = nullptr;
    };

    struct WorldSessionStartRequest
    {
        resources::ResourceReference world;
        resources::ResourceReference input;
        WorldSessionInputMode inputMode = WorldSessionInputMode::Unspecified;
    };

    /// Owns the world/game-world/input transaction. Sources are already available
    /// through ResourceStreamingService, independently of package or loose storage.
    /// Stop releases the session input after component teardown and returns to Idle.
    class WorldSessionService : public application::Service
    {
    public:
        ~WorldSessionService() override = default;

        [[nodiscard]] virtual bool Begin(const WorldSessionStartRequest& request, WorldSessionFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual WorldSessionStatus Poll(WorldSessionFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool RequestStop(WorldSessionFailure* failure = nullptr) noexcept = 0;

        [[nodiscard]] virtual WorldSessionStatus GetStatus() const noexcept = 0;
        [[nodiscard]] virtual const WorldSessionFailure& GetLastFailure() const noexcept = 0;

    protected:
        WorldSessionService() noexcept = default;
    };

    [[nodiscard]] WorldSessionService* FindWorldSessionService(application::EngineHost& host) noexcept;
    [[nodiscard]] WorldSessionService* FindWorldSessionService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

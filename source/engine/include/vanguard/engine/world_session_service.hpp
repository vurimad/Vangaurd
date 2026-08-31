#pragma once

#include <vanguard/engine/game_world_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::engine
{
    enum class WorldSessionStatus : u8
    {
        Idle,
        Mounted,
        LoadingWorld,
        Running,
        Stopping,
        Failed
    };

    enum class WorldSessionStopMode : u8
    {
        ReleaseWorld,
        ReleaseEverything
    };

    enum class WorldSessionFailureCode : u8
    {
        None,
        InvalidState,
        InvalidRequest,
        PackageMountFailure,
        InputRequestFailure,
        InputLoadFailure,
        InputInstallFailure,
        WorldRequestFailure,
        WorldLoadFailure,
        GameWorldStartFailure,
        GameWorldStopFailure,
        PackageUnmountFailure
    };

    struct WorldSessionFailure
    {
        WorldSessionFailureCode code = WorldSessionFailureCode::None;
        streaming::PackageSetMountResult packageResult = streaming::PackageSetMountResult::Success;
        resources::Failure resourceFailure = resources::Failure::None;
        const char* message = nullptr;
    };

    struct WorldSessionStartRequest
    {
        filesystem::AbsolutePath gameDirectory;
        resources::ResourceReference world;
        streaming::PackageSetMountConfig packageConfig;
        bool mountPackages = true;
    };

    /// Owns the active package/world/game-world transaction, not the engine-wide services that execute it.
    /// ReleaseWorld retains the mounted package set for another Begin request; ReleaseEverything returns to Idle.
    class WorldSessionService : public application::Service
    {
    public:
        ~WorldSessionService() override = default;

        [[nodiscard]] virtual bool Begin(const WorldSessionStartRequest& request, WorldSessionFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual WorldSessionStatus Poll(WorldSessionFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool RequestStop(WorldSessionStopMode mode, WorldSessionFailure* failure = nullptr) noexcept = 0;

        [[nodiscard]] virtual WorldSessionStatus GetStatus() const noexcept = 0;
        [[nodiscard]] virtual const WorldSessionFailure& GetLastFailure() const noexcept = 0;

    protected:
        WorldSessionService() noexcept = default;
    };

    [[nodiscard]] WorldSessionService* FindWorldSessionService(application::EngineHost& host) noexcept;
    [[nodiscard]] WorldSessionService* FindWorldSessionService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

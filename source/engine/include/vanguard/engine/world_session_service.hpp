#pragma once

#include <vanguard/engine/game_world_service.hpp>

namespace vanguard::engine
{
    /// BeginFrame participant that owns the selected camera's streaming observer.
    /// It runs before the Streaming Observer PreSimulation snapshot so frame N
    /// streaming uses the camera pose completed at the end of frame N-1.
    inline constexpr FrameParticipantId WorldSessionCameraObserverParticipantId = 0x77736b6372616d6full;

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

    enum class WorldSessionCameraSource : u8
    {
        Unspecified,
        None,
        WorldDefault
    };

    enum class WorldSessionCameraBindingStatus : u8
    {
        None,
        Pending,
        Ready,
        Failed
    };

    enum class WorldSessionCameraBindingSource : u8
    {
        None,
        WorldDefault,
        Live
    };

    enum class WorldSessionCameraBindingFailure : u8
    {
        None,
        RequiredWorldCameraUnavailable,
        LiveCameraUnavailable,
        WrongScene
    };

    struct WorldSessionCameraSelectionToken
    {
        u64 worldGeneration = 0;
        u64 selectionGeneration = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return worldGeneration != 0 && selectionGeneration != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const WorldSessionCameraSelectionToken&,
                                                       const WorldSessionCameraSelectionToken&) noexcept = default;
    };

    struct WorldSessionCameraBinding
    {
        WorldSessionCameraBindingStatus status = WorldSessionCameraBindingStatus::None;
        WorldSessionCameraBindingSource source = WorldSessionCameraBindingSource::None;
        WorldSessionCameraBindingFailure failure = WorldSessionCameraBindingFailure::None;
        WorldCameraResolutionFailure worldFailure = WorldCameraResolutionFailure::None;
        u64 worldGeneration = 0;
        u64 selectionGeneration = 0;
        rendering::RenderCameraHandle camera;
        WorldCameraPose pose;
        bool hasPrecisePose = false;

        [[nodiscard]] constexpr WorldSessionCameraSelectionToken GetSelectionToken() const noexcept
        {
            return source != WorldSessionCameraBindingSource::None
                       ? WorldSessionCameraSelectionToken{worldGeneration, selectionGeneration}
                       : WorldSessionCameraSelectionToken{};
        }
    };

    enum class WorldSessionFailureCode : u8
    {
        None,
        InvalidState,
        InvalidRequest,
        InputRequestFailure,
        InputCancelFailure,
        InputLoadFailure,
        InputInstallFailure,
        InputClearFailure,
        WorldRequestFailure,
        WorldLoadFailure,
        GameWorldStartFailure,
        RequiredCameraFailure,
        GameWorldStopFailure,
        CameraObserverFailure,
        WorldReleaseFailure
    };

    struct WorldSessionFailure
    {
        WorldSessionFailureCode code = WorldSessionFailureCode::None;
        resources::Failure resourceFailure = resources::Failure::None;
        const char* message = nullptr;
        GameWorldStopFailure gameWorldStopFailure;
    };

    struct WorldSessionStartRequest
    {
        resources::ResourceReference world;
        resources::ResourceReference input;
        WorldSessionInputMode inputMode = WorldSessionInputMode::Unspecified;
        WorldSessionCameraSource cameraSource = WorldSessionCameraSource::Unspecified;
    };

    /// Owns the world/game-world/input transaction. Sources are already available
    /// through ResourceStreamingService, independently of package or loose storage.
    /// A clean stop releases session ownership and returns to Idle. A latched terminal
    /// fault releases all safely releasable ownership first, then remains Failed.
    class WorldSessionService : public application::Service
    {
    public:
        ~WorldSessionService() override = default;

        [[nodiscard]] virtual bool Begin(const WorldSessionStartRequest& request, WorldSessionFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual WorldSessionStatus Poll(WorldSessionFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool RequestStop(WorldSessionFailure* failure = nullptr) noexcept = 0;

        /// Replaces the current session selection with the world's authored default.
        /// The locator remains owned by WorldResource and is re-read from that owner.
        [[nodiscard]] virtual bool SelectWorldDefaultCamera(WorldSessionCameraSelectionToken* selection = nullptr,
                                                            WorldSessionFailure* failure = nullptr) noexcept = 0;
        /// Borrows a caller-owned live camera in the current world scene. The caller
        /// retains renderer-camera ownership and uses the returned generation token
        /// for later pose updates or revocation.
        [[nodiscard]] virtual bool SelectLiveCamera(rendering::RenderCameraHandle camera, const WorldCameraPose& pose,
                                                    WorldSessionCameraSelectionToken* selection = nullptr,
                                                    WorldSessionFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool UpdateLiveCameraPose(WorldSessionCameraSelectionToken selection, const WorldCameraPose& pose,
                                                        WorldSessionFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool ClearCameraSelection(WorldSessionCameraSelectionToken selection,
                                                        WorldSessionFailure* failure = nullptr) noexcept = 0;

        [[nodiscard]] virtual WorldSessionStatus GetStatus() const noexcept = 0;
        [[nodiscard]] virtual const WorldSessionFailure& GetLastFailure() const noexcept = 0;
        /// Returns an explicit selected root candidate. Reading the binding creates
        /// no view, viewport or output region; the session `BeginFrame` participant
        /// owns at most one streaming observer for this selection.
        [[nodiscard]] virtual WorldSessionCameraBinding GetCameraBinding() const noexcept = 0;

    protected:
        WorldSessionService() noexcept = default;
    };

    [[nodiscard]] WorldSessionService* FindWorldSessionService(application::EngineHost& host) noexcept;
    [[nodiscard]] WorldSessionService* FindWorldSessionService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

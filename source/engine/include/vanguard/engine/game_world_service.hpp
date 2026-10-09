#pragma once

#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/entities/cell_streaming_system.hpp>
#include <vanguard/entities/component_runtime.hpp>
#include <vanguard/entities/rendering_runtime.hpp>
#include <vanguard/entities/transform_runtime.hpp>
#include <vanguard/rendering/render_camera.hpp>

#include <redMathPublic.h>

namespace vanguard::engine
{
    inline constexpr FrameParticipantId GameWorldFrameParticipantId = 0x67616d65776f7266ull;
    inline constexpr FrameParticipantId GameWorldTransformCompletionParticipantId = 0x67616d657874666eull;

    enum class GameWorldStatus : u8
    {
        Idle,
        Running,
        Draining,
        Failed
    };

    enum class GameWorldStopResult : u8
    {
        Complete,
        Pending,
        Failure
    };

    enum class GameWorldStopFailureCode : u8
    {
        None,
        Transform,
        Streaming,
        Component,
        RendererProgress,
        SceneRelease,
        WorldShutdown
    };

    struct GameWorldStopFailure
    {
        GameWorldStopFailureCode code = GameWorldStopFailureCode::None;
        entities::TransformRuntimeFailure transformFailure;
        entities::CellStreamingReleaseFailure streamingFailure;
        entities::DistantProxyStreamingFailure distantProxyFailure;
        RenderingProgressFailure renderingFailure;
        rendering::RenderSceneFailure sceneFailure;
        const char* message = nullptr;
    };

    enum class WorldCameraResolutionStatus : u8
    {
        None,
        Pending,
        Ready,
        Failed
    };

    enum class WorldCameraResolutionFailure : u8
    {
        None,
        CellFailed,
        CellUnavailable,
        MissingEntity,
        MissingComponent,
        WrongComponentType,
        CameraUnavailable
    };

    struct WorldCameraResolution
    {
        WorldCameraResolutionStatus status = WorldCameraResolutionStatus::None;
        WorldCameraResolutionFailure failure = WorldCameraResolutionFailure::None;
        world::WorldCameraLocator locator;
        entities::ComponentHandle component;
        rendering::RenderCameraHandle camera;
    };

    struct WorldCameraPose
    {
        math::WorldPosition position;
        math::Quaternion orientation;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return position.IsOk() && orientation.IsOk();
        }
    };

    /// One world-scoped named-camera resolution transaction. It retains only
    /// durable identity plus generational runtime handles; never a component pointer.
    class WorldCameraResolver final
    {
    public:
        [[nodiscard]] bool Begin(world::WorldCameraLocator locator) noexcept;
        void Revoke() noexcept;
        void Reset() noexcept;
        [[nodiscard]] bool Update(const world::WorldStreamingGrid& grid, entities::CellMaterializer& materializer,
                                  entities::EntityReferenceRegistry& references, entities::ComponentDirectory& components,
                                  const rendering::RenderCameraStorage& cameras) noexcept;
        [[nodiscard]] const WorldCameraResolution& GetResolution() const noexcept;

    private:
        [[nodiscard]] bool Fail(WorldCameraResolutionFailure failure) noexcept;
        WorldCameraResolution m_resolution;
    };

    struct GameWorldServiceConfig
    {
        entities::RegisterWorldComponents registerComponents = nullptr;
        /// Optional observer invoked after RenderingRuntime has consumed a non-cell event.
        /// It must not acknowledge release or otherwise assume ownership of the streamed resource.
        entities::ForwardStreamingEvent forwardNonCellEvent = nullptr;
        void* userData = nullptr;
        entities::MaterializationConfig materialization;
        entities::ComponentDirectoryConfig components;
        entities::TransformRuntimeConfig transforms;
        entities::RenderingRuntimeConfig rendering;
        game::GameWorldConfig world;
    };

    /// Managed owner of one Flecs-backed game world and its entity runtime systems.
    /// Configuration is accepted only while idle and remains caller-owned only through callback invocations.
    class GameWorldService : public application::Service
    {
    public:
        ~GameWorldService() override = default;

        [[nodiscard]] virtual bool Configure(const GameWorldServiceConfig& config) noexcept = 0;
        [[nodiscard]] virtual bool BeginWorld(world::WorldCameraLocator startupCamera = {}) noexcept = 0;
        /// Replaces only the named world-camera resolution transaction. An empty
        /// locator disables world-camera resolution without changing the world.
        [[nodiscard]] virtual bool SetWorldCameraLocator(world::WorldCameraLocator camera) noexcept = 0;
        [[nodiscard]] virtual bool SetStreamingInput(const world::StreamingProcessInput& input) noexcept = 0;
        [[nodiscard]] virtual bool Tick(f32 deltaSeconds) noexcept = 0;
        [[nodiscard]] virtual GameWorldStopResult StopWorld(GameWorldStopFailure* failure = nullptr) noexcept = 0;

        [[nodiscard]] virtual GameWorldStatus GetStatus() const noexcept = 0;
        [[nodiscard]] virtual game::GameWorld* GetWorld() noexcept = 0;
        [[nodiscard]] virtual entities::CellStreamingSystem* GetCellStreaming() noexcept = 0;
        [[nodiscard]] virtual entities::ComponentDirectory* GetComponents() noexcept = 0;
        [[nodiscard]] virtual entities::TransformRuntime* GetTransforms() noexcept = 0;
        [[nodiscard]] virtual entities::RenderingRuntime* GetRendering() noexcept = 0;
        [[nodiscard]] virtual const WorldCameraResolution& GetWorldCameraResolution() const noexcept = 0;
        /// Consumer-boundary validation of the resolved component/camera generations.
        /// The returned pose is a transient value, not camera-state authority.
        [[nodiscard]] virtual bool GetWorldCameraPose(WorldCameraPose& pose) const noexcept = 0;

    protected:
        GameWorldService() noexcept = default;
    };

    [[nodiscard]] GameWorldService* FindGameWorldService(application::EngineHost& host) noexcept;
    [[nodiscard]] GameWorldService* FindGameWorldService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

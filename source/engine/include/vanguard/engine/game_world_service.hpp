#pragma once

#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/entities/cell_streaming_system.hpp>
#include <vanguard/entities/component_runtime.hpp>
#include <vanguard/entities/rendering_runtime.hpp>
#include <vanguard/entities/transform_runtime.hpp>

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
        [[nodiscard]] virtual bool BeginWorld() noexcept = 0;
        [[nodiscard]] virtual bool SetStreamingInput(const world::StreamingProcessInput& input) noexcept = 0;
        [[nodiscard]] virtual bool Tick(f32 deltaSeconds) noexcept = 0;
        [[nodiscard]] virtual GameWorldStopResult StopWorld() noexcept = 0;

        [[nodiscard]] virtual GameWorldStatus GetStatus() const noexcept = 0;
        [[nodiscard]] virtual game::GameWorld* GetWorld() noexcept = 0;
        [[nodiscard]] virtual entities::CellStreamingSystem* GetCellStreaming() noexcept = 0;
        [[nodiscard]] virtual entities::ComponentDirectory* GetComponents() noexcept = 0;
        [[nodiscard]] virtual entities::TransformRuntime* GetTransforms() noexcept = 0;
        [[nodiscard]] virtual entities::RenderingRuntime* GetRendering() noexcept = 0;

    protected:
        GameWorldService() noexcept = default;
    };

    [[nodiscard]] GameWorldService* FindGameWorldService(application::EngineHost& host) noexcept;
    [[nodiscard]] GameWorldService* FindGameWorldService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

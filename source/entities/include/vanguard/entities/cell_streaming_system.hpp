#pragma once

#include <vanguard/entities/materializer.hpp>
#include <vanguard/game_world/game_world.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/world/streaming_executor.hpp>

namespace vanguard::entities
{
    inline constexpr game::RuntimeSystemId CellStreamingSystemId = 64;

    using RegisterWorldComponents = bool (*)(ComponentRegistry& registry, void* userData) noexcept;
    using ForwardStreamingEvent = void (*)(const world::StreamingResourceEvent& event, void* userData) noexcept;

    struct CellDecoderContext
    {
        world::ReadLimits limits;
    };

    [[nodiscard]] streaming::DecoderDescriptor MakeCellDecoder(CellDecoderContext& context) noexcept;

    struct CellStreamingSystemConfig
    {
        world::WorldStreamingExecutor* executor = nullptr;
        RegisterWorldComponents registerComponents = nullptr;
        PrefabResolver resolvePrefab = nullptr;
        ForwardStreamingEvent forwardNonCellEvent = nullptr;
        void* userData = nullptr;
        MaterializationConfig materialization;
    };

    struct CellStreamingSystemStats
    {
        u32 trackedCells = 0;
        u32 awaitingActivation = 0;
        u32 activeCells = 0;
        u32 awaitingRelease = 0;
        u32 failedCells = 0;
        u64 queuedCells = 0;
        u64 activatedCells = 0;
        u64 releasedCells = 0;
        u64 cancelledCells = 0;
        u64 downstreamFailures = 0;
    };

    /// Cell-side streaming runtime system. Resource completion queues one materialization transaction;
    /// the post-world-flush epilogue publishes readiness and acknowledges release only after Flecs commits.
    class CellStreamingSystem final : public game::RuntimeSystem
    {
    public:
        struct Impl;

        explicit CellStreamingSystem(const CellStreamingSystemConfig& config) noexcept;
        ~CellStreamingSystem() override;

        CellStreamingSystem(const CellStreamingSystem&) = delete;
        CellStreamingSystem& operator=(const CellStreamingSystem&) = delete;

        [[nodiscard]] bool SetProcessInput(const world::StreamingProcessInput& input) noexcept;
        [[nodiscard]] Result AcquireActivationGroup(u64 cellId, u64 groupId, ActivationOwnerId ownerId) noexcept;
        [[nodiscard]] Result ReleaseActivationGroup(u64 cellId, u64 groupId, ActivationOwnerId ownerId) noexcept;
        [[nodiscard]] ComponentRegistry* Components() noexcept;
        [[nodiscard]] EntityReferenceRegistry* References() noexcept;
        [[nodiscard]] CellMaterializer* Materializer() noexcept;
        [[nodiscard]] CellStreamingSystemStats GetStats() const noexcept;

    protected:
        [[nodiscard]] bool OnInitialize(game::GameWorld& world) noexcept override;
        void OnUninitialize(game::GameWorld& world) noexcept override;
        void OnBeginFrame(game::GameWorld& world, f32 deltaSeconds) noexcept override;
        void OnAfterWorldFlush(game::GameWorld& world) noexcept override;
        [[nodiscard]] const char* ReadinessBlocker() const noexcept override;

    private:
        CellStreamingSystemConfig m_config;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::entities

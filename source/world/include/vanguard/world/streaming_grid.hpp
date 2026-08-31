#pragma once

#include <vanguard/world/worlds.hpp>

namespace vanguard::world
{
    inline constexpr u32 MaximumStreamingObservers = 16;

    enum class StreamingNodeKind : u8
    {
        Cell,
        DistantProxy
    };

    enum class StreamingNodeState : u8
    {
        Unloaded,
        StreamingIn,
        Streamed,
        Failed,
        StreamingOut
    };

    enum class StreamingCommandType : u8
    {
        StreamIn,
        StreamOut
    };

    struct StreamingNodeKey
    {
        u64 id = 0;
        StreamingNodeKind kind = StreamingNodeKind::Cell;

        [[nodiscard]] friend constexpr bool operator==(const StreamingNodeKey&, const StreamingNodeKey&) noexcept = default;
    };

    struct StreamingObserver
    {
        /// Global position predicted from the observer's current velocity.
        f64 predictedPosition[3]{};
    };

    struct StreamingProcessInput
    {
        containers::ArraySpan<const StreamingObserver> observers;
        /// Render-camera position used by secondary-reference and near-auto-hide queries.
        f64 cameraPosition[3]{};
        f32 globalDistanceScale = 1.0f;
    };

    struct StreamingCommand
    {
        StreamingCommandType type = StreamingCommandType::StreamIn;
        StreamingNodeKey key;
        resources::ResourceReference resource;
        StreamingPriority priority = StreamingPriority::Normal;
        f32 distanceSquared = 0.0f;
    };

    struct StreamingGridConfig
    {
        u32 maximumStreamInsPerUpdate = 1024;
        f32 runtimeDistanceBoost = 0.0f;
    };

    struct StreamingGridStats
    {
        u32 registeredNodes = 0;
        u32 desiredNodes = 0;
        u32 inRangeNodes = 0;
        u32 streamingNodes = 0;
        u32 streamedNodes = 0;
        u32 failedNodes = 0;
        u32 antiStreamingLockedProxies = 0;
    };

    /// Mask-based world streaming selector. It owns logical proxy state but
    /// deliberately leaves resource loading and renderer/Flecs attachment to explicit commands.
    class WorldStreamingGrid final
    {
    public:
        struct Impl;

        WorldStreamingGrid() noexcept = default;
        ~WorldStreamingGrid();

        WorldStreamingGrid(const WorldStreamingGrid&) = delete;
        WorldStreamingGrid& operator=(const WorldStreamingGrid&) = delete;

        [[nodiscard]] bool Initialize(const WorldFile& world, const StreamingGridConfig& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool RequestShutdown() noexcept;
        [[nodiscard]] bool IsShutdownRequested() const noexcept;

        /// Produces explicit stream-in/out commands. Stream-in commands are sorted by descending
        /// priority and then ascending distance, and are capped by maximumStreamInsPerUpdate.
        [[nodiscard]] bool Process(const StreamingProcessInput& input, containers::DynamicArray<StreamingCommand>& commands) noexcept;

        [[nodiscard]] bool NotifyStreamInComplete(StreamingNodeKey key, bool success, bool renderReady = false) noexcept;
        [[nodiscard]] bool NotifyResidentFailed(StreamingNodeKey key) noexcept;
        [[nodiscard]] bool NotifyStreamOutComplete(StreamingNodeKey key) noexcept;
        [[nodiscard]] bool SetRenderReady(StreamingNodeKey key, bool ready) noexcept;
        [[nodiscard]] bool SetLocked(StreamingNodeKey key, bool locked) noexcept;
        [[nodiscard]] bool SetStreamInAllowed(StreamingNodeKey key, bool allowed) noexcept;

        [[nodiscard]] StreamingNodeState GetState(StreamingNodeKey key) const noexcept;
        [[nodiscard]] bool IsRenderReady(StreamingNodeKey key) const noexcept;
        [[nodiscard]] bool IsAntiStreamingLocked(u64 proxyId) const noexcept;
        [[nodiscard]] StreamingGridStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::world

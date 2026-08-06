#pragma once

#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/world/streaming_grid.hpp>

namespace vanguard::world
{
    enum class StreamingResourceEventType : u8
    {
        ResourceAvailable,
        ResourceFailed,
        ReleaseRequested,
        RequestCancelled
    };

    struct StreamingResourceEvent
    {
        StreamingResourceEventType type = StreamingResourceEventType::ResourceAvailable;
        StreamingNodeKey key;
        resources::ResourceReference resource;
        resources::Failure failure = resources::Failure::None;
    };

    struct StreamingExecutorStats
    {
        u32 knownNodes = 0;
        u32 requestingNodes = 0;
        u32 residentNodes = 0;
        u32 releasePendingNodes = 0;
        u32 failedNodes = 0;
        u64 submittedRequests = 0;
        u64 completedRequests = 0;
        u64 failedRequests = 0;
        u64 cancelledRequests = 0;
        u64 releasedResources = 0;
    };

    /// Executes streaming-grid commands through the shared resource pipeline. Resource availability
    /// and release remain explicit boundaries for the world materializer and renderer residency layers.
    class WorldStreamingExecutor final
    {
    public:
        struct Impl;

        WorldStreamingExecutor() noexcept = default;
        ~WorldStreamingExecutor();

        WorldStreamingExecutor(const WorldStreamingExecutor&) = delete;
        WorldStreamingExecutor& operator=(const WorldStreamingExecutor&) = delete;

        [[nodiscard]] bool Initialize(WorldStreamingGrid& grid, resources::ResourcePipeline& pipeline) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Polls completed requests, evaluates the grid, submits new requests and reports lifecycle events.
        /// This function never waits for I/O or Jobs work.
        [[nodiscard]] bool Process(const StreamingProcessInput& input,
                                   containers::DynamicArray<StreamingResourceEvent>& events) noexcept;

        /// Marks a resident resource as usable by its downstream consumer. Detailed cells should become
        /// ready after ECS activation; render-only proxies should become ready after renderer residency.
        [[nodiscard]] bool SetReady(StreamingNodeKey key, bool ready) noexcept;

        /// Completes a ReleaseRequested event after downstream state has detached from the resource.
        [[nodiscard]] bool CompleteRelease(StreamingNodeKey key) noexcept;
        [[nodiscard]] bool FailResident(StreamingNodeKey key, resources::Failure failure) noexcept;

        [[nodiscard]] const resources::ResourceHandle* Resource(StreamingNodeKey key) const noexcept;
        [[nodiscard]] resources::Failure LastFailure(StreamingNodeKey key) const noexcept;
        [[nodiscard]] bool GetFailureTrace(StreamingNodeKey key, resources::FailureTrace& trace) const noexcept;
        [[nodiscard]] StreamingExecutorStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::world

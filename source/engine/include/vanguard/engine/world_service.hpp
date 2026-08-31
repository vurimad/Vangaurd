#pragma once

#include <vanguard/engine/engine_services.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/world/streaming_executor.hpp>

namespace vanguard::engine
{
    enum class WorldResourceStatus : u8
    {
        Idle,
        Loading,
        Ready,
        Failed
    };

    /// Owns one active world resource and its package-backed streaming machinery.
    /// State-machine code coordinates this service but never owns resource requests or handles.
    class WorldService : public application::Service
    {
    public:
        ~WorldService() override = default;

        [[nodiscard]] virtual bool BeginWorld(resources::ResourceReference reference) noexcept = 0;
        [[nodiscard]] virtual WorldResourceStatus PollWorld() noexcept = 0;
        [[nodiscard]] virtual bool CancelWorld() noexcept = 0;
        [[nodiscard]] virtual bool ReleaseWorld() noexcept = 0;

        [[nodiscard]] virtual WorldResourceStatus GetStatus() const noexcept = 0;
        [[nodiscard]] virtual resources::Failure GetLastFailure() const noexcept = 0;
        [[nodiscard]] virtual const world::WorldResource* GetResource() const noexcept = 0;
        [[nodiscard]] virtual world::WorldStreamingGrid* GetGrid() noexcept = 0;
        [[nodiscard]] virtual world::WorldStreamingExecutor* GetExecutor() noexcept = 0;

    protected:
        WorldService() noexcept = default;
    };

    [[nodiscard]] WorldService* FindWorldService(application::EngineHost& host) noexcept;
    [[nodiscard]] WorldService* FindWorldService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

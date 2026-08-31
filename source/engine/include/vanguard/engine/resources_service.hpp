#pragma once

#include <vanguard/engine/engine_services.hpp>
#include <vanguard/resources/resource_pipeline.hpp>

namespace vanguard::engine
{
    /// Typed access boundary for the process-owned resource registry and asynchronous pipeline.
    /// Callers do not own either object and must release every request and handle before shutdown.
    class ResourcesService : public application::Service
    {
    public:
        ~ResourcesService() override = default;

        [[nodiscard]] virtual resources::ResourceRegistry& GetRegistry() noexcept = 0;
        [[nodiscard]] virtual const resources::ResourceRegistry& GetRegistry() const noexcept = 0;
        [[nodiscard]] virtual resources::ResourcePipeline& GetPipeline() noexcept = 0;
        [[nodiscard]] virtual const resources::ResourcePipeline& GetPipeline() const noexcept = 0;

    protected:
        ResourcesService() noexcept = default;
    };

    [[nodiscard]] ResourcesService* FindResourcesService(application::EngineHost& host) noexcept;
    [[nodiscard]] ResourcesService* FindResourcesService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

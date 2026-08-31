#pragma once

#include <vanguard/engine/engine_services.hpp>
#include <vanguard/streaming/streaming.hpp>

namespace vanguard::engine
{
    /// Typed access boundary for resource decoders, loose sources, the owned runtime package set, individual VPAK mounts, and requests.
    /// Individual package readers and decoder callback state remain caller-owned until unregistered.
    class ResourceStreamingService : public application::Service
    {
    public:
        ~ResourceStreamingService() override = default;

        [[nodiscard]] virtual streaming::ResourceStreamer& GetStreamer() noexcept = 0;
        [[nodiscard]] virtual const streaming::ResourceStreamer& GetStreamer() const noexcept = 0;
        [[nodiscard]] virtual streaming::PackageSetMount& GetPackageSet() noexcept = 0;
        [[nodiscard]] virtual const streaming::PackageSetMount& GetPackageSet() const noexcept = 0;

    protected:
        ResourceStreamingService() noexcept = default;
    };

    [[nodiscard]] ResourceStreamingService* FindResourceStreamingService(application::EngineHost& host) noexcept;
    [[nodiscard]] ResourceStreamingService* FindResourceStreamingService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine

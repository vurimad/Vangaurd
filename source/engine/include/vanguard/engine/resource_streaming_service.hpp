#pragma once

#include <vanguard/engine/engine_services.hpp>
#include <vanguard/streaming/streaming.hpp>

namespace vanguard::engine
{
    struct ResourceStreamingServiceConfig
    {
        // Empty means no package source. A supplied root is required to mount
        // successfully; optional fallback is a composition decision, not probing.
        filesystem::AbsolutePath packageDirectory;
        streaming::PackageSetMountConfig packages;
        // Committed artifact locations and runtime dependencies supplied by tools.
        // Backing arrays must survive service initialization. The streamer copies
        // descriptors; files must stay immutable while any source reader uses them.
        containers::ArraySpan<const streaming::LooseResourceDescriptor> looseResources;
    };

    [[nodiscard]] bool RegisterResourceStreamingService(application::EngineHost& host, const ResourceStreamingServiceConfig& config,
                                                        application::HostFailure* failure = nullptr) noexcept;

    /// Typed access boundary for resource decoders, loose sources, the owned runtime package set, individual VPAK mounts, and requests.
    /// The service owns the core artifact decoder callback state. Individual package readers and any externally registered
    /// decoder callback state remain caller-owned until unregistered. Configured
    /// sources are established before dependent services initialize and released
    /// during shutdown after those services. World sessions never unmount them.
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

#include <vanguard/engine/resource_streaming_service.hpp>

#include <vanguard/engine/resources_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/pipelines/renderer_catalog.hpp>
#include <vanguard/shaders/shaders.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    class ManagedResourceStreamingService final : public vanguard::engine::ResourceStreamingService
    {
    public:
        explicit ManagedResourceStreamingService(const vanguard::engine::ResourceStreamingServiceConfig& config) noexcept
            : m_config(config)
        {
        }

        [[nodiscard]] vanguard::streaming::ResourceStreamer& GetStreamer() noexcept override
        {
            return m_streamer;
        }
        [[nodiscard]] const vanguard::streaming::ResourceStreamer& GetStreamer() const noexcept override
        {
            return m_streamer;
        }
        [[nodiscard]] vanguard::streaming::PackageSetMount& GetPackageSet() noexcept override
        {
            return m_packageSet;
        }
        [[nodiscard]] const vanguard::streaming::PackageSetMount& GetPackageSet() const noexcept override
        {
            return m_packageSet;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            vanguard::engine::ResourcesService* const resources = vanguard::engine::FindResourcesService(context);
            if (resources == nullptr || !resources->GetPipeline().IsInitialized() || !vanguard::filesystem::IsInitialized() || !vanguard::io::IsInitialized() ||
                !vanguard::jobs::IsInitialized())
                return app::LifecycleStatus::Failure("Resource Streaming dependencies are not running");
            if (m_streamer.IsInitialized())
                return app::LifecycleStatus::Failure("Resource Streaming was initialized outside its managed lifecycle");
            if (!m_streamer.Initialize(resources->GetPipeline()))
                return app::LifecycleStatus::Failure("Resource Streaming initialization failed");
            if (!m_meshLoader.Initialize(m_streamer, resources->GetPipeline()))
            {
                static_cast<void>(m_streamer.Shutdown());
                return app::LifecycleStatus::Failure("Mesh metadata loader registration failed");
            }
            if (!m_textureLoader.Initialize(m_streamer, resources->GetPipeline()))
            {
                static_cast<void>(m_meshLoader.Shutdown());
                static_cast<void>(m_streamer.Shutdown());
                return app::LifecycleStatus::Failure("Texture metadata loader registration failed");
            }
            if (!m_streamer.RegisterDecoder({vanguard::shaders::ShaderResourceType, "Vanguard shader artifact",
                                             &vanguard::shaders::DecodeShaderResource, &vanguard::shaders::DestroyShaderResource,
                                             &m_shaderDecoderConfig}))
            {
                static_cast<void>(m_textureLoader.Shutdown());
                static_cast<void>(m_meshLoader.Shutdown());
                static_cast<void>(m_streamer.Shutdown());
                return app::LifecycleStatus::Failure("Shader artifact decoder registration failed");
            }
            m_shaderDecoderRegistered = true;
            if (!m_streamer.RegisterDecoder({vanguard::pipelines::PipelineResourceType, "Vanguard pipeline artifact",
                                             &vanguard::pipelines::DecodePipelineResource, &vanguard::pipelines::DestroyPipelineResource,
                                             &m_pipelineDecoderConfig}))
            {
                static_cast<void>(m_streamer.UnregisterDecoder(vanguard::shaders::ShaderResourceType));
                m_shaderDecoderRegistered = false;
                static_cast<void>(m_textureLoader.Shutdown());
                static_cast<void>(m_meshLoader.Shutdown());
                static_cast<void>(m_streamer.Shutdown());
                return app::LifecycleStatus::Failure("Pipeline artifact decoder registration failed");
            }
            m_pipelineDecoderRegistered = true;
            if (!m_streamer.RegisterDecoder({vanguard::materials::MaterialResourceType, "Vanguard material artifact",
                                             &vanguard::materials::DecodeMaterialResource, &vanguard::materials::DestroyMaterialResource,
                                             &m_materialDecoderConfig}))
            {
                static_cast<void>(m_streamer.UnregisterDecoder(vanguard::pipelines::PipelineResourceType));
                static_cast<void>(m_streamer.UnregisterDecoder(vanguard::shaders::ShaderResourceType));
                m_pipelineDecoderRegistered = false;
                m_shaderDecoderRegistered = false;
                static_cast<void>(m_textureLoader.Shutdown());
                static_cast<void>(m_meshLoader.Shutdown());
                static_cast<void>(m_streamer.Shutdown());
                return app::LifecycleStatus::Failure("Material artifact decoder registration failed");
            }
            m_materialDecoderRegistered = true;
            const bool catalogRegistered = m_streamer.RegisterDecoder({vanguard::pipelines::RendererCatalogResourceType, "Renderer bootstrap catalog",
                &vanguard::pipelines::DecodeRendererCatalog, &vanguard::pipelines::DestroyRendererCatalog, nullptr});
            if (!catalogRegistered)
                return app::LifecycleStatus::Failure("Renderer catalog decoder registration failed");
            m_catalogDecoderRegistered = true;
            if (!m_config.packageDirectory.Empty())
            {
                const vanguard::streaming::PackageSetMountResult mounted =
                    m_packageSet.Mount(m_streamer, m_config.packageDirectory, m_config.packages);
                if (mounted != vanguard::streaming::PackageSetMountResult::Success)
                    return app::LifecycleStatus::Failure(vanguard::streaming::ToString(mounted));
            }
            for (const vanguard::streaming::LooseResourceDescriptor& resource : m_config.looseResources)
            {
                const bool registered = m_streamer.RegisterLoose(resource);
                if (!registered)
                    return app::LifecycleStatus::Failure("Committed loose resource registration failed");
            }
            // No consumer has started yet. EngineHost invokes OnShutdown even
            // after failed initialization, so partial source setup unwinds here
            // through the same owner as normal process shutdown.
            m_config.looseResources = {};
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext& context) noexcept override
        {
            vanguard::engine::ResourcesService* const resources = vanguard::engine::FindResourcesService(context);
            return m_streamer.IsInitialized() && m_meshLoader.IsInitialized() && m_textureLoader.IsInitialized() &&
                           m_shaderDecoderRegistered && m_pipelineDecoderRegistered && m_materialDecoderRegistered && m_catalogDecoderRegistered && resources != nullptr &&
                           resources->GetPipeline().HasLoader(vanguard::shaders::ShaderResourceType) &&
                           resources->GetPipeline().HasLoader(vanguard::pipelines::PipelineResourceType) &&
                           resources->GetPipeline().HasLoader(vanguard::materials::MaterialResourceType)
                       ? app::LifecycleStatus::Success()
                       : app::LifecycleStatus::Failure("Resource Streaming did not enter a running state");
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            const vanguard::streaming::Stats stats = m_streamer.GetStats();
            return stats.activeLoads == 0 && stats.activeReads == 0 ? app::LifecycleStatus::Success()
                                                                    : app::LifecycleStatus::Failure("Resource Streaming still has active loads or I/O reads");
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            const vanguard::streaming::Stats stats = m_streamer.GetStats();
            return stats.activeLoads == 0 && stats.activeReads == 0 && stats.stagingBytesInUse == 0
                       ? app::LifecycleStatus::Success()
                       : app::LifecycleStatus::Failure("Resource Streaming did not drain all loads, reads, and staging memory");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            const vanguard::streaming::Stats stats = m_streamer.GetStats();
            if (stats.activeLoads != 0 || stats.activeReads != 0 || stats.stagingBytesInUse != 0)
                return app::LifecycleStatus::Failure("Resource Streaming shutdown was blocked by active loads, reads, or staging memory");
            if (m_packageSet.IsMounted())
            {
                const vanguard::streaming::PackageSetMountResult unmounted = m_packageSet.Unmount();
                if (unmounted != vanguard::streaming::PackageSetMountResult::Success)
                    return app::LifecycleStatus::Failure(vanguard::streaming::ToString(unmounted));
            }
            if (m_catalogDecoderRegistered)
            {
                const bool removed = m_streamer.UnregisterDecoder(vanguard::pipelines::RendererCatalogResourceType);
                if (!removed)
                    return app::LifecycleStatus::Failure("Renderer catalog decoder still has live loads");
                m_catalogDecoderRegistered = false;
            }
            if (m_materialDecoderRegistered && !m_streamer.UnregisterDecoder(vanguard::materials::MaterialResourceType))
                return app::LifecycleStatus::Failure("Material artifact decoder shutdown was blocked by live loads");
            m_materialDecoderRegistered = false;
            if (m_pipelineDecoderRegistered && !m_streamer.UnregisterDecoder(vanguard::pipelines::PipelineResourceType))
                return app::LifecycleStatus::Failure("Pipeline artifact decoder shutdown was blocked by live loads");
            m_pipelineDecoderRegistered = false;
            if (m_shaderDecoderRegistered && !m_streamer.UnregisterDecoder(vanguard::shaders::ShaderResourceType))
                return app::LifecycleStatus::Failure("Shader artifact decoder shutdown was blocked by live loads");
            m_shaderDecoderRegistered = false;
            if (!m_textureLoader.Shutdown())
                return app::LifecycleStatus::Failure("Texture metadata loader shutdown was blocked by live loads");
            if (!m_meshLoader.Shutdown())
                return app::LifecycleStatus::Failure("Mesh metadata loader shutdown was blocked by live loads");
            return m_streamer.Shutdown() ? app::LifecycleStatus::Success()
                                         : app::LifecycleStatus::Failure("Resource Streaming shutdown was blocked by live state");
        }

    private:
        vanguard::engine::ResourceStreamingServiceConfig m_config;
        vanguard::streaming::PackageSetMount m_packageSet;
        vanguard::streaming::ResourceStreamer m_streamer;
        vanguard::meshes::MeshResourceLoader m_meshLoader;
        vanguard::textures::TextureResourceLoader m_textureLoader;
        vanguard::shaders::ShaderResourceDecoderConfig m_shaderDecoderConfig;
        vanguard::pipelines::PipelineResourceDecoderConfig m_pipelineDecoderConfig;
        vanguard::materials::MaterialResourceDecoderConfig m_materialDecoderConfig;
        bool m_shaderDecoderRegistered = false;
        bool m_pipelineDecoderRegistered = false;
        bool m_materialDecoderRegistered = false;
        bool m_catalogDecoderRegistered = false;
    };

    app::Service* CreateResourceStreamingService(void* const userData) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Streaming, sizeof(ManagedResourceStreamingService), alignof(ManagedResourceStreamingService));
        return block ? ::new (block.address) ManagedResourceStreamingService(
                           *static_cast<const vanguard::engine::ResourceStreamingServiceConfig*>(userData)) : nullptr;
    }

    void DestroyResourceStreamingService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedResourceStreamingService*>(service)->~ManagedResourceStreamingService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedResourceStreamingService), vanguard::memory::PoolId::Streaming};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::engine
{
    bool RegisterResourceStreamingService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        static const ResourceStreamingServiceConfig config;
        return RegisterResourceStreamingService(host, config, failure);
    }

    bool RegisterResourceStreamingService(application::EngineHost& host, const ResourceStreamingServiceConfig& config,
                                          application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{{ResourcesServiceId, application::DependencyKind::Required},
                                                                {FilesystemServiceId, application::DependencyKind::Required},
                                                                {IoServiceId, application::DependencyKind::Required},
                                                                {JobsServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId providedCapabilities[]{ResourceStreamingCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = ResourceStreamingServiceId;
        descriptor.name = "resourceStreaming";
        descriptor.profiles = application::ApplicationProfile::All;
        descriptor.scope = application::ServiceScope::Process;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 4};
        descriptor.provides = {providedCapabilities, 1};
        descriptor.create = CreateResourceStreamingService;
        descriptor.destroy = DestroyResourceStreamingService;
        descriptor.userData = const_cast<ResourceStreamingServiceConfig*>(&config);
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    ResourceStreamingService* FindResourceStreamingService(application::EngineHost& host) noexcept
    {
        return static_cast<ResourceStreamingService*>(host.FindCapability(ResourceStreamingCapabilityId));
    }

    ResourceStreamingService* FindResourceStreamingService(application::ServiceContext& context) noexcept
    {
        return static_cast<ResourceStreamingService*>(context.FindCapability(ResourceStreamingCapabilityId));
    }
} // namespace vanguard::engine

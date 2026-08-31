#include <vanguard/engine/rendering_service.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/frame_renderer.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace memory = vanguard::memory;
    namespace rendering = vanguard::rendering;
    namespace rhi = vanguard::rhi;

    [[nodiscard]] const char* FailureMessage(const char* const message, const char* const fallback) noexcept
    {
        return message != nullptr ? message : fallback;
    }

    class RenderingServiceImpl final : public engine::RenderingService
    {
    public:
        explicit RenderingServiceImpl(const engine::RenderingServiceConfig& config) noexcept
            : m_config(config), m_frameRenderer(m_cameras, m_gpuSceneRuntime)
        {
        }

        [[nodiscard]] rendering::RenderCommandSystem& GetCommands() noexcept override
        {
            return m_commands;
        }
        [[nodiscard]] const rendering::RenderCommandSystem& GetCommands() const noexcept override
        {
            return m_commands;
        }
        [[nodiscard]] rendering::ViewportManager& GetViewports() noexcept override
        {
            return m_viewports;
        }
        [[nodiscard]] const rendering::ViewportManager& GetViewports() const noexcept override
        {
            return m_viewports;
        }
        [[nodiscard]] rendering::RenderSceneManager& GetScenes() noexcept override
        {
            return m_scenes;
        }
        [[nodiscard]] const rendering::RenderSceneManager& GetScenes() const noexcept override
        {
            return m_scenes;
        }
        [[nodiscard]] const rendering::RenderCameraStorage& GetCameras() const noexcept override
        {
            return m_cameras;
        }
        [[nodiscard]] rendering::GpuSceneRuntime& GetGpuScene() noexcept override
        {
            return m_gpuSceneRuntime;
        }
        [[nodiscard]] const rendering::GpuSceneRuntime& GetGpuScene() const noexcept override
        {
            return m_gpuSceneRuntime;
        }
        [[nodiscard]] rendering::MeshResidencyManager& GetMeshResidency() noexcept override
        {
            return m_meshResidency;
        }
        [[nodiscard]] const rendering::MeshResidencyManager& GetMeshResidency() const noexcept override
        {
            return m_meshResidency;
        }
        [[nodiscard]] rendering::TextureResidencyRuntime& GetTextureResidency() noexcept override
        {
            return m_textureResidency;
        }
        [[nodiscard]] const rendering::TextureResidencyRuntime& GetTextureResidency() const noexcept override
        {
            return m_textureResidency;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            engine::FramePipelineService* const framePipeline = engine::FindFramePipelineService(context);
            if (framePipeline == nullptr)
                return app::LifecycleStatus::Failure("RenderingService Frame Pipeline dependency is unavailable");

            rendering::RenderSceneFailure sceneFailure;
            if (!m_scenes.Initialize({}, &sceneFailure))
                return app::LifecycleStatus::Failure(FailureMessage(sceneFailure.message, "RenderSceneManager initialization failed"));
            const app::LifecycleStatus deviceStatus = InitializeRendererDevice();
            if (!deviceStatus)
            {
                static_cast<void>(m_scenes.Shutdown());
                return deviceStatus;
            }
            rendering::RenderCameraFailure cameraFailure;
            if (!m_cameras.Initialize(m_scenes, {}, &cameraFailure))
            {
                RollbackRendererDevice();
                static_cast<void>(m_scenes.Shutdown());
                return app::LifecycleStatus::Failure(FailureMessage(cameraFailure.message, "RenderCameraStorage initialization failed"));
            }

            rendering::RenderCommandSystemConfig commandConfig;
            commandConfig.executeFrameTick = &rendering::FrameRenderer::ExecuteFrameTick;
            commandConfig.executeFrame = &rendering::FrameRenderer::ExecuteFrame;
            commandConfig.userData = &m_frameRenderer;
            rendering::RenderCommandFailure commandFailure;
            if (!m_commands.Initialize(m_scenes, m_cameras, commandConfig, &commandFailure))
            {
                static_cast<void>(m_cameras.Shutdown());
                RollbackRendererDevice();
                static_cast<void>(m_scenes.Shutdown());
                return app::LifecycleStatus::Failure(FailureMessage(commandFailure.message, "RenderCommandSystem initialization failed"));
            }
            rendering::ViewportFailure viewportFailure;
            if (!m_viewports.Initialize(m_commands, &viewportFailure))
            {
                static_cast<void>(m_commands.Shutdown());
                static_cast<void>(m_cameras.Shutdown());
                RollbackRendererDevice();
                static_cast<void>(m_scenes.Shutdown());
                return app::LifecycleStatus::Failure(FailureMessage(viewportFailure.message, "ViewportManager initialization failed"));
            }

            constexpr app::ApplicationProfile renderingProfiles =
                app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            engine::FrameParticipantDescriptor updateParticipant;
            updateParticipant.id = engine::RenderingUpdateFrameParticipantId;
            updateParticipant.name = "rendering.updateBoundary";
            updateParticipant.phase = engine::FramePhase::RenderUpdate;
            updateParticipant.profiles = renderingProfiles;
            updateParticipant.affinity = app::ThreadAffinity::MainThread;
            updateParticipant.execute = &ExecuteRenderUpdate;
            updateParticipant.userData = this;

            engine::FrameFailure frameFailure;
            if (!framePipeline->RegisterParticipant(updateParticipant, &frameFailure))
            {
                static_cast<void>(m_viewports.Shutdown());
                static_cast<void>(m_commands.Shutdown());
                static_cast<void>(m_cameras.Shutdown());
                RollbackRendererDevice();
                static_cast<void>(m_scenes.Shutdown());
                return app::LifecycleStatus::Failure(FailureMessage(frameFailure.message, "Rendering update boundary registration failed"));
            }

            constexpr engine::FrameParticipantId frameTickDependencies[]{engine::RenderingUpdateFrameParticipantId};
            engine::FrameParticipantDescriptor frameTickParticipant;
            frameTickParticipant.id = engine::RenderingFrameTickParticipantId;
            frameTickParticipant.name = "rendering.frameTick";
            frameTickParticipant.phase = engine::FramePhase::Render;
            frameTickParticipant.profiles = renderingProfiles;
            frameTickParticipant.affinity = app::ThreadAffinity::MainThread;
            frameTickParticipant.after = {frameTickDependencies, 1};
            frameTickParticipant.execute = &ExecuteRenderingFrameTick;
            frameTickParticipant.userData = this;
            if (!framePipeline->RegisterParticipant(frameTickParticipant, &frameFailure))
            {
                static_cast<void>(m_viewports.Shutdown());
                static_cast<void>(m_commands.Shutdown());
                static_cast<void>(m_cameras.Shutdown());
                RollbackRendererDevice();
                static_cast<void>(m_scenes.Shutdown());
                return app::LifecycleStatus::Failure(FailureMessage(frameFailure.message, "Rendering FrameTick registration failed"));
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            const rendering::ViewportManagerStats viewports = m_viewports.GetStats();
            if (viewports.renderViewports != 0 || viewports.engineViewports != 0 || viewports.buildingFrames != 0)
                return app::LifecycleStatus::Failure("RenderingService has live viewports");
            rendering::RenderCommandFailure failure;
            if (!m_commands.FlushPreviousFrameProcessing(&failure))
                return app::LifecycleStatus::Failure(FailureMessage(failure.message, "RenderingService CPU chain drain failed"));
            rendering::GpuSceneRuntimeFailure gpuSceneFailure;
            if (m_gpuSceneRuntime.IsInitialized() && !m_gpuSceneRuntime.ResolveContributions(&gpuSceneFailure))
                return app::LifecycleStatus::Failure(
                    FailureMessage(gpuSceneFailure.message, "RenderingService contribution resolution failed"));
            if (m_commands.ConsumeExecutionFailure(failure))
                return app::LifecycleStatus::Failure(FailureMessage(failure.message, "RenderingService drained failed asynchronous work"));
            if (m_gpuSceneRuntime.IsInitialized() && m_gpuSceneRuntime.ConsumePublicationFailure(gpuSceneFailure))
                return app::LifecycleStatus::Failure(FailureMessage(gpuSceneFailure.message, "RenderingService drained failed GPU Scene publication"));
            if (m_gpuSceneRuntime.IsInitialized())
            {
                rendering::GpuSceneLifetimeFailure lifetimeFailure;
                static_cast<void>(m_gpuSceneRuntime.GetLifetime().Collect(&lifetimeFailure));
                if (lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::None)
                    return app::LifecycleStatus::Failure(
                        FailureMessage(lifetimeFailure.message, "RenderingService GPU Scene retirement collection failed"));
            }
            if (m_textureResidency.IsInitialized())
            {
                rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
                static_cast<void>(m_textureResidency.CollectRetirements(&textureResidencyFailure));
                if (textureResidencyFailure.code != rendering::TextureResidencyRuntimeFailureCode::None)
                    return app::LifecycleStatus::Failure(FailureMessage(
                        textureResidencyFailure.message, "RenderingService texture retirement collection failed"));
                const rendering::TextureResidencyRuntimeStats textureStats = m_textureResidency.GetStats();
                if (textureStats.liveDemands != 0 || textureStats.residencyRecords != 0)
                    return app::LifecycleStatus::Failure(
                        "RenderingService quiesce requires zero live texture demands and zero texture residency records");
            }
            const rendering::RenderSceneManagerStats sceneStats = m_scenes.GetStats();
            if (sceneStats.activeScenes != 0 || sceneStats.destroyingScenes != 0)
                return app::LifecycleStatus::Failure("RenderingService has live RenderScenes");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            rendering::ViewportFailure viewportFailure;
            if (!m_viewports.Shutdown(&viewportFailure))
                return app::LifecycleStatus::Failure(FailureMessage(viewportFailure.message, "ViewportManager shutdown failed"));
            rendering::RenderCommandFailure commandFailure;
            if (!m_commands.Shutdown(&commandFailure))
                return app::LifecycleStatus::Failure(FailureMessage(commandFailure.message, "RenderCommandSystem shutdown failed"));
            const app::LifecycleStatus deviceStatus = ShutdownRendererDevice();
            if (!deviceStatus)
                return deviceStatus;
            rendering::RenderCameraFailure cameraFailure;
            if (!m_cameras.Shutdown(&cameraFailure))
                return app::LifecycleStatus::Failure(FailureMessage(cameraFailure.message, "RenderCameraStorage shutdown failed"));
            rendering::RenderSceneFailure sceneFailure;
            if (!m_scenes.Shutdown(&sceneFailure))
                return app::LifecycleStatus::Failure(FailureMessage(sceneFailure.message, "RenderSceneManager shutdown failed"));
            return app::LifecycleStatus::Success();
        }

    private:
        app::LifecycleStatus InitializeRendererDevice() noexcept
        {
            if (m_config.deviceMode == engine::RenderingDeviceMode::Disabled)
                return app::LifecycleStatus::Success();
            if (m_config.deviceMode != engine::RenderingDeviceMode::Required || !m_config.backendFactory.IsValid())
                return app::LifecycleStatus::Failure("RenderingService requires a valid renderer backend factory");
            if (rhi::IsInitialized())
                return app::LifecycleStatus::Failure("RenderingService requires exclusive ownership of the process RHI");

            m_backend = m_config.backendFactory.create();
            if (m_backend == nullptr)
                return app::LifecycleStatus::Failure("RenderingService backend creation failed");

            rhi::Failure rhiFailure;
            if (!rhi::Initialize(*m_backend, m_config.device, &rhiFailure))
            {
                m_config.backendFactory.destroy(m_backend);
                m_backend = nullptr;
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "RHI initialization failed"));
            }

            m_resourceDescriptors = rhi::DescriptorDomain(rhi::AdoptReference, rhi::CreateDescriptorDomain(m_config.resourceDescriptors, &rhiFailure));
            if (!m_resourceDescriptors.IsValid())
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "global resource descriptor-domain creation failed"));
            }

            rendering::GpuSceneRuntimeConfig gpuSceneConfig = m_config.gpuScene;
            gpuSceneConfig.tables.resourceDescriptors = m_resourceDescriptors;
            rendering::GpuSceneRuntimeFailure gpuSceneFailure;
            if (!m_gpuSceneRuntime.Initialize(m_scenes, gpuSceneConfig, &gpuSceneFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(gpuSceneFailure.message, "GPU Scene runtime initialization failed"));
            }
            rendering::MeshResidencyFailure meshResidencyFailure;
            if (!m_meshResidency.Initialize(m_gpuSceneRuntime.GetDefinitions(), m_config.meshResidency, &meshResidencyFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(meshResidencyFailure.message, "mesh residency initialization failed"));
            }
            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (!m_textureResidency.Initialize(m_gpuSceneRuntime.GetLifetime(), m_resourceDescriptors,
                                               m_config.textureResidency, &textureResidencyFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(
                    FailureMessage(textureResidencyFailure.message, "texture residency runtime initialization failed"));
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus ShutdownRendererDevice() noexcept
        {
            if (m_backend == nullptr)
                return app::LifecycleStatus::Success();

            rhi::Failure rhiFailure;
            if (!rhi::WaitIdle(&rhiFailure))
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "RHI idle wait failed during renderer shutdown"));

            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (!m_textureResidency.Shutdown(&textureResidencyFailure))
                return app::LifecycleStatus::Failure(
                    FailureMessage(textureResidencyFailure.message, "texture residency runtime shutdown failed"));

            rendering::MeshResidencyFailure meshResidencyFailure;
            if (!m_meshResidency.Shutdown(&meshResidencyFailure))
                return app::LifecycleStatus::Failure(FailureMessage(meshResidencyFailure.message, "mesh residency shutdown failed"));

            rendering::GpuSceneRuntimeFailure gpuSceneFailure;
            if (!m_gpuSceneRuntime.Shutdown({}, &gpuSceneFailure))
                return app::LifecycleStatus::Failure(FailureMessage(gpuSceneFailure.message, "GPU Scene runtime shutdown failed"));

            m_resourceDescriptors.Reset();
            if (!rhi::RetireResources(&rhiFailure) || !rhi::FlushRetiredResources(&rhiFailure) || !rhi::Shutdown(&rhiFailure))
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "RHI shutdown failed"));

            m_config.backendFactory.destroy(m_backend);
            m_backend = nullptr;
            return app::LifecycleStatus::Success();
        }

        void RollbackRendererDevice() noexcept
        {
            if (m_backend == nullptr)
                return;
            static_cast<void>(rhi::WaitIdle());
            static_cast<void>(m_textureResidency.Shutdown());
            static_cast<void>(m_meshResidency.Shutdown());
            static_cast<void>(m_gpuSceneRuntime.Shutdown({}));
            m_resourceDescriptors.Reset();
            static_cast<void>(rhi::RetireResources());
            static_cast<void>(rhi::FlushRetiredResources());
            static_cast<void>(rhi::Shutdown());
            m_config.backendFactory.destroy(m_backend);
            m_backend = nullptr;
        }

        static engine::FrameParticipantStatus ExecuteRenderUpdate(const engine::FrameContext&, void* const userData) noexcept
        {
            auto* const service = static_cast<RenderingServiceImpl*>(userData);
            return service != nullptr ? service->RenderUpdate() : engine::FrameParticipantStatus::Failure("RenderingService is unavailable");
        }

        static engine::FrameParticipantStatus ExecuteRenderingFrameTick(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<RenderingServiceImpl*>(userData);
            return service != nullptr ? service->DispatchFrameTick(context) : engine::FrameParticipantStatus::Failure("RenderingService is unavailable");
        }

        engine::FrameParticipantStatus RenderUpdate() noexcept
        {
            rendering::RenderCommandFailure failure;
            if (!m_commands.FlushPreviousFrameProcessing(&failure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(failure.message, "previous CPU rendering chain flush failed"));
            rendering::GpuSceneRuntimeFailure gpuSceneFailure;
            if (m_gpuSceneRuntime.IsInitialized() && !m_gpuSceneRuntime.ResolveContributions(&gpuSceneFailure))
                return engine::FrameParticipantStatus::Failure(
                    FailureMessage(gpuSceneFailure.message, "GPU Scene contribution resolution failed"));
            if (m_commands.ConsumeExecutionFailure(failure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(failure.message, "asynchronous rendering execution failed"));
            if (m_gpuSceneRuntime.IsInitialized() && m_gpuSceneRuntime.ConsumePublicationFailure(gpuSceneFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(gpuSceneFailure.message, "asynchronous GPU Scene publication failed"));
            if (m_gpuSceneRuntime.IsInitialized())
            {
                rendering::GpuSceneLifetimeFailure lifetimeFailure;
                static_cast<void>(m_gpuSceneRuntime.GetLifetime().Collect(&lifetimeFailure));
                if (lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::None)
                    return engine::FrameParticipantStatus::Failure(
                        FailureMessage(lifetimeFailure.message, "GPU Scene retirement collection failed"));
            }
            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (m_textureResidency.IsInitialized() && !m_textureResidency.Tick(&textureResidencyFailure))
                return engine::FrameParticipantStatus::Failure(
                    FailureMessage(textureResidencyFailure.message, "texture residency progress failed"));
            return engine::FrameParticipantStatus::Success();
        }

        engine::FrameParticipantStatus DispatchFrameTick(const engine::FrameContext& context) noexcept
        {
            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (m_textureResidency.IsInitialized() &&
                !m_textureResidency.StageGpuSceneContribution(m_gpuSceneRuntime,
                                                               &textureResidencyFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(
                    textureResidencyFailure.message, "texture GPU Scene contribution staging failed"));
            rendering::RenderCommandFrameTickResult result;
            rendering::RenderCommandFailure failure;
            if (!m_commands.FrameTick(m_scenes.GetFramePipelineScenes(), context.frame + 1u, result, &failure))
            {
                rendering::GpuSceneRuntimeFailure gpuSceneFailure;
                static_cast<void>(m_gpuSceneRuntime.ResolveContributions(&gpuSceneFailure));
                return engine::FrameParticipantStatus::Failure(FailureMessage(failure.message, "render command FrameTick dispatch failed"));
            }
            return engine::FrameParticipantStatus::Success();
        }

        engine::RenderingServiceConfig m_config;
        rendering::RenderSceneManager m_scenes;
        rendering::RenderCameraStorage m_cameras;
        rhi::IBackend* m_backend = nullptr;
        rhi::DescriptorDomain m_resourceDescriptors;
        rendering::GpuSceneRuntime m_gpuSceneRuntime;
        rendering::MeshResidencyManager m_meshResidency;
        rendering::TextureResidencyRuntime m_textureResidency;
        rendering::FrameRenderer m_frameRenderer;
        rendering::RenderCommandSystem m_commands;
        rendering::ViewportManager m_viewports;
    };

    app::Service* CreateRenderingService(void* const userData) noexcept
    {
        if (userData == nullptr)
            return nullptr;
        auto block = memory::Allocate(memory::PoolId::Rendering, sizeof(RenderingServiceImpl), alignof(RenderingServiceImpl));
        return block ? ::new (block.address) RenderingServiceImpl(*static_cast<const engine::RenderingServiceConfig*>(userData)) : nullptr;
    }

    void DestroyRenderingService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<RenderingServiceImpl*>(service)->~RenderingServiceImpl();
        memory::MemoryBlock block{service, sizeof(RenderingServiceImpl), memory::PoolId::Rendering};
        memory::Free(block);
    }
} // namespace

namespace vanguard::engine
{
    namespace
    {
        bool RegisterRenderingServiceInternal(application::EngineHost& host, const RenderingServiceConfig& config,
                                              application::HostFailure* const failure) noexcept
        {
            using Profile = application::ApplicationProfile;
            constexpr application::ServiceDependency dependencies[]{{JobsServiceId, application::DependencyKind::Required},
                                                                    {FramePipelineServiceId, application::DependencyKind::Required}};
            constexpr application::CapabilityId capabilities[]{RenderingCapabilityId};
            application::ServiceDescriptor descriptor;
            descriptor.id = RenderingServiceId;
            descriptor.name = "rendering";
            descriptor.profiles = Profile::Runtime | Profile::Editor | Profile::Tool;
            descriptor.scope = application::ServiceScope::Process;
            descriptor.affinity = application::ThreadAffinity::MainThread;
            descriptor.dependencies = {dependencies, 2};
            descriptor.provides = {capabilities, 1};
            descriptor.create = CreateRenderingService;
            descriptor.destroy = DestroyRenderingService;
            descriptor.userData = const_cast<RenderingServiceConfig*>(&config);
            return host.RegisterService(EngineModuleId, descriptor, failure);
        }
    } // namespace

    bool RegisterRenderingService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        static const RenderingServiceConfig config;
        return RegisterRenderingServiceInternal(host, config, failure);
    }

    bool RegisterRenderingService(application::EngineHost& host, const RenderingServiceConfig& config, application::HostFailure* const failure) noexcept
    {
        return RegisterRenderingServiceInternal(host, config, failure);
    }

    RenderingService* FindRenderingService(application::EngineHost& host) noexcept
    {
        return static_cast<RenderingService*>(host.FindCapability(RenderingCapabilityId));
    }

    RenderingService* FindRenderingService(application::ServiceContext& context) noexcept
    {
        return static_cast<RenderingService*>(context.FindCapability(RenderingCapabilityId));
    }
} // namespace vanguard::engine

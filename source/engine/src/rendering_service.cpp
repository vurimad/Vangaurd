#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/frame_renderer.hpp>
#include <vanguard/rendering/material_program_layout.hpp>
#include <vanguard/rendering/renderer_feature_programs.hpp>
#include <vanguard/pipelines/renderer_catalog.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>

#include <new>
#include <cstring>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace memory = vanguard::memory;
    namespace rendering = vanguard::rendering;
    namespace rhi = vanguard::rhi;

    [[nodiscard]] const char* FailureMessage(const char* const message, const char* const fallback) noexcept
    {
        return message != nullptr && message[0] != '\0' ? message : fallback;
    }

    class RenderingServiceImpl final : public engine::RenderingService
    {
    public:
        explicit RenderingServiceImpl(const engine::RenderingServiceConfig& config) noexcept
            : m_config(config),
              m_frameRenderer(m_scenes, m_cameras, m_gpuSceneRuntime, config.geometryFrames, config.meshResidency.geometryBatcher.maximumBins,
                              &m_meshResidency.GetGeometryBatcher(), config.meshResidency.geometryBatcher.maximumShells,
                              &m_meshResidency.GetGeometryAllocator(), &m_renderPhases)
        {
        }

        [[nodiscard]] rhi::DescriptorDomainRef GetResourceDescriptorDomain() const noexcept override { return m_resourceDescriptors.GetRef(); }
        [[nodiscard]] rhi::DescriptorDomainRef GetSamplerDescriptorDomain() const noexcept override { return m_samplerDescriptors.GetRef(); }
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
        [[nodiscard]] rendering::MaterialProgramLayoutRegistry& GetMaterialProgramLayouts() noexcept override
        {
            return m_materialProgramLayouts;
        }
        [[nodiscard]] const rendering::MaterialProgramLayoutRegistry& GetMaterialProgramLayouts() const noexcept override
        {
            return m_materialProgramLayouts;
        }
        [[nodiscard]] const rendering::RenderPhaseRegistry& GetRenderPhases() const noexcept override
        {
            return m_renderPhases;
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
        [[nodiscard]] rendering::MaterialResourceResolver& GetMaterialResources() noexcept override
        {
            return m_materialResources;
        }
        [[nodiscard]] const rendering::MaterialResourceResolver& GetMaterialResources() const noexcept override
        {
            return m_materialResources;
        }
        [[nodiscard]] rendering::MaterialMaterializer& GetMaterialMaterializer() noexcept override
        {
            return m_materialMaterializer;
        }
        [[nodiscard]] const rendering::MaterialMaterializer& GetMaterialMaterializer() const noexcept override
        {
            return m_materialMaterializer;
        }
        [[nodiscard]] rendering::MaterialResidencyRuntime& GetMaterialResidency() noexcept override
        {
            return m_materialResidency;
        }
        [[nodiscard]] const rendering::MaterialResidencyRuntime& GetMaterialResidency() const noexcept override
        {
            return m_materialResidency;
        }
        [[nodiscard]] rendering::MaterialSceneBindingBridge& GetMaterialBindings() noexcept override
        {
            return m_materialBindings;
        }
        [[nodiscard]] const rendering::MaterialSceneBindingBridge& GetMaterialBindings() const noexcept override
        {
            return m_materialBindings;
        }
        [[nodiscard]] engine::RenderingResourceAllocatorDiagnostics GetResourceAllocatorDiagnostics() const noexcept override
        {
            return {m_frameRenderer.IsResourceAllocatorInitialized(), m_frameRenderer.GetResourceAllocatorStats()};
        }

        [[nodiscard]] rendering::GeometryDiagnosticsPoll PollGeometryDiagnostics(rendering::GeometryFrameDiagnostics& output, rhi::Failure* failure) noexcept override
        {
            return m_frameRenderer.PollGeometryDiagnostics(output, failure);
        }
        [[nodiscard]] vanguard::u64 GetDroppedGeometryDiagnosticSamples() const noexcept override
        {
            return m_frameRenderer.GetDroppedGeometryDiagnosticSamples();
        }

        [[nodiscard]] bool SealResidencyRetirements(const rhi::ResidencyFenceSet& safeAfter, engine::RenderingRetirementFailure* const failure) noexcept override
        {
            if (failure != nullptr)
                *failure = {};
            if (m_deviceUnavailable || !m_gpuSceneRuntime.IsInitialized())
            {
                if (failure != nullptr)
                    *failure = {engine::RenderingRetirementFailureCode::DeviceDisabled, "renderer residency retirement requires an initialized device"};
                return false;
            }
            if (!safeAfter.Covers(rhi::QueueType::Graphics) || !safeAfter.Covers(rhi::QueueType::Compute) || !safeAfter.Covers(rhi::QueueType::Copy))
            {
                if (failure != nullptr)
                    *failure = {engine::RenderingRetirementFailureCode::MissingCutover, "renderer residency retirement requires graphics, compute, and copy cutover fences"};
                return false;
            }
            rendering::MaterialResidencyRuntimeFailure materialFailure;
            if (!m_materialResidency.SealRetirements(safeAfter, &materialFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingRetirementFailureCode::MaterialFailure;
                    failure->message = FailureMessage(materialFailure.message, "material retirement sealing failed");
                    failure->materialFailure = materialFailure;
                }
                return false;
            }
            rendering::TextureResidencyRuntimeFailure textureFailure;
            if (!m_textureResidency.SealRetirements(safeAfter, &textureFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingRetirementFailureCode::TextureFailure;
                    failure->message = FailureMessage(textureFailure.message, "texture retirement sealing failed");
                    failure->textureFailure = textureFailure;
                }
                return false;
            }
            rendering::MeshResidencyFailure meshFailure;
            if (!m_meshResidency.SealRetirements(safeAfter, &meshFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingRetirementFailureCode::MeshFailure;
                    failure->message = FailureMessage(meshFailure.message, "mesh retirement sealing failed");
                    failure->meshFailure = meshFailure;
                }
                return false;
            }
            rendering::GpuSceneLifetimeFailure gpuSceneFailure;
            if (!m_gpuSceneRuntime.GetLifetime().SealRetirements(safeAfter, &gpuSceneFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingRetirementFailureCode::GpuSceneFailure;
                    failure->message = FailureMessage(gpuSceneFailure.message, "GPU Scene retirement sealing failed");
                    failure->gpuSceneFailure = gpuSceneFailure;
                }
                return false;
            }
            return true;
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
            const app::LifecycleStatus deviceStatus = InitializeRendererDevice(engine::FindResourceStreamingService(context));
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

            constexpr app::ApplicationProfile renderingProfiles = app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
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
                return app::LifecycleStatus::Failure(FailureMessage(gpuSceneFailure.message, "RenderingService contribution resolution failed"));
            rendering::RenderSceneFailure sceneBindingFailure;
            if (!m_scenes.ResolveMeshBindings(256u, &sceneBindingFailure))
                return app::LifecycleStatus::Failure(FailureMessage(sceneBindingFailure.message, "mesh scene-binding acceptance drain failed"));
            if (m_commands.ConsumeExecutionFailure(failure))
                return app::LifecycleStatus::Failure(FailureMessage(failure.message, "RenderingService drained failed asynchronous work"));
            if (m_gpuSceneRuntime.IsInitialized() && m_gpuSceneRuntime.ConsumePublicationFailure(gpuSceneFailure))
                return app::LifecycleStatus::Failure(FailureMessage(gpuSceneFailure.message, "RenderingService drained failed GPU Scene publication"));
            if (m_gpuSceneRuntime.IsInitialized())
            {
                rendering::GpuSceneLifetimeFailure lifetimeFailure;
                static_cast<void>(m_gpuSceneRuntime.GetLifetime().Collect(&lifetimeFailure));
                if (lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::None)
                    return app::LifecycleStatus::Failure(FailureMessage(lifetimeFailure.message, "RenderingService GPU Scene retirement collection failed"));
            }
            if (m_textureResidency.IsInitialized())
            {
                rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
                static_cast<void>(m_textureResidency.CollectRetirements(&textureResidencyFailure));
                if (textureResidencyFailure.code != rendering::TextureResidencyRuntimeFailureCode::None)
                    return app::LifecycleStatus::Failure(FailureMessage(textureResidencyFailure.message, "RenderingService texture retirement collection failed"));
                const rendering::TextureResidencyRuntimeStats textureStats = m_textureResidency.GetStats();
                if (textureStats.liveDemands != 0 || textureStats.residencyRecords != 0)
                    return app::LifecycleStatus::Failure("RenderingService quiesce requires zero live texture demands and zero texture residency records");
            }
            if (m_meshResidency.IsInitialized())
            {
                rendering::MeshResidencyFailure meshFailure;
                static_cast<void>(m_meshResidency.CollectRetirements(&meshFailure));
                if (meshFailure.code != rendering::MeshResidencyFailureCode::None)
                    return app::LifecycleStatus::Failure(FailureMessage(meshFailure.message, "RenderingService mesh retirement collection failed"));
            }
            if (m_materialBindings.IsInitialized() && m_materialBindings.GetStats().bindings != 0)
                return app::LifecycleStatus::Failure("RenderingService quiesce requires zero live material scene bindings");
            if (m_materialResidency.IsInitialized())
            {
                const rendering::MaterialResidencyRuntimeStats materialStats = m_materialResidency.GetStats();
                if (materialStats.residencyRecords != 0 || materialStats.liveDemands != 0 || materialStats.liveTechniqueRequests != 0 || materialStats.referencedNativePrograms != 0)
                    return app::LifecycleStatus::Failure("RenderingService quiesce requires zero live material residency and technique work");
                rendering::MaterialResidencyRuntimeFailure materialFailure;
                if (!m_materialResidency.ClearNativeProgramCache(&materialFailure))
                    return app::LifecycleStatus::Failure(FailureMessage(materialFailure.message, "RenderingService native material program cache clear failed"));
            }
            if (m_materialMaterializer.IsInitialized())
            {
                const rendering::MaterialMaterializerStats materializerStats = m_materialMaterializer.GetStats();
                if (materializerStats.activeOperations != 0 || materializerStats.liveReferences != 0)
                    return app::LifecycleStatus::Failure("RenderingService quiesce requires zero materializer operations and references");
            }
            if (m_materialResources.IsInitialized())
            {
                const rendering::MaterialResourceResolverStats resourceStats = m_materialResources.GetStats();
                if (resourceStats.activeOperations != 0 || resourceStats.liveReferences != 0 || resourceStats.cachedDescriptors != 0)
                    return app::LifecycleStatus::Failure("RenderingService quiesce requires zero material resource operations, references, and descriptors");
                rendering::MaterialResourceResolverFailure resourceFailure;
                if (!m_materialResources.ClearFallbacks(&resourceFailure))
                    return app::LifecycleStatus::Failure(FailureMessage(resourceFailure.message, "RenderingService material fallback clear failed"));
            }
            if (m_frameRenderer.IsResourceAllocatorInitialized())
            {
                const rendering::RenderFlowResourceAllocatorStats resourceStats = m_frameRenderer.GetResourceAllocatorStats();
                if (resourceStats.state != rendering::RenderFlowResourceSessionState::Idle || resourceStats.outstandingPublishedExports != 0)
                    return app::LifecycleStatus::Failure("RenderingService quiesce requires an idle frame-resource allocator with no outstanding published exports");
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
            m_frameRenderer.ClearGraphCache();
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
        static app::LifecycleStatus FeatureFailure(vanguard::containers::StringView name, const char* message) noexcept
        {
            VG_LOG_ERROR(vanguard::diagnostics::Category::Rendering, "renderer feature %.*s: %s", static_cast<int>(name.Size()), name.Data(), message);
            return app::LifecycleStatus::Failure(message);
        }

        template <typename Entry>
        app::LifecycleStatus LoadCatalog(const vanguard::containers::ArraySpan<const Entry> catalog, engine::ResourceStreamingService* const service, const vanguard::resources::ResourceTypeId type,
                                         vanguard::containers::DynamicArray<vanguard::resources::ResourceHandle>& loaded) noexcept
        {
            if (!catalog.Empty() && service == nullptr)
                return app::LifecycleStatus::Failure("renderer feature catalog requires ResourceStreamingService");
            vanguard::containers::DynamicArray<vanguard::resources::PipelineRequest> requests{memory::pools::Rendering::GetInstance()};
            requests.Reserve(catalog.Size());
            loaded.Reserve(catalog.Size());
            for (const Entry& entry : catalog)
            {
                if (entry.name.Size() == 0 || !entry.resource.IsValid() || entry.resource.ExpectedType() != type)
                    return app::LifecycleStatus::Failure("renderer feature catalog has an invalid name or typed resource reference");
                requests.PushBack(service->GetStreamer().Request(entry.resource, vanguard::resources::LoadPriority::High));
            }
            for (vanguard::u32 index = 0; index < requests.Size(); ++index)
            {
                const auto& request = requests[index];
                request.Wait();
                if (!request.HasLoaded())
                    return FeatureFailure(catalog[index].name, "catalog resource could not be loaded");
                auto resource = request.Acquire();
                if (!resource.IsValid() || resource.GetType() != type)
                    return app::LifecycleStatus::Failure("renderer feature catalog resource has an invalid loaded type");
                loaded.PushBack(std::move(resource));
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus LoadRendererBootstrap(engine::ResourceStreamingService* const service) noexcept
        {
            using namespace vanguard;
            resources::ResourceReference reference = m_config.rendererBootstrap;
            if (!reference.IsValid() && m_config.usePackageRendererBootstrap && service != nullptr)
                reference = service->GetPackageSet().RendererBootstrap();
            if (!reference.IsValid())
                return m_config.usePackageRendererBootstrap ? app::LifecycleStatus::Failure("DATA boot record has no renderer catalog")
                                                           : app::LifecycleStatus::Success();
            if (service == nullptr || reference.ExpectedType() != pipelines::RendererCatalogResourceType ||
                !m_config.rendererShaders.Empty() || !m_config.rendererPipelines.Empty())
                return app::LifecycleStatus::Failure("renderer catalog requires a typed source and cannot be combined with startup arrays");
            auto request = service->GetStreamer().Request(reference, resources::LoadPriority::Critical);
            if (!request.IsValid())
                return app::LifecycleStatus::Failure("renderer catalog request was rejected");
            request.Wait();
            auto loaded = request.Acquire();
            if (!loaded.IsValid() || loaded.GetType() != pipelines::RendererCatalogResourceType)
                return app::LifecycleStatus::Failure("renderer catalog could not be loaded");
            const auto entries = static_cast<const pipelines::RendererCatalogResource*>(loaded.Get())->Entries();
            m_bootstrapRecords.Resize(entries.Size());
            m_bootstrapShaders.Resize(entries.Size());
            m_bootstrapPipelines.Resize(entries.Size());
            m_bootstrapConstants.Resize(entries.Size());
            m_bootstrapLayouts.Resize(entries.Size());
            if (m_bootstrapRecords.Size() != entries.Size() || m_bootstrapShaders.Size() != entries.Size() ||
                m_bootstrapPipelines.Size() != entries.Size() || m_bootstrapConstants.Size() != entries.Size() ||
                m_bootstrapLayouts.Size() != entries.Size())
                return app::LifecycleStatus::Failure("renderer catalog allocation failed");
            for (u32 index = 0; index < entries.Size(); ++index)
            {
                m_bootstrapRecords[index] = entries[index];
                const auto& entry = m_bootstrapRecords[index];
                m_bootstrapShaders[index] = {entry.name, entry.shader};
                m_bootstrapPipelines[index].name = entry.name;
                m_bootstrapPipelines[index].resource = entry.pipeline;
            }
            for (const auto& feature : rendering::RendererFeaturePrograms)
            {
                if (feature.optional)
                    continue;
                bool found = false;
                for (const auto& entry : m_bootstrapRecords)
                    found |= std::strcmp(entry.name, feature.name) == 0;
                if (!found)
                {
                    VG_LOG_ERROR(diagnostics::Category::Rendering, "renderer catalog is missing feature %s", feature.name);
                    return app::LifecycleStatus::Failure("renderer catalog is missing a required geometry feature");
                }
            }
            m_config.rendererShaders = m_bootstrapShaders;
            m_config.rendererPipelines = m_bootstrapPipelines;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus LoadRendererShaders(engine::ResourceStreamingService* const service) noexcept
        {
            vanguard::containers::DynamicArray<vanguard::resources::ResourceHandle> loaded{memory::pools::Rendering::GetInstance()};
            const app::LifecycleStatus status = LoadCatalog(m_config.rendererShaders, service, vanguard::shaders::ShaderResourceType, loaded);
            if (!status)
                return status;
            vanguard::containers::DynamicArray<rendering::NamedRenderShader> shaders{memory::pools::Rendering::GetInstance()};
            shaders.Reserve(loaded.Size());
            for (vanguard::u32 index = 0; index < loaded.Size(); ++index)
            {
                const auto* resource = static_cast<const vanguard::shaders::ShaderResourceObject*>(loaded[index].Get());
                if (!m_bootstrapRecords.Empty())
                {
                    const auto& file = resource->GetFile();
                    for (const auto& feature : rendering::RendererFeaturePrograms)
                    {
                        if (std::strcmp(m_bootstrapRecords[index].name, feature.name) != 0)
                            continue;
                        const auto expectedKind = feature.compute != nullptr ? vanguard::shaders::ProgramKind::Compute : vanguard::shaders::ProgramKind::Graphics;
                        if (file.GetKind() != expectedKind)
                            return FeatureFailure(feature.name, "shader program kind disagrees with the renderer feature");
                    }
                    const auto buffers = file.GetConstantBuffers();
                    const bool push = m_bootstrapRecords[index].constants == vanguard::pipelines::RendererConstants::SinglePushConstant;
                    if (buffers.Size() != (push ? 1u : 0u))
                        return FeatureFailure(m_bootstrapRecords[index].name, "constants policy disagrees with shader reflection");
                    if (push)
                    {
                        rhi::ShaderStageMask visibility = 0;
                        for (const auto& stage : file.GetStages())
                        {
                            switch (stage.stage)
                            {
                            case vanguard::shaders::ShaderStage::Vertex: visibility |= rhi::ShaderStageBit(rhi::ShaderStage::Vertex); break;
                            case vanguard::shaders::ShaderStage::Fragment: visibility |= rhi::ShaderStageBit(rhi::ShaderStage::Pixel); break;
                            case vanguard::shaders::ShaderStage::Compute: visibility |= rhi::ShaderStageBit(rhi::ShaderStage::Compute); break;
                            default: return app::LifecycleStatus::Failure("renderer bootstrap program has an unsupported stage");
                            }
                        }
                        m_bootstrapConstants[index] = {buffers[0].binding, buffers[0].byteSize, rhi::BindingType::PushConstants};
                        m_bootstrapLayouts[index] = {&m_bootstrapConstants[index], 1, buffers[0].space, visibility};
                        m_bootstrapPipelines[index].bindingLayouts = {&m_bootstrapLayouts[index], 1};
                    }
                }
                shaders.PushBack({m_config.rendererShaders[index].name, &resource->GetFile()});
            }
            rhi::Failure failure;
            if (m_frameRenderer.InitializeShaders(shaders, &failure) != rendering::RenderShaderResult::Success)
                return app::LifecycleStatus::Failure("renderer feature shader catalog initialization failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus PrepareRendererPipelines(engine::ResourceStreamingService* const service, const rendering::PipelineInterfaceResources& interfaces) noexcept
        {
            vanguard::containers::DynamicArray<vanguard::resources::ResourceHandle> loaded{memory::pools::Rendering::GetInstance()};
            const app::LifecycleStatus status = LoadCatalog(m_config.rendererPipelines, service, vanguard::pipelines::PipelineResourceType, loaded);
            if (!status)
                return status;
            vanguard::containers::DynamicArray<rendering::ResolvedRenderShader> shaders{memory::pools::Rendering::GetInstance()};
            shaders.Reserve(m_config.rendererShaders.Size());
            for (const auto& shader : m_config.rendererShaders)
                shaders.PushBack({shader.resource.GetPath().Id(), m_frameRenderer.GetShader(shader.name)});
            vanguard::containers::DynamicArray<rendering::NamedRenderPipeline> pipelines{memory::pools::Rendering::GetInstance()};
            pipelines.Reserve(loaded.Size());
            vanguard::containers::DynamicArray<rhi::BindingLayout> layoutOwners{memory::pools::Rendering::GetInstance()};
            vanguard::containers::DynamicArray<rhi::BindingLayoutRef> layouts{memory::pools::Rendering::GetInstance()};
            vanguard::u32 layoutCount = 0;
            for (const auto& source : m_config.rendererPipelines)
            {
                if (source.bindingLayouts.Size() > rhi::MaximumBindingLayoutsPerPipeline || source.bindingLayouts.Size() > ~vanguard::u32{0} - layoutCount)
                    return app::LifecycleStatus::Failure("renderer pipeline has too many binding layouts");
                layoutCount += source.bindingLayouts.Size();
            }
            layoutOwners.Reserve(layoutCount);
            layouts.Reserve(layoutCount);
            for (vanguard::u32 index = 0; index < loaded.Size(); ++index)
            {
                const auto* resource = static_cast<const vanguard::pipelines::PipelineResourceObject*>(loaded[index].Get());
                if (!m_bootstrapRecords.Empty())
                {
                    const auto references = resource->GetFile().GetShaders();
                    if (references.Size() != 1 || references[0].resource != m_bootstrapRecords[index].shader.GetPath().Id())
                        return FeatureFailure(m_bootstrapRecords[index].name, "pipeline references a different shader from its catalog entry");
                }
                if (!m_bootstrapRecords.Empty() && resource->GetFile().GetKind() == vanguard::pipelines::PipelineKind::Graphics)
                {
                    const auto& graphics = resource->GetFile().GetGraphics();
                    if (graphics.attachmentPolicy == vanguard::pipelines::AttachmentPolicy::Exact)
                        m_bootstrapPipelines[index].attachments = graphics.exactAttachments;
                    else
                    {
                        if (m_config.rendererOutputAttachments.colorCount == 0)
                            return app::LifecycleStatus::Failure("renderer output pipeline requires an explicit output attachment signature");
                        m_bootstrapPipelines[index].attachments = m_config.rendererOutputAttachments;
                    }
                }
                const auto& source = m_config.rendererPipelines[index];
                rendering::RenderPipelineRequest request;
                request.pipeline = &resource->GetFile();
                request.shaders = shaders;
                request.attachments = resource->GetFile().GetKind() == vanguard::pipelines::PipelineKind::Graphics ? &source.attachments : nullptr;
                request.interfaceResources = interfaces;
                const vanguard::u32 firstLayout = layouts.Size();
                for (const rhi::BindingLayoutDesc& description : source.bindingLayouts)
                {
                    rhi::BindingLayout layout(rhi::AdoptReference, rhi::RequestBindingLayout(description));
                    if (!layout)
                        return app::LifecycleStatus::Failure("renderer pipeline binding layout creation failed");
                    layouts.PushBack(layout.GetRef());
                    layoutOwners.PushBack(std::move(layout));
                }
                if (!source.bindingLayouts.Empty())
                    request.interfaceResources.bindingLayouts = {layouts.TypedData() + firstLayout, source.bindingLayouts.Size()};
                pipelines.PushBack({source.name, request});
            }
            if (m_frameRenderer.InitializePipelines(pipelines, m_materialPipelines) != rendering::RenderPipelineResult::Success)
                return app::LifecycleStatus::Failure("renderer feature pipeline preparation failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus InitializeRendererDevice(engine::ResourceStreamingService* const streamingService) noexcept
        {
            m_deviceUnavailable = false;
            if (m_config.deviceMode == engine::RenderingDeviceMode::Disabled)
                return m_config.rendererShaders.Empty() && m_config.rendererPipelines.Empty() && !m_config.rendererBootstrap.IsValid() &&
                       !m_config.usePackageRendererBootstrap ? app::LifecycleStatus::Success()
                                                                                              : app::LifecycleStatus::Failure("renderer feature catalogs require an enabled device");
            if (m_config.deviceMode != engine::RenderingDeviceMode::Required || !m_config.backendFactory.IsValid())
                return app::LifecycleStatus::Failure("RenderingService requires a valid renderer backend factory");
            if (m_config.materialBindingLayouts.Size() > rhi::MaximumBindingLayoutsPerPipeline)
                return app::LifecycleStatus::Failure("material draw interface has too many binding layouts");
            if (rhi::IsInitialized())
                return app::LifecycleStatus::Failure("RenderingService requires exclusive ownership of the process RHI");

            const app::LifecycleStatus bootstrapStatus = LoadRendererBootstrap(streamingService);
            if (!bootstrapStatus)
                return bootstrapStatus;

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

            const app::LifecycleStatus shadersStatus = LoadRendererShaders(streamingService);
            if (!shadersStatus)
            {
                RollbackRendererDevice();
                return shadersStatus;
            }
            m_resourceDescriptors = rhi::DescriptorDomain(rhi::AdoptReference, rhi::CreateDescriptorDomain(m_config.resourceDescriptors, &rhiFailure));
            if (!m_resourceDescriptors.IsValid())
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "global resource descriptor-domain creation failed"));
            }
            m_samplerDescriptors = rhi::DescriptorDomain(rhi::AdoptReference, rhi::CreateDescriptorDomain(m_config.samplerDescriptors, &rhiFailure));
            if (!m_samplerDescriptors.IsValid())
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "global sampler descriptor-domain creation failed"));
            }

            rendering::RenderFlowResourceFailure frameResourceFailure;
            auto frameResources = m_config.frameResources;
            frameResources.resourceDescriptors = m_resourceDescriptors.GetRef();
            const bool frameResourcesInitialized = m_frameRenderer.InitializeResourceAllocator(frameResources, &frameResourceFailure);
            if (!frameResourcesInitialized)
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(frameResourceFailure.message, "frame-resource allocator initialization failed"));
            }

            rendering::GpuSceneRuntimeConfig gpuSceneConfig = m_config.gpuScene;
            gpuSceneConfig.tables.resourceDescriptors = m_resourceDescriptors;
            rendering::GpuSceneRuntimeFailure gpuSceneFailure;
            if (!m_gpuSceneRuntime.Initialize(m_scenes, gpuSceneConfig, &gpuSceneFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(gpuSceneFailure.message, "GPU Scene runtime initialization failed"));
            }
            rendering::MaterialProgramLayoutRegistryConfig layoutConfig;
            layoutConfig.backend = rhi::GetCapabilities().backend;
            layoutConfig.maximumLayouts = m_config.maximumMaterialProgramLayouts;
            rendering::MaterialProgramLayoutFailure layoutFailure;
            if (!m_materialProgramLayouts.Initialize(layoutConfig, &layoutFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(layoutFailure.message, "material program-layout registry initialization failed"));
            }
            rendering::RenderPhaseFailure phaseFailure;
            if (!m_renderPhases.Initialize(m_config.renderPhases, &phaseFailure) || !rendering::RegisterStandardRenderPhases(m_renderPhases, &phaseFailure) || !m_renderPhases.Seal(&phaseFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(phaseFailure.message, "render phase registry initialization failed"));
            }
            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (!m_textureResidency.Initialize(m_gpuSceneRuntime.GetLifetime(), m_resourceDescriptors, m_config.textureResidency, &textureResidencyFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(textureResidencyFailure.message, "texture residency runtime initialization failed"));
            }
            rendering::MaterialResourceResolverFailure resourceFailure;
            if (!m_materialResources.Initialize(m_textureResidency, m_resourceDescriptors.GetRef(), m_samplerDescriptors.GetRef(), m_config.materialResources, &resourceFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(resourceFailure.message, "material resource resolver initialization failed"));
            }
            rendering::MaterialMaterializerFailure materializerFailure;
            if (!m_materialMaterializer.Initialize(m_materialProgramLayouts, m_materialResources, m_gpuSceneRuntime, m_config.materialMaterializer, &materializerFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(materializerFailure.message, "material materializer initialization failed"));
            }
            if (!m_materialPipelines.Initialize(m_config.materialPipelines))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure("material pipeline cache initialization failed");
            }
            const rhi::DescriptorDomainRef materialDomains[]{m_resourceDescriptors.GetRef(), m_samplerDescriptors.GetRef()};
            const rendering::PipelineInterfaceResources interfaceResources{{}, {materialDomains, 2}};
            const app::LifecycleStatus pipelinesStatus = PrepareRendererPipelines(streamingService, interfaceResources);
            if (!pipelinesStatus)
            {
                RollbackRendererDevice();
                return pipelinesStatus;
            }
            rendering::MaterialResidencyRuntimeFailure materialResidencyFailure;
            rhi::BindingLayoutRef materialLayouts[rhi::MaximumBindingLayoutsPerPipeline]{};
            for (vanguard::u32 index = 0; index < m_config.materialBindingLayouts.Size(); ++index)
            {
                m_materialBindingLayouts[index] = rhi::BindingLayout(rhi::AdoptReference, rhi::RequestBindingLayout(m_config.materialBindingLayouts[index]));
                if (!m_materialBindingLayouts[index])
                {
                    RollbackRendererDevice();
                    return app::LifecycleStatus::Failure("material draw binding layout creation failed");
                }
                materialLayouts[index] = m_materialBindingLayouts[index].GetRef();
            }
            const rendering::PipelineInterfaceResources materialInterfaces{{materialLayouts, m_config.materialBindingLayouts.Size()}, {materialDomains, 2}};
            if (!m_materialResidency.Initialize(m_materialMaterializer, m_materialPipelines, materialInterfaces, m_config.materialResidency, &materialResidencyFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(materialResidencyFailure.message, "material residency runtime initialization failed"));
            }
            rendering::MeshResidencyFailure meshResidencyFailure;
            if (!m_meshResidency.Initialize(m_gpuSceneRuntime, m_materialResidency, m_renderPhases, m_config.meshResidency, &meshResidencyFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(meshResidencyFailure.message, "mesh residency initialization failed"));
            }
            rendering::MaterialSceneBindingFailure bindingFailure;
            if (!m_materialBindings.Initialize(m_materialResidency, m_gpuSceneRuntime.GetScenePublisher(), m_config.materialBindings, &bindingFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(bindingFailure.message, "material scene-binding bridge initialization failed"));
            }
            return app::LifecycleStatus::Success();
        }

        // Producer shutdown can enqueue final persistent-table retirements after
        // the normal frame cutover. Both callers have already joined the CPU tail
        // and waited for the device; use the actual RHI receipts for that last epoch.
        [[nodiscard]] const char* DrainShutdownGpuSceneRetirements() noexcept
        {
            if (!m_gpuSceneRuntime.IsInitialized())
                return nullptr;
            auto& lifetime = m_gpuSceneRuntime.GetLifetime();
            rhi::ResidencyFenceSet cutover;
            rendering::GpuSceneLifetimeFailure failure;
            if (!rhi::GetSubmittedResidencyFences(cutover))
                return "renderer shutdown has no complete GPU retirement receipt";
            if (!lifetime.SealRetirements(cutover, &failure))
                return FailureMessage(failure.message, "renderer shutdown retirement sealing failed");
            static_cast<void>(lifetime.Collect(&failure));
            if (failure.code != rendering::GpuSceneLifetimeFailureCode::None)
                return FailureMessage(failure.message, "renderer shutdown retirement collection failed");
            return lifetime.GetStats().retiring != 0 ? "renderer shutdown still has pending GPU retirements" : nullptr;
        }

        app::LifecycleStatus ShutdownRendererDevice() noexcept
        {
            if (m_backend == nullptr)
                return app::LifecycleStatus::Success();
            if (m_deviceUnavailable)
            {
                AbandonRendererDevice();
                return app::LifecycleStatus::Success();
            }

            rhi::Failure rhiFailure;
            if (!rhi::WaitIdle(&rhiFailure))
            {
                if (rhiFailure.code == rhi::FailureCode::DeviceLost)
                {
                    AbandonRendererDevice();
                    return app::LifecycleStatus::Success();
                }
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "RHI idle wait failed during renderer shutdown"));
            }

            rendering::MaterialSceneBindingFailure bindingFailure;
            if (!m_materialBindings.Shutdown(&bindingFailure))
                return app::LifecycleStatus::Failure(FailureMessage(bindingFailure.message, "material scene-binding bridge shutdown failed"));

            rendering::MeshResidencyFailure meshResidencyFailure;
            if (!m_meshResidency.Shutdown(&meshResidencyFailure))
                return app::LifecycleStatus::Failure(FailureMessage(meshResidencyFailure.message, "mesh residency shutdown failed"));

            rendering::MaterialResidencyRuntimeFailure materialResidencyFailure;
            if (!m_materialResidency.Shutdown(&materialResidencyFailure))
                return app::LifecycleStatus::Failure(FailureMessage(materialResidencyFailure.message, "material residency runtime shutdown failed"));

            m_frameRenderer.ClearPipelines();
            m_materialPipelines.WaitIdle();
            if (!m_materialPipelines.Shutdown())
                return app::LifecycleStatus::Failure("material pipeline cache shutdown failed");
            for (rhi::BindingLayout& layout : m_materialBindingLayouts)
                layout.Reset();

            rendering::MaterialMaterializerFailure materializerFailure;
            if (!m_materialMaterializer.Shutdown(&materializerFailure))
                return app::LifecycleStatus::Failure(FailureMessage(materializerFailure.message, "material materializer shutdown failed"));

            rendering::MaterialResourceResolverFailure resourceFailure;
            if (!m_materialResources.Shutdown(&resourceFailure))
                return app::LifecycleStatus::Failure(FailureMessage(resourceFailure.message, "material resource resolver shutdown failed"));

            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (!m_textureResidency.Shutdown(&textureResidencyFailure))
                return app::LifecycleStatus::Failure(FailureMessage(textureResidencyFailure.message, "texture residency runtime shutdown failed"));

            rendering::MaterialProgramLayoutFailure layoutFailure;
            if (!m_materialProgramLayouts.Shutdown(&layoutFailure))
                return app::LifecycleStatus::Failure(FailureMessage(layoutFailure.message, "material program-layout registry shutdown failed"));

            rendering::RenderPhaseFailure phaseFailure;
            if (!m_renderPhases.Shutdown(&phaseFailure))
                return app::LifecycleStatus::Failure(FailureMessage(phaseFailure.message, "render phase registry shutdown failed"));

            if (const char* const retirementFailure = DrainShutdownGpuSceneRetirements())
                return app::LifecycleStatus::Failure(retirementFailure);
            rendering::GpuSceneRuntimeFailure gpuSceneFailure;
            if (!m_gpuSceneRuntime.Shutdown({}, &gpuSceneFailure))
                return app::LifecycleStatus::Failure(FailureMessage(gpuSceneFailure.message, "GPU Scene runtime shutdown failed"));

            rendering::RenderFlowResourceFailure frameResourceFailure;
            if (!m_frameRenderer.ClearResourceAllocatorCaches(&frameResourceFailure))
                return app::LifecycleStatus::Failure(FailureMessage(frameResourceFailure.message, "frame-resource allocator cache clear failed"));

            m_samplerDescriptors.Reset();
            m_resourceDescriptors.Reset();
            if (!rhi::RetireResources(&rhiFailure) || !rhi::FlushRetiredResources(&rhiFailure))
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "RHI resource retirement failed during renderer shutdown"));
            const rendering::RenderFlowResourceAllocatorStats frameResourceStats = m_frameRenderer.GetResourceAllocatorStats();
            if (frameResourceStats.chargedNativeBytes != 0 || frameResourceStats.pendingRetirementResources != 0 || frameResourceStats.pendingNativeDestructionResources != 0 ||
                frameResourceStats.pendingRetirementPlacedHeaps != 0 || frameResourceStats.pendingNativeDestructionPlacedHeaps != 0)
                return app::LifecycleStatus::Failure("frame-resource allocator retained native ownership after the RHI retirement flush");
            if (!m_frameRenderer.ShutdownResourceAllocator(&frameResourceFailure))
                return app::LifecycleStatus::Failure(FailureMessage(frameResourceFailure.message, "frame-resource allocator shutdown failed"));
            if (!rhi::Shutdown(&rhiFailure))
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "RHI shutdown failed"));

            m_config.backendFactory.destroy(m_backend);
            m_backend = nullptr;
            return app::LifecycleStatus::Success();
        }

        void AbandonRendererDevice() noexcept
        {
            if (m_backend == nullptr)
                return;
            AbandonRendererOwners();
            m_samplerDescriptors.Reset();
            m_resourceDescriptors.Reset();
            static_cast<void>(rhi::AbandonDevice());
            m_config.backendFactory.destroy(m_backend);
            m_backend = nullptr;
        }

        void AbandonRendererOwners() noexcept
        {
            if (m_deviceUnavailable)
                return;
            m_deviceUnavailable = true;
            if (m_materialBindings.IsInitialized())
            {
                static_cast<void>(m_materialBindings.AbandonDevice());
                static_cast<void>(m_materialBindings.Shutdown());
            }
            if (m_meshResidency.IsInitialized())
                static_cast<void>(m_meshResidency.AbandonDevice());
            if (m_materialResidency.IsInitialized())
            {
                static_cast<void>(m_materialResidency.AbandonDevice());
                static_cast<void>(m_materialResidency.Shutdown());
            }
            if (m_materialPipelines.IsInitialized())
            {
                m_frameRenderer.ClearPipelines();
                static_cast<void>(m_materialPipelines.InvalidateAll());
                m_materialPipelines.WaitIdle();
                static_cast<void>(m_materialPipelines.Shutdown());
            }
            for (rhi::BindingLayout& layout : m_materialBindingLayouts)
                layout.Reset();
            if (m_materialMaterializer.IsInitialized())
            {
                static_cast<void>(m_materialMaterializer.AbandonDevice());
                static_cast<void>(m_materialMaterializer.Shutdown());
            }
            if (m_materialResources.IsInitialized())
            {
                static_cast<void>(m_materialResources.AbandonDevice());
                static_cast<void>(m_materialResources.Shutdown());
            }
            if (m_textureResidency.IsInitialized())
                static_cast<void>(m_textureResidency.AbandonDevice());
            if (m_materialProgramLayouts.IsInitialized())
                static_cast<void>(m_materialProgramLayouts.Shutdown());
            if (m_renderPhases.IsInitialized())
                static_cast<void>(m_renderPhases.Shutdown());
            if (m_gpuSceneRuntime.IsInitialized())
                static_cast<void>(m_gpuSceneRuntime.AbandonDevice());
            static_cast<void>(m_frameRenderer.ShutdownResourceAllocator());
        }

        void RollbackRendererDevice() noexcept
        {
            if (m_backend == nullptr)
                return;
            static_cast<void>(rhi::WaitIdle());
            if (m_materialBindings.IsInitialized())
                static_cast<void>(m_materialBindings.Shutdown());
            if (m_meshResidency.IsInitialized())
                static_cast<void>(m_meshResidency.Shutdown());
            if (m_materialResidency.IsInitialized())
                static_cast<void>(m_materialResidency.Shutdown());
            if (m_materialPipelines.IsInitialized())
            {
                m_frameRenderer.ClearPipelines();
                m_materialPipelines.WaitIdle();
                static_cast<void>(m_materialPipelines.Shutdown());
            }
            for (rhi::BindingLayout& layout : m_materialBindingLayouts)
                layout.Reset();
            if (m_materialMaterializer.IsInitialized())
                static_cast<void>(m_materialMaterializer.Shutdown());
            if (m_materialResources.IsInitialized())
                static_cast<void>(m_materialResources.Shutdown());
            if (m_textureResidency.IsInitialized())
                static_cast<void>(m_textureResidency.Shutdown());
            if (m_materialProgramLayouts.IsInitialized())
                static_cast<void>(m_materialProgramLayouts.Shutdown());
            if (m_renderPhases.IsInitialized())
                static_cast<void>(m_renderPhases.Shutdown());
            if (m_gpuSceneRuntime.IsInitialized())
            {
                static_cast<void>(DrainShutdownGpuSceneRetirements());
                static_cast<void>(m_gpuSceneRuntime.Shutdown({}));
            }
            static_cast<void>(m_frameRenderer.ShutdownResourceAllocator());
            m_samplerDescriptors.Reset();
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
            const rhi::DeviceState deviceState = m_backend != nullptr ? rhi::TestDeviceState() : rhi::DeviceState::Unknown;
            if (deviceState == rhi::DeviceState::Removed || deviceState == rhi::DeviceState::ResetRequired)
            {
                AbandonRendererOwners();
                return engine::FrameParticipantStatus::Failure("renderer device became unavailable");
            }
            if (m_deviceUnavailable)
                return engine::FrameParticipantStatus::Failure("renderer device is unavailable");
            rendering::GpuSceneRuntimeFailure gpuSceneFailure;
            if (m_gpuSceneRuntime.IsInitialized() && !m_gpuSceneRuntime.ResolveContributions(&gpuSceneFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(gpuSceneFailure.message, "GPU Scene contribution resolution failed"));
            rendering::RenderSceneFailure sceneBindingFailure;
            if (!m_scenes.ResolveMeshBindings(256u, &sceneBindingFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(sceneBindingFailure.message, "mesh scene-binding acceptance failed"));
            if (m_commands.ConsumeExecutionFailure(failure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(failure.message, "asynchronous rendering execution failed"));
            if (m_gpuSceneRuntime.IsInitialized() && m_gpuSceneRuntime.ConsumePublicationFailure(gpuSceneFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(gpuSceneFailure.message, "asynchronous GPU Scene publication failed"));
            if (m_gpuSceneRuntime.IsInitialized())
            {
                rendering::GpuSceneLifetimeFailure lifetimeFailure;
                static_cast<void>(m_gpuSceneRuntime.GetLifetime().Collect(&lifetimeFailure));
                if (lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::None)
                    return engine::FrameParticipantStatus::Failure(FailureMessage(lifetimeFailure.message, "GPU Scene retirement collection failed"));
            }
            if (m_meshResidency.IsInitialized())
            {
                rendering::MeshResidencyFailure meshFailure;
                static_cast<void>(m_meshResidency.CollectRetirements(&meshFailure));
                if (meshFailure.code != rendering::MeshResidencyFailureCode::None)
                    return engine::FrameParticipantStatus::Failure(FailureMessage(meshFailure.message, "mesh retirement collection failed"));
            }
            rendering::TextureResidencyRuntimeFailure textureRetirementFailure;
            if (m_textureResidency.IsInitialized())
            {
                static_cast<void>(m_textureResidency.CollectRetirements(&textureRetirementFailure));
                if (textureRetirementFailure.code != rendering::TextureResidencyRuntimeFailureCode::None)
                    return engine::FrameParticipantStatus::Failure(FailureMessage(textureRetirementFailure.message, "texture retirement collection failed"));
            }
            rhi::Failure rhiFailure;
            if (m_backend != nullptr && !rhi::RetireResources(&rhiFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(rhiFailure.message, "retired native resource collection failed"));
            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (m_textureResidency.IsInitialized() && !m_textureResidency.Tick(&textureResidencyFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(textureResidencyFailure.message, "texture residency progress failed"));
            rendering::MaterialResidencyRuntimeFailure materialResidencyFailure;
            if (m_materialResidency.IsInitialized() && !m_materialResidency.Update(&materialResidencyFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(materialResidencyFailure.message, "material residency progress failed"));
            rendering::MeshResidencyFailure meshResidencyFailure;
            if (m_meshResidency.IsInitialized() && !m_meshResidency.Tick(&meshResidencyFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(meshResidencyFailure.message, "mesh residency progress failed"));
            rendering::MaterialSceneBindingFailure bindingFailure;
            if (m_materialBindings.IsInitialized() && !m_materialBindings.Update(&bindingFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(bindingFailure.message, "material scene-binding progress failed"));
            // FlushPreviousFrameProcessing above joins every retained-frame
            // submission. Snapshot actual RHI receipts after residency producers
            // progress; do not fabricate work for a queue that has never run.
            if (m_gpuSceneRuntime.IsInitialized())
            {
                rhi::ResidencyFenceSet cutover;
                if (!rhi::GetSubmittedResidencyFences(cutover))
                    return engine::FrameParticipantStatus::Failure("submitted renderer work has no complete retirement receipt");
                engine::RenderingRetirementFailure retirementFailure;
                if (!SealResidencyRetirements(cutover, &retirementFailure))
                    return engine::FrameParticipantStatus::Failure(FailureMessage(retirementFailure.message, "normal renderer residency retirement sealing failed"));
            }
            return engine::FrameParticipantStatus::Success();
        }

        engine::FrameParticipantStatus DispatchFrameTick(const engine::FrameContext& context) noexcept
        {
            if (m_deviceUnavailable)
                return engine::FrameParticipantStatus::Failure("renderer device is unavailable");
            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (m_textureResidency.IsInitialized() && !m_textureResidency.StageGpuSceneContribution(m_gpuSceneRuntime, &textureResidencyFailure))
                return engine::FrameParticipantStatus::Failure(FailureMessage(textureResidencyFailure.message, "texture GPU Scene contribution staging failed"));
            rendering::MeshResidencyFailure meshFailure;
            if (m_meshResidency.IsInitialized() && !m_meshResidency.StageGpuSceneContribution(&meshFailure))
            {
                rendering::GpuSceneRuntimeFailure gpuSceneFailure;
                static_cast<void>(m_gpuSceneRuntime.ResolveContributions(&gpuSceneFailure));
                return engine::FrameParticipantStatus::Failure(FailureMessage(meshFailure.message, "mesh GPU Scene contribution staging failed"));
            }
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
        vanguard::containers::DynamicArray<vanguard::pipelines::RendererCatalogEntry> m_bootstrapRecords{memory::pools::Rendering::GetInstance()};
        vanguard::containers::DynamicArray<engine::RenderingServiceConfig::Shader> m_bootstrapShaders{memory::pools::Rendering::GetInstance()};
        vanguard::containers::DynamicArray<engine::RenderingServiceConfig::Pipeline> m_bootstrapPipelines{memory::pools::Rendering::GetInstance()};
        vanguard::containers::DynamicArray<rhi::BindingLayoutEntry> m_bootstrapConstants{memory::pools::Rendering::GetInstance()};
        vanguard::containers::DynamicArray<rhi::BindingLayoutDesc> m_bootstrapLayouts{memory::pools::Rendering::GetInstance()};
        rendering::RenderSceneManager m_scenes;
        rendering::RenderCameraStorage m_cameras;
        rhi::IBackend* m_backend = nullptr;
        bool m_deviceUnavailable = false;
        rhi::DescriptorDomain m_resourceDescriptors;
        rhi::DescriptorDomain m_samplerDescriptors;
        rendering::GpuSceneRuntime m_gpuSceneRuntime;
        rendering::MaterialProgramLayoutRegistry m_materialProgramLayouts;
        rendering::RenderPhaseRegistry m_renderPhases;
        rendering::MeshResidencyManager m_meshResidency;
        rendering::TextureResidencyRuntime m_textureResidency;
        rendering::MaterialResourceResolver m_materialResources;
        rendering::MaterialMaterializer m_materialMaterializer;
        rendering::PipelineCache m_materialPipelines;
        rhi::BindingLayout m_materialBindingLayouts[rhi::MaximumBindingLayoutsPerPipeline];
        rendering::MaterialResidencyRuntime m_materialResidency;
        rendering::MaterialSceneBindingBridge m_materialBindings;
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
        bool RegisterRenderingServiceInternal(application::EngineHost& host, const RenderingServiceConfig& config, application::HostFailure* const failure) noexcept
        {
            using Profile = application::ApplicationProfile;
            const application::ServiceDependency dependencies[]{{JobsServiceId, application::DependencyKind::Required},
                                                                {FramePipelineServiceId, application::DependencyKind::Required},
                                                                {ResourcesServiceId, application::DependencyKind::Required},
                                                                {ResourceStreamingServiceId, application::DependencyKind::Optional},
                                                                {config.rendererCatalogSourceService, application::DependencyKind::Required}};
            constexpr application::CapabilityId capabilities[]{RenderingCapabilityId};
            application::ServiceDescriptor descriptor;
            descriptor.id = RenderingServiceId;
            descriptor.name = "rendering";
            descriptor.profiles = Profile::Runtime | Profile::Editor | Profile::Tool;
            descriptor.scope = application::ServiceScope::Process;
            descriptor.affinity = application::ThreadAffinity::MainThread;
            descriptor.dependencies = {dependencies, config.rendererCatalogSourceService != application::InvalidServiceId ? 5u : 4u};
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

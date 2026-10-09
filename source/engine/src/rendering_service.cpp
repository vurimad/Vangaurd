#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/rendering_service_detail.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/frame_renderer.hpp>
#include <vanguard/rendering/geometry_graph_nodes.hpp>
#include <vanguard/rendering/material_program_layout.hpp>
#include <vanguard/rendering/renderer_feature_programs.hpp>
#include <vanguard/rendering/renderer_draw_contract.hpp>
#include <vanguard/pipelines/renderer_catalog.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/system/time.hpp>

#include <algorithm>
#include <new>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace memory = vanguard::memory;
    namespace rendering = vanguard::rendering;
    namespace rhi = vanguard::rhi;

    inline constexpr engine::FrameParticipantId RenderingPreviousFrameCompletionParticipantId = 0x72656e6470727601ull;
    inline constexpr vanguard::u64 AutomaticFrameResourceHardCeilingBytes = 1024ull * 1024ull * 1024ull;

    [[nodiscard]] constexpr vanguard::u64 Minimum(const vanguard::u64 left, const vanguard::u64 right) noexcept
    {
        return left < right ? left : right;
    }

    struct ResolvedFrameResourceBudget final
    {
        rendering::RenderFlowResourceAllocatorConfig config;
        vanguard::u64 localBudgetBytes = 0;
        bool localBudgetAvailable = false;
    };

    [[nodiscard]] ResolvedFrameResourceBudget ResolveFrameResourceBudget(const rendering::RenderFlowResourceAllocatorConfig& requested,
                                                                         const bool automatic) noexcept
    {
        ResolvedFrameResourceBudget resolved{requested};
        if (!automatic)
            return resolved;

        vanguard::u64 hardCeiling = AutomaticFrameResourceHardCeilingBytes;
        if (rhi::GetCapabilities().memoryBudgetQueries)
        {
            rhi::MemoryBudgetSnapshot budget;
            rhi::Failure ignored;
            if (rhi::QueryMemoryBudget(rhi::MemorySegment::Local, budget, &ignored) && budget.budget != 0)
            {
                resolved.localBudgetBytes = budget.budget;
                resolved.localBudgetAvailable = true;
                const vanguard::u64 quarterBudget = budget.budget / 4u;
                if (quarterBudget != 0)
                    hardCeiling = Minimum(hardCeiling, quarterBudget);
            }
        }

        resolved.config.hardNativeByteLimit = Minimum(resolved.config.hardNativeByteLimit, hardCeiling);
        const vanguard::u64 softCeiling = resolved.config.hardNativeByteLimit - resolved.config.hardNativeByteLimit / 4u;
        resolved.config.textureSoftTargetBytes = Minimum(resolved.config.textureSoftTargetBytes, softCeiling);
        resolved.config.bufferSoftTargetBytes = Minimum(resolved.config.bufferSoftTargetBytes, softCeiling);
        return resolved;
    }

    [[nodiscard]] const char* FailureMessage(const char* const message, const char* const fallback) noexcept
    {
        return message != nullptr && message[0] != '\0' ? message : fallback;
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rhi::Failure& failure) noexcept
    {
        return failure.code == rhi::FailureCode::DeviceLost;
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::GpuSceneTablesFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.rhiFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::GpuSceneLifetimeFailure& failure) noexcept
    {
        // Collect() cannot make forward progress when a sealed retirement epoch
        // contains a stale allocation; that epoch remains at the collection head.
        return failure.code == rendering::GpuSceneLifetimeFailureCode::InvalidState ||
               RequiresRendererAbandonment(failure.tableFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::GpuSceneUploadFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.rhiFailure) || RequiresRendererAbandonment(failure.lifetimeFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::GpuSceneDefinitionFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.lifetimeFailure) || RequiresRendererAbandonment(failure.uploadFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::TextureResidencyFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.rhiFailure) || RequiresRendererAbandonment(failure.lifetimeFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::TextureUploadFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.rhiFailure) || RequiresRendererAbandonment(failure.residencyFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::GpuSceneRuntimeFailure& failure) noexcept
    {
        return failure.requiresRendererAbandonment || RequiresRendererAbandonment(failure.uploadFailure) ||
               RequiresRendererAbandonment(failure.lifetimeFailure) || RequiresRendererAbandonment(failure.tablesFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::TextureResidencyRuntimeFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.residencyFailure) || RequiresRendererAbandonment(failure.uploadFailure) ||
               RequiresRendererAbandonment(failure.gpuSceneFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::MaterialResourceResolverFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.rhiFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::MaterialMaterializerFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.resourceFailure) || RequiresRendererAbandonment(failure.definitionFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::MaterialResidencyRuntimeFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.rhiFailure) || RequiresRendererAbandonment(failure.materializationFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::GeometryAllocatorFailure& failure) noexcept
    {
        // GeometryAllocator::Collect leaves a stale sealed retirement epoch in
        // place on InvalidState, so repeating collection cannot repair ownership.
        return failure.code == rendering::GeometryAllocatorFailureCode::InvalidState ||
               RequiresRendererAbandonment(failure.rhiFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::GeometryUploadFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.rhiFailure) || RequiresRendererAbandonment(failure.allocatorFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::MeshGeometryUploadFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.allocatorFailure) || RequiresRendererAbandonment(failure.uploadFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::MeshLodGeometryUploadFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.preparationFailure) || RequiresRendererAbandonment(failure.submissionFailure) ||
               RequiresRendererAbandonment(failure.allocatorFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::MeshLodDefinitionFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.definitionFailure) || RequiresRendererAbandonment(failure.allocatorFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::MeshResidencyFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.allocatorFailure) || RequiresRendererAbandonment(failure.uploadFailure) ||
               RequiresRendererAbandonment(failure.lodUploadFailure) || RequiresRendererAbandonment(failure.lodDefinitionFailure) ||
               RequiresRendererAbandonment(failure.materialFailure) || RequiresRendererAbandonment(failure.renderableDefinitionFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const rendering::MaterialSceneBindingFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.residencyFailure);
    }

    [[nodiscard]] bool RequiresRendererAbandonment(const engine::RenderingRetirementFailure& failure) noexcept
    {
        return RequiresRendererAbandonment(failure.materialFailure) || RequiresRendererAbandonment(failure.textureFailure) ||
               RequiresRendererAbandonment(failure.meshFailure) || RequiresRendererAbandonment(failure.gpuSceneFailure);
    }

    class ManagedRenderingService final : public engine::RenderingService
    {
    public:
        explicit ManagedRenderingService(const engine::RenderingServiceConfig& config) noexcept
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
        [[nodiscard]] engine::RenderingDeviceInfo GetDeviceInfo() const noexcept override
        {
            engine::RenderingDeviceInfo info;
            if (m_backend == nullptr || m_deviceUnavailable || !rhi::IsInitialized())
                return info;
            const rhi::Capabilities& capabilities = rhi::GetCapabilities();
            info.available = true;
            info.backend = capabilities.backend;
            info.dedicatedVideoMemory = capabilities.dedicatedVideoMemory;
            const size_t bytes = std::min(std::strlen(capabilities.adapterName), sizeof(info.adapterName) - 1u);
            if (bytes != 0u)
                std::memcpy(info.adapterName, capabilities.adapterName, bytes);
            info.adapterName[bytes] = '\0';
            return info;
        }
        [[nodiscard]] rendering::RenderPhaseSet GetDefaultCameraPhases() const noexcept override
        {
            return m_drawInterfaceAvailable ? m_defaultCameraPhases : rendering::RenderPhaseSet{};
        }
        [[nodiscard]] bool IsFrameDiagnosticsEnabled() const noexcept override
        {
            return m_config.enableFrameDiagnostics;
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
        [[nodiscard]] bool IsGpuTimingAvailable() const noexcept override
        {
            return m_frameRenderer.IsGpuTimingAvailable();
        }
        [[nodiscard]] rendering::FrameGpuTimingPoll PollGpuTimings(rendering::FrameGpuTimingSample& output,
                                                                    rhi::Failure* const failure) noexcept override
        {
            return m_frameRenderer.PollGpuTimings(output, failure);
        }
        [[nodiscard]] vanguard::u64 GetDroppedGpuTimingFrames() const noexcept override
        {
            return m_frameRenderer.GetDroppedGpuTimingFrames();
        }

        [[nodiscard]] engine::RenderingSessionDrainResult ProgressSessionDrain(const rendering::RenderSceneHandle scene,
                                                                               engine::RenderingProgressFailure* const failure,
                                                                               const bool requireFrameTick) noexcept override
        {
            if (failure != nullptr)
                *failure = {};
            engine::RenderingSessionDrainResult result;
            result.rendererProgressEpoch = m_rendererProgressEpoch;
            if (!vanguard::concurrency::IsMainThread())
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::WrongThread;
                    failure->message = "renderer session drain must run on the main thread";
                }
                result.status = engine::RenderingSessionDrainStatus::Failed;
                return result;
            }

            // Ordinary FramePipeline participants can fail before the application
            // transitions to Exiting. Preserve that exact typed cause across the
            // FrameParticipantStatus string boundary and transfer it once to the
            // session drain. GameWorld/WorldSession then own the first-fault latch;
            // later polls continue renderer cleanup instead of replaying this report.
            engine::RenderingProgressFailure participantFailure;
            if (m_participantFailure.Consume(participantFailure))
            {
                if (failure != nullptr)
                    *failure = participantFailure;
                result.status = engine::RenderingSessionDrainStatus::Failed;
                return result;
            }
            const bool hasScene = scene != rendering::InvalidRenderSceneHandle;
            if (hasScene && (!scene.IsValid() || !m_scenes.IsAlive(scene)))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::InvalidScene;
                    failure->message = "renderer session drain received an invalid or stale RenderScene";
                }
                result.status = engine::RenderingSessionDrainStatus::Failed;
                return result;
            }

            engine::RenderingProgressFailure progressFailure;
            if (!ProgressRendererOwners(&progressFailure))
            {
                if (failure != nullptr)
                    *failure = progressFailure;
                result.status = engine::RenderingSessionDrainStatus::Failed;
                return result;
            }

            bool sceneNeedsFrameTick = false;
            bool sceneHasPendingBindings = false;
            if (hasScene)
            {
                rendering::RenderSceneSnapshot sceneState;
                if (!m_scenes.GetSnapshot(scene, sceneState))
                {
                    if (failure != nullptr)
                    {
                        failure->code = engine::RenderingProgressFailureCode::InvalidScene;
                        failure->message = "renderer session drain received an invalid or stale RenderScene";
                    }
                    result.status = engine::RenderingSessionDrainStatus::Failed;
                    return result;
                }
                if (sceneState.framePrepared)
                {
                    if (failure != nullptr)
                    {
                        failure->code = engine::RenderingProgressFailureCode::SceneFailure;
                        failure->message = "renderer session drain found a prepared RenderScene after joining the CPU chain";
                        failure->sceneFailure = {rendering::RenderSceneFailureCode::InvalidState, scene, {},
                                                 "RenderScene remained prepared after renderer CPU-chain join"};
                    }
                    result.status = engine::RenderingSessionDrainStatus::Failed;
                    return result;
                }
                sceneNeedsFrameTick = sceneState.pendingProxyMutations != 0;
                sceneHasPendingBindings = sceneState.pendingMeshBindings != 0;

                if (m_gpuSceneRuntime.IsInitialized())
                {
                    bool publicationPending = false;
                    rendering::RenderSceneGpuFailure publicationFailure;
                    if (!m_gpuSceneRuntime.GetScenePublisher().HasPendingPublicationWork(scene, publicationPending, &publicationFailure))
                    {
                        if (failure != nullptr)
                        {
                            failure->code = engine::RenderingProgressFailureCode::ScenePublicationFailure;
                            failure->message = FailureMessage(publicationFailure.message, "RenderScene GPU publication state is unavailable");
                            failure->scenePublicationFailure = publicationFailure;
                        }
                        result.status = engine::RenderingSessionDrainStatus::Failed;
                        return result;
                    }
                    sceneNeedsFrameTick |= publicationPending;
                }
            }

            sceneNeedsFrameTick |= hasScene && requireFrameTick;
            const rendering::RenderSceneHandle sceneStorage[] = {scene};
            const vanguard::containers::ArraySpan<const rendering::RenderSceneHandle> scenes =
                sceneNeedsFrameTick ? vanguard::containers::ArraySpan<const rendering::RenderSceneHandle>(sceneStorage, 1u)
                                    : vanguard::containers::ArraySpan<const rendering::RenderSceneHandle>{};
            bool frameTickDispatched = false;
            if (!DispatchRendererFrameTick(scenes, sceneNeedsFrameTick, frameTickDispatched, &progressFailure))
            {
                if (failure != nullptr)
                    *failure = progressFailure;
                result.status = engine::RenderingSessionDrainStatus::Failed;
                result.rendererProgressEpoch = m_rendererProgressEpoch;
                return result;
            }

            result.rendererProgressEpoch = m_rendererProgressEpoch;
            result.frameTickDispatched = frameTickDispatched;
            result.status = frameTickDispatched || sceneHasPendingBindings ? engine::RenderingSessionDrainStatus::Pending
                                                                          : engine::RenderingSessionDrainStatus::Complete;
            return result;
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
            m_framePipeline = framePipeline;
            m_diagnosticGeometrySamples = 0;
            m_latestGpuTiming = {};
            m_hasLatestGpuTiming = false;
            m_rendererProgressEpoch = 0;
            m_participantFailure.Reset();

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
            engine::FrameParticipantDescriptor previousFrameCompletionParticipant;
            previousFrameCompletionParticipant.id = RenderingPreviousFrameCompletionParticipantId;
            previousFrameCompletionParticipant.name = "rendering.previousFrameCompletion";
            // Keep the previous renderer CPU-chain join ahead of FixedSimulation/Simulation,
            // where camera and RenderScene mutation can require quiescent renderer-owned state,
            // but do not stall platform/input/BeginFrame work on presentation and terminal cleanup.
            previousFrameCompletionParticipant.phase = engine::FramePhase::PreSimulation;
            previousFrameCompletionParticipant.profiles = renderingProfiles;
            previousFrameCompletionParticipant.affinity = app::ThreadAffinity::MainThread;
            previousFrameCompletionParticipant.execute = &ExecutePreviousFrameCompletion;
            previousFrameCompletionParticipant.userData = this;

            engine::FrameFailure frameFailure;
            if (!framePipeline->RegisterParticipant(previousFrameCompletionParticipant, &frameFailure))
            {
                static_cast<void>(m_viewports.Shutdown());
                static_cast<void>(m_commands.Shutdown());
                static_cast<void>(m_cameras.Shutdown());
                RollbackRendererDevice();
                static_cast<void>(m_scenes.Shutdown());
                return app::LifecycleStatus::Failure(FailureMessage(frameFailure.message, "Rendering previous-frame completion registration failed"));
            }

            engine::FrameParticipantDescriptor updateParticipant;
            updateParticipant.id = engine::RenderingUpdateFrameParticipantId;
            updateParticipant.name = "rendering.updateBoundary";
            updateParticipant.phase = engine::FramePhase::RenderUpdate;
            updateParticipant.profiles = renderingProfiles;
            updateParticipant.affinity = app::ThreadAffinity::MainThread;
            updateParticipant.execute = &ExecuteRenderUpdate;
            updateParticipant.userData = this;

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
            // Final process-service teardown is the one place where the renderer may wait for the device. World/session replacement remains non-blocking and uses ProgressSessionDrain instead.
            if (const char* const ownerDrainFailure = DrainRendererOwnersForQuiesce())
                return app::LifecycleStatus::Failure(ownerDrainFailure);
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
                // Final queue/native retirement must complete during quiesce so a dependent
                // composition owner can consume the resulting generation evidence in Drain.
                // Delivery itself is checked in OnDrain, after dependants have had that chance.
                if (const char* const textureRetirementFailure = FinalizeTextureGenerationRetirements(false))
                    return app::LifecycleStatus::Failure(textureRetirementFailure);
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

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            if (m_textureResidency.IsInitialized() && m_textureResidency.GetStats().residency.pendingGenerationRetirements != 0)
                return app::LifecycleStatus::Failure("RenderingService drain has undelivered texture generation-retirement evidence");
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
            app::LifecycleStatus shutdownStatus = ShutdownRendererDevice();
            rendering::RenderCameraFailure cameraFailure;
            if (!m_cameras.Shutdown(&cameraFailure) && shutdownStatus)
                shutdownStatus = app::LifecycleStatus::Failure(FailureMessage(cameraFailure.message, "RenderCameraStorage shutdown failed"));
            rendering::RenderSceneFailure sceneFailure;
            if (!m_scenes.Shutdown(&sceneFailure) && shutdownStatus)
                shutdownStatus = app::LifecycleStatus::Failure(FailureMessage(sceneFailure.message, "RenderSceneManager shutdown failed"));
            m_framePipeline = nullptr;
            return shutdownStatus;
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

        app::LifecycleStatus LoadRendererResourceCatalog(engine::ResourceStreamingService* const service) noexcept
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

        app::LifecycleStatus InitializeRendererDrawInterface() noexcept
        {
            const rhi::ShaderStageMask staticSurfaceStages = rhi::ShaderStageBit(rhi::ShaderStage::Vertex) | rhi::ShaderStageBit(rhi::ShaderStage::Pixel);
            const rendering::RenderPhaseId depthPrepass = m_renderPhases.Find(rendering::standardRenderPhases::DepthPrepass);
            const rendering::RenderPhaseId opaque = m_renderPhases.Find(rendering::standardRenderPhases::Opaque);
            if (!depthPrepass.IsValid() || !opaque.IsValid())
                return app::LifecycleStatus::Failure("renderer draw interface requires registered depth-prepass and opaque phases");
            if (m_config.resourceDescriptors.kind != rhi::DescriptorDomainKind::Resources || m_config.samplerDescriptors.kind != rhi::DescriptorDomainKind::Samplers ||
                (m_config.resourceDescriptors.visibility & staticSurfaceStages) != staticSurfaceStages || (m_config.samplerDescriptors.visibility & staticSurfaceStages) != staticSurfaceStages)
                return app::LifecycleStatus::Failure("renderer draw interface descriptor-domain types do not match renderer configuration");

            if (m_config.materialBindingLayouts.Empty())
            {
                const rhi::BindingLayoutDesc drawLayout = rendering::GetStaticSurfaceDrawBindingLayout();
                m_materialBindingLayouts[0] = rhi::BindingLayout(rhi::AdoptReference, rhi::RequestBindingLayout(drawLayout));
                if (!m_materialBindingLayouts[0])
                    return app::LifecycleStatus::Failure("static-surface draw binding layout creation failed");
                m_materialBindingLayoutCount = 1;
            }
            else
            {
                for (vanguard::u32 index = 0; index < m_config.materialBindingLayouts.Size(); ++index)
                {
                    m_materialBindingLayouts[index] = rhi::BindingLayout(rhi::AdoptReference, rhi::RequestBindingLayout(m_config.materialBindingLayouts[index]));
                    if (!m_materialBindingLayouts[index])
                    {
                        ResetRendererDrawInterface();
                        return app::LifecycleStatus::Failure("material draw binding layout creation failed");
                    }
                }
                m_materialBindingLayoutCount = m_config.materialBindingLayouts.Size();
            }

            rendering::RenderPhaseSet defaultCameraPhases;
            if (!defaultCameraPhases.Add(depthPrepass) || !defaultCameraPhases.Add(opaque))
            {
                ResetRendererDrawInterface();
                return app::LifecycleStatus::Failure("renderer draw interface could not resolve default camera phases");
            }
            m_defaultCameraPhases = defaultCameraPhases;
            m_drawInterfaceAvailable = true;
            return app::LifecycleStatus::Success();
        }

        void ResetRendererDrawInterface() noexcept
        {
            m_drawInterfaceAvailable = false;
            m_defaultCameraPhases.Clear();
            m_materialBindingLayoutCount = 0;
            for (rhi::BindingLayout& layout : m_materialBindingLayouts)
                layout.Reset();
        }

        app::LifecycleStatus InitializeRendererDevice(engine::ResourceStreamingService* const streamingService) noexcept
        {
            m_deviceUnavailable = false;
            ResetRendererDrawInterface();
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

            const app::LifecycleStatus catalogStatus = LoadRendererResourceCatalog(streamingService);
            if (!catalogStatus)
                return catalogStatus;

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
            const ResolvedFrameResourceBudget resolvedFrameResources = ResolveFrameResourceBudget(m_config.frameResources, m_config.enableAutomaticFrameResourceBudget);
            auto frameResources = resolvedFrameResources.config;
            frameResources.resourceDescriptors = m_resourceDescriptors.GetRef();
            const bool frameResourcesInitialized = m_frameRenderer.InitializeResourceAllocator(frameResources, &frameResourceFailure);
            if (!frameResourcesInitialized)
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(frameResourceFailure.message, "frame-resource allocator initialization failed"));
            }
            if (m_config.enableFrameDiagnostics)
                VG_LOG_INFO(vanguard::diagnostics::Category::Rendering,
                            "render diagnostics: frameResourceHardBytes=%llu frameResourceTextureSoftBytes=%llu frameResourceBufferSoftBytes=%llu localBudgetAvailable=%u localBudgetBytes=%llu",
                            static_cast<unsigned long long>(frameResources.hardNativeByteLimit), static_cast<unsigned long long>(frameResources.textureSoftTargetBytes),
                            static_cast<unsigned long long>(frameResources.bufferSoftTargetBytes), resolvedFrameResources.localBudgetAvailable ? 1u : 0u,
                            static_cast<unsigned long long>(resolvedFrameResources.localBudgetBytes));
            if (!m_frameRenderer.InitializeGpuTimings(&rhiFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "renderer GPU timing initialization failed"));
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
            const app::LifecycleStatus drawInterfaceStatus = InitializeRendererDrawInterface();
            if (!drawInterfaceStatus)
            {
                RollbackRendererDevice();
                return drawInterfaceStatus;
            }
            rendering::MaterialResidencyRuntimeFailure materialResidencyFailure;
            rhi::BindingLayoutRef materialLayouts[rhi::MaximumBindingLayoutsPerPipeline]{};
            for (vanguard::u32 index = 0; index < m_materialBindingLayoutCount; ++index)
                materialLayouts[index] = m_materialBindingLayouts[index].GetRef();
            const rendering::PipelineInterfaceResources materialInterfaces{{materialLayouts, m_materialBindingLayoutCount}, {materialDomains, 2}};
            if (!m_materialResidency.Initialize(m_materialMaterializer, m_materialPipelines, materialInterfaces, m_config.materialResidency, &materialResidencyFailure))
            {
                RollbackRendererDevice();
                return app::LifecycleStatus::Failure(FailureMessage(materialResidencyFailure.message, "material residency runtime initialization failed"));
            }
            rendering::MeshResidencyFailure meshResidencyFailure;
            if (!m_meshResidency.Initialize(m_gpuSceneRuntime, m_materialResidency, m_renderPhases,
                                            {rendering::StandardCameraMeshDrawPhases, rendering::StandardCameraMeshDrawPhaseCount},
                                            m_config.meshResidency, &meshResidencyFailure))
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

        [[nodiscard]] const char* FinalizeTextureGenerationRetirements(const bool requireDelivered = true) noexcept
        {
            if (!m_textureResidency.IsInitialized())
                return nullptr;
            rhi::ResidencyFenceSet cutover;
            if (!rhi::GetSubmittedResidencyFences(cutover))
                return "renderer shutdown has no complete texture-retirement receipt";
            rendering::TextureResidencyRuntimeFailure textureFailure;
            if (!m_textureResidency.SealRetirements(cutover, &textureFailure))
                return FailureMessage(textureFailure.message, "texture residency shutdown retirement sealing failed");
            rhi::Failure rhiFailure;
            if (!rhi::RetireResources(&rhiFailure) || !rhi::FlushRetiredResources(&rhiFailure))
                return FailureMessage(rhiFailure.message, "texture residency native retirement flush failed during renderer shutdown");
            static_cast<void>(m_textureResidency.CollectRetirements(&textureFailure));
            if (textureFailure.code != rendering::TextureResidencyRuntimeFailureCode::None)
                return FailureMessage(textureFailure.message, "texture residency shutdown retirement collection failed");
            if (requireDelivered && m_textureResidency.GetStats().residency.pendingGenerationRetirements != 0)
                return "RenderingService shutdown has undelivered texture generation-retirement evidence";
            return nullptr;
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
            if (!m_frameRenderer.ShutdownGpuTimings(&rhiFailure))
                return app::LifecycleStatus::Failure(FailureMessage(rhiFailure.message, "renderer GPU timing shutdown failed"));

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
            ResetRendererDrawInterface();

            rendering::MaterialMaterializerFailure materializerFailure;
            if (!m_materialMaterializer.Shutdown(&materializerFailure))
                return app::LifecycleStatus::Failure(FailureMessage(materializerFailure.message, "material materializer shutdown failed"));

            rendering::MaterialResourceResolverFailure resourceFailure;
            if (!m_materialResources.Shutdown(&resourceFailure))
                return app::LifecycleStatus::Failure(FailureMessage(resourceFailure.message, "material resource resolver shutdown failed"));

            if (const char* const textureRetirementFailure = FinalizeTextureGenerationRetirements())
            {
                // Host cleanup continues after lifecycle failures. Abandon the device only after
                // surfacing the lossless-handoff failure so backend-scoped observations cannot
                // outlive the RHI or be discarded as a successful graceful shutdown.
                AbandonRendererDevice();
                return app::LifecycleStatus::Failure(textureRetirementFailure);
            }
            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (!m_textureResidency.Shutdown(&textureResidencyFailure))
            {
                const char* const message = FailureMessage(textureResidencyFailure.message, "texture residency runtime shutdown failed");
                static_cast<void>(m_textureResidency.AbandonDevice());
                AbandonRendererDevice();
                return app::LifecycleStatus::Failure(message);
            }

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
            ResetRendererDrawInterface();
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
            m_frameRenderer.AbandonGpuTimings();
            static_cast<void>(m_frameRenderer.ShutdownResourceAllocator());
        }

        void RollbackRendererDevice() noexcept
        {
            if (m_backend == nullptr)
                return;
            static_cast<void>(rhi::WaitIdle());
            static_cast<void>(m_frameRenderer.ShutdownGpuTimings());
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
            ResetRendererDrawInterface();
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

        static engine::FrameParticipantStatus ExecutePreviousFrameCompletion(const engine::FrameContext&, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedRenderingService*>(userData);
            return service != nullptr ? service->CompletePreviousFrame() : engine::FrameParticipantStatus::Failure("RenderingService is unavailable");
        }

        static engine::FrameParticipantStatus ExecuteRenderUpdate(const engine::FrameContext&, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedRenderingService*>(userData);
            return service != nullptr ? service->RenderUpdate() : engine::FrameParticipantStatus::Failure("RenderingService is unavailable");
        }

        static engine::FrameParticipantStatus ExecuteRenderingFrameTick(const engine::FrameContext&, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedRenderingService*>(userData);
            return service != nullptr ? service->DispatchFrameTick() : engine::FrameParticipantStatus::Failure("RenderingService is unavailable");
        }

        [[nodiscard]] bool ResolvePreviousFrame(engine::RenderingProgressFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
            rendering::RenderCommandFailure commandFailure;
            if (!m_commands.FlushPreviousFrameProcessing(&commandFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::CommandFailure;
                    failure->message = FailureMessage(commandFailure.message, "previous CPU rendering chain flush failed");
                    failure->commandFailure = commandFailure;
                }
                return false;
            }
            // The joined CPU chain can publish a concrete asynchronous failure
            // before device-state polling observes a later removal/reset. Preserve
            // that first owner-reported cause; a subsequent stop poll will still
            // observe and terminalize device loss if it also occurred.
            if (m_commands.ConsumeExecutionFailure(commandFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::CommandFailure;
                    failure->message = FailureMessage(commandFailure.message, "asynchronous rendering execution failed");
                    failure->commandFailure = commandFailure;
                }
                return false;
            }
            rendering::GpuSceneRuntimeFailure gpuSceneFailure;
            if (m_gpuSceneRuntime.IsInitialized() && m_gpuSceneRuntime.ConsumePublicationFailure(gpuSceneFailure))
            {
                const bool abandonOwners = RequiresRendererAbandonment(gpuSceneFailure);
                if (abandonOwners)
                    AbandonRendererOwners();
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::GpuSceneFailure;
                    failure->message = FailureMessage(gpuSceneFailure.message, "asynchronous GPU Scene publication failed");
                    failure->ownersAbandoned = abandonOwners;
                    failure->gpuSceneFailure = gpuSceneFailure;
                }
                return false;
            }
            const rhi::DeviceState deviceState = m_backend != nullptr ? rhi::TestDeviceState() : rhi::DeviceState::Unknown;
            if (deviceState == rhi::DeviceState::Removed || deviceState == rhi::DeviceState::ResetRequired)
            {
                AbandonRendererOwners();
                if (failure != nullptr)
                    static_cast<void>(engine::detail::BuildDeviceUnavailableFailure(deviceState, *failure));
                return false;
            }
            if (m_deviceUnavailable)
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::DeviceUnavailable;
                    failure->message = "renderer device is unavailable";
                    failure->ownersAbandoned = true;
                }
                return false;
            }
            const bool contributionsResolved = !m_gpuSceneRuntime.IsInitialized() || m_gpuSceneRuntime.ResolveContributions(&gpuSceneFailure);
            if (!contributionsResolved)
            {
                const bool abandonOwners = RequiresRendererAbandonment(gpuSceneFailure);
                if (abandonOwners)
                    AbandonRendererOwners();
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::GpuSceneFailure;
                    failure->message = FailureMessage(gpuSceneFailure.message, "GPU Scene contribution resolution failed");
                    failure->ownersAbandoned = abandonOwners;
                    failure->gpuSceneFailure = gpuSceneFailure;
                }
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ResolvePreviousFrameOwners(engine::RenderingProgressFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};

            bool requiresTerminalResolution = false;
            rendering::RenderCommandFailure commandFailure;
            if (!m_commands.FlushPreviousFrameOwnerProcessing(requiresTerminalResolution, &commandFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::CommandFailure;
                    failure->message = FailureMessage(commandFailure.message, "previous renderer-owner CPU chain flush failed");
                    failure->commandFailure = commandFailure;
                }
                return false;
            }

            // Core/FrameTick failures retain the old full-drain semantics. Terminal-only
            // presentation/output failures are deliberately not consumed here even if that
            // work happened to finish early: doing so would make the Simulation failure point
            // scheduling-dependent. RenderUpdate owns the deterministic terminal boundary.
            if (requiresTerminalResolution)
                return ResolvePreviousFrame(failure);

            if (m_gpuSceneRuntime.IsInitialized() && m_gpuSceneRuntime.GetStats().publicationFailurePending)
                return ResolvePreviousFrame(failure);

            if (m_deviceUnavailable)
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::DeviceUnavailable;
                    failure->message = "renderer device is unavailable";
                    failure->ownersAbandoned = true;
                }
                return false;
            }

            // GPU Scene publication is renderer-owner state: Simulation may mutate
            // RenderScene proxies only after these accepted contributions return to
            // their producers. This is deliberately earlier than terminal Present.
            rendering::GpuSceneRuntimeFailure gpuSceneFailure;
            if (!m_gpuSceneRuntime.IsInitialized() || m_gpuSceneRuntime.ResolveContributions(&gpuSceneFailure))
                return true;

            // A contribution-resolution failure may require renderer abandonment.
            // Drain terminal output first so owner teardown cannot race the still-live
            // frame. Preserve the canonical command/publication-failure precedence.
            rendering::RenderCommandFailure terminalCommandFailure;
            if (!m_commands.FlushPreviousFrameProcessing(&terminalCommandFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::CommandFailure;
                    failure->message = FailureMessage(terminalCommandFailure.message, "previous CPU rendering chain flush failed after owner-resolution failure");
                    failure->commandFailure = terminalCommandFailure;
                }
                return false;
            }
            if (m_commands.ConsumeExecutionFailure(terminalCommandFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::CommandFailure;
                    failure->message = FailureMessage(terminalCommandFailure.message, "asynchronous rendering execution failed");
                    failure->commandFailure = terminalCommandFailure;
                }
                return false;
            }
            rendering::GpuSceneRuntimeFailure publicationFailure;
            if (m_gpuSceneRuntime.ConsumePublicationFailure(publicationFailure))
                gpuSceneFailure = publicationFailure;

            const bool abandonOwners = RequiresRendererAbandonment(gpuSceneFailure);
            if (abandonOwners)
                AbandonRendererOwners();
            if (failure != nullptr)
            {
                failure->code = engine::RenderingProgressFailureCode::GpuSceneFailure;
                failure->message = FailureMessage(gpuSceneFailure.message, "GPU Scene contribution resolution failed");
                failure->ownersAbandoned = abandonOwners;
                failure->gpuSceneFailure = gpuSceneFailure;
            }
            return false;
        }

        [[nodiscard]] bool ProgressRendererOwners(engine::RenderingProgressFailure* const failure) noexcept
        {
            if (!ResolvePreviousFrame(failure))
                return false;
            // ResolvePreviousFrame joins every retained-frame submission, so this snapshot is the exact
            // prior-reader cutover required before same-generation texture transitions may copy shared mips.
            // A later fresh snapshot still seals retirements after this frame's producer progress.
            rhi::ResidencyFenceSet priorReaderCutover;
            if (m_textureResidency.IsInitialized() && !rhi::GetSubmittedResidencyFences(priorReaderCutover))
            {
                AbandonRendererOwners();
                if (failure != nullptr)
                    engine::detail::BuildSubmittedReceiptCoverageFailure(*failure);
                return false;
            }
            rendering::RenderSceneFailure sceneBindingFailure;
            if (!m_scenes.ResolveMeshBindings(256u, &sceneBindingFailure))
            {
                // ResolveMeshBindings only reports InvalidState here after an accepted
                // binding receipt has become unreadable/stale. That transaction has no
                // forward-progress action left; terminalize the existing renderer owners
                // once, then let GameWorld perform CPU-only proxy/scene release.
                const bool abandonOwners = sceneBindingFailure.code == rendering::RenderSceneFailureCode::InvalidState;
                if (abandonOwners)
                    AbandonRendererOwners();
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::SceneFailure;
                    failure->message = FailureMessage(sceneBindingFailure.message, "mesh scene-binding acceptance failed");
                    failure->ownersAbandoned = abandonOwners;
                    failure->sceneFailure = sceneBindingFailure;
                }
                return false;
            }
            if (m_gpuSceneRuntime.IsInitialized())
            {
                rendering::GpuSceneLifetimeFailure lifetimeFailure;
                static_cast<void>(m_gpuSceneRuntime.GetLifetime().Collect(&lifetimeFailure));
                if (lifetimeFailure.code != rendering::GpuSceneLifetimeFailureCode::None)
                {
                    const bool abandonOwners = RequiresRendererAbandonment(lifetimeFailure);
                    if (abandonOwners)
                        AbandonRendererOwners();
                    if (failure != nullptr)
                    {
                        failure->code = engine::RenderingProgressFailureCode::GpuSceneLifetimeFailure;
                        failure->message = FailureMessage(lifetimeFailure.message, "GPU Scene retirement collection failed");
                        failure->ownersAbandoned = abandonOwners;
                        failure->gpuSceneLifetimeFailure = lifetimeFailure;
                    }
                    return false;
                }
            }
            if (m_meshResidency.IsInitialized())
            {
                rendering::MeshResidencyFailure meshFailure;
                static_cast<void>(m_meshResidency.CollectRetirements(&meshFailure));
                if (meshFailure.code != rendering::MeshResidencyFailureCode::None)
                {
                    const bool abandonOwners = RequiresRendererAbandonment(meshFailure);
                    if (abandonOwners)
                        AbandonRendererOwners();
                    if (failure != nullptr)
                    {
                        failure->code = engine::RenderingProgressFailureCode::MeshFailure;
                        failure->message = FailureMessage(meshFailure.message, "mesh retirement collection failed");
                        failure->ownersAbandoned = abandonOwners;
                        failure->meshFailure = meshFailure;
                    }
                    return false;
                }
            }
            rendering::TextureResidencyRuntimeFailure textureRetirementFailure;
            if (m_textureResidency.IsInitialized())
            {
                static_cast<void>(m_textureResidency.CollectRetirements(&textureRetirementFailure));
                if (textureRetirementFailure.code != rendering::TextureResidencyRuntimeFailureCode::None)
                {
                    const bool abandonOwners = RequiresRendererAbandonment(textureRetirementFailure);
                    if (abandonOwners)
                        AbandonRendererOwners();
                    if (failure != nullptr)
                    {
                        failure->code = engine::RenderingProgressFailureCode::TextureFailure;
                        failure->message = FailureMessage(textureRetirementFailure.message, "texture retirement collection failed");
                        failure->ownersAbandoned = abandonOwners;
                        failure->textureFailure = textureRetirementFailure;
                    }
                    return false;
                }
            }
            rhi::Failure rhiFailure;
            bool retiredResources = true;
            if (m_backend != nullptr)
            {
#if defined(VG_BUILD_PROFILE)
                const vanguard::u64 retireBegin = vanguard::system::GetMonotonicTicks();
#endif
                retiredResources = rhi::RetireResources(&rhiFailure);
#if defined(VG_BUILD_PROFILE)
                const vanguard::u64 retireTicks = vanguard::system::GetMonotonicTicks() - retireBegin;
                static vanguard::u64 accumulatedRetireTicks = 0;
                static vanguard::u64 maximumRetireTicks = 0;
                static vanguard::u64 retireWindow[256]{};
                static vanguard::u32 retireSamples = 0;
                accumulatedRetireTicks += retireTicks;
                maximumRetireTicks = retireTicks > maximumRetireTicks ? retireTicks : maximumRetireTicks;
                retireWindow[retireSamples++] = retireTicks;
                if (retireSamples == 256u)
                {
                    std::sort(retireWindow, retireWindow + 256u);
                    const double microsecondsPerTick = 1'000'000.0 / static_cast<double>(vanguard::system::GetMonotonicFrequency());
                    std::fprintf(stderr, "RF2_PROFILE_RHI_RETIRE samples=256 p50_us=%.3f p95_us=%.3f mean_us=%.3f max_us=%.3f\n",
                                 static_cast<double>(retireWindow[127]) * microsecondsPerTick,
                                 static_cast<double>(retireWindow[242]) * microsecondsPerTick,
                                 static_cast<double>(accumulatedRetireTicks) * microsecondsPerTick / 256.0,
                                 static_cast<double>(maximumRetireTicks) * microsecondsPerTick);
                    accumulatedRetireTicks = 0;
                    maximumRetireTicks = 0;
                    retireSamples = 0;
                }
#endif
            }
            if (!retiredResources)
            {
                const bool abandonOwners = rhiFailure.code == rhi::FailureCode::DeviceLost;
                if (abandonOwners)
                    AbandonRendererOwners();
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::NativeResourceFailure;
                    failure->message = FailureMessage(rhiFailure.message, "retired native resource collection failed");
                    failure->ownersAbandoned = abandonOwners;
                    failure->nativeFailure = rhiFailure;
                }
                return false;
            }
            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (m_textureResidency.IsInitialized() && !m_textureResidency.Tick(priorReaderCutover, &textureResidencyFailure))
            {
                const bool abandonOwners = RequiresRendererAbandonment(textureResidencyFailure);
                if (abandonOwners)
                    AbandonRendererOwners();
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::TextureFailure;
                    failure->message = FailureMessage(textureResidencyFailure.message, "texture residency progress failed");
                    failure->ownersAbandoned = abandonOwners;
                    failure->textureFailure = textureResidencyFailure;
                }
                return false;
            }
            rendering::MaterialResidencyRuntimeFailure materialResidencyFailure;
            if (m_materialResidency.IsInitialized() && !m_materialResidency.Update(&materialResidencyFailure))
            {
                const bool abandonOwners = RequiresRendererAbandonment(materialResidencyFailure);
                if (abandonOwners)
                    AbandonRendererOwners();
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::MaterialFailure;
                    failure->message = FailureMessage(materialResidencyFailure.message, "material residency progress failed");
                    failure->ownersAbandoned = abandonOwners;
                    failure->materialFailure = materialResidencyFailure;
                }
                return false;
            }
            rendering::MeshResidencyFailure meshResidencyFailure;
            if (m_meshResidency.IsInitialized() && !m_meshResidency.Tick(&meshResidencyFailure))
            {
                const bool abandonOwners = RequiresRendererAbandonment(meshResidencyFailure);
                if (abandonOwners)
                    AbandonRendererOwners();
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::MeshFailure;
                    failure->message = FailureMessage(meshResidencyFailure.message, "mesh residency progress failed");
                    failure->ownersAbandoned = abandonOwners;
                    failure->meshFailure = meshResidencyFailure;
                }
                return false;
            }
            rendering::MaterialSceneBindingFailure bindingFailure;
            if (m_materialBindings.IsInitialized() && !m_materialBindings.Update(&bindingFailure))
            {
                const bool abandonOwners = RequiresRendererAbandonment(bindingFailure);
                if (abandonOwners)
                    AbandonRendererOwners();
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::MaterialBindingFailure;
                    failure->message = FailureMessage(bindingFailure.message, "material scene-binding progress failed");
                    failure->ownersAbandoned = abandonOwners;
                    failure->materialBindingFailure = bindingFailure;
                }
                return false;
            }
            // FlushPreviousFrameProcessing above joins every retained-frame
            // submission. Read actual RHI receipts after residency producers
            // progress; do not fabricate work for a queue that has never run.
            if (m_gpuSceneRuntime.IsInitialized())
            {
                rhi::ResidencyFenceSet cutover;
                if (!rhi::GetSubmittedResidencyFences(cutover))
                {
                    AbandonRendererOwners();
                    if (failure != nullptr)
                        engine::detail::BuildSubmittedReceiptCoverageFailure(*failure);
                    return false;
                }
                engine::RenderingRetirementFailure retirementFailure;
                if (!SealResidencyRetirements(cutover, &retirementFailure))
                {
                    const bool abandonOwners = RequiresRendererAbandonment(retirementFailure);
                    if (abandonOwners)
                        AbandonRendererOwners();
                    if (failure != nullptr)
                    {
                        failure->code = engine::RenderingProgressFailureCode::RetirementFailure;
                        failure->message = FailureMessage(retirementFailure.message, "renderer residency retirement sealing failed");
                        failure->ownersAbandoned = abandonOwners;
                        failure->retirementFailure = retirementFailure;
                    }
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] vanguard::u64 QuiesceOwnerWorkCount() const noexcept
        {
            vanguard::u64 work = 0;
            if (m_textureResidency.IsInitialized())
            {
                const rendering::TextureResidencyRuntimeStats stats = m_textureResidency.GetStats();
                work += stats.liveDemands;
                work += stats.residencyRecords;
            }
            if (m_meshResidency.IsInitialized())
            {
                const rendering::MeshResidencyStats stats = m_meshResidency.GetStats();
                work += stats.liveDemands;
                work += stats.residencyRecords;
            }
            if (m_materialBindings.IsInitialized())
                work += m_materialBindings.GetStats().bindings;
            if (m_materialResidency.IsInitialized())
            {
                const rendering::MaterialResidencyRuntimeStats stats = m_materialResidency.GetStats();
                work += stats.residencyRecords;
                work += stats.liveDemands;
                work += stats.liveTechniqueRequests;
                work += stats.referencedNativePrograms;
            }
            if (m_materialMaterializer.IsInitialized())
            {
                const rendering::MaterialMaterializerStats stats = m_materialMaterializer.GetStats();
                work += stats.activeOperations;
                work += stats.liveReferences;
            }
            if (m_materialResources.IsInitialized())
            {
                const rendering::MaterialResourceResolverStats stats = m_materialResources.GetStats();
                work += stats.activeOperations;
                work += stats.liveReferences;
                work += stats.cachedDescriptors;
            }
            return work;
        }

        [[nodiscard]] const char* DrainRendererOwnersForQuiesce() noexcept
        {
            if (m_backend == nullptr || m_deviceUnavailable)
                return nullptr;

            rhi::Failure rhiFailure;
            if (!rhi::WaitIdle(&rhiFailure))
            {
                if (rhiFailure.code == rhi::FailureCode::DeviceLost)
                {
                    AbandonRendererDevice();
                    return nullptr;
                }
                return FailureMessage(rhiFailure.message, "RHI idle wait failed during renderer quiesce");
            }

            const vanguard::u64 initialWork = QuiesceOwnerWorkCount();
            const vanguard::u64 maximumPasses = 32u + initialWork * 4u;
            for (vanguard::u64 pass = 0; pass < maximumPasses && QuiesceOwnerWorkCount() != 0; ++pass)
            {
                engine::RenderingProgressFailure progressFailure;
                if (!ProgressRendererOwners(&progressFailure))
                    return FailureMessage(progressFailure.message, "renderer owner quiesce progression failed");
                if (!rhi::RetireResources(&rhiFailure) || !rhi::FlushRetiredResources(&rhiFailure))
                    return FailureMessage(rhiFailure.message, "renderer owner quiesce native retirement flush failed");
            }
            return QuiesceOwnerWorkCount() == 0 ? nullptr : "renderer owner quiesce did not settle after device idle";
        }

        [[nodiscard]] bool StageFrameContributions(bool& contributionStaged, engine::RenderingProgressFailure* const failure) noexcept
        {
            contributionStaged = false;
            if (failure != nullptr)
                *failure = {};
            if (m_deviceUnavailable)
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::DeviceUnavailable;
                    failure->message = "renderer device is unavailable";
                }
                return false;
            }
            if (!m_gpuSceneRuntime.IsInitialized())
                return true;

            bool textureContributionStaged = false;
            rendering::TextureResidencyRuntimeFailure textureResidencyFailure;
            if (m_textureResidency.IsInitialized() &&
                !m_textureResidency.StageGpuSceneContribution(m_gpuSceneRuntime, textureContributionStaged, &textureResidencyFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::TextureFailure;
                    failure->message = FailureMessage(textureResidencyFailure.message, "texture GPU Scene contribution staging failed");
                    failure->textureFailure = textureResidencyFailure;
                }
                return false;
            }
            bool meshContributionStaged = false;
            rendering::MeshResidencyFailure meshFailure;
            if (m_meshResidency.IsInitialized() && !m_meshResidency.StageGpuSceneContribution(meshContributionStaged, &meshFailure))
            {
                rendering::GpuSceneRuntimeFailure gpuSceneFailure;
                if (!m_gpuSceneRuntime.ResolveContributions(&gpuSceneFailure))
                {
                    if (failure != nullptr)
                    {
                        failure->code = engine::RenderingProgressFailureCode::GpuSceneFailure;
                        failure->message = FailureMessage(gpuSceneFailure.message, "GPU Scene contribution rollback failed after mesh staging rejection");
                        failure->meshFailure = meshFailure;
                        failure->gpuSceneFailure = gpuSceneFailure;
                    }
                    return false;
                }
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::MeshFailure;
                    failure->message = FailureMessage(meshFailure.message, "mesh GPU Scene contribution staging failed");
                    failure->meshFailure = meshFailure;
                }
                return false;
            }
            contributionStaged = textureContributionStaged || meshContributionStaged;
            return true;
        }

        [[nodiscard]] bool DispatchRendererFrameTick(const vanguard::containers::ArraySpan<const rendering::RenderSceneHandle> scenes,
                                                     const bool forceDispatch, bool& dispatched,
                                                     engine::RenderingProgressFailure* const failure) noexcept
        {
            dispatched = false;
            bool contributionStaged = false;
            if (!StageFrameContributions(contributionStaged, failure))
                return false;
            if (!forceDispatch && !contributionStaged)
                return true;
            if (m_rendererProgressEpoch == std::numeric_limits<vanguard::u64>::max())
            {
                if (contributionStaged && m_gpuSceneRuntime.IsInitialized())
                {
                    rendering::GpuSceneRuntimeFailure rollbackFailure;
                    if (!m_gpuSceneRuntime.ResolveContributions(&rollbackFailure))
                    {
                        if (failure != nullptr)
                        {
                            failure->code = engine::RenderingProgressFailureCode::GpuSceneFailure;
                            failure->message = FailureMessage(rollbackFailure.message, "GPU Scene contribution rollback failed at renderer epoch exhaustion");
                            failure->gpuSceneFailure = rollbackFailure;
                        }
                        return false;
                    }
                }
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::ProgressEpochExhausted;
                    failure->message = "renderer progress epoch is exhausted";
                }
                return false;
            }
            const vanguard::u64 progressEpoch = ++m_rendererProgressEpoch;
            rendering::RenderCommandFrameTickResult result;
            rendering::RenderCommandFailure commandFailure;
            if (!m_commands.FrameTick(scenes, progressEpoch, result, &commandFailure))
            {
                rendering::GpuSceneRuntimeFailure gpuSceneFailure;
                if (m_gpuSceneRuntime.IsInitialized() && !m_gpuSceneRuntime.ResolveContributions(&gpuSceneFailure))
                {
                    if (failure != nullptr)
                    {
                        failure->code = engine::RenderingProgressFailureCode::GpuSceneFailure;
                        failure->message = FailureMessage(gpuSceneFailure.message, "GPU Scene contribution rollback failed after FrameTick rejection");
                        failure->commandFailure = commandFailure;
                        failure->gpuSceneFailure = gpuSceneFailure;
                    }
                    return false;
                }
                if (failure != nullptr)
                {
                    failure->code = engine::RenderingProgressFailureCode::CommandFailure;
                    failure->message = FailureMessage(commandFailure.message, "render command FrameTick dispatch failed");
                    failure->commandFailure = commandFailure;
                }
                return false;
            }
            dispatched = true;
            return true;
        }

        void ReportFrameDiagnostics() noexcept
        {
            if (!m_config.enableFrameDiagnostics)
                return;

            rendering::FrameGpuTimingSample gpuTiming;
            rhi::Failure gpuFailure;
            const rendering::FrameGpuTimingPoll gpuPoll = m_frameRenderer.PollGpuTimings(gpuTiming, &gpuFailure);
            if (gpuPoll == rendering::FrameGpuTimingPoll::Ready)
            {
                m_latestGpuTiming = gpuTiming;
                m_hasLatestGpuTiming = true;
            }
            else if (gpuPoll == rendering::FrameGpuTimingPoll::Failed)
            {
                VG_LOG_ERROR(vanguard::diagnostics::Category::Rendering, "render diagnostics GPU timing poll failed: code=%u backendCode=%lld message=%s",
                             static_cast<vanguard::u32>(gpuFailure.code), static_cast<long long>(gpuFailure.backendCode),
                             gpuFailure.message[0] != '\0' ? gpuFailure.message : "<none>");
            }

            rendering::GeometryFrameDiagnostics geometry;
            rhi::Failure geometryFailure;
            const rendering::GeometryDiagnosticsPoll geometryPoll = m_frameRenderer.PollGeometryDiagnostics(geometry, &geometryFailure);
            if (geometryPoll == rendering::GeometryDiagnosticsPoll::Failed)
            {
                VG_LOG_ERROR(vanguard::diagnostics::Category::Rendering, "render diagnostics geometry poll failed: code=%u backendCode=%lld message=%s",
                             static_cast<vanguard::u32>(geometryFailure.code), static_cast<long long>(geometryFailure.backendCode),
                             geometryFailure.message[0] != '\0' ? geometryFailure.message : "<none>");
                return;
            }
            if (geometryPoll != rendering::GeometryDiagnosticsPoll::Ready)
                return;

            ++m_diagnosticGeometrySamples;
            if (m_diagnosticGeometrySamples != 1 && (m_diagnosticGeometrySamples % 120u) != 0)
                return;

            const rendering::GeometryCpuTimingSample geometryCpu = rendering::ConsumeGeometryCpuTimingSample();
            const vanguard::u64 geometryCpuFrequency = vanguard::system::GetMonotonicFrequency();
            const auto averageMicroseconds = [geometryCpuFrequency](const vanguard::u64 ticks, const vanguard::u32 calls) noexcept
            {
                return geometryCpuFrequency != 0 && calls != 0
                           ? static_cast<double>(ticks) * 1000000.0 / static_cast<double>(geometryCpuFrequency) / static_cast<double>(calls)
                           : 0.0;
            };
            VG_LOG_INFO(vanguard::diagnostics::Category::Rendering,
                        "geometry CPU recording: initUs=%.3f initUploadUs=%.3f initClearUs=%.3f initShellsUs=%.3f cullUs=%.3f "
                        "expandAppendUs=%.3f allocateBinsUs=%.3f scatterInstancesUs=%.3f",
                        averageMicroseconds(geometryCpu.initializeTicks, geometryCpu.initializeCalls),
                        averageMicroseconds(geometryCpu.initializeUploadTicks, geometryCpu.initializeCalls),
                        averageMicroseconds(geometryCpu.initializeClearTicks, geometryCpu.initializeCalls),
                        averageMicroseconds(geometryCpu.stageTicks[static_cast<vanguard::u32>(rendering::GeometryComputeStage::InitializeShells)],
                                            geometryCpu.stageCalls[static_cast<vanguard::u32>(rendering::GeometryComputeStage::InitializeShells)]),
                        averageMicroseconds(geometryCpu.stageTicks[static_cast<vanguard::u32>(rendering::GeometryComputeStage::Cull)],
                                            geometryCpu.stageCalls[static_cast<vanguard::u32>(rendering::GeometryComputeStage::Cull)]),
                        averageMicroseconds(geometryCpu.stageTicks[static_cast<vanguard::u32>(rendering::GeometryComputeStage::ExpandAppend)],
                                            geometryCpu.stageCalls[static_cast<vanguard::u32>(rendering::GeometryComputeStage::ExpandAppend)]),
                        averageMicroseconds(geometryCpu.stageTicks[static_cast<vanguard::u32>(rendering::GeometryComputeStage::AllocateBins)],
                                            geometryCpu.stageCalls[static_cast<vanguard::u32>(rendering::GeometryComputeStage::AllocateBins)]),
                        averageMicroseconds(geometryCpu.stageTicks[static_cast<vanguard::u32>(rendering::GeometryComputeStage::ScatterInstances)],
                                            geometryCpu.stageCalls[static_cast<vanguard::u32>(rendering::GeometryComputeStage::ScatterInstances)]));

            const engine::FramePipelineStats cpu = m_framePipeline != nullptr ? m_framePipeline->GetStats() : engine::FramePipelineStats{};
            const rendering::RenderFlowResourceAllocatorStats frameResources = m_frameRenderer.GetResourceAllocatorStats();
            const rhi::ResourceLifetimeStats nativeLifetime = rhi::GetResourceLifetimeStats();
            const rhi::DescriptorDomainStats resourceDescriptors =
                m_resourceDescriptors.IsValid() ? rhi::GetDescriptorDomainStats(m_resourceDescriptors.GetRef()) : rhi::DescriptorDomainStats{};
            const auto phaseMilliseconds = [&](const engine::FramePhase phase) noexcept
            {
                return static_cast<double>(cpu.lastPhaseSeconds[static_cast<vanguard::u32>(phase)]) * 1000.0;
            };
            const rendering::FrameGpuTimingSample timing = m_hasLatestGpuTiming ? m_latestGpuTiming : rendering::FrameGpuTimingSample{};
            if (geometry.viewCount == 0)
            {
                VG_LOG_INFO(vanguard::diagnostics::Category::Rendering,
                            "render diagnostics: geometryFrame=%llu views=0 frameFailed=%u gpuAvailable=%u gpuFrame=%llu geometryWorkNs=%llu "
                            "cameraGeometryNs=%llu cameraSamples=%u droppedScopes=%u droppedGeometry=%llu droppedGpu=%llu cpuFrame=%llu "
                            "cpuExecutionMs=%.3f renderUpdateMs=%.3f presentationMs=%.3f renderMs=%.3f frameNativeBytes=%llu frameTextureBytes=%llu frameBufferBytes=%llu "
                            "framePoolHits=%llu framePoolMisses=%llu framePendingRetire=%llu framePendingDestroy=%llu framePlacedHeapHits=%llu framePlacedHeapMisses=%llu frameExports=%u "
                            "frameCanonicalLive=%u frameCanonicalReusable=%u frameCanonicalPeak=%u frameCanonicalHits=%llu frameCanonicalMisses=%llu "
                            "frameCanonicalPressureEvictions=%llu frameCanonicalMaintenanceEvictions=%llu frameCanonicalRejected=%llu "
                            "resourceDescriptorAllocated=%u resourceDescriptorPopulated=%u resourceDescriptorPendingRetire=%u resourceDescriptorCompletedRetire=%llu "
                            "resourceDescriptorAllocationFailures=%llu rhiLive=%u rhiPendingRetire=%u rhiDestroying=%u rhiReclaimActive=%u",
                            static_cast<unsigned long long>(geometry.frameSerial), geometry.frameFailed ? 1u : 0u, geometry.gpuAvailable ? 1u : 0u,
                            static_cast<unsigned long long>(timing.frameSerial), static_cast<unsigned long long>(timing.geometryWorkNanoseconds),
                            static_cast<unsigned long long>(timing.cameraGeometryNanoseconds), timing.cameraGeometrySamples, timing.droppedScopes,
                            static_cast<unsigned long long>(m_frameRenderer.GetDroppedGeometryDiagnosticSamples()),
                            static_cast<unsigned long long>(m_frameRenderer.GetDroppedGpuTimingFrames()), static_cast<unsigned long long>(cpu.frames),
                            static_cast<double>(cpu.lastExecutionSeconds) * 1000.0, phaseMilliseconds(engine::FramePhase::RenderUpdate),
                            phaseMilliseconds(engine::FramePhase::Presentation), phaseMilliseconds(engine::FramePhase::Render),
                            static_cast<unsigned long long>(frameResources.chargedNativeBytes), static_cast<unsigned long long>(frameResources.chargedTextureBytes),
                            static_cast<unsigned long long>(frameResources.chargedBufferBytes), static_cast<unsigned long long>(frameResources.dedicatedResourcePoolHits),
                            static_cast<unsigned long long>(frameResources.dedicatedResourcePoolMisses), static_cast<unsigned long long>(frameResources.pendingRetirementResources),
                            static_cast<unsigned long long>(frameResources.pendingNativeDestructionResources), static_cast<unsigned long long>(frameResources.placedHeapPoolHits),
                            static_cast<unsigned long long>(frameResources.placedHeapPoolMisses), frameResources.outstandingPublishedExports,
                            frameResources.canonicalDescriptorLiveSlots, frameResources.canonicalDescriptorReusableSlots, frameResources.canonicalDescriptorPeakLiveSlots,
                            static_cast<unsigned long long>(frameResources.canonicalDescriptorPublicationHits),
                            static_cast<unsigned long long>(frameResources.canonicalDescriptorPublicationMisses),
                            static_cast<unsigned long long>(frameResources.canonicalDescriptorPressureEvictions),
                            static_cast<unsigned long long>(frameResources.canonicalDescriptorMaintenanceEvictions),
                            static_cast<unsigned long long>(frameResources.canonicalDescriptorRejectedReservations), resourceDescriptors.allocated, resourceDescriptors.populated,
                            resourceDescriptors.pendingRetirement, static_cast<unsigned long long>(resourceDescriptors.completedRetirements),
                            static_cast<unsigned long long>(resourceDescriptors.allocationFailures),
                            nativeLifetime.liveResources, nativeLifetime.pendingRetirements, nativeLifetime.destroyingResources, nativeLifetime.reclamationJobActive ? 1u : 0u);
                return;
            }

            const rendering::GeometryViewDiagnostics& view = geometry.views[0];
            const rendering::TextureResidencyRuntimeStats textureStats = m_textureResidency.GetStats();
            const rendering::MaterialResourceResolverStats resourceStats = m_materialResources.GetStats();
            const rendering::MaterialResidencyRuntimeStats materialStats = m_materialResidency.GetStats();
            const rendering::MaterialSceneBindingStats bindingStats = m_materialBindings.GetStats();
            VG_LOG_INFO(vanguard::diagnostics::Category::Rendering,
                        "render diagnostics: geometryFrame=%llu views=%u regions=%u lights=%u frameFailed=%u gpuAvailable=%u candidates=%u unresolved=%u shells=%u pipelineBinds=%u "
                        "visibleCapacity=%u workCapacity=%u instanceCapacity=%u argumentCapacity=%u visible=%u culled=%u visibilityOverflow=%u visibilityRejected=%u "
                        "requestedWork=%u emittedWork=%u rejectedWork=%u expansionOverflow=%u invalidBin=%u instanceOverflow=%u visibleBins=%u rejectedIndirectBins=%u "
                        "emittedArguments=%u argumentOverflow=%u nonemptyShells=%u gpuFrame=%llu geometryWorkNs=%llu cameraGeometryNs=%llu cameraSamples=%u "
                        "droppedScopes=%u droppedGeometry=%llu droppedGpu=%llu textureRecords=%u textureDemands=%u textureReady=%u texturePending=%u textureFailed=%llu "
                        "resourceOperations=%u resourceReferences=%u resourceReady=%llu resourceFallbacks=%llu resourceRejected=%llu materialRecords=%u materialDemands=%u "
                        "materialFailed=%llu bindings=%u activeBindings=%u candidateBindings=%u bindingFailures=%llu cpuFrame=%llu cpuExecutionMs=%.3f renderUpdateMs=%.3f "
                        "presentationMs=%.3f renderMs=%.3f frameNativeBytes=%llu frameTextureBytes=%llu frameBufferBytes=%llu framePoolHits=%llu framePoolMisses=%llu "
                        "framePendingRetire=%llu framePendingDestroy=%llu framePlacedHeapHits=%llu framePlacedHeapMisses=%llu frameExports=%u "
                        "frameCanonicalLive=%u frameCanonicalReusable=%u frameCanonicalPeak=%u frameCanonicalHits=%llu frameCanonicalMisses=%llu "
                        "frameCanonicalPressureEvictions=%llu frameCanonicalMaintenanceEvictions=%llu frameCanonicalRejected=%llu "
                        "resourceDescriptorAllocated=%u resourceDescriptorPopulated=%u resourceDescriptorPendingRetire=%u resourceDescriptorCompletedRetire=%llu "
                        "resourceDescriptorAllocationFailures=%llu rhiLive=%u rhiPendingRetire=%u rhiDestroying=%u rhiReclaimActive=%u",
                        static_cast<unsigned long long>(geometry.frameSerial), geometry.viewCount, geometry.outputRegions, view.directionalLights, geometry.frameFailed ? 1u : 0u,
                        geometry.gpuAvailable ? 1u : 0u, view.candidates, view.unresolvedGpuIdentities, view.plannedShells, view.pipelineBinds, view.visibleCapacity, view.workCapacity,
                        view.instanceCapacity, view.argumentCapacity, view.gpu.visibility.visibleCount, view.gpu.visibility.culledCount,
                        view.gpu.visibility.overflowCount, view.gpu.visibility.rejectedCount, view.gpu.geometry.requestedWorkCount, view.gpu.geometry.emittedWorkCount,
                        view.gpu.geometry.rejectedWorkCount, view.gpu.geometry.expansionOverflowCount, view.gpu.geometry.invalidBinWorkCount,
                        view.gpu.geometry.instanceOverflowCount, view.gpu.geometry.visibleBinCount, view.gpu.geometry.rejectedIndirectBinCount,
                        view.gpu.emittedArguments, view.gpu.argumentOverflow, view.gpu.nonemptyShells, static_cast<unsigned long long>(timing.frameSerial),
                        static_cast<unsigned long long>(timing.geometryWorkNanoseconds), static_cast<unsigned long long>(timing.cameraGeometryNanoseconds),
                        timing.cameraGeometrySamples, timing.droppedScopes, static_cast<unsigned long long>(m_frameRenderer.GetDroppedGeometryDiagnosticSamples()),
                        static_cast<unsigned long long>(m_frameRenderer.GetDroppedGpuTimingFrames()), textureStats.residencyRecords, textureStats.liveDemands,
                        textureStats.residency.bindlessReady, textureStats.residency.pendingInstallations, static_cast<unsigned long long>(textureStats.failedResidencies),
                        resourceStats.activeOperations, resourceStats.liveReferences, static_cast<unsigned long long>(resourceStats.resolutionsReady),
                        static_cast<unsigned long long>(resourceStats.fallbacksSelected), static_cast<unsigned long long>(resourceStats.rejectedOperations), materialStats.residencyRecords,
                        materialStats.liveDemands, static_cast<unsigned long long>(materialStats.materializationsFailed), bindingStats.bindings, bindingStats.activeBindings,
                        bindingStats.candidateBindings, static_cast<unsigned long long>(bindingStats.candidateFailures), static_cast<unsigned long long>(cpu.frames),
                        static_cast<double>(cpu.lastExecutionSeconds) * 1000.0, phaseMilliseconds(engine::FramePhase::RenderUpdate),
                        phaseMilliseconds(engine::FramePhase::Presentation), phaseMilliseconds(engine::FramePhase::Render),
                        static_cast<unsigned long long>(frameResources.chargedNativeBytes), static_cast<unsigned long long>(frameResources.chargedTextureBytes),
                        static_cast<unsigned long long>(frameResources.chargedBufferBytes), static_cast<unsigned long long>(frameResources.dedicatedResourcePoolHits),
                        static_cast<unsigned long long>(frameResources.dedicatedResourcePoolMisses), static_cast<unsigned long long>(frameResources.pendingRetirementResources),
                        static_cast<unsigned long long>(frameResources.pendingNativeDestructionResources), static_cast<unsigned long long>(frameResources.placedHeapPoolHits),
                        static_cast<unsigned long long>(frameResources.placedHeapPoolMisses), frameResources.outstandingPublishedExports,
                        frameResources.canonicalDescriptorLiveSlots, frameResources.canonicalDescriptorReusableSlots, frameResources.canonicalDescriptorPeakLiveSlots,
                        static_cast<unsigned long long>(frameResources.canonicalDescriptorPublicationHits),
                        static_cast<unsigned long long>(frameResources.canonicalDescriptorPublicationMisses),
                        static_cast<unsigned long long>(frameResources.canonicalDescriptorPressureEvictions),
                        static_cast<unsigned long long>(frameResources.canonicalDescriptorMaintenanceEvictions),
                        static_cast<unsigned long long>(frameResources.canonicalDescriptorRejectedReservations), resourceDescriptors.allocated, resourceDescriptors.populated,
                        resourceDescriptors.pendingRetirement, static_cast<unsigned long long>(resourceDescriptors.completedRetirements),
                        static_cast<unsigned long long>(resourceDescriptors.allocationFailures),
                        nativeLifetime.liveResources, nativeLifetime.pendingRetirements, nativeLifetime.destroyingResources, nativeLifetime.reclamationJobActive ? 1u : 0u);
        }

        engine::FrameParticipantStatus CompletePreviousFrame() noexcept
        {
            engine::RenderingProgressFailure failure;
            if (ResolvePreviousFrameOwners(&failure))
            {
                ReportFrameDiagnostics();
                return engine::FrameParticipantStatus::Success();
            }
            LatchParticipantFailure(failure);
            return engine::FrameParticipantStatus::Failure(FailureMessage(failure.message, "previous renderer frame completion failed"));
        }

        engine::FrameParticipantStatus RenderUpdate() noexcept
        {
            engine::RenderingProgressFailure failure;
            if (ProgressRendererOwners(&failure))
                return engine::FrameParticipantStatus::Success();
            if (failure.code == engine::RenderingProgressFailureCode::GpuSceneFailure)
            {
                const rendering::GpuSceneUploadFailure& upload = failure.gpuSceneFailure.uploadFailure;
                VG_LOG_ERROR(vanguard::diagnostics::Category::Rendering, "GPU Scene progression failed: uploadCode=%u request=%u rhiCode=%u backendCode=%lld rhiMessage=%s uploadMessage=%s",
                             static_cast<vanguard::u32>(upload.code), upload.request, static_cast<vanguard::u32>(upload.rhiFailure.code), static_cast<long long>(upload.rhiFailure.backendCode),
                             upload.rhiFailure.message[0] != '\0' ? upload.rhiFailure.message : "<none>", upload.message != nullptr ? upload.message : "<none>");
            }
            LatchParticipantFailure(failure);
            return engine::FrameParticipantStatus::Failure(FailureMessage(failure.message, "renderer owner progression failed"));
        }

        engine::FrameParticipantStatus DispatchFrameTick() noexcept
        {
            engine::RenderingProgressFailure failure;
            bool dispatched = false;
            if (!DispatchRendererFrameTick(m_scenes.GetFramePipelineScenes(), true, dispatched, &failure))
            {
                LatchParticipantFailure(failure);
                return engine::FrameParticipantStatus::Failure(FailureMessage(failure.message, "renderer FrameTick progression failed"));
            }
            return engine::FrameParticipantStatus::Success();
        }

        void LatchParticipantFailure(const engine::RenderingProgressFailure& failure) noexcept
        {
            m_participantFailure.Latch(failure);
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
        bool m_drawInterfaceAvailable = false;
        rendering::RenderPhaseSet m_defaultCameraPhases;
        rendering::MeshResidencyManager m_meshResidency;
        rendering::TextureResidencyRuntime m_textureResidency;
        rendering::MaterialResourceResolver m_materialResources;
        rendering::MaterialMaterializer m_materialMaterializer;
        rendering::PipelineCache m_materialPipelines;
        rhi::BindingLayout m_materialBindingLayouts[rhi::MaximumBindingLayoutsPerPipeline];
        vanguard::u32 m_materialBindingLayoutCount = 0;
        rendering::MaterialResidencyRuntime m_materialResidency;
        rendering::MaterialSceneBindingBridge m_materialBindings;
        rendering::FrameRenderer m_frameRenderer;
        rendering::RenderCommandSystem m_commands;
        rendering::ViewportManager m_viewports;
        engine::FramePipelineService* m_framePipeline = nullptr;
        rendering::FrameGpuTimingSample m_latestGpuTiming;
        vanguard::u64 m_diagnosticGeometrySamples = 0;
        bool m_hasLatestGpuTiming = false;
        vanguard::u64 m_rendererProgressEpoch = 0;
        engine::detail::RenderingProgressFailureLatch m_participantFailure;
    };

    app::Service* CreateRenderingService(void* const userData) noexcept
    {
        if (userData == nullptr)
            return nullptr;
        auto block = memory::Allocate(memory::PoolId::Rendering, sizeof(ManagedRenderingService), alignof(ManagedRenderingService));
        return block ? ::new (block.address) ManagedRenderingService(*static_cast<const engine::RenderingServiceConfig*>(userData)) : nullptr;
    }

    void DestroyRenderingService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedRenderingService*>(service)->~ManagedRenderingService();
        memory::MemoryBlock block{service, sizeof(ManagedRenderingService), memory::PoolId::Rendering};
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

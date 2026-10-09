#include <vanguard/runtime/runtime_viewport_service.hpp>

#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/time.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace diagnostics = vanguard::diagnostics;
    namespace engine = vanguard::engine;
    namespace memory = vanguard::memory;
    namespace rendering = vanguard::rendering;
    namespace rhi = vanguard::rhi;
    namespace runtime = vanguard::runtime;
    namespace system = vanguard::system;
    namespace window = vanguard::window;
    using vanguard::u32;
    using vanguard::u64;

    class ManagedRuntimeViewportService final : public app::Service
    {
    public:
        ManagedRuntimeViewportService() noexcept = default;

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_framePipeline = engine::FindFramePipelineService(context);
            m_rendering = engine::FindRenderingService(context);
            m_windows = engine::FindWindowService(context);
            m_session = engine::FindWorldSessionService(context);
            if (m_framePipeline == nullptr || m_rendering == nullptr || m_windows == nullptr || m_session == nullptr)
                return app::LifecycleStatus::Failure("runtime viewport dependencies are unavailable");

            rendering::PresentationFailure presentationFailure;
            if (!m_presentation.Initialize(m_windows->GetManager(), m_rendering->GetViewports(), &presentationFailure))
                return app::LifecycleStatus::Failure(runtime::detail::FailureMessage(presentationFailure.message, "runtime presentation service initialization failed"));

            const engine::FrameParticipantDescriptor presentationParticipant =
                runtime::detail::MakeRuntimePresentationParticipant(&ExecutePresentationReconcile, this);

            engine::FrameFailure frameFailure;
            if (!m_framePipeline->RegisterParticipant(presentationParticipant, &frameFailure))
                return app::LifecycleStatus::Failure(runtime::detail::FailureMessage(frameFailure.message, "runtime presentation participant registration failed"));

            const engine::FrameParticipantDescriptor worldFrameParticipant =
                runtime::detail::MakeRuntimeWorldFrameParticipant(&ExecuteWorldFrame, this);
            if (!m_framePipeline->RegisterParticipant(worldFrameParticipant, &frameFailure))
                return app::LifecycleStatus::Failure(runtime::detail::FailureMessage(frameFailure.message, "runtime world-frame participant registration failed"));

            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            const window::WindowHandle primaryWindow = m_windows != nullptr ? m_windows->GetPrimaryWindow() : window::WindowHandle{};
            window::WindowSnapshot snapshot;
            if (!primaryWindow.IsValid() || !m_windows->GetManager().GetSnapshot(primaryWindow, snapshot) ||
                snapshot.lifecycle != window::WindowLifecycleState::Alive)
                return app::LifecycleStatus::Failure("runtime presentation requires the live primary window");

            const window::WindowExtent initialPixelExtent = snapshot.nativeState.pixelExtent.IsValid()
                                                                        ? snapshot.nativeState.pixelExtent
                                                                        : snapshot.requested.logicalExtent;
            if (!initialPixelExtent.IsValid())
                return app::LifecycleStatus::Failure("runtime primary window has no valid presentation extent");

            rendering::PresentationOutputDesc outputDesc;
            outputDesc.name = "runtime.presentation";
            outputDesc.window = primaryWindow;
            outputDesc.renderExtent = {initialPixelExtent.width, initialPixelExtent.height};
            outputDesc.swapChain.colorPreference = rendering::SwapChainPolicy::ColorPreference::Sdr;
            outputDesc.swapChain.presentMode = rhi::PresentMode::Immediate;
            outputDesc.swapChain.allowTearing = true;
            outputDesc.swapChain.enableFrameLatencyPacing = false;
            rendering::PresentationFailure presentationFailure;
            if (!m_presentation.CreateOutput(outputDesc, m_output, &presentationFailure))
                return app::LifecycleStatus::Failure(runtime::detail::FailureMessage(presentationFailure.message, "runtime presentation output creation failed"));

            rendering::PresentationOutput* const output = m_presentation.Resolve(m_output);
            rendering::RenderViewport* const renderViewport = output != nullptr ? output->GetRenderViewport() : nullptr;
            if (renderViewport == nullptr || !renderViewport->IsValid())
            {
                rendering::PresentationFailure cleanupFailure;
                const bool cleaned = m_presentation.DestroyOutput(m_output, &cleanupFailure);
                m_output = {};
                return app::LifecycleStatus::Failure(
                    cleaned ? "runtime presentation output has no render viewport"
                            : runtime::detail::FailureMessage(cleanupFailure.message, "runtime presentation output cleanup failed after resolution failure"));
            }

            rendering::EngineViewportDesc viewportDesc;
            viewportDesc.contextName = "runtime.world";
            viewportDesc.output = renderViewport->GetHandle();
            viewportDesc.presentByDefault = true;
            rendering::ViewportFailure viewportFailure;
            if (!m_rendering->GetViewports().CreateEngineViewport(viewportDesc, m_engineViewport, &viewportFailure))
            {
                rendering::PresentationFailure cleanupFailure;
                const bool cleaned = m_presentation.DestroyOutput(m_output, &cleanupFailure);
                m_output = {};
                return app::LifecycleStatus::Failure(
                    cleaned ? runtime::detail::FailureMessage(viewportFailure.message, "runtime engine viewport creation failed")
                            : runtime::detail::FailureMessage(cleanupFailure.message, "runtime presentation output cleanup failed after engine viewport creation failure"));
            }

            m_acceptFrames = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            m_acceptFrames = false;
            if (m_rendering != nullptr && m_engineViewport.IsValid())
            {
                rendering::ViewportFailure viewportFailure;
                if (!m_rendering->GetViewports().DestroyEngineViewport(m_engineViewport, &viewportFailure))
                    return app::LifecycleStatus::Failure(runtime::detail::FailureMessage(viewportFailure.message, "runtime engine viewport quiesce failed"));
                m_engineViewport = {};
            }
            if (m_output.IsValid())
            {
                rendering::PresentationFailure presentationFailure;
                if (!m_presentation.DestroyOutput(m_output, &presentationFailure))
                    return app::LifecycleStatus::Failure(runtime::detail::FailureMessage(presentationFailure.message, "runtime presentation output quiesce failed"));
                m_output = {};
            }
            if (m_presentation.GetStats().activeOutputs != 0)
                return app::LifecycleStatus::Failure("runtime presentation outputs remain alive after quiesce");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            if (m_engineViewport.IsValid() || m_output.IsValid() || m_presentation.GetStats().activeOutputs != 0)
                return app::LifecycleStatus::Failure("runtime viewport ownership survived drain");
            if (m_rendering != nullptr)
            {
                auto& manager = m_rendering->GetTextureResidency().GetResidencyManager();
                rendering::TextureGenerationRetirement retirements[32]{};
                rendering::TextureResidencyFailure failure;
                for (;;)
                {
                    const u32 count = manager.PeekCompletedGenerationRetirements({retirements, 32}, &failure);
                    if (failure.code != rendering::TextureResidencyFailureCode::None)
                        return app::LifecycleStatus::Failure(runtime::detail::FailureMessage(failure.message, "runtime texture retirement evidence query failed"));
                    if (count == 0)
                        break;
                    for (u32 index = 0; index < count; ++index)
                        if (!manager.AcknowledgeGenerationRetirement(retirements[index].retirementId, &failure))
                            return app::LifecycleStatus::Failure(runtime::detail::FailureMessage(failure.message, "runtime texture retirement evidence acknowledgement failed"));
                }
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_acceptFrames = false;
            rendering::PresentationFailure presentationFailure;
            if (!m_presentation.Shutdown(&presentationFailure))
                return app::LifecycleStatus::Failure(runtime::detail::FailureMessage(presentationFailure.message, "runtime presentation service shutdown failed"));
            m_framePipeline = nullptr;
            m_rendering = nullptr;
            m_windows = nullptr;
            m_session = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        static engine::FrameParticipantStatus ExecutePresentationReconcile(const engine::FrameContext&, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedRuntimeViewportService*>(userData);
            return service != nullptr ? service->ReconcilePresentation()
                                      : engine::FrameParticipantStatus::Failure("runtime viewport service is unavailable");
        }

        static engine::FrameParticipantStatus ExecuteWorldFrame(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedRuntimeViewportService*>(userData);
            return service != nullptr ? service->SupplyWorldFrame(context)
                                      : engine::FrameParticipantStatus::Failure("runtime viewport service is unavailable");
        }

        engine::FrameParticipantStatus ReconcilePresentation() noexcept
        {
            if (!m_acceptFrames)
                return engine::FrameParticipantStatus::Success();
            rendering::PresentationFailure failure;
            if (m_presentation.Tick(&failure))
            {
                const rendering::PresentationOutput* const output = m_presentation.Resolve(m_output);
                if (output == nullptr)
                    return engine::FrameParticipantStatus::Failure("runtime presentation output disappeared during reconciliation");
                if (m_rendering->IsFrameDiagnosticsEnabled() && m_windows != nullptr)
                {
                    window::WindowSnapshot windowState;
                    const rendering::RenderViewport* const viewport = output->GetRenderViewport();
                    if (viewport != nullptr && viewport->IsValid() &&
                        m_windows->GetManager().GetSnapshot(output->GetWindow(), windowState) && !windowState.nativeState.focused)
                    {
                        const u64 frequency = system::GetMonotonicFrequency();
                        const u64 now = frequency != 0 ? system::GetMonotonicTicks() : 0;
                        if (frequency != 0 && (m_diagnosticsHeartbeatTick == 0 || now - m_diagnosticsHeartbeatTick >= frequency / 2u))
                        {
                            const rhi::SwapChainRef swapChain = viewport->GetSwapChain();
                            const rhi::SwapChainStats swapChainStats = swapChain.IsValid() ? rhi::GetSwapChainStats(swapChain) : rhi::SwapChainStats{};
                            const u64 presentedFrames = viewport->GetPresentedFrameCount();
                            VG_LOG_INFO(diagnostics::Category::Engine,
                                        "runtime host state: windowRevision=%llu focused=%u mouseFocus=%u visible=%u minimized=%u occluded=%u presentationState=%u viewportState=%u swapChainValid=%u swapChainState=%u presentedFrames=%llu presentedDelta=%llu rhiPresentedFrames=%llu acquisitions=%llu acquireWaits=%llu acquireWaitMs=%.3f frameLatencyWaits=%llu frameLatencyWaitMs=%.3f longestFrameLatencyWaitMs=%.3f frameLatencyTimeouts=%llu presentMs=%.3f longestPresentMs=%.3f presentationFailures=%llu",
                                        static_cast<unsigned long long>(windowState.stateRevision), u32(windowState.nativeState.focused), u32(windowState.nativeState.mouseFocus),
                                        u32(windowState.nativeState.placement.visible), u32(windowState.nativeState.minimized), u32(windowState.nativeState.occluded),
                                        u32(output->GetState()), u32(viewport->GetState()), u32(swapChain.IsValid()), u32(swapChainStats.state),
                                        static_cast<unsigned long long>(presentedFrames),
                                        static_cast<unsigned long long>(presentedFrames >= m_diagnosticsPresentedFrames ? presentedFrames - m_diagnosticsPresentedFrames : 0),
                                        static_cast<unsigned long long>(swapChainStats.presentedFrames), static_cast<unsigned long long>(swapChainStats.acquisitions),
                                        static_cast<unsigned long long>(swapChainStats.acquireWaits),
                                        static_cast<double>(swapChainStats.acquireWaitNanoseconds) / 1000000.0,
                                        static_cast<unsigned long long>(swapChainStats.frameLatencyWaits),
                                        static_cast<double>(swapChainStats.frameLatencyWaitNanoseconds) / 1000000.0,
                                        static_cast<double>(swapChainStats.longestFrameLatencyWaitNanoseconds) / 1000000.0,
                                        static_cast<unsigned long long>(swapChainStats.frameLatencyTimeouts),
                                        static_cast<double>(swapChainStats.presentNanoseconds) / 1000000.0,
                                        static_cast<double>(swapChainStats.longestPresentNanoseconds) / 1000000.0,
                                        static_cast<unsigned long long>(swapChainStats.presentationFailures));
                            m_diagnosticsHeartbeatTick = now;
                            m_diagnosticsPresentedFrames = presentedFrames;
                        }
                    }
                }
                if (output->GetState() == rendering::PresentationOutputState::Ready &&
                    !runtime::detail::MatchesRuntimeSdrPresentation(output->GetActiveFormat(), output->GetActiveColorSpace()))
                    return engine::FrameParticipantStatus::Failure("runtime presentation output does not match the declared SDR renderer attachment contract");
                return engine::FrameParticipantStatus::Success();
            }
            const rendering::PresentationOutput* const output = m_presentation.Resolve(m_output);
            const rendering::PresentationOutputState state =
                output != nullptr ? output->GetState() : rendering::PresentationOutputState::Failed;
            if (runtime::detail::IsRetryablePresentationFailure(state, failure))
                return engine::FrameParticipantStatus::Success();
            return engine::FrameParticipantStatus::Failure(runtime::detail::FailureMessage(failure.message, "runtime presentation reconciliation failed"));
        }

        engine::FrameParticipantStatus SupplyWorldFrame(const engine::FrameContext& context) noexcept
        {
            if (!m_acceptFrames || m_session == nullptr || m_rendering == nullptr)
                return engine::FrameParticipantStatus::Success();
            if (!runtime::detail::IsRuntimeWorldSessionReady(m_session->GetStatus()))
                return engine::FrameParticipantStatus::Success();

            const engine::WorldSessionCameraBinding binding = m_session->GetCameraBinding();
            const runtime::detail::RuntimeFrameAdmission cameraAdmission =
                runtime::detail::ClassifyRuntimeWorldCamera(binding.status, binding.camera.IsValid(),
                                                            binding.camera.IsValid() && m_rendering->GetCameras().IsAlive(binding.camera));
            if (cameraAdmission == runtime::detail::RuntimeFrameAdmission::NoFrame)
                return engine::FrameParticipantStatus::Success();
            if (cameraAdmission == runtime::detail::RuntimeFrameAdmission::Fail)
                return engine::FrameParticipantStatus::Failure("runtime world camera binding is not renderable");

            const rendering::PresentationOutput* const output = m_presentation.Resolve(m_output);
            const rendering::PresentationOutputState outputState =
                output != nullptr ? output->GetState() : rendering::PresentationOutputState::Failed;
            const runtime::detail::RuntimeFrameAdmission outputAdmission =
                runtime::detail::ClassifyRuntimePresentationOutput(output != nullptr, outputState,
                                                                   output != nullptr && runtime::detail::IsDrawablePresentationOutput(*output));
            if (outputAdmission == runtime::detail::RuntimeFrameAdmission::Fail)
                return engine::FrameParticipantStatus::Failure(output == nullptr ? "runtime presentation output is unavailable"
                                                                                : "runtime presentation output failed");
            if (outputAdmission == runtime::detail::RuntimeFrameAdmission::NoFrame)
                return engine::FrameParticipantStatus::Success();

            rendering::EngineViewport* const viewport = m_rendering->GetViewports().Resolve(m_engineViewport);
            if (viewport == nullptr || !viewport->IsValid())
                return engine::FrameParticipantStatus::Failure("runtime engine viewport is unavailable");

            rendering::RenderFrameSetup frameSetup;
            frameSetup.mode = rendering::RenderingMode::Shaded;
            frameSetup.purpose = rendering::RenderFramePurpose::Normal;
            frameSetup.present = true;
            frameSetup.features.gameMode = true;
            frameSetup.features.geometryDiagnostics = m_rendering->IsFrameDiagnosticsEnabled();

            const rendering::RenderCameraHandle rootCamera = binding.camera;
            rendering::RenderFrameViewSetup viewSetup;
            viewSetup.scene = rootCamera.scene;
            viewSetup.rootCameras = {&rootCamera, 1};
            viewSetup.jitterIndex = static_cast<u32>(context.frame);
            viewSetup.enableTemporalJitter = true;
            return runtime::detail::SubmitRuntimeWorldFrame(*viewport, frameSetup, viewSetup);
        }

        engine::FramePipelineService* m_framePipeline = nullptr;
        engine::RenderingService* m_rendering = nullptr;
        engine::WindowService* m_windows = nullptr;
        engine::WorldSessionService* m_session = nullptr;
        rendering::PresentationService m_presentation;
        rendering::PresentationOutputHandle m_output;
        rendering::EngineViewportHandle m_engineViewport;
        u64 m_diagnosticsHeartbeatTick = 0;
        u64 m_diagnosticsPresentedFrames = 0;
        bool m_acceptFrames = false;
    };

    app::Service* CreateRuntimeViewportService(void*) noexcept
    {
        const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Runtime, sizeof(ManagedRuntimeViewportService), alignof(ManagedRuntimeViewportService));
        return block ? ::new (block.address) ManagedRuntimeViewportService() : nullptr;
    }

    void DestroyRuntimeViewportService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedRuntimeViewportService*>(service)->~ManagedRuntimeViewportService();
        memory::MemoryBlock block{service, sizeof(ManagedRuntimeViewportService), memory::PoolId::Runtime};
        memory::Free(block);
    }
} // namespace

namespace vanguard::runtime
{
    bool RegisterRuntimeViewportService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{{engine::FramePipelineServiceId, application::DependencyKind::Required},
                                                                {engine::RenderingServiceId, application::DependencyKind::Required},
                                                                {engine::WindowServiceId, application::DependencyKind::Required},
                                                                {engine::WorldSessionServiceId, application::DependencyKind::Required}};
        application::ServiceDescriptor descriptor;
        descriptor.id = RuntimeViewportServiceId;
        descriptor.name = "runtimeViewport";
        descriptor.profiles = application::ApplicationProfile::Runtime;
        descriptor.scope = application::ServiceScope::ApplicationSession;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 4};
        descriptor.create = CreateRuntimeViewportService;
        descriptor.destroy = DestroyRuntimeViewportService;
        return host.RegisterService(RuntimeModuleId, descriptor, failure);
    }
} // namespace vanguard::runtime

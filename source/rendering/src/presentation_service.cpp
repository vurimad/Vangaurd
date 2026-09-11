#include <vanguard/rendering/presentation_service.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(PresentationFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        bool Fail(PresentationFailure* const failure, const PresentationFailureCode code, const char* const message, const PresentationOutputHandle output = {},
                  const window::Failure* const windowFailure = nullptr, const ViewportFailure* const viewportFailure = nullptr,
                  const rhi::Failure* const rhiFailure = nullptr) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->output = output;
                failure->message = message;
                if (windowFailure != nullptr)
                    failure->windowFailure = *windowFailure;
                if (viewportFailure != nullptr)
                    failure->viewportFailure = *viewportFailure;
                if (rhiFailure != nullptr)
                    failure->rhiFailure = *rhiFailure;
            }
            return false;
        }

        [[nodiscard]] bool IsPolicyValid(const SwapChainPolicy& policy) noexcept
        {
            return policy.bufferCount >= 2 && policy.bufferCount <= rhi::MaximumSwapChainBuffers && policy.presentMode != rhi::PresentMode::Mailbox &&
                   (!policy.enableFrameLatencyPacing || (policy.maximumFramesInFlight != 0 && policy.maximumFramesInFlight <= policy.bufferCount &&
                                                         policy.frameLatencyWaitTimeoutMilliseconds != 0));
        }

        [[nodiscard]] bool IsRequiredHdr(const SwapChainPolicy::ColorPreference preference) noexcept
        {
            return preference == SwapChainPolicy::ColorPreference::RequireHdr10 || preference == SwapChainPolicy::ColorPreference::RequireScRgb;
        }

        void ResolveColorConfiguration(const SwapChainPolicy& policy, const bool hdrActive, rhi::Format& format, rhi::ColorSpace& colorSpace) noexcept
        {
            format = rhi::Format::B8G8R8A8UNorm;
            colorSpace = rhi::ColorSpace::Srgb;
            if (!hdrActive || policy.colorPreference == SwapChainPolicy::ColorPreference::Sdr)
                return;
            if (policy.colorPreference == SwapChainPolicy::ColorPreference::PreferScRgb ||
                policy.colorPreference == SwapChainPolicy::ColorPreference::RequireScRgb)
            {
                format = rhi::Format::R16G16B16A16Float;
                colorSpace = rhi::ColorSpace::ScRgb;
                return;
            }
            format = rhi::Format::R10G10B10A2UNorm;
            colorSpace = rhi::ColorSpace::Hdr10;
        }

        [[nodiscard]] bool ConvertSurface(const window::NativePresentationSurface& source, rhi::PresentationSurface& destination) noexcept
        {
            destination = {};
            if (source.kind == window::NativePresentationSurfaceKind::Win32)
            {
                destination = {rhi::PresentationSurfaceKind::Win32, source.window, source.display};
                return true;
            }
            return false;
        }
    } // namespace

    void PresentationOutput::Reset() noexcept
    {
        m_handle = {};
        m_state = PresentationOutputState::Vacant;
        m_window = {};
        m_attachment = {};
        m_renderViewport = nullptr;
        m_swapChainPolicy = {};
        m_activeFormat = rhi::Format::Unknown;
        m_activeColorSpace = rhi::ColorSpace::Srgb;
        m_displayColor = {};
        m_sdrWhiteLevel = 1.0f;
        m_hdrHeadroom = 1.0f;
        m_reconciliations = 0;
        m_swapChainCreations = 0;
        m_resizeApplications = 0;
        m_surfaceReplacements = 0;
        m_colorFallbacks = 0;
        m_failures = 0;
        m_active = false;
    }

    struct PresentationService::Impl
    {
        struct Record
        {
            PresentationOutput output;
            u32 generation = 1;
        };

        window::WindowManager* windows = nullptr;
        ViewportManager* viewports = nullptr;
        Record records[MaximumPresentationOutputs]{};
        PresentationServiceStats stats;

        [[nodiscard]] Record* Find(const PresentationOutputHandle handle) noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            Record& record = records[handle.index];
            return record.output.IsValid() && record.generation == handle.generation ? &record : nullptr;
        }

        [[nodiscard]] const Record* Find(const PresentationOutputHandle handle) const noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            const Record& record = records[handle.index];
            return record.output.IsValid() && record.generation == handle.generation ? &record : nullptr;
        }

        void Reject() noexcept
        {
            ++stats.rejectedOperations;
        }

        void RefreshStateCounts() noexcept
        {
            stats.readyOutputs = 0;
            stats.suspendedOutputs = 0;
            for (const Record& record : records)
            {
                if (!record.output.IsValid())
                    continue;
                if (record.output.GetState() == PresentationOutputState::Ready)
                    ++stats.readyOutputs;
                if (record.output.GetState() == PresentationOutputState::Suspended)
                    ++stats.suspendedOutputs;
            }
        }
    };

    PresentationService::~PresentationService()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool PresentationService::Initialize(window::WindowManager& windows, ViewportManager& viewports, PresentationFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, PresentationFailureCode::AlreadyInitialized, "presentation service is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, PresentationFailureCode::WrongThread, "presentation service initialization requires the main thread");
        if (!windows.IsInitialized() || !viewports.IsInitialized() || !rhi::IsInitialized())
            return Fail(failure, PresentationFailureCode::InvalidDescriptor, "presentation service requires initialized window, viewport, and RHI systems");
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, PresentationFailureCode::CapacityExceeded, "presentation service storage allocation failed");
        m_impl = ::new (block.address) Impl();
        m_impl->windows = &windows;
        m_impl->viewports = &viewports;
        return true;
    }

    bool PresentationService::Shutdown(PresentationFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, PresentationFailureCode::WrongThread, "presentation service shutdown requires the main thread");
        if (m_impl->stats.activeOutputs != 0)
        {
            m_impl->Reject();
            return Fail(failure, PresentationFailureCode::OutputsRemainAlive, "all presentation outputs must be explicitly destroyed before shutdown");
        }
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        m_impl = nullptr;
        return true;
    }

    bool PresentationService::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool PresentationService::CreateOutput(const PresentationOutputDesc& desc, PresentationOutputHandle& output, PresentationFailure* const failure) noexcept
    {
        output = {};
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, PresentationFailureCode::NotInitialized, "presentation service is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, PresentationFailureCode::WrongThread, "presentation output creation requires the main thread");
        if (desc.name == nullptr || desc.name[0] == '\0' || !desc.window.IsValid() || !desc.renderExtent.IsValid() || !IsPolicyValid(desc.swapChain))
        {
            m_impl->Reject();
            return Fail(failure, PresentationFailureCode::InvalidDescriptor, "presentation output descriptor is invalid");
        }
        window::WindowSnapshot windowSnapshot;
        if (!m_impl->windows->GetSnapshot(desc.window, windowSnapshot) || windowSnapshot.lifecycle != window::WindowLifecycleState::Alive)
        {
            m_impl->Reject();
            return Fail(failure, PresentationFailureCode::InvalidDescriptor, "presentation output requires a live window");
        }
        Impl::Record* record = nullptr;
        for (Impl::Record& candidate : m_impl->records)
            if (!candidate.output.IsValid())
            {
                record = &candidate;
                break;
            }
        if (record == nullptr)
        {
            m_impl->Reject();
            return Fail(failure, PresentationFailureCode::CapacityExceeded, "presentation output capacity is exhausted");
        }

        window::Failure windowFailure;
        window::PresentationAttachmentHandle attachment;
        if (!m_impl->windows->AttachPresentation(desc.window, attachment, &windowFailure))
            return Fail(failure, PresentationFailureCode::WindowFailure, "window presentation attachment failed", {}, &windowFailure);

        RenderViewportDesc viewportDesc;
        viewportDesc.name = desc.name;
        viewportDesc.outputKind = RenderViewportOutputKind::Presentation;
        viewportDesc.renderExtent = desc.renderExtent;
        viewportDesc.outputExtent = desc.renderExtent;
        viewportDesc.presentation = attachment;
        ViewportFailure viewportFailure;
        RenderViewportHandle renderViewport;
        if (!m_impl->viewports->CreateRenderViewport(viewportDesc, renderViewport, &viewportFailure))
        {
            static_cast<void>(m_impl->windows->DetachPresentation(attachment, &windowFailure));
            return Fail(failure, PresentationFailureCode::ViewportFailure, "presentation render viewport creation failed", {}, nullptr, &viewportFailure);
        }

        const u32 index = static_cast<u32>(record - m_impl->records);
        PresentationOutput& created = record->output;
        created.m_handle = {index, record->generation};
        created.m_state = PresentationOutputState::AwaitingSurface;
        created.m_window = desc.window;
        created.m_attachment = attachment;
        created.m_renderViewport = m_impl->viewports->Resolve(renderViewport);
        created.m_swapChainPolicy = desc.swapChain;
        created.m_active = created.m_renderViewport != nullptr;
        if (!created.m_active)
        {
            static_cast<void>(m_impl->viewports->DestroyRenderViewport(renderViewport, &viewportFailure));
            static_cast<void>(m_impl->windows->DetachPresentation(attachment, &windowFailure));
            created.Reset();
            return Fail(failure, PresentationFailureCode::ViewportFailure, "presentation render viewport resolution failed");
        }
        output = created.m_handle;
        ++m_impl->stats.activeOutputs;
        m_impl->RefreshStateCounts();
        return true;
    }

    bool PresentationService::DestroyOutput(const PresentationOutputHandle output, PresentationFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, PresentationFailureCode::NotInitialized, "presentation service is not initialized", output);
        if (!concurrency::IsMainThread())
            return Fail(failure, PresentationFailureCode::WrongThread, "presentation output destruction requires the main thread", output);
        Impl::Record* const record = m_impl->Find(output);
        if (record == nullptr)
        {
            m_impl->Reject();
            return Fail(failure, PresentationFailureCode::InvalidHandle, "presentation output handle is stale or invalid", output);
        }
        RenderViewport* const viewport = record->output.m_renderViewport;
        if (viewport == nullptr || !viewport->IsValid())
        {
            ++record->output.m_failures;
            ++m_impl->stats.failures;
            return Fail(failure, PresentationFailureCode::ViewportFailure, "presentation render viewport is unavailable", output);
        }
        if (viewport->GetEngineViewportReferenceCount() != 0)
        {
            m_impl->Reject();
            return Fail(failure, PresentationFailureCode::Busy, "engine viewports must be destroyed before their presentation output", output);
        }
        if (viewport->GetSwapChain().IsValid())
        {
            ViewportFailure viewportFailure;
            if (!viewport->UnbindSwapChain(&viewportFailure))
                return Fail(failure,
                            viewportFailure.code == ViewportFailureCode::Busy ? PresentationFailureCode::Busy : PresentationFailureCode::ViewportFailure,
                            "presentation swap chain could not be unbound", output, nullptr, &viewportFailure);
        }
        ViewportFailure viewportFailure;
        if (!m_impl->viewports->DestroyRenderViewport(viewport->GetHandle(), &viewportFailure))
            return Fail(failure, PresentationFailureCode::ViewportFailure, "presentation render viewport destruction failed", output, nullptr,
                        &viewportFailure);
        window::Failure windowFailure;
        if (!m_impl->windows->DetachPresentation(record->output.m_attachment, &windowFailure))
            return Fail(failure, PresentationFailureCode::WindowFailure, "window presentation detachment failed after viewport destruction", output,
                        &windowFailure);

        record->output.Reset();
        ++record->generation;
        if (record->generation == 0)
            record->generation = 1;
        --m_impl->stats.activeOutputs;
        m_impl->RefreshStateCounts();
        return true;
    }

    bool PresentationService::Tick(PresentationFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, PresentationFailureCode::NotInitialized, "presentation service is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, PresentationFailureCode::WrongThread, "presentation reconciliation requires the main thread");

        ++m_impl->stats.ticks;
        bool success = true;
        for (Impl::Record& record : m_impl->records)
        {
            if (!record.output.IsValid())
                continue;
            PresentationOutput& output = record.output;
            ++output.m_reconciliations;
            ++m_impl->stats.reconciliations;

            window::PresentationAttachmentSnapshot attachment;
            if (!m_impl->windows->GetSnapshot(output.m_attachment, attachment))
            {
                output.m_state = PresentationOutputState::Failed;
                ++output.m_failures;
                ++m_impl->stats.failures;
                if (success)
                    Fail(failure, PresentationFailureCode::WindowFailure, "presentation attachment disappeared during reconciliation", output.m_handle);
                success = false;
                continue;
            }
            output.m_sdrWhiteLevel = attachment.sdrWhiteLevel;
            output.m_hdrHeadroom = attachment.hdrHeadroom;

            RenderViewport* const viewport = output.m_renderViewport;
            if (viewport == nullptr || !viewport->IsValid())
            {
                output.m_state = PresentationOutputState::Failed;
                ++output.m_failures;
                ++m_impl->stats.failures;
                if (success)
                    Fail(failure, PresentationFailureCode::ViewportFailure, "presentation viewport disappeared during reconciliation", output.m_handle);
                success = false;
                continue;
            }
            RenderViewportPresentationUpdate update;
            update.attachment = attachment.handle;
            update.pixelExtent = {attachment.pixelExtent.width, attachment.pixelExtent.height};
            update.requiredPixelExtentRevision = attachment.requiredPixelExtentRevision;
            update.requiredSurfaceRevision = attachment.requiredSurfaceRevision;
            update.acknowledgedPixelExtentRevision = attachment.acknowledgedPixelExtentRevision;
            update.acknowledgedSurfaceRevision = attachment.acknowledgedSurfaceRevision;
            update.visible = attachment.visible;
            update.occluded = attachment.occluded;
            update.minimized = attachment.minimized;
            update.suspended = window::HasRequirement(attachment.requirements, window::PresentationRequirement::Suspended);
            RenderViewportPresentationResult updateResult;
            ViewportFailure viewportFailure;
            if (!viewport->UpdatePresentation(update, updateResult, &viewportFailure))
            {
                output.m_state = PresentationOutputState::Failed;
                ++output.m_failures;
                ++m_impl->stats.failures;
                if (success)
                    Fail(failure, PresentationFailureCode::ViewportFailure, "presentation viewport reconciliation failed", output.m_handle, nullptr, &viewportFailure);
                success = false;
                continue;
            }
            if (updateResult.resizeApplied)
            {
                ++output.m_resizeApplications;
                ++m_impl->stats.resizeApplications;
            }
            if (updateResult.surfaceReplaced)
            {
                ++output.m_surfaceReplacements;
                ++m_impl->stats.surfaceReplacements;
            }

            if (viewport->GetState() == RenderViewportState::Suspended)
            {
                output.m_state = PresentationOutputState::Suspended;
                continue;
            }
            if (!viewport->GetSwapChain().IsValid())
            {
                if (updateResult.surfaceReplaced)
                {
                    rhi::Failure rhiFailure;
                    if (!rhi::FlushRetiredResources(&rhiFailure))
                    {
                        output.m_state = PresentationOutputState::Failed;
                        ++output.m_failures;
                        ++m_impl->stats.failures;
                        if (success)
                            Fail(failure, PresentationFailureCode::RhiFailure, "retired swap-chain destruction failed before surface replacement",
                                 output.m_handle, nullptr, nullptr, &rhiFailure);
                        success = false;
                        continue;
                    }
                }
                window::NativePresentationSurface nativeSurface;
                window::Failure windowFailure;
                if (!m_impl->windows->ResolvePresentationSurface(output.m_attachment, nativeSurface, &windowFailure))
                {
                    output.m_state = PresentationOutputState::AwaitingSurface;
                    ++output.m_failures;
                    ++m_impl->stats.failures;
                    if (success)
                        Fail(failure, PresentationFailureCode::WindowFailure, "native presentation surface resolution failed", output.m_handle, &windowFailure);
                    success = false;
                    continue;
                }
                rhi::PresentationSurface surface;
                if (!ConvertSurface(nativeSurface, surface))
                {
                    output.m_state = PresentationOutputState::Failed;
                    ++output.m_failures;
                    ++m_impl->stats.failures;
                    if (success)
                        Fail(failure, PresentationFailureCode::UnsupportedSurface, "native presentation surface is unsupported by the active RHI",
                             output.m_handle);
                    success = false;
                    continue;
                }

                const SwapChainPolicy& policy = output.m_swapChainPolicy;
                rhi::SwapChainDesc swapChainDesc;
                swapChainDesc.surface = surface;
                swapChainDesc.width = attachment.pixelExtent.width;
                swapChainDesc.height = attachment.pixelExtent.height;
                swapChainDesc.bufferCount = policy.bufferCount;
                swapChainDesc.presentMode = policy.presentMode;
                ResolveColorConfiguration(policy, attachment.hdrCapable, swapChainDesc.format, swapChainDesc.colorSpace);
                swapChainDesc.hdr10Metadata = policy.hdr10Metadata;
                swapChainDesc.frameLatency.enabled = policy.enableFrameLatencyPacing;
                swapChainDesc.frameLatency.maximumFramesInFlight = policy.maximumFramesInFlight;
                swapChainDesc.frameLatency.waitTimeoutMilliseconds = policy.frameLatencyWaitTimeoutMilliseconds;
                swapChainDesc.allowTearing = policy.allowTearing;
                if (!attachment.hdrCapable && IsRequiredHdr(policy.colorPreference) && !policy.allowSdrFallback)
                {
                    output.m_state = PresentationOutputState::AwaitingSurface;
                    ++output.m_failures;
                    ++m_impl->stats.failures;
                    if (success)
                        Fail(failure, PresentationFailureCode::RhiFailure, "the presentation policy requires HDR but the active window output is SDR",
                             output.m_handle);
                    success = false;
                    continue;
                }
                rhi::Failure rhiFailure;
                rhi::SwapChainRef swapChain = rhi::CreateSwapChainWithBackBuffer(swapChainDesc, &rhiFailure);
                if (!swapChain.IsValid() && swapChainDesc.colorSpace != rhi::ColorSpace::Srgb && policy.allowSdrFallback)
                {
                    swapChainDesc.format = rhi::Format::B8G8R8A8UNorm;
                    swapChainDesc.colorSpace = rhi::ColorSpace::Srgb;
                    swapChain = rhi::CreateSwapChainWithBackBuffer(swapChainDesc, &rhiFailure);
                    if (swapChain.IsValid())
                    {
                        ++output.m_colorFallbacks;
                        ++m_impl->stats.colorFallbacks;
                    }
                }
                if (!swapChain.IsValid())
                {
                    output.m_state = PresentationOutputState::AwaitingSurface;
                    ++output.m_failures;
                    ++m_impl->stats.failures;
                    if (success)
                        Fail(failure, PresentationFailureCode::RhiFailure, "swap-chain creation failed", output.m_handle, nullptr, nullptr, &rhiFailure);
                    success = false;
                    continue;
                }
                if (!viewport->BindSwapChain(swapChain, &viewportFailure))
                {
                    static_cast<void>(rhi::SafeRelease(swapChain));
                    output.m_state = PresentationOutputState::AwaitingSurface;
                    ++output.m_failures;
                    ++m_impl->stats.failures;
                    if (success)
                        Fail(failure, PresentationFailureCode::ViewportFailure, "swap-chain binding failed", output.m_handle, nullptr, &viewportFailure);
                    success = false;
                    continue;
                }
                static_cast<void>(rhi::SafeRelease(swapChain));
                output.m_activeFormat = swapChainDesc.format;
                output.m_activeColorSpace = swapChainDesc.colorSpace;
                ++output.m_swapChainCreations;
                ++m_impl->stats.swapChainCreations;
            }

            if (viewport->GetSwapChain().IsValid())
                output.m_displayColor = rhi::GetSwapChainStats(viewport->GetSwapChain()).displayColor;

            window::PresentationAcknowledgement acknowledgement;
            if (viewport->GetPresentationAcknowledgement(acknowledgement) &&
                (acknowledgement.pixelExtentRevision > attachment.acknowledgedPixelExtentRevision ||
                 acknowledgement.surfaceRevision > attachment.acknowledgedSurfaceRevision))
            {
                window::Failure windowFailure;
                if (!m_impl->windows->AcknowledgePresentation(output.m_attachment, acknowledgement, &windowFailure))
                {
                    output.m_state = PresentationOutputState::Failed;
                    ++output.m_failures;
                    ++m_impl->stats.failures;
                    if (success)
                        Fail(failure, PresentationFailureCode::WindowFailure, "presentation acknowledgement failed", output.m_handle, &windowFailure);
                    success = false;
                    continue;
                }
            }
            output.m_state = PresentationOutputState::Ready;
        }
        m_impl->RefreshStateCounts();
        return success;
    }

    PresentationOutput* PresentationService::Resolve(const PresentationOutputHandle output) noexcept
    {
        if (m_impl == nullptr)
            return nullptr;
        Impl::Record* const record = m_impl->Find(output);
        return record != nullptr ? &record->output : nullptr;
    }

    const PresentationOutput* PresentationService::Resolve(const PresentationOutputHandle output) const noexcept
    {
        if (m_impl == nullptr)
            return nullptr;
        const Impl::Record* const record = m_impl->Find(output);
        return record != nullptr ? &record->output : nullptr;
    }

    PresentationServiceStats PresentationService::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : PresentationServiceStats{};
    }
} // namespace vanguard::rendering

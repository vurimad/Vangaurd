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

    struct PresentationService::Impl
    {
        struct Record
        {
            PresentationOutputSnapshot snapshot;
            u32 generation = 1;
            bool occupied = false;
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
            return record.occupied && record.generation == handle.generation ? &record : nullptr;
        }

        [[nodiscard]] const Record* Find(const PresentationOutputHandle handle) const noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            const Record& record = records[handle.index];
            return record.occupied && record.generation == handle.generation ? &record : nullptr;
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
                if (!record.occupied)
                    continue;
                if (record.snapshot.state == PresentationOutputState::Ready)
                    ++stats.readyOutputs;
                if (record.snapshot.state == PresentationOutputState::Suspended)
                    ++stats.suspendedOutputs;
            }
        }

        void RecordFailure(Record& record) noexcept
        {
            ++record.snapshot.failures;
            ++stats.failures;
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
            if (!candidate.occupied)
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
        record->occupied = true;
        record->snapshot = {};
        record->snapshot.handle = {index, record->generation};
        record->snapshot.state = PresentationOutputState::AwaitingSurface;
        record->snapshot.window = desc.window;
        record->snapshot.attachment = attachment;
        record->snapshot.renderViewport = renderViewport;
        record->snapshot.swapChain = desc.swapChain;
        output = record->snapshot.handle;
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
        RenderViewportSnapshot viewportSnapshot;
        if (!m_impl->viewports->GetSnapshot(record->snapshot.renderViewport, viewportSnapshot))
        {
            m_impl->RecordFailure(*record);
            return Fail(failure, PresentationFailureCode::ViewportFailure, "presentation render viewport is unavailable", output);
        }
        if (viewportSnapshot.engineViewportReferences != 0)
        {
            m_impl->Reject();
            return Fail(failure, PresentationFailureCode::Busy, "engine viewports must be destroyed before their presentation output", output);
        }
        if (viewportSnapshot.swapChain.IsValid())
        {
            ViewportFailure viewportFailure;
            if (!m_impl->viewports->UnbindSwapChain(record->snapshot.renderViewport, &viewportFailure))
                return Fail(failure,
                            viewportFailure.code == ViewportFailureCode::Busy ? PresentationFailureCode::Busy : PresentationFailureCode::ViewportFailure,
                            "presentation swap chain could not be unbound", output, nullptr, &viewportFailure);
        }
        ViewportFailure viewportFailure;
        if (!m_impl->viewports->DestroyRenderViewport(record->snapshot.renderViewport, &viewportFailure))
            return Fail(failure, PresentationFailureCode::ViewportFailure, "presentation render viewport destruction failed", output, nullptr,
                        &viewportFailure);
        window::Failure windowFailure;
        if (!m_impl->windows->DetachPresentation(record->snapshot.attachment, &windowFailure))
            return Fail(failure, PresentationFailureCode::WindowFailure, "window presentation detachment failed after viewport destruction", output,
                        &windowFailure);

        record->occupied = false;
        record->snapshot = {};
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
            if (!record.occupied)
                continue;
            ++record.snapshot.reconciliations;
            ++m_impl->stats.reconciliations;

            window::PresentationAttachmentSnapshot attachment;
            if (!m_impl->windows->GetSnapshot(record.snapshot.attachment, attachment))
            {
                record.snapshot.state = PresentationOutputState::Failed;
                m_impl->RecordFailure(record);
                if (success)
                    Fail(failure, PresentationFailureCode::WindowFailure, "presentation attachment disappeared during reconciliation", record.snapshot.handle);
                success = false;
                continue;
            }
            record.snapshot.sdrWhiteLevel = attachment.sdrWhiteLevel;
            record.snapshot.hdrHeadroom = attachment.hdrHeadroom;

            RenderViewportSnapshot before;
            static_cast<void>(m_impl->viewports->GetSnapshot(record.snapshot.renderViewport, before));
            ViewportFailure viewportFailure;
            if (!m_impl->viewports->UpdatePresentation(record.snapshot.renderViewport, attachment, &viewportFailure))
            {
                if (viewportFailure.code != ViewportFailureCode::Busy)
                {
                    record.snapshot.state = PresentationOutputState::Failed;
                    m_impl->RecordFailure(record);
                }
                if (success)
                    Fail(failure, viewportFailure.code == ViewportFailureCode::Busy ? PresentationFailureCode::Busy : PresentationFailureCode::ViewportFailure,
                         "presentation viewport reconciliation failed", record.snapshot.handle, nullptr, &viewportFailure);
                success = false;
                continue;
            }

            RenderViewportSnapshot current;
            if (!m_impl->viewports->GetSnapshot(record.snapshot.renderViewport, current))
            {
                record.snapshot.state = PresentationOutputState::Failed;
                m_impl->RecordFailure(record);
                if (success)
                    Fail(failure, PresentationFailureCode::ViewportFailure, "presentation viewport disappeared during reconciliation", record.snapshot.handle);
                success = false;
                continue;
            }
            if (current.appliedPixelExtentRevision > before.appliedPixelExtentRevision && before.swapChain.IsValid())
            {
                ++record.snapshot.resizeApplications;
                ++m_impl->stats.resizeApplications;
            }
            if (current.requiredSurfaceRevision > before.appliedSurfaceRevision && before.swapChain.IsValid() && !current.swapChain.IsValid())
            {
                ++record.snapshot.surfaceReplacements;
                ++m_impl->stats.surfaceReplacements;
            }

            if (current.state == RenderViewportState::Suspended)
            {
                record.snapshot.state = PresentationOutputState::Suspended;
                continue;
            }
            if (!current.swapChain.IsValid())
            {
                if (before.swapChain.IsValid())
                {
                    rhi::Failure rhiFailure;
                    if (!rhi::FlushRetiredResources(&rhiFailure))
                    {
                        record.snapshot.state = PresentationOutputState::Failed;
                        m_impl->RecordFailure(record);
                        if (success)
                            Fail(failure, PresentationFailureCode::RhiFailure, "retired swap-chain destruction failed before surface replacement",
                                 record.snapshot.handle, nullptr, nullptr, &rhiFailure);
                        success = false;
                        continue;
                    }
                }
                window::NativePresentationSurface nativeSurface;
                window::Failure windowFailure;
                if (!m_impl->windows->ResolvePresentationSurface(record.snapshot.attachment, nativeSurface, &windowFailure))
                {
                    record.snapshot.state = PresentationOutputState::AwaitingSurface;
                    m_impl->RecordFailure(record);
                    if (success)
                        Fail(failure, PresentationFailureCode::WindowFailure, "native presentation surface resolution failed", record.snapshot.handle,
                             &windowFailure);
                    success = false;
                    continue;
                }
                rhi::PresentationSurface surface;
                if (!ConvertSurface(nativeSurface, surface))
                {
                    record.snapshot.state = PresentationOutputState::Failed;
                    m_impl->RecordFailure(record);
                    if (success)
                        Fail(failure, PresentationFailureCode::UnsupportedSurface, "native presentation surface is unsupported by the active RHI",
                             record.snapshot.handle);
                    success = false;
                    continue;
                }

                rhi::SwapChainDesc swapChainDesc;
                swapChainDesc.surface = surface;
                swapChainDesc.width = attachment.pixelExtent.width;
                swapChainDesc.height = attachment.pixelExtent.height;
                swapChainDesc.bufferCount = record.snapshot.swapChain.bufferCount;
                swapChainDesc.presentMode = record.snapshot.swapChain.presentMode;
                ResolveColorConfiguration(record.snapshot.swapChain, attachment.hdrCapable, swapChainDesc.format, swapChainDesc.colorSpace);
                swapChainDesc.hdr10Metadata = record.snapshot.swapChain.hdr10Metadata;
                swapChainDesc.frameLatency.enabled = record.snapshot.swapChain.enableFrameLatencyPacing;
                swapChainDesc.frameLatency.maximumFramesInFlight = record.snapshot.swapChain.maximumFramesInFlight;
                swapChainDesc.frameLatency.waitTimeoutMilliseconds = record.snapshot.swapChain.frameLatencyWaitTimeoutMilliseconds;
                swapChainDesc.allowTearing = record.snapshot.swapChain.allowTearing;
                if (!attachment.hdrCapable && IsRequiredHdr(record.snapshot.swapChain.colorPreference) && !record.snapshot.swapChain.allowSdrFallback)
                {
                    record.snapshot.state = PresentationOutputState::AwaitingSurface;
                    m_impl->RecordFailure(record);
                    if (success)
                        Fail(failure, PresentationFailureCode::RhiFailure, "the presentation policy requires HDR but the active window output is SDR",
                             record.snapshot.handle);
                    success = false;
                    continue;
                }
                rhi::Failure rhiFailure;
                rhi::SwapChainRef swapChain = rhi::CreateSwapChainWithBackBuffer(swapChainDesc, &rhiFailure);
                if (!swapChain.IsValid() && swapChainDesc.colorSpace != rhi::ColorSpace::Srgb && record.snapshot.swapChain.allowSdrFallback)
                {
                    swapChainDesc.format = rhi::Format::B8G8R8A8UNorm;
                    swapChainDesc.colorSpace = rhi::ColorSpace::Srgb;
                    swapChain = rhi::CreateSwapChainWithBackBuffer(swapChainDesc, &rhiFailure);
                    if (swapChain.IsValid())
                    {
                        ++record.snapshot.colorFallbacks;
                        ++m_impl->stats.colorFallbacks;
                    }
                }
                if (!swapChain.IsValid())
                {
                    record.snapshot.state = PresentationOutputState::AwaitingSurface;
                    m_impl->RecordFailure(record);
                    if (success)
                        Fail(failure, PresentationFailureCode::RhiFailure, "swap-chain creation failed", record.snapshot.handle, nullptr, nullptr, &rhiFailure);
                    success = false;
                    continue;
                }
                if (!m_impl->viewports->BindSwapChain(record.snapshot.renderViewport, swapChain, &viewportFailure))
                {
                    static_cast<void>(rhi::SafeRelease(swapChain));
                    record.snapshot.state = PresentationOutputState::AwaitingSurface;
                    m_impl->RecordFailure(record);
                    if (success)
                        Fail(failure, PresentationFailureCode::ViewportFailure, "swap-chain binding failed", record.snapshot.handle, nullptr, &viewportFailure);
                    success = false;
                    continue;
                }
                static_cast<void>(rhi::SafeRelease(swapChain));
                record.snapshot.activeFormat = swapChainDesc.format;
                record.snapshot.activeColorSpace = swapChainDesc.colorSpace;
                ++record.snapshot.swapChainCreations;
                ++m_impl->stats.swapChainCreations;
                static_cast<void>(m_impl->viewports->GetSnapshot(record.snapshot.renderViewport, current));
            }

            if (current.swapChain.IsValid())
                record.snapshot.displayColor = rhi::GetSwapChainStats(current.swapChain).displayColor;

            window::PresentationAcknowledgement acknowledgement;
            if (m_impl->viewports->GetPresentationAcknowledgement(record.snapshot.renderViewport, acknowledgement) &&
                (acknowledgement.pixelExtentRevision > attachment.acknowledgedPixelExtentRevision ||
                 acknowledgement.surfaceRevision > attachment.acknowledgedSurfaceRevision))
            {
                window::Failure windowFailure;
                if (!m_impl->windows->AcknowledgePresentation(record.snapshot.attachment, acknowledgement, &windowFailure))
                {
                    record.snapshot.state = PresentationOutputState::Failed;
                    m_impl->RecordFailure(record);
                    if (success)
                        Fail(failure, PresentationFailureCode::WindowFailure, "presentation acknowledgement failed", record.snapshot.handle, &windowFailure);
                    success = false;
                    continue;
                }
            }
            record.snapshot.state = PresentationOutputState::Ready;
        }
        m_impl->RefreshStateCounts();
        return success;
    }

    bool PresentationService::GetSnapshot(const PresentationOutputHandle output, PresentationOutputSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (m_impl == nullptr)
            return false;
        const Impl::Record* const record = m_impl->Find(output);
        if (record == nullptr)
            return false;
        snapshot = record->snapshot;
        return true;
    }

    RenderViewportHandle PresentationService::ResolveRenderViewport(const PresentationOutputHandle output) const noexcept
    {
        if (m_impl == nullptr)
            return {};
        const Impl::Record* const record = m_impl->Find(output);
        return record != nullptr ? record->snapshot.renderViewport : RenderViewportHandle{};
    }

    PresentationServiceStats PresentationService::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : PresentationServiceStats{};
    }
} // namespace vanguard::rendering

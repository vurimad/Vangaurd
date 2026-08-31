#include <vanguard/rendering/viewport.hpp>
#include <vanguard/rendering/render_command_system.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/pool.hpp>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(ViewportFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(ViewportFailure* const failure, const ViewportFailureCode code, const char* const message,
                                const RenderViewportHandle renderViewport = {}, const EngineViewportHandle engineViewport = {},
                                const rhi::Failure* const rhiFailure = nullptr) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->renderViewport = renderViewport;
                failure->engineViewport = engineViewport;
                failure->message = message;
                if (rhiFailure != nullptr)
                    failure->rhiFailure = *rhiFailure;
            }
            return false;
        }

        [[nodiscard]] bool CopyName(char* const destination, const char* const source) noexcept
        {
            if (source == nullptr || source[0] == '\0')
                return false;
            u32 index = 0;
            while (index + 1u < MaximumViewportNameBytes && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            if (source[index] != '\0')
                return false;
            destination[index] = '\0';
            return true;
        }

        void CopyNameUnchecked(char* const destination, const char* const source) noexcept
        {
            u32 index = 0;
            while (index + 1u < MaximumViewportNameBytes && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            destination[index] = '\0';
        }

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] bool ValidOutputKind(const RenderViewportOutputKind kind) noexcept
        {
            return static_cast<u32>(kind) <= static_cast<u32>(RenderViewportOutputKind::Headless);
        }

        [[nodiscard]] bool ValidFrameSetup(const RenderFrameSetup& setup) noexcept
        {
            return static_cast<u32>(setup.mode) <= static_cast<u32>(RenderingMode::SafeMode) &&
                   static_cast<u32>(setup.purpose) <= static_cast<u32>(RenderFramePurpose::Diagnostic);
        }

    } // namespace

    struct ViewportManager::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        struct RenderSlot
        {
            RenderViewportSnapshot snapshot;
            u32 generation = 0;
            bool active = false;
        };

        struct EngineSlot
        {
            EngineViewportSnapshot snapshot;
            u32 generation = 0;
            bool active = false;
        };

        RenderSlot renderSlots[MaximumRenderViewports]{};
        EngineSlot engineSlots[MaximumEngineViewports]{};
        RenderCommandSystem* commands = nullptr;
        ViewportManagerStats stats;
        u64 nextFrameSerial = 1;
        bool initialized = false;

        [[nodiscard]] RenderSlot* Find(const RenderViewportHandle handle) noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            RenderSlot& slot = renderSlots[handle.index];
            return slot.active && slot.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] const RenderSlot* Find(const RenderViewportHandle handle) const noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            const RenderSlot& slot = renderSlots[handle.index];
            return slot.active && slot.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] EngineSlot* Find(const EngineViewportHandle handle) noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            EngineSlot& slot = engineSlots[handle.index];
            return slot.active && slot.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] const EngineSlot* Find(const EngineViewportHandle handle) const noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            const EngineSlot& slot = engineSlots[handle.index];
            return slot.active && slot.generation == handle.generation ? &slot : nullptr;
        }

        void Reject() noexcept
        {
            ++stats.rejectedOperations;
        }
    };

    ViewportManager::~ViewportManager()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool ViewportManager::Initialize(RenderCommandSystem& commands, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, ViewportFailureCode::AlreadyInitialized, "viewport manager is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "viewport manager must initialize on the main thread");
        if (!commands.IsInitialized())
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "viewport manager requires an initialized RenderCommandSystem");

        m_impl = VANGUARD_NEW(Impl);
        if (m_impl == nullptr)
            return Fail(failure, ViewportFailureCode::CapacityExceeded, "viewport manager allocation failed");
        m_impl->commands = &commands;
        m_impl->initialized = true;
        return true;
    }

    bool ViewportManager::Shutdown(ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "viewport manager must shut down on the main thread");
        if (m_impl->commands == nullptr || !m_impl->commands->IsInitialized())
            return Fail(failure, ViewportFailureCode::InvalidState, "RenderCommandSystem was shut down before the viewport manager");
        RenderCommandFailure commandFailure;
        if (!m_impl->commands->FlushPreviousFrameProcessing(&commandFailure))
            return Fail(failure, ViewportFailureCode::SubmissionFailure,
                        commandFailure.message != nullptr ? commandFailure.message : "render command flush failed");

        for (u32 index = 0; index < MaximumEngineViewports; ++index)
            m_impl->engineSlots[index].active = false;
        for (u32 index = 0; index < MaximumRenderViewports; ++index)
        {
            Impl::RenderSlot& slot = m_impl->renderSlots[index];
            if (!slot.active)
                continue;
            if (slot.snapshot.swapChain.IsValid())
                static_cast<void>(rhi::SafeRelease(slot.snapshot.swapChain));
            if (slot.snapshot.outputTexture.IsValid())
                static_cast<void>(rhi::SafeRelease(slot.snapshot.outputTexture));
            slot.active = false;
        }
        m_impl->initialized = false;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool ViewportManager::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_impl->initialized;
    }

    bool ViewportManager::CreateRenderViewport(const RenderViewportDesc& desc, RenderViewportHandle& viewport, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        viewport = {};
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render viewport creation must run on the main thread");

        char validatedName[MaximumViewportNameBytes]{};
        const bool descriptorValid = CopyName(validatedName, desc.name) && ValidOutputKind(desc.outputKind) && desc.renderExtent.IsValid() &&
                                     desc.outputExtent.IsValid() &&
                                     (desc.outputKind != RenderViewportOutputKind::Presentation || desc.presentation.IsValid()) &&
                                     (desc.outputKind != RenderViewportOutputKind::Texture || desc.outputTexture.IsValid()) &&
                                     (desc.outputKind == RenderViewportOutputKind::Texture || !desc.outputTexture.IsValid());
        if (!descriptorValid)
        {
            m_impl->Reject();
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "invalid render viewport descriptor");
        }
        if (desc.outputKind == RenderViewportOutputKind::Texture &&
            (!rhi::IsInitialized() || !rhi::IsResourceReferenceValid(rhi::ResourceRef(desc.outputTexture))))
        {
            m_impl->Reject();
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "texture output must be a live RHI texture reference");
        }

        for (u32 index = 0; index < MaximumRenderViewports; ++index)
        {
            Impl::RenderSlot& slot = m_impl->renderSlots[index];
            if (slot.active)
                continue;
            slot.generation = NextGeneration(slot.generation);
            slot.snapshot = {};
            slot.snapshot.handle = {index, slot.generation};
            slot.snapshot.outputKind = desc.outputKind;
            slot.snapshot.state = desc.outputKind == RenderViewportOutputKind::Presentation ? RenderViewportState::AwaitingOutput : RenderViewportState::Ready;
            slot.snapshot.renderExtent = desc.renderExtent;
            slot.snapshot.outputExtent = desc.outputExtent;
            slot.snapshot.requestedOutputExtent = desc.outputExtent;
            slot.snapshot.presentation = desc.presentation;
            slot.snapshot.outputTexture = desc.outputTexture;
            slot.snapshot.visible = desc.outputKind != RenderViewportOutputKind::Presentation;
            CopyNameUnchecked(slot.snapshot.name, validatedName);
            if (desc.outputTexture.IsValid())
                rhi::AddRef(desc.outputTexture);
            slot.active = true;
            viewport = slot.snapshot.handle;
            ++m_impl->stats.renderViewports;
            return true;
        }

        m_impl->Reject();
        return Fail(failure, ViewportFailureCode::CapacityExceeded, "maximum render viewport count exceeded");
    }

    bool ViewportManager::DestroyRenderViewport(const RenderViewportHandle viewport, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render viewport destruction must run on the main thread", viewport);
        Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
        {
            m_impl->Reject();
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid render viewport handle", viewport);
        }
        if (slot->snapshot.engineViewportReferences != 0)
        {
            m_impl->Reject();
            return Fail(failure, ViewportFailureCode::OutputStillReferenced, "engine viewports must be destroyed before their render output", viewport);
        }
        if (!m_impl->commands->IsIdle())
        {
            m_impl->Reject();
            return Fail(failure, ViewportFailureCode::Busy, "render frame work must be flushed before destroying a render viewport", viewport);
        }
        if (slot->snapshot.swapChain.IsValid() && rhi::GetSwapChainStats(slot->snapshot.swapChain).state == rhi::SwapChainState::Acquired)
        {
            m_impl->Reject();
            return Fail(failure, ViewportFailureCode::Busy, "the acquired presentation output must be presented or abandoned before destroying its viewport",
                        viewport);
        }
        if (slot->snapshot.swapChain.IsValid())
            static_cast<void>(rhi::SafeRelease(slot->snapshot.swapChain));
        if (slot->snapshot.outputTexture.IsValid())
            static_cast<void>(rhi::SafeRelease(slot->snapshot.outputTexture));
        slot->active = false;
        slot->snapshot = {};
        --m_impl->stats.renderViewports;
        return true;
    }

    bool ViewportManager::BindSwapChain(const RenderViewportHandle viewport, const rhi::SwapChainRef swapChain, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "swapchain binding must run on the main thread", viewport);
        Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid render viewport handle", viewport);
        if (slot->snapshot.outputKind != RenderViewportOutputKind::Presentation || slot->snapshot.swapChain.IsValid() || !swapChain.IsValid() ||
            !rhi::IsInitialized() || !rhi::IsResourceReferenceValid(rhi::ResourceRef(swapChain)))
            return Fail(failure, ViewportFailureCode::InvalidState, "render viewport cannot bind the requested swapchain", viewport);
        const rhi::SwapChainStats swapChainStats = rhi::GetSwapChainStats(swapChain);
        if (slot->snapshot.requiredPixelExtentRevision == 0 || slot->snapshot.requiredSurfaceRevision == 0 || !slot->snapshot.requestedOutputExtent.IsValid() ||
            swapChainStats.state == rhi::SwapChainState::Failed || swapChainStats.width == 0 || swapChainStats.height == 0)
            return Fail(failure, ViewportFailureCode::InvalidState, "swapchain binding requires current presentation state and a valid back buffer", viewport);
        rhi::AddRef(swapChain);
        const bool suspended = slot->snapshot.state == RenderViewportState::Suspended || !slot->snapshot.visible;
        slot->snapshot.swapChain = swapChain;
        slot->snapshot.outputExtent = slot->snapshot.requestedOutputExtent;
        slot->snapshot.appliedPixelExtentRevision = slot->snapshot.requiredPixelExtentRevision;
        slot->snapshot.appliedSurfaceRevision = slot->snapshot.requiredSurfaceRevision;
        slot->snapshot.state = suspended ? RenderViewportState::Suspended : RenderViewportState::Ready;
        ++slot->snapshot.outputRevision;
        return true;
    }

    bool ViewportManager::UnbindSwapChain(const RenderViewportHandle viewport, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "swapchain unbinding must run on the main thread", viewport);
        Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid render viewport handle", viewport);
        if (slot->snapshot.outputKind != RenderViewportOutputKind::Presentation || !slot->snapshot.swapChain.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidState, "render viewport has no bound swapchain", viewport);
        if (!m_impl->commands->IsIdle())
            return Fail(failure, ViewportFailureCode::Busy, "render frame work must be flushed before unbinding a swapchain", viewport);
        if (rhi::GetSwapChainStats(slot->snapshot.swapChain).state == rhi::SwapChainState::Acquired)
            return Fail(failure, ViewportFailureCode::Busy, "the acquired presentation output must be presented or abandoned before unbinding its swapchain",
                        viewport);
        static_cast<void>(rhi::SafeRelease(slot->snapshot.swapChain));
        slot->snapshot.state = RenderViewportState::AwaitingOutput;
        ++slot->snapshot.outputRevision;
        return true;
    }

    bool ViewportManager::UpdatePresentation(const RenderViewportHandle viewport, const window::PresentationAttachmentSnapshot& presentation,
                                             ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "presentation updates must run on the main thread", viewport);
        Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid render viewport handle", viewport);
        if (slot->snapshot.outputKind != RenderViewportOutputKind::Presentation || presentation.handle != slot->snapshot.presentation ||
            presentation.surfaceKind == window::PresentationSurfaceKind::None || !presentation.pixelExtent.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "presentation snapshot does not belong to this render viewport", viewport);
        if (presentation.requiredPixelExtentRevision < slot->snapshot.requiredPixelExtentRevision ||
            presentation.requiredSurfaceRevision < slot->snapshot.requiredSurfaceRevision ||
            presentation.acknowledgedPixelExtentRevision > presentation.requiredPixelExtentRevision ||
            presentation.acknowledgedSurfaceRevision > presentation.requiredSurfaceRevision)
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "presentation snapshot revisions are stale or inconsistent", viewport);

        const bool suspended =
            !presentation.visible || presentation.minimized || window::HasRequirement(presentation.requirements, window::PresentationRequirement::Suspended);
        const ViewportExtent requestedExtent{presentation.pixelExtent.width, presentation.pixelExtent.height};
        const bool surfaceChanged = presentation.requiredSurfaceRevision > slot->snapshot.appliedSurfaceRevision;
        const bool extentChanged = presentation.requiredPixelExtentRevision > slot->snapshot.appliedPixelExtentRevision;
        const RenderViewportState previousState = slot->snapshot.state;
        const ViewportExtent previousRequestedExtent = slot->snapshot.requestedOutputExtent;
        const ViewportExtent previousOutputExtent = slot->snapshot.outputExtent;
        const u64 previousRequiredPixelRevision = slot->snapshot.requiredPixelExtentRevision;
        const u64 previousRequiredSurfaceRevision = slot->snapshot.requiredSurfaceRevision;
        const bool previousVisible = slot->snapshot.visible;
        const bool previousOccluded = slot->snapshot.occluded;
        const bool hadSwapChain = slot->snapshot.swapChain.IsValid();

        if (slot->snapshot.swapChain.IsValid() && (surfaceChanged || (extentChanged && !suspended)) && !m_impl->commands->IsIdle())
            return Fail(failure, ViewportFailureCode::Busy,
                        surfaceChanged ? "render frame work must be flushed before replacing a swapchain surface"
                                       : "render frame work must be flushed before resizing a swapchain",
                        viewport);
        if (slot->snapshot.swapChain.IsValid() && (surfaceChanged || extentChanged) &&
            rhi::GetSwapChainStats(slot->snapshot.swapChain).state == rhi::SwapChainState::Acquired)
            return Fail(failure, ViewportFailureCode::Busy, "the acquired presentation output must be presented or abandoned before changing its surface",
                        viewport);

        slot->snapshot.requestedOutputExtent = requestedExtent;
        slot->snapshot.requiredPixelExtentRevision = presentation.requiredPixelExtentRevision;
        slot->snapshot.requiredSurfaceRevision = presentation.requiredSurfaceRevision;
        slot->snapshot.visible = presentation.visible;
        slot->snapshot.occluded = presentation.occluded;

        if (slot->snapshot.swapChain.IsValid() && surfaceChanged)
        {
            static_cast<void>(rhi::SafeRelease(slot->snapshot.swapChain));
        }

        if (slot->snapshot.swapChain.IsValid() && extentChanged && !suspended)
        {
            rhi::Failure rhiFailure;
            if (!rhi::ResizeBackbuffer(requestedExtent.width, requestedExtent.height, slot->snapshot.swapChain, &rhiFailure))
            {
                slot->snapshot.state = RenderViewportState::Failed;
                return Fail(failure, ViewportFailureCode::BackendFailure, "swapchain resize failed", viewport, {}, &rhiFailure);
            }
            slot->snapshot.outputExtent = requestedExtent;
            slot->snapshot.appliedPixelExtentRevision = presentation.requiredPixelExtentRevision;
        }

        if (suspended)
            slot->snapshot.state = RenderViewportState::Suspended;
        else if (slot->snapshot.swapChain.IsValid() && slot->snapshot.appliedPixelExtentRevision == slot->snapshot.requiredPixelExtentRevision &&
                 slot->snapshot.appliedSurfaceRevision == slot->snapshot.requiredSurfaceRevision)
            slot->snapshot.state = RenderViewportState::Ready;
        else
            slot->snapshot.state = RenderViewportState::AwaitingOutput;
        if (previousState != slot->snapshot.state || previousRequestedExtent != slot->snapshot.requestedOutputExtent ||
            previousOutputExtent != slot->snapshot.outputExtent || previousRequiredPixelRevision != slot->snapshot.requiredPixelExtentRevision ||
            previousRequiredSurfaceRevision != slot->snapshot.requiredSurfaceRevision || previousVisible != slot->snapshot.visible ||
            previousOccluded != slot->snapshot.occluded || (hadSwapChain && !slot->snapshot.swapChain.IsValid()))
            ++slot->snapshot.outputRevision;
        return true;
    }

    bool ViewportManager::GetPresentationAcknowledgement(const RenderViewportHandle viewport,
                                                         window::PresentationAcknowledgement& acknowledgement) const noexcept
    {
        acknowledgement = {};
        if (!IsInitialized())
            return false;
        const Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr || slot->snapshot.outputKind != RenderViewportOutputKind::Presentation)
            return false;
        acknowledgement.pixelExtentRevision = slot->snapshot.appliedPixelExtentRevision;
        acknowledgement.surfaceRevision = slot->snapshot.appliedSurfaceRevision;
        return acknowledgement.pixelExtentRevision != 0 || acknowledgement.surfaceRevision != 0;
    }

    bool ViewportManager::RequestRenderExtent(const RenderViewportHandle viewport, const ViewportExtent extent, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render extent requests must run on the main thread", viewport);
        Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid render viewport handle", viewport);
        if (!extent.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "render extent must be non-zero", viewport);
        slot->snapshot.renderExtent = extent;
        return true;
    }

    bool ViewportManager::AcquireOutput(const RenderViewportHandle viewport, RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        acquisition = {};
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render-output acquisition must run on the main thread", viewport);
        Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid render viewport handle", viewport);
        if (slot->snapshot.state != RenderViewportState::Ready)
            return Fail(failure, ViewportFailureCode::OutputUnavailable, "render viewport output is not ready for acquisition", viewport);
        if (slot->snapshot.outputKind == RenderViewportOutputKind::Texture && slot->snapshot.outputTexture.IsValid())
        {
            acquisition = {viewport, slot->snapshot.outputTexture, {}, slot->snapshot.outputRevision};
            return true;
        }
        if (slot->snapshot.outputKind != RenderViewportOutputKind::Presentation || !slot->snapshot.swapChain.IsValid() || slot->snapshot.occluded)
            return Fail(failure, ViewportFailureCode::OutputUnavailable, "presentation output is unavailable", viewport);

        rhi::Failure rhiFailure;
        rhi::AcquiredBackBuffer backBuffer;
        if (!rhi::AcquireBackBuffer(slot->snapshot.swapChain, backBuffer, &rhiFailure))
            return Fail(failure, rhiFailure.code == rhi::FailureCode::Busy ? ViewportFailureCode::Busy : ViewportFailureCode::BackendFailure,
                        "swapchain back-buffer acquisition failed", viewport, {}, &rhiFailure);
        acquisition = {viewport, backBuffer.texture, backBuffer, slot->snapshot.outputRevision};
        return true;
    }

    bool ViewportManager::AbandonOutput(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render-output abandonment must run on the main thread", acquisition.viewport);
        Impl::RenderSlot* const slot = m_impl->Find(acquisition.viewport);
        if (slot == nullptr || !acquisition.IsValid() || acquisition.outputRevision != slot->snapshot.outputRevision)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "stale or foreign render-output acquisition", acquisition.viewport);
        if (acquisition.backBuffer.IsValid())
        {
            rhi::Failure rhiFailure;
            if (!rhi::AbandonBackBuffer(acquisition.backBuffer, &rhiFailure))
                return Fail(failure, ViewportFailureCode::BackendFailure, "swapchain back-buffer abandonment failed", acquisition.viewport, {}, &rhiFailure);
        }
        acquisition = {};
        return true;
    }

    bool ViewportManager::Present(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "presentation must run on the main thread", acquisition.viewport);
        Impl::RenderSlot* const slot = m_impl->Find(acquisition.viewport);
        if (slot == nullptr || !acquisition.IsValid() || acquisition.outputRevision != slot->snapshot.outputRevision ||
            slot->snapshot.outputKind != RenderViewportOutputKind::Presentation || slot->snapshot.state != RenderViewportState::Ready ||
            !acquisition.backBuffer.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidHandle, "stale or foreign presentation acquisition", acquisition.viewport);
        rhi::Failure rhiFailure;
        if (!rhi::Present(acquisition.backBuffer, &rhiFailure))
        {
            slot->snapshot.state = RenderViewportState::Failed;
            return Fail(failure, ViewportFailureCode::BackendFailure, "swapchain presentation failed", acquisition.viewport, {}, &rhiFailure);
        }
        ++slot->snapshot.presentedFrames;
        ++m_impl->stats.presentedFrames;
        acquisition = {};
        return true;
    }

    bool ViewportManager::CreateEngineViewport(const EngineViewportDesc& desc, EngineViewportHandle& viewport, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        viewport = {};
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "engine viewport creation must run on the main thread");
        char validatedName[MaximumViewportNameBytes]{};
        Impl::RenderSlot* const output = m_impl->Find(desc.output);
        if (!CopyName(validatedName, desc.contextName) || output == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "invalid engine viewport descriptor", desc.output);

        for (u32 index = 0; index < MaximumEngineViewports; ++index)
        {
            Impl::EngineSlot& slot = m_impl->engineSlots[index];
            if (slot.active)
                continue;
            slot.generation = NextGeneration(slot.generation);
            slot.snapshot = {};
            slot.snapshot.handle = {index, slot.generation};
            slot.snapshot.output = desc.output;
            slot.snapshot.presentByDefault = desc.presentByDefault;
            CopyNameUnchecked(slot.snapshot.contextName, validatedName);
            slot.active = true;
            ++output->snapshot.engineViewportReferences;
            ++m_impl->stats.engineViewports;
            viewport = slot.snapshot.handle;
            return true;
        }
        return Fail(failure, ViewportFailureCode::CapacityExceeded, "maximum engine viewport count exceeded", desc.output);
    }

    bool ViewportManager::DestroyEngineViewport(const EngineViewportHandle viewport, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", {}, viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "engine viewport destruction must run on the main thread", {}, viewport);
        Impl::EngineSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid engine viewport handle", {}, viewport);
        if (slot->snapshot.buildingFrameSerial != 0)
            return Fail(failure, ViewportFailureCode::FrameAlreadyBuilding,
                        "building frame must be submitted or abandoned before destroying its engine viewport", slot->snapshot.output, viewport);
        if (!m_impl->commands->IsIdle())
            return Fail(failure, ViewportFailureCode::Busy, "render frame work must be flushed before destroying an engine viewport", slot->snapshot.output,
                        viewport);
        Impl::RenderSlot* const output = m_impl->Find(slot->snapshot.output);
        if (output != nullptr && output->snapshot.engineViewportReferences != 0)
            --output->snapshot.engineViewportReferences;
        slot->active = false;
        slot->snapshot = {};
        --m_impl->stats.engineViewports;
        return true;
    }

    bool ViewportManager::BeginFrame(const EngineViewportHandle viewport, const RenderFrameSetup& setup, RenderFrameInfo& frame,
                                     ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", {}, viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render frame construction must begin on the main thread", {}, viewport);
        Impl::EngineSlot* const engine = m_impl->Find(viewport);
        if (engine == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid engine viewport handle", {}, viewport);
        Impl::RenderSlot* const output = m_impl->Find(engine->snapshot.output);
        if (output == nullptr || output->snapshot.state == RenderViewportState::AwaitingOutput || output->snapshot.state == RenderViewportState::Failed)
            return Fail(failure, ViewportFailureCode::OutputUnavailable, "engine viewport output is unavailable", engine->snapshot.output, viewport);
        if (engine->snapshot.buildingFrameSerial != 0)
            return Fail(failure, ViewportFailureCode::FrameAlreadyBuilding, "engine viewport already has a building frame", engine->snapshot.output, viewport);
        if (!ValidFrameSetup(setup))
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "invalid render frame setup", engine->snapshot.output, viewport);

        const u64 serial = m_impl->nextFrameSerial++;
        if (m_impl->nextFrameSerial == 0)
            m_impl->nextFrameSerial = 1;
        frame = {};
        frame.m_serial = serial;
        frame.m_engineViewport = viewport;
        frame.m_renderViewport = engine->snapshot.output;
        frame.m_mode = setup.mode;
        frame.m_purpose = setup.purpose;
        frame.m_renderExtent = output->snapshot.renderExtent;
        frame.m_outputExtent = output->snapshot.outputExtent;
        frame.m_present = setup.present && engine->snapshot.presentByDefault && output->snapshot.outputKind == RenderViewportOutputKind::Presentation &&
                          output->snapshot.state == RenderViewportState::Ready && !output->snapshot.occluded;
        CopyNameUnchecked(frame.m_contextName, engine->snapshot.contextName);
        engine->snapshot.buildingFrameSerial = serial;
        ++engine->snapshot.begunFrames;
        ++m_impl->stats.buildingFrames;
        ++m_impl->stats.begunFrames;
        return true;
    }

    bool ViewportManager::ConfigureViews(const EngineViewportHandle viewport, RenderFrameInfo& frame, const RenderFrameViewSetup& setup,
                                         ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", {}, viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render frame views must be configured on the main thread", {}, viewport);
        Impl::EngineSlot* const engine = m_impl->Find(viewport);
        if (engine == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid engine viewport handle", {}, viewport);
        if (engine->snapshot.buildingFrameSerial == 0)
            return Fail(failure, ViewportFailureCode::FrameNotBuilding, "engine viewport has no building frame", engine->snapshot.output, viewport);
        if (frame.m_engineViewport != viewport || frame.m_renderViewport != engine->snapshot.output || frame.m_serial != engine->snapshot.buildingFrameSerial)
            return Fail(failure, ViewportFailureCode::ForeignFrame, "render frame does not belong to this engine viewport", engine->snapshot.output, viewport);
        if (frame.m_viewSetupConfigured || frame.m_viewFamily.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidState, "render frame views are already configured", engine->snapshot.output, viewport);
        if (!setup.scene.IsValid() || setup.rootCameras.Empty() || setup.rootCameras.Size() > MaximumRenderViewsPerFamily ||
            setup.rootCameras.Data() == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "render frame view setup is invalid", engine->snapshot.output, viewport);

        for (u32 index = 0; index < setup.rootCameras.Size(); ++index)
        {
            const RenderCameraHandle camera = setup.rootCameras[index];
            if (!camera.IsValid() || camera.scene != setup.scene)
                return Fail(failure, ViewportFailureCode::InvalidDescriptor, "render frame root camera is invalid or belongs to another scene",
                            engine->snapshot.output, viewport);
            for (u32 previous = 0; previous < index; ++previous)
                if (setup.rootCameras[previous] == camera)
                    return Fail(failure, ViewportFailureCode::InvalidDescriptor, "render frame contains a duplicate root camera", engine->snapshot.output,
                                viewport);
        }

        frame.m_scene = setup.scene;
        frame.m_rootCameraCount = setup.rootCameras.Size();
        frame.m_jitterIndex = setup.jitterIndex;
        frame.m_enableTemporalJitter = setup.enableTemporalJitter;
        frame.m_forceCameraCut = setup.forceCameraCut;
        for (u32 index = 0; index < frame.m_rootCameraCount; ++index)
            frame.m_rootCameras[index] = setup.rootCameras[index];
        frame.m_viewSetupConfigured = true;
        return true;
    }

    bool ViewportManager::SubmitFrame(const EngineViewportHandle viewport, RenderFrameInfo& frame, RenderFrameSubmission& submission,
                                      ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        submission = {};
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", {}, viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render frame submission must run on the main thread", {}, viewport);
        Impl::EngineSlot* const engine = m_impl->Find(viewport);
        if (engine == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid engine viewport handle", {}, viewport);
        if (frame.m_engineViewport.IsValid() && frame.m_engineViewport != viewport)
            return Fail(failure, ViewportFailureCode::ForeignFrame, "render frame does not belong to this engine viewport", engine->snapshot.output, viewport);
        if (engine->snapshot.buildingFrameSerial == 0)
            return Fail(failure, ViewportFailureCode::FrameNotBuilding, "engine viewport has no building frame", engine->snapshot.output, viewport);
        if (frame.m_engineViewport != viewport || frame.m_renderViewport != engine->snapshot.output || frame.m_serial != engine->snapshot.buildingFrameSerial)
            return Fail(failure, ViewportFailureCode::ForeignFrame, "render frame does not belong to this engine viewport", engine->snapshot.output, viewport);
        if (!frame.m_payload.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "render frame payload is invalid", engine->snapshot.output, viewport);
        RenderCommandFailure commandFailure;
        if (!m_impl->commands->RenderFrame(frame, submission, &commandFailure))
            return Fail(failure, ViewportFailureCode::SubmissionFailure,
                        commandFailure.message != nullptr ? commandFailure.message : "render command frame submission failed", frame.GetOutputViewport(),
                        frame.GetEngineViewport());
        Impl::RenderSlot* const output = m_impl->Find(engine->snapshot.output);
        if (output != nullptr)
            ++output->snapshot.renderedFrames;
        engine->snapshot.buildingFrameSerial = 0;
        ++engine->snapshot.submittedFrames;
        --m_impl->stats.buildingFrames;
        ++m_impl->stats.submittedFrames;
        frame = {};
        return true;
    }

    bool ViewportManager::AbandonFrame(const EngineViewportHandle viewport, RenderFrameInfo& frame, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", {}, viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render frame abandonment must run on the main thread", {}, viewport);
        Impl::EngineSlot* const engine = m_impl->Find(viewport);
        if (engine == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid engine viewport handle", {}, viewport);
        if (engine->snapshot.buildingFrameSerial == 0)
            return Fail(failure, ViewportFailureCode::FrameNotBuilding, "engine viewport has no building frame", engine->snapshot.output, viewport);
        if (frame.m_engineViewport != viewport || frame.m_serial != engine->snapshot.buildingFrameSerial)
            return Fail(failure, ViewportFailureCode::ForeignFrame, "render frame does not belong to this engine viewport", engine->snapshot.output, viewport);
        if (frame.m_viewFamily.IsValid())
            frame.m_viewFamily.Release();
        engine->snapshot.buildingFrameSerial = 0;
        --m_impl->stats.buildingFrames;
        frame = {};
        return true;
    }

    bool ViewportManager::FlushFrame(const EngineViewportHandle viewport, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", {}, viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "render frame flush must run on the main thread", {}, viewport);
        const Impl::EngineSlot* const engine = m_impl->Find(viewport);
        if (engine == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid engine viewport handle", {}, viewport);
        RenderCommandFailure commandFailure;
        if (!m_impl->commands->FlushPreviousFrameProcessing(&commandFailure))
            return Fail(failure, ViewportFailureCode::SubmissionFailure,
                        commandFailure.message != nullptr ? commandFailure.message : "render command frame flush failed", engine->snapshot.output, viewport);
        if (m_impl->commands->ConsumeExecutionFailure(commandFailure))
            return Fail(failure, ViewportFailureCode::SubmissionFailure,
                        commandFailure.message != nullptr ? commandFailure.message : "asynchronous render frame execution failed", engine->snapshot.output,
                        viewport);
        return true;
    }

    bool ViewportManager::GetSnapshot(const RenderViewportHandle viewport, RenderViewportSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (!IsInitialized())
            return false;
        const Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return false;
        snapshot = slot->snapshot;
        return true;
    }

    bool ViewportManager::GetSnapshot(const EngineViewportHandle viewport, EngineViewportSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        if (!IsInitialized())
            return false;
        const Impl::EngineSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return false;
        snapshot = slot->snapshot;
        return true;
    }

    bool ViewportManager::Resolve(const RenderViewportHandle handle, RenderViewport& viewport) noexcept
    {
        viewport = {};
        if (!IsInitialized() || m_impl->Find(handle) == nullptr)
            return false;
        viewport.m_manager = this;
        viewport.m_handle = handle;
        return true;
    }

    bool ViewportManager::Resolve(const EngineViewportHandle handle, EngineViewport& viewport) noexcept
    {
        viewport = {};
        if (!IsInitialized() || m_impl->Find(handle) == nullptr)
            return false;
        viewport.m_manager = this;
        viewport.m_handle = handle;
        return true;
    }

    void ViewportManager::VisitRenderViewports(const VisitRenderViewport visitor, void* const userData) const noexcept
    {
        if (!IsInitialized() || visitor == nullptr)
            return;
        for (u32 index = 0; index < MaximumRenderViewports; ++index)
            if (m_impl->renderSlots[index].active)
                visitor(m_impl->renderSlots[index].snapshot, userData);
    }

    void ViewportManager::VisitEngineViewports(const VisitEngineViewport visitor, void* const userData) const noexcept
    {
        if (!IsInitialized() || visitor == nullptr)
            return;
        for (u32 index = 0; index < MaximumEngineViewports; ++index)
            if (m_impl->engineSlots[index].active)
                visitor(m_impl->engineSlots[index].snapshot, userData);
    }

    ViewportManagerStats ViewportManager::GetStats() const noexcept
    {
        return IsInitialized() ? m_impl->stats : ViewportManagerStats{};
    }

    bool RenderViewport::IsValid() const noexcept
    {
        RenderViewportSnapshot snapshot;
        return GetSnapshot(snapshot);
    }

    bool RenderViewport::GetSnapshot(RenderViewportSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        return m_manager != nullptr && m_manager->GetSnapshot(m_handle, snapshot);
    }

    bool RenderViewport::AcquireOutput(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->AcquireOutput(m_handle, acquisition, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport facade is invalid", m_handle);
    }

    bool RenderViewport::RequestRenderExtent(const ViewportExtent extent, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->RequestRenderExtent(m_handle, extent, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport facade is invalid", m_handle);
    }

    bool RenderViewport::BindSwapChain(const rhi::SwapChainRef swapChain, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->BindSwapChain(m_handle, swapChain, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport facade is invalid", m_handle);
    }

    bool RenderViewport::UnbindSwapChain(ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->UnbindSwapChain(m_handle, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport facade is invalid", m_handle);
    }

    bool RenderViewport::UpdatePresentation(const window::PresentationAttachmentSnapshot& presentation, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->UpdatePresentation(m_handle, presentation, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport facade is invalid", m_handle);
    }

    bool RenderViewport::GetPresentationAcknowledgement(window::PresentationAcknowledgement& acknowledgement) const noexcept
    {
        acknowledgement = {};
        return m_manager != nullptr && m_manager->GetPresentationAcknowledgement(m_handle, acknowledgement);
    }

    bool RenderViewport::AbandonOutput(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        if (acquisition.viewport != m_handle)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "render-output acquisition belongs to another viewport", m_handle);
        return m_manager != nullptr ? m_manager->AbandonOutput(acquisition, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport facade is invalid", m_handle);
    }

    bool RenderViewport::Present(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        if (acquisition.viewport != m_handle)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "presentation acquisition belongs to another viewport", m_handle);
        return m_manager != nullptr ? m_manager->Present(acquisition, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport facade is invalid", m_handle);
    }

    bool EngineViewport::IsValid() const noexcept
    {
        EngineViewportSnapshot snapshot;
        return GetSnapshot(snapshot);
    }

    bool EngineViewport::GetSnapshot(EngineViewportSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        return m_manager != nullptr && m_manager->GetSnapshot(m_handle, snapshot);
    }

    bool EngineViewport::BeginFrame(const RenderFrameSetup& setup, RenderFrameInfo& frame, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->BeginFrame(m_handle, setup, frame, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport facade is invalid", {}, m_handle);
    }

    bool EngineViewport::ConfigureViews(RenderFrameInfo& frame, const RenderFrameViewSetup& setup, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->ConfigureViews(m_handle, frame, setup, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport facade is invalid", {}, m_handle);
    }

    bool EngineViewport::SubmitFrame(RenderFrameInfo& frame, RenderFrameSubmission& submission, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->SubmitFrame(m_handle, frame, submission, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport facade is invalid", {}, m_handle);
    }

    bool EngineViewport::AbandonFrame(RenderFrameInfo& frame, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->AbandonFrame(m_handle, frame, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport facade is invalid", {}, m_handle);
    }

    bool EngineViewport::FlushFrame(ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->FlushFrame(m_handle, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport facade is invalid", {}, m_handle);
    }
} // namespace vanguard::rendering

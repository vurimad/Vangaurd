#include <vanguard/rendering/viewport.hpp>
#include <vanguard/rendering/render_command_system.hpp>

#include <vanguard/concurrency/atomic.hpp>
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
            return static_cast<u32>(setup.mode) <= static_cast<u32>(RenderingMode::OverlayOnly) &&
                   static_cast<u32>(setup.purpose) <= static_cast<u32>(RenderFramePurpose::Blank) &&
                   static_cast<u32>(setup.features.debugView) <= static_cast<u32>(RenderDebugView::HitProxies);
        }

    } // namespace

    struct ViewportManager::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        struct RenderSlot
        {
            RenderViewport viewport;
            u32 generation = 0;
            concurrency::Atomic<bool> outputAcquired{false};
        };

        struct EngineSlot
        {
            EngineViewport viewport;
            u32 generation = 0;
        };

        RenderSlot renderSlots[MaximumRenderViewports]{};
        EngineSlot engineSlots[MaximumEngineViewports]{};
        RenderCommandSystem* commands = nullptr;
        ViewportManagerStats stats;
        concurrency::Atomic<u64> presentedFrames{0};
        u64 nextFrameSerial = 1;
        bool initialized = false;

        [[nodiscard]] RenderSlot* Find(const RenderViewportHandle handle) noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            RenderSlot& slot = renderSlots[handle.index];
            return slot.viewport.IsValid() && slot.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] const RenderSlot* Find(const RenderViewportHandle handle) const noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            const RenderSlot& slot = renderSlots[handle.index];
            return slot.viewport.IsValid() && slot.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] EngineSlot* Find(const EngineViewportHandle handle) noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            EngineSlot& slot = engineSlots[handle.index];
            return slot.viewport.IsValid() && slot.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] const EngineSlot* Find(const EngineViewportHandle handle) const noexcept
        {
            if (!handle.IsValid())
                return nullptr;
            const EngineSlot& slot = engineSlots[handle.index];
            return slot.viewport.IsValid() && slot.generation == handle.generation ? &slot : nullptr;
        }

        void Reject() noexcept
        {
            ++stats.rejectedOperations;
        }

        [[nodiscard]] bool JoinRenderTail(ViewportFailure* const failure, const RenderViewportHandle renderViewport = {},
                                          const EngineViewportHandle engineViewport = {}) noexcept
        {
            if (stats.buildingFrames != 0)
            {
                Reject();
                return Fail(failure, ViewportFailureCode::FrameAlreadyBuilding,
                            "viewport mutation must run before render frame construction", renderViewport, engineViewport);
            }
            RenderCommandFailure commandFailure;
            if (!commands->FlushPreviousFrameProcessing(&commandFailure))
                return Fail(failure, ViewportFailureCode::SubmissionFailure,
                            commandFailure.message != nullptr ? commandFailure.message : "render command tail join failed before viewport mutation",
                            renderViewport, engineViewport);
            return true;
        }
    };

    RenderFrameOutputTransaction::~RenderFrameOutputTransaction()
    {
        Reset();
    }

    RenderFrameOutputTransaction::RenderFrameOutputTransaction(RenderFrameOutputTransaction&& other) noexcept
        : m_viewport(other.m_viewport), m_acquisition(other.m_acquisition), m_resourceImportIndex(other.m_resourceImportIndex),
          m_resourceImportGeneration(other.m_resourceImportGeneration), m_presentNodeReached(other.m_presentNodeReached)
    {
        other.m_viewport = nullptr;
        other.m_acquisition = {};
        other.m_resourceImportIndex = ~u32{0};
        other.m_resourceImportGeneration = 0;
        other.m_presentNodeReached = false;
    }

    RenderFrameOutputTransaction& RenderFrameOutputTransaction::operator=(RenderFrameOutputTransaction&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_viewport = other.m_viewport;
        m_acquisition = other.m_acquisition;
        m_resourceImportIndex = other.m_resourceImportIndex;
        m_resourceImportGeneration = other.m_resourceImportGeneration;
        m_presentNodeReached = other.m_presentNodeReached;
        other.m_viewport = nullptr;
        other.m_acquisition = {};
        other.m_resourceImportIndex = ~u32{0};
        other.m_resourceImportGeneration = 0;
        other.m_presentNodeReached = false;
        return *this;
    }

    void RenderFrameOutputTransaction::Reset() noexcept
    {
        if (m_viewport != nullptr && m_acquisition.IsValid())
        {
            ViewportFailure failure;
            if (!m_viewport->AbandonOutput(m_acquisition, &failure))
                m_viewport->DeviceLostOutput(m_acquisition);
        }
        m_viewport = nullptr;
        m_acquisition = {};
        m_resourceImportIndex = ~u32{0};
        m_resourceImportGeneration = 0;
        m_presentNodeReached = false;
    }

    RenderViewportOutputKind RenderFrameOutputTransaction::GetKind() const noexcept
    {
        return m_viewport != nullptr ? m_viewport->GetOutputKind() : RenderViewportOutputKind::Headless;
    }

    RenderViewportOutputKind RenderFrameInfo::GetOutputKind() const noexcept
    {
        return m_viewport != nullptr ? m_viewport->GetOutputKind() : RenderViewportOutputKind::Headless;
    }

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
        if (m_impl->stats.buildingFrames != 0)
            return Fail(failure, ViewportFailureCode::InvalidState, "building render frames must be abandoned before viewport shutdown");
        RenderCommandFailure commandFailure;
        if (!m_impl->commands->FlushPreviousFrameProcessing(&commandFailure))
            return Fail(failure, ViewportFailureCode::SubmissionFailure,
                        commandFailure.message != nullptr ? commandFailure.message : "render command flush failed");

        for (u32 index = 0; index < MaximumRenderViewports; ++index)
        {
            Impl::RenderSlot& slot = m_impl->renderSlots[index];
            if (!slot.viewport.IsValid())
                continue;
            if (slot.outputAcquired.GetValue())
                return Fail(failure, ViewportFailureCode::Busy, "render output ownership survived the drained command tail", slot.viewport.m_handle);
            if (slot.viewport.m_swapChain.IsValid() && rhi::GetSwapChainStats(slot.viewport.m_swapChain).state == rhi::SwapChainState::Acquired)
                return Fail(failure, ViewportFailureCode::Busy, "swap-chain acquisition survived the drained command tail", slot.viewport.m_handle);
        }

        for (u32 index = 0; index < MaximumEngineViewports; ++index)
            m_impl->engineSlots[index].viewport.Reset();
        for (u32 index = 0; index < MaximumRenderViewports; ++index)
        {
            Impl::RenderSlot& slot = m_impl->renderSlots[index];
            if (!slot.viewport.IsValid())
                continue;
            if (slot.viewport.m_swapChain.IsValid())
                static_cast<void>(rhi::SafeRelease(slot.viewport.m_swapChain));
            if (slot.viewport.m_outputTexture.IsValid())
                static_cast<void>(rhi::SafeRelease(slot.viewport.m_outputTexture));
            slot.viewport.Reset();
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
            if (slot.viewport.IsValid())
                continue;
            slot.generation = NextGeneration(slot.generation);
            slot.viewport.Reset();
            slot.viewport.m_manager = this;
            slot.viewport.m_handle = {index, slot.generation};
            slot.viewport.m_outputKind = desc.outputKind;
            slot.viewport.m_state = desc.outputKind == RenderViewportOutputKind::Presentation ? RenderViewportState::AwaitingOutput : RenderViewportState::Ready;
            slot.viewport.m_renderExtent = desc.renderExtent;
            slot.viewport.m_outputExtent = desc.outputExtent;
            slot.viewport.m_requestedOutputExtent = desc.outputExtent;
            slot.viewport.m_presentation = desc.presentation;
            slot.viewport.m_outputTexture = desc.outputTexture;
            slot.viewport.m_visible = desc.outputKind != RenderViewportOutputKind::Presentation;
            CopyNameUnchecked(slot.viewport.m_name, validatedName);
            if (desc.outputTexture.IsValid())
                rhi::AddRef(desc.outputTexture);
            slot.outputAcquired.SetValue(false);
            slot.viewport.m_active = true;
            viewport = slot.viewport.m_handle;
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
        if (slot->viewport.m_engineViewportReferences != 0)
        {
            m_impl->Reject();
            return Fail(failure, ViewportFailureCode::OutputStillReferenced, "engine viewports must be destroyed before their render output", viewport);
        }
        if (!m_impl->JoinRenderTail(failure, viewport))
            return false;
        if (slot->outputAcquired.GetValue())
        {
            m_impl->Reject();
            return Fail(failure, ViewportFailureCode::Busy, "the acquired output must be completed or abandoned before destroying its viewport", viewport);
        }
        if (slot->viewport.m_swapChain.IsValid() && rhi::GetSwapChainStats(slot->viewport.m_swapChain).state == rhi::SwapChainState::Acquired)
        {
            m_impl->Reject();
            return Fail(failure, ViewportFailureCode::Busy, "the acquired presentation output must be presented or abandoned before destroying its viewport",
                        viewport);
        }
        if (slot->viewport.m_swapChain.IsValid())
            static_cast<void>(rhi::SafeRelease(slot->viewport.m_swapChain));
        if (slot->viewport.m_outputTexture.IsValid())
            static_cast<void>(rhi::SafeRelease(slot->viewport.m_outputTexture));
        slot->viewport.Reset();
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
        if (slot->viewport.m_outputKind != RenderViewportOutputKind::Presentation || slot->viewport.m_swapChain.IsValid() || !swapChain.IsValid() ||
            !rhi::IsInitialized() || !rhi::IsResourceReferenceValid(rhi::ResourceRef(swapChain)))
            return Fail(failure, ViewportFailureCode::InvalidState, "render viewport cannot bind the requested swapchain", viewport);
        if (!m_impl->JoinRenderTail(failure, viewport))
            return false;
        if (slot->outputAcquired.GetValue())
            return Fail(failure, ViewportFailureCode::Busy, "the acquired output must finish before binding a swapchain", viewport);
        const rhi::SwapChainStats swapChainStats = rhi::GetSwapChainStats(swapChain);
        if (slot->viewport.m_requiredPixelExtentRevision == 0 || slot->viewport.m_requiredSurfaceRevision == 0 || !slot->viewport.m_requestedOutputExtent.IsValid() ||
            swapChainStats.state == rhi::SwapChainState::Failed || swapChainStats.width == 0 || swapChainStats.height == 0)
            return Fail(failure, ViewportFailureCode::InvalidState, "swapchain binding requires current presentation state and a valid back buffer", viewport);
        rhi::AddRef(swapChain);
        const bool suspended = slot->viewport.m_state == RenderViewportState::Suspended || !slot->viewport.m_visible;
        slot->viewport.m_swapChain = swapChain;
        slot->viewport.m_outputExtent = slot->viewport.m_requestedOutputExtent;
        slot->viewport.m_appliedPixelExtentRevision = slot->viewport.m_requiredPixelExtentRevision;
        slot->viewport.m_appliedSurfaceRevision = slot->viewport.m_requiredSurfaceRevision;
        slot->viewport.m_state = suspended ? RenderViewportState::Suspended : RenderViewportState::Ready;
        ++slot->viewport.m_outputRevision;
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
        if (slot->viewport.m_outputKind != RenderViewportOutputKind::Presentation || !slot->viewport.m_swapChain.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidState, "render viewport has no bound swapchain", viewport);
        if (!m_impl->JoinRenderTail(failure, viewport))
            return false;
        if (slot->outputAcquired.GetValue())
            return Fail(failure, ViewportFailureCode::Busy, "the acquired output must finish before unbinding its swapchain", viewport);
        if (rhi::GetSwapChainStats(slot->viewport.m_swapChain).state == rhi::SwapChainState::Acquired)
            return Fail(failure, ViewportFailureCode::Busy, "the acquired presentation output must be presented or abandoned before unbinding its swapchain",
                        viewport);
        static_cast<void>(rhi::SafeRelease(slot->viewport.m_swapChain));
        slot->viewport.m_state = RenderViewportState::AwaitingOutput;
        ++slot->viewport.m_outputRevision;
        return true;
    }

    bool ViewportManager::UpdatePresentation(const RenderViewportHandle viewport, const RenderViewportPresentationUpdate& update,
                                              RenderViewportPresentationResult& result, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized", viewport);
        if (!concurrency::IsMainThread())
            return Fail(failure, ViewportFailureCode::WrongThread, "presentation updates must run on the main thread", viewport);
        Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "invalid render viewport handle", viewport);
        if (slot->viewport.m_outputKind != RenderViewportOutputKind::Presentation || update.attachment != slot->viewport.m_presentation ||
            !update.pixelExtent.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "presentation update does not belong to this render viewport", viewport);
        if (update.requiredPixelExtentRevision < slot->viewport.m_requiredPixelExtentRevision ||
            update.requiredSurfaceRevision < slot->viewport.m_requiredSurfaceRevision ||
            update.acknowledgedPixelExtentRevision > update.requiredPixelExtentRevision ||
            update.acknowledgedSurfaceRevision > update.requiredSurfaceRevision)
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "presentation update revisions are stale or inconsistent", viewport);
        const bool suspended = !update.visible || update.minimized || update.suspended;
        const ViewportExtent requestedExtent = update.pixelExtent;
        const bool presentationChanged = requestedExtent != slot->viewport.m_requestedOutputExtent ||
                                          update.requiredPixelExtentRevision != slot->viewport.m_requiredPixelExtentRevision ||
                                          update.requiredSurfaceRevision != slot->viewport.m_requiredSurfaceRevision ||
                                          update.visible != slot->viewport.m_visible || update.occluded != slot->viewport.m_occluded ||
                                          suspended != slot->viewport.m_suspended;
        if (!presentationChanged)
            return true;
        if (!m_impl->JoinRenderTail(failure, viewport))
            return false;

        const bool surfaceChanged = update.requiredSurfaceRevision > slot->viewport.m_appliedSurfaceRevision;
        const bool extentChanged = update.requiredPixelExtentRevision > slot->viewport.m_appliedPixelExtentRevision;
        const RenderViewportState previousState = slot->viewport.m_state;
        const ViewportExtent previousRequestedExtent = slot->viewport.m_requestedOutputExtent;
        const ViewportExtent previousOutputExtent = slot->viewport.m_outputExtent;
        const u64 previousRequiredPixelRevision = slot->viewport.m_requiredPixelExtentRevision;
        const u64 previousRequiredSurfaceRevision = slot->viewport.m_requiredSurfaceRevision;
        const bool previousVisible = slot->viewport.m_visible;
        const bool previousOccluded = slot->viewport.m_occluded;
        const bool hadSwapChain = slot->viewport.m_swapChain.IsValid();
        if (slot->outputAcquired.GetValue())
            return Fail(failure, ViewportFailureCode::Busy, "the acquired presentation output must be presented or abandoned before reconciliation", viewport);
        if (slot->viewport.m_swapChain.IsValid() && (surfaceChanged || extentChanged) &&
            rhi::GetSwapChainStats(slot->viewport.m_swapChain).state == rhi::SwapChainState::Acquired)
            return Fail(failure, ViewportFailureCode::Busy, "the acquired presentation output must be presented or abandoned before changing its surface",
                        viewport);

        slot->viewport.m_requestedOutputExtent = requestedExtent;
        slot->viewport.m_requiredPixelExtentRevision = update.requiredPixelExtentRevision;
        slot->viewport.m_requiredSurfaceRevision = update.requiredSurfaceRevision;
        slot->viewport.m_visible = update.visible;
        slot->viewport.m_occluded = update.occluded;
        slot->viewport.m_suspended = suspended;

        if (slot->viewport.m_swapChain.IsValid() && surfaceChanged)
        {
            static_cast<void>(rhi::SafeRelease(slot->viewport.m_swapChain));
            result.surfaceReplaced = true;
        }

        if (slot->viewport.m_swapChain.IsValid() && extentChanged && !suspended)
        {
            rhi::Failure rhiFailure;
            if (!rhi::ResizeBackbuffer(requestedExtent.width, requestedExtent.height, slot->viewport.m_swapChain, &rhiFailure))
            {
                slot->viewport.m_state = RenderViewportState::Failed;
                return Fail(failure, ViewportFailureCode::BackendFailure, "swapchain resize failed", viewport, {}, &rhiFailure);
            }
            slot->viewport.m_outputExtent = requestedExtent;
            slot->viewport.m_appliedPixelExtentRevision = update.requiredPixelExtentRevision;
            result.resizeApplied = true;
        }

        if (suspended)
            slot->viewport.m_state = RenderViewportState::Suspended;
        else if (slot->viewport.m_swapChain.IsValid() && slot->viewport.m_appliedPixelExtentRevision == slot->viewport.m_requiredPixelExtentRevision &&
                 slot->viewport.m_appliedSurfaceRevision == slot->viewport.m_requiredSurfaceRevision)
            slot->viewport.m_state = RenderViewportState::Ready;
        else
            slot->viewport.m_state = RenderViewportState::AwaitingOutput;
        if (previousState != slot->viewport.m_state || previousRequestedExtent != slot->viewport.m_requestedOutputExtent ||
            previousOutputExtent != slot->viewport.m_outputExtent || previousRequiredPixelRevision != slot->viewport.m_requiredPixelExtentRevision ||
            previousRequiredSurfaceRevision != slot->viewport.m_requiredSurfaceRevision || previousVisible != slot->viewport.m_visible ||
            previousOccluded != slot->viewport.m_occluded || (hadSwapChain && !slot->viewport.m_swapChain.IsValid()))
            ++slot->viewport.m_outputRevision;
        return true;
    }

    bool ViewportManager::GetPresentationAcknowledgement(const RenderViewportHandle viewport,
                                                         window::PresentationAcknowledgement& acknowledgement) const noexcept
    {
        acknowledgement = {};
        if (!IsInitialized())
            return false;
        const Impl::RenderSlot* const slot = m_impl->Find(viewport);
        if (slot == nullptr)
            return false;
        if (slot->viewport.m_outputKind != RenderViewportOutputKind::Presentation)
            return false;
        acknowledgement.pixelExtentRevision = slot->viewport.m_appliedPixelExtentRevision;
        acknowledgement.surfaceRevision = slot->viewport.m_appliedSurfaceRevision;
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
        if (slot->viewport.m_renderExtent == extent)
            return true;
        if (!m_impl->JoinRenderTail(failure, viewport))
            return false;
        slot->viewport.m_renderExtent = extent;
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
        if (slot->outputAcquired.GetValue())
            return Fail(failure, ViewportFailureCode::Busy, "render viewport output is already acquired", viewport);
        if (slot->viewport.m_state != RenderViewportState::Ready)
            return Fail(failure, ViewportFailureCode::OutputUnavailable, "render viewport output is not ready for acquisition", viewport);
        if (slot->viewport.m_outputKind == RenderViewportOutputKind::Texture && slot->viewport.m_outputTexture.IsValid())
        {
            acquisition = {viewport, slot->viewport.m_outputTexture, {}, slot->viewport.m_outputRevision};
            slot->outputAcquired.SetValue(true);
            return true;
        }
        if (slot->viewport.m_outputKind != RenderViewportOutputKind::Presentation || !slot->viewport.m_swapChain.IsValid() || slot->viewport.m_occluded)
            return Fail(failure, ViewportFailureCode::OutputUnavailable, "presentation output is unavailable", viewport);

        rhi::Failure rhiFailure;
        rhi::AcquiredBackBuffer backBuffer;
        if (!rhi::AcquireBackBuffer(slot->viewport.m_swapChain, backBuffer, &rhiFailure))
            return Fail(failure, rhiFailure.code == rhi::FailureCode::Busy ? ViewportFailureCode::Busy : ViewportFailureCode::BackendFailure,
                        "swapchain back-buffer acquisition failed", viewport, {}, &rhiFailure);
        acquisition = {viewport, backBuffer.texture, backBuffer, slot->viewport.m_outputRevision};
        slot->outputAcquired.SetValue(true);
        return true;
    }

    bool ViewportManager::AbandonOutput(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized");
        Impl::RenderSlot* const slot = m_impl->Find(acquisition.viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "stale or foreign render-output acquisition", acquisition.viewport);
        if (!slot->outputAcquired.GetValue() || !acquisition.IsValid() || acquisition.outputRevision != slot->viewport.m_outputRevision)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "stale or foreign render-output acquisition", acquisition.viewport);
        if (acquisition.backBuffer.IsValid())
        {
            rhi::Failure rhiFailure;
            if (!rhi::AbandonBackBuffer(acquisition.backBuffer, &rhiFailure))
                return Fail(failure, ViewportFailureCode::BackendFailure, "swapchain back-buffer abandonment failed", acquisition.viewport, {}, &rhiFailure);
        }
        slot->outputAcquired.SetValue(false);
        acquisition = {};
        return true;
    }

    bool ViewportManager::CompleteOutput(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized");
        Impl::RenderSlot* const slot = m_impl->Find(acquisition.viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "stale or foreign texture-output acquisition", acquisition.viewport);
        if (!slot->outputAcquired.GetValue() || !acquisition.IsValid() || acquisition.outputRevision != slot->viewport.m_outputRevision ||
            slot->viewport.m_outputKind != RenderViewportOutputKind::Texture || acquisition.backBuffer.IsValid() || acquisition.texture != slot->viewport.m_outputTexture)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "stale or foreign texture-output acquisition", acquisition.viewport);
        slot->outputAcquired.SetValue(false);
        acquisition = {};
        return true;
    }

    void ViewportManager::DeviceLostOutput(RenderOutputAcquisition& acquisition) noexcept
    {
        if (!IsInitialized())
        {
            acquisition = {};
            return;
        }
        Impl::RenderSlot* const slot = m_impl->Find(acquisition.viewport);
        if (slot != nullptr)
        {
            if (acquisition.IsValid() && acquisition.outputRevision == slot->viewport.m_outputRevision)
            {
                slot->viewport.m_state = RenderViewportState::Failed;
                slot->outputAcquired.SetValue(false);
            }
        }
        acquisition = {};
    }

    bool ViewportManager::Present(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, ViewportFailureCode::NotInitialized, "viewport manager is not initialized");
        Impl::RenderSlot* const slot = m_impl->Find(acquisition.viewport);
        if (slot == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "stale or foreign presentation acquisition", acquisition.viewport);
        if (!slot->outputAcquired.GetValue() || !acquisition.IsValid() || acquisition.outputRevision != slot->viewport.m_outputRevision ||
            slot->viewport.m_outputKind != RenderViewportOutputKind::Presentation || slot->viewport.m_state != RenderViewportState::Ready ||
            !acquisition.backBuffer.IsValid() || acquisition.backBuffer.swapChain != slot->viewport.m_swapChain)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "stale or foreign presentation acquisition", acquisition.viewport);
        rhi::Failure rhiFailure;
        const bool presented = rhi::Present(acquisition.backBuffer, &rhiFailure);
        if (!presented)
        {
            slot->viewport.m_state = RenderViewportState::Failed;
            return Fail(failure, ViewportFailureCode::BackendFailure, "swapchain presentation failed", acquisition.viewport, {}, &rhiFailure);
        }
        ++slot->viewport.m_presentedFrames;
        static_cast<void>(m_impl->presentedFrames.Increment());
        slot->outputAcquired.SetValue(false);
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
        if (!m_impl->JoinRenderTail(failure, desc.output))
            return false;

        for (u32 index = 0; index < MaximumEngineViewports; ++index)
        {
            Impl::EngineSlot& slot = m_impl->engineSlots[index];
            if (slot.viewport.IsValid())
                continue;
            slot.generation = NextGeneration(slot.generation);
            slot.viewport.Reset();
            slot.viewport.m_manager = this;
            slot.viewport.m_handle = {index, slot.generation};
            slot.viewport.m_output = desc.output;
            slot.viewport.m_presentByDefault = desc.presentByDefault;
            CopyNameUnchecked(slot.viewport.m_contextName, validatedName);
            slot.viewport.m_active = true;
            ++output->viewport.m_engineViewportReferences;
            ++m_impl->stats.engineViewports;
            viewport = slot.viewport.m_handle;
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
        if (slot->viewport.m_buildingFrameSerial != 0)
            return Fail(failure, ViewportFailureCode::FrameAlreadyBuilding,
                        "building frame must be submitted or abandoned before destroying its engine viewport", slot->viewport.m_output, viewport);
        if (!m_impl->JoinRenderTail(failure, slot->viewport.m_output, viewport))
            return false;
        Impl::RenderSlot* const output = m_impl->Find(slot->viewport.m_output);
        if (output != nullptr)
        {
            if (output->viewport.m_engineViewportReferences != 0)
                --output->viewport.m_engineViewportReferences;
        }
        slot->viewport.Reset();
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
        Impl::RenderSlot* const output = m_impl->Find(engine->viewport.m_output);
        if (output == nullptr)
            return Fail(failure, ViewportFailureCode::OutputUnavailable, "engine viewport output is unavailable", engine->viewport.m_output, viewport);
        if (output->viewport.m_state == RenderViewportState::AwaitingOutput || output->viewport.m_state == RenderViewportState::Failed)
            return Fail(failure, ViewportFailureCode::OutputUnavailable, "engine viewport output is unavailable", engine->viewport.m_output, viewport);
        if (engine->viewport.m_buildingFrameSerial != 0)
            return Fail(failure, ViewportFailureCode::FrameAlreadyBuilding, "engine viewport already has a building frame", engine->viewport.m_output, viewport);
        if (!ValidFrameSetup(setup))
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "invalid render frame setup", engine->viewport.m_output, viewport);

        const u64 serial = m_impl->nextFrameSerial++;
        if (m_impl->nextFrameSerial == 0)
            m_impl->nextFrameSerial = 1;
        frame = {};
        frame.m_serial = serial;
        frame.m_engineViewport = viewport;
        frame.m_viewport = &output->viewport;
        frame.m_mode = setup.mode;
        frame.m_purpose = setup.purpose;
        frame.m_features = setup.features;
        frame.m_renderExtent = output->viewport.m_renderExtent;
        frame.m_outputExtent = output->viewport.m_outputExtent;
        frame.m_present = setup.present && engine->viewport.m_presentByDefault && output->viewport.m_outputKind == RenderViewportOutputKind::Presentation &&
                          output->viewport.m_state == RenderViewportState::Ready && !output->viewport.m_occluded;
        CopyNameUnchecked(frame.m_contextName, engine->viewport.m_contextName);
        engine->viewport.m_buildingFrameSerial = serial;
        ++engine->viewport.m_begunFrames;
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
        Impl::RenderSlot* const output = m_impl->Find(engine->viewport.m_output);
        if (output == nullptr)
            return Fail(failure, ViewportFailureCode::OutputUnavailable, "engine viewport output is unavailable", engine->viewport.m_output, viewport);
        if (engine->viewport.m_buildingFrameSerial == 0)
            return Fail(failure, ViewportFailureCode::FrameNotBuilding, "engine viewport has no building frame", engine->viewport.m_output, viewport);
        if (frame.m_engineViewport != viewport || frame.m_viewport != &output->viewport || frame.m_serial != engine->viewport.m_buildingFrameSerial)
            return Fail(failure, ViewportFailureCode::ForeignFrame, "render frame does not belong to this engine viewport", engine->viewport.m_output, viewport);
        if (frame.m_viewSetupConfigured || frame.m_viewFamily.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidState, "render frame views are already configured", engine->viewport.m_output, viewport);
        if (!setup.scene.IsValid() || setup.rootCameras.Empty() || setup.rootCameras.Size() > MaximumRenderViewsPerFamily ||
            setup.rootCameras.Data() == nullptr)
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "render frame view setup is invalid", engine->viewport.m_output, viewport);

        for (u32 index = 0; index < setup.rootCameras.Size(); ++index)
        {
            const RenderCameraHandle camera = setup.rootCameras[index];
            if (!camera.IsValid() || camera.scene != setup.scene)
                return Fail(failure, ViewportFailureCode::InvalidDescriptor, "render frame root camera is invalid or belongs to another scene",
                            engine->viewport.m_output, viewport);
            for (u32 previous = 0; previous < index; ++previous)
                if (setup.rootCameras[previous] == camera)
                    return Fail(failure, ViewportFailureCode::InvalidDescriptor, "render frame contains a duplicate root camera", engine->viewport.m_output,
                                viewport);
        }

        if (setup.outputRegions.Size() > MaximumRenderViewsPerFamily ||
            (!setup.outputRegions.Empty() && setup.outputRegions.Data() == nullptr))
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "invalid camera output region storage", engine->viewport.m_output, viewport);
        for (u32 index = 0; index < setup.outputRegions.Size(); ++index)
        {
            const auto& region = setup.outputRegions[index];
            bool isRoot = false;
            for (const auto root : setup.rootCameras)
                isRoot |= root == region.camera;
            const auto extent = frame.GetOutputExtent();
            if (!isRoot || !region.rect.IsValid() || region.rect.x >= extent.width || region.rect.y >= extent.height ||
                region.rect.width > extent.width - region.rect.x || region.rect.height > extent.height - region.rect.y)
                return Fail(failure, ViewportFailureCode::InvalidDescriptor, "camera output region must name a root and fit the output", engine->viewport.m_output, viewport);
            for (u32 previous = 0; previous < index; ++previous)
                if (setup.outputRegions[previous].camera == region.camera)
                    return Fail(failure, ViewportFailureCode::InvalidDescriptor, "duplicate camera output region", engine->viewport.m_output, viewport);
        }

        frame.m_outputRegionCount = setup.outputRegions.Size();
        for (u32 index = 0; index < frame.m_outputRegionCount; ++index)
            frame.m_outputRegions[index] = setup.outputRegions[index];
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
        Impl::RenderSlot* const output = m_impl->Find(engine->viewport.m_output);
        if (output == nullptr)
            return Fail(failure, ViewportFailureCode::OutputUnavailable, "engine viewport output is unavailable", engine->viewport.m_output, viewport);
        if (frame.m_engineViewport.IsValid() && frame.m_engineViewport != viewport)
            return Fail(failure, ViewportFailureCode::ForeignFrame, "render frame does not belong to this engine viewport", engine->viewport.m_output, viewport);
        if (engine->viewport.m_buildingFrameSerial == 0)
            return Fail(failure, ViewportFailureCode::FrameNotBuilding, "engine viewport has no building frame", engine->viewport.m_output, viewport);
        if (frame.m_engineViewport != viewport || frame.m_viewport != &output->viewport || frame.m_serial != engine->viewport.m_buildingFrameSerial)
            return Fail(failure, ViewportFailureCode::ForeignFrame, "render frame does not belong to this engine viewport", engine->viewport.m_output, viewport);
        if (!frame.m_payload.IsValid())
            return Fail(failure, ViewportFailureCode::InvalidDescriptor, "render frame payload is invalid", engine->viewport.m_output, viewport);
        RenderFrameOutputTransaction outputTransaction;
        if (frame.GetOutputKind() == RenderViewportOutputKind::Texture ||
            (frame.GetOutputKind() == RenderViewportOutputKind::Presentation && frame.ShouldPresent()))
        {
            RenderOutputAcquisition acquisition;
            if (!frame.GetViewport()->AcquireOutput(acquisition, failure))
                return false;
            outputTransaction.m_viewport = frame.GetViewport();
            outputTransaction.m_acquisition = acquisition;
        }
        RenderCommandFailure commandFailure;
        if (!m_impl->commands->RenderFrame(frame, outputTransaction, submission, &commandFailure))
            return Fail(failure, ViewportFailureCode::SubmissionFailure,
                        commandFailure.message != nullptr ? commandFailure.message : "render command frame submission failed", frame.GetViewport()->GetHandle(),
                        frame.GetEngineViewport());
        ++output->viewport.m_renderedFrames;
        engine->viewport.m_buildingFrameSerial = 0;
        ++engine->viewport.m_submittedFrames;
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
        if (engine->viewport.m_buildingFrameSerial == 0)
            return Fail(failure, ViewportFailureCode::FrameNotBuilding, "engine viewport has no building frame", engine->viewport.m_output, viewport);
        if (frame.m_engineViewport != viewport || frame.m_serial != engine->viewport.m_buildingFrameSerial)
            return Fail(failure, ViewportFailureCode::ForeignFrame, "render frame does not belong to this engine viewport", engine->viewport.m_output, viewport);
        if (frame.m_viewFamily.IsValid())
            frame.m_viewFamily.Release();
        engine->viewport.m_buildingFrameSerial = 0;
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
                        commandFailure.message != nullptr ? commandFailure.message : "render command frame flush failed", engine->viewport.m_output, viewport);
        if (m_impl->commands->ConsumeExecutionFailure(commandFailure))
            return Fail(failure, ViewportFailureCode::SubmissionFailure,
                        commandFailure.message != nullptr ? commandFailure.message : "asynchronous render frame execution failed", engine->viewport.m_output,
                        viewport);
        return true;
    }

    RenderViewport* ViewportManager::Resolve(const RenderViewportHandle handle) noexcept
    {
        Impl::RenderSlot* const slot = IsInitialized() ? m_impl->Find(handle) : nullptr;
        return slot != nullptr ? &slot->viewport : nullptr;
    }

    const RenderViewport* ViewportManager::Resolve(const RenderViewportHandle handle) const noexcept
    {
        const Impl::RenderSlot* const slot = IsInitialized() ? m_impl->Find(handle) : nullptr;
        return slot != nullptr ? &slot->viewport : nullptr;
    }

    EngineViewport* ViewportManager::Resolve(const EngineViewportHandle handle) noexcept
    {
        Impl::EngineSlot* const slot = IsInitialized() ? m_impl->Find(handle) : nullptr;
        return slot != nullptr ? &slot->viewport : nullptr;
    }

    const EngineViewport* ViewportManager::Resolve(const EngineViewportHandle handle) const noexcept
    {
        const Impl::EngineSlot* const slot = IsInitialized() ? m_impl->Find(handle) : nullptr;
        return slot != nullptr ? &slot->viewport : nullptr;
    }

    void ViewportManager::VisitRenderViewports(const VisitRenderViewport visitor, void* const userData) const noexcept
    {
        if (!IsInitialized() || visitor == nullptr)
            return;
        for (u32 index = 0; index < MaximumRenderViewports; ++index)
            if (m_impl->renderSlots[index].viewport.IsValid())
                visitor(m_impl->renderSlots[index].viewport, userData);
    }

    void ViewportManager::VisitEngineViewports(const VisitEngineViewport visitor, void* const userData) const noexcept
    {
        if (!IsInitialized() || visitor == nullptr)
            return;
        for (u32 index = 0; index < MaximumEngineViewports; ++index)
            if (m_impl->engineSlots[index].viewport.IsValid())
                visitor(m_impl->engineSlots[index].viewport, userData);
    }

    ViewportManagerStats ViewportManager::GetStats() const noexcept
    {
        if (!IsInitialized())
            return {};
        ViewportManagerStats stats = m_impl->stats;
        stats.presentedFrames = m_impl->presentedFrames.GetValue();
        return stats;
    }

    bool RenderViewport::IsValid() const noexcept
    {
        return m_active && m_manager != nullptr && m_handle.IsValid();
    }

    void RenderViewport::Reset() noexcept
    {
        m_manager = nullptr;
        m_handle = {};
        m_outputKind = RenderViewportOutputKind::Headless;
        m_state = RenderViewportState::Vacant;
        m_renderExtent = {};
        m_outputExtent = {};
        m_presentation = {};
        m_swapChain = {};
        m_outputTexture = {};
        m_requestedOutputExtent = {};
        m_requiredPixelExtentRevision = 0;
        m_appliedPixelExtentRevision = 0;
        m_requiredSurfaceRevision = 0;
        m_appliedSurfaceRevision = 0;
        m_outputRevision = 0;
        m_renderedFrames = 0;
        m_presentedFrames = 0;
        m_engineViewportReferences = 0;
        m_visible = false;
        m_occluded = false;
        m_suspended = false;
        m_active = false;
        m_name[0] = '\0';
    }

    bool RenderViewport::AcquireOutput(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->AcquireOutput(m_handle, acquisition, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport is invalid", m_handle);
    }

    bool RenderViewport::RequestRenderExtent(const ViewportExtent extent, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->RequestRenderExtent(m_handle, extent, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport is invalid", m_handle);
    }

    bool RenderViewport::BindSwapChain(const rhi::SwapChainRef swapChain, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->BindSwapChain(m_handle, swapChain, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport is invalid", m_handle);
    }

    bool RenderViewport::UnbindSwapChain(ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->UnbindSwapChain(m_handle, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport is invalid", m_handle);
    }

    bool RenderViewport::UpdatePresentation(const RenderViewportPresentationUpdate& update, RenderViewportPresentationResult& result,
                                            ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->UpdatePresentation(m_handle, update, result, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport is invalid", m_handle);
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
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport is invalid", m_handle);
    }

    bool RenderViewport::Present(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        if (acquisition.viewport != m_handle)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "presentation acquisition belongs to another viewport", m_handle);
        return m_manager != nullptr ? m_manager->Present(acquisition, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport is invalid", m_handle);
    }

    bool EngineViewport::IsValid() const noexcept
    {
        return m_active && m_manager != nullptr && m_handle.IsValid();
    }

    bool RenderViewport::CompleteOutput(RenderOutputAcquisition& acquisition, ViewportFailure* const failure) noexcept
    {
        if (acquisition.viewport != m_handle)
            return Fail(failure, ViewportFailureCode::InvalidHandle, "render-output acquisition belongs to another viewport", m_handle);
        return m_manager != nullptr ? m_manager->CompleteOutput(acquisition, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "render viewport is invalid", m_handle);
    }

    void RenderViewport::DeviceLostOutput(RenderOutputAcquisition& acquisition) noexcept
    {
        if (m_manager != nullptr && acquisition.viewport == m_handle)
            m_manager->DeviceLostOutput(acquisition);
    }

    void EngineViewport::Reset() noexcept
    {
        m_manager = nullptr;
        m_handle = {};
        m_output = {};
        m_begunFrames = 0;
        m_submittedFrames = 0;
        m_buildingFrameSerial = 0;
        m_presentByDefault = true;
        m_active = false;
        m_contextName[0] = '\0';
    }

    bool EngineViewport::BeginFrame(const RenderFrameSetup& setup, RenderFrameInfo& frame, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->BeginFrame(m_handle, setup, frame, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport is invalid", {}, m_handle);
    }

    bool EngineViewport::ConfigureViews(RenderFrameInfo& frame, const RenderFrameViewSetup& setup, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->ConfigureViews(m_handle, frame, setup, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport is invalid", {}, m_handle);
    }

    bool EngineViewport::SubmitFrame(RenderFrameInfo& frame, RenderFrameSubmission& submission, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->SubmitFrame(m_handle, frame, submission, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport is invalid", {}, m_handle);
    }

    bool EngineViewport::AbandonFrame(RenderFrameInfo& frame, ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->AbandonFrame(m_handle, frame, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport is invalid", {}, m_handle);
    }

    bool EngineViewport::FlushFrame(ViewportFailure* const failure) noexcept
    {
        return m_manager != nullptr ? m_manager->FlushFrame(m_handle, failure)
                                    : Fail(failure, ViewportFailureCode::InvalidHandle, "engine viewport is invalid", {}, m_handle);
    }
} // namespace vanguard::rendering

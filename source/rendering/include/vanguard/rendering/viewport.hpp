#pragma once

#include <vanguard/jobs/jobs.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/window/window_types.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumRenderViewports = 64;
    inline constexpr u32 MaximumEngineViewports = 64;
    inline constexpr u32 MaximumViewportNameBytes = 96;

    struct RenderViewportHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumRenderViewports && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const RenderViewportHandle&,
                                                       const RenderViewportHandle&) noexcept = default;
    };

    struct EngineViewportHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEngineViewports && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const EngineViewportHandle&,
                                                       const EngineViewportHandle&) noexcept = default;
    };

    inline constexpr RenderViewportHandle InvalidRenderViewportHandle{};
    inline constexpr EngineViewportHandle InvalidEngineViewportHandle{};

    struct ViewportExtent
    {
        u32 width = 0;
        u32 height = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return width != 0 && height != 0; }
        [[nodiscard]] friend constexpr bool operator==(const ViewportExtent&, const ViewportExtent&) noexcept = default;
    };

    enum class RenderViewportOutputKind : u8
    {
        Presentation,
        Texture,
        Headless
    };

    enum class RenderViewportState : u8
    {
        Vacant,
        AwaitingOutput,
        Ready,
        Suspended,
        Failed
    };

    enum class RenderingMode : u8
    {
        Shaded,
        Selection,
        SafeMode
    };

    enum class RenderFramePurpose : u8
    {
        Normal,
        Selection,
        Capture,
        Thumbnail,
        Diagnostic
    };

    enum class ViewportFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDescriptor,
        InvalidHandle,
        InvalidState,
        CapacityExceeded,
        OutputUnavailable,
        OutputStillReferenced,
        FrameAlreadyBuilding,
        FrameNotBuilding,
        ForeignFrame,
        SubmissionFailure,
        BackendFailure,
        Busy
    };

    struct ViewportFailure
    {
        ViewportFailureCode code = ViewportFailureCode::None;
        RenderViewportHandle renderViewport;
        EngineViewportHandle engineViewport;
        rhi::Failure rhiFailure;
        const char* message = nullptr;
    };

    struct RenderViewportDesc
    {
        const char* name = nullptr;
        RenderViewportOutputKind outputKind = RenderViewportOutputKind::Headless;
        ViewportExtent renderExtent{1280, 720};
        ViewportExtent outputExtent{1280, 720};
        window::PresentationAttachmentHandle presentation;
        rhi::TextureRef outputTexture;
    };

    struct EngineViewportDesc
    {
        const char* contextName = nullptr;
        RenderViewportHandle output;
        bool presentByDefault = true;
    };

    struct RenderViewportSnapshot
    {
        RenderViewportHandle handle;
        RenderViewportOutputKind outputKind = RenderViewportOutputKind::Headless;
        RenderViewportState state = RenderViewportState::Vacant;
        ViewportExtent renderExtent;
        ViewportExtent outputExtent;
        window::PresentationAttachmentHandle presentation;
        rhi::SwapChainRef swapChain;
        rhi::TextureRef outputTexture;
        ViewportExtent requestedOutputExtent;
        u64 requiredPixelExtentRevision = 0;
        u64 appliedPixelExtentRevision = 0;
        u64 requiredSurfaceRevision = 0;
        u64 appliedSurfaceRevision = 0;
        u64 outputRevision = 0;
        u64 renderedFrames = 0;
        u64 presentedFrames = 0;
        u32 engineViewportReferences = 0;
        bool visible = false;
        bool occluded = false;
        char name[MaximumViewportNameBytes]{};
    };

    struct EngineViewportSnapshot
    {
        EngineViewportHandle handle;
        RenderViewportHandle output;
        u64 begunFrames = 0;
        u64 submittedFrames = 0;
        u64 buildingFrameSerial = 0;
        bool presentByDefault = true;
        char contextName[MaximumViewportNameBytes]{};
    };

    struct RenderFramePayload
    {
        void* data = nullptr;
        void (*retain)(void* data) noexcept = nullptr;
        void (*release)(void* data) noexcept = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return data == nullptr || (retain != nullptr && release != nullptr);
        }
    };

    struct RenderFrameSetup
    {
        RenderingMode mode = RenderingMode::Shaded;
        RenderFramePurpose purpose = RenderFramePurpose::Normal;
        bool present = true;
    };

    class RenderFrameInfo final
    {
    public:
        RenderFrameInfo() noexcept = default;

        [[nodiscard]] u64 Serial() const noexcept { return m_serial; }
        [[nodiscard]] EngineViewportHandle EngineViewport() const noexcept { return m_engineViewport; }
        [[nodiscard]] RenderViewportHandle OutputViewport() const noexcept { return m_renderViewport; }
        [[nodiscard]] RenderingMode Mode() const noexcept { return m_mode; }
        [[nodiscard]] RenderFramePurpose Purpose() const noexcept { return m_purpose; }
        [[nodiscard]] ViewportExtent RenderExtent() const noexcept { return m_renderExtent; }
        [[nodiscard]] ViewportExtent OutputExtent() const noexcept { return m_outputExtent; }
        [[nodiscard]] bool ShouldPresent() const noexcept { return m_present; }
        [[nodiscard]] const char* ContextName() const noexcept { return m_contextName; }
        [[nodiscard]] const RenderFramePayload& Payload() const noexcept { return m_payload; }

        [[nodiscard]] bool SetPayload(const RenderFramePayload& payload) noexcept
        {
            if (!payload.IsValid()) return false;
            m_payload = payload;
            return true;
        }

    private:
        friend class RenderFrameDispatcher;
        friend class ViewportManager;

        u64 m_serial = 0;
        EngineViewportHandle m_engineViewport;
        RenderViewportHandle m_renderViewport;
        RenderingMode m_mode = RenderingMode::Shaded;
        RenderFramePurpose m_purpose = RenderFramePurpose::Normal;
        ViewportExtent m_renderExtent;
        ViewportExtent m_outputExtent;
        RenderFramePayload m_payload;
        bool m_present = false;
        char m_contextName[MaximumViewportNameBytes]{};
    };

    struct RenderFrameSubmission
    {
        u64 serial = 0;
        [[nodiscard]] constexpr bool IsValid() const noexcept { return serial != 0; }
    };

    /// Identifies the exact output image acquired for one render frame. Presentation outputs must be
    /// explicitly presented or abandoned before their viewport can be resized, rebound, or destroyed.
    struct RenderOutputAcquisition
    {
        RenderViewportHandle viewport;
        rhi::TextureRef texture;
        rhi::AcquiredBackBuffer backBuffer;
        u64 outputRevision = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return viewport.IsValid() && texture.IsValid() && outputRevision != 0;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    };

    class ViewportManager;

    /// Non-owning, generation-checked rendering-output facade. ViewportManager retains ownership.
    class RenderViewport final
    {
    public:
        RenderViewport() noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] RenderViewportHandle Handle() const noexcept { return m_handle; }
        [[nodiscard]] bool Snapshot(RenderViewportSnapshot& snapshot) const noexcept;
        [[nodiscard]] bool AcquireOutput(RenderOutputAcquisition& acquisition,
                                         ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonOutput(RenderOutputAcquisition& acquisition,
                                         ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestRenderExtent(ViewportExtent extent, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BindSwapChain(rhi::SwapChainRef swapChain, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnbindSwapChain(ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdatePresentation(const window::PresentationAttachmentSnapshot& presentation,
                                              ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetPresentationAcknowledgement(
            window::PresentationAcknowledgement& acknowledgement) const noexcept;
        [[nodiscard]] bool Present(RenderOutputAcquisition& acquisition,
                                   ViewportFailure* failure = nullptr) noexcept;

    private:
        friend class ViewportManager;
        ViewportManager* m_manager = nullptr;
        RenderViewportHandle m_handle;
    };

    /// Non-owning, generation-checked frame construction and submission entry point.
    class EngineViewport final
    {
    public:
        EngineViewport() noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] EngineViewportHandle Handle() const noexcept { return m_handle; }
        [[nodiscard]] bool Snapshot(EngineViewportSnapshot& snapshot) const noexcept;
        [[nodiscard]] bool BeginFrame(const RenderFrameSetup& setup, RenderFrameInfo& frame,
                                      ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SubmitFrame(RenderFrameInfo& frame, RenderFrameSubmission& submission,
                                       ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonFrame(RenderFrameInfo& frame, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool FlushFrame(ViewportFailure* failure = nullptr) noexcept;

    private:
        friend class ViewportManager;
        ViewportManager* m_manager = nullptr;
        EngineViewportHandle m_handle;
    };

    struct RenderFrameExecutionStatus
    {
        bool success = true;
        const char* message = nullptr;

        [[nodiscard]] static constexpr RenderFrameExecutionStatus Success() noexcept { return {}; }
        [[nodiscard]] static constexpr RenderFrameExecutionStatus Failure(const char* const message) noexcept
        {
            return {false, message};
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return success; }
    };

    using ExecuteRenderFrame = RenderFrameExecutionStatus (*)(const RenderFrameInfo& frame,
                                                               const jobs::JobContext& context,
                                                               void* userData) noexcept;

    struct RenderFrameDispatcherStats
    {
        u64 submittedFrames = 0;
        u64 completedFrames = 0;
        u64 failedFrames = 0;
        u64 lastCompletedSerial = 0;
        bool initialized = false;
        bool workOutstanding = false;
    };

    class RenderFrameDispatcher final
    {
    public:
        struct Impl;

        RenderFrameDispatcher() noexcept = default;
        ~RenderFrameDispatcher();

        RenderFrameDispatcher(const RenderFrameDispatcher&) = delete;
        RenderFrameDispatcher& operator=(const RenderFrameDispatcher&) = delete;

        [[nodiscard]] bool Initialize(ExecuteRenderFrame execute, void* userData = nullptr,
                                      ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool Submit(const RenderFrameInfo& frame, RenderFrameSubmission& submission,
                                  ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Flush(ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsIdle() const noexcept;
        [[nodiscard]] RenderFrameDispatcherStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };

    struct ViewportManagerStats
    {
        u32 renderViewports = 0;
        u32 engineViewports = 0;
        u32 buildingFrames = 0;
        u64 begunFrames = 0;
        u64 submittedFrames = 0;
        u64 presentedFrames = 0;
        u64 rejectedOperations = 0;
    };

    using VisitRenderViewport = void (*)(const RenderViewportSnapshot& viewport, void* userData) noexcept;
    using VisitEngineViewport = void (*)(const EngineViewportSnapshot& viewport, void* userData) noexcept;

    class ViewportManager final
    {
    public:
        struct Impl;

        ViewportManager() noexcept = default;
        ~ViewportManager();

        ViewportManager(const ViewportManager&) = delete;
        ViewportManager& operator=(const ViewportManager&) = delete;

        [[nodiscard]] bool Initialize(RenderFrameDispatcher& dispatcher,
                                      ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool CreateRenderViewport(const RenderViewportDesc& desc, RenderViewportHandle& viewport,
                                                ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyRenderViewport(RenderViewportHandle viewport,
                                                 ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BindSwapChain(RenderViewportHandle viewport, rhi::SwapChainRef swapChain,
                                         ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnbindSwapChain(RenderViewportHandle viewport,
                                           ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdatePresentation(RenderViewportHandle viewport,
                                              const window::PresentationAttachmentSnapshot& presentation,
                                              ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetPresentationAcknowledgement(
            RenderViewportHandle viewport, window::PresentationAcknowledgement& acknowledgement) const noexcept;
        [[nodiscard]] bool RequestRenderExtent(RenderViewportHandle viewport, ViewportExtent extent,
                                               ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AcquireOutput(RenderViewportHandle viewport, RenderOutputAcquisition& acquisition,
                                         ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonOutput(RenderOutputAcquisition& acquisition,
                                         ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Present(RenderOutputAcquisition& acquisition,
                                   ViewportFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool CreateEngineViewport(const EngineViewportDesc& desc, EngineViewportHandle& viewport,
                                                ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyEngineViewport(EngineViewportHandle viewport,
                                                 ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginFrame(EngineViewportHandle viewport, const RenderFrameSetup& setup,
                                      RenderFrameInfo& frame, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SubmitFrame(EngineViewportHandle viewport, RenderFrameInfo& frame,
                                       RenderFrameSubmission& submission,
                                       ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonFrame(EngineViewportHandle viewport, RenderFrameInfo& frame,
                                        ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool FlushFrame(EngineViewportHandle viewport,
                                      ViewportFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool Snapshot(RenderViewportHandle viewport, RenderViewportSnapshot& snapshot) const noexcept;
        [[nodiscard]] bool Snapshot(EngineViewportHandle viewport, EngineViewportSnapshot& snapshot) const noexcept;
        [[nodiscard]] bool Resolve(RenderViewportHandle handle, RenderViewport& viewport) noexcept;
        [[nodiscard]] bool Resolve(EngineViewportHandle handle, EngineViewport& viewport) noexcept;
        void VisitRenderViewports(VisitRenderViewport visitor, void* userData = nullptr) const noexcept;
        void VisitEngineViewports(VisitEngineViewport visitor, void* userData = nullptr) const noexcept;
        [[nodiscard]] ViewportManagerStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

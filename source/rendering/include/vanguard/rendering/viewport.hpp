#pragma once

#include <vanguard/jobs/jobs.hpp>
#include <vanguard/rendering/render_camera.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/window/window_types.hpp>

namespace vanguard::rendering
{
    class RenderCommandSystem;

    class RenderNodeGraph;
    class RenderFlowResourceAllocator;
    class RenderFrameInfo;
    struct RenderFlowResourceFailure;

    /// Optional caller-owned graph, kept alive through the command-chain join.
    /// Mutable per-frame data belongs in the retained frame payload, not in cached nodes.
    struct RenderFrameGraph
    {
        RenderNodeGraph* graph = nullptr;
        bool (*registerImports)(RenderFlowResourceAllocator&, const RenderFrameInfo&, RenderFlowResourceFailure*) noexcept = nullptr;
        bool processResourceEviction = false;
    };

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
        [[nodiscard]] friend constexpr bool operator==(const RenderViewportHandle&, const RenderViewportHandle&) noexcept = default;
    };

    struct EngineViewportHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEngineViewports && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const EngineViewportHandle&, const EngineViewportHandle&) noexcept = default;
    };

    inline constexpr RenderViewportHandle InvalidRenderViewportHandle{};
    inline constexpr EngineViewportHandle InvalidEngineViewportHandle{};

    struct ViewportExtent
    {
        u32 width = 0;
        u32 height = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return width != 0 && height != 0;
        }
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
        SafeMode,
        ShadedNoAmbient,
        GBufferOnly,
        TodvisBake,
        OverlayOnly
    };

    enum class RenderFramePurpose : u8
    {
        Normal,
        Selection,
        Capture,
        Thumbnail,
        Diagnostic,
        Blank
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

    struct RenderViewportPresentationUpdate
    {
        window::PresentationAttachmentHandle attachment;
        ViewportExtent pixelExtent;
        u64 requiredPixelExtentRevision = 0;
        u64 requiredSurfaceRevision = 0;
        u64 acknowledgedPixelExtentRevision = 0;
        u64 acknowledgedSurfaceRevision = 0;
        bool visible = false;
        bool occluded = false;
        bool minimized = false;
        bool suspended = false;
    };

    struct RenderViewportPresentationResult
    {
        bool resizeApplied = false;
        bool surfaceReplaced = false;
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

    enum class RenderDebugView : u8
    {
        None,
        Todvis,
        Visualization,
        HitProxies
    };

    struct RenderFrameFeatures
    {
        RenderDebugView debugView = RenderDebugView::None;
        bool wireframe = false;
        bool multilayerSelection = false;
        bool gameMode = false;
        bool enablePlacedResources = false;
        bool geometryDiagnostics = false;
    };

    struct RenderFrameSetup
    {
        RenderingMode mode = RenderingMode::Shaded;
        RenderFramePurpose purpose = RenderFramePurpose::Normal;
        bool present = true;
        RenderFrameFeatures features;
    };

    struct RenderFrameViewSetup
    {
        RenderSceneHandle scene;
        containers::ArraySpan<const RenderCameraHandle> rootCameras;
        u32 jitterIndex = 0;
        bool enableTemporalJitter = true;
        bool forceCameraCut = false;
        /// Empty retains single-primary output selection. Explicit regions select
        /// root cameras only; dependency cameras are not implicitly presented.
        containers::ArraySpan<const RenderCameraOutputRegion> outputRegions;
    };

    class RenderViewport;

    class RenderFrameInfo final
    {
    public:
        RenderFrameInfo() noexcept = default;

        [[nodiscard]] u64 GetSerial() const noexcept
        {
            return m_serial;
        }
        [[nodiscard]] EngineViewportHandle GetEngineViewport() const noexcept
        {
            return m_engineViewport;
        }
        [[nodiscard]] RenderViewport* GetViewport() const noexcept
        {
            return m_viewport;
        }
        [[nodiscard]] RenderingMode GetMode() const noexcept
        {
            return m_mode;
        }
        [[nodiscard]] RenderFramePurpose GetPurpose() const noexcept
        {
            return m_purpose;
        }
        [[nodiscard]] const RenderFrameFeatures& GetFeatures() const noexcept
        {
            return m_features;
        }
        [[nodiscard]] ViewportExtent GetRenderExtent() const noexcept
        {
            return m_renderExtent;
        }
        [[nodiscard]] ViewportExtent GetOutputExtent() const noexcept
        {
            return m_outputExtent;
        }
        [[nodiscard]] RenderViewportOutputKind GetOutputKind() const noexcept;
        [[nodiscard]] bool ShouldPresent() const noexcept
        {
            return m_present;
        }
        [[nodiscard]] const char* GetContextName() const noexcept
        {
            return m_contextName;
        }
        [[nodiscard]] const RenderFrameGraph& GetGraph() const noexcept { return m_graph; }
        void SetGraph(const RenderFrameGraph& graph) noexcept { m_graph = graph; }
        [[nodiscard]] const RenderFramePayload& GetPayload() const noexcept
        {
            return m_payload;
        }
        [[nodiscard]] const PreparedRenderViewFamily& GetViewFamily() const noexcept
        {
            return m_viewFamily;
        }
        [[nodiscard]] bool HasViewSetup() const noexcept
        {
            return m_viewSetupConfigured;
        }
        [[nodiscard]] RenderFrameViewSetup GetViewSetup() const noexcept
        {
            return {m_scene, containers::ArraySpan<const RenderCameraHandle>(m_rootCameras, m_rootCameraCount), m_jitterIndex, m_enableTemporalJitter,
                    m_forceCameraCut, {m_outputRegions, m_outputRegionCount}};
        }

        /// Transfers the caller's prepared view family into this frame packet.
        [[nodiscard]] bool AttachViewFamily(PreparedRenderViewFamily& family) noexcept
        {
            if (!family.IsValid() || family.GetFrameSerial() != m_serial || m_viewFamily.IsValid() || (m_viewSetupConfigured && family.GetScene() != m_scene))
                return false;
            m_viewFamily = static_cast<PreparedRenderViewFamily&&>(family);
            return true;
        }

        [[nodiscard]] bool SetPayload(const RenderFramePayload& payload) noexcept
        {
            if (!payload.IsValid())
                return false;
            m_payload = payload;
            return true;
        }

    private:
        friend class RenderCommandSystem;
        friend class ViewportManager;

        u64 m_serial = 0;
        EngineViewportHandle m_engineViewport;
        RenderViewport* m_viewport = nullptr;
        RenderingMode m_mode = RenderingMode::Shaded;
        RenderFramePurpose m_purpose = RenderFramePurpose::Normal;
        RenderFrameFeatures m_features;
        ViewportExtent m_renderExtent;
        ViewportExtent m_outputExtent;
        RenderFramePayload m_payload;
        RenderFrameGraph m_graph;
        PreparedRenderViewFamily m_viewFamily;
        RenderCameraHandle m_rootCameras[MaximumRenderViewsPerFamily]{};
        RenderCameraOutputRegion m_outputRegions[MaximumRenderViewsPerFamily]{};
        u32 m_outputRegionCount = 0;
        RenderSceneHandle m_scene;
        u32 m_rootCameraCount = 0;
        u32 m_jitterIndex = 0;
        bool m_present = false;
        bool m_enableTemporalJitter = true;
        bool m_forceCameraCut = false;
        bool m_viewSetupConfigured = false;
        char m_contextName[MaximumViewportNameBytes]{};
    };

    struct RenderFrameSubmission
    {
        u64 serial = 0;
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return serial != 0;
        }
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
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
    };

    class ViewportManager;
    class FrameRenderer;
    class RenderCommandSystem;
    class RetainedRenderFrameRef;

    class RenderFrameOutputTransaction final
    {
    public:
        RenderFrameOutputTransaction() noexcept = default;
        ~RenderFrameOutputTransaction();
        RenderFrameOutputTransaction(const RenderFrameOutputTransaction&) = delete;
        RenderFrameOutputTransaction& operator=(const RenderFrameOutputTransaction&) = delete;
        RenderFrameOutputTransaction(RenderFrameOutputTransaction&& other) noexcept;
        RenderFrameOutputTransaction& operator=(RenderFrameOutputTransaction&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept { return m_viewport != nullptr && m_acquisition.IsValid(); }
        [[nodiscard]] RenderViewportOutputKind GetKind() const noexcept;
        [[nodiscard]] rhi::TextureRef GetTexture() const noexcept { return IsValid() ? m_acquisition.texture : rhi::TextureRef{}; }
        [[nodiscard]] const rhi::AcquiredBackBuffer& GetBackBuffer() const noexcept { return m_acquisition.backBuffer; }

    private:
        void Reset() noexcept;
        void MarkPresentNodeReached() noexcept { m_presentNodeReached = true; }
        [[nodiscard]] bool WasPresentNodeReached() const noexcept { return m_presentNodeReached; }

        RenderViewport* m_viewport = nullptr;
        RenderOutputAcquisition m_acquisition;
        u32 m_resourceImportIndex = ~u32{0};
        u32 m_resourceImportGeneration = 0;
        bool m_presentNodeReached = false;

        friend class ViewportManager;
        friend class FrameRenderer;
        friend class RenderCommandSystem;
        friend class RetainedRenderFrameRef;
        friend struct RenderNodeImplContext;
    };

    /// Stable rendering-output object owned by ViewportManager. External handles remain generation checked. Mutable live
    /// properties are observed and changed at the main-thread viewport boundary after the preceding command tail is joined.
    class RenderViewport final
    {
    public:
        RenderViewport() noexcept = default;
        RenderViewport(const RenderViewport&) = delete;
        RenderViewport& operator=(const RenderViewport&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] RenderViewportHandle GetHandle() const noexcept
        {
            return m_handle;
        }
        [[nodiscard]] RenderViewportOutputKind GetOutputKind() const noexcept { return m_outputKind; }
        [[nodiscard]] RenderViewportState GetState() const noexcept { return m_state; }
        [[nodiscard]] ViewportExtent GetRenderExtent() const noexcept { return m_renderExtent; }
        [[nodiscard]] ViewportExtent GetOutputExtent() const noexcept { return m_outputExtent; }
        [[nodiscard]] u32 GetRenderWidth() const noexcept { return m_renderExtent.width; }
        [[nodiscard]] u32 GetRenderHeight() const noexcept { return m_renderExtent.height; }
        [[nodiscard]] u32 GetOutputWidth() const noexcept { return m_outputExtent.width; }
        [[nodiscard]] u32 GetOutputHeight() const noexcept { return m_outputExtent.height; }
        [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
        [[nodiscard]] bool IsOccluded() const noexcept { return m_occluded; }
        [[nodiscard]] rhi::SwapChainRef GetSwapChain() const noexcept { return m_swapChain; }
        [[nodiscard]] rhi::TextureRef GetOutputTexture() const noexcept { return m_outputTexture; }
        [[nodiscard]] ViewportExtent GetRequestedOutputExtent() const noexcept { return m_requestedOutputExtent; }
        [[nodiscard]] u64 GetRequiredPixelExtentRevision() const noexcept { return m_requiredPixelExtentRevision; }
        [[nodiscard]] u64 GetAppliedPixelExtentRevision() const noexcept { return m_appliedPixelExtentRevision; }
        [[nodiscard]] u64 GetRequiredSurfaceRevision() const noexcept { return m_requiredSurfaceRevision; }
        [[nodiscard]] u64 GetAppliedSurfaceRevision() const noexcept { return m_appliedSurfaceRevision; }
        [[nodiscard]] u64 GetOutputRevision() const noexcept { return m_outputRevision; }
        [[nodiscard]] u64 GetRenderedFrameCount() const noexcept { return m_renderedFrames; }
        [[nodiscard]] u64 GetPresentedFrameCount() const noexcept { return m_presentedFrames; }
        [[nodiscard]] u32 GetEngineViewportReferenceCount() const noexcept { return m_engineViewportReferences; }
        [[nodiscard]] bool AcquireOutput(RenderOutputAcquisition& acquisition, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonOutput(RenderOutputAcquisition& acquisition, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CompleteOutput(RenderOutputAcquisition& acquisition, ViewportFailure* failure = nullptr) noexcept;
        void DeviceLostOutput(RenderOutputAcquisition& acquisition) noexcept;
        [[nodiscard]] bool RequestRenderExtent(ViewportExtent extent, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BindSwapChain(rhi::SwapChainRef swapChain, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnbindSwapChain(ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdatePresentation(const RenderViewportPresentationUpdate& update, RenderViewportPresentationResult& result,
                                              ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetPresentationAcknowledgement(window::PresentationAcknowledgement& acknowledgement) const noexcept;
        [[nodiscard]] bool Present(RenderOutputAcquisition& acquisition, ViewportFailure* failure = nullptr) noexcept;

    private:
        friend class ViewportManager;
        friend class RenderFrameOutputTransaction;

        void Reset() noexcept;

        ViewportManager* m_manager = nullptr;
        RenderViewportHandle m_handle;
        RenderViewportOutputKind m_outputKind = RenderViewportOutputKind::Headless;
        RenderViewportState m_state = RenderViewportState::Vacant;
        ViewportExtent m_renderExtent;
        ViewportExtent m_outputExtent;
        window::PresentationAttachmentHandle m_presentation;
        rhi::SwapChainRef m_swapChain;
        rhi::TextureRef m_outputTexture;
        ViewportExtent m_requestedOutputExtent;
        u64 m_requiredPixelExtentRevision = 0;
        u64 m_appliedPixelExtentRevision = 0;
        u64 m_requiredSurfaceRevision = 0;
        u64 m_appliedSurfaceRevision = 0;
        u64 m_outputRevision = 0;
        u64 m_renderedFrames = 0;
        u64 m_presentedFrames = 0;
        u32 m_engineViewportReferences = 0;
        bool m_visible = false;
        bool m_occluded = false;
        bool m_suspended = false;
        bool m_active = false;
        char m_name[MaximumViewportNameBytes]{};
    };

    /// Stable frame-construction object owned by ViewportManager. External handles remain generation checked.
    class EngineViewport final
    {
    public:
        EngineViewport() noexcept = default;
        EngineViewport(const EngineViewport&) = delete;
        EngineViewport& operator=(const EngineViewport&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] EngineViewportHandle GetHandle() const noexcept
        {
            return m_handle;
        }
        [[nodiscard]] RenderViewportHandle GetOutput() const noexcept { return m_output; }
        [[nodiscard]] u64 GetBegunFrameCount() const noexcept { return m_begunFrames; }
        [[nodiscard]] u64 GetSubmittedFrameCount() const noexcept { return m_submittedFrames; }
        [[nodiscard]] u64 GetBuildingFrameSerial() const noexcept { return m_buildingFrameSerial; }
        [[nodiscard]] bool PresentsByDefault() const noexcept { return m_presentByDefault; }
        [[nodiscard]] const char* GetContextName() const noexcept { return m_contextName; }
        [[nodiscard]] bool BeginFrame(const RenderFrameSetup& setup, RenderFrameInfo& frame, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ConfigureViews(RenderFrameInfo& frame, const RenderFrameViewSetup& setup, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SubmitFrame(RenderFrameInfo& frame, RenderFrameSubmission& submission, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonFrame(RenderFrameInfo& frame, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool FlushFrame(ViewportFailure* failure = nullptr) noexcept;

    private:
        friend class ViewportManager;

        void Reset() noexcept;

        ViewportManager* m_manager = nullptr;
        EngineViewportHandle m_handle;
        RenderViewportHandle m_output;
        u64 m_begunFrames = 0;
        u64 m_submittedFrames = 0;
        u64 m_buildingFrameSerial = 0;
        bool m_presentByDefault = true;
        bool m_active = false;
        char m_contextName[MaximumViewportNameBytes]{};
    };

    enum class RenderFrameExecutionOutcome : u8
    {
        Success,
        Failure,
        Skipped
    };

    struct RenderFrameExecutionStatus
    {
        RenderFrameExecutionOutcome outcome = RenderFrameExecutionOutcome::Success;
        const char* message = nullptr;

        [[nodiscard]] static constexpr RenderFrameExecutionStatus Success() noexcept
        {
            return {};
        }
        [[nodiscard]] static constexpr RenderFrameExecutionStatus Failure(const char* const message) noexcept
        {
            return {RenderFrameExecutionOutcome::Failure, message};
        }
        [[nodiscard]] static constexpr RenderFrameExecutionStatus Skipped(const char* const message = nullptr) noexcept
        {
            return {RenderFrameExecutionOutcome::Skipped, message};
        }
        [[nodiscard]] constexpr bool IsSuccess() const noexcept
        {
            return outcome == RenderFrameExecutionOutcome::Success;
        }
        [[nodiscard]] constexpr bool IsFailure() const noexcept
        {
            return outcome == RenderFrameExecutionOutcome::Failure;
        }
        [[nodiscard]] constexpr bool IsSkipped() const noexcept
        {
            return outcome == RenderFrameExecutionOutcome::Skipped;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsSuccess();
        }
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

    using VisitRenderViewport = void (*)(const RenderViewport& viewport, void* userData) noexcept;
    using VisitEngineViewport = void (*)(const EngineViewport& viewport, void* userData) noexcept;

    class ViewportManager final
    {
    public:
        struct Impl;

        ViewportManager() noexcept = default;
        ~ViewportManager();

        ViewportManager(const ViewportManager&) = delete;
        ViewportManager& operator=(const ViewportManager&) = delete;

        [[nodiscard]] bool Initialize(RenderCommandSystem& commands, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool CreateRenderViewport(const RenderViewportDesc& desc, RenderViewportHandle& viewport, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyRenderViewport(RenderViewportHandle viewport, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BindSwapChain(RenderViewportHandle viewport, rhi::SwapChainRef swapChain, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnbindSwapChain(RenderViewportHandle viewport, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdatePresentation(RenderViewportHandle viewport, const RenderViewportPresentationUpdate& update,
                                              RenderViewportPresentationResult& result, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetPresentationAcknowledgement(RenderViewportHandle viewport, window::PresentationAcknowledgement& acknowledgement) const noexcept;
        [[nodiscard]] bool RequestRenderExtent(RenderViewportHandle viewport, ViewportExtent extent, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AcquireOutput(RenderViewportHandle viewport, RenderOutputAcquisition& acquisition, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonOutput(RenderOutputAcquisition& acquisition, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CompleteOutput(RenderOutputAcquisition& acquisition, ViewportFailure* failure = nullptr) noexcept;
        void DeviceLostOutput(RenderOutputAcquisition& acquisition) noexcept;
        [[nodiscard]] bool Present(RenderOutputAcquisition& acquisition, ViewportFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool CreateEngineViewport(const EngineViewportDesc& desc, EngineViewportHandle& viewport, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyEngineViewport(EngineViewportHandle viewport, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginFrame(EngineViewportHandle viewport, const RenderFrameSetup& setup, RenderFrameInfo& frame,
                                      ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ConfigureViews(EngineViewportHandle viewport, RenderFrameInfo& frame, const RenderFrameViewSetup& setup,
                                          ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SubmitFrame(EngineViewportHandle viewport, RenderFrameInfo& frame, RenderFrameSubmission& submission,
                                       ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonFrame(EngineViewportHandle viewport, RenderFrameInfo& frame, ViewportFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool FlushFrame(EngineViewportHandle viewport, ViewportFailure* failure = nullptr) noexcept;

        [[nodiscard]] RenderViewport* Resolve(RenderViewportHandle handle) noexcept;
        [[nodiscard]] const RenderViewport* Resolve(RenderViewportHandle handle) const noexcept;
        [[nodiscard]] EngineViewport* Resolve(EngineViewportHandle handle) noexcept;
        [[nodiscard]] const EngineViewport* Resolve(EngineViewportHandle handle) const noexcept;
        void VisitRenderViewports(VisitRenderViewport visitor, void* userData = nullptr) const noexcept;
        void VisitEngineViewports(VisitEngineViewport visitor, void* userData = nullptr) const noexcept;
        [[nodiscard]] ViewportManagerStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

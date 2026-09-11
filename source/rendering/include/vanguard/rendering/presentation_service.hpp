#pragma once

#include <vanguard/rendering/viewport.hpp>
#include <vanguard/window/window_manager.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumPresentationOutputs = window::MaximumPresentationAttachments;

    struct PresentationOutputHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumPresentationOutputs && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const PresentationOutputHandle&, const PresentationOutputHandle&) noexcept = default;
    };

    enum class PresentationOutputState : u8
    {
        Vacant,
        AwaitingSurface,
        Ready,
        Suspended,
        Failed
    };

    enum class PresentationFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDescriptor,
        InvalidHandle,
        CapacityExceeded,
        Busy,
        WindowFailure,
        ViewportFailure,
        RhiFailure,
        UnsupportedSurface,
        OutputsRemainAlive
    };

    struct PresentationFailure
    {
        PresentationFailureCode code = PresentationFailureCode::None;
        PresentationOutputHandle output;
        window::Failure windowFailure;
        ViewportFailure viewportFailure;
        rhi::Failure rhiFailure;
        const char* message = nullptr;
    };

    struct SwapChainPolicy
    {
        enum class ColorPreference : u8
        {
            Sdr,
            PreferHdr10,
            RequireHdr10,
            PreferScRgb,
            RequireScRgb
        };

        rhi::PresentMode presentMode = rhi::PresentMode::Fifo;
        ColorPreference colorPreference = ColorPreference::PreferHdr10;
        rhi::Hdr10Metadata hdr10Metadata;
        u8 bufferCount = 3;
        u8 maximumFramesInFlight = 2;
        u32 frameLatencyWaitTimeoutMilliseconds = 5'000;
        bool allowTearing = true;
        bool enableFrameLatencyPacing = true;
        bool allowSdrFallback = true;
    };

    struct PresentationOutputDesc
    {
        const char* name = nullptr;
        window::WindowHandle window;
        ViewportExtent renderExtent{1280, 720};
        SwapChainPolicy swapChain;
    };

    class PresentationService;

    class PresentationOutput final
    {
    public:
        PresentationOutput() noexcept = default;
        PresentationOutput(const PresentationOutput&) = delete;
        PresentationOutput& operator=(const PresentationOutput&) = delete;

        [[nodiscard]] bool IsValid() const noexcept { return m_active && m_handle.IsValid() && m_renderViewport != nullptr; }
        [[nodiscard]] PresentationOutputHandle GetHandle() const noexcept { return m_handle; }
        [[nodiscard]] PresentationOutputState GetState() const noexcept { return m_state; }
        [[nodiscard]] window::WindowHandle GetWindow() const noexcept { return m_window; }
        [[nodiscard]] window::PresentationAttachmentHandle GetAttachment() const noexcept { return m_attachment; }
        [[nodiscard]] RenderViewport* GetRenderViewport() const noexcept { return m_renderViewport; }
        [[nodiscard]] const SwapChainPolicy& GetSwapChainPolicy() const noexcept { return m_swapChainPolicy; }
        [[nodiscard]] rhi::Format GetActiveFormat() const noexcept { return m_activeFormat; }
        [[nodiscard]] rhi::ColorSpace GetActiveColorSpace() const noexcept { return m_activeColorSpace; }
        [[nodiscard]] const rhi::DisplayColorCapabilities& GetDisplayColor() const noexcept { return m_displayColor; }
        [[nodiscard]] f32 GetSdrWhiteLevel() const noexcept { return m_sdrWhiteLevel; }
        [[nodiscard]] f32 GetHdrHeadroom() const noexcept { return m_hdrHeadroom; }
        [[nodiscard]] u64 GetReconciliationCount() const noexcept { return m_reconciliations; }
        [[nodiscard]] u64 GetSwapChainCreationCount() const noexcept { return m_swapChainCreations; }
        [[nodiscard]] u64 GetResizeApplicationCount() const noexcept { return m_resizeApplications; }
        [[nodiscard]] u64 GetSurfaceReplacementCount() const noexcept { return m_surfaceReplacements; }
        [[nodiscard]] u64 GetColorFallbackCount() const noexcept { return m_colorFallbacks; }
        [[nodiscard]] u64 GetFailureCount() const noexcept { return m_failures; }

    private:
        void Reset() noexcept;

        PresentationOutputHandle m_handle;
        PresentationOutputState m_state = PresentationOutputState::Vacant;
        window::WindowHandle m_window;
        window::PresentationAttachmentHandle m_attachment;
        RenderViewport* m_renderViewport = nullptr;
        SwapChainPolicy m_swapChainPolicy;
        rhi::Format m_activeFormat = rhi::Format::Unknown;
        rhi::ColorSpace m_activeColorSpace = rhi::ColorSpace::Srgb;
        rhi::DisplayColorCapabilities m_displayColor;
        f32 m_sdrWhiteLevel = 1.0f;
        f32 m_hdrHeadroom = 1.0f;
        u64 m_reconciliations = 0;
        u64 m_swapChainCreations = 0;
        u64 m_resizeApplications = 0;
        u64 m_surfaceReplacements = 0;
        u64 m_colorFallbacks = 0;
        u64 m_failures = 0;
        bool m_active = false;

        friend class PresentationService;
    };

    struct PresentationServiceStats
    {
        u32 activeOutputs = 0;
        u32 readyOutputs = 0;
        u32 suspendedOutputs = 0;
        u64 ticks = 0;
        u64 reconciliations = 0;
        u64 swapChainCreations = 0;
        u64 resizeApplications = 0;
        u64 surfaceReplacements = 0;
        u64 colorFallbacks = 0;
        u64 failures = 0;
        u64 rejectedOperations = 0;
    };

    /// Owns the complete native-window presentation chain: window attachment, render viewport, and swap chain.
    /// Engine and editor code retain only generation-checked output and viewport handles.
    class PresentationService final
    {
    public:
        struct Impl;

        PresentationService() noexcept = default;
        ~PresentationService();

        PresentationService(const PresentationService&) = delete;
        PresentationService& operator=(const PresentationService&) = delete;

        [[nodiscard]] bool Initialize(window::WindowManager& windows, ViewportManager& viewports, PresentationFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(PresentationFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool CreateOutput(const PresentationOutputDesc& desc, PresentationOutputHandle& output, PresentationFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyOutput(PresentationOutputHandle output, PresentationFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Tick(PresentationFailure* failure = nullptr) noexcept;

        [[nodiscard]] PresentationOutput* Resolve(PresentationOutputHandle output) noexcept;
        [[nodiscard]] const PresentationOutput* Resolve(PresentationOutputHandle output) const noexcept;
        [[nodiscard]] PresentationServiceStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

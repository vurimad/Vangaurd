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
        [[nodiscard]] friend constexpr bool operator==(const PresentationOutputHandle&,
                                                       const PresentationOutputHandle&) noexcept = default;
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

    struct PresentationOutputSnapshot
    {
        PresentationOutputHandle handle;
        PresentationOutputState state = PresentationOutputState::Vacant;
        window::WindowHandle window;
        window::PresentationAttachmentHandle attachment;
        RenderViewportHandle renderViewport;
        SwapChainPolicy swapChain;
        rhi::Format activeFormat = rhi::Format::Unknown;
        rhi::ColorSpace activeColorSpace = rhi::ColorSpace::Srgb;
        rhi::DisplayColorCapabilities displayColor;
        f32 sdrWhiteLevel = 1.0f;
        f32 hdrHeadroom = 1.0f;
        u64 reconciliations = 0;
        u64 swapChainCreations = 0;
        u64 resizeApplications = 0;
        u64 surfaceReplacements = 0;
        u64 colorFallbacks = 0;
        u64 failures = 0;
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

        [[nodiscard]] bool Initialize(window::WindowManager& windows, ViewportManager& viewports,
                                      PresentationFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(PresentationFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool CreateOutput(const PresentationOutputDesc& desc, PresentationOutputHandle& output,
                                        PresentationFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyOutput(PresentationOutputHandle output,
                                         PresentationFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Tick(PresentationFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool Snapshot(PresentationOutputHandle output,
                                    PresentationOutputSnapshot& snapshot) const noexcept;
        [[nodiscard]] RenderViewportHandle ResolveRenderViewport(PresentationOutputHandle output) const noexcept;
        [[nodiscard]] PresentationServiceStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

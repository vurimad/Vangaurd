#pragma once

#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/rendering/texture_uploader.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 InvalidTextureDemandIndex = 0xffffffffu;

    struct TextureDemandId
    {
        u32 index = InvalidTextureDemandIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidTextureDemandIndex && generation != 0;
        }
    };

    class TextureResidencyRuntime;

    /// One caller's move-only demand for the guaranteed mip tail of an exact loaded texture generation.
    /// Destruction releases only this demand; equal callers remain coalesced on the same GPU residency identity.
    class TextureDemandHandle final
    {
    public:
        TextureDemandHandle() noexcept = default;
        ~TextureDemandHandle();

        TextureDemandHandle(const TextureDemandHandle&) = delete;
        TextureDemandHandle& operator=(const TextureDemandHandle&) = delete;
        TextureDemandHandle(TextureDemandHandle&& other) noexcept;
        TextureDemandHandle& operator=(TextureDemandHandle&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] GpuTextureResidencyHandle GetResidency() const noexcept;
        void Reset() noexcept;

    private:
        TextureDemandHandle(TextureResidencyRuntime& runtime, TextureDemandId demand, GpuTextureResidencyHandle residency) noexcept;

        TextureResidencyRuntime* m_runtime = nullptr;
        TextureDemandId m_demand;
        GpuTextureResidencyHandle m_residency;

        friend class TextureResidencyRuntime;
    };

    enum class TextureRuntimeState : u8
    {
        Invalid,
        MipTailLoading,
        InstallationPending,
        BindlessReady,
        Failed,
        Cancelling,
        Retiring
    };

    struct TextureRuntimeInfo
    {
        GpuTextureResidencyHandle residency;
        resources::ResourcePath resourcePath;
        u32 resourceGeneration = 0;
        u32 demandCount = 0;
        TextureRuntimeState state = TextureRuntimeState::Invalid;
        TextureUploadFailure uploadFailure;
    };

    struct TextureResidencyRuntimeConfig
    {
        TextureResidencyConfig residency;
        TextureUploaderConfig uploader;
        u32 maximumResidencyRecords = 65'536;
        u32 maximumDemands = 131'072;
        u32 maximumInstallationsPerTick = 64;
        /// Reserved share contributed before scene mutations consume the remaining shared batch capacity.
        u32 maximumTableInstallationsPerFrame = 64;
        /// Bounds runtime state/cancellation polling independently from the uploader's internal caps.
        u32 maximumStateChecksPerTick = 256;
    };

    enum class TextureResidencyRuntimeFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDependency,
        InvalidConfiguration,
        InvalidArgument,
        CapacityExceeded,
        StaleDemand,
        LiveResidencyRemains,
        ResidencyFailure,
        UploaderFailure,
        GpuSceneFailure
    };

    struct TextureResidencyRuntimeFailure
    {
        TextureResidencyRuntimeFailureCode code = TextureResidencyRuntimeFailureCode::None;
        const char* message = nullptr;
        TextureResidencyFailure residencyFailure;
        TextureUploadFailure uploadFailure;
        GpuSceneRuntimeFailure gpuSceneFailure;
    };

    struct TextureResidencyRuntimeStats
    {
        TextureResidencyStats residency;
        TextureUploaderStats uploader;
        u32 residencyRecords = 0;
        u32 liveDemands = 0;
        u64 demandsIssued = 0;
        u64 demandsCoalesced = 0;
        u64 demandsReleased = 0;
        u64 failedResidencies = 0;
    };

    /// Renderer-global composition owner for stable texture identities and bounded physical upload work.
    /// Resource loading remains owned by ResourcePipeline; GPU Scene table submission remains owned by
    /// GpuSceneRuntime. Phase 5A stops after a completed candidate enters a pending residency installation.
    class TextureResidencyRuntime final
    {
    public:
        TextureResidencyRuntime() noexcept = default;
        ~TextureResidencyRuntime();

        TextureResidencyRuntime(const TextureResidencyRuntime&) = delete;
        TextureResidencyRuntime& operator=(const TextureResidencyRuntime&) = delete;

        [[nodiscard]] bool Initialize(GpuSceneLifetime& lifetime, rhi::DescriptorDomainRef resourceDescriptors, const TextureResidencyRuntimeConfig& config = {},
                                      TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AbandonDevice(TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Main-thread. Returns the real stable GPU residency identity immediately and coalesces only an
        /// identical resource path and generation. The first demand queues the guaranteed mip-tail upload.
        [[nodiscard]] bool RequestTexture(const resources::ResourceHandle& resource, TextureDemandHandle& demand, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        /// Thread-safe explicit counterpart to TextureDemandHandle::Reset/destruction.
        [[nodiscard]] bool CancelDemand(TextureDemandHandle& demand, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;

        /// Main-thread, non-blocking progress. Empty prerequisites are valid for initial mip-tail uploads;
        /// future copies from an installed texture must receive the renderer's real prior-reader cutover.
        [[nodiscard]] bool Tick(const rhi::ResidencyFenceSet& prerequisiteFences, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Tick(TextureResidencyRuntimeFailure* failure = nullptr) noexcept
        {
            return Tick({}, failure);
        }

        /// Main-thread immediately before renderer FrameTick dispatch. Freezes a bounded pending table batch
        /// and stages it into the one shared GPU Scene transaction.
        [[nodiscard]] bool StageGpuSceneContribution(GpuSceneRuntime& gpuScene, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool SealRetirements(const rhi::ResidencyFenceSet& safeAfter, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] u32 CollectRetirements(TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetInfo(GpuTextureResidencyHandle residency, TextureRuntimeInfo& info, TextureResidencyRuntimeFailure* failure = nullptr) const noexcept;
        [[nodiscard]] TextureResidencyRuntimeStats GetStats() const noexcept;

        /// Phase 5B uses this manager only through the shared GPU Scene contribution coordinator.
        [[nodiscard]] TextureResidencyManager& GetResidencyManager() noexcept;
        [[nodiscard]] const TextureResidencyManager& GetResidencyManager() const noexcept;

    private:
        struct Impl;
        void ReleaseDemand(TextureDemandId demand) noexcept;
        [[nodiscard]] bool IsDemandValid(TextureDemandId demand) const noexcept;

        TextureResidencyManager m_residency;
        TextureUploader m_uploader;
        Impl* m_impl = nullptr;

        friend class TextureDemandHandle;
    };
} // namespace vanguard::rendering

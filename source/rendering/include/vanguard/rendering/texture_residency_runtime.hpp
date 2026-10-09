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
        u32 session = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidTextureDemandIndex && generation != 0 && session != 0;
        }
    };

    class TextureResidencyRuntime;

    /// One caller's move-only residency/quality demand for an exact loaded texture generation. Destruction releases
    /// only this demand; equal callers remain coalesced on the same stable GPU residency identity.
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

    enum class TextureReplacementState : u8
    {
        None,
        Uploading,
        InstallationPending,
        Failed
    };

    enum class TextureQualityState : u8
    {
        Stable,
        Uploading,
        Cancelling,
        InstallationPending,
        Failed
    };

    enum class TextureDemandPriority : u8
    {
        Background,
        Streaming,
        Interactive,
        Focused,
        Count
    };

    struct TextureDemandDesc
    {
        /// 0xffffffff requests the generation's guaranteed mip tail. Explicit mip values are clamped so the
        /// guaranteed tail is always resident.
        u32 firstResidentMip = 0xffffffffu;
        TextureDemandPriority priority = TextureDemandPriority::Streaming;
    };

    struct TextureRuntimeInfo
    {
        GpuTextureResidencyHandle residency;
        resources::ResourcePath resourcePath;
        u32 resourceGeneration = 0;
        u32 demandCount = 0;
        TextureRuntimeState state = TextureRuntimeState::Invalid;
        TextureUploadFailure uploadFailure;
        u32 currentFirstResidentMip = 0xffffffffu;
        u32 desiredFirstResidentMip = 0xffffffffu;
        u32 qualityTargetFirstResidentMip = 0xffffffffu;
        TextureDemandPriority demandPriority = TextureDemandPriority::Streaming;
        TextureQualityState qualityState = TextureQualityState::Stable;
        bool qualitySatisfied = false;
        bool qualityRetryRequired = false;
        TextureUploadFailure qualityFailure;
        u64 committedGpuBytes = 0;
        u32 replacementResourceGeneration = 0;
        TextureReplacementState replacementState = TextureReplacementState::None;
        bool replacementRetryRequired = false;
        TextureUploadFailure replacementFailure;
    };

    enum class TextureResidentLookupResult : u8
    {
        NotFound,
        Found,
        Ambiguous
    };

    struct TextureResidentResource
    {
        GpuTextureResidencyHandle residency;
        u32 resourceGeneration = 0;
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
        /// Bounds new same-generation mip transition starts independently from initial texture delivery.
        u32 maximumMipTransitionsPerTick = 16;
        /// Number of residency-runtime ticks that a lower-quality aggregate target must remain stable before a
        /// copy-only demotion is admitted. Promotions are never delayed by this hysteresis.
        u32 mipDemotionHysteresisTicks = 8;
        /// Soft logical budget for current committed texture installations. Initial availability and generation
        /// replacement remain forward-progress operations; quality promotions are deferred while their projected
        /// committed footprint would exceed this budget.
        u64 maximumCommittedTextureGpuBytes = 4ull * 1024ull * 1024ull * 1024ull;
        /// Aggregate physical GPU bytes retained by failed replacement candidates after ownership leaves
        /// TextureUploader. Retained candidates are retried without a second upload while they fit this budget.
        u64 maximumRetainedReplacementCandidateGpuBytes = 512ull * 1024ull * 1024ull;
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
        ReplacementBusy,
        ReplacementRetryRequired,
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
        u64 qualityTransitionsStarted = 0;
        u64 qualityTransitionFailures = 0;
        u64 qualityTransitionAdmissionDeferrals = 0;
        u64 qualityPromotionBudgetDeferrals = 0;
        u64 committedTextureGpuBytes = 0;
        u64 reservedQualityPromotionGpuBytes = 0;
        u64 peakCommittedTextureGpuBytes = 0;
        u64 retainedReplacementCandidateGpuBytes = 0;
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
        /// identical resource path and generation. The first demand queues the current aggregate quality target
        /// directly; the generation's guaranteed mip tail remains the default when no explicit target is supplied.
        [[nodiscard]] bool RequestTexture(const resources::ResourceHandle& resource, TextureDemandHandle& demand, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        /// Main-thread explicit quality/urgency request. Quality and priority aggregate independently across all
        /// live demands sharing the exact resource generation.
        [[nodiscard]] bool RequestTexture(const resources::ResourceHandle& resource, const TextureDemandDesc& desc, TextureDemandHandle& demand,
                                          TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        /// Main-thread atomic update of one live demand's quality and scheduling urgency.
        [[nodiscard]] bool UpdateDemand(TextureDemandHandle& demand, const TextureDemandDesc& desc, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        /// Main-thread convenience update preserving the demand's existing scheduling urgency.
        [[nodiscard]] bool SetDemandFirstResidentMip(TextureDemandHandle& demand, u32 firstResidentMip, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        /// Replaces the source generation behind an existing stable GPU texture
        /// identity. The old CPU/GPU generation remains authoritative until the
        /// renderer accepts the candidate's shared GPU Scene table update.
        [[nodiscard]] bool ReplaceTexture(const resources::ResourceHandle& resource, GpuTextureResidencyHandle residency,
                                          TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        /// Explicitly retries a failed same-generation replacement without
        /// reloading the ResourceHandle. If upload already completed, the retained
        /// candidate is re-admitted directly instead of re-uploading.
        [[nodiscard]] bool RetryTextureReplacement(GpuTextureResidencyHandle residency,
                                                   TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
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
        /// and stages it into the one shared GPU Scene transaction. The staged output is true only when this call
        /// transferred a contribution into that transaction.
        [[nodiscard]] bool StageGpuSceneContribution(GpuSceneRuntime& gpuScene, bool& staged, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool StageGpuSceneContribution(GpuSceneRuntime& gpuScene, TextureResidencyRuntimeFailure* failure = nullptr) noexcept
        {
            bool staged = false;
            return StageGpuSceneContribution(gpuScene, staged, failure);
        }

        [[nodiscard]] bool SealRetirements(const rhi::ResidencyFenceSet& safeAfter, TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] u32 CollectRetirements(TextureResidencyRuntimeFailure* failure = nullptr) noexcept;
        /// Read-only lookup for an actively demanded bindless-ready generation of one texture path. Multiple live
        /// generations are reported as ambiguous rather than selecting an arbitrary stable GPU identity.
        [[nodiscard]] TextureResidentLookupResult FindResidentResource(resources::ResourcePath path, TextureResidentResource& output) const noexcept;
        /// Read-only lookup for any actively demanded runtime record of one texture path, including loading and
        /// replacement states. Multiple live records are ambiguous. This is the admission-safe counterpart to
        /// FindResidentResource for callers that must not create a parallel same-path generation while one is loading.
        [[nodiscard]] TextureResidentLookupResult FindDemandedResource(resources::ResourcePath path, TextureRuntimeInfo& output) const noexcept;
        /// Read-only physical observation for a residency identity owned by this runtime. Runtime ownership is
        /// validated before and after the lower residency-manager query so callers do not bypass QoS ownership.
        [[nodiscard]] bool GetResidencyInfo(GpuTextureResidencyHandle residency, TextureResidencyInfo& info,
                                            TextureResidencyRuntimeFailure* failure = nullptr) const noexcept;
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
        u32 m_session = 0;

        friend class TextureDemandHandle;
    };
} // namespace vanguard::rendering

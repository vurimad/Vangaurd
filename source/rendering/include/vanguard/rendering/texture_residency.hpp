#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/rendering/gpu_scene_upload.hpp>
#include <vanguard/resources/resources.hpp>

namespace vanguard::rendering
{
    enum class TextureResidencyState : u8
    {
        Invalid,
        Allocated,
        BindlessReady,
        Retiring
    };

    struct TextureResidencyConfig
    {
        u32 maximumTextures = 65'536;
        u32 maximumPendingInstallations = 8'192;
        /// Replacements temporarily retain the previous immutable descriptor until a renderer fence cutover.
        u32 maximumPendingRetirements = 8'192;
    };

    struct TextureTransitionToken
    {
        GpuTextureResidencyHandle texture;
        u32 serial = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return texture.IsValid() && serial != 0;
        }
    };

    struct TextureInstallationDesc
    {
        rhi::TextureRef texture;
        u32 firstResidentMip = 0;
        u32 residentMipCount = 0;
        resources::WeakResourceHandle source;
        crypto::Digest256 contentFingerprint;
        /// Present for manager-owned streaming transitions. The manager compares the retained source,
        /// content, target mip, and installation revision before admitting the candidate.
        TextureTransitionToken transition;
    };

    struct TextureInstallationTicket
    {
        GpuTextureResidencyHandle texture;
        u32 revision = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return texture.IsValid() && revision != 0;
        }
    };

    /// Identifies one frozen contribution to a renderer-owned GPU Scene upload batch.
    struct TextureResidencyBatch
    {
        u32 serial = 0;
        u32 installationCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return serial != 0;
        }
    };

    struct TextureResidencyInfo
    {
        GpuTextureResidencyHandle handle;
        TextureResidencyState state = TextureResidencyState::Invalid;
        rhi::TextureRef texture;
        rhi::DescriptorHandle descriptor;
        u32 firstResidentMip = 0;
        u32 residentMipCount = 0;
        u32 installationRevision = 0;
        resources::WeakResourceHandle source;
        crypto::Digest256 contentFingerprint;
    };

    /// Read-only view of the manager-owned source retained for one active compact-texture transition.
    struct TextureTransitionInfo
    {
        TextureTransitionToken token;
        rhi::TextureRef currentTexture;
        resources::WeakResourceHandle source;
        crypto::Digest256 contentFingerprint;
        u32 expectedInstallationRevision = 0;
        u32 currentFirstResidentMip = 0;
        u32 currentResidentMipCount = 0;
        u32 targetFirstResidentMip = 0;
    };

    enum class TextureResidencyFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDependency,
        InvalidConfiguration,
        InvalidArgument,
        StaleHandle,
        Busy,
        InvalidBatch,
        ReservationMismatch,
        CapacityExceeded,
        PendingRetirementCapacityExceeded,
        MissingRetirementFence,
        LiveResidenciesRemain,
        LifetimeFailure,
        DescriptorFailure
    };

    struct TextureResidencyFailure
    {
        TextureResidencyFailureCode code = TextureResidencyFailureCode::None;
        const char* message = nullptr;
        GpuSceneLifetimeFailure lifetimeFailure;
        rhi::Failure rhiFailure;
    };

    struct TextureResidencyStats
    {
        u32 liveResidencies = 0;
        u32 bindlessReady = 0;
        u32 pendingInstallations = 0;
        u32 frozenInstallations = 0;
        u32 pendingDescriptorRetirements = 0;
        u64 allocations = 0;
        u64 installations = 0;
        u64 replacements = 0;
        u64 installationBatches = 0;
        u64 retirements = 0;
        u64 reclaimed = 0;
        u64 rejectedOperations = 0;
    };

    /// Owns stable GPU texture identities and immutable descriptor-backed physical installations.
    /// Candidate texture creation/upload is deliberately outside this class. Install accepts a complete texture
    /// and stages its table record; a renderer-owned GPU Scene batch later commits it atomically.
    class TextureResidencyManager final
    {
    public:
        struct Impl;

        TextureResidencyManager() noexcept = default;
        ~TextureResidencyManager();

        TextureResidencyManager(const TextureResidencyManager&) = delete;
        TextureResidencyManager& operator=(const TextureResidencyManager&) = delete;

        [[nodiscard]] bool Initialize(GpuSceneLifetime& lifetime, rhi::DescriptorDomainRef resourceDescriptors, const TextureResidencyConfig& config = {}, TextureResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(TextureResidencyFailure* failure = nullptr) noexcept;
        void AbandonDevice() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Allocates a stable identity. It remains shader-inaccessible until its first installation batch commits.
        [[nodiscard]] bool Allocate(GpuTextureResidencyHandle& handle, TextureResidencyFailure* failure = nullptr) noexcept;
        /// Admits a complete physical texture through a new write-once descriptor without recording or submitting
        /// GPU work. The previous installation remains authoritative until AcceptSubmittedBatch runs.
        [[nodiscard]] bool Install(GpuTextureResidencyHandle handle, const TextureInstallationDesc& installation, TextureInstallationTicket& ticket, TextureResidencyFailure* failure = nullptr) noexcept;

        /// Starts the only active physical transition for a stable texture identity and retains its current
        /// texture as the shared-mip copy source. Initial installations have no current texture.
        [[nodiscard]] bool BeginTransition(GpuTextureResidencyHandle handle, const resources::WeakResourceHandle& source, const crypto::Digest256& contentFingerprint, u32 targetFirstResidentMip,
                                           TextureTransitionToken& token, TextureResidencyFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetTransitionInfo(TextureTransitionToken token, TextureTransitionInfo& info, TextureResidencyFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool EndTransition(TextureTransitionToken token, TextureResidencyFailure* failure = nullptr) noexcept;

        /// Freezes at most maximumInstallations into one contribution, leaving excess work pending. An empty
        /// contribution is valid, but maximumInstallations must be non-zero.
        [[nodiscard]] bool PrepareBatch(u32 maximumInstallations, TextureResidencyBatch& batch, TextureResidencyFailure* failure = nullptr) noexcept;
        /// Fills exactly batch.installationCount requests. The caller may place this span inside a larger shared batch.
        [[nodiscard]] bool BuildUploadRequests(const TextureResidencyBatch& batch, containers::ArraySpan<GpuSceneUploadRequest> requests, TextureResidencyFailure* failure = nullptr) const noexcept;
        /// Writes final table values into the exact reservations assigned to this contribution. It never completes
        /// reservations or submits the uploader batch.
        [[nodiscard]] bool WriteBatch(const TextureResidencyBatch& batch, containers::ArraySpan<const GpuSceneUploadReservation> reservations, TextureResidencyFailure* failure = nullptr) noexcept;
        /// Makes the already validated frozen candidates authoritative after renderer-owned submission. This is
        /// deliberately infallible: all caller-correctable validation must happen before GPU Scene submission.
        void AcceptSubmittedBatch(const TextureResidencyBatch& batch, rhi::GpuFence sharedCompletion) noexcept;
        /// Returns a frozen contribution to the pending queue after a transient shared-uploader failure.
        [[nodiscard]] bool RetryBatch(const TextureResidencyBatch& batch, TextureResidencyFailure* failure = nullptr) noexcept;
        /// Permanently releases every frozen candidate while preserving prior usable installations.
        [[nodiscard]] bool DiscardBatch(const TextureResidencyBatch& batch, TextureResidencyFailure* failure = nullptr) noexcept;
        /// Stops admitting new installations and retires the GPU Scene identity. Descriptor/resource release
        /// still waits for SealRetirements and the shared GPU Scene lifetime collector.
        [[nodiscard]] bool Retire(GpuTextureResidencyHandle handle, TextureResidencyFailure* failure = nullptr) noexcept;
        /// Supplies the renderer-owned cutover fences for every immutable descriptor replaced or retired so far.
        [[nodiscard]] bool SealRetirements(const rhi::ResidencyFenceSet& safeAfter, TextureResidencyFailure* failure = nullptr) noexcept;
        /// Recycles records after the owner has also collected the shared GpuSceneLifetime allocation.
        [[nodiscard]] u32 CollectRetirements(TextureResidencyFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool GetInfo(GpuTextureResidencyHandle handle, TextureResidencyInfo& info, TextureResidencyFailure* failure = nullptr) const noexcept;
        [[nodiscard]] TextureResidencyStats GetStats() const noexcept;

    private:
        GpuSceneLifetime* m_lifetime = nullptr;
        rhi::DescriptorDomain m_resourceDescriptors;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

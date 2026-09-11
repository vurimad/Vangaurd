#pragma once

#include <vanguard/io/io.hpp>
#include <vanguard/rendering/gpu_scene_types.hpp>
#include <vanguard/rendering/texture_residency.hpp>
#include <vanguard/rendering/texture_upload_candidate.hpp>
#include <vanguard/resources/resources.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 InvalidTextureUploadRequestIndex = 0xffffffffu;

    struct TextureUploadRequestId
    {
        u32 index = InvalidTextureUploadRequestIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidTextureUploadRequestIndex && generation != 0;
        }

        [[nodiscard]] constexpr bool operator==(const TextureUploadRequestId&) const noexcept = default;
    };

    enum class TextureUploadState : u8
    {
        Invalid,
        Acquiring,
        Submitted,
        ReadyToInstall,
        Failed
    };

    struct TextureUploaderConfig
    {
        u32 maximumRequests = 4096;
        /// Maximum number of new asynchronous source windows issued by one Tick. This is independent from
        /// the number of ready candidates admitted into the GPU copy batch.
        u32 maximumAcquisitionStartsPerTick = 64;
        u32 maximumCandidatesPerBatch = 64;
        /// Bounds non-blocking fence checks and completed physical textures retained while residency
        /// installation is backpressured.
        u32 maximumCompletionPollsPerTick = 64;
        u32 maximumReadyCandidates = 256;
        u32 maximumWritesPerBatch = 256;
        u32 maximumCopiesPerBatch = 256;
        u32 maximumAcquisitionWindowSubresources = 8;
        u64 maximumBytesPerBatch = 64ull * 1024ull * 1024ull;
        u64 maximumCopyBytesPerBatch = 64ull * 1024ull * 1024ull;
        u64 maximumCandidateGpuBytesPerBatch = 128ull * 1024ull * 1024ull;
        /// Maximum exact physical allocation bytes retained by live upload candidates. This is separate from
        /// maximumPendingCandidateBytes, which reserves cooker-authored source bytes before the RHI allocation
        /// requirements are known.
        u64 maximumPendingCandidateGpuBytes = 512ull * 1024ull * 1024ull;
        u64 maximumPendingCandidateBytes = 512ull * 1024ull * 1024ull;
        u64 maximumAcquisitionWindowBytes = 64ull * 1024ull * 1024ull;
    };

    enum class TextureUploadFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidArgument,
        CapacityExceeded,
        CommandListBusy,
        AcquisitionFailure,
        CandidateFailure,
        RhiFailure,
        SubmissionFailure,
        ResidencyFailure,
        LiveRequestsRemain
    };

    struct TextureUploadFailure
    {
        TextureUploadFailureCode code = TextureUploadFailureCode::None;
        TextureUploadRequestId request;
        const char* message = nullptr;
        textures::Result acquisitionFailure = textures::Result::Success;
        TextureUploadCandidateFailure candidateFailure;
        TextureResidencyFailure residencyFailure;
        rhi::Failure rhiFailure;
    };

    /// A complete compact texture whose final upload batch has been submitted. The persistent initial state
    /// returns every touched subresource to shader-read when a command list closes. The shared fence identifies
    /// the CopySync batch containing its last upload and must complete before residency installation.
    struct SubmittedTextureCandidate
    {
        SubmittedTextureCandidate() noexcept = default;
        SubmittedTextureCandidate(const SubmittedTextureCandidate&) = delete;
        SubmittedTextureCandidate& operator=(const SubmittedTextureCandidate&) = delete;
        SubmittedTextureCandidate(SubmittedTextureCandidate&&) noexcept = default;
        SubmittedTextureCandidate& operator=(SubmittedTextureCandidate&&) noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept;
        void Reset() noexcept;

        resources::WeakResourceHandle resource;
        crypto::Digest256 contentFingerprint;
        GpuTextureResidencyHandle residency;
        rhi::Texture texture;
        rhi::GpuFence copyCompletion;
        u32 firstResidentMip = 0;
        u32 residentMipCount = 0;
        u32 subresourceCount = 0;
        u64 sourceBytesUploaded = 0;
        u64 gpuBytesCopied = 0;
        u64 candidateGpuBytes = 0;
    };

    struct TextureUploaderStats
    {
        u64 requestsIssued = 0;
        u64 requestsCoalesced = 0;
        u64 requestsCancelled = 0;
        u64 requestsFailed = 0;
        u64 acquisitionWindowsStarted = 0;
        u64 batchesSubmitted = 0;
        u64 candidatesSubmitted = 0;
        u64 writesSubmitted = 0;
        u64 copiesSubmitted = 0;
        u64 bytesSubmitted = 0;
        u64 copyBytesSubmitted = 0;
        u64 candidateGpuBytesSubmitted = 0;
        u32 liveRequests = 0;
        u32 acquiringRequests = 0;
        u32 submittedRequests = 0;
        u32 readyToInstallRequests = 0;
        u32 failedRequests = 0;
        /// Exact RHI allocation requirements retained by Acquiring, Submitted, and ReadyToInstall candidates.
        u64 pendingCandidateGpuBytes = 0;
        /// Cooker-authored source bytes reserved by live requests.
        u64 pendingCandidateBytes = 0;
    };

    /// Renderer-serialized Phase 3B bridge from verified texture windows to compact physical textures. Tick
    /// never waits for I/O or GPU completion and records at most one bounded CopySync batch.
    class TextureUploader final
    {
    public:
        struct Impl;

        TextureUploader() noexcept = default;
        ~TextureUploader();

        TextureUploader(const TextureUploader&) = delete;
        TextureUploader& operator=(const TextureUploader&) = delete;

        [[nodiscard]] bool Initialize(const TextureUploaderConfig& config = {}, TextureUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(TextureUploadFailure* failure = nullptr) noexcept;
        void AbandonDevice() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Low-level candidate-only path used by tooling/tests that take the submitted texture directly. Normal
        /// residency streaming must use the manager-owned overload below.
        [[nodiscard]] bool RequestMipTail(const resources::ResourceHandle& resource, GpuTextureResidencyHandle residency, TextureUploadRequestId& request,
                                          io::AsyncPriority priority = io::eAsyncPriority_Streaming, TextureUploadFailure* failure = nullptr) noexcept;
        /// Requests the guaranteed tail as the initial manager-owned residency transition.
        [[nodiscard]] bool RequestMipTail(const resources::ResourceHandle& resource, TextureResidencyManager& residencyManager, GpuTextureResidencyHandle residency, TextureUploadRequestId& request,
                                          io::AsyncPriority priority = io::eAsyncPriority_Streaming, TextureUploadFailure* failure = nullptr) noexcept;
        /// Requests one explicit compact suffix. The target is clamped so the guaranteed mip tail is never
        /// omitted. A target equal to the current first mip succeeds as a no-op and returns an invalid request.
        [[nodiscard]] bool RequestMipTransition(const resources::ResourceHandle& resource, TextureResidencyManager& residencyManager, GpuTextureResidencyHandle residency, u32 targetFirstResidentMip,
                                                TextureUploadRequestId& request, io::AsyncPriority priority = io::eAsyncPriority_Streaming, TextureUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Cancel(TextureUploadRequestId request, TextureUploadFailure* failure = nullptr) noexcept;

        /// Advances acquisitions, stages ready windows, releases their CPU bytes immediately, and submits one
        /// shared CopySync batch. It also polls a bounded number of prior batch fences without waiting.
        /// prerequisiteFences identify prior readers of current textures. Copies are deferred until every
        /// supplied fence is complete; Tick only polls and never waits.
        [[nodiscard]] bool Tick(const rhi::ResidencyFenceSet& prerequisiteFences, TextureUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Tick(TextureUploadFailure* failure = nullptr) noexcept
        {
            return Tick({}, failure);
        }

        /// Admits up to maximumInstallations completed physical textures into the existing residency manager.
        /// This only creates pending descriptor/table installations; the renderer still contributes and submits
        /// them through its shared GpuSceneUploader batch.
        [[nodiscard]] bool InstallReadyCandidates(TextureResidencyManager& residencyManager, u32 maximumInstallations, u32& installed, TextureUploadFailure* failure = nullptr) noexcept;

        [[nodiscard]] TextureUploadState GetState(TextureUploadRequestId request) const noexcept;
        [[nodiscard]] bool GetRequestFailure(TextureUploadRequestId request, TextureUploadFailure& failure) const noexcept;
        /// Low-level ownership escape hatch. The caller must honor copyCompletion if the request is still
        /// Submitted; normal streaming should use InstallReadyCandidates.
        [[nodiscard]] bool TakeSubmitted(TextureUploadRequestId request, SubmittedTextureCandidate& candidate, TextureUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] TextureUploaderStats GetStats() const noexcept;

    private:
        [[nodiscard]] bool RequestInternal(const resources::ResourceHandle& resource, TextureResidencyManager* residencyManager, GpuTextureResidencyHandle residency, u32 targetFirstResidentMip,
                                           TextureUploadRequestId& request, io::AsyncPriority priority, TextureUploadFailure* failure) noexcept;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

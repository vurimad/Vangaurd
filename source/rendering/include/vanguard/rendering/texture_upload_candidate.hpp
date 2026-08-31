#pragma once

#include <vanguard/rhi/rhi.hpp>
#include <vanguard/textures/texture_resource.hpp>

namespace vanguard::rendering
{
    enum class TextureUploadCandidateFailureCode : u8
    {
        None,
        InvalidArgument,
        InvalidState,
        UnsupportedFormat,
        DirectUploadRequired,
        CapacityExceeded,
        ArithmeticOverflow,
        InvalidSubresource,
        DuplicateSubresource
    };

    struct TextureUploadCandidateFailure
    {
        TextureUploadCandidateFailureCode code = TextureUploadCandidateFailureCode::None;
        const char* message = nullptr;
        u32 sourceSubresource = textures::InvalidSubresourceIndex;
        u32 assetMip = 0xffffffffu;
        u32 arrayLayer = 0xffffffffu;
        u32 face = 0xffffffffu;
    };

    enum class TextureMipTransitionKind : u8
    {
        Invalid,
        NoOp,
        Promotion,
        Demotion
    };

    struct TextureMipSharedCopy
    {
        u32 assetMip = 0xffffffffu;
        u32 sourcePhysicalMip = 0xffffffffu;
        u32 destinationPhysicalMip = 0xffffffffu;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return assetMip != 0xffffffffu && sourcePhysicalMip != 0xffffffffu && destinationPhysicalMip != 0xffffffffu;
        }
    };

    /// Pure compact-suffix transition plan. It selects no policy and owns no resources; it only converts a
    /// caller-provided target into exact source-upload and shared-GPU-copy mip intervals.
    class TextureMipTransitionPlan final
    {
    public:
        [[nodiscard]] bool Build(u32 totalMipCount, u32 guaranteedTailFirstMip, u32 currentFirstResidentMip,
                                 u32 requestedFirstResidentMip, TextureUploadCandidateFailure* failure = nullptr) noexcept;
        void Reset() noexcept;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_kind != TextureMipTransitionKind::Invalid; }
        [[nodiscard]] constexpr TextureMipTransitionKind GetKind() const noexcept { return m_kind; }
        [[nodiscard]] constexpr u32 GetTotalMipCount() const noexcept { return m_totalMipCount; }
        [[nodiscard]] constexpr u32 GetGuaranteedTailFirstMip() const noexcept { return m_guaranteedTailFirstMip; }
        [[nodiscard]] constexpr u32 GetCurrentFirstMip() const noexcept { return m_currentFirstMip; }
        [[nodiscard]] constexpr u32 GetRequestedFirstMip() const noexcept { return m_requestedFirstMip; }
        [[nodiscard]] constexpr u32 GetTargetFirstMip() const noexcept { return m_targetFirstMip; }
        [[nodiscard]] constexpr u32 GetCandidateMipCount() const noexcept { return IsValid() ? m_totalMipCount - m_targetFirstMip : 0; }
        [[nodiscard]] constexpr u32 GetUploadFirstMip() const noexcept { return m_uploadFirstMip; }
        [[nodiscard]] constexpr u32 GetUploadMipCount() const noexcept { return m_uploadMipCount; }
        [[nodiscard]] constexpr u32 GetSharedFirstMip() const noexcept { return m_sharedFirstMip; }
        [[nodiscard]] constexpr u32 GetSharedMipCount() const noexcept { return m_sharedMipCount; }
        [[nodiscard]] bool GetSharedMipCopy(u32 sharedMipIndex, TextureMipSharedCopy& copy) const noexcept;

    private:
        TextureMipTransitionKind m_kind = TextureMipTransitionKind::Invalid;
        u32 m_totalMipCount = 0;
        u32 m_guaranteedTailFirstMip = 0;
        u32 m_currentFirstMip = 0;
        u32 m_requestedFirstMip = 0;
        u32 m_targetFirstMip = 0;
        u32 m_uploadFirstMip = 0;
        u32 m_uploadMipCount = 0;
        u32 m_sharedFirstMip = 0;
        u32 m_sharedMipCount = 0;
    };

    /// Validated non-owning upload view for one compact physical candidate subresource. The source span remains
    /// owned by TextureMipAcquisition and is valid only until its current window is released.
    struct TextureUploadCandidateSubresource
    {
        rhi::TextureSubresourceData upload;
        u32 sourceSubresource = textures::InvalidSubresourceIndex;
        u32 coverageIndex = 0xffffffffu;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return upload.data != nullptr && upload.size != 0 && sourceSubresource != textures::InvalidSubresourceIndex && coverageIndex != 0xffffffffu;
        }
    };

    struct TextureCandidateCopySubresource
    {
        rhi::TextureCopyRegion copy;
        u32 assetMip = 0xffffffffu;
        u32 coverageIndex = 0xffffffffu;
        u64 byteSize = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return assetMip != 0xffffffffu && coverageIndex != 0xffffffffu && byteSize != 0;
        }
    };

    enum class TextureCandidateSubresourceCoverage : u8
    {
        Missing,
        UploadedFromSource,
        CopiedFromCurrent
    };

    /// Pure Phase 3A plan for one compact physical texture. It performs no RHI allocation or submission. The
    /// TextureFile used to initialize it must outlive the plan; Phase 3B will enforce that through its retained
    /// TextureResource handle.
    class TextureUploadCandidatePlan final
    {
    public:
        struct Impl;

        TextureUploadCandidatePlan() noexcept = default;
        ~TextureUploadCandidatePlan();

        TextureUploadCandidatePlan(const TextureUploadCandidatePlan&) = delete;
        TextureUploadCandidatePlan& operator=(const TextureUploadCandidatePlan&) = delete;
        TextureUploadCandidatePlan(TextureUploadCandidatePlan&& other) noexcept;
        TextureUploadCandidatePlan& operator=(TextureUploadCandidatePlan&& other) noexcept;

        [[nodiscard]] bool Initialize(const textures::TextureFile& texture, u32 firstAssetMip, u32 residentMipCount,
                                      const rhi::Capabilities& capabilities, TextureUploadCandidateFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool InitializeMipTail(const textures::TextureFile& texture, const rhi::Capabilities& capabilities,
                                             TextureUploadCandidateFailure* failure = nullptr) noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const rhi::TextureDesc& GetTextureDesc() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetContentFingerprint() const noexcept;
        [[nodiscard]] u32 GetFirstAssetMip() const noexcept;
        [[nodiscard]] u32 GetResidentMipCount() const noexcept;
        [[nodiscard]] u32 GetExpectedSubresourceCount() const noexcept;
        [[nodiscard]] u32 GetUploadedSubresourceCount() const noexcept;
        [[nodiscard]] u32 GetCopiedSubresourceCount() const noexcept;
        [[nodiscard]] u32 GetInitializedSubresourceCount() const noexcept;
        [[nodiscard]] TextureCandidateSubresourceCoverage GetCoverage(u32 coverageIndex) const noexcept;
        [[nodiscard]] u64 GetExpectedByteCount() const noexcept;

        /// Validates and maps one verified Phase 2 view. This does not change coverage; call MarkUploaded only
        /// after the corresponding RHI WriteTexture operation succeeds.
        [[nodiscard]] bool MapSubresource(const textures::TextureSubresourceView& source, TextureUploadCandidateSubresource& mapped,
                                          TextureUploadCandidateFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool MarkUploaded(const TextureUploadCandidateSubresource& mapped,
                                        TextureUploadCandidateFailure* failure = nullptr) noexcept;
        /// Maps one shared asset mip to the old and candidate physical subresources. Call MarkCopied only after
        /// the corresponding RHI CopyTexture operation succeeds.
        [[nodiscard]] bool MapSharedCopy(const TextureMipTransitionPlan& transition, u32 sharedMipIndex, u32 arraySlice,
                                         TextureCandidateCopySubresource& mapped,
                                         TextureUploadCandidateFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool MarkCopied(const TextureCandidateCopySubresource& mapped,
                                      TextureUploadCandidateFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsComplete() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering

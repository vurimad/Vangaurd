#pragma once

#include <vanguard/streaming/resource_source.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/textures/textures.hpp>

namespace vanguard::textures
{
    struct TextureSubresourceReadStats
    {
        u64 storedBytesRead = 0;
        u64 decodedBytesProduced = 0;
        u32 decodedSegments = 0;
    };

    class TextureSubresourceReadRequest final
    {
    public:
        struct Impl;
        TextureSubresourceReadRequest() noexcept = default;
        ~TextureSubresourceReadRequest();
        TextureSubresourceReadRequest(const TextureSubresourceReadRequest&) = delete;
        TextureSubresourceReadRequest& operator=(const TextureSubresourceReadRequest&) = delete;
        TextureSubresourceReadRequest(TextureSubresourceReadRequest&& other) noexcept;
        TextureSubresourceReadRequest& operator=(TextureSubresourceReadRequest&& other) noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        void Wait() const noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds) const noexcept;
        [[nodiscard]] bool Cancel() noexcept;
        [[nodiscard]] Result GetResult() const noexcept;
        [[nodiscard]] TextureSubresourceReadStats GetStats() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> GetBytes() const noexcept;
        void Reset() noexcept;

    private:
        explicit TextureSubresourceReadRequest(Impl* impl) noexcept;
        Impl* m_impl = nullptr;
        friend class TextureSubresourceSource;
    };

    /// VTEX addressing and validation over the generic pinned loose/package source.
    class TextureSubresourceSource final
    {
    public:
        struct Impl;
        TextureSubresourceSource() noexcept = default;
        ~TextureSubresourceSource();
        TextureSubresourceSource(const TextureSubresourceSource&) = delete;
        TextureSubresourceSource& operator=(const TextureSubresourceSource&) = delete;
        TextureSubresourceSource(TextureSubresourceSource&& other) noexcept;
        TextureSubresourceSource& operator=(TextureSubresourceSource&& other) noexcept;
        [[nodiscard]] Result OpenLoose(const filesystem::AbsolutePath& physicalPath) noexcept;
        [[nodiscard]] Result OpenPackage(const filesystem::AbsolutePath& physicalPath, resources::ResourceId resource) noexcept;
        [[nodiscard]] Result Open(streaming::ResourceSource&& source) noexcept;
        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] u64 GetLogicalSize() const noexcept;
        /// Shared ResourceStreamer staging budget, or zero for an ungoverned tooling source.
        [[nodiscard]] u64 GetStagingBudgetBytes() const noexcept;
        [[nodiscard]] Result PlanSubresource(const TextureFile& texture, u32 subresource, streaming::ResourceReadPlan& plan, u64& admissionBytes) const noexcept;
        [[nodiscard]] Result ReadSubresourceAsync(const TextureFile& texture, u32 subresource, TextureSubresourceReadRequest& request,
                                                  io::AsyncPriority priority = io::eAsyncPriority_Streaming) const noexcept;

    private:
        Impl* m_impl = nullptr;
    };

    /// Metadata and pinned physical generation for one loaded VTEX resource.
    /// GPU images, descriptors, and residency state deliberately live elsewhere.
    class TextureResourceObject final : public resources::ResourceObject
    {
    public:
        TextureResourceObject() noexcept = default;
        ~TextureResourceObject() override;
        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override;
        [[nodiscard]] Result OpenPrepared(TextureSubresourceSource&& source, TextureFile&& metadata) noexcept;
        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] const TextureFile& GetMetadata() const noexcept;
        [[nodiscard]] const TextureSubresourceSource& GetSubresourceSource() const noexcept;

    private:
        TextureFile m_metadata;
        TextureSubresourceSource m_source;
        bool m_open = false;
    };

    class TextureResourceLoader final
    {
    public:
        struct Impl;
        TextureResourceLoader() noexcept = default;
        ~TextureResourceLoader();
        TextureResourceLoader(const TextureResourceLoader&) = delete;
        TextureResourceLoader& operator=(const TextureResourceLoader&) = delete;
        [[nodiscard]] bool Initialize(streaming::ResourceStreamer& streamer, resources::ResourcePipeline& pipeline, const ReadLimits& limits = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };

    struct TextureMipReadWindowLimits
    {
        /// Per-window ceiling. Governed runtime sources additionally clamp this
        /// to their actual shared ResourceStreamer staging budget.
        u64 maximumAdmissionBytes = 64ull * 1024ull * 1024ull;
        /// The safe default cannot retain one admitted read while waiting for
        /// another behind the same global staging budget.
        u32 maximumSubresources = 1;
    };

    struct TextureSubresourceView
    {
        u32 subresource = InvalidSubresourceIndex;
        const SubresourceRecord* record = nullptr;
        containers::ArraySpan<const u8> bytes;
    };

    enum class TextureMipAcquisitionState : u8
    {
        Invalid,
        Idle,
        Reading,
        Ready,
        Complete,
        Failed,
        Cancelled
    };

    /// Acquires one mip interval through bounded, explicitly released windows.
    /// It retains the resource handle and allows at most one live window.
    class TextureMipAcquisition final
    {
    public:
        struct Impl;
        TextureMipAcquisition() noexcept = default;
        ~TextureMipAcquisition();
        TextureMipAcquisition(const TextureMipAcquisition&) = delete;
        TextureMipAcquisition& operator=(const TextureMipAcquisition&) = delete;
        TextureMipAcquisition(TextureMipAcquisition&& other) noexcept;
        TextureMipAcquisition& operator=(TextureMipAcquisition&& other) noexcept;
        [[nodiscard]] Result Open(resources::ResourceHandle texture, u8 firstMip, u8 mipCount, const TextureMipReadWindowLimits& limits = {}) noexcept;
        [[nodiscard]] Result OpenMipTail(resources::ResourceHandle texture, const TextureMipReadWindowLimits& limits = {}) noexcept;
        [[nodiscard]] Result BeginNextWindow(io::AsyncPriority priority = io::eAsyncPriority_Streaming) noexcept;
        [[nodiscard]] TextureMipAcquisitionState Poll() noexcept;
        void Wait() noexcept;
        [[nodiscard]] TextureMipAcquisitionState GetState() const noexcept;
        [[nodiscard]] Result GetResult() const noexcept;
        [[nodiscard]] containers::ArraySpan<const TextureSubresourceView> GetWindow() const noexcept;
        [[nodiscard]] Result ReleaseWindow() noexcept;
        [[nodiscard]] bool Cancel() noexcept;
        void Reset() noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::textures

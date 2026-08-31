#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/packages/packages.hpp>

namespace vanguard::streaming
{
    namespace detail
    {
        struct ResourceSourceAccounting;
        struct ResourceSourcePackageGeneration;
    } // namespace detail
    enum class ResourceSourceResult : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        NotFound,
        TypeMismatch,
        IntegrityFailure,
        UnsupportedVersion,
        LimitExceeded,
        BufferTooSmall,
        Cancelled,
        IoFailure
    };

    [[nodiscard]] const char* ToString(ResourceSourceResult result) noexcept;

    enum class ResourceFailureClass : u8
    {
        None,
        Transient,
        Cancelled,
        Permanent
    };

    [[nodiscard]] ResourceFailureClass ClassifyFailure(ResourceSourceResult result) noexcept;

    enum class ResourceSourceKind : u8
    {
        LooseFile,
        Package
    };

    struct ResourceReadStats
    {
        u64 storedBytesRead = 0;
        u64 decodedBytesProduced = 0;
        u32 decodedSegments = 0;
    };

    /// Exact transient cost of one logical range read. Package-backed reads
    /// include every complete storage segment needed to authenticate and decode
    /// the requested range; loose reads map one-to-one to the requested bytes.
    struct ResourceReadPlan
    {
        u64 logicalBytes = 0;
        u64 storedBytes = 0;
        u64 decodedBytes = 0;
        u64 decodedRangeOffset = 0;
        u64 decodedRangeBytes = 0;
        u32 touchedSegments = 0;
    };

    struct ResourceRangeRead
    {
        u64 offset = 0;
        u64 size = 0;
        void* destination = nullptr;
        usize destinationSize = 0;
        io::AsyncPriority priority = io::eAsyncPriority_Streaming;
    };

    using ResourceReadCallback = void (*)(ResourceSourceResult result, const ResourceReadStats& stats, void* userData);

    /// One owned asynchronous logical-range operation. Destruction requests
    /// best-effort cancellation; the operation and source generation remain
    /// alive until all physical callbacks have completed.
    class ResourceReadRequest final
    {
    public:
        struct Impl;

        ResourceReadRequest() noexcept = default;
        ~ResourceReadRequest();

        ResourceReadRequest(const ResourceReadRequest&) = delete;
        ResourceReadRequest& operator=(const ResourceReadRequest&) = delete;
        ResourceReadRequest(ResourceReadRequest&& other) noexcept;
        ResourceReadRequest& operator=(ResourceReadRequest&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        void Wait() const noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds) const noexcept;
        [[nodiscard]] bool Cancel() noexcept;
        [[nodiscard]] ResourceSourceResult GetResult() const noexcept;
        [[nodiscard]] ResourceReadPlan GetPlan() const noexcept;
        [[nodiscard]] ResourceReadStats GetStats() const noexcept;
        void Reset() noexcept;

    private:
        explicit ResourceReadRequest(Impl* impl) noexcept;

        Impl* m_impl = nullptr;

        friend class ResourceSource;
    };

    class ResourceRangeReadQueue;

    /// One caller interest in an exact-range read. Equal ranges submitted to
    /// the same queue share one buffer and one physical read. Cancelling an
    /// interest cancels the physical operation only after the last interest
    /// leaves.
    class CoalescedResourceReadRequest final
    {
    public:
        struct Impl;

        CoalescedResourceReadRequest() noexcept = default;
        ~CoalescedResourceReadRequest();
        CoalescedResourceReadRequest(const CoalescedResourceReadRequest&) = delete;
        CoalescedResourceReadRequest& operator=(const CoalescedResourceReadRequest&) = delete;
        CoalescedResourceReadRequest(CoalescedResourceReadRequest&& other) noexcept;
        CoalescedResourceReadRequest& operator=(CoalescedResourceReadRequest&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        void Wait() const noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds) const noexcept;
        [[nodiscard]] bool Cancel() noexcept;
        [[nodiscard]] ResourceSourceResult GetResult() const noexcept;
        [[nodiscard]] ResourceReadStats GetStats() const noexcept;
        [[nodiscard]] u32 GetAttemptCount() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> GetBytes() const noexcept;
        [[nodiscard]] bool IsSameOperation(const CoalescedResourceReadRequest& other) const noexcept;
        void Reset() noexcept;

    private:
        explicit CoalescedResourceReadRequest(Impl* impl) noexcept;
        Impl* m_impl = nullptr;
        bool m_hasInterest = false;
        friend class ResourceRangeReadQueue;
    };

    /// Generic exact-range coalescer with bounded FIFO staging admission.
    /// Its source generation is retained by outstanding requests.
    class ResourceRangeReadQueue final
    {
    public:
        struct Impl;

        ResourceRangeReadQueue() noexcept = default;
        ~ResourceRangeReadQueue();
        ResourceRangeReadQueue(const ResourceRangeReadQueue&) = delete;
        ResourceRangeReadQueue& operator=(const ResourceRangeReadQueue&) = delete;

        [[nodiscard]] ResourceSourceResult Open(const ResourceSource& source, u32 maximumTransientRetries = 2) noexcept;
        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] ResourceSourceResult Read(u64 offset, u64 size, CoalescedResourceReadRequest& request, io::AsyncPriority priority = io::eAsyncPriority_Streaming) noexcept;
        /// Reads and verifies one exact logical range before exposing its bytes. Equal ranges only coalesce when
        /// their expected digests also match, so every physical result is hashed exactly once on completion.
        [[nodiscard]] ResourceSourceResult ReadVerified(u64 offset, u64 size, const crypto::Digest256& expectedDigest, CoalescedResourceReadRequest& request,
                                                        io::AsyncPriority priority = io::eAsyncPriority_Streaming) noexcept;

    private:
        [[nodiscard]] ResourceSourceResult ReadInternal(u64 offset, u64 size, const crypto::Digest256* expectedDigest, CoalescedResourceReadRequest& request,
                                                        io::AsyncPriority priority) noexcept;
        Impl* m_impl = nullptr;
    };

    /// Strong ownership of one immutable physical resource generation.
    /// The retained I/O handle pins the physical file while an owned package
    /// index maps logical resource ranges to independently decoded segments.
    class ResourceSource final
    {
    public:
        struct Impl;

        ResourceSource() noexcept = default;
        ~ResourceSource();

        ResourceSource(const ResourceSource&) = delete;
        ResourceSource& operator=(const ResourceSource&) = delete;
        ResourceSource(ResourceSource&& other) noexcept;
        ResourceSource& operator=(ResourceSource&& other) noexcept;

        [[nodiscard]] ResourceSourceResult OpenLoose(const filesystem::AbsolutePath& physicalPath, resources::ResourceId resource = resources::InvalidResourceId,
                                                     resources::ResourceTypeId type = resources::InvalidResourceTypeId) noexcept;
        [[nodiscard]] ResourceSourceResult OpenPackage(const filesystem::AbsolutePath& physicalPath, resources::ResourceId resource,
                                                       resources::ResourceTypeId expectedType = resources::InvalidResourceTypeId) noexcept;
        [[nodiscard]] ResourceSourceResult OpenPackage(detail::ResourceSourcePackageGeneration* generation, resources::ResourceId resource,
                                                       resources::ResourceTypeId expectedType = resources::InvalidResourceTypeId) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] ResourceSourceKind GetKind() const noexcept;
        [[nodiscard]] const filesystem::AbsolutePath& GetPhysicalPath() const noexcept;
        [[nodiscard]] u64 GetLogicalSize() const noexcept;
        [[nodiscard]] resources::ResourceId GetResourceId() const noexcept;
        [[nodiscard]] resources::ResourceTypeId GetResourceType() const noexcept;
        /// Returns the shared staging admission budget attached by ResourceStreamer.
        /// Zero means this is an ungoverned tooling source.
        [[nodiscard]] u64 GetStagingBudgetBytes() const noexcept;

        [[nodiscard]] ResourceSourceResult PlanRead(u64 offset, u64 size, ResourceReadPlan& plan) const noexcept;
        [[nodiscard]] ResourceSourceResult ReadAsync(const ResourceRangeRead& read, ResourceReadRequest& request, ResourceReadCallback callback = nullptr,
                                                     void* userData = nullptr) const noexcept;

        // ResourceStreamer-owned admission state; direct sources intentionally
        // remain ungoverned for tooling. Engine source resolution attaches it.
        void AttachAccounting(detail::ResourceSourceAccounting* accounting) noexcept;

    private:
        [[nodiscard]] ResourceSourceResult ReadAsyncInternal(const ResourceRangeRead& read, ResourceReadRequest& request, ResourceReadCallback callback, void* userData,
                                                             bool stagingPreReserved) const noexcept;
        Impl* m_impl = nullptr;

        friend struct CoalescedResourceReadRequest::Impl;
        friend class ResourceSourceReader;
        friend class ResourceRangeReadQueue;
        friend class ResourceStreamer;
    };

    /// Independently seekable logical view over an owned ResourceSource.
    /// Multiple readers may issue positional reads against the same pinned
    /// source concurrently; each package reader has independent decode state.
    class ResourceSourceReader final : public filesystem::IFile
    {
    public:
        struct Impl;

        ResourceSourceReader() noexcept;
        ~ResourceSourceReader() override;

        ResourceSourceReader(const ResourceSourceReader&) = delete;
        ResourceSourceReader& operator=(const ResourceSourceReader&) = delete;

        [[nodiscard]] ResourceSourceResult Open(const ResourceSource& source) noexcept;
        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] ResourceSourceResult GetLastResult() const noexcept;
        [[nodiscard]] ResourceReadStats GetStats() const noexcept;

        void Serialize(void* buffer, size_t size) override;
        [[nodiscard]] Uint64 GetOffset() const override;
        [[nodiscard]] Uint64 GetSize() const override;
        void Seek(Int64 offset) override;
        void Flush() override;
        [[nodiscard]] const char* GetFileNameForDebug() const override;

    private:
        Impl* m_impl = nullptr;
    };

    /// Read-only view of an acquired document prefix while preserving the
    /// complete logical file size for serialization bounds validation.
    class ResourcePrefixReader final : public filesystem::IFile
    {
    public:
        ResourcePrefixReader(containers::ArraySpan<const u8> bytes, u64 logicalSize, const char* debugName) noexcept;

        void Serialize(void* destination, size_t size) override;
        [[nodiscard]] Uint64 GetOffset() const override;
        [[nodiscard]] Uint64 GetSize() const override;
        void Seek(Int64 offset) override;
        void Flush() override;
        [[nodiscard]] const char* GetFileNameForDebug() const override;

    private:
        containers::ArraySpan<const u8> m_bytes;
        const char* m_debugName = "resource metadata prefix";
        u64 m_logicalSize = 0;
        u64 m_offset = 0;
    };
} // namespace vanguard::streaming

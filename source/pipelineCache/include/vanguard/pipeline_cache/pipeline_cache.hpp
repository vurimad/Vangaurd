#pragma once

#include <vanguard/pipelines/pipelines.hpp>

namespace vanguard::pipeline_cache
{
    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        CapacityExceeded,
        OutOfMemory,
        PayloadRetentionFailed,
        SchedulingFailed
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class State : u8
    {
        Pending,
        Valid,
        Invalid
    };

    enum class Failure : u8
    {
        None,
        InvalidRequest,
        CapacityExceeded,
        PayloadRetentionFailed,
        SchedulingFailed,
        BackendRejected,
        InvalidNativeObject,
        Invalidated,
        ShuttingDown
    };

    struct FailureEvidence
    {
        Failure failure = Failure::None;
        i64 backendCode = 0;
        char message[192]{};
    };

    struct NativePipeline
    {
        void* object = nullptr;
        u64 backendType = 0;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return object != nullptr;
        }
    };

    struct CreationPayload
    {
        void* data = nullptr;
        bool (*retain)(void* data) noexcept = nullptr;
        void (*release)(void* data) noexcept = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return data != nullptr && retain != nullptr && release != nullptr;
        }
    };

    using CreateFunction = bool (*)(pipelines::PipelineKind kind, const crypto::Digest256& concreteKey, void* payload,
                                    NativePipeline& output, FailureEvidence& failure, void* userData) noexcept;
    using DestroyFunction = void (*)(NativePipeline pipeline, void* userData) noexcept;

    struct Backend
    {
        CreateFunction create = nullptr;
        DestroyFunction destroy = nullptr;
        void* userData = nullptr;
        u64 identity = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return create != nullptr && destroy != nullptr && identity != 0;
        }
    };

    enum class Priority : u8
    {
        Background,
        Normal,
        Critical
    };

    struct Config
    {
        u32 maximumEntries = 16384;
        u32 maximumConcurrentCreations = 4;
    };

    struct Stats
    {
        u32 knownEntries = 0;
        u32 pendingEntries = 0;
        u32 validEntries = 0;
        u32 invalidEntries = 0;
        u32 activeWorkers = 0;
        u32 liveRequests = 0;
        u64 issuedRequests = 0;
        u64 coalescedRequests = 0;
        u64 completedCreations = 0;
        u64 failedCreations = 0;
        u64 invalidations = 0;
        u64 warmupRequests = 0;
    };

    class PipelineCache;
    struct Entry;

    class PipelineRequest final
    {
    public:
        PipelineRequest() noexcept = default;
        PipelineRequest(PipelineRequest&& other) noexcept;
        PipelineRequest& operator=(PipelineRequest&& other) noexcept;
        ~PipelineRequest();

        PipelineRequest(const PipelineRequest&) = delete;
        PipelineRequest& operator=(const PipelineRequest&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] State Status() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        [[nodiscard]] bool HasSucceeded() const noexcept;
        void Wait() const noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds = 0) const noexcept;
        [[nodiscard]] NativePipeline NativeObject() const noexcept;
        [[nodiscard]] FailureEvidence Error() const noexcept;
        [[nodiscard]] const crypto::Digest256& Key() const noexcept;
        [[nodiscard]] u64 Generation() const noexcept;
        [[nodiscard]] bool IsSameGeneration(const PipelineRequest& other) const noexcept;
        void Reset() noexcept;

    private:
        PipelineRequest(PipelineCache* cache, Entry* entry) noexcept;

        PipelineCache* m_cache = nullptr;
        Entry* m_entry = nullptr;

        friend class PipelineCache;
    };

    struct WarmupItem
    {
        crypto::Digest256 concreteKey;
        pipelines::PipelineKind kind = pipelines::PipelineKind::Graphics;
        CreationPayload payload;
        Priority priority = Priority::Background;
    };

    struct WarmupResult
    {
        u32 accepted = 0;
        u32 rejected = 0;
    };

    class PipelineCache final
    {
    public:
        struct Impl;

        PipelineCache() noexcept = default;
        ~PipelineCache();

        PipelineCache(const PipelineCache&) = delete;
        PipelineCache& operator=(const PipelineCache&) = delete;

        [[nodiscard]] bool Initialize(const Backend& backend, const Config& config = {}) noexcept;
        // Jobs and the backend device must outlive the cache. Shutdown refuses
        // outstanding work or request handles.
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] Result Request(const crypto::Digest256& concreteKey, pipelines::PipelineKind kind, const CreationPayload& payload,
                                     PipelineRequest& output, Priority priority = Priority::Normal) noexcept;
        [[nodiscard]] WarmupResult Warmup(containers::ArraySpan<const WarmupItem> items) noexcept;
        [[nodiscard]] bool Invalidate(const crypto::Digest256& concreteKey) noexcept;
        [[nodiscard]] u32 InvalidateAll() noexcept;
        void WaitIdle() const noexcept;
        [[nodiscard]] bool TryWaitIdle(u32 timeoutMilliseconds = 0) const noexcept;
        [[nodiscard]] Stats GetStats() const noexcept;

    private:
        void ReleaseInterest(Entry* entry) noexcept;

        Impl* m_impl = nullptr;

        friend class PipelineRequest;
    };
} // namespace vanguard::pipeline_cache

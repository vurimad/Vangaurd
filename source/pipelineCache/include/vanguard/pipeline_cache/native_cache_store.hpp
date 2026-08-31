#pragma once

#include <vanguard/pipeline_cache/pipeline_cache.hpp>

namespace vanguard::pipeline_cache
{
    inline constexpr u32 NativeCacheMagic = serialization::MakeFourCC('V', 'N', 'P', 'C');

    struct NativeCacheIdentity
    {
        u64 backend = 0;
        u32 backendVersion = 0;
        u32 cacheSchema = 0;
        u32 vendorId = 0;
        u32 deviceId = 0;
        u64 adapterId = 0;
        u64 driverVersion = 0;
        crypto::Digest256 backendCompatibility;
        crypto::Digest256 engineBuild;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return backend != 0 && backendVersion != 0 && cacheSchema != 0 && vendorId != 0 && deviceId != 0 && adapterId != 0 && driverVersion != 0 &&
                   !backendCompatibility.IsEmpty() && !engineBuild.IsEmpty();
        }
    };

    [[nodiscard]] bool operator==(const NativeCacheIdentity& left, const NativeCacheIdentity& right) noexcept;
    [[nodiscard]] crypto::Digest256 CalculateIdentityFingerprint(const NativeCacheIdentity& identity) noexcept;

    enum class StoreResult : u8
    {
        Success,
        Miss,
        Incompatible,
        Corrupt,
        InvalidArgument,
        InvalidState,
        LimitExceeded,
        OutOfMemory,
        BackendRejected,
        IoFailure
    };

    [[nodiscard]] const char* ToString(StoreResult result) noexcept;

    struct NativeCacheConfig
    {
        filesystem::AbsolutePath root;
        NativeCacheIdentity identity;
        u64 maximumBlobBytes = 256ull * 1024ull * 1024ull;
    };

    struct NativeCacheStats
    {
        u64 loadAttempts = 0;
        u64 hits = 0;
        u64 misses = 0;
        u64 incompatibleRecords = 0;
        u64 corruptRecords = 0;
        u64 backendRejections = 0;
        u64 stores = 0;
        u64 ioFailures = 0;
        u64 recoveries = 0;
        u64 loadedBytes = 0;
        u64 storedBytes = 0;
    };

    using ImportNativeCacheFunction = bool (*)(containers::ArraySpan<const u8> blob, void* userData) noexcept;
    using ExportNativeCacheFunction = bool (*)(containers::DynamicArray<u8>& blob, void* userData) noexcept;

    class NativeCacheStore final
    {
    public:
        struct Impl;

        NativeCacheStore() noexcept = default;
        ~NativeCacheStore();

        NativeCacheStore(const NativeCacheStore&) = delete;
        NativeCacheStore& operator=(const NativeCacheStore&) = delete;

        [[nodiscard]] bool Initialize(const NativeCacheConfig& config) noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] StoreResult Load(containers::DynamicArray<u8>& blob) noexcept;
        [[nodiscard]] StoreResult Publish(containers::ArraySpan<const u8> blob) noexcept;
        [[nodiscard]] StoreResult Restore(ImportNativeCacheFunction importer, void* userData = nullptr) noexcept;
        [[nodiscard]] StoreResult CaptureAndPublish(ExportNativeCacheFunction exporter, void* userData = nullptr) noexcept;
        [[nodiscard]] bool Clear() noexcept;

        [[nodiscard]] const NativeCacheIdentity& GetIdentity() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetIdentityFingerprint() const noexcept;
        [[nodiscard]] const filesystem::AbsolutePath& RecordPath() const noexcept;
        [[nodiscard]] NativeCacheStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::pipeline_cache

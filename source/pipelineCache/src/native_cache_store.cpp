#include <vanguard/pipeline_cache/native_cache_store.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/memory/pool.hpp>

namespace
{
    using namespace vanguard;
    namespace cache = vanguard::pipeline_cache;

    constexpr u16 MajorVersion = 1;
    constexpr u16 MinorVersion = 0;
    constexpr u32 HeaderSize = 208;
    constexpr char RecordName[] = "pipeline.native.vnpc";

    enum class ReadResult : u8
    {
        Hit,
        Miss,
        Incompatible,
        Corrupt,
        LimitExceeded,
        OutOfMemory,
        IoFailure
    };

    void HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashU64(crypto::Sha256Builder& hash, const u64 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u),
                            static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u), static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    [[nodiscard]] bool WriteIdentity(::vanguard::serialization::BinaryWriter& writer, const cache::NativeCacheIdentity& identity) noexcept
    {
        return writer.WriteU64(identity.backend) && writer.WriteU32(identity.backendVersion) && writer.WriteU32(identity.cacheSchema) &&
               writer.WriteU32(identity.vendorId) && writer.WriteU32(identity.deviceId) && writer.WriteU64(identity.adapterId) &&
               writer.WriteU64(identity.driverVersion) && writer.WriteBytes(identity.backendCompatibility.bytes, crypto::Digest256::ByteCount) &&
               writer.WriteBytes(identity.engineBuild.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool ReadIdentity(::vanguard::serialization::BinaryReader& reader, cache::NativeCacheIdentity& identity) noexcept
    {
        return reader.ReadU64(identity.backend) && reader.ReadU32(identity.backendVersion) && reader.ReadU32(identity.cacheSchema) &&
               reader.ReadU32(identity.vendorId) && reader.ReadU32(identity.deviceId) && reader.ReadU64(identity.adapterId) &&
               reader.ReadU64(identity.driverVersion) && reader.ReadBytes(identity.backendCompatibility.bytes, crypto::Digest256::ByteCount) &&
               reader.ReadBytes(identity.engineBuild.bytes, crypto::Digest256::ByteCount);
    }

    [[nodiscard]] bool WriteRecord(const filesystem::AbsolutePath& path, const cache::NativeCacheIdentity& identity,
                                   const containers::ArraySpan<const u8> blob) noexcept
    {
        auto file = filesystem::GetManager().CreateFileWriter(path, filesystem::FOF_Buffered);
        if (!file)
        {
            return false;
        }
        const crypto::Digest256 identityFingerprint = cache::CalculateIdentityFingerprint(identity);
        const crypto::Digest256 payloadFingerprint = crypto::Sha256(blob.Data(), blob.SizeInBytes());
        const u64 fileSize = HeaderSize + blob.SizeInBytes();
        ::vanguard::serialization::BinaryWriter writer(*file);
        const bool written = writer.WriteU32(cache::NativeCacheMagic) && writer.WriteU16(MajorVersion) && writer.WriteU16(MinorVersion) &&
                             writer.WriteU32(HeaderSize) && writer.WriteU32(0) && writer.WriteU64(fileSize) && writer.WriteU64(blob.SizeInBytes()) &&
                             writer.WriteBytes(identityFingerprint.bytes, crypto::Digest256::ByteCount) &&
                             writer.WriteBytes(payloadFingerprint.bytes, crypto::Digest256::ByteCount) && WriteIdentity(writer, identity) &&
                             writer.WriteU64(0) && writer.WriteBytes(blob.Data(), blob.SizeInBytes()) && writer.Position() == fileSize && writer.Flush();
        file.Reset();
        return written;
    }

    [[nodiscard]] ReadResult ReadRecord(const filesystem::AbsolutePath& path, const cache::NativeCacheIdentity& expected, const u64 maximumBlobBytes,
                                        containers::DynamicArray<u8>& blob) noexcept
    {
        filesystem::Manager& manager = filesystem::GetManager();
        if (!manager.FileExist(path))
        {
            return ReadResult::Miss;
        }
        auto file = manager.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!file)
        {
            return ReadResult::IoFailure;
        }
        ::vanguard::serialization::BinaryReader reader(*file);
        u32 magic = 0;
        u16 major = 0;
        u16 minor = 0;
        u32 headerSize = 0;
        u32 flags = 0;
        u64 fileSize = 0;
        u64 payloadSize = 0;
        u64 reserved = 0;
        crypto::Digest256 storedIdentityFingerprint;
        crypto::Digest256 payloadFingerprint;
        cache::NativeCacheIdentity storedIdentity;
        if (!reader.ReadU32(magic) || !reader.ReadU16(major) || !reader.ReadU16(minor) || !reader.ReadU32(headerSize) || !reader.ReadU32(flags) ||
            !reader.ReadU64(fileSize) || !reader.ReadU64(payloadSize) || !reader.ReadBytes(storedIdentityFingerprint.bytes, crypto::Digest256::ByteCount) ||
            !reader.ReadBytes(payloadFingerprint.bytes, crypto::Digest256::ByteCount) || !ReadIdentity(reader, storedIdentity) || !reader.ReadU64(reserved))
        {
            return ReadResult::Corrupt;
        }
        if (magic != cache::NativeCacheMagic || major != MajorVersion || minor > MinorVersion || headerSize != HeaderSize || flags != 0 || reserved != 0 ||
            fileSize != reader.Size() || fileSize < HeaderSize || payloadSize != fileSize - HeaderSize)
        {
            return ReadResult::Corrupt;
        }
        if (!storedIdentity.IsValid() || cache::CalculateIdentityFingerprint(storedIdentity) != storedIdentityFingerprint)
        {
            return ReadResult::Corrupt;
        }
        if (!(storedIdentity == expected))
        {
            return ReadResult::Incompatible;
        }
        if (payloadSize == 0 || payloadSize > maximumBlobBytes || payloadSize > ~u32{0})
        {
            return ReadResult::LimitExceeded;
        }
        blob.Clear();
        blob.Resize(static_cast<u32>(payloadSize));
        if (blob.Size() != payloadSize)
        {
            return ReadResult::OutOfMemory;
        }
        if (!reader.ReadBytes(blob.Data(), blob.Size()) || reader.Position() != fileSize || crypto::Sha256(blob.Data(), blob.Size()) != payloadFingerprint)
        {
            blob.Clear();
            return ReadResult::Corrupt;
        }
        return ReadResult::Hit;
    }

    [[nodiscard]] cache::StoreResult ConvertReadResult(const ReadResult result) noexcept
    {
        switch (result)
        {
        case ReadResult::Hit:
            return cache::StoreResult::Success;
        case ReadResult::Miss:
            return cache::StoreResult::Miss;
        case ReadResult::Incompatible:
            return cache::StoreResult::Incompatible;
        case ReadResult::Corrupt:
            return cache::StoreResult::Corrupt;
        case ReadResult::LimitExceeded:
            return cache::StoreResult::LimitExceeded;
        case ReadResult::OutOfMemory:
            return cache::StoreResult::OutOfMemory;
        case ReadResult::IoFailure:
            return cache::StoreResult::IoFailure;
        }
        return cache::StoreResult::IoFailure;
    }
} // namespace

namespace vanguard::pipeline_cache
{
    struct NativeCacheStore::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        NativeCacheConfig config;
        crypto::Digest256 identityFingerprint;
        filesystem::AbsolutePath recordPath;
        mutable concurrency::Mutex lock;
        NativeCacheStats stats;
        bool initialized = false;
    };

    bool operator==(const NativeCacheIdentity& left, const NativeCacheIdentity& right) noexcept
    {
        return left.backend == right.backend && left.backendVersion == right.backendVersion && left.cacheSchema == right.cacheSchema &&
               left.vendorId == right.vendorId && left.deviceId == right.deviceId && left.adapterId == right.adapterId &&
               left.driverVersion == right.driverVersion && left.backendCompatibility == right.backendCompatibility && left.engineBuild == right.engineBuild;
    }

    crypto::Digest256 CalculateIdentityFingerprint(const NativeCacheIdentity& identity) noexcept
    {
        constexpr char Domain[] = "vanguard.native-pipeline-cache-identity.v1";
        crypto::Sha256Builder hash;
        static_cast<void>(hash.Update(Domain, sizeof(Domain) - 1u));
        HashU64(hash, identity.backend);
        HashU32(hash, identity.backendVersion);
        HashU32(hash, identity.cacheSchema);
        HashU32(hash, identity.vendorId);
        HashU32(hash, identity.deviceId);
        HashU64(hash, identity.adapterId);
        HashU64(hash, identity.driverVersion);
        static_cast<void>(hash.Update(identity.backendCompatibility.bytes, crypto::Digest256::ByteCount));
        static_cast<void>(hash.Update(identity.engineBuild.bytes, crypto::Digest256::ByteCount));
        crypto::Digest256 result;
        static_cast<void>(hash.Finalize(result));
        return result;
    }

    const char* ToString(const StoreResult result) noexcept
    {
        switch (result)
        {
        case StoreResult::Success:
            return "Success";
        case StoreResult::Miss:
            return "Miss";
        case StoreResult::Incompatible:
            return "Incompatible";
        case StoreResult::Corrupt:
            return "Corrupt";
        case StoreResult::InvalidArgument:
            return "InvalidArgument";
        case StoreResult::InvalidState:
            return "InvalidState";
        case StoreResult::LimitExceeded:
            return "LimitExceeded";
        case StoreResult::OutOfMemory:
            return "OutOfMemory";
        case StoreResult::BackendRejected:
            return "BackendRejected";
        case StoreResult::IoFailure:
            return "IoFailure";
        }
        return "Unknown";
    }

    NativeCacheStore::~NativeCacheStore()
    {
        Shutdown();
    }

    bool NativeCacheStore::Initialize(const NativeCacheConfig& config) noexcept
    {
        if (m_impl != nullptr || !filesystem::IsInitialized() || config.root.Empty() || !config.identity.IsValid() || config.maximumBlobBytes == 0 ||
            config.maximumBlobBytes > ~u32{0})
        {
            return false;
        }
        Impl* const impl = VANGUARD_NEW(Impl);
        if (impl == nullptr)
        {
            return false;
        }
        impl->config = config;
        impl->identityFingerprint = CalculateIdentityFingerprint(config.identity);
        impl->recordPath = config.root.AddFilePath(RecordName);
        filesystem::Manager& manager = filesystem::GetManager();
        if (!manager.CreatePath(config.root))
        {
            VANGUARD_DELETE(impl);
            return false;
        }
        containers::DynamicArray<filesystem::AbsolutePath> stale{memory::pools::Rendering::GetInstance()};
        manager.FindFiles(config.root, containers::String("*.tmp"), stale, false);
        for (const filesystem::AbsolutePath& path : stale)
        {
            if (manager.DeleteFile(path))
            {
                ++impl->stats.recoveries;
            }
        }
        impl->initialized = true;
        m_impl = impl;
        return true;
    }

    void NativeCacheStore::Shutdown() noexcept
    {
        if (m_impl != nullptr)
        {
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
        }
    }

    bool NativeCacheStore::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_impl->initialized;
    }

    StoreResult NativeCacheStore::Load(containers::DynamicArray<u8>& blob) noexcept
    {
        blob.Clear();
        if (m_impl == nullptr)
        {
            return StoreResult::InvalidState;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        ++m_impl->stats.loadAttempts;
        const ReadResult result = ReadRecord(m_impl->recordPath, m_impl->config.identity, m_impl->config.maximumBlobBytes, blob);
        switch (result)
        {
        case ReadResult::Hit:
            ++m_impl->stats.hits;
            m_impl->stats.loadedBytes += blob.Size();
            break;
        case ReadResult::Miss:
            ++m_impl->stats.misses;
            break;
        case ReadResult::Incompatible:
            ++m_impl->stats.incompatibleRecords;
            if (filesystem::GetManager().DeleteFile(m_impl->recordPath))
            {
                ++m_impl->stats.recoveries;
            }
            break;
        case ReadResult::Corrupt:
            ++m_impl->stats.corruptRecords;
            if (filesystem::GetManager().DeleteFile(m_impl->recordPath))
            {
                ++m_impl->stats.recoveries;
            }
            break;
        case ReadResult::LimitExceeded:
        case ReadResult::OutOfMemory:
        case ReadResult::IoFailure:
            ++m_impl->stats.ioFailures;
            break;
        }
        return ConvertReadResult(result);
    }

    StoreResult NativeCacheStore::Publish(const containers::ArraySpan<const u8> blob) noexcept
    {
        if (m_impl == nullptr)
        {
            return StoreResult::InvalidState;
        }
        if (blob.Empty())
        {
            return StoreResult::InvalidArgument;
        }
        if (blob.SizeInBytes() > m_impl->config.maximumBlobBytes)
        {
            return StoreResult::LimitExceeded;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        filesystem::Manager& manager = filesystem::GetManager();
        const filesystem::AbsolutePath generated = manager.GenerateTemporaryFilePath();
        const filesystem::AbsolutePath temporary = m_impl->config.root.AddFilePath(filesystem::paths::GetFileName(generated));
        if (!WriteRecord(temporary, m_impl->config.identity, blob))
        {
            static_cast<void>(manager.DeleteFile(temporary));
            ++m_impl->stats.ioFailures;
            return StoreResult::IoFailure;
        }
        containers::DynamicArray<u8> validation{memory::pools::Rendering::GetInstance()};
        if (ReadRecord(temporary, m_impl->config.identity, m_impl->config.maximumBlobBytes, validation) != ReadResult::Hit ||
            validation.Size() != blob.Size() || crypto::Sha256(validation.Data(), validation.Size()) != crypto::Sha256(blob.Data(), blob.SizeInBytes()))
        {
            static_cast<void>(manager.DeleteFile(temporary));
            ++m_impl->stats.ioFailures;
            return StoreResult::IoFailure;
        }
        const bool published = filesystem::SystemIO::MoveFile(temporary.AsChar(), m_impl->recordPath.AsChar());
        if (!published)
        {
            static_cast<void>(manager.DeleteFile(temporary));
            ++m_impl->stats.ioFailures;
            return StoreResult::IoFailure;
        }
        ++m_impl->stats.stores;
        m_impl->stats.storedBytes += blob.SizeInBytes();
        return StoreResult::Success;
    }

    StoreResult NativeCacheStore::Restore(const ImportNativeCacheFunction importer, void* const userData) noexcept
    {
        if (importer == nullptr)
        {
            return StoreResult::InvalidArgument;
        }
        containers::DynamicArray<u8> blob{memory::pools::Rendering::GetInstance()};
        const StoreResult loaded = Load(blob);
        if (loaded != StoreResult::Success)
        {
            return loaded;
        }
        if (!importer({blob.TypedData(), blob.Size()}, userData))
        {
            concurrency::ScopedLock guard(m_impl->lock);
            ++m_impl->stats.backendRejections;
            if (filesystem::GetManager().DeleteFile(m_impl->recordPath))
            {
                ++m_impl->stats.recoveries;
            }
            return StoreResult::BackendRejected;
        }
        return StoreResult::Success;
    }

    StoreResult NativeCacheStore::CaptureAndPublish(const ExportNativeCacheFunction exporter, void* const userData) noexcept
    {
        if (m_impl == nullptr)
        {
            return StoreResult::InvalidState;
        }
        if (exporter == nullptr)
        {
            return StoreResult::InvalidArgument;
        }
        containers::DynamicArray<u8> blob{memory::pools::Rendering::GetInstance()};
        if (!exporter(blob, userData))
        {
            return StoreResult::BackendRejected;
        }
        return Publish({blob.TypedData(), blob.Size()});
    }

    bool NativeCacheStore::Clear() noexcept
    {
        if (m_impl == nullptr)
        {
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        return !filesystem::GetManager().FileExist(m_impl->recordPath) || filesystem::GetManager().DeleteFile(m_impl->recordPath);
    }

    const NativeCacheIdentity& NativeCacheStore::GetIdentity() const noexcept
    {
        static const NativeCacheIdentity invalid;
        return m_impl != nullptr ? m_impl->config.identity : invalid;
    }

    const crypto::Digest256& NativeCacheStore::GetIdentityFingerprint() const noexcept
    {
        static const crypto::Digest256 invalid;
        return m_impl != nullptr ? m_impl->identityFingerprint : invalid;
    }

    const filesystem::AbsolutePath& NativeCacheStore::RecordPath() const noexcept
    {
        static const filesystem::AbsolutePath invalid;
        return m_impl != nullptr ? m_impl->recordPath : invalid;
    }

    NativeCacheStats NativeCacheStore::GetStats() const noexcept
    {
        if (m_impl == nullptr)
        {
            return {};
        }
        concurrency::ScopedLock guard(m_impl->lock);
        return m_impl->stats;
    }
} // namespace vanguard::pipeline_cache

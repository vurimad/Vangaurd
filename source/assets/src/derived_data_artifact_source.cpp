#include <vanguard/assets/derived_data_artifact_source.hpp>
#include <vanguard/assets/derived_data_package_artifact_reader.hpp>

#include <vanguard/assets/derived_data_artifact_source_internal.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/serialization/serialization.hpp>

#include <limits>

namespace
{
    using namespace vanguard;
    namespace assets = vanguard::assets;

    constexpr u16 KnownArtifactFlags = static_cast<u16>(assets::ArtifactFlags::Primary) |
                                       static_cast<u16>(assets::ArtifactFlags::Streamable) |
                                       static_cast<u16>(assets::ArtifactFlags::MemoryResident) |
                                       static_cast<u16>(assets::ArtifactFlags::EditorOnly);

    [[nodiscard]] bool HashU8(crypto::Sha256Builder& hash, const u8 value) noexcept
    {
        return hash.Update(&value, sizeof(value));
    }

    [[nodiscard]] bool HashU16(crypto::Sha256Builder& hash, const u16 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u)};
        return hash.Update(bytes, sizeof(bytes));
    }

    [[nodiscard]] bool HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u),
                            static_cast<u8>(value >> 24u)};
        return hash.Update(bytes, sizeof(bytes));
    }

    [[nodiscard]] bool HashU64(crypto::Sha256Builder& hash, const u64 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value),        static_cast<u8>(value >> 8u),
                            static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u),
                            static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u),
                            static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
        return hash.Update(bytes, sizeof(bytes));
    }

    struct ArtifactIdentity
    {
        u64 path = resources::InvalidResourceId;
        u32 type = resources::InvalidResourceTypeId;
        u32 segment = 0;

        [[nodiscard]] u32 CalcHash() const noexcept
        {
            u64 value = path;
            value ^= static_cast<u64>(type) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
            value ^= static_cast<u64>(segment) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
            value ^= value >> 33u;
            value *= 0xff51afd7ed558ccdull;
            value ^= value >> 33u;
            return static_cast<u32>(value ^ (value >> 32u));
        }

        [[nodiscard]] friend constexpr bool operator==(const ArtifactIdentity&, const ArtifactIdentity&) noexcept = default;
    };

    [[nodiscard]] ArtifactIdentity Identity(const assets::CachedArtifactDescriptor& artifact) noexcept
    {
        return {artifact.resource.GetPath().Id(), artifact.resource.ExpectedType(), artifact.segment};
    }
} // namespace

namespace vanguard::assets::detail
{
    namespace
    {
        [[nodiscard]] char HexDigit(const u8 value) noexcept
        {
            return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
        }

        [[nodiscard]] bool HexValue(const char value, u8& output) noexcept
        {
            if (value >= '0' && value <= '9')
            {
                output = static_cast<u8>(value - '0');
                return true;
            }
            if (value >= 'a' && value <= 'f')
            {
                output = static_cast<u8>(value - 'a' + 10);
                return true;
            }
            if (value >= 'A' && value <= 'F')
            {
                output = static_cast<u8>(value - 'A' + 10);
                return true;
            }
            return false;
        }

        [[nodiscard]] int CompareFingerprint(const BuildFingerprint& left, const BuildFingerprint& right) noexcept
        {
            for (u32 index = 0; index < BuildFingerprint::ByteCount; ++index)
            {
                if (left.bytes[index] < right.bytes[index])
                    return -1;
                if (left.bytes[index] > right.bytes[index])
                    return 1;
            }
            return 0;
        }

        [[nodiscard]] bool IsBuildProtected(const containers::ArraySpan<const ArtifactSetKey> keys,
                                            const BuildFingerprint& build) noexcept
        {
            u32 first = 0u;
            u32 count = keys.Count();
            while (count != 0u)
            {
                const u32 step = count / 2u;
                const u32 middle = first + step;
                const int comparison = CompareFingerprint(keys[middle].build, build);
                if (comparison < 0)
                {
                    first = middle + 1u;
                    count -= step + 1u;
                }
                else
                {
                    count = step;
                }
            }
            return first < keys.Count() && keys[first].build == build;
        }
    } // namespace

    filesystem::AbsolutePath PersistentRecordDirectory(const filesystem::AbsolutePath& root,
                                                        const BuildFingerprint& fingerprint) noexcept
    {
        char first[3] = {HexDigit(fingerprint.bytes[0] >> 4u), HexDigit(fingerprint.bytes[0] & 0x0fu), '\0'};
        char second[3] = {HexDigit(fingerprint.bytes[1] >> 4u), HexDigit(fingerprint.bytes[1] & 0x0fu), '\0'};
        return root.AddDirPath(first).AddDirPath(second);
    }

    filesystem::AbsolutePath PersistentRecordPath(const filesystem::AbsolutePath& root,
                                                   const BuildFingerprint& fingerprint) noexcept
    {
        char fileName[70];
        for (u32 index = 0; index < BuildFingerprint::ByteCount; ++index)
        {
            fileName[index * 2u] = HexDigit(fingerprint.bytes[index] >> 4u);
            fileName[index * 2u + 1u] = HexDigit(fingerprint.bytes[index] & 0x0fu);
        }
        fileName[64] = '.';
        fileName[65] = 'v';
        fileName[66] = 'd';
        fileName[67] = 'd';
        fileName[68] = 'c';
        fileName[69] = '\0';
        return PersistentRecordDirectory(root, fingerprint).AddFilePath(fileName);
    }

    filesystem::AbsolutePath PersistentRecordStagePath(const filesystem::AbsolutePath& root,
                                                        const BuildFingerprint& fingerprint) noexcept
    {
        char fileName[74];
        for (u32 index = 0; index < BuildFingerprint::ByteCount; ++index)
        {
            fileName[index * 2u] = HexDigit(fingerprint.bytes[index] >> 4u);
            fileName[index * 2u + 1u] = HexDigit(fingerprint.bytes[index] & 0x0fu);
        }
        constexpr char suffix[] = ".vddc.tmp";
        for (u32 index = 0; index < sizeof(suffix); ++index)
            fileName[64u + index] = suffix[index];
        return PersistentRecordDirectory(root, fingerprint).AddFilePath(fileName);
    }

    bool ParsePersistentRecordPath(const filesystem::AbsolutePath& root, const filesystem::AbsolutePath& path,
                                   BuildFingerprint& fingerprint) noexcept
    {
        fingerprint = {};
        if (root.Empty() || path.Empty())
            return false;
        const containers::StringView name = filesystem::paths::GetFileName(path);
        if (name.Length() != 69u || name.Slice(64u, 69u) != containers::StringView(".vddc"))
            return false;
        for (u32 index = 0; index < BuildFingerprint::ByteCount; ++index)
        {
            u8 high = 0;
            u8 low = 0;
            if (!HexValue(name[index * 2u], high) || !HexValue(name[index * 2u + 1u], low))
            {
                fingerprint = {};
                return false;
            }
            fingerprint.bytes[index] = static_cast<u8>((high << 4u) | low);
        }
        return PersistentRecordPath(root, fingerprint) == path;
    }

    bool ParsePersistentRecordStagePath(const filesystem::AbsolutePath& root, const filesystem::AbsolutePath& path,
                                        BuildFingerprint& fingerprint) noexcept
    {
        fingerprint = {};
        if (root.Empty() || path.Empty())
            return false;
        const containers::StringView name = filesystem::paths::GetFileName(path);
        if (name.Length() != 73u || name.Slice(64u, 73u) != containers::StringView(".vddc.tmp"))
            return false;
        for (u32 index = 0; index < BuildFingerprint::ByteCount; ++index)
        {
            u8 high = 0;
            u8 low = 0;
            if (!HexValue(name[index * 2u], high) || !HexValue(name[index * 2u + 1u], low))
            {
                fingerprint = {};
                return false;
            }
            fingerprint.bytes[index] = static_cast<u8>((high << 4u) | low);
        }
        return PersistentRecordStagePath(root, fingerprint) == path;
    }

    ArtifactSetFingerprintBuilder::ArtifactSetFingerprintBuilder(const u32 artifactCount) noexcept
    {
        constexpr char Domain[] = "vanguard.artifact-set.v1";
        m_valid = m_hash.Update(Domain, sizeof(Domain) - 1u);
        m_valid = m_valid && HashU32(m_hash, artifactCount);
    }

    bool ArtifactSetFingerprintBuilder::AddDescriptor(const resources::ResourceReference resource, const u32 segment,
                                                      const ArtifactFlags flags, const u8 alignmentLog2,
                                                      const u64 byteCount) noexcept
    {
        if (!m_valid)
            return false;
        m_valid = HashU64(m_hash, resource.GetPath().Id()) && HashU32(m_hash, resource.ExpectedType()) &&
                  HashU32(m_hash, segment) && HashU16(m_hash, static_cast<u16>(flags)) &&
                  HashU8(m_hash, alignmentLog2) && HashU64(m_hash, byteCount);
        return m_valid;
    }

    bool ArtifactSetFingerprintBuilder::AddBytes(const void* const bytes, const usize size) noexcept
    {
        m_valid = m_valid && m_hash.Update(bytes, size);
        return m_valid;
    }

    bool ArtifactSetFingerprintBuilder::Finalize(BuildFingerprint& fingerprint) noexcept
    {
        return m_valid && m_hash.Finalize(fingerprint);
    }
} // namespace vanguard::assets::detail

namespace vanguard::assets
{
    struct BuildOutputProtection::Impl final
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        detail::PersistentRootAuthority* authority = nullptr;
        BuildFingerprint build;
        BuildFingerprint durableContent;
        u64 durableDomainGeneration = 0u;
        u32 references = 1;
    };
} // namespace vanguard::assets

namespace vanguard::assets::detail
{
    struct PersistentLeaseRecord final
    {
        BuildFingerprint build;
        u32 readers = 0;
        u32 writers = 0;
        u32 publications = 0;
        u32 deletions = 0;
    };

    struct PersistentRootAuthority final
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        PersistentRootAuthority() noexcept : leases(memory::pools::Assets::GetInstance()) {}

        filesystem::AbsolutePath root;
        PersistentCacheCoordinationLimits limits;
        containers::DynamicArray<PersistentLeaseRecord> leases;
        const void* protectionDomainOwner = nullptr;
        u64 protectionDomainGeneration = 0u;
        u32 participants = 0;
        u32 liveLeases = 0;
        u64 protectionEpoch = 1;
    };

    namespace
    {
        struct PersistentAuthorityRegistry final
        {
            PersistentAuthorityRegistry() noexcept : roots(memory::pools::Assets::GetInstance()) {}

            concurrency::Mutex lock;
            containers::DynamicArray<PersistentRootAuthority*> roots;
            u32 maximumRegisteredRoots = 0;
        };

        [[nodiscard]] PersistentAuthorityRegistry& AuthorityRegistry() noexcept
        {
            static PersistentAuthorityRegistry registry;
            return registry;
        }

        [[nodiscard]] u32 FindLease(const PersistentRootAuthority& authority, const BuildFingerprint& build) noexcept
        {
            for (u32 index = 0; index < authority.leases.Size(); ++index)
                if (authority.leases[index].build == build)
                    return index;
            return authority.leases.Size();
        }

        [[nodiscard]] PersistentLeaseRecord* EnsureLease(PersistentRootAuthority& authority, const BuildFingerprint& build) noexcept
        {
            const u32 found = FindLease(authority, build);
            if (found < authority.leases.Size())
                return &authority.leases[found];
            const u32 previous = authority.leases.Size();
            authority.leases.PushBack({build});
            return authority.leases.Size() == previous + 1u ? &authority.leases.Back() : nullptr;
        }

        void RemoveEmptyLease(PersistentRootAuthority& authority, const BuildFingerprint& build) noexcept
        {
            const u32 index = FindLease(authority, build);
            if (index >= authority.leases.Size())
                return;
            const PersistentLeaseRecord& lease = authority.leases[index];
            if (lease.readers == 0u && lease.writers == 0u && lease.publications == 0u && lease.deletions == 0u)
                static_cast<void>(authority.leases.RemoveAt(index));
        }

        void DestroyAuthorityIfUnusedLocked(PersistentAuthorityRegistry& registry, PersistentRootAuthority* const authority) noexcept
        {
            if (authority == nullptr || authority->participants != 0u || authority->liveLeases != 0u ||
                authority->protectionDomainOwner != nullptr)
                return;
            for (u32 index = 0; index < registry.roots.Size(); ++index)
            {
                if (registry.roots[index] != authority)
                    continue;
                static_cast<void>(registry.roots.RemoveAt(index));
                authority->leases.Clear();
                authority->leases.Shrink();
                VANGUARD_DELETE(authority);
                if (registry.roots.Empty())
                {
                    registry.roots.Shrink();
                    registry.maximumRegisteredRoots = 0u;
                }
                return;
            }
        }

        [[nodiscard]] bool BeginDeleteLocked(PersistentRootAuthority& authority, const BuildFingerprint& build) noexcept
        {
            if (authority.liveLeases >= authority.limits.maximumLiveLeasesPerRoot)
                return false;
            PersistentLeaseRecord* const record = EnsureLease(authority, build);
            if (record == nullptr || record->readers != 0u || record->writers != 0u || record->publications != 0u || record->deletions != 0u)
                return false;
            record->deletions = 1u;
            ++authority.liveLeases;
            return true;
        }

        void EndDeleteLocked(PersistentRootAuthority& authority, const BuildFingerprint& build) noexcept
        {
            const u32 index = FindLease(authority, build);
            if (index < authority.leases.Size() && authority.leases[index].deletions != 0u)
            {
                authority.leases[index].deletions = 0u;
                if (authority.liveLeases != 0u)
                    --authority.liveLeases;
            }
            RemoveEmptyLease(authority, build);
        }

        [[nodiscard]] bool IsExactMutationPath(const PersistentRootAuthority& authority, const BuildFingerprint& build,
                                               const filesystem::AbsolutePath& path, const bool stage) noexcept
        {
            const filesystem::AbsolutePath expected = stage ? PersistentRecordStagePath(authority.root, build)
                                                            : PersistentRecordPath(authority.root, build);
            return expected == path && filesystem::ValidateMutationPath(authority.root, path) == filesystem::MutationPathResult::Success;
        }
    } // namespace

    bool PersistentCacheAuthorityAccess::Join(const filesystem::AbsolutePath& root, const PersistentCacheCoordinationLimits& limits,
                                              PersistentRootAuthority*& authority) noexcept
    {
        authority = nullptr;
        if (root.Empty() || !limits.IsValid() || filesystem::ValidateMutationRoot(root) != filesystem::MutationPathResult::Success)
            return false;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        for (PersistentRootAuthority* const existing : registry.roots)
        {
            if (existing->root != root)
                continue;
            if (existing->limits != limits)
                return false;
            ++existing->participants;
            authority = existing;
            return true;
        }
        if ((registry.maximumRegisteredRoots != 0u && registry.maximumRegisteredRoots != limits.maximumRegisteredRoots) ||
            registry.roots.Size() >= limits.maximumRegisteredRoots)
            return false;
        PersistentRootAuthority* const created = VANGUARD_NEW(PersistentRootAuthority);
        if (created == nullptr)
            return false;
        created->root = root;
        created->limits = limits;
        created->participants = 1u;
        const u32 previous = registry.roots.Size();
        registry.roots.PushBack(created);
        if (registry.roots.Size() != previous + 1u)
        {
            VANGUARD_DELETE(created);
            return false;
        }
        if (registry.maximumRegisteredRoots == 0u)
            registry.maximumRegisteredRoots = limits.maximumRegisteredRoots;
        authority = created;
        return true;
    }

    void PersistentCacheAuthorityAccess::Leave(PersistentRootAuthority*& authority) noexcept
    {
        if (authority == nullptr)
            return;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        PersistentRootAuthority* const leaving = authority;
        authority = nullptr;
        if (leaving->participants != 0u)
            --leaving->participants;
        DestroyAuthorityIfUnusedLocked(registry, leaving);
    }

    DerivedDataArtifactResult PersistentCacheAuthorityAccess::AcquireReader(PersistentRootAuthority* const authority,
                                                                            const BuildFingerprint& build) noexcept
    {
        if (authority == nullptr || build.IsEmpty())
            return DerivedDataArtifactResult::InvalidArgument;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        if (authority->liveLeases >= authority->limits.maximumLiveLeasesPerRoot)
            return DerivedDataArtifactResult::LimitExceeded;
        PersistentLeaseRecord* const record = EnsureLease(*authority, build);
        if (record == nullptr)
            return DerivedDataArtifactResult::OutOfMemory;
        if (record->writers != 0u || record->deletions != 0u)
            return DerivedDataArtifactResult::InvalidState;
        ++record->readers;
        ++authority->liveLeases;
        return DerivedDataArtifactResult::Success;
    }

    void PersistentCacheAuthorityAccess::ReleaseReader(PersistentRootAuthority* const authority, const BuildFingerprint& build) noexcept
    {
        if (authority == nullptr || build.IsEmpty())
            return;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        const u32 index = FindLease(*authority, build);
        if (index < authority->leases.Size() && authority->leases[index].readers != 0u)
        {
            --authority->leases[index].readers;
            if (authority->liveLeases != 0u)
                --authority->liveLeases;
        }
        RemoveEmptyLease(*authority, build);
        DestroyAuthorityIfUnusedLocked(registry, authority);
    }

    bool PersistentCacheAuthorityAccess::AcquirePublication(PersistentRootAuthority* const authority, const BuildFingerprint& build,
                                                            BuildOutputProtection& protection) noexcept
    {
        protection.Reset();
        if (authority == nullptr || build.IsEmpty())
            return false;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        if (authority->liveLeases >= authority->limits.maximumLiveLeasesPerRoot)
            return false;
        PersistentLeaseRecord* const record = EnsureLease(*authority, build);
        if (record == nullptr || record->writers != 0u || record->deletions != 0u)
            return false;
        BuildOutputProtection::Impl* const implementation = VANGUARD_NEW(BuildOutputProtection::Impl);
        if (implementation == nullptr)
            return false;
        implementation->authority = authority;
        implementation->build = build;
        ++record->publications;
        ++authority->liveLeases;
        protection.m_impl = implementation;
        return true;
    }

    DerivedDataArtifactResult PersistentCacheAuthorityAccess::AcquireWriter(PersistentRootAuthority* const authority,
                                                                            const BuildFingerprint& build,
                                                                            PersistentWriterLease& lease) noexcept
    {
        lease.Reset();
        if (authority == nullptr || build.IsEmpty())
            return DerivedDataArtifactResult::InvalidArgument;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        if (authority->liveLeases >= authority->limits.maximumLiveLeasesPerRoot)
            return DerivedDataArtifactResult::LimitExceeded;
        PersistentLeaseRecord* const record = EnsureLease(*authority, build);
        if (record == nullptr)
            return DerivedDataArtifactResult::OutOfMemory;
        if (record->readers != 0u || record->writers != 0u || record->publications != 0u || record->deletions != 0u)
            return DerivedDataArtifactResult::InvalidState;
        record->writers = 1u;
        ++authority->liveLeases;
        lease.m_authority = authority;
        lease.m_build = build;
        return DerivedDataArtifactResult::Success;
    }

    bool PersistentCacheAuthorityAccess::PromoteWriterToPublication(PersistentWriterLease& writer,
                                                                    BuildOutputProtection& protection) noexcept
    {
        protection.Reset();
        if (!writer.IsValid())
            return false;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        PersistentRootAuthority* const authority = writer.m_authority;
        const BuildFingerprint build = writer.m_build;
        const u32 index = FindLease(*authority, build);
        if (index >= authority->leases.Size() || authority->leases[index].writers == 0u)
            return false;
        BuildOutputProtection::Impl* const implementation = VANGUARD_NEW(BuildOutputProtection::Impl);
        if (implementation == nullptr)
            return false;
        implementation->authority = authority;
        implementation->build = build;
        authority->leases[index].writers = 0u;
        ++authority->leases[index].publications;
        writer.m_authority = nullptr;
        writer.m_build = {};
        protection.m_impl = implementation;
        return true;
    }

    bool PersistentCacheAuthorityAccess::DeleteCanonical(PersistentRootAuthority* const authority, const BuildFingerprint& build,
                                                         const filesystem::AbsolutePath& path) noexcept
    {
        if (authority == nullptr || build.IsEmpty())
            return false;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        registry.lock.Acquire();
        if (!BeginDeleteLocked(*authority, build))
        {
            registry.lock.Release();
            return false;
        }
        registry.lock.Release();
        filesystem::Manager& manager = filesystem::GetManager();
        const bool valid = IsExactMutationPath(*authority, build, path, false);
        const bool removed = valid && (!manager.FileExist(path) || manager.DeleteFile(path));
        registry.lock.Acquire();
        EndDeleteLocked(*authority, build);
        DestroyAuthorityIfUnusedLocked(registry, authority);
        registry.lock.Release();
        return removed;
    }

    PersistentCacheAuthorityAccess::MaintenanceDeleteResult PersistentCacheAuthorityAccess::DeleteCanonicalForMaintenance(
        PersistentRootAuthority* const authority, const void* const protectionDomainOwner, const u64 protectionEpoch,
        const BuildFingerprint& build, const filesystem::AbsolutePath& path,
        const containers::ArraySpan<const ArtifactSetKey> protectedKeys) noexcept
    {
        if (authority == nullptr || protectionDomainOwner == nullptr || protectionEpoch == 0u || build.IsEmpty() || path.Empty())
            return MaintenanceDeleteResult::InvalidPath;

        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        registry.lock.Acquire();
        if (authority->protectionDomainOwner != protectionDomainOwner || authority->protectionEpoch != protectionEpoch)
        {
            registry.lock.Release();
            return MaintenanceDeleteResult::ProtectionChanged;
        }
        if (IsBuildProtected(protectedKeys, build) || !BeginDeleteLocked(*authority, build))
        {
            registry.lock.Release();
            return MaintenanceDeleteResult::Deferred;
        }
        registry.lock.Release();

        filesystem::Manager& manager = filesystem::GetManager();
        const bool valid = IsExactMutationPath(*authority, build, path, false);
        const bool removed = valid && (!manager.FileExist(path) || manager.DeleteFile(path));

        registry.lock.Acquire();
        EndDeleteLocked(*authority, build);
        DestroyAuthorityIfUnusedLocked(registry, authority);
        registry.lock.Release();

        if (!valid)
            return MaintenanceDeleteResult::InvalidPath;
        return removed ? MaintenanceDeleteResult::Removed : MaintenanceDeleteResult::IoFailure;
    }

    bool PersistentCacheAuthorityAccess::DeleteCanonical(PersistentWriterLease& writer, const filesystem::AbsolutePath& path) noexcept
    {
        if (!writer.IsValid())
            return false;
        filesystem::Manager& manager = filesystem::GetManager();
        return IsExactMutationPath(*writer.m_authority, writer.m_build, path, false) &&
               (!manager.FileExist(path) || manager.DeleteFile(path));
    }

    bool PersistentCacheAuthorityAccess::DeleteStage(PersistentWriterLease& writer, const filesystem::AbsolutePath& path) noexcept
    {
        if (!writer.IsValid())
            return false;
        filesystem::Manager& manager = filesystem::GetManager();
        return IsExactMutationPath(*writer.m_authority, writer.m_build, path, true) &&
               (!manager.FileExist(path) || manager.DeleteFile(path));
    }

    bool PersistentCacheAuthorityAccess::RegisterProtectionDomain(PersistentRootAuthority* const authority, const void* const owner) noexcept
    {
        if (authority == nullptr || owner == nullptr)
            return false;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        if (authority->protectionDomainOwner != nullptr)
            return authority->protectionDomainOwner == owner;
        ++authority->protectionDomainGeneration;
        if (authority->protectionDomainGeneration == 0u)
            ++authority->protectionDomainGeneration;
        if (authority->protectionDomainGeneration == 0u)
            return false;
        authority->protectionDomainOwner = owner;
        return true;
    }

    void PersistentCacheAuthorityAccess::UnregisterProtectionDomain(PersistentRootAuthority* const authority, const void* const owner) noexcept
    {
        if (authority == nullptr || owner == nullptr)
            return;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        if (authority->protectionDomainOwner == owner)
            authority->protectionDomainOwner = nullptr;
        DestroyAuthorityIfUnusedLocked(registry, authority);
    }

    bool PersistentCacheAuthorityAccess::MarkDurablePublication(PersistentRootAuthority* const authority, const void* const owner,
                                                                const BuildFingerprint& build, const BuildFingerprint& content,
                                                                BuildOutputProtection& protection) noexcept
    {
        if (authority == nullptr || owner == nullptr || build.IsEmpty() || content.IsEmpty() || !protection.IsValid())
            return false;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        if (authority->protectionDomainOwner != owner || authority->protectionDomainGeneration == 0u || protection.m_impl == nullptr ||
            protection.m_impl->authority != authority || protection.m_impl->build != build)
            return false;
        protection.m_impl->durableContent = content;
        protection.m_impl->durableDomainGeneration = authority->protectionDomainGeneration;
        return true;
    }

    void PersistentCacheAuthorityAccess::AdoptPublication(BuildOutputProtection& protection) noexcept
    {
        if (!protection.IsValid())
            return;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        registry.lock.Acquire();
        if (protection.m_impl != nullptr && protection.m_impl->authority != nullptr)
            ++protection.m_impl->authority->protectionEpoch;
        registry.lock.Release();
        protection.Reset();
    }

    bool PersistentCacheAuthorityAccess::OwnsDurablePublication(PersistentRootAuthority* const authority, const void* const owner,
                                                                const BuildFingerprint& build, const BuildFingerprint& content,
                                                                const BuildOutputProtection& protection) noexcept
    {
        if (authority == nullptr || owner == nullptr || build.IsEmpty() || content.IsEmpty() || !protection.IsValid())
            return false;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        return authority->protectionDomainOwner == owner && authority->protectionDomainGeneration != 0u &&
               protection.m_impl != nullptr && protection.m_impl->authority == authority && protection.m_impl->build == build &&
               protection.m_impl->durableContent == content &&
               protection.m_impl->durableDomainGeneration == authority->protectionDomainGeneration;
    }

    u64 PersistentCacheAuthorityAccess::ProtectionEpoch(PersistentRootAuthority* const authority) noexcept
    {
        if (authority == nullptr)
            return 0u;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        return authority->protectionEpoch;
    }

    filesystem::AbsolutePath PersistentCacheAuthorityAccess::Root(PersistentRootAuthority* const authority) noexcept
    {
        if (authority == nullptr)
            return {};
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        return authority->root;
    }
} // namespace vanguard::assets::detail

namespace vanguard::assets
{
    struct ArtifactSetReader::Impl final
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        Impl() noexcept
            : artifacts(memory::pools::Assets::GetInstance()), payloadOffsets(memory::pools::Assets::GetInstance()),
              lookup(memory::pools::Assets::GetInstance())
        {
        }

        ArtifactSetKey key;
        detail::PersistentRootAuthority* authority = nullptr;
        red::UniquePtr<filesystem::IFile> file;
        containers::DynamicArray<CachedArtifactDescriptor> artifacts;
        containers::DynamicArray<u64> payloadOffsets;
        containers::HashMap<ArtifactIdentity, u32> lookup;
    };

    bool detail::PersistentCacheAuthorityAccess::PromoteReaderToPublication(ArtifactSetReader& reader,
                                                                           BuildOutputProtection& protection) noexcept
    {
        protection.Reset();
        if (reader.m_impl == nullptr || reader.m_impl->authority == nullptr || reader.m_impl->key.build.IsEmpty())
            return false;
        PersistentAuthorityRegistry& registry = AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        PersistentRootAuthority* const authority = reader.m_impl->authority;
        const BuildFingerprint build = reader.m_impl->key.build;
        const u32 index = FindLease(*authority, build);
        if (index >= authority->leases.Size() || authority->leases[index].readers == 0u)
            return false;
        BuildOutputProtection::Impl* const implementation = VANGUARD_NEW(BuildOutputProtection::Impl);
        if (implementation == nullptr)
            return false;
        implementation->authority = authority;
        implementation->build = build;
        --authority->leases[index].readers;
        ++authority->leases[index].publications;
        // The publication lease is the same live lease, transferred from this
        // exact reader. Detach it while the authority is locked so Close() can
        // never release a different same-key reader's aggregate count.
        reader.m_impl->authority = nullptr;
        protection.m_impl = implementation;
        return true;
    }

    struct DerivedDataArtifactSource::Impl final
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        filesystem::AbsolutePath root;
        DerivedDataArtifactLimits limits;
        detail::PersistentRootAuthority* authority = nullptr;
    };

    detail::PersistentRootAuthority* detail::PersistentCacheAuthorityAccess::Authority(const DerivedDataArtifactSource& source) noexcept
    {
        return source.m_impl != nullptr ? source.m_impl->authority : nullptr;
    }

    BuildOutputProtection::BuildOutputProtection(const BuildOutputProtection& other) noexcept
    {
        if (other.m_impl == nullptr)
            return;
        detail::PersistentAuthorityRegistry& registry = detail::AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        ++other.m_impl->references;
        m_impl = other.m_impl;
    }

    BuildOutputProtection& BuildOutputProtection::operator=(const BuildOutputProtection& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        if (other.m_impl == nullptr)
            return *this;
        detail::PersistentAuthorityRegistry& registry = detail::AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        ++other.m_impl->references;
        m_impl = other.m_impl;
        return *this;
    }

    BuildOutputProtection::BuildOutputProtection(BuildOutputProtection&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    BuildOutputProtection& BuildOutputProtection::operator=(BuildOutputProtection&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    BuildOutputProtection::~BuildOutputProtection()
    {
        Reset();
    }

    void BuildOutputProtection::Reset() noexcept
    {
        if (m_impl == nullptr)
            return;
        Impl* const implementation = m_impl;
        m_impl = nullptr;
        detail::PersistentAuthorityRegistry& registry = detail::AuthorityRegistry();
        registry.lock.Acquire();
        if (implementation->references > 1u)
        {
            --implementation->references;
            registry.lock.Release();
            return;
        }
        detail::PersistentRootAuthority* const authority = implementation->authority;
        const BuildFingerprint build = implementation->build;
        if (authority != nullptr)
        {
            const u32 index = detail::FindLease(*authority, build);
            if (index < authority->leases.Size() && authority->leases[index].publications != 0u)
            {
                --authority->leases[index].publications;
                if (authority->liveLeases != 0u)
                    --authority->liveLeases;
            }
            detail::RemoveEmptyLease(*authority, build);
            detail::DestroyAuthorityIfUnusedLocked(registry, authority);
        }
        registry.lock.Release();
        VANGUARD_DELETE(implementation);
    }

    detail::PersistentWriterLease::PersistentWriterLease(PersistentWriterLease&& other) noexcept
        : m_authority(other.m_authority), m_build(other.m_build)
    {
        other.m_authority = nullptr;
        other.m_build = {};
    }

    detail::PersistentWriterLease& detail::PersistentWriterLease::operator=(PersistentWriterLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_authority = other.m_authority;
            m_build = other.m_build;
            other.m_authority = nullptr;
            other.m_build = {};
        }
        return *this;
    }

    detail::PersistentWriterLease::~PersistentWriterLease()
    {
        Reset();
    }

    void detail::PersistentWriterLease::Reset() noexcept
    {
        if (m_authority == nullptr)
            return;
        detail::PersistentAuthorityRegistry& registry = detail::AuthorityRegistry();
        concurrency::ScopedLock guard(registry.lock);
        detail::PersistentRootAuthority* const authority = m_authority;
        const BuildFingerprint build = m_build;
        m_authority = nullptr;
        m_build = {};
        const u32 index = detail::FindLease(*authority, build);
        if (index < authority->leases.Size() && authority->leases[index].writers != 0u)
        {
            authority->leases[index].writers = 0u;
            if (authority->liveLeases != 0u)
                --authority->liveLeases;
        }
        detail::RemoveEmptyLease(*authority, build);
        detail::DestroyAuthorityIfUnusedLocked(registry, authority);
    }

    DerivedDataPackageArtifactReader::DerivedDataPackageArtifactReader(const DerivedDataArtifactSource& source) noexcept
        : m_source(&source)
    {
    }

    void DerivedDataPackageArtifactReader::Reset() noexcept
    {
        m_reader.Close();
        m_openCount = 0;
    }

    bool DerivedDataPackageArtifactReader::Read(const ArtifactSetKey origin, const IndexedArtifact& artifact,
                                                containers::DynamicArray<u8>& bytes) noexcept
    {
        bytes.Clear();
        if (m_source == nullptr || !m_source->IsInitialized() || !origin.IsValid() || origin.content.IsEmpty())
            return false;
        if (!m_reader.IsOpen() || m_reader.Key() != origin)
        {
            m_reader.Close();
            if (m_source->Open(origin, m_reader) != DerivedDataArtifactResult::Success)
                return false;
            ++m_openCount;
        }

        const CachedArtifactDescriptor expected{artifact.resource, artifact.segment, artifact.flags, artifact.alignmentLog2,
                                                artifact.byteCount};
        return m_reader.Read(expected, bytes) == DerivedDataArtifactResult::Success;
    }

    u64 DerivedDataPackageArtifactReader::GetOpenCount() const noexcept
    {
        return m_openCount;
    }

    bool DerivedDataPackageArtifactReader::ReadCallback(const ArtifactSetKey origin, const IndexedArtifact& artifact,
                                                        containers::DynamicArray<u8>& bytes, void* const userData) noexcept
    {
        return userData != nullptr && static_cast<DerivedDataPackageArtifactReader*>(userData)->Read(origin, artifact, bytes);
    }

    const char* ToString(const DerivedDataArtifactResult result) noexcept
    {
        switch (result)
        {
        case DerivedDataArtifactResult::Success: return "Success";
        case DerivedDataArtifactResult::NotFound: return "NotFound";
        case DerivedDataArtifactResult::InvalidArgument: return "InvalidArgument";
        case DerivedDataArtifactResult::InvalidState: return "InvalidState";
        case DerivedDataArtifactResult::ContentMismatch: return "ContentMismatch";
        case DerivedDataArtifactResult::DescriptorMismatch: return "DescriptorMismatch";
        case DerivedDataArtifactResult::Corrupt: return "Corrupt";
        case DerivedDataArtifactResult::IoFailure: return "IoFailure";
        case DerivedDataArtifactResult::OutOfMemory: return "OutOfMemory";
        case DerivedDataArtifactResult::LimitExceeded: return "LimitExceeded";
        }
        return "Unknown";
    }

    ArtifactSetReader::~ArtifactSetReader()
    {
        Close();
    }

    void ArtifactSetReader::Close() noexcept
    {
        if (m_impl != nullptr)
        {
            detail::PersistentRootAuthority* const authority = m_impl->authority;
            const BuildFingerprint build = m_impl->key.build;
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            if (authority != nullptr)
                detail::PersistentCacheAuthorityAccess::ReleaseReader(authority, build);
        }
    }

    bool ArtifactSetReader::IsOpen() const noexcept
    {
        return m_impl != nullptr;
    }

    ArtifactSetKey ArtifactSetReader::Key() const noexcept
    {
        return m_impl != nullptr ? m_impl->key : ArtifactSetKey{};
    }

    containers::ArraySpan<const CachedArtifactDescriptor> ArtifactSetReader::Artifacts() const noexcept
    {
        return m_impl != nullptr ? containers::ArraySpan<const CachedArtifactDescriptor>{m_impl->artifacts.TypedData(), m_impl->artifacts.Size()}
                                 : containers::ArraySpan<const CachedArtifactDescriptor>{};
    }

    const CachedArtifactDescriptor* ArtifactSetReader::Find(const resources::ResourceReference resource,
                                                            const u32 segment) const noexcept
    {
        if (m_impl == nullptr)
            return nullptr;
        const ArtifactIdentity identity{resource.GetPath().Id(), resource.ExpectedType(), segment};
        const u32* const index = m_impl->lookup.FindPtr(identity);
        return index != nullptr && *index < m_impl->artifacts.Size() ? &m_impl->artifacts[*index] : nullptr;
    }

    DerivedDataArtifactResult ArtifactSetReader::Read(const CachedArtifactDescriptor& expected,
                                                      containers::DynamicArray<u8>& bytes) noexcept
    {
        bytes.Clear();
        if (m_impl == nullptr)
            return DerivedDataArtifactResult::InvalidState;
        const CachedArtifactDescriptor* const stored = Find(expected.resource, expected.segment);
        if (stored == nullptr || stored->flags != expected.flags || stored->alignmentLog2 != expected.alignmentLog2 ||
            stored->byteCount != expected.byteCount)
            return DerivedDataArtifactResult::DescriptorMismatch;
        const u32* const index = m_impl->lookup.FindPtr(Identity(*stored));
        if (index == nullptr || *index >= m_impl->payloadOffsets.Size() || stored->byteCount > std::numeric_limits<u32>::max())
            return DerivedDataArtifactResult::Corrupt;
        bytes.Resize(static_cast<u32>(stored->byteCount));
        if (bytes.Size() != stored->byteCount)
            return DerivedDataArtifactResult::OutOfMemory;
        serialization::BinaryReader reader(*m_impl->file);
        if (!reader.Seek(m_impl->payloadOffsets[*index]) || !reader.ReadBytes(bytes.Data(), bytes.Size()))
        {
            bytes.Clear();
            return DerivedDataArtifactResult::IoFailure;
        }
        return DerivedDataArtifactResult::Success;
    }

    DerivedDataArtifactResult ArtifactSetReader::CopyTo(const CachedArtifactDescriptor& expected,
                                                        filesystem::IFile& output,
                                                        const containers::ArraySpan<u8> scratch,
                                                        crypto::Sha256Builder* const contentHash) noexcept
    {
        if (m_impl == nullptr)
            return DerivedDataArtifactResult::InvalidState;
        if (scratch.Empty())
            return DerivedDataArtifactResult::InvalidArgument;
        const CachedArtifactDescriptor* const stored = Find(expected.resource, expected.segment);
        if (stored == nullptr || stored->flags != expected.flags || stored->alignmentLog2 != expected.alignmentLog2 ||
            stored->byteCount != expected.byteCount)
            return DerivedDataArtifactResult::DescriptorMismatch;
        const u32* const index = m_impl->lookup.FindPtr(Identity(*stored));
        if (index == nullptr || *index >= m_impl->payloadOffsets.Size())
            return DerivedDataArtifactResult::Corrupt;

        serialization::BinaryReader reader(*m_impl->file);
        serialization::BinaryWriter writer(output);
        if (!reader.Seek(m_impl->payloadOffsets[*index]))
            return DerivedDataArtifactResult::IoFailure;
        u64 remaining = stored->byteCount;
        while (remaining != 0)
        {
            const u32 chunk = static_cast<u32>(remaining < scratch.Count() ? remaining : scratch.Count());
            if (!reader.ReadBytes(scratch.Data(), chunk) || !writer.WriteBytes(scratch.Data(), chunk) ||
                (contentHash != nullptr && !contentHash->Update(scratch.Data(), chunk)))
                return DerivedDataArtifactResult::IoFailure;
            remaining -= chunk;
        }
        return DerivedDataArtifactResult::Success;
    }

    DerivedDataArtifactResult ArtifactSetReader::Hash(const CachedArtifactDescriptor& expected,
                                                      const containers::ArraySpan<u8> scratch,
                                                      crypto::Sha256Builder& contentHash) noexcept
    {
        if (m_impl == nullptr)
            return DerivedDataArtifactResult::InvalidState;
        if (scratch.Empty())
            return DerivedDataArtifactResult::InvalidArgument;
        const CachedArtifactDescriptor* const stored = Find(expected.resource, expected.segment);
        if (stored == nullptr || stored->flags != expected.flags || stored->alignmentLog2 != expected.alignmentLog2 ||
            stored->byteCount != expected.byteCount)
            return DerivedDataArtifactResult::DescriptorMismatch;
        const u32* const index = m_impl->lookup.FindPtr(Identity(*stored));
        if (index == nullptr || *index >= m_impl->payloadOffsets.Size())
            return DerivedDataArtifactResult::Corrupt;

        serialization::BinaryReader reader(*m_impl->file);
        if (!reader.Seek(m_impl->payloadOffsets[*index]))
            return DerivedDataArtifactResult::IoFailure;
        u64 remaining = stored->byteCount;
        while (remaining != 0)
        {
            const u32 chunk = static_cast<u32>(remaining < scratch.Count() ? remaining : scratch.Count());
            if (!reader.ReadBytes(scratch.Data(), chunk) || !contentHash.Update(scratch.Data(), chunk))
                return DerivedDataArtifactResult::IoFailure;
            remaining -= chunk;
        }
        return DerivedDataArtifactResult::Success;
    }

    DerivedDataArtifactResult ArtifactSetReader::ReadAll(BuildOutput& output) noexcept
    {
        output.Reset();
        if (m_impl == nullptr)
            return DerivedDataArtifactResult::InvalidState;
        output.artifacts.Resize(m_impl->artifacts.Size());
        if (output.artifacts.Size() != m_impl->artifacts.Size())
            return DerivedDataArtifactResult::OutOfMemory;
        serialization::BinaryReader reader(*m_impl->file);
        if (m_impl->payloadOffsets.Empty() || !reader.Seek(m_impl->payloadOffsets[0]))
        {
            output.Reset();
            return DerivedDataArtifactResult::IoFailure;
        }
        for (u32 index = 0; index < m_impl->artifacts.Size(); ++index)
        {
            const CachedArtifactDescriptor& descriptor = m_impl->artifacts[index];
            if (descriptor.byteCount > std::numeric_limits<u32>::max())
            {
                output.Reset();
                return DerivedDataArtifactResult::LimitExceeded;
            }
            Artifact& artifact = output.artifacts[index];
            artifact.resource = descriptor.resource;
            artifact.segment = descriptor.segment;
            artifact.flags = descriptor.flags;
            artifact.alignmentLog2 = descriptor.alignmentLog2;
            artifact.bytes.Resize(static_cast<u32>(descriptor.byteCount));
            const bool allocated = artifact.bytes.Size() == descriptor.byteCount;
            if (!allocated || !reader.ReadBytes(artifact.bytes.Data(), artifact.bytes.Size()))
            {
                const DerivedDataArtifactResult result = allocated ? DerivedDataArtifactResult::IoFailure
                                                                  : DerivedDataArtifactResult::OutOfMemory;
                output.Reset();
                return result;
            }
        }
        output.disposition = BuildDisposition::CacheHit;
        output.buildFingerprint = m_impl->key.build;
        output.contentFingerprint = m_impl->key.content;
        return DerivedDataArtifactResult::Success;
    }

    DerivedDataArtifactSource::~DerivedDataArtifactSource()
    {
        static_cast<void>(Shutdown());
    }

    bool DerivedDataArtifactSource::Initialize(const DerivedDataArtifactSourceConfig& config) noexcept
    {
        if (m_impl != nullptr || config.root == nullptr || config.root[0] == '\0' || !config.limits.IsValid() || !config.coordination.IsValid())
            return false;
        Impl* const implementation = VANGUARD_NEW(Impl);
        if (implementation == nullptr)
            return false;
        implementation->root = filesystem::AbsolutePath::ParseDirPath(config.root);
        implementation->limits = config.limits;
        if (implementation->root.Empty() ||
            !detail::PersistentCacheAuthorityAccess::Join(implementation->root, config.coordination, implementation->authority))
        {
            VANGUARD_DELETE(implementation);
            return false;
        }
        m_impl = implementation;
        return true;
    }

    bool DerivedDataArtifactSource::Shutdown() noexcept
    {
        if (m_impl != nullptr)
        {
            detail::PersistentCacheAuthorityAccess::Leave(m_impl->authority);
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
        }
        return true;
    }

    bool DerivedDataArtifactSource::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    namespace
    {
    [[nodiscard]] DerivedDataArtifactResult OpenPersistentArtifactSet(const filesystem::AbsolutePath& path,
                                                                      const ArtifactSetKey key,
                                                                      const DerivedDataArtifactLimits& limits,
                                                                      ArtifactSetReader::Impl*& output) noexcept
    {
        output = nullptr;
        filesystem::Manager& manager = filesystem::GetManager();
        if (!manager.FileExist(path))
            return DerivedDataArtifactResult::NotFound;
        red::UniquePtr<filesystem::IFile> file = manager.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!file)
            return DerivedDataArtifactResult::IoFailure;

        serialization::BinaryReader reader(*file);
        u32 magic = 0;
        u16 major = 0;
        u16 minor = 0;
        u32 headerSize = 0;
        u32 artifactCount = 0;
        u64 fileSize = 0;
        BuildFingerprint build;
        BuildFingerprint content;
        u64 payloadBytes = 0;
        if (!reader.ReadU32(magic) || !reader.ReadU16(major) || !reader.ReadU16(minor) || !reader.ReadU32(headerSize) ||
            !reader.ReadU32(artifactCount) || !reader.ReadU64(fileSize) ||
            !reader.ReadBytes(build.bytes, BuildFingerprint::ByteCount) ||
            !reader.ReadBytes(content.bytes, BuildFingerprint::ByteCount) || !reader.ReadU64(payloadBytes))
            return DerivedDataArtifactResult::Corrupt;
        if (magic != detail::PersistentCacheMagic || major != detail::PersistentCacheMajorVersion ||
            minor > detail::PersistentCacheMinorVersion || headerSize != detail::PersistentCacheHeaderSize ||
            build != key.build || artifactCount == 0 || fileSize != reader.Size())
            return DerivedDataArtifactResult::Corrupt;
        if (!key.content.IsEmpty() && content != key.content)
            return DerivedDataArtifactResult::ContentMismatch;
        if (artifactCount > limits.maximumArtifacts || payloadBytes > limits.maximumArtifactBytes)
            return DerivedDataArtifactResult::LimitExceeded;
        const u64 descriptorBytes = static_cast<u64>(artifactCount) * detail::PersistentArtifactDescriptorSize;
        if (fileSize < detail::PersistentCacheHeaderSize || descriptorBytes > fileSize - detail::PersistentCacheHeaderSize ||
            payloadBytes != fileSize - detail::PersistentCacheHeaderSize - descriptorBytes)
            return DerivedDataArtifactResult::Corrupt;

        ArtifactSetReader::Impl* const opened = VANGUARD_NEW(ArtifactSetReader::Impl);
        if (opened == nullptr)
            return DerivedDataArtifactResult::OutOfMemory;
        opened->artifacts.Resize(artifactCount);
        opened->payloadOffsets.Resize(artifactCount);
        opened->lookup.Reserve(artifactCount);
        if (opened->artifacts.Size() != artifactCount || opened->payloadOffsets.Size() != artifactCount)
        {
            VANGUARD_DELETE(opened);
            return DerivedDataArtifactResult::OutOfMemory;
        }

        bool hasPrimary = false;
        u64 describedPayloadBytes = 0;
        for (u32 index = 0; index < artifactCount; ++index)
        {
            u64 pathIdentity = 0;
            u32 typeIdentity = 0;
            u16 flags = 0;
            u8 reserved8 = 0;
            u32 reserved32 = 0;
            CachedArtifactDescriptor& descriptor = opened->artifacts[index];
            if (!reader.ReadU64(pathIdentity) || !reader.ReadU32(typeIdentity) || !reader.ReadU32(descriptor.segment) ||
                !reader.ReadU16(flags) || !reader.ReadU8(descriptor.alignmentLog2) || !reader.ReadU8(reserved8) ||
                !reader.ReadU64(descriptor.byteCount) || !reader.ReadU32(reserved32) ||
                pathIdentity == resources::InvalidResourceId || typeIdentity == resources::InvalidResourceTypeId ||
                descriptor.byteCount == 0 || descriptor.alignmentLog2 > 20 || reserved8 != 0 || reserved32 != 0 ||
                (flags & ~KnownArtifactFlags) != 0 || descriptor.byteCount > payloadBytes - describedPayloadBytes)
            {
                VANGUARD_DELETE(opened);
                return DerivedDataArtifactResult::Corrupt;
            }
            descriptor.resource = resources::ResourceReference(resources::ResourcePath::FromId(pathIdentity), typeIdentity);
            descriptor.flags = static_cast<ArtifactFlags>(flags);
            const ArtifactIdentity identity = Identity(descriptor);
            if (opened->lookup.KeyExist(identity))
            {
                VANGUARD_DELETE(opened);
                return DerivedDataArtifactResult::Corrupt;
            }
            if (!opened->lookup.Insert(identity, index).IsSuccessful())
            {
                VANGUARD_DELETE(opened);
                return DerivedDataArtifactResult::OutOfMemory;
            }
            opened->payloadOffsets[index] = detail::PersistentCacheHeaderSize + descriptorBytes + describedPayloadBytes;
            describedPayloadBytes += descriptor.byteCount;
            hasPrimary = hasPrimary || HasFlag(descriptor.flags, ArtifactFlags::Primary);
        }
        if (!hasPrimary || describedPayloadBytes != payloadBytes)
        {
            VANGUARD_DELETE(opened);
            return DerivedDataArtifactResult::Corrupt;
        }

        containers::DynamicArray<u8> scratch(memory::pools::Assets::GetInstance());
        const u32 scratchBytes = static_cast<u32>(payloadBytes < limits.validationScratchBytes
                                                      ? payloadBytes
                                                      : limits.validationScratchBytes);
        scratch.Resize(scratchBytes);
        if (scratch.Size() != scratchBytes)
        {
            VANGUARD_DELETE(opened);
            return DerivedDataArtifactResult::OutOfMemory;
        }
        detail::ArtifactSetFingerprintBuilder fingerprint(artifactCount);
        for (const CachedArtifactDescriptor& descriptor : opened->artifacts)
        {
            if (!fingerprint.AddDescriptor(descriptor.resource, descriptor.segment, descriptor.flags,
                                           descriptor.alignmentLog2, descriptor.byteCount))
            {
                VANGUARD_DELETE(opened);
                return DerivedDataArtifactResult::Corrupt;
            }
            u64 remaining = descriptor.byteCount;
            while (remaining != 0)
            {
                const u32 chunk = static_cast<u32>(remaining < scratch.Size() ? remaining : scratch.Size());
                if (chunk == 0 || !reader.ReadBytes(scratch.Data(), chunk) || !fingerprint.AddBytes(scratch.Data(), chunk))
                {
                    VANGUARD_DELETE(opened);
                    return DerivedDataArtifactResult::Corrupt;
                }
                remaining -= chunk;
            }
        }
        BuildFingerprint verified;
        if (reader.Position() != fileSize || !fingerprint.Finalize(verified) || verified != content)
        {
            VANGUARD_DELETE(opened);
            return DerivedDataArtifactResult::Corrupt;
        }

        opened->key = {build, content};
        opened->file = static_cast<red::UniquePtr<filesystem::IFile>&&>(file);
        output = opened;
        return DerivedDataArtifactResult::Success;
    }
    } // namespace

    DerivedDataArtifactResult DerivedDataArtifactSource::Open(const ArtifactSetKey key,
                                                              ArtifactSetReader& output) const noexcept
    {
        output.Close();
        if (m_impl == nullptr)
            return DerivedDataArtifactResult::InvalidState;
        if (!key.IsValid())
            return DerivedDataArtifactResult::InvalidArgument;

        const DerivedDataArtifactResult acquired = detail::PersistentCacheAuthorityAccess::AcquireReader(m_impl->authority, key.build);
        if (acquired != DerivedDataArtifactResult::Success)
            return acquired;
        ArtifactSetReader::Impl* opened = nullptr;
        const DerivedDataArtifactResult result = OpenPersistentArtifactSet(
            detail::PersistentRecordPath(m_impl->root, key.build), key, m_impl->limits, opened);
        if (result == DerivedDataArtifactResult::Success)
        {
            opened->authority = m_impl->authority;
            output.m_impl = opened;
        }
        else
            detail::PersistentCacheAuthorityAccess::ReleaseReader(m_impl->authority, key.build);
        return result;
    }
} // namespace vanguard::assets

namespace vanguard::assets::detail
{
    DerivedDataArtifactResult ValidatePersistentRecord(const filesystem::AbsolutePath& path, const ArtifactSetKey key,
                                                       const DerivedDataArtifactLimits limits) noexcept
    {
        if (path.Empty() || !key.IsValid() || !limits.IsValid())
            return DerivedDataArtifactResult::InvalidArgument;
        ArtifactSetReader::Impl* opened = nullptr;
        const DerivedDataArtifactResult result = OpenPersistentArtifactSet(path, key, limits, opened);
        if (opened != nullptr)
            VANGUARD_DELETE(opened);
        return result;
    }
} // namespace vanguard::assets::detail

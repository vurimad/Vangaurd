#pragma once

#include <vanguard/assets/derived_data_artifact_source.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::assets::detail
{
    inline constexpr u32 PersistentCacheMagic = serialization::MakeFourCC('V', 'D', 'D', 'C');
    inline constexpr u16 PersistentCacheMajorVersion = 1;
    inline constexpr u16 PersistentCacheMinorVersion = 0;
    inline constexpr u32 PersistentCacheHeaderSize = 96;
    inline constexpr u32 PersistentArtifactDescriptorSize = 32;

    [[nodiscard]] filesystem::AbsolutePath PersistentRecordDirectory(const filesystem::AbsolutePath& root,
                                                                     const BuildFingerprint& fingerprint) noexcept;
    [[nodiscard]] filesystem::AbsolutePath PersistentRecordPath(const filesystem::AbsolutePath& root,
                                                                const BuildFingerprint& fingerprint) noexcept;
    [[nodiscard]] filesystem::AbsolutePath PersistentRecordStagePath(const filesystem::AbsolutePath& root,
                                                                     const BuildFingerprint& fingerprint) noexcept;
    [[nodiscard]] bool ParsePersistentRecordPath(const filesystem::AbsolutePath& root,
                                                 const filesystem::AbsolutePath& path,
                                                 BuildFingerprint& fingerprint) noexcept;
    [[nodiscard]] bool ParsePersistentRecordStagePath(const filesystem::AbsolutePath& root,
                                                      const filesystem::AbsolutePath& path,
                                                      BuildFingerprint& fingerprint) noexcept;
    [[nodiscard]] DerivedDataArtifactResult ValidatePersistentRecord(const filesystem::AbsolutePath& path,
                                                                     ArtifactSetKey key,
                                                                     DerivedDataArtifactLimits limits) noexcept;

    struct PersistentRootAuthority;

    class PersistentWriterLease final
    {
    public:
        PersistentWriterLease() noexcept = default;
        PersistentWriterLease(PersistentWriterLease&& other) noexcept;
        PersistentWriterLease& operator=(PersistentWriterLease&& other) noexcept;
        ~PersistentWriterLease();

        PersistentWriterLease(const PersistentWriterLease&) = delete;
        PersistentWriterLease& operator=(const PersistentWriterLease&) = delete;

        [[nodiscard]] bool IsValid() const noexcept { return m_authority != nullptr; }
        void Reset() noexcept;

    private:
        PersistentRootAuthority* m_authority = nullptr;
        BuildFingerprint m_build;

        friend class PersistentCacheAuthorityAccess;
    };

    class PersistentCacheAuthorityAccess final
    {
    public:
        [[nodiscard]] static bool Join(const filesystem::AbsolutePath& root,
                                       const PersistentCacheCoordinationLimits& limits,
                                       PersistentRootAuthority*& authority) noexcept;
        static void Leave(PersistentRootAuthority*& authority) noexcept;

        [[nodiscard]] static DerivedDataArtifactResult AcquireReader(PersistentRootAuthority* authority,
                                                                     const BuildFingerprint& build) noexcept;
        static void ReleaseReader(PersistentRootAuthority* authority, const BuildFingerprint& build) noexcept;
        [[nodiscard]] static bool PromoteReaderToPublication(ArtifactSetReader& reader,
                                                             BuildOutputProtection& protection) noexcept;

        [[nodiscard]] static DerivedDataArtifactResult AcquireWriter(PersistentRootAuthority* authority,
                                                                     const BuildFingerprint& build,
                                                                     PersistentWriterLease& lease) noexcept;
        [[nodiscard]] static bool PromoteWriterToPublication(PersistentWriterLease& writer,
                                                             BuildOutputProtection& protection) noexcept;
        [[nodiscard]] static bool AcquirePublication(PersistentRootAuthority* authority,
                                                     const BuildFingerprint& build,
                                                     BuildOutputProtection& protection) noexcept;
        [[nodiscard]] static bool DeleteCanonical(PersistentRootAuthority* authority,
                                                  const BuildFingerprint& build,
                                                  const filesystem::AbsolutePath& path) noexcept;
        enum class MaintenanceDeleteResult : u8
        {
            Removed,
            Deferred,
            ProtectionChanged,
            InvalidPath,
            IoFailure
        };
        [[nodiscard]] static MaintenanceDeleteResult DeleteCanonicalForMaintenance(
            PersistentRootAuthority* authority, const void* protectionDomainOwner, u64 protectionEpoch,
            const BuildFingerprint& build, const filesystem::AbsolutePath& path,
            containers::ArraySpan<const ArtifactSetKey> protectedKeys) noexcept;
        [[nodiscard]] static bool DeleteCanonical(PersistentWriterLease& writer,
                                                  const filesystem::AbsolutePath& path) noexcept;
        [[nodiscard]] static bool DeleteStage(PersistentWriterLease& writer,
                                              const filesystem::AbsolutePath& path) noexcept;

        [[nodiscard]] static bool RegisterProtectionDomain(PersistentRootAuthority* authority,
                                                           const void* owner) noexcept;
        static void UnregisterProtectionDomain(PersistentRootAuthority* authority, const void* owner) noexcept;
        [[nodiscard]] static bool MarkDurablePublication(PersistentRootAuthority* authority, const void* owner,
                                                         const BuildFingerprint& build, const BuildFingerprint& content,
                                                         BuildOutputProtection& protection) noexcept;
        static void AdoptPublication(BuildOutputProtection& protection) noexcept;
        [[nodiscard]] static bool OwnsDurablePublication(PersistentRootAuthority* authority, const void* owner,
                                                         const BuildFingerprint& build, const BuildFingerprint& content,
                                                         const BuildOutputProtection& protection) noexcept;
        [[nodiscard]] static u64 ProtectionEpoch(PersistentRootAuthority* authority) noexcept;
        [[nodiscard]] static PersistentRootAuthority* Authority(const DerivedDataArtifactSource& source) noexcept;

        [[nodiscard]] static filesystem::AbsolutePath Root(PersistentRootAuthority* authority) noexcept;
    };

    class ArtifactSetFingerprintBuilder final
    {
    public:
        explicit ArtifactSetFingerprintBuilder(u32 artifactCount) noexcept;

        [[nodiscard]] bool AddDescriptor(resources::ResourceReference resource, u32 segment, ArtifactFlags flags,
                                         u8 alignmentLog2, u64 byteCount) noexcept;
        [[nodiscard]] bool AddBytes(const void* bytes, usize size) noexcept;
        [[nodiscard]] bool Finalize(BuildFingerprint& fingerprint) noexcept;

    private:
        crypto::Sha256Builder m_hash;
        bool m_valid = true;
    };
} // namespace vanguard::assets::detail

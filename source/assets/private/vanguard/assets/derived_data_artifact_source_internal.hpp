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
    [[nodiscard]] DerivedDataArtifactResult ValidatePersistentRecord(const filesystem::AbsolutePath& path,
                                                                     ArtifactSetKey key,
                                                                     DerivedDataArtifactLimits limits) noexcept;

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

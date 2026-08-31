#pragma once

#include <vanguard/assets/package_planner.hpp>

namespace vanguard::assets
{
    /// Adapts immutable VDDC artifact sets to PackageAssembler's segment read
    /// callback. The current set remains open while consecutive resources or
    /// segments use the same origin key.
    class DerivedDataPackageArtifactReader final
    {
    public:
        explicit DerivedDataPackageArtifactReader(const DerivedDataArtifactSource& source) noexcept;

        DerivedDataPackageArtifactReader(const DerivedDataPackageArtifactReader&) = delete;
        DerivedDataPackageArtifactReader& operator=(const DerivedDataPackageArtifactReader&) = delete;

        void Reset() noexcept;
        [[nodiscard]] bool Read(ArtifactSetKey origin, const IndexedArtifact& artifact,
                                containers::DynamicArray<u8>& bytes) noexcept;
        [[nodiscard]] u64 GetOpenCount() const noexcept;

        [[nodiscard]] static bool ReadCallback(ArtifactSetKey origin, const IndexedArtifact& artifact,
                                               containers::DynamicArray<u8>& bytes, void* userData) noexcept;

    private:
        const DerivedDataArtifactSource* m_source = nullptr;
        ArtifactSetReader m_reader;
        u64 m_openCount = 0;
    };
} // namespace vanguard::assets

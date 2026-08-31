#pragma once

#include <vanguard/assets/asset_index.hpp>
#include <vanguard/assets/derived_data_artifact_source.hpp>

namespace vanguard::assets
{
    enum class LooseMaterializationResult : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        ResourceNotFound,
        MissingSegment,
        DuplicateSegment,
        DescriptorMismatch,
        ArtifactNotFound,
        CorruptArtifactSet,
        ValidationFailed,
        PublicationFailed,
        IoFailure,
        OutOfMemory,
        LimitExceeded
    };

    [[nodiscard]] const char* ToString(LooseMaterializationResult result) noexcept;

    struct LooseMaterializationLimits
    {
        u32 maximumSegments = 4096;
        u64 maximumResourceBytes = 2ull * 1024ull * 1024ull * 1024ull;
        u32 scratchBytes = 1024u * 1024u;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumSegments != 0 && maximumResourceBytes != 0 && scratchBytes != 0;
        }
    };

    class LooseResourceMaterializer final
    {
    public:
        // Reconstructs one logical resource from the artifacts described by
        // record. Target and temporary must be sibling file paths. The previous
        // target is preserved until the completed temporary passes byte-exact
        // size and digest verification.
        [[nodiscard]] LooseMaterializationResult Materialize(const DependencyRecord& record,
                                                              resources::ResourceReference resource,
                                                              const DerivedDataArtifactSource& source,
                                                              const filesystem::AbsolutePath& target,
                                                              const filesystem::AbsolutePath& temporary,
                                                              const LooseMaterializationLimits& limits = {}) const noexcept;
    };
} // namespace vanguard::assets

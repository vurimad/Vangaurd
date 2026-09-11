#pragma once

#include <vanguard/rendering/material_program_layout.hpp>

namespace vanguard::rendering::detail
{
    /// Internal canonical registration seam used by the public shader-object
    /// path and focused collision/capacity proofs. Production callers register
    /// only validated ShaderResourceObject instances.
    struct MaterialProgramLayoutCanonical
    {
        crypto::Digest256 layoutFingerprint;
        crypto::Digest256 domainFingerprint;
        shaders::MaterialDomainContract domain;
        u32 accessorAbiVersion = 0;
        u32 parameterByteSize = 0;
        containers::ArraySpan<const shaders::ConstantMember> parameters;
        containers::ArraySpan<const shaders::MaterialResourceRole> resources;
    };

    struct MaterialProgramLayoutRegistryAccess
    {
        [[nodiscard]] static bool RegisterCanonical(MaterialProgramLayoutRegistry& registry,
                                                    const MaterialProgramLayoutCanonical& canonical,
                                                    MaterialProgramLayoutId& layout,
                                                    MaterialProgramLayoutFailure* failure = nullptr) noexcept;
    };
} // namespace vanguard::rendering::detail

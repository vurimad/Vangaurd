#pragma once

#include <vanguard/rendering/material_program_layout.hpp>

namespace vanguard::rendering::detail
{
    /// Internal canonical registration path shared by shader-object registration
    /// and canonical layout validation. Public callers register
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

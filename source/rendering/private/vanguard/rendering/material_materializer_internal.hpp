#pragma once

#include <vanguard/rendering/material_materializer.hpp>

namespace vanguard::rendering::detail
{
    /// Private proof seam for the post-resolution half of the transaction. The
    /// production path reaches the same operation state through Begin/Poll.
    struct MaterialMaterializerAccess
    {
        [[nodiscard]] static bool BeginResolved(MaterialMaterializer& materializer, MaterialProgramLayoutId layout, containers::ArraySpan<const u8> parameterBytes,
                                                containers::ArraySpan<MaterialResourceReference> references, MaterialMaterializationTicket& ticket, MaterialMaterializerFailure* failure = nullptr) noexcept;
    };
} // namespace vanguard::rendering::detail

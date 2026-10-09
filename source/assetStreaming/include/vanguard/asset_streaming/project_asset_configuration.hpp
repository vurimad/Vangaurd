#pragma once

#include <vanguard/asset_streaming/project_asset_pipeline.hpp>

namespace vanguard::asset_streaming
{
    // Pure frontend-neutral derivation for the shared project-asset backend.
    // The caller selects one project target before entering this helper. No I/O
    // is performed and `config` remains unchanged on failure.
    [[nodiscard]] bool MakeProjectAssetPipelineConfig(const filesystem::AbsolutePath& derivedDataRoot,
                                                      const filesystem::AbsolutePath& intermediateRoot,
                                                      containers::StringView target,
                                                      ProjectAssetPipelineConfig& config) noexcept;
} // namespace vanguard::asset_streaming

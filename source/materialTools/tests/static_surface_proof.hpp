#pragma once

#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/pipelines/pipelines.hpp>

namespace vanguard::material_tools::tests
{
    // Optional native verification used by engine tests; materialToolsTests
    // remains independent of the RHI and runs the same offline fixture alone.
    using StaticSurfaceNativeProof = bool (*)(const shaders::ShaderFile&, const pipelines::PipelineFile&, void*);
    bool RunStaticSurfaceProof(const filesystem::AbsolutePath& working, StaticSurfaceNativeProof nativeProof = nullptr, void* context = nullptr);
} // namespace vanguard::material_tools::tests

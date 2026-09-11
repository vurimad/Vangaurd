#pragma once

namespace vanguard::rendering
{
    // Cook recipes and startup coverage for the existing geometry renderer.
    // Resource locations are assigned by the cooker/catalog, never by runtime.
    struct RendererFeatureProgram
    {
        const char* name;
        const char* source;
        const char* vertex;
        const char* fragment;
        const char* compute;
        bool frameOutput = false;
        bool optional = false;
    };

    inline constexpr RendererFeatureProgram RendererFeaturePrograms[]{
        {"InitializeGpuSceneGeometryShellRanges", "gpu_scene_visibility.vsl", nullptr, nullptr, "InitializeGpuSceneGeometryShellRanges"},
        {"CullGpuSceneCandidates", "gpu_scene_visibility.vsl", nullptr, nullptr, "CullGpuSceneCandidates"},
        {"CountGpuSceneGeometryWork", "gpu_scene_visibility.vsl", nullptr, nullptr, "CountGpuSceneGeometryWork"},
        {"ScanGpuSceneGeometryWorkBlocks", "gpu_scene_visibility.vsl", nullptr, nullptr, "ScanGpuSceneGeometryWorkBlocks"},
        {"PrefixGpuSceneGeometryWorkBlocks", "gpu_scene_visibility.vsl", nullptr, nullptr, "PrefixGpuSceneGeometryWorkBlocks"},
        {"ResolveGpuSceneGeometryWorkOffsets", "gpu_scene_visibility.vsl", nullptr, nullptr, "ResolveGpuSceneGeometryWorkOffsets"},
        {"ScatterGpuSceneGeometryWork", "gpu_scene_visibility.vsl", nullptr, nullptr, "ScatterGpuSceneGeometryWork"},
        {"CountGpuSceneGeometryBins", "gpu_scene_visibility.vsl", nullptr, nullptr, "CountGpuSceneGeometryBins"},
        {"ScanGpuSceneGeometryBinBlocks", "gpu_scene_visibility.vsl", nullptr, nullptr, "ScanGpuSceneGeometryBinBlocks"},
        {"PrefixGpuSceneGeometryBinBlocks", "gpu_scene_visibility.vsl", nullptr, nullptr, "PrefixGpuSceneGeometryBinBlocks"},
        {"ResolveGpuSceneGeometryBinOffsets", "gpu_scene_visibility.vsl", nullptr, nullptr, "ResolveGpuSceneGeometryBinOffsets"},
        {"ScatterGpuSceneGeometryInstances", "gpu_scene_visibility.vsl", nullptr, nullptr, "ScatterGpuSceneGeometryInstances"},
        {"BuildGpuSceneGeometryIndirectArguments", "gpu_scene_visibility.vsl", nullptr, nullptr, "BuildGpuSceneGeometryIndirectArguments"},
        {"VisualizeGBuffer", "geometry_visualization.vsl", "GeometryVisualizationVertexMain", "GeometryVisualizationFragmentMain", nullptr},
        {"DirectionalDiffuse", "directional_diffuse.vsl", "DirectionalDiffuseVertexMain", "DirectionalDiffuseFragmentMain", nullptr},
        {"ResolveCameraOutput", "geometry_visualization.vsl", "GeometryVisualizationVertexMain", "GeometryVisualizationFragmentMain", nullptr, true},
        {"GeometryDiagnostics", "geometry_diagnostics.vsl", nullptr, nullptr, "GeometryDiagnosticsMain", false, true}
    };
}

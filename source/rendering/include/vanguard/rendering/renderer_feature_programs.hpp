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
        const char* define = nullptr;
        bool frameOutput = false;
        bool optional = false;
    };

    inline constexpr RendererFeatureProgram RendererFeaturePrograms[]{
        {"InitializeGpuSceneGeometryShellRanges", "gpu_scene_visibility.vsl", nullptr, nullptr, "InitializeGpuSceneGeometryShellRanges", "VG_GPU_SCENE_INITIALIZE"},
        {"CullGpuSceneInstances", "gpu_scene_visibility.vsl", nullptr, nullptr, "CullGpuSceneInstances", "VG_GPU_SCENE_VISIBILITY"},
        {"ExpandAppendGpuSceneGeometry", "gpu_scene_visibility.vsl", nullptr, nullptr, "ExpandAppendGpuSceneGeometry", "VG_GPU_SCENE_SPARSE_EXPAND"},
        {"AllocateGpuSceneGeometryBins", "gpu_scene_visibility.vsl", nullptr, nullptr, "AllocateGpuSceneGeometryBins", "VG_GPU_SCENE_SPARSE_ALLOCATE"},
        {"ScatterGpuSceneGeometryInstances", "gpu_scene_visibility.vsl", nullptr, nullptr, "ScatterGpuSceneGeometryInstances", "VG_GPU_SCENE_SPARSE_SCATTER"},
        {"VisualizeGBuffer", "geometry_visualization.vsl", "GeometryVisualizationVertexMain", "GeometryVisualizationFragmentMain", nullptr},
        {"DirectionalDiffuse", "directional_diffuse.vsl", "DirectionalDiffuseVertexMain", "DirectionalDiffuseFragmentMain", nullptr},
        {"ResolveCameraOutput", "geometry_visualization.vsl", "GeometryVisualizationVertexMain", "ResolveCameraOutputSceneLinearFragmentMain", nullptr, nullptr, true},
        {"ResolveCameraOutputDisplaySrgb", "geometry_visualization.vsl", "GeometryVisualizationVertexMain", "ResolveCameraOutputDisplaySrgbFragmentMain", nullptr, nullptr, true},
        {"GeometryDiagnostics", "geometry_diagnostics.vsl", nullptr, nullptr, "GeometryDiagnosticsMain", nullptr, false, true}
    };
}

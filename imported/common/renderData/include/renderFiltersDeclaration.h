/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "debugFilter.h"

namespace rend
{
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_DepthMap;
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_BrickProbe;
	extern RENDER_DATA_API DebugFilter DF_ClusteredLightsCount_Debug;
	extern RENDER_DATA_API DebugFilter DF_LightsWithContactShadowCount_Debug;
	extern RENDER_DATA_API DebugFilter DF_ContactShadowOverflow_Debug;
	extern RENDER_DATA_API DebugFilter DF_LightSpheresView_Debug;
	extern RENDER_DATA_API DebugFilter DF_LightBlockersSpheresView_Debug;
	extern RENDER_DATA_API DebugFilter DF_ContactShadowLights;
	extern RENDER_DATA_API DebugFilter DF_ContactShadowLightsHideOutOfRange;
	extern RENDER_DATA_API DebugFilter DF_LocalShadowLights;
	extern RENDER_DATA_API DebugFilter DF_LocalShadowLightsHideOutOfRange;
	extern RENDER_DATA_API DebugFilter DF_ProxyLightsCones_Debug;
	extern RENDER_DATA_API DebugFilter DF_LightingSkyLookup;
	extern RENDER_DATA_API DebugFilter DF_GlobalShadows_Debug;
	extern RENDER_DATA_API DebugFilter DF_LocalShadows_Debug;
	extern RENDER_DATA_API DebugFilter DF_LocalShadows_DebugMeshesInMainView;
	extern RENDER_DATA_API DebugFilter DF_CascadesShadows_DebugMeshesInMainView;
	extern RENDER_DATA_API DebugFilter DF_PostprocessTonemappingDebugValues_Debug;
	extern RENDER_DATA_API DebugFilter DF_PostprocessDisableLUT_Debug;
	extern RENDER_DATA_API DebugFilter DF_PostprocessDisableTonemapping_Debug;
	extern RENDER_DATA_API DebugFilter DF_PostprocessShowDebugGradiends_Debug;
	extern RENDER_DATA_API DebugFilter DF_PostprocessUnlockedExposure_Debug;
	extern RENDER_DATA_API DebugFilter DF_PostprocessExposureLuminance_Debug;
	extern RENDER_DATA_API DebugFilter DF_PostprocessSSR_Planar_Debug;
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_Update;
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_Probes;
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_MissingTetra;
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_Surfels;
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_Tetrahedron;
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_Volume;
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_Hide_Invisible;
	extern RENDER_DATA_API DebugFilter DF_GI_Debug_Hide_Transparent;
	extern RENDER_DATA_API DebugFilter DF_EyeAdaptation;
	extern RENDER_DATA_API DebugFilter DF_GlyphCache;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_ScreenPreview;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_ChromeBalls;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_ChromeBallClosest;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_ChromeBallGlobal;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_ChromeBallSky;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_ChromeBallSH;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_FastRelight;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_TilesCount;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_ID;
	extern RENDER_DATA_API DebugFilter DF_EnvProbe_Debug_Overlap;
	extern RENDER_DATA_API DebugFilter DF_SurfaceCacheAtlas;
	extern RENDER_DATA_API DebugFilter DF_SurfaceCacheClearUnusedPages;
	extern RENDER_DATA_API DebugFilter DF_MultilayerProxy_Surfaces;
	extern RENDER_DATA_API DebugFilter DF_Render_Debug_Textures;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_GeometryProxies;
	extern RENDER_DATA_API DebugFilter DF_HierarchicalZ_Debug;
	extern RENDER_DATA_API DebugFilter DF_DepthPrepass_Debug;
	extern RENDER_DATA_API DebugFilter DF_DistantGI_Debug;
	extern RENDER_DATA_API DebugFilter DF_SimpleFogPreview;
	extern RENDER_DATA_API DebugFilter DF_SSRRoughnessMips_Debug;
	extern RENDER_DATA_API DebugFilter DF_MirrorMaterialTexture_Debug;
	extern RENDER_DATA_API DebugFilter DF_MirrorMaterialLocation_Debug;
	extern RENDER_DATA_API DebugFilter DF_MirrorMaterialCamera_Debug;
	extern RENDER_DATA_API DebugFilter DF_MirrorMaterialReflectionPlane_Debug;
	extern RENDER_DATA_API DebugFilter DF_MirrorCullingBox_Debug;
	extern RENDER_DATA_API DebugFilter DF_Render_Debug_DFG;
	extern RENDER_DATA_API DebugFilter DF_Render_Debug_DPL;
    extern RENDER_DATA_API DebugFilter DF_Render_AutoGrass_TerrainTextures;
    extern RENDER_DATA_API DebugFilter DF_Render_AutoGrass_RenderTerrainEachFrame;

	extern RENDER_DATA_API DebugFilter DF_RenderGPUProfiler;
	extern RENDER_DATA_API DebugFilter DF_HairReference;
	extern RENDER_DATA_API DebugFilter DF_DistantShadowsShowShadowmap;
	extern RENDER_DATA_API DebugFilter DF_DistantShadowsFastUpdate;
	extern RENDER_DATA_API DebugFilter DF_DistantShadowsPreview;
	extern RENDER_DATA_API DebugFilter DF_GlobalShadowsContactPreview;
	extern RENDER_DATA_API DebugFilter DF_GlobalShadowsContactAreaPreview;
	extern RENDER_DATA_API DebugFilter DF_ShowPersistentRainMap;
	extern RENDER_DATA_API DebugFilter DF_ShowTempRainTexture;
	extern RENDER_DATA_API DebugFilter DF_Selection;

	// Rendering filters
	extern RENDER_DATA_API DebugFilter DF_RenderMask_DepthPrepass;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_GBuffer;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_GBufferLate;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_WeaponPlane;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_Forward;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_ForwardNoTXAA;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_Discarded;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_DebugDraw;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_Unlit;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_TopDownCarProxyDebug;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_HUD;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_GameplayPostProcess;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_GeometryStatic;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_GeometrySkinned;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_Particles;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_Decals;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_Lights;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_DistantLights;
	extern RENDER_DATA_API DebugFilter DF_RenderMask_Terrain;
    extern RENDER_DATA_API DebugFilter DF_RenderMask_Foliage;
    extern RENDER_DATA_API DebugFilter DF_RenderMask_AutoGrass;
    extern RENDER_DATA_API DebugFilter DF_RenderMask_EnvProbes;
    extern RENDER_DATA_API DebugFilter DF_RenderMask_Emissive;
    extern RENDER_DATA_API DebugFilter DF_RenderMask_LightChannels;
    extern RENDER_DATA_API DebugFilter DF_RenderMask_Fog;
    
	// GameplayFX Debug
	extern RENDER_DATA_API DebugFilter DF_GameplayFX_ForceFocusMode;
	extern RENDER_DATA_API DebugFilter DF_GameplayFX_ForceSandevistan;
	extern RENDER_DATA_API DebugFilter DF_GameplayFX_ForceKereznikov;
	extern RENDER_DATA_API DebugFilter DF_GameplayFX_ForceCyberspacePostFX;
	extern RENDER_DATA_API DebugFilter DF_GameplayFX_ForceDataMoshing;

	// Visibility system filters
	extern RENDER_DATA_API DebugFilter DF_VisEnableXRay;
	extern RENDER_DATA_API DebugFilter DF_VisCascadeVisibleTargets;
	extern RENDER_DATA_API DebugFilter DF_VisCascadeFrustums;
	extern RENDER_DATA_API DebugFilter DF_VisCameraUseOcclusion;
	extern RENDER_DATA_API DebugFilter DF_VisCameraVisibleTargets;
    extern RENDER_DATA_API DebugFilter DF_VisQueryBoxVisibleTargets;
	extern RENDER_DATA_API DebugFilter DF_VisCameraShowOcclusionBuffer;
	extern RENDER_DATA_API DebugFilter DF_VisCameraShowOccluders;
	extern RENDER_DATA_API DebugFilter DF_VisCameraShowOccludersRasterized;
	extern RENDER_DATA_API DebugFilter DF_VisCameraShowSectors;
	extern RENDER_DATA_API DebugFilter DF_VisOccBufferInternal;
	extern RENDER_DATA_API DebugFilter DF_VisOccBufferRoughMin;
	extern RENDER_DATA_API DebugFilter DF_VisOccBufferRoughMax;
	extern RENDER_DATA_API DebugFilter DF_VisOccBufferFullscreen;
	extern RENDER_DATA_API DebugFilter DF_VisOccBufferTransparent;
	extern RENDER_DATA_API DebugFilter DF_VisOccBufferNormalize;
	extern RENDER_DATA_API DebugFilter DF_VisStaticBVHObjects;
	extern RENDER_DATA_API DebugFilter DF_VisStaticBVHExceptionalObjects;

} // rend

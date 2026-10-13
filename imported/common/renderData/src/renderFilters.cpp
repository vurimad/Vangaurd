/**
* Copyright @ 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderFilters.h"
#include "renderFiltersDeclaration.h"
#include "../../redContainers/include/fixedArray.h"

namespace rend
{	
	red::FixedArray< red::StringView, rend::RenderFilterPreset::Count > c_presetKeyName =
	{
		{ 
			"Game",
			"User",
		}
	};

	static RenderFilterPreset s_gameRenderPreset = RenderFilterPreset::Game;

	// meshes
	DebugFilter DF_Rendering( "Rendering" );

	DebugFilter DF_GI_Debug_DepthMap( "Rendering/Debug/GI/DepthMap" );
	DebugFilter DF_GI_Debug_BrickProbe( "Rendering/Debug/GI/ProbeToBricks" );
	DebugFilter DF_RenderMask( "Rendering/RenderMask" );
	DebugFilter DF_RenderMask_GeometryStatic( "Rendering/RenderMask/GeometryStatic" );
	DebugFilter DF_RenderMask_GeometrySkinned( "Rendering/RenderMask/GeometrySkinned" );
	DebugFilter DF_RenderMask_GeometryProxies( "Rendering/RenderMask/GeometryProxies" );
	DebugFilter DF_RenderMask_DepthPrepass( "Rendering/RenderMask/DepthPrepass" );
	DebugFilter DF_RenderMask_GBuffer( "Rendering/RenderMask/GBuffer" );
	DebugFilter DF_RenderMask_GBufferLate( "Rendering/RenderMask/GBufferLate" );
	DebugFilter DF_RenderMask_WeaponPlane( "Rendering/RenderMask/WeaponPlane" );
	DebugFilter DF_RenderMask_Forward( "Rendering/RenderMask/Forward" );
	DebugFilter DF_RenderMask_ForwardNoTXAA( "Rendering/RenderMask/ForwardNoTXAA" );
	DebugFilter DF_RenderMask_Discarded( "Rendering/RenderMask/Discarded" );
	DebugFilter DF_RenderMask_DebugDraw( "Rendering/RenderMask/DebugDraw" );
	DebugFilter DF_RenderMask_Unlit( "Rendering/RenderMask/Unlit" );
	DebugFilter DF_RenderMask_TopDownCarProxyDebug( "Rendering/RenderMask/TopDownCarProxy" );
	DebugFilter DF_RenderMask_HUD( "Rendering/RenderMask/HUD" );
	DebugFilter DF_RenderMask_GameplayPostProcess( "Rendering/RenderMask/GameplayPostProcess" );
	DebugFilter DF_RenderMask_Particles( "Rendering/RenderMask/Particles" );
	DebugFilter DF_RenderMask_Decals( "Rendering/RenderMask/Decals" );
	DebugFilter DF_RenderMask_Lights( "Rendering/RenderMask/Lights" );
	DebugFilter DF_RenderMask_DistantLights( "Rendering/RenderMask/DistantLights" );
	DebugFilter DF_RenderMask_Terrain( "Rendering/RenderMask/Terrain" );
    DebugFilter DF_RenderMask_Foliage( "Rendering/RenderMask/Foliage" );
    DebugFilter DF_RenderMask_AutoGrass( "Rendering/RenderMask/AutoGrass" );
    DebugFilter DF_RenderMask_EnvProbes( "Rendering/RenderMask/EnvProbes" );
    DebugFilter DF_RenderMask_Emissive( "Rendering/RenderMask/Emissive" );
    DebugFilter DF_RenderMask_LightChannels( "Rendering/RenderMask/LightChannels" );
    DebugFilter DF_RenderMask_Fog( "Rendering/RenderMask/Fog" );

	DebugFilter DF_Selection( "Rendering/Selection" );

	// debug
	DebugFilter DF_RenderGPUProfiler( "Rendering/Debug/RenderGPUProfiler" );
	DebugFilter DF_DepthPrepass_Debug( "Rendering/Debug/DepthPrepass" );
	DebugFilter DF_HierarchicalZ_Debug( "Rendering/Debug/HierarchicalDepthDebug" );
	DebugFilter DF_DistantGI_Debug( "Rendering/Debug/DistantGI" );
	DebugFilter DF_SimpleFogPreview( "Rendering/Debug/SimpleFogPreview" );
	DebugFilter DF_SSRRoughnessMips_Debug( "Rendering/Debug/SSRRoughnessMips" );
	DebugFilter DF_MirrorMaterialTexture_Debug( "Rendering/Debug/Mirror/MaterialTexture" );
	DebugFilter DF_MirrorMaterialLocation_Debug( "Rendering/Debug/Mirror/MaterialLocation" );
	DebugFilter DF_MirrorMaterialCamera_Debug( "Rendering/Debug/Mirror/MaterialCamera" );
	DebugFilter DF_MirrorMaterialReflectionPlane_Debug( "Rendering/Debug/Mirror/MaterialReflectionPlane" );
	DebugFilter DF_MirrorCullingBox_Debug("Rendering/Debug/Mirror/CullingBox");
	DebugFilter DF_PostprocessTonemappingDebugValues_Debug( "Rendering/Debug/Postprocess/DebugValues" );
	DebugFilter DF_PostprocessDisableLUT_Debug( "Rendering/Debug/Postprocess/DisableLUT" );
	DebugFilter DF_PostprocessDisableTonemapping_Debug( "Rendering/Debug/Postprocess/DisableTonemapping" );
	DebugFilter DF_PostprocessShowDebugGradiends_Debug( "Rendering/Debug/Postprocess/ShowDebugGradiends" );
	DebugFilter DF_PostprocessExposureLuminance_Debug( "Rendering/Debug/Postprocess/ExposureLuminance" );
	DebugFilter DF_PostprocessUnlockedExposure_Debug( "Rendering/Debug/Postprocess/UnlockedExposure" );
	DebugFilter DF_ClusteredLightsCount_Debug( "Rendering/Debug/Lighting/ClusteredLightsCount" );
	DebugFilter DF_LightsWithContactShadowCount_Debug( "Rendering/Debug/Lighting/LightsWithContactShadowCount" );
	DebugFilter DF_ContactShadowOverflow_Debug( "Rendering/Debug/Lighting/ContactShadowOverflow" );
	DebugFilter DF_LightSpheresView_Debug( "Rendering/Debug/Lighting/LightSpheresView" );
	DebugFilter DF_LightBlockersSpheresView_Debug( "Rendering/Debug/Lighting/LightBlockersSpheresView" );
	DebugFilter DF_LocalShadowLights( "Rendering/Debug/Shadows/HighlightLightsWithLocalShadows" );
	DebugFilter DF_LocalShadowLightsHideOutOfRange( "Rendering/Debug/Shadows/HighlightLightsWithLocalShadows/HideOutOfRange" );
	DebugFilter DF_ContactShadowLights("Rendering/Debug/Shadows/HighlightLightsWithContactShadows");
	DebugFilter DF_ContactShadowLightsHideOutOfRange( "Rendering/Debug/Shadows/HighlightLightsWithContactShadows/HideOutOfRange" );
	DebugFilter DF_ProxyLightsCones_Debug( "Rendering/Debug/Lighting/ProxyCones" );
	DebugFilter DF_LightingSkyLookup( "Rendering/Debug/Lighting/SkyLookup" );
	DebugFilter DF_Render_Debug_DFG( "Rendering/Debug/Lighting/DFG" );
	DebugFilter DF_PostprocessSSR_Planar_Debug( "Rendering/Debug/Postprocess/SSR_Planar" );
    DebugFilter DF_Render_AutoGrass_TerrainTextures( "Rendering/Debug/AutoGrass/TerrainTextures" );
    DebugFilter DF_Render_AutoGrass_RenderTerrainEachFrame( "Rendering/Debug/AutoGrass/RenderTerrainEachFrame" );

	DebugFilter DF_GlobalShadows_Debug( "Rendering/Debug/Shadows/GlobalShadows" );
	DebugFilter DF_LocalShadows_Debug( "Rendering/Debug/Shadows/LocalShadows" );
	DebugFilter DF_LocalShadows_DebugMeshesInMainView( "Rendering/Debug/Shadows/LocalShadowsRenderedMeshes" );
	DebugFilter DF_CascadesShadows_DebugMeshesInMainView( "Rendering/Debug/Shadows/CascadesShadowsRenderedMeshes" );

	// #TEMP: So distant shadows can be seen, before things have been fully set up.
	DebugFilter DF_DistantShadowsShowShadowmap( "Rendering/Debug/Shadows/DistantShowShadowmap" );
	DebugFilter DF_DistantShadowsFastUpdate( "Rendering/Debug/Shadows/DistantFastUpdate" );
	DebugFilter DF_DistantShadowsPreview( "Rendering/Debug/Shadows/DistantPreview" );
	DebugFilter DF_GlobalShadowsContactPreview( "Rendering/Debug/Shadows/GlobalContact" );
	DebugFilter DF_GlobalShadowsContactAreaPreview( "Rendering/Debug/Shadows/GlobalContactArea" );

	DebugFilter DF_ShowPersistentRainMap( "Rendering/Debug/RainMap/ShowPersistentRainMap" );
	DebugFilter DF_ShowTempRainTexture( "Rendering/Debug/RainMap/ShowRainTexture" );

	DebugFilter DF_GlyphCache( "Rendering/Debug/GlyphCache" );
	DebugFilter DF_EnvProbe_Debug_ScreenPreview( "Rendering/Debug/ReflectionProbes/ScreenPreview" );
	DebugFilter DF_EnvProbe_Debug_ChromeBalls( "Rendering/Debug/ReflectionProbes/ChromeBalls" );
	DebugFilter DF_EnvProbe_Debug_ChromeBallClosest( "Rendering/Debug/ReflectionProbes/ChromeBallClosest" );
	DebugFilter DF_EnvProbe_Debug_ChromeBallGlobal( "Rendering/Debug/ReflectionProbes/ChromeBallGlobal" );
	DebugFilter DF_EnvProbe_Debug_ChromeBallSky( "Rendering/Debug/ReflectionProbes/ChromeBallSky" );
	DebugFilter DF_EnvProbe_Debug_ChromeBallSH( "Rendering/Debug/ReflectionProbes/ChromeBallSH" );
	DebugFilter DF_EnvProbe_Debug_FastRelight( "Rendering/Debug/ReflectionProbes/FastRelight" );
	DebugFilter DF_EnvProbe_Debug_TilesCount( "Rendering/Debug/ReflectionProbes/TilesCount" );
	DebugFilter DF_EnvProbe_Debug_ID( "Rendering/Debug/ReflectionProbes/EnvProbeID" );
	DebugFilter DF_EnvProbe_Debug_Overlap( "Rendering/Debug/ReflectionProbes/EnvProbeOverlap" );
	DebugFilter DF_HairReference( "Rendering/Debug/HairReference" );

	DebugFilter DF_GI_Debug_Update( "Rendering/Debug/GI/Update" );
	DebugFilter DF_GI_Debug_Probes( "Rendering/Debug/GI/Probes" );
	DebugFilter DF_GI_Debug_MissingTetra( "Rendering/Debug/GI/MissingTetraIndicator" );
	DebugFilter DF_GI_Debug_Surfels( "Rendering/Debug/GI/Surfels" );
	DebugFilter DF_GI_Debug_Tetrahedron("Rendering/Debug/GI/Tetrahedron");
	DebugFilter DF_GI_Debug_Volume( "Rendering/Debug/GI/Volume" );
	DebugFilter DF_GI_Debug_Hide_Invisible( "Rendering/Debug/GI/Hide/Invisible" );
	DebugFilter DF_GI_Debug_Hide_Transparent( "Rendering/Debug/GI/Hide/Transparent" );
	DebugFilter DF_SurfaceCacheAtlas( "Rendering/Debug/SurfaceCacheAtlas" );
	DebugFilter DF_SurfaceCacheClearUnusedPages( "Rendering/Debug/SurfaceCacheClearUnusedPages" );
	DebugFilter DF_Render_Debug_Textures( "Rendering/Debug/InternalTexturesTest" );
	DebugFilter DF_Render_Debug_DPL( "Rendering/Debug/DecoupledParticleTexture" );

	DebugFilter DF_MultilayerProxy_Surfaces( "Rendering/Debug/MultilayerProxy/Surfaces" );

	DebugFilter DF_GameplayFX_ForceFocusMode( "Rendering/Debug/GameplayEffect/ForceFocusMode" );
	DebugFilter DF_GameplayFX_ForceSandevistan( "Rendering/Debug/GameplayEffect/ForceSandevistan" );
	DebugFilter DF_GameplayFX_ForceKereznikov( "Rendering/Debug/GameplayEffect/ForceKereznikov" );
	DebugFilter DF_GameplayFX_ForceCyberspacePostFX("Rendering/Debug/GameplayEffect/ForceCyberspacePostFX");
	DebugFilter DF_GameplayFX_ForceDataMoshing("Rendering/Debug/GameplayEffect/ForceCyberspacePostFX/DataMoshing");


	// visibility
	DebugFilter DF_Vis( "Rendering/Visibility" );
	DebugFilter DF_VisOccBuffer( "Rendering/Visibility/OccBuffer" );
	DebugFilter DF_VisEnableXRay( "Rendering/Visibility/EnableXRay" );
	DebugFilter DF_VisCascadeVisibleTargets( "Rendering/Visibility/CascadesVisibleTargets" );
	DebugFilter DF_VisCascadeFrustums( "Rendering/Visibility/CascadesFrustums" );
	DebugFilter DF_VisCameraUseOcclusion( "Rendering/Visibility/CameraUseOcclusion" );
	DebugFilter DF_VisCameraUseOcclusionPvo( "Rendering/Visibility/CameraUseOcclusionPvo" );
	DebugFilter DF_VisCameraVisibleTargets( "Rendering/Visibility/CameraVisibileTargets" );
    DebugFilter DF_VisQueryBoxVisibleTargets( "Rendering/Visibility/QueryBoxVisibileTargets" );
	DebugFilter DF_VisCameraShowOcclusionBuffer( "Rendering/Visibility/CameraShowOcclusionBuffer" );
	DebugFilter DF_VisCameraShowOccluders( "Rendering/Visibility/CameraShowOccluders" );
	DebugFilter DF_VisCameraShowOccludersRasterized( "Rendering/Visibility/CameraShowOccludersRasterized" );
	DebugFilter DF_VisCameraShowSectors( "Rendering/Visibility/CameraShowSectors" );
	DebugFilter DF_VisCameraShowOccluderVisibleRange( "Rendering/Visibility/CameraShowOccluderVisibleRange" );
	DebugFilter DF_VisCameraShowOccluderBroadphaseRange( "Rendering/Visibility/CameraShowOccluderBroadphaseRange" );
	DebugFilter DF_VisOccBufferRoughMin( "Rendering/Visibility/OccBuffer/RoughMin" );
	DebugFilter DF_VisOccBufferRoughMax( "Rendering/Visibility/OccBuffer/RoughMax" );
	DebugFilter DF_VisOccBufferInternal( "Rendering/Visibility/OccBuffer/InternalData" );
	DebugFilter DF_VisOccBufferFullscreen( "Rendering/Visibility/OccBuffer/Fullscreen" );
	DebugFilter DF_VisOccBufferTransparent( "Rendering/Visibility/OccBuffer/Transparent" );
	DebugFilter DF_VisOccBufferNormalize( "Rendering/Visibility/OccBuffer/Normalize" );
	DebugFilter DF_VisStaticBVHObjects( "Rendering/Visibility/StaticBVHObjects" );
	DebugFilter DF_VisStaticBVHExceptionalObjects( "Rendering/Visibility/StaticBVHExceptionalObjects" );

	typedef red::StaticArray< std::pair< Uint32, Uint8 >, rend::c_maxDebugFilter > RenderingFilterPresetBitmap;
	
	RenderingFilterPresetBitmap s_renderingFilterPresetBitmap;

	void SetPresetValue( const DebugFilter & filter, Uint8 value )
	{
		const Uint32 index = filter.GetBitIndex();
		s_renderingFilterPresetBitmap.PushBack( std::make_pair( index, value ) );
	}

	void InitializeRenderFilterPresets()
	{	
		if( !s_renderingFilterPresetBitmap.Empty() )
			return;

		SetPresetValue( DF_Rendering,						All );

		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// ALSO ADD THIS TO engine\editor\debugFilters\default.json TO MAKE IT WORK IN EDITOR - DUH....
		//					engine\editor\debugFilters\game.json
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!

		SetPresetValue( DF_RenderMask,						All );
		SetPresetValue( DF_RenderMask_GeometryStatic,		All );
		SetPresetValue( DF_RenderMask_GeometrySkinned,		All );
		SetPresetValue( DF_RenderMask_GeometryProxies,		All );
		SetPresetValue( DF_RenderMask_DepthPrepass,			All );
		SetPresetValue( DF_RenderMask_GBuffer,				All );
		SetPresetValue( DF_RenderMask_GBufferLate,			All );
		SetPresetValue( DF_RenderMask_WeaponPlane,			All );
		SetPresetValue( DF_RenderMask_Forward,				All );
		SetPresetValue( DF_RenderMask_ForwardNoTXAA,		All );
		SetPresetValue( DF_RenderMask_Discarded,			All );
		SetPresetValue( DF_RenderMask_DebugDraw,			All );
		SetPresetValue( DF_RenderMask_Unlit,				All );
		SetPresetValue( DF_RenderMask_HUD,					All );
		SetPresetValue( DF_RenderMask_GameplayPostProcess,	All );
		SetPresetValue( DF_RenderMask_Particles,			All );
		SetPresetValue( DF_RenderMask_Decals,				All );
		SetPresetValue( DF_RenderMask_Lights,				All );
		SetPresetValue( DF_RenderMask_DistantLights,		All );
		SetPresetValue( DF_RenderMask_Terrain,				All );
        SetPresetValue( DF_RenderMask_Foliage,              All );
        SetPresetValue( DF_RenderMask_AutoGrass,            All );
        SetPresetValue( DF_RenderMask_EnvProbes,            All );
        SetPresetValue( DF_RenderMask_Emissive,             All );
        SetPresetValue( DF_RenderMask_LightChannels,        All );
        SetPresetValue( DF_RenderMask_Fog,					All );
		SetPresetValue( DF_Selection,						All );

		SetPresetValue( DF_Vis,								All );
		SetPresetValue( DF_VisOccBuffer,					All );
		SetPresetValue( DF_VisCameraUseOcclusion,			All );
		// A few modifiers are on by default, but none of the actual visualizations
		SetPresetValue( DF_VisEnableXRay,					All );
		SetPresetValue( DF_VisOccBufferNormalize,			All );

		// !!!!!!!!!!!!!!!!!!!!
		// ALSO ADD THIS TO engine\editor\debugFilters\default.json TO MAKE IT WORK IN EDITOR - DUH....
		//					engine\editor\debugFilters\game.json
		// !!!!!!!!!!!!!!!!!!!!
	}

	void ApplyRenderFilterMask( RenderFilterPreset preset, DebugFilterMask & filterMask )
	{
		InitializeRenderFilterPresets();

		if( preset != RenderFilterPreset::User )
		{
			for( auto & filter : s_renderingFilterPresetBitmap )
			{
				const Uint32 index = filter.first;
				const Uint8 mask = filter.second;
				const bool value = ( mask & preset ) != 0;

				filterMask.SetSingleBitState( index, value );
			}
		}
	}

	RenderFilterPreset GetRenderFilterPreset( red::StringView value )
	{
		auto iter = std::find( c_presetKeyName.Begin(), c_presetKeyName.End(), value );
		if( iter != c_presetKeyName.End() )
		{
			const rend::RenderFilterPreset presetKey = static_cast< rend::RenderFilterPreset >( 1 << std::distance( c_presetKeyName.Begin(), iter ) );
			return presetKey; 
		}

		return RenderFilterPreset::Game;
	}

	red::ArraySpan< const red::StringView > GetRenderFilterPresetNames()
	{
		return c_presetKeyName;
	}

	red::StringView GetRenderFilterPresetName( RenderFilterPreset preset )
	{
		Uint32 index = red::BitUtils::BitScanForward<Uint32>( preset );
		return c_presetKeyName[ index ];
	}

	
	void SetGameRenderPreset( RenderFilterPreset preset )
	{
		s_gameRenderPreset = preset;
	}

	RenderFilterPreset GetGameRenderPreset()
	{
		return s_gameRenderPreset;
	}

} // rend

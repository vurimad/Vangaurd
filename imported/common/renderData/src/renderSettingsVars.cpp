/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "renderSettingsVars.h"
#include "renderer.h"
#include "../../redCore/include/names.h"
#include "../../redMemory/src/systemAllocatorType.h"

// For UpdateWithInGameVideoSettings procedure.
#include "../../redConfig/include/inGameConfigUtils.h"
#include "../../redConfig/include/inGameConfigSystem.h"
#include "../../redConfig/include/inGameConfigGroup.h"
#include "../../redConfig/include/inGameConfigVar.h"
#include "renderInGameSettings.h"
#include "rendererDevConfig.h"

using red::DynArray;
RTTI_BEGIN_ENUM_IN_NAMESPACE( GraphicsQualityLevel, Config )
{
	RTTI_ENUM_OPTION( Low			);
	RTTI_ENUM_OPTION( Medium		);
	RTTI_ENUM_OPTION( High			);
	RTTI_ENUM_OPTION( Ultra			);
	RTTI_ENUM_OPTION( RTXMedium		);
	RTTI_ENUM_OPTION( RTXUltra		);
	RTTI_ENUM_OPTION( Cinematic		);
	RTTI_ENUM_OPTION( Cinematic_RTX );
	RTTI_ENUM_OPTION( CinematicEXR	);
	RTTI_ENUM_OPTION( CinematicEXR_RTX );
	RTTI_ENUM_OPTION( SafeMode		);
	RTTI_ENUM_OPTION( Console		);
	RTTI_ENUM_OPTION( ConsolePro );
	RTTI_ENUM_OPTION( ConsoleEarlyNextGen );
	RTTI_ENUM_OPTION( ConsoleEarlyNextGenQuality );
	RTTI_ENUM_OPTION( GeForceNow	);
	RTTI_ENUM_OPTION( IconsGeneration );
	RTTI_ENUM_OPTION( Auto			);
}
RTTI_END_ENUM();
RTTI_BEGIN_ENUM_IN_NAMESPACE( TextureQualityLevel, Config )
{
	RTTI_ENUM_OPTION( Console	);
	RTTI_ENUM_OPTION( ConsoleSafe );
	RTTI_ENUM_OPTION( Low		);
	RTTI_ENUM_OPTION( Medium	);
	RTTI_ENUM_OPTION( High		);
	RTTI_ENUM_OPTION( SafeMode	);
	RTTI_ENUM_OPTION( Auto		);
}
RTTI_END_ENUM();

#ifdef RED_PLATFORM_WINPC
namespace crash
{
	red::CrashData<Bool> rtxEnabled { "Gpu/RayTracing", "Enabled" };
}
#endif


namespace Config
{
	//////////////////////////////////////////////////////////////////////////
	// Constants shared between GlobalRenderingSettings and in game settings. These are variants available via
	// in game settings mapped to values meaningful to GlobalRenderingSettings.
	// [BD] I am not sure how readable is that... if you have better idea please let me know.
	namespace video_settings
	{
		// Cascades shadows

		constexpr Uint32 s_NumShadowCascades_LOW = 2;
		constexpr Uint32 s_NumShadowCascades_MEDIUM = 3;
		constexpr Uint32 s_NumShadowCascades_HIGH = 4;

		constexpr Uint32 s_ShadowCascadeResolution_LOW = 1024;
		constexpr Uint32 s_ShadowCascadeResolution_MEDIUM = 2048;
		constexpr Uint32 s_ShadowCascadeResolution_HIGH = 4096;

		// Distant shadows

		constexpr Uint32 s_DistantShadowmapResolution_LOW = 1024;
		constexpr Uint32 s_DistantShadowmapResolution_HIGH = 2048;

		// Volumetric fog

		constexpr Uint32 s_VolumetricFogTileSize_LOW = 16;
		constexpr Uint32 s_VolumetricFogNumSlices_LOW = 64;
		constexpr Uint32 s_VolumetricFogNumSamples_LOW = 4;

		constexpr Uint32 s_VolumetricFogTileSize_MEDIUM = 12;
		constexpr Uint32 s_VolumetricFogNumSlices_MEDIUM = 64;
		constexpr Uint32 s_VolumetricFogNumSamples_MEDIUM = 8;

		constexpr Uint32 s_VolumetricFogTileSize_HIGH = 8;
		constexpr Uint32 s_VolumetricFogNumSlices_HIGH = 96;
		constexpr Uint32 s_VolumetricFogNumSamples_HIGH = 16;

		constexpr Uint32 s_VolumetricFogTileSize_ULTRA = 8;
		constexpr Uint32 s_VolumetricFogNumSlices_ULTRA = 128;
		constexpr Uint32 s_VolumetricFogNumSamples_ULTRA = 16;

		// SSR

		constexpr Uint8 s_ScreenSpaceReflectionLevel_OFF = -1;
		constexpr Uint8 s_ScreenSpaceReflectionLevel_LOW = 0;
		constexpr Uint8 s_ScreenSpaceReflectionLevel_MEDIUM = 1;
		constexpr Uint8 s_ScreenSpaceReflectionLevel_HIGH = 2;
		constexpr Uint8 s_ScreenSpaceReflectionLevel_ULTRA = 3;
		constexpr Uint8 s_ScreenSpaceReflectionLevel_INSANE = 4;

		// Skin

		constexpr Uint32  s_SubsurfaceQualityProfileIndex_LOW = 3;
		constexpr Uint32  s_SubsurfaceQualityProfileIndex_MEDIUM = 2;
		constexpr Uint32  s_SubsurfaceQualityProfileIndex_HIGH = 1;

		// AO

		constexpr Uint8 s_ConeAOQuality_LOW = 0;
		constexpr Uint8 s_ConeAOQuality_MEDIUM = 1;
		constexpr Uint8 s_ConeAOQuality_HIGH = 2;

		// Mirror

		constexpr Float s_MirrorFinalResolutionScale_LOW = 0.7f;
		constexpr Float s_MirrorFinalResolutionScale_MEDIUM = 0.8f;
		constexpr Float s_MirrorFinalResolutionScale_HIGH = 1.0f;

		constexpr Float s_MirrorInternalResolutionScale_LOW = 0.7f;
		constexpr Float s_MirrorInternalResolutionScale_MEDIUM = 0.9f;
		constexpr Float s_MirrorInternalResolutionScale_HIGH = 1.0f;

		// RTX  
		constexpr Uint8 s_RTX_AmbientOcclusionRayNumber_OFF = 1;
		constexpr Uint8 s_RTX_AmbientOcclusionRayNumber_MEDIUM = 1;
		constexpr Uint8 s_RTX_AmbientOcclusionRayNumber_ULTRA = 2;
		constexpr Uint8 s_RTX_AmbientOcclusionRayNumber_PSYCHO = 3;

		constexpr Uint8 s_RTX_ImportanceSamplingTransitionMin_OFF = 1;
		constexpr Uint8 s_RTX_ImportanceSamplingTransitionMin_MEDIUM = 2;
		constexpr Uint8 s_RTX_ImportanceSamplingTransitionMin_ULTRA = 8;
		constexpr Uint8 s_RTX_ImportanceSamplingTransitionMin_PSYCHO = 8;

		constexpr Uint8 s_RTX_ImportanceSamplingTransitionMax_OFF = 1;
		constexpr Uint8 s_RTX_ImportanceSamplingTransitionMax_MEDIUM = 4;
		constexpr Uint8 s_RTX_ImportanceSamplingTransitionMax_ULTRA = 12;
		constexpr Uint8 s_RTX_ImportanceSamplingTransitionMax_PSYCHO = 12;

		//////////////////////////////////////////////////////////////////////////
		// RTX Reflections
		Bool RTX_EnableReflections;
		Bool RTX_EnableTransparentReflections;

		//////////////////////////////////////////////////////////////////////////
		// RTX Shadows
		Bool RTX_EnableDirectionalShadow;

	}

	//////////////////////////////////////////////////////////////////////////
	// Setting groups
	GlobalRenderingSettings::GlobalRenderingSettings()
	{
		SafeMode = false;
		ForceInternalResolutionScaling = false;	// No scaling. The same as final resolution.

		MirrorFinalResolutionScale = video_settings::s_MirrorFinalResolutionScale_LOW;
		MirrorInternalResolutionScale = video_settings::s_MirrorInternalResolutionScale_LOW;

		// Let's have this always inited with basic values. This way all perf profiles can just modified what they want.
		AnisotropyLevel = 1;
		TextureMipBias = -0.5f;		// Even on Low, since we ALWAYS have TAA enabled
		ShadowLODBias = UINT32_MAX;	// When casting shadows, just select worst LOD instead of what's visible in main camera
		ShadowCascadeResolution = video_settings::s_ShadowCascadeResolution_LOW;
		NumShadowCascades = video_settings::s_NumShadowCascades_LOW;
		DistantShadowmapResolution = video_settings::s_DistantShadowmapResolution_LOW;
		DistantShadowsMaxLevels = 4;
		UnrestrictedShadowCubeUpdate = false;
		RainMapShadowsResolution = 2048;
		RainMapCascade0Size = 20.0f;
		RainMapCascade1Size = 200.0f;
		VolFogTileSize	= video_settings::s_VolumetricFogTileSize_LOW;
		VolFogNumSlices = video_settings::s_VolumetricFogNumSlices_LOW;
		VolFogNumSamples = video_settings::s_VolumetricFogNumSamples_LOW;
		DecalChance = 1.0f;
		DecalSpawnDistanceCutoff = 10.0f;
		SubsurfaceQualityProfileIndex = video_settings::s_SubsurfaceQualityProfileIndex_LOW; // LOW
		ConeAOQuality = video_settings::s_ConeAOQuality_LOW;
		MotionBlurQuality = 0;
		UseHighPrecisionRenderTargets = false;
		AllowColorCorrection = true;
		AllowFilmGrain = false;
		allowSimpleFog = true;
		allowVolumetricFog = false;
		allowDistantVolFog = false;
		allowVolumetricClouds = false;
		allowDistantFog = false;
		allowFXAA = false;
		allowTXAA = true;
		allowSSR = false;	ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_OFF; allowSSR_UberQuality = false; allowSSR_Planar = false;
		allowBloom = false;
		allowImageBasedFlares = false;
		allowDOF = false;
		allowSSAO = false;
		allowMotionBlur = false;
		allowChromaticAberration = false;
		allowDistantShadows = true;
		allowRainMap = true;
		allowLocalShadows = false;
		allowDistantGI = true;
		allowGI = true;
		allowContactShadows_Global = false;
		allowContactShadows_Local = false;
		allowAtmosphere = true;
		allowReflectionProbes = true;
		allowRuntimeTangentUpdate = false;
		allowAutoSpawnSystemShadows = false;
		allowCyberspacePostProcess = false;
		allowMirrorMaterial = true;	// Mirror enabled by default.
		forceHighestLOD = false;
		UseMaxSkeletalLODOnEverything = false;
		SyncLipsyncToAudio = true;
		volumetricCloudsDownsample = 2;
		volumetricCloudsNumShadowSteps = 4;
		volumetricCloudsNumSteps = 16;
		volumetricCloudsStepScale = 2;
		volumetricCloudsShadowStepScale = 2;
		volumetricCloudsShadowResolution = 512u;
		globalIllumination.volumeDistance = 128;
		globalIllumination.maxSectors = 256;
		globalIllumination.nodeStreamingData = 153.0f;
		globalIllumination.tetraWorkspaceMaxSlots = 256;
		globalIllumination.tetraWorkspaceBudget = 48 * 1024 * 1024;
		globalIllumination.accelerationStructureResolution = 64;
		globalIllumination.volumeResolution = 128;
		globalIllumination.ultraOn = false;

		using namespace rend::settings;
		Set< InGameEntry::LocalShadowsQuality, InGameOption::Off >( *this );
		Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Off >( *this );
		Set< InGameEntry::MaxDynamicDecals, InGameOption::Low >( *this );

		//////////////////////////////////////////////////////////////////////////
		// RTX Lighting 
		allowRaytracing = false;
		RTX_EnableDiffuseIllumination = false;
		RTX_EnableAmbientOcclusion = false;
		RTX_EnableGlobalIllumination = false;
		RTX_AmbientOcclusionRayNumber = video_settings::s_RTX_AmbientOcclusionRayNumber_OFF;
		RTX_ImportanceSamplingTransitionMin = video_settings::s_RTX_ImportanceSamplingTransitionMin_OFF;
		RTX_ImportanceSamplingTransitionMax = video_settings::s_RTX_ImportanceSamplingTransitionMax_OFF;

		//////////////////////////////////////////////////////////////////////////
		// RTX Reflections
		RTX_EnableReflections = false;
		RTX_EnableTransparentReflections = false;

		//////////////////////////////////////////////////////////////////////////
		// RTX Shadows
		RTX_EnableDirectionalShadow = false;
	}

	GlobalRenderingSettings GetRenderSettings_LOW()
	{
		// Just return the base one
		return GlobalRenderingSettings();
	}
	GlobalRenderingSettings GetRenderSettings_MEDIUM()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_LOW();

		renderSettings.MirrorFinalResolutionScale = video_settings::s_MirrorFinalResolutionScale_MEDIUM;
		renderSettings.MirrorInternalResolutionScale = video_settings::s_MirrorInternalResolutionScale_MEDIUM;

		renderSettings.AnisotropyLevel = 4;
		renderSettings.DistantShadowmapResolution = video_settings::s_DistantShadowmapResolution_HIGH;

		renderSettings.AllowColorCorrection = true;
		renderSettings.allowVolumetricFog = true;
		renderSettings.allowDistantVolFog = true;
		renderSettings.allowFXAA = false;
		renderSettings.allowSSR = true;
		renderSettings.allowSSR_Planar = true;
		renderSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_MEDIUM;
		renderSettings.allowBloom = true;
		renderSettings.allowImageBasedFlares = true;
		renderSettings.allowSSAO = true;
		renderSettings.allowAutoSpawnSystemShadows = true;
		renderSettings.allowLocalShadows = true;	// 512 resolution.
		renderSettings.allowCyberspacePostProcess = true;

		renderSettings.NumShadowCascades = video_settings::s_NumShadowCascades_MEDIUM;
		renderSettings.ShadowCascadeResolution = video_settings::s_ShadowCascadeResolution_MEDIUM;
		renderSettings.SubsurfaceQualityProfileIndex = video_settings::s_SubsurfaceQualityProfileIndex_MEDIUM;

		using namespace rend::settings;
		Set< InGameEntry::LocalShadowsQuality, InGameOption::Low >( renderSettings );	// 512x512, 2 refreshes per frame.
		Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Medium >( renderSettings );
		Set< InGameEntry::MaxDynamicDecals, InGameOption::Medium >( renderSettings );

		return renderSettings;
	}

	GlobalRenderingSettings GetRenderSettings_HIGH()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_MEDIUM();

		renderSettings.MirrorFinalResolutionScale = video_settings::s_MirrorFinalResolutionScale_MEDIUM;
		renderSettings.MirrorInternalResolutionScale = video_settings::s_MirrorInternalResolutionScale_MEDIUM;

		renderSettings.AnisotropyLevel = 8;

		renderSettings.ShadowLODBias = 1;	// Instead of worst LOD, use bias of 1

		renderSettings.UseHighPrecisionRenderTargets = true;
		renderSettings.AllowFilmGrain = true;
		renderSettings.allowDistantFog = true;
		renderSettings.allowFXAA = false;
		renderSettings.allowTXAA = true;
		renderSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_HIGH;
		renderSettings.allowDOF = true;
		renderSettings.allowMotionBlur = true;
		renderSettings.allowLocalShadows = true;
		renderSettings.allowRuntimeTangentUpdate = true;

		renderSettings.VolFogTileSize	= video_settings::s_VolumetricFogTileSize_HIGH;
		renderSettings.VolFogNumSlices	= video_settings::s_VolumetricFogNumSlices_HIGH;
		renderSettings.VolFogNumSamples	= video_settings::s_VolumetricFogNumSamples_HIGH;
		renderSettings.SubsurfaceQualityProfileIndex = video_settings::s_SubsurfaceQualityProfileIndex_HIGH;
		renderSettings.NumShadowCascades = video_settings::s_NumShadowCascades_HIGH;
		renderSettings.ShadowCascadeResolution = video_settings::s_ShadowCascadeResolution_MEDIUM;
		renderSettings.ConeAOQuality = video_settings::s_ConeAOQuality_MEDIUM;

		using namespace rend::settings;
		Set< InGameEntry::LocalShadowsQuality, InGameOption::Medium >( renderSettings );	// 512x512, 3 refreshes per frame.
		Set< InGameEntry::VolumetricCloudsQuality, InGameOption::High >( renderSettings );
		Set< InGameEntry::MaxDynamicDecals, InGameOption::High >( renderSettings );

		return renderSettings;
	}
	GlobalRenderingSettings GetRenderSettings_ULTRA()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_HIGH();

		renderSettings.MirrorFinalResolutionScale = video_settings::s_MirrorFinalResolutionScale_HIGH;
		renderSettings.MirrorInternalResolutionScale = video_settings::s_MirrorInternalResolutionScale_HIGH;

		renderSettings.AnisotropyLevel = 16;
		renderSettings.ShadowLODBias = 0;	// NO bias whatsoever

		renderSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_ULTRA;
		renderSettings.allowChromaticAberration = true;
		renderSettings.allowContactShadows_Global = true;
		renderSettings.allowContactShadows_Local = true;
		renderSettings.allowMirrorMaterial = true;

		renderSettings.VolFogNumSlices	= video_settings::s_VolumetricFogNumSlices_ULTRA;


		renderSettings.ShadowCascadeResolution = video_settings::s_ShadowCascadeResolution_HIGH;
		renderSettings.ConeAOQuality = video_settings::s_ConeAOQuality_HIGH;
		renderSettings.MotionBlurQuality = 1;

		using namespace rend::settings;
		Set< InGameEntry::LocalShadowsQuality, InGameOption::High >( renderSettings );	// 1k x 1k, 5 refreshes per frame.
		Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Ultra >( renderSettings );
		Set< InGameEntry::MaxDynamicDecals, InGameOption::Ultra >( renderSettings );

		return renderSettings;
	}
	GlobalRenderingSettings GetRenderSettings_RTXMEDIUM()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_ULTRA();

		//
		// Enable ray tracing only when it is supported by the hardware.
		//
		const Bool rtSupported = GRender->GetHardwareRayTracingSupport();
		renderSettings.allowRaytracing = rtSupported;

		//
		// RTX Lighting
		//
		renderSettings.RTX_EnableDiffuseIllumination = true;
		renderSettings.RTX_EnableAmbientOcclusion = true;
		renderSettings.RTX_EnableGlobalIllumination = false;
		renderSettings.RTX_AmbientOcclusionRayNumber = video_settings::s_RTX_AmbientOcclusionRayNumber_MEDIUM;
		renderSettings.RTX_ImportanceSamplingTransitionMin = video_settings::s_RTX_ImportanceSamplingTransitionMin_MEDIUM;
		renderSettings.RTX_ImportanceSamplingTransitionMax = video_settings::s_RTX_ImportanceSamplingTransitionMax_MEDIUM;

		//
		// RTX Reflections
		//
		renderSettings.RTX_EnableReflections = false;
		renderSettings.RTX_EnableTransparentReflections = false;

		//
		// RTX Shadows
		//
		renderSettings.RTX_EnableDirectionalShadow = true;

		return renderSettings;
	}
	GlobalRenderingSettings GetRenderSettings_RTXULTRA()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_ULTRA();

		//
		// Enable ray tracing only when it is supported by the hardware.
		//
		const Bool rtSupported = GRender->GetHardwareRayTracingSupport();
		renderSettings.allowRaytracing = rtSupported;

		//
		// RTX Lighting
		//
		renderSettings.RTX_EnableDiffuseIllumination = true;
		renderSettings.RTX_EnableAmbientOcclusion = true;
		renderSettings.RTX_EnableGlobalIllumination = false;
		renderSettings.RTX_AmbientOcclusionRayNumber = video_settings::s_RTX_AmbientOcclusionRayNumber_ULTRA;
		renderSettings.RTX_ImportanceSamplingTransitionMin = video_settings::s_RTX_ImportanceSamplingTransitionMin_ULTRA;
		renderSettings.RTX_ImportanceSamplingTransitionMax = video_settings::s_RTX_ImportanceSamplingTransitionMax_ULTRA;

		//
		// RTX Reflections
		//
		renderSettings.RTX_EnableReflections = true;
		renderSettings.RTX_EnableTransparentReflections = true;

		//
		// RTX Shadows
		//
		renderSettings.RTX_EnableDirectionalShadow = true;

		return renderSettings;
	}
	GlobalRenderingSettings GetRenderSettings_CINEMATIC()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_RTXULTRA();

		// NOTE: we pull RTXUltra and disable ray tracing so that other RTX settings are filled in and
		//		 in case of CINEMATIC_RTX/CINEMATICEXR_RTX we just toggle single bool.
		renderSettings.allowRaytracing = false;

		renderSettings.MirrorFinalResolutionScale = video_settings::s_MirrorFinalResolutionScale_HIGH;
		renderSettings.MirrorInternalResolutionScale = video_settings::s_MirrorInternalResolutionScale_HIGH;

		renderSettings.DistantShadowmapResolution = video_settings::s_DistantShadowmapResolution_HIGH;
		renderSettings.ShadowCascadeResolution = video_settings::s_ShadowCascadeResolution_HIGH;
		renderSettings.UnrestrictedShadowCubeUpdate = true;

		renderSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_INSANE;

		renderSettings.UseMaxSkeletalLODOnEverything = true;
		renderSettings.SyncLipsyncToAudio = false;
		renderSettings.forceHighestLOD = true;

		renderSettings.allowChromaticAberration = false;
		renderSettings.AllowFilmGrain = false;

		return renderSettings;
	}
	GlobalRenderingSettings GetRenderSettings_CINEMATIC_RTX()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_CINEMATIC();

		//
		// Enable ray tracing only when it is supported by the hardware.
		//
		const Bool rtSupported = GRender->GetHardwareRayTracingSupport();
		renderSettings.allowRaytracing = rtSupported;

		return renderSettings;
	}
	GlobalRenderingSettings GetRenderSettings_CINEMATICEXR()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_CINEMATIC();
		renderSettings.TextureMipBias = -2.0f;
		renderSettings.AllowColorCorrection = false;
		renderSettings.AllowFilmGrain = false;
		renderSettings.allowTXAA = false;
		renderSettings.allowMotionBlur = false;
		renderSettings.allowDOF = false;
		return  renderSettings;
	}
	GlobalRenderingSettings GetRenderSettings_CINEMATICEXR_RTX()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_CINEMATICEXR();

		//
		// Enable ray tracing only when it is supported by the hardware.
		//
		const Bool rtSupported = GRender->GetHardwareRayTracingSupport();
		renderSettings.allowRaytracing = rtSupported;

		return renderSettings;
	}
	GlobalRenderingSettings GetRenderSettings_CONSOLE()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_HIGH();

		renderSettings.ForceInternalResolutionScaling = true;

		renderSettings.AnisotropyLevel = 4;
		renderSettings.ShadowLODBias = UINT32_MAX;	// On consoles processing vertices sucks. Choose worst LOD at all times.
		renderSettings.DistantShadowmapResolution = video_settings::s_DistantShadowmapResolution_LOW;

		// we don't have memory bandwidth on consoles for that
		renderSettings.UseHighPrecisionRenderTargets = false;

		renderSettings.AllowFilmGrain = true;
		renderSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_MEDIUM;
		renderSettings.allowChromaticAberration = true;
		renderSettings.allowContactShadows_Global = true;
		renderSettings.allowContactShadows_Local = true;
		renderSettings.allowAutoSpawnSystemShadows = false;
		renderSettings.allowMirrorMaterial = true;

		renderSettings.DynamicDecalsChunkPerTargetLimit = 10;
		renderSettings.ConeAOQuality = video_settings::s_ConeAOQuality_LOW;
		// Temporarily make it easier for console to keep up.

		renderSettings.allowSSR_Planar	= false; // Temnporarly disable on console. Fix Meantime

		renderSettings.VolFogTileSize	= video_settings::s_VolumetricFogTileSize_MEDIUM;
		renderSettings.VolFogNumSlices	= video_settings::s_VolumetricFogNumSlices_MEDIUM;
		renderSettings.VolFogNumSamples	= video_settings::s_VolumetricFogNumSamples_MEDIUM;

		renderSettings.SubsurfaceQualityProfileIndex = video_settings::s_SubsurfaceQualityProfileIndex_MEDIUM;
#ifdef RED_PLATFORM_DURANGO
		renderSettings.NumShadowCascades = video_settings::s_NumShadowCascades_LOW;
#else
		renderSettings.NumShadowCascades = video_settings::s_NumShadowCascades_MEDIUM;
#endif

		using namespace rend::settings;
		Set< InGameEntry::LocalShadowsQuality, InGameOption::Medium >( renderSettings );	// 512x512, 3 refreshes per frame.
		Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Medium >( renderSettings );

		return renderSettings;
	}

	GlobalRenderingSettings GetRenderSettings_CONSOLEPRO()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_CONSOLE();

		// NOTE:Keep in mind, that this quality level is actually NOT used on PS4 Pro
		// Hence we can allow ourselves to use larger resolutions - it will only apply to stronger Xbox hardware (Scorpio+)
		renderSettings.ScreenSpaceReflectionLevel	= video_settings::s_ScreenSpaceReflectionLevel_MEDIUM;
		renderSettings.DistantShadowmapResolution	= video_settings::s_DistantShadowmapResolution_HIGH;
		renderSettings.LocalShadowsResolution		= 1024;
		renderSettings.ShadowLODBias				= 1;

#ifdef RED_PLATFORM_DURANGO
		// for base Durango, we lowered num shadow cascades to LOW, so have to bump it here
		renderSettings.NumShadowCascades = video_settings::s_NumShadowCascades_MEDIUM;
#endif

		return renderSettings;
	}

	GlobalRenderingSettings GetRenderSettings_CONSOLEEARLYNEXTGEN_PERFORMANCE()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_CONSOLEPRO();

		// !!!!! NOTE !!!!!
		// WHENEVER ANOTHER OPTION GETS MODIFIED HERE, IT NEEDS TO BE REFLECTED IN renderInGameSettings.cpp - OnConsoleSettingsChanged.

		renderSettings.ShadowCascadeResolution = video_settings::s_ShadowCascadeResolution_HIGH;

		return renderSettings;
	}

	GlobalRenderingSettings GetRenderSettings_CONSOLEEARLYNEXTGEN_QUALITY()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_CONSOLEEARLYNEXTGEN_PERFORMANCE();

		// !!!!! NOTE !!!!!
		// WHENEVER ANOTHER OPTION GETS MODIFIED HERE, IT NEEDS TO BE REFLECTED IN renderInGameSettings.cpp - OnConsoleSettingsChanged.

		renderSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_ULTRA;

		renderSettings.AnisotropyLevel = 16;
		renderSettings.ShadowLODBias = 0;	// NO bias whatsoever

		renderSettings.VolFogTileSize = video_settings::s_VolumetricFogTileSize_HIGH;
		renderSettings.VolFogNumSlices = video_settings::s_VolumetricFogNumSlices_HIGH;
		renderSettings.VolFogNumSamples = video_settings::s_VolumetricFogNumSamples_HIGH;

		renderSettings.ConeAOQuality = video_settings::s_ConeAOQuality_HIGH;
		renderSettings.MotionBlurQuality = 1;
		rend::settings::Set< rend::settings::InGameEntry::LocalShadowsQuality, rend::settings::InGameOption::High >( renderSettings );

		return renderSettings;
	}

	GlobalRenderingSettings GetRenderSettings_GEFORCENOW()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_HIGH();
		renderSettings.AllowFilmGrain = false; // Can be bad for video compression
		renderSettings.MirrorFinalResolutionScale = video_settings::s_MirrorFinalResolutionScale_HIGH;
		renderSettings.MirrorInternalResolutionScale = video_settings::s_MirrorInternalResolutionScale_HIGH;

		renderSettings.allowChromaticAberration = true; // ULTRA
		renderSettings.allowContactShadows_Global = true; // ULTRA
		renderSettings.allowContactShadows_Local = true; // ULTRA
		renderSettings.allowMirrorMaterial = true; // ULTRA


		//
		// Enable ray tracing only when it is supported by the hardware.
		//
		const Bool rtSupported = GRender->GetHardwareRayTracingSupport();
		renderSettings.allowRaytracing = rtSupported;

		renderSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_ULTRA; // ULTRA

		renderSettings.DynamicDecalsChunkPerTargetLimit = 10; // CONSOLE

		renderSettings.ConeAOQuality = video_settings::s_ConeAOQuality_HIGH; // ULTRA

		renderSettings.NumShadowCascades = video_settings::s_NumShadowCascades_LOW;

		return renderSettings;
	}
	GlobalRenderingSettings GetRenderSettings_ICONS_GENERATION()
	{
		GlobalRenderingSettings renderSettings = GetRenderSettings_CINEMATIC();
		renderSettings.AllowFilmGrain = false;
		renderSettings.allowVolumetricFog = false;
		renderSettings.allowDistantVolFog = false;
		renderSettings.allowVolumetricClouds = false;
		renderSettings.allowDistantFog = false;
		renderSettings.allowBloom = false;
		renderSettings.allowImageBasedFlares = false;
		renderSettings.allowDOF = false;
		renderSettings.allowMotionBlur = false;
		renderSettings.allowChromaticAberration = false;
		renderSettings.allowTXAA = false;
		renderSettings.allowFXAA = false;
		return renderSettings;
	}

	GlobalRenderingSettings GetRenderSettings_SAFEMODE()
	{
		GlobalRenderingSettings retSettings;
		retSettings.SafeMode = true;
		retSettings.allowDistantShadows = false;
		retSettings.allowRainMap = false;
		retSettings.allowAtmosphere = false;
		retSettings.allowReflectionProbes = false;
		retSettings.allowMirrorMaterial = false;
		retSettings.allowGI = false;
		return retSettings;
	}
	GlobalRenderingSettings GetRenderSettings_AUTO( const MemoryInfo& mem )
	{
		GlobalRenderingSettings outSettings = {};
		{
			const Uint32 gfxQuality = DetectQualityLevel( mem );
			GetSettingsForQualityLevel( gfxQuality, outSettings );
		}
		return outSettings;
	}

//////////////////////////////////////////////////////////////////////////
	GlobalTextureSettings::GlobalTextureSettings()
	{
		NumMipsToDrop = 0;
#ifndef RED_PLATFORM_CONSOLE
		UseConsoleBias = false;
#endif
	}

#ifdef RED_PLATFORM_WINPC
	void GlobalTextureSettings::UpdateWithInGameVideoSettings()
	{
		const TextureQualityLevel tql = TextureQualityFromInGameSettings();
		SelectTextureQualityLevel( ( Uint32 )tql, Memory );
	}
#endif

	GlobalTextureSettings GetTextureSettings_Console()
	{
		GlobalTextureSettings settings;
		settings.NumMipsToDrop = 0;
#ifndef RED_PLATFORM_CONSOLE
		settings.UseConsoleBias = true;
#endif
		return settings;
	}

	GlobalTextureSettings GetTextureSettings_ConsoleSafe()
	{
		GlobalTextureSettings settings;
		settings.NumMipsToDrop = 1;
#ifndef RED_PLATFORM_CONSOLE
		settings.UseConsoleBias = true;
#endif
		return settings;
	}

	GlobalTextureSettings GetTextureSettings_LOW()
	{
		GlobalTextureSettings settings;
		settings.NumMipsToDrop = 2;
		return settings;
	}
	GlobalTextureSettings GetTextureSettings_MEDIUM()
	{
		GlobalTextureSettings settings;
		settings.NumMipsToDrop = 1;
		return settings;
	}
	GlobalTextureSettings GetTextureSettings_HIGH()
	{
		return GlobalTextureSettings();
	}
	GlobalTextureSettings GetTextureSettings_SAFEMODE()
	{
		GlobalTextureSettings settings = GetTextureSettings_LOW();
		settings.NumMipsToDrop = 5;
		return settings;
	}
	GlobalTextureSettings GetTextureSettings_AUTO( const MemoryInfo& mem )
	{
		if( mem.MemoryBudget_GPU < RED_GIGA_BYTE(2) )			return GetTextureSettings_SAFEMODE();
		if( mem.MemoryBudget_GPU < RED_GIGA_BYTE(4) )			return GetTextureSettings_LOW();
		if( mem.MemoryBudget_GPU < RED_GIGA_BYTE(6) )			return GetTextureSettings_MEDIUM();
		// #TODO: Some way to select RTX. Except we're in renderData, so we can't just ask GpuApi.
		return GetTextureSettings_HIGH();
	}
	//////////////////////////////////////////////////////////////////////////
	// Selecting quality level
	Bool GetSettingsForQualityLevel( Uint32 qualityLevel, GlobalRenderingSettings& outSettings )
	{
		auto oldMem = outSettings.Memory;
		switch ( (GraphicsQualityLevel)qualityLevel )
		{
		case GraphicsQualityLevel::Low:									outSettings = GetRenderSettings_LOW();			break;
		case GraphicsQualityLevel::Medium:								outSettings = GetRenderSettings_MEDIUM();		break;
		case GraphicsQualityLevel::High:								outSettings = GetRenderSettings_HIGH();			break;
		case GraphicsQualityLevel::Ultra:								outSettings = GetRenderSettings_ULTRA();		break;
		case GraphicsQualityLevel::RTXMedium:							outSettings = GetRenderSettings_RTXMEDIUM();	break;
		case GraphicsQualityLevel::RTXUltra:							outSettings = GetRenderSettings_RTXULTRA();		break;
		case GraphicsQualityLevel::Cinematic:							outSettings = GetRenderSettings_CINEMATIC();	break;
		case GraphicsQualityLevel::Cinematic_RTX:						outSettings = GetRenderSettings_CINEMATIC_RTX();	break;
		case GraphicsQualityLevel::CinematicEXR:						outSettings = GetRenderSettings_CINEMATICEXR(); break;
		case GraphicsQualityLevel::CinematicEXR_RTX:					outSettings = GetRenderSettings_CINEMATICEXR_RTX(); break;
		case GraphicsQualityLevel::SafeMode:							outSettings = GetRenderSettings_SAFEMODE();		break;
		case GraphicsQualityLevel::Console:								outSettings = GetRenderSettings_CONSOLE();		break;
		case GraphicsQualityLevel::ConsolePro:							outSettings = GetRenderSettings_CONSOLEPRO();	break;
		case GraphicsQualityLevel::ConsoleEarlyNextGen:					outSettings = GetRenderSettings_CONSOLEEARLYNEXTGEN_PERFORMANCE();	break;
		case GraphicsQualityLevel::ConsoleEarlyNextGenQuality:			outSettings = GetRenderSettings_CONSOLEEARLYNEXTGEN_QUALITY();	break;
		case GraphicsQualityLevel::GeForceNow:							outSettings = GetRenderSettings_GEFORCENOW();	break;
		case GraphicsQualityLevel::IconsGeneration:						outSettings = GetRenderSettings_ICONS_GENERATION();		break;
		case GraphicsQualityLevel::Auto:								outSettings = GetRenderSettings_AUTO(outSettings.Memory); break;
#ifdef RED_PLATFORM_WINPC
		//
		// DO nothing for these. They are handled separately and are kinda hacky.
		//
		case GraphicsQualityLevel::FromUserSettings: break;
		case GraphicsQualityLevel::Custom: break;
#endif
		default:
			RED_FATAL( "Unknown quality level!" );
			return false;
			break;
		}
		outSettings.Memory = oldMem;

		return true;
	}


	// Forward declared. Go down to video settings section.
	namespace video_settings
	{
		namespace from_global_to_in_game
		{
#ifdef RED_PLATFORM_WINPC
			RED_INLINE void QuickPreset( GraphicsQualityLevel qualityLevel );
#endif
			RED_INLINE void TextureQuality( InGameConfig::Group& group, TextureQualityLevel tql, InGameConfig::Source source );
		}
	}
	
	// This casually looking function will actually change the quality level for the whole flippin engine. Be warned.
	// The settings are completely untweakable after they are set. There are no user overrides or anything like that.
	// You can toggle some thing on and off, but not here. Here you are guaranteed, that if you set Medium, you get Medium.
	// In case of running the game it will also map selected settings to in-game settings menu.
	void SelectQualityLevel( Uint32 qualityLevel, const MemoryInfo& mem )
	{
		RED_ASSERT( qualityLevel < ( Uint32 ) GraphicsQualityLevel::Count, "Quality level outside of range" );

		//
		// Can't change settings while we're currently rendering, so just make sure stuff is finished.
		//
		if ( GRender != nullptr )
		{
			GRender->Flush();
		}
		if ( mem.MemoryBudget_GPU && mem.MemoryBudget_RAM )
		{
			GGlobalRenderingSettings.Memory = mem;
		}

#ifdef RED_PLATFORM_WINPC
		//
		// Console flag or not - load everything from InGameConfigSystem if it is available
		// Some of the variables needs to be populated anyway. We do this only on PC.
		//
		if ( rend::settings::Valid() )
		{
			const auto& igcSys = InGameConfig::System::GetInstance();
			if ( igcSys.IsFirstRun() )
				rend::settings::callbacks::OnDetectCapabilitiesOnFreshStart_GraphicsTab( mem );
			else
				rend::settings::callbacks::OnLoadQuickPresetFromUserSettings( mem );
		}
#endif

#ifdef RED_PLATFORM_WINPC
		//
		// Now, If we had qualityLevel flag specified - override the preset.
		//
		const Bool loadedFromCmdFlag = ( GraphicsQualityLevel ) qualityLevel != GraphicsQualityLevel::FromUserSettings;
		if ( loadedFromCmdFlag )
		{
			GetSettingsForQualityLevel( qualityLevel, GGlobalRenderingSettings );
			if ( rend::settings::Valid() )
			{
				rend::settings::graphics_tab::SetQuickPreset( ( GraphicsQualityLevel ) qualityLevel );
			}

		}

		//
		// Register ray tracing for crash dumps on PC (required by the editor).
		//
		crash::rtxEnabled.Set( GGlobalRenderingSettings.allowRaytracing );
#else
		GetSettingsForQualityLevel( qualityLevel, GGlobalRenderingSettings );
#endif
	}


#ifdef RED_PLATFORM_WINPC
	TextureQualityLevel TextureQualityFromInGameSettings()
	{
		auto& sys = InGameConfig::System::GetInstance();
		const auto mode = sys.GetMode();

		//
		// We are interested only in mode that supports in game config.
		//
		if ( rend::settings::Valid() )
		{
			const auto loadStatus = sys.GetUserSettingsLoadStatus();
			RED_ASSERT( loadStatus != InGameConfig::UserSettingsLoadStatus::NotLoaded, "At this point on PC platform in game settings must be loaded" );

			//
			// If settings were properly loaded - pull saved data from there and apply it to engine init params.
			//
			if ( loadStatus == InGameConfig::UserSettingsLoadStatus::Loaded || loadStatus == InGameConfig::UserSettingsLoadStatus::ImportedFromOldVersion )
			{
				auto& group = sys.GetGroup( RED_NAME_CONSTEXPR( "/graphics" ) );
				auto& texQualityVar = group.GetVarListString( RED_NAME_CONSTEXPR( "TextureQuality" ) );

				const Int32 indexRequestedFromUserSettings = texQualityVar.GetRequestedIndex();
				RED_ASSERT( indexRequestedFromUserSettings >= 0 && indexRequestedFromUserSettings <= 2, "Texture quality handles only Low, Medium, High options" );

				if ( indexRequestedFromUserSettings == 0 )
					return Config::TextureQualityLevel::Low;
				else if ( indexRequestedFromUserSettings == 1 )
					return Config::TextureQualityLevel::Medium;
				else if ( indexRequestedFromUserSettings == 2 )
					return Config::TextureQualityLevel::High;

				RED_HALT( "Never should come here." );
			}
		}

		return TextureQualityLevel::High;
	}
#endif

	Uint32 DetectQualityLevel( const MemoryInfo& mem )
	{
		if( mem.MemoryBudget_RAM < RED_GIGA_BYTE( 1 ) || mem.MemoryBudget_GPU < RED_GIGA_BYTE( 2 ) )	return ( Uint32 ) Config::GraphicsQualityLevel::Low;
		if( mem.MemoryBudget_RAM < RED_GIGA_BYTE( 2 ) || mem.MemoryBudget_GPU < RED_GIGA_BYTE( 4 ) )	return ( Uint32 ) Config::GraphicsQualityLevel::Medium;
		if( mem.MemoryBudget_RAM < RED_GIGA_BYTE( 3 ) || mem.MemoryBudget_GPU < RED_GIGA_BYTE( 6 ) )	return ( Uint32 ) Config::GraphicsQualityLevel::High;

		const Bool rtSupported = GRender->GetHardwareRayTracingSupport();
		if( rtSupported && mem.MemoryBudget_GPU >= RED_GIGA_BYTE( 7 ) )
			return ( Uint32 ) Config::GraphicsQualityLevel::RTXUltra;
		if( rtSupported && mem.MemoryBudget_GPU >= RED_GIGA_BYTE( 6 ) )
			return ( Uint32 ) Config::GraphicsQualityLevel::RTXMedium;

		return ( Uint32 ) Config::GraphicsQualityLevel::Ultra;
	}

	Bool GetSettingsForTextureQualityLevel( Uint32 qualityLevel, GlobalTextureSettings& outSettings )
	{
		auto oldMem = outSettings.Memory;
		switch ( (TextureQualityLevel)qualityLevel )
		{
		case TextureQualityLevel::Console:		outSettings = GetTextureSettings_Console();			break;
		case TextureQualityLevel::ConsoleSafe:  outSettings = GetTextureSettings_ConsoleSafe();		break;
		case TextureQualityLevel::Low:			outSettings = GetTextureSettings_LOW();				break;
		case TextureQualityLevel::Medium:		outSettings = GetTextureSettings_MEDIUM();			break;
		case TextureQualityLevel::High:			outSettings = GetTextureSettings_HIGH();			break;
		case TextureQualityLevel::SafeMode:		outSettings = GetTextureSettings_SAFEMODE();		break;
		case TextureQualityLevel::Auto:			outSettings = GetTextureSettings_AUTO( outSettings.Memory ); break;
		default:
			RED_FATAL( "Unknown quality level!" );
			return false;
		}
		outSettings.Memory = oldMem;
		return true;
	}
	void SelectTextureQualityLevel( Uint32 qualityLevel, const MemoryInfo& mem )
	{
		RED_ASSERT( qualityLevel < (Uint32)TextureQualityLevel::Count, "Quality level outside of range" );
		// Can't change settings while we're currently rendering, so just make sure stuff is finished.
		if ( GRender != nullptr )
		{
			GRender->Flush();
		}
		if( mem.MemoryBudget_GPU && mem.MemoryBudget_RAM )
		{
			GGlobalTextureSettings.Memory = mem;
		}
		GetSettingsForTextureQualityLevel( qualityLevel, GGlobalTextureSettings );

		// We expose texture quality as a setting only on PC.
#ifdef RED_PLATFORM_WINPC
		// Process in-game settings. Texture quality is only possible via main menu.
		auto& sys = InGameConfig::System::GetInstance();
		const auto mode = sys.GetMode();
		if ( rend::settings::Valid() )
		{
			video_settings::from_global_to_in_game::TextureQuality( sys.GetGroup( RED_NAME_CONSTEXPR( "/graphics" ) ), ( TextureQualityLevel )qualityLevel, InGameConfig::Source::PresetCode );
		}
#endif

	}
	// Global settings object makes it available for all systems to query the settings anytime
	GlobalRenderingSettings	GGlobalRenderingSettings = GetRenderSettings_ULTRA();
	GlobalTextureSettings	GGlobalTextureSettings = GetTextureSettings_HIGH();
	//////////////////////////////////////////////////////////////////////////
	// Helpers
	GraphicsQualityLevel QualityLevelNameToEnum( const red::StringView& name )
	{
		if( auto rttiClass = GetRttiSystem().FindEnum( ::GetTypeName< GraphicsQualityLevel >() ) )
		{
			Int64 outVal;
			if( rttiClass->FindValue( name, outVal ) )
			{
				return GraphicsQualityLevel( outVal );
			}
			const auto& options = rttiClass->GetOptions();
			for( Uint32 i = 0; i < options.Size(); ++i )
			{
				if( name.CompareIgnoreCase( options[ i ].AsStringView() ) )
				{
					return GraphicsQualityLevel( rttiClass->GetValues()[ i ] );
				}
			}
		}
		RED_FATAL( "Unknown quality level name!" );
		return GraphicsQualityLevel::High;
	}
	red::StringView EnumToQualityLevelName( GraphicsQualityLevel qlevel )
	{
		auto rttiClass = GetRttiSystem().FindEnum( ::GetTypeName< GraphicsQualityLevel >() );
		RED_FATAL_ASSERT( rttiClass );
		CName outName;
		rttiClass->FindName( (Uint32)qlevel, outName );
		return outName.AsStringView();
	}
	TextureQualityLevel TextureQualityLevelNameToEnum( const red::StringView& name )
	{
		if( auto rttiClass = GetRttiSystem().FindEnum( ::GetTypeName< TextureQualityLevel >() ) )
		{
			Int64 outVal;
			if( rttiClass->FindValue( name, outVal ) )
			{
				return TextureQualityLevel( outVal );
			}
			const auto& options = rttiClass->GetOptions();
			for( Uint32 i = 0; i < options.Size(); ++i )
			{
				if( name.CompareIgnoreCase( options[ i ].AsStringView() ) )
				{
					return TextureQualityLevel( rttiClass->GetValues()[ i ] );
				}
			}
		}
		RED_FATAL( "Unknown quality level name!" );
		return TextureQualityLevel::High;
	}
	red::StringView EnumToTextureQualityLevelName( TextureQualityLevel qlevel )
	{
		auto rttiClass = GetRttiSystem().FindEnum( ::GetTypeName< TextureQualityLevel >() );
		RED_FATAL_ASSERT( rttiClass );
		CName outName;
		rttiClass->FindName( (Uint32)qlevel, outName );
		return outName.AsStringView();
	}

	TConfigVar< Bool >  cvRayTracingOverrideEnable					( "RayTracing", "OverrideEnable",					false, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvRayTracingEnable							( "RayTracing", "Enable",							true, eConsoleVarFlag_Developer);
	TConfigVar< Bool >  cvRayTracingEnableAmbientOcclusion			( "RayTracing", "EnableAmbientOcclusion",			true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvRayTracingEnableDiffuseIllumination		( "RayTracing", "EnableDiffuseIllumination",		true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvRayTracingEnableReflection				( "RayTracing", "EnableReflection",					true, eConsoleVarFlag_Developer);
	TConfigVar< Bool >  cvRayTracingEnableMirrorMaterialReflection	( "RayTracing", "EnableMirrorMaterialReflection",	false, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvRayTracingEnableTransparentReflection		( "RayTracing", "EnableTransparentReflection",		true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvRayTracingEnableDirectionalShadow			( "RayTracing", "EnableDirectionalShadow",			true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvRayTracingEnableShadowCasadesOptimization	( "RayTracing", "EnableShadowCasadesOptimization",	false, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvRayTracingEnableNRD						( "RayTracing", "EnableNRD",						true, eConsoleVarFlag_Developer );
	TConfigVar< Int32 > cvRayTracingAmbientOcclusionRayNumber		( "RayTracing", "AmbientOcclusionRayNumber",		1, eConsoleVarFlag_Developer );
	TConfigVar< Float > cvRayTracingDiffuseIlluminationAOModulation	( "RayTracing", "DiffuseIlluminationAOModulation",	0.4f, eConsoleVarFlag_Developer); // requested by Jakub
	TConfigVar< Int32 > cvRayTracingDebugMode						( "RayTracing", "DebugMode",						0, eConsoleVarFlag_Developer );

	// Below you can find toggles for the features. This way you can switch some things off even if they are enabled in the current performance profile
	// NEVER make them eConsoleVarFlag_Save. We don't want these to save ever. This is purely for debugging and it does not work as a "setting" at all.
	// NOTE: Don't educate people to use those switches! We don't really want them to use them. This is for programmers and QA, to investigate issues easier.
	TConfigVar< Bool >  cvFilmGrain							( "Developer/FeatureToggles", "FilmGrain",						true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvVolumetricFog						( "Developer/FeatureToggles", "VolumetricFog",					true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvVolumetricClouds					( "Developer/FeatureToggles", "VolumetricClouds",				true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvDistantFog						( "Developer/FeatureToggles", "DistantFog",						true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvDistantVolFog						( "Developer/FeatureToggles", "DistantVolFog",					true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvContrastAdaptiveSharpening		( "Developer/FeatureToggles", "ConstrastAdaptiveSharpening",	true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvAntialiasing						( "Developer/FeatureToggles", "Antialiasing",					true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvAntialiasingSuppressed			( "Developer/FeatureToggles", "AntialiasingSuppressed",			false, eConsoleVarFlag_Developer);
	TConfigVar< Bool >  cvSSR								( "Developer/FeatureToggles", "ScreenSpaceReflection",			true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvSSR_Planar						( "Developer/FeatureToggles", "ScreenSpacePlanarReflection",	true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvBloom								( "Developer/FeatureToggles", "Bloom",							true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvImageBasedFlares					( "Developer/FeatureToggles", "ImageBasedFlares",				true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvChromaticAberration				( "Developer/FeatureToggles", "ChromaticAberration",			true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvDepthOfField						( "Developer/FeatureToggles", "DepthOfField",					true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvSSAO								( "Developer/FeatureToggles", "SSAO",							true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvMotionBlur						( "Developer/FeatureToggles", "MotionBlur",						true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvDistantShadows					( "Developer/FeatureToggles", "DistantShadows",					true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvRainMap							( "Developer/FeatureToggles", "RainMap",						true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvLocalShadows						( "Developer/FeatureToggles", "LocalShadows",					true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvDistantGI							( "Developer/FeatureToggles", "DistantGI",						true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvGlobalIllumination				( "Developer/FeatureToggles", "GlobalIllumination",				true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvContactShadows					( "Developer/FeatureToggles", "ContactShadows",					true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvRuntimeTangentUpdate				( "Developer/FeatureToggles", "RuntimeTangentUpdate",			true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >	cvDynamicDecals						( "Developer/FeatureToggles", "DynamicDecals",					true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >	cvHair								( "Developer/FeatureToggles", "Hair",							true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvCharacterSubsurface				( "Developer/FeatureToggles", "CharacterSubsurfaceScattering",	true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvCharacterSubsurfaceTranslucency	( "Developer/FeatureToggles", "CharacterSubsurfaceTranslucency", false, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvCharacterRim						( "Developer/FeatureToggles", "CharacterRimEnhancement",		true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvScreenSpaceUnderwater				( "Developer/FeatureToggles", "ScreenSpaceUnderwater",			true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvScreenSpaceHeatHaze				( "Developer/FeatureToggles", "ScreenSpaceHeatHaze",			true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvCharacterLightBlockers			( "Developer/FeatureToggles", "CharacterLightBlockers",			true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvWeather							( "Developer/FeatureToggles", "Weather",			true, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvScreenSpaceRain					( "Developer/FeatureToggles", "ScreenSpaceRain",	true, eConsoleVarFlag_Developer );

	TConfigVar< Bool >  cvForceVolumeHeuristicRefresh		( "Rendering/Shadows", "CascadesHeuristicForceRefresh", false, eConsoleVarFlag_Developer );
	TConfigVar< Float > cvCascadeShadowReceiverBiasAlongNormal	( "Rendering/Shadows", "CascadeShadowReceiverBiasAlongNormal", 0.02f, eConsoleVarFlag_Developer );
	TConfigVar< Float > cvCascadeRange[MAX_CASCADES] = {
		TConfigVar< Float >( "Rendering/Shadows", "CascadeRange0", 6.f, eConsoleVarFlag_Developer ),
		TConfigVar< Float >( "Rendering/Shadows", "CascadeRange1", 20.f, eConsoleVarFlag_Developer ),
		TConfigVar< Float >( "Rendering/Shadows", "CascadeRange2", 50.f, eConsoleVarFlag_Developer ),
		TConfigVar< Float >( "Rendering/Shadows", "CascadeRange3", 100.f, eConsoleVarFlag_Developer ) };
	TConfigVar< Float > cvCascadeDepthBias[MAX_CASCADES] = {
		TConfigVar< Float >( "Rendering/Shadows", "CascadeDepthBias0", 0.03f, eConsoleVarFlag_Developer ),
		TConfigVar< Float >( "Rendering/Shadows", "CascadeDepthBias1", 0.06f, eConsoleVarFlag_Developer ),
		TConfigVar< Float >( "Rendering/Shadows", "CascadeDepthBias2", 0.12f, eConsoleVarFlag_Developer ),
		TConfigVar< Float >( "Rendering/Shadows", "CascadeDepthBias3", 0.24f, eConsoleVarFlag_Developer ) };

	TConfigVar< Float > cvDissolveTime						( "Rendering", "DissolveTime", 0.5f, 0.001f, 5.0f, eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvUseSkinningLOD                    ( "Rendering", "UseSkinningLOD", true, eConsoleVarFlag_Developer );

	/***** Hair *****/
	TConfigVar< Float >  cvHairRoughnessFactor				( "Editor/Characters/Hair",					"RoughnessFactor",					1.0f,	0.01f,	3.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairAlbedoMultiplier				( "Editor/Characters/Hair",					"AlbedoMultiplier",					0.6f,	0.f,	10.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHair_AdditionalAreaRoughness		( "Editor/Characters/Hair",					"AdditionalAreaRoughness",			0.1f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvHair_useReferenceImpl				( "Editor/Characters/Hair",					"UseReferenceImplementation",		false,					eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvHair_useGlobalContactShadows		( "Editor/Characters/Hair",					"UseGlobalContactShadowsOnHair",	true,					eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvHair_useLocalContactShadows		( "Editor/Characters/Hair",					"UseLocalContactShadowsOnHair",		true,					eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHair_contactShadowsClamp			( "Editor/Characters/Hair",					"ContactShadowClamp",				0.35f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHair_SpecRandomMin				( "Editor/Characters/Hair",					"SpecularRandom_Min",				-0.3f,	-0.5f,	0.5f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHair_SpecRandomMax				( "Editor/Characters/Hair",					"SpecularRandom_Max",				0.0f,	-0.5f,	0.5f,	eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvHair_debugSwitch1					( "Editor/Characters/Hair/Debug",			"DebugSwitch1",						false,					eConsoleVarFlag_Developer );
	TConfigVar< Bool >  cvHair_debugSwitch2					( "Editor/Characters/Hair/Debug",			"DebugSwitch2",						false,					eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairR_Local						( "Editor/Characters/Hair/LocalLight",		"R",								0.350f, 0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairTT_Local						( "Editor/Characters/Hair/LocalLight",		"TT",								0.005f, 0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairTRT_Local					( "Editor/Characters/Hair/LocalLight",		"TRT",								0.800f, 0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatter_Local				( "Editor/Characters/Hair/LocalLight",		"MultiScatter",						0.47f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatterDepth_Local			( "Editor/Characters/Hair/LocalLight",		"ScatterDepth",						0.8f,	0.f,	5.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairR_Global						( "Editor/Characters/Hair/GlobalLight",		"R",								0.3f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairTT_Global					( "Editor/Characters/Hair/GlobalLight",		"TT",								0.005f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairTRT_Global					( "Editor/Characters/Hair/GlobalLight",		"TRT",								0.8f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatter_Global				( "Editor/Characters/Hair/GlobalLight",		"MultiScatter",						0.47f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatterDepth_Global			( "Editor/Characters/Hair/GlobalLight",		"ScatterDepth",						1.25f,	0.f,	5.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairR_EnvProbe					( "Editor/Characters/Hair/EnvProbe",		"R",								0.3f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairTT_EnvProbe					( "Editor/Characters/Hair/EnvProbe",		"TT",								0.005f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairTRT_EnvProbe					( "Editor/Characters/Hair/EnvProbe",		"TRT",								0.8f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatter_EnvProbe				( "Editor/Characters/Hair/EnvProbe",		"MultiScatter",						0.47f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatterDepth_EnvProbe		( "Editor/Characters/Hair/EnvProbe",		"ScatterDepth",						1.3f,	0.f,	5.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatter_Wrap					( "Editor/Characters/Hair/MultiScatter",	"Wrap",								0.35f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatter_DiffuseScatterFactor	( "Editor/Characters/Hair/MultiScatter",	"DiffuseScatterFactor",				0.0f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatter_ShadowFactorExp		( "Editor/Characters/Hair/MultiScatter",	"ShadowFactorExp",					0.37f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairScatter_MaskIntensity		( "Editor/Characters/Hair/MultiScatter",	"Mask_Intensity",					1.0f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairSpec_Wrap					( "Editor/Characters/Hair/Specular",		"Wrap",								0.4f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairSpec_MaskIntensity			( "Editor/Characters/Hair/Specular",		"Mask_Intensity",					1.0f,	0.f,	1.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairAlphaShift_R					( "Editor/Characters/Hair/AlphaShifts",		"R",								-0.083f,-2.f,	2.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairAlphaShift_TT				( "Editor/Characters/Hair/AlphaShifts",		"TT",								0.0f,	-2.f,	2.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHairAlphaShift_TRT				( "Editor/Characters/Hair/AlphaShifts",		"TRT",								0.375f,	-2.f,	2.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHair_TRT_EXPSCALE				( "Editor/Characters/Hair/TRT_Params",		"EXP_SCALE",						17.f,	0.f,	20.f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHair_TRT_EXPBIAS					( "Editor/Characters/Hair/TRT_Params",		"EXP_BIAS",							16.5f,	0.f,	20.f,	eConsoleVarFlag_Developer );

	TConfigVar< Float >  cvHACK_hairFactor0					( "Editor/Characters/Hair/HACKS",			"HACK_Factor0",						66.f,	0.f,	1000.f, eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHACK_hairFactor1					( "Editor/Characters/Hair/HACKS",			"HACK_Factor1",						95.f,	0.f,	1000.f, eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHACK_hairFactor2					( "Editor/Characters/Hair/HACKS",			"HACK_Factor2",						213.f,	0.f,	1000.f, eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvHACK_hairFactor3					( "Editor/Characters/Hair/HACKS",			"HACK_Factor3",						450.f,	0.f,	1000.f, eConsoleVarFlag_Developer );
	TConfigVar< Bool >	 cvHACK_hairModifiedLocalLightIntensity( "Editor/Characters/Hair/HACKS",		"AAAA_HACK_hairModifiedLocalLightIntensity", true,			eConsoleVarFlag_Developer );
	/***** Foliage *****/
	TConfigVar< Float >  cvFoliage_contactShadowClamp		( "Editor/FoliageParameters",				"ContactShadowClamp", 0.6f, 0.f, 1.f, eConsoleVarFlag_Developer );
	/***** Volumetric Fog *****/
	TConfigVar< Float >  cvVolFog_Exponent					( "Editor/VolumetricFog",					"Exponent",		2.f,			1.f,	5.f, eConsoleVarFlag_Developer );
	/***** Dismemberment Decals *****/
	//TConfigVar< Float > cvDismembermentDecalFadeOrigin			("Editor/Dismemberment/Decals",				"FadeOrigin",						0.7f, 0.0f, 1.0f, eConsoleVarFlag_Developer );
	//TConfigVar< Float > cvDismembermentDecalFadePower			("Editor/Dismemberment/Decals",					"FadePower",						1.0f, 1.0f, 6.0f, eConsoleVarFlag_Developer );
	
	/***** Volumetric Clouds *****/
	TConfigVar< Float > cvVolumetricCloudsDetailNoiseScale		( "Editor/VolumetricClouds",			"DetailNoiseScale", 0.75f,	eConsoleVarFlag_Developer );
	TConfigVar< Float > cvVolumetricCloudsNoiseScale			( "Editor/VolumetricClouds",			"NoiseScale",		1.1f,	eConsoleVarFlag_Developer );
	TConfigVar< Float > cvVolumetricCloudsCoverageScale			( "Editor/VolumetricClouds",			"CoverageScale",	1.3f,	eConsoleVarFlag_Developer );

	/***** Rim Enhancement *****/
	TConfigVar< Float >  cvCharacterRim_Skin_Specular			("Editor/Characters/RimEnhancement/Skin",		"SpecularCoefficient",				1.0f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_Skin_Fresnel			("Editor/Characters/RimEnhancement/Skin",		"FresnelCoefficient",				1.5f, 0.0f, 100.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_Skin_ConstOffset		("Editor/Characters/RimEnhancement/Skin",		"ConstOffsetCoefficient",			0.0f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);

	TConfigVar< Float >  cvCharacterRim_Foliage_Specular		("Editor/Characters/RimEnhancement/Foliage",	"Foliage_SpecularCoefficient",		1.2f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_Foliage_Fresnel			("Editor/Characters/RimEnhancement/Foliage",	"Foliage_FresnelCoefficient",		2.5f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_Foliage_ConstOffset		("Editor/Characters/RimEnhancement/Foliage",	"Foliage_ConstOffsetCoefficient",	0.0f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);

	TConfigVar< Float >  cvCharacterRim_Weapon_Specular			("Editor/Characters/RimEnhancement/Weapon",		"Weapon_SpecularCoefficient",		1.4f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_Weapon_Fresnel			("Editor/Characters/RimEnhancement/Weapon",		"Weapon_FresnelCoefficient",		2.4f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_Weapon_ConstOffset		("Editor/Characters/RimEnhancement/Weapon",		"Weapon_ConstOffsetCoefficient",	0.0f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);

	TConfigVar< Float >  cvCharacterRim_Standard_Specular		("Editor/Characters/RimEnhancement/Standard",	"Standard_SpecularCoefficient",		1.4f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_Standard_Fresnel		("Editor/Characters/RimEnhancement/Standard",	"Standard_FresnelCoefficient",		1.5f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_Standard_ConstOffset	("Editor/Characters/RimEnhancement/Standard",	"Standard_ConstOffsetCoefficient",	0.0f, 0.0f, 10.0f,	eConsoleVarFlag_Developer);

	TConfigVar< Float >  cvCharacterRim_Character_Fresnel		("Editor/Characters/RimEnhancement",			"GlobalCharacterFresnel",			1.5f, 0.0f, 100.0f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvCharacterRim_RoughnessFactor_Scale	("Editor/Characters/RimEnhancement",			"RoughnessFactor_Scale",			1.0f, 0.0f, 10.0f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvCharacterRim_RoughnessFactor_Bias	("Editor/Characters/RimEnhancement",			"RoughnessFactor_Bias",				0.0f, 0.0f, 10.0f,	eConsoleVarFlag_Developer );
	TConfigVar< Float >  cvCharacterRim_LightBlockerInfluence	("Editor/Characters/RimEnhancement",			"LightBlockerInfluence",			0.7f, 0.0f, 1.0f,	eConsoleVarFlag_Developer );

	/***** Rim Enhancement RayTracing *****/
	TConfigVar< Float >  cvCharacterRim_RayTracing_Skin_Specular		("Editor/Characters/RimEnhancement_RayTracing/Skin", "SpecularCoefficient",					1.0f, 0.0f, 10.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_Skin_Fresnel			("Editor/Characters/RimEnhancement_RayTracing/Skin", "FresnelCoefficient",					1.5f, 0.0f, 100.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_Skin_ConstOffset		("Editor/Characters/RimEnhancement_RayTracing/Skin", "ConstOffsetCoefficient",				0.0f, 0.0f, 10.0f, eConsoleVarFlag_Developer);

	TConfigVar< Float >  cvCharacterRim_RayTracing_Foliage_Specular		("Editor/Characters/RimEnhancement_RayTracing/Foliage", "Foliage_SpecularCoefficient",		1.2f, 0.0f, 10.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_Foliage_Fresnel		("Editor/Characters/RimEnhancement_RayTracing/Foliage", "Foliage_FresnelCoefficient",		2.5f, 0.0f, 10.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_Foliage_ConstOffset	("Editor/Characters/RimEnhancement_RayTracing/Foliage", "Foliage_ConstOffsetCoefficient",	0.0f, 0.0f, 10.0f, eConsoleVarFlag_Developer);

	TConfigVar< Float >  cvCharacterRim_RayTracing_Weapon_Specular		("Editor/Characters/RimEnhancement_RayTracing/Weapon", "Weapon_SpecularCoefficient",		1.4f, 0.0f, 10.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_Weapon_Fresnel		("Editor/Characters/RimEnhancement_RayTracing/Weapon", "Weapon_FresnelCoefficient",			2.4f, 0.0f, 10.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_Weapon_ConstOffset	("Editor/Characters/RimEnhancement_RayTracing/Weapon", "Weapon_ConstOffsetCoefficient",		0.0f, 0.0f, 10.0f, eConsoleVarFlag_Developer);

	TConfigVar< Float >  cvCharacterRim_RayTracing_Standard_Specular	("Editor/Characters/RimEnhancement_RayTracing/Standard", "Standard_SpecularCoefficient",	1.4f, 0.0f, 10.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_Standard_Fresnel		("Editor/Characters/RimEnhancement_RayTracing/Standard", "Standard_FresnelCoefficient",		1.5f, 0.0f, 10.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_Standard_ConstOffset	("Editor/Characters/RimEnhancement_RayTracing/Standard", "Standard_ConstOffsetCoefficient", 0.0f, 0.0f, 10.0f, eConsoleVarFlag_Developer);

	TConfigVar< Float >  cvCharacterRim_RayTracing_Character_Fresnel	("Editor/Characters/RimEnhancement_RayTracing", "GlobalCharacterFresnel",					1.5f, 0.0f, 100.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_RoughnessFactor_Scale("Editor/Characters/RimEnhancement_RayTracing", "RoughnessFactor_Scale",					1.0f, 0.0f, 10.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_RoughnessFactor_Bias	("Editor/Characters/RimEnhancement_RayTracing", "RoughnessFactor_Bias",						0.0f, 0.0f, 10.0f, eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvCharacterRim_RayTracing_LightBlockerInfluence("Editor/Characters/RimEnhancement_RayTracing", "LightBlockerInfluence",					0.7f, 0.0f, 1.0f, eConsoleVarFlag_Developer);

	/***** Light Blockers *****/
	// max limit must match LIGHT_BLOCKERS_CAPACITY in renderCollector.h
	TConfigVar< Int32 >  cvCharacterLightBlockers_Limit			("Editor/Characters/LightBlockers",				"ClosestBlockersLimit",				16,		0,	64,		eConsoleVarFlag_Developer );

	/***** Skin *****/
	TConfigVar< Float >  cvSkin_SpecularTint_R					("Editor/Characters/Skin",						"SubsurfaceSpecularTint_R",			0.125f, 0.f, 1.f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvSkin_SpecularTint_G					("Editor/Characters/Skin",						"SubsurfaceSpecularTint_G",			0.260f, 0.f, 1.f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvSkin_SpecularTint_B					("Editor/Characters/Skin",						"SubsurfaceSpecularTint_B",			0.290f, 0.f, 1.f,	eConsoleVarFlag_Developer);
	TConfigVar< Float >  cvSkin_SpecularTint_Weight				("Editor/Characters/Skin",						"SubsurfaceSpecularTintWeight",		0.3f, 0.f, 1.f,		eConsoleVarFlag_Developer);

	/***** Shader Compiler *****/
	TConfigVar< Bool >	cvShaderCompilerOptimize			( "Editor/ShaderCompiler", "Optimize",			true,	eConsoleVarFlag_Save );
	TConfigVar< Bool >	cvShaderCompilerDumpAssembly		( "Editor/ShaderCompiler", "DumpAssembly",		false,	eConsoleVarFlag_Save );
	TConfigVar< Bool >	cvShaderCompilerForceRecompile		( "Editor/ShaderCompiler", "ForceRecompile",	false,	eConsoleVarFlag_Save );
	TConfigVar< Bool >	cvShaderCompilerUseLocalCompiler	( "Editor/ShaderCompiler", "UseLocalCompiler",	false,	eConsoleVarFlag_Save );

	/***** Level of Detail *****/
	TConfigVar<Float> cvDecalsHideDistance				( "LevelOfDetail",	 "DecalsHideDistance",				40.0f,		eConsoleVarFlag_Save );
	TConfigVar<Float> cvDynDecalsHideDistance			( "LevelOfDetail",	 "DynamicDecalsHideDistance",		20.0f,		eConsoleVarFlag_Save );
	/***** Visuals *****/
	TConfigVar<Float>	cvGamma						( "Visuals", "GammaValue",			1.0f,	0.1f, 10.0f,	eConsoleVarFlag_Save );
	TConfigVar<Float>	cvMidPoint					( "Visuals", "MidPoint",			1.0f,	0.1f, 10.0f,	eConsoleVarFlag_Save );
	TConfigVar<Float>	cvMotionBlurScale   		( "Visuals", "MotionBlurScale",		1.0f,   0.0f, 1.0f,		eConsoleVarFlag_Save );
	TConfigVar<Float>	cvDebugVelocityBufferScale	( "Visuals", "VelocityBufferScale",	100.0f, 0.0f, 100.0f,	eConsoleVarFlag_Developer );
	/***** DLSS *****/
	TConfigVar< Bool >	cvDLSSEnable		( "DLSS", "Enable", false, eConsoleVarFlag_Developer );
	TConfigVar< Int32 >	cvDLSSQuality		( "DLSS", "Quality", 2, 0, 4, eConsoleVarFlag_Developer );
	TConfigVar< Float >	cvDLSSSharpness		( "DLSS", "Sharpness", 0.0f, 0.0f, 1.0f, eConsoleVarFlag_Developer );

	/***** Helpers *****/
	namespace Helper
	{
		String PackResolutionAsString( Int32 width, Int32 height )
		{
			return String::Printf(rend::PoolRenderingCore(), "%dx%d", width, height );
		}
		Bool ExtractDisplayModeValues( const String& mode, Int32& width, Int32& height )
		{
			Int32 tempWidth = width;
			Int32 tempHeight = height;
			DynArray<String> splitted = mode.Split( "x" );
			Bool isCountValid = splitted.Size() == 2;
			Bool isWidthValid = false;
			Bool isHeightValid = false;
			Bool isResolutionValid = false;
			if( isCountValid == true )
			{
				isWidthValid = FromString( splitted[0], tempWidth );
				isHeightValid = FromString( splitted[1], tempHeight );
				isResolutionValid = (tempWidth > 0) && (tempHeight > 0);
			}
			if( !(isCountValid && isWidthValid && isHeightValid && isResolutionValid) )
			{
				RED_LOG_WARNING( "Renderer: Resolution setting is missing from config. Falling back to native resolution." );
				return false;
			}
			width = tempWidth;
			height = tempHeight;
			return true;
		}
		String PackDisplayModeValues( const Int32 width, const Int32 height )
		{
			return String::Printf(rend::PoolRenderingCore(), "%ix%i", width, height );
		}
		Bool IsAntialiasOn()
		{
			return cvAntialiasing.Get() && !cvAntialiasingSuppressed.Get();
		}
	}


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// G R A P H I C S  S E T T I N G S

	// Groups
	static CName s_inGameConfigGroup( RED_NAME_CONSTEXPR( "/graphics" ) );
	static CName s_inGameConfigGroup_Basic( RED_NAME_CONSTEXPR( "/graphics/basic" ) );
	static CName s_inGameConfigGroup_Advanced( RED_NAME_CONSTEXPR( "/graphics/advanced" ) );
	static CName s_inGameConfigGroup_RayTracing( RED_NAME_CONSTEXPR( "/graphics/raytracing" ) );

	// Variables
	static CName s_inGameConfigVar_FilmGrain( RED_NAME_CONSTEXPR( "FilmGrain" ) );
	static CName s_inGameConfigVar_ChromaticAberation( RED_NAME_CONSTEXPR( "ChromaticAberration" ) );
	static CName s_inGameConfigVar_DepthOfField( RED_NAME_CONSTEXPR( "DepthOfField" ) );
	static CName s_inGameConfigVar_LensFlares( RED_NAME_CONSTEXPR( "LensFlares" ) );
	static CName s_inGameConfigVar_MotionBlur( RED_NAME_CONSTEXPR( "MotionBlur" ) );

#ifdef RED_PLATFORM_WINPC
	// RTX
	static CName s_inGameConfigVar_RayTracing( RED_NAME_CONSTEXPR( "RayTracing" ) );
	static CName s_inGameConfigVar_RayTracedReflections( RED_NAME_CONSTEXPR( "RayTracedReflections" ) );
	static CName s_inGameConfigVar_RayTracedSunShadows( RED_NAME_CONSTEXPR( "RayTracedSunShadows" ) );
	static CName s_inGameConfigVar_RayTracedLighting( RED_NAME_CONSTEXPR( "RayTracedLighting" ) );

	static CName s_inGameConfigVar_VSync( RED_NAME_CONSTEXPR( "VSync" ) );
	static CName s_inGameConfigVar_QuickPresets( RED_NAME_CONSTEXPR( "QuickPresets" ) );
	static CName s_inGameConfigVar_ContactShadows( RED_NAME_CONSTEXPR( "ContactShadows" ) );
	static CName s_inGameConfigVar_FacialTangentUpdates( RED_NAME_CONSTEXPR( "FacialTangentUpdates" ) );
	static CName s_inGameConfigVar_DynamicResolutionScaling( RED_NAME_CONSTEXPR( "DynamicResolutionScaling" ) );
	static CName s_inGameConfigVar_DLSS( RED_NAME_CONSTEXPR( "DLSS" ) );
	static CName s_inGameConfigVar_StaticResolutionScaling( RED_NAME_CONSTEXPR( "StaticResolutionScaling" ) );
	static CName s_inGameConfigVar_AntiAliasing( RED_NAME_CONSTEXPR( "AntiAliasing" ) );
	static CName s_inGameConfigVar_Anisotropy( RED_NAME_CONSTEXPR( "Anisotropy" ) );
	static CName s_inGameConfigVar_ShadowMeshQuality( RED_NAME_CONSTEXPR( "ShadowMeshQuality" ) );
	static CName s_inGameConfigVar_LocalShadowsResolution( RED_NAME_CONSTEXPR( "LocalShadowsResolution" ) );
	static CName s_inGameConfigVar_LocalShadowsMapUpdatesPerFrame( RED_NAME_CONSTEXPR( "LocalShadowsMapUpdatesPerFrame" ) );
	static CName s_inGameConfigVar_LocalShadowsQuality( RED_NAME_CONSTEXPR( "LocalShadowsQuality" ) );
	static CName s_inGameConfigVar_CascadedShadowsRange( RED_NAME_CONSTEXPR( "CascadedShadowsRange" ) );
	static CName s_inGameConfigVar_CascadedShadowsResolution( RED_NAME_CONSTEXPR( "CascadedShadowsResolution" ) );
	static CName s_inGameConfigVar_DistantShadowsResolution( RED_NAME_CONSTEXPR( "DistantShadowsResolution" ) );
	static CName s_inGameConfigVar_VolumetricFogResolution( RED_NAME_CONSTEXPR( "VolumetricFogResolution" ) );
	static CName s_inGameConfigVar_VolumetricCloudsQuality( RED_NAME_CONSTEXPR( "VolumetricCloudsQuality" ) );
	static CName s_inGameConfigVar_MaxDynamicDecals( RED_NAME_CONSTEXPR( "MaxDynamicDecals" ) );
	static CName s_inGameConfigVar_ScreenSpaceReflectionsQuality( RED_NAME_CONSTEXPR( "ScreenSpaceReflectionsQuality" ) );
	static CName s_inGameConfigVar_SubsurfaceScatteringQuality( RED_NAME_CONSTEXPR( "SubsurfaceScatteringQuality" ) );
	static CName s_inGameConfigVar_AmbientOcclusion( RED_NAME_CONSTEXPR( "AmbientOcclusion" ) );
	static CName s_inGameConfigVar_ColorPrecision( RED_NAME_CONSTEXPR( "ColorPrecision" ) );
	static CName s_inGameConfigVar_GlobaIlluminationRange( RED_NAME_CONSTEXPR( "GlobaIlluminationRange" ) );
	static CName s_inGameConfigVar_MirrorQuality( RED_NAME_CONSTEXPR( "MirrorQuality" ) );
#endif

	namespace video_settings
	{

		namespace from_global_to_in_game
		{
			RED_INLINE void FilmGrain( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				group.GetVarBool( s_inGameConfigVar_FilmGrain ).SetValue( renderingOptionsToMirror.AllowFilmGrain, source );
			}

			RED_INLINE void ChromaticAberration( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				group.GetVarBool( s_inGameConfigVar_ChromaticAberation ).SetValue( renderingOptionsToMirror.allowChromaticAberration, source );
			}

			RED_INLINE void DepthOfField( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				group.GetVarBool( s_inGameConfigVar_DepthOfField ).SetValue( renderingOptionsToMirror.allowDOF, source );
			}

			RED_INLINE void LensFlares( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				group.GetVarBool( s_inGameConfigVar_LensFlares ).SetValue( renderingOptionsToMirror.allowImageBasedFlares, source );
			}

			RED_INLINE void MotionBlur( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
#ifdef RED_PLATFORM_WINPC
				if ( renderingOptionsToMirror.allowMotionBlur )
				{
					if ( renderingOptionsToMirror.MotionBlurQuality == 0 )
					{
						group.GetVarListString( s_inGameConfigVar_MotionBlur ).SetIndex( 1, source );
						return;
					}
					else if ( renderingOptionsToMirror.MotionBlurQuality == 1 )
					{
						group.GetVarListString( s_inGameConfigVar_MotionBlur ).SetIndex( 2, source );
						return;
					}
				}
				else
				{
					group.GetVarListString( s_inGameConfigVar_MotionBlur ).SetIndex( 0, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][MotionBlur]: Unsuported value %d", renderingOptionsToMirror.MotionBlurQuality );
#else
				group.GetVarBool( s_inGameConfigVar_MotionBlur ).SetValue( renderingOptionsToMirror.allowMotionBlur, source );
#endif
			}

#ifdef RED_PLATFORM_WINPC

			RED_INLINE void RayTracing( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				const Bool rtHardwareEnabled = GRender->GetHardwareRayTracingSupport();
				auto& rtVal = group.GetVarBool( s_inGameConfigVar_RayTracing );
				rtVal.SetValue( renderingOptionsToMirror.allowRaytracing & rtHardwareEnabled, source );

				crash::rtxEnabled.Set( renderingOptionsToMirror.allowRaytracing & rtHardwareEnabled );
			}
			
			RED_INLINE void RayTracedReflections( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				auto& rtReflectionsVal = group.GetVarBool( s_inGameConfigVar_RayTracedReflections );
				RED_ASSERT( renderingOptionsToMirror.RTX_EnableReflections == renderingOptionsToMirror.RTX_EnableTransparentReflections );
				rtReflectionsVal.SetValue( renderingOptionsToMirror.RTX_EnableReflections, source );
			}

			RED_INLINE void RayTracedSunShadows( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				auto& rtSunShadowsVal = group.GetVarBool( s_inGameConfigVar_RayTracedSunShadows );
				rtSunShadowsVal.SetValue( renderingOptionsToMirror.RTX_EnableDirectionalShadow, source );
			}

			RED_INLINE void RayTracedLighting( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				auto& rtLightingVal = group.GetVarListString( s_inGameConfigVar_RayTracedLighting );
				
				const Bool rtLightingMatchesOff = renderingOptionsToMirror.RTX_EnableDiffuseIllumination == false &&
					renderingOptionsToMirror.RTX_EnableAmbientOcclusion == false &&
					renderingOptionsToMirror.RTX_EnableGlobalIllumination == false &&
					renderingOptionsToMirror.RTX_AmbientOcclusionRayNumber == video_settings::s_RTX_AmbientOcclusionRayNumber_OFF &&
					renderingOptionsToMirror.RTX_ImportanceSamplingTransitionMin == video_settings::s_RTX_ImportanceSamplingTransitionMin_OFF &&
					renderingOptionsToMirror.RTX_ImportanceSamplingTransitionMax == video_settings::s_RTX_ImportanceSamplingTransitionMax_OFF;

				const Bool rtLightingMatchesMedium = renderingOptionsToMirror.RTX_EnableDiffuseIllumination == true &&
					renderingOptionsToMirror.RTX_EnableAmbientOcclusion == true &&
					renderingOptionsToMirror.RTX_EnableGlobalIllumination == false &&
					renderingOptionsToMirror.RTX_AmbientOcclusionRayNumber == video_settings::s_RTX_AmbientOcclusionRayNumber_MEDIUM &&
					renderingOptionsToMirror.RTX_ImportanceSamplingTransitionMin == video_settings::s_RTX_ImportanceSamplingTransitionMin_MEDIUM &&
					renderingOptionsToMirror.RTX_ImportanceSamplingTransitionMax == video_settings::s_RTX_ImportanceSamplingTransitionMax_MEDIUM;

				const Bool rtLightingMatchesUltra = renderingOptionsToMirror.RTX_EnableDiffuseIllumination == true &&
					renderingOptionsToMirror.RTX_EnableAmbientOcclusion == true &&
					renderingOptionsToMirror.RTX_EnableGlobalIllumination == false &&
					renderingOptionsToMirror.RTX_AmbientOcclusionRayNumber == video_settings::s_RTX_AmbientOcclusionRayNumber_ULTRA &&
					renderingOptionsToMirror.RTX_ImportanceSamplingTransitionMin == video_settings::s_RTX_ImportanceSamplingTransitionMin_ULTRA &&
					renderingOptionsToMirror.RTX_ImportanceSamplingTransitionMax == video_settings::s_RTX_ImportanceSamplingTransitionMax_ULTRA;

				const Bool rtLightingMatchesPsycho = renderingOptionsToMirror.RTX_EnableDiffuseIllumination == true &&
					renderingOptionsToMirror.RTX_EnableAmbientOcclusion == true &&
					renderingOptionsToMirror.RTX_EnableGlobalIllumination == true &&
					renderingOptionsToMirror.RTX_AmbientOcclusionRayNumber == video_settings::s_RTX_AmbientOcclusionRayNumber_PSYCHO &&
					renderingOptionsToMirror.RTX_ImportanceSamplingTransitionMin == video_settings::s_RTX_ImportanceSamplingTransitionMin_PSYCHO &&
					renderingOptionsToMirror.RTX_ImportanceSamplingTransitionMax == video_settings::s_RTX_ImportanceSamplingTransitionMax_PSYCHO;

				if ( rtLightingMatchesOff )
					rtLightingVal.SetIndex( 0, source );
				else if ( rtLightingMatchesMedium )
					rtLightingVal.SetIndex( 1, source );
				else if ( rtLightingMatchesUltra )
					rtLightingVal.SetIndex( 2, source );
				else if ( rtLightingMatchesPsycho )
					rtLightingVal.SetIndex( 3, source );
				else
					RED_HALT( "Uknown settings for RayTracedLighting" );
			}

			RED_INLINE void ContactShadows( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				RED_ASSERT( ( renderingOptionsToMirror.allowContactShadows_Local && renderingOptionsToMirror.allowContactShadows_Global ) ||
					( !renderingOptionsToMirror.allowContactShadows_Local && !renderingOptionsToMirror.allowContactShadows_Global ),
					"[VideoSettings][FromGlobalToInGame][ContactShadows]: Unsuported combination: local %d global: %d", renderingOptionsToMirror.allowContactShadows_Local,
					renderingOptionsToMirror.allowContactShadows_Global );

				if ( renderingOptionsToMirror.allowContactShadows_Local && renderingOptionsToMirror.allowContactShadows_Global )
				{
					group.GetVarBool( s_inGameConfigVar_ContactShadows ).SetValue( true, source );
				}
				else
				{
					group.GetVarBool( s_inGameConfigVar_ContactShadows ).SetValue( false, source );
				}
			}

			RED_INLINE void FacialTangentUpdates( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				 group.GetVarBool( s_inGameConfigVar_FacialTangentUpdates ).SetValue( renderingOptionsToMirror.allowRuntimeTangentUpdate, source );
			}

			RED_INLINE void ShadowMeshQuality( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( renderingOptionsToMirror.ShadowLODBias == 0 )
				{
					group.GetVarListString( s_inGameConfigVar_ShadowMeshQuality ).SetIndex( 2, source );
					return;
				}
				else if ( renderingOptionsToMirror.ShadowLODBias == 1 )
				{
					group.GetVarListString( s_inGameConfigVar_ShadowMeshQuality ).SetIndex( 1, source );
					return;
				}
				else if ( renderingOptionsToMirror.ShadowLODBias == UINT32_MAX )
				{
					group.GetVarListString( s_inGameConfigVar_ShadowMeshQuality ).SetIndex( 0, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][ShadowMeshQuality]: Unsuported value: %d", renderingOptionsToMirror.ShadowLODBias );
			}

			RED_INLINE void LocalShadowsQuality( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				using namespace rend::settings;
				auto& var = group.GetVarListString( s_inGameConfigVar_LocalShadowsQuality );

				if ( Is< InGameEntry::LocalShadowsQuality, InGameOption::Off >( renderingOptionsToMirror ) )
					var.SetIndex( 0, source );
				else if ( Is< InGameEntry::LocalShadowsQuality, InGameOption::Low >( renderingOptionsToMirror ) )
					var.SetIndex( 1, source );
				else if ( Is< InGameEntry::LocalShadowsQuality, InGameOption::Medium >( renderingOptionsToMirror ) )
					var.SetIndex( 2, source );
				else if ( Is< InGameEntry::LocalShadowsQuality, InGameOption::High >( renderingOptionsToMirror ) )
					var.SetIndex( 3, source );
				else
					RED_HALT( "[VideoSettings][FromGlobalToInGame][LocalShadowsQuality]: Unsuported value" );
			}

			RED_INLINE void CascadedShadowsRange( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( renderingOptionsToMirror.NumShadowCascades == s_NumShadowCascades_LOW )
				{
					group.GetVarListString( s_inGameConfigVar_CascadedShadowsRange ).SetIndex( 0, source );
					return;
				}
				else if ( renderingOptionsToMirror.NumShadowCascades == s_NumShadowCascades_MEDIUM )
				{
					group.GetVarListString( s_inGameConfigVar_CascadedShadowsRange ).SetIndex( 1, source );
					return;
				}
				else if ( renderingOptionsToMirror.NumShadowCascades == s_NumShadowCascades_HIGH )
				{
					group.GetVarListString( s_inGameConfigVar_CascadedShadowsRange ).SetIndex( 2, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][CascadedShadowsRange]: Unsuported value: %d", renderingOptionsToMirror.NumShadowCascades );
			}

			RED_INLINE void CascadedShadowsResolution( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( renderingOptionsToMirror.ShadowCascadeResolution == s_ShadowCascadeResolution_LOW )
				{
					group.GetVarListString( s_inGameConfigVar_CascadedShadowsResolution ).SetIndex( 0, source );
					return;
				}
				else if ( renderingOptionsToMirror.ShadowCascadeResolution == s_ShadowCascadeResolution_MEDIUM )
				{
					group.GetVarListString( s_inGameConfigVar_CascadedShadowsResolution ).SetIndex( 1, source );
					return;
				}
				else if ( renderingOptionsToMirror.ShadowCascadeResolution == s_ShadowCascadeResolution_HIGH )
				{
					group.GetVarListString( s_inGameConfigVar_CascadedShadowsResolution ).SetIndex( 2, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][CascadedShadowsResolution]: Unsuported value: %d", renderingOptionsToMirror.ShadowCascadeResolution );
			}

			RED_INLINE void DistantShadowsResolution( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( renderingOptionsToMirror.DistantShadowmapResolution == s_DistantShadowmapResolution_LOW )
				{
					group.GetVarListString( s_inGameConfigVar_DistantShadowsResolution ).SetIndex( 0, source );
					return;
				}
				else if ( renderingOptionsToMirror.DistantShadowmapResolution == s_DistantShadowmapResolution_HIGH )
				{
					group.GetVarListString( s_inGameConfigVar_DistantShadowsResolution ).SetIndex( 1, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][DistantShadowsResolution]: Unsuported value: %d", renderingOptionsToMirror.DistantShadowmapResolution );
			}

			RED_INLINE void VolumetricFogResolution( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( renderingOptionsToMirror.VolFogTileSize == s_VolumetricFogTileSize_LOW && renderingOptionsToMirror.VolFogNumSlices == s_VolumetricFogNumSlices_LOW &&
					renderingOptionsToMirror.VolFogNumSamples == s_VolumetricFogNumSamples_LOW )
				{
					group.GetVarListString( s_inGameConfigVar_VolumetricFogResolution ).SetIndex( 0, source );
					return;
				}
				else if ( renderingOptionsToMirror.VolFogTileSize == s_VolumetricFogTileSize_MEDIUM && renderingOptionsToMirror.VolFogNumSlices == s_VolumetricFogNumSlices_MEDIUM &&
					renderingOptionsToMirror.VolFogNumSamples == s_VolumetricFogNumSamples_MEDIUM )
				{
					group.GetVarListString( s_inGameConfigVar_VolumetricFogResolution ).SetIndex( 1, source );
					return;
				}
				else if ( renderingOptionsToMirror.VolFogTileSize == s_VolumetricFogTileSize_HIGH && renderingOptionsToMirror.VolFogNumSlices == s_VolumetricFogNumSlices_HIGH &&
					renderingOptionsToMirror.VolFogNumSamples == s_VolumetricFogNumSamples_HIGH )
				{
					group.GetVarListString( s_inGameConfigVar_VolumetricFogResolution ).SetIndex( 2, source );
					return;
				}
				else if ( renderingOptionsToMirror.VolFogTileSize == s_VolumetricFogTileSize_ULTRA && renderingOptionsToMirror.VolFogNumSlices == s_VolumetricFogNumSlices_ULTRA &&
					renderingOptionsToMirror.VolFogNumSamples == s_VolumetricFogNumSamples_ULTRA )
				{
					group.GetVarListString( s_inGameConfigVar_VolumetricFogResolution ).SetIndex( 3, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][VolumetricFogResolution]: Unsuported values: VolFogTileSize %d VolFogNumSlices %d VolFogNumSamples %d", renderingOptionsToMirror.VolFogTileSize,
					renderingOptionsToMirror.VolFogNumSlices, renderingOptionsToMirror.VolFogNumSamples );
			}

			RED_INLINE void MaxDynamicDecals( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				using namespace rend::settings;
				auto& var = group.GetVarListString( s_inGameConfigVar_MaxDynamicDecals );

				if ( Is< InGameEntry::MaxDynamicDecals, InGameOption::Low >( renderingOptionsToMirror ) )
					var.SetIndex( 0, source );
				else if ( Is< InGameEntry::MaxDynamicDecals, InGameOption::Medium >( renderingOptionsToMirror ) )
					var.SetIndex( 1, source );
				else if ( Is< InGameEntry::MaxDynamicDecals, InGameOption::High >( renderingOptionsToMirror ) )
					var.SetIndex( 2, source );
				else if ( Is< InGameEntry::MaxDynamicDecals, InGameOption::Ultra >( renderingOptionsToMirror ) )
					var.SetIndex( 3, source );
				else
					RED_HALT( "Invalid index for MaxDynamicDecals." );
			}

			RED_INLINE void VolumetricCloudsQuality( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				using namespace rend::settings;
				auto& var = group.GetVarListString( s_inGameConfigVar_VolumetricCloudsQuality );

				if ( Is< InGameEntry::VolumetricCloudsQuality, InGameOption::Off >( renderingOptionsToMirror ) )
					var.SetIndex( 0, source );
				else if ( Is< InGameEntry::VolumetricCloudsQuality, InGameOption::Medium >( renderingOptionsToMirror ) )
					var.SetIndex( 1, source );
				else if ( Is< InGameEntry::VolumetricCloudsQuality, InGameOption::High >( renderingOptionsToMirror ) )
					var.SetIndex( 2, source );
				else if ( Is< InGameEntry::VolumetricCloudsQuality, InGameOption::Ultra >( renderingOptionsToMirror ) )
					var.SetIndex( 3, source );
				else
					RED_HALT( "Invalid index for VolumetricCloudsQuality." );

			}

			RED_INLINE void ScreenSpaceReflectionsQuality( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( !renderingOptionsToMirror.allowSSR && renderingOptionsToMirror.ScreenSpaceReflectionLevel == s_ScreenSpaceReflectionLevel_OFF )
				{
					group.GetVarListString( s_inGameConfigVar_ScreenSpaceReflectionsQuality ).SetIndex( 0, source );
					return;
				}
				else if ( renderingOptionsToMirror.allowSSR && renderingOptionsToMirror.ScreenSpaceReflectionLevel == s_ScreenSpaceReflectionLevel_LOW )
				{
					group.GetVarListString( s_inGameConfigVar_ScreenSpaceReflectionsQuality ).SetIndex( 1, source );
					return;
				}
				else if ( renderingOptionsToMirror.allowSSR && renderingOptionsToMirror.ScreenSpaceReflectionLevel == s_ScreenSpaceReflectionLevel_MEDIUM )
				{
					group.GetVarListString( s_inGameConfigVar_ScreenSpaceReflectionsQuality ).SetIndex( 2, source );
					return;
				}
				else if ( renderingOptionsToMirror.allowSSR && renderingOptionsToMirror.ScreenSpaceReflectionLevel == s_ScreenSpaceReflectionLevel_HIGH )
				{
					group.GetVarListString( s_inGameConfigVar_ScreenSpaceReflectionsQuality ).SetIndex( 3, source );
					return;
				}
				else if ( renderingOptionsToMirror.allowSSR && renderingOptionsToMirror.ScreenSpaceReflectionLevel == s_ScreenSpaceReflectionLevel_ULTRA )
				{
					group.GetVarListString( s_inGameConfigVar_ScreenSpaceReflectionsQuality ).SetIndex( 4, source );
					return;
				}
				else if ( renderingOptionsToMirror.allowSSR && renderingOptionsToMirror.ScreenSpaceReflectionLevel == s_ScreenSpaceReflectionLevel_INSANE )
				{
					group.GetVarListString( s_inGameConfigVar_ScreenSpaceReflectionsQuality ).SetIndex( 5, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][ScreenSpaceReflectionsQuality]: Unsuported value: %d", renderingOptionsToMirror.ScreenSpaceReflectionLevel );
			}

			RED_INLINE void SubsurfaceQualityProfileIndex( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( renderingOptionsToMirror.SubsurfaceQualityProfileIndex == s_SubsurfaceQualityProfileIndex_LOW )
				{
					group.GetVarListString( s_inGameConfigVar_SubsurfaceScatteringQuality ).SetIndex( 0, source );
					return;
				}
				else if ( renderingOptionsToMirror.SubsurfaceQualityProfileIndex == s_SubsurfaceQualityProfileIndex_MEDIUM )
				{
					group.GetVarListString( s_inGameConfigVar_SubsurfaceScatteringQuality ).SetIndex( 1, source );
					return;
				}
				else if ( renderingOptionsToMirror.SubsurfaceQualityProfileIndex == s_SubsurfaceQualityProfileIndex_HIGH )
				{
					group.GetVarListString( s_inGameConfigVar_SubsurfaceScatteringQuality ).SetIndex( 2, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][SubsurfaceQualityProfileIndex]: Unsuported value: %d", renderingOptionsToMirror.SubsurfaceQualityProfileIndex );
			}

			RED_INLINE void AmbientOcclusion( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( !renderingOptionsToMirror.allowSSAO && renderingOptionsToMirror.ConeAOQuality == s_ConeAOQuality_LOW )
				{
					group.GetVarListString( s_inGameConfigVar_AmbientOcclusion ).SetIndex( 0, source );
					return;
				}
				else if ( renderingOptionsToMirror.allowSSAO && renderingOptionsToMirror.ConeAOQuality == s_ConeAOQuality_LOW )
				{
					group.GetVarListString( s_inGameConfigVar_AmbientOcclusion ).SetIndex( 1, source );
					return;
				}
				else if ( renderingOptionsToMirror.allowSSAO && renderingOptionsToMirror.ConeAOQuality == s_ConeAOQuality_MEDIUM )
				{
					group.GetVarListString( s_inGameConfigVar_AmbientOcclusion ).SetIndex( 2, source );
					return;
				}
				else if ( renderingOptionsToMirror.allowSSAO && renderingOptionsToMirror.ConeAOQuality == s_ConeAOQuality_HIGH )
				{
					group.GetVarListString( s_inGameConfigVar_AmbientOcclusion ).SetIndex( 3, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][AmbientOcclusion]: Unsuported value: %d", renderingOptionsToMirror.ConeAOQuality );
			}

			RED_INLINE void ColorPrecision( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( renderingOptionsToMirror.UseHighPrecisionRenderTargets )
				{
					group.GetVarListString( s_inGameConfigVar_ColorPrecision ).SetIndex( 1, source );
				}
				else
				{
					group.GetVarListString( s_inGameConfigVar_ColorPrecision ).SetIndex( 0, source );
				}
			}

			RED_INLINE void MirrorQuality( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				if ( renderingOptionsToMirror.MirrorFinalResolutionScale == s_MirrorFinalResolutionScale_LOW &&
					renderingOptionsToMirror.MirrorInternalResolutionScale == s_MirrorInternalResolutionScale_LOW )
				{
					group.GetVarListString( s_inGameConfigVar_MirrorQuality ).SetIndex( 0, source );
					return;
				}
				else if ( renderingOptionsToMirror.MirrorFinalResolutionScale == s_MirrorFinalResolutionScale_MEDIUM &&
					renderingOptionsToMirror.MirrorInternalResolutionScale == s_MirrorInternalResolutionScale_MEDIUM )
				{
					group.GetVarListString( s_inGameConfigVar_MirrorQuality ).SetIndex( 1, source );
					return;
				}
				else if ( renderingOptionsToMirror.MirrorFinalResolutionScale == s_MirrorFinalResolutionScale_HIGH &&
					renderingOptionsToMirror.MirrorInternalResolutionScale == s_MirrorInternalResolutionScale_HIGH )
				{
					group.GetVarListString( s_inGameConfigVar_MirrorQuality ).SetIndex( 2, source );
					return;
				}

				RED_HALT( "[VideoSettings][FromGlobalToInGame][MirrorQuality]: Unsuported values: MirrorFinalResolutionScale %d MirrorInternalResolutionScale %d",
					renderingOptionsToMirror.MirrorFinalResolutionScale, renderingOptionsToMirror.MirrorInternalResolutionScale );
			}

			RED_INLINE void GlobaIlluminationRange( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				// TODO: Most probably needs restarting/reloading the game. To be decided. Hidden in the menu for now.
			}

			RED_INLINE void DLSS( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				// TODO: Will go with next batch of 'dependent' settings. Hidden in the menu for now.
			}

			RED_INLINE void AntiAliasing( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				// TODO: Will go with next batch of 'dependent' settings. Hidden in the menu for now.
			}

			RED_INLINE void Anisotropy( InGameConfig::Group& group, const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source )
			{
				switch ( renderingOptionsToMirror.AnisotropyLevel )
				{
				case 1:
					group.GetVarListInt( s_inGameConfigVar_Anisotropy ).SetIndex( 0, source );
					break;
				case 4:
					group.GetVarListInt( s_inGameConfigVar_Anisotropy ).SetIndex( 1, source );
					break;
				case 8:
					group.GetVarListInt( s_inGameConfigVar_Anisotropy ).SetIndex( 2, source );
					break;
				case 16:
					group.GetVarListInt( s_inGameConfigVar_Anisotropy ).SetIndex( 3, source );
					break;

				default:
					RED_HALT( "[VideoSettings][FromGlobalToInGame][Anisotropy]: Unsuported value %d", GGlobalRenderingSettings.AnisotropyLevel );
				}
			}

			RED_INLINE void TextureQuality( InGameConfig::Group& group, TextureQualityLevel tql, InGameConfig::Source source )
			{
				auto& var = group.GetVarListString( RED_NAME_CONSTEXPR( "TextureQuality" ) );
				switch( tql )
				{
				case Config::TextureQualityLevel::Low:
					var.SetIndex( 0, source );
					break;
				case Config::TextureQualityLevel::Medium:
					var.SetIndex( 1, source );
					break;
				case Config::TextureQualityLevel::High:
					var.SetIndex( 2, source );
					break;
				case Config::TextureQualityLevel::Console:
				case Config::TextureQualityLevel::ConsoleSafe:
				case Config::TextureQualityLevel::SafeMode:
				case Config::TextureQualityLevel::Auto:
				case Config::TextureQualityLevel::Count:
				default:
					RED_LOG_WARNING( "[InGameSettings][Rendering] Running game with texture quality set to %d - it won't be reflected in in-game settings.", ( Uint32 )tql );
				}
			}
#endif
		}

		namespace from_in_game_to_global
		{
			RED_INLINE void VSync( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				// TODO: Will go with next batch of 'dependent' settings. Hidden in the menu for now.
			}

			RED_INLINE void FilmGrain( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				outSettings.AllowFilmGrain = group.GetVarBool( s_inGameConfigVar_FilmGrain ).GetValue();
			}

			RED_INLINE void ChromaticAberration( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				outSettings.allowChromaticAberration = group.GetVarBool( s_inGameConfigVar_ChromaticAberation ).GetValue();
			}

			RED_INLINE void DepthOfField( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				outSettings.allowDOF = group.GetVarBool( s_inGameConfigVar_DepthOfField ).GetValue();
			}

			RED_INLINE void LensFlares( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				outSettings.allowImageBasedFlares = group.GetVarBool( s_inGameConfigVar_LensFlares ).GetValue();
			}

			RED_INLINE void MotionBlur( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
#ifdef RED_PLATFORM_WINPC
				const Int32 motionBlur_i = group.GetVarListString( s_inGameConfigVar_MotionBlur ).GetIndex();

				if ( motionBlur_i == 0 ) // Off
				{
					outSettings.MotionBlurQuality = 0;
					outSettings.allowMotionBlur = false;
					return;
				}
				else if ( motionBlur_i == 1 ) // Low
				{
					outSettings.MotionBlurQuality = 0;
					outSettings.allowMotionBlur = true;
					return;
				}
				else if ( motionBlur_i == 2 ) // High
				{
					outSettings.MotionBlurQuality = 1;
					outSettings.allowMotionBlur = true;
					return;
				}

				RED_HALT( "[VideoSettings][FromInGameToGlobal][MotionBlur]: Unsuported index: %d", ( Uint32 ) motionBlur_i );
#else
				if ( group.GetVarBool( s_inGameConfigVar_MotionBlur ).GetValue() )
				{
					outSettings.MotionBlurQuality = 0;
					outSettings.allowMotionBlur = true;
				}
				else
				{
					outSettings.MotionBlurQuality = 0;
					outSettings.allowMotionBlur = false;
				}
#endif
			}

#ifdef RED_PLATFORM_WINPC

			RED_INLINE void RayTracing( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				auto& rtVar = group.GetVarBool( s_inGameConfigVar_RayTracing );
				const Bool rtVal = rtVar.GetValue();
				const Bool rtHardwareEnabled = GRender->GetHardwareRayTracingSupport();
				outSettings.allowRaytracing = rtVal & rtHardwareEnabled;

				crash::rtxEnabled.Set( outSettings.allowRaytracing );
			}

			RED_INLINE void RayTracedLighting( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				auto& rtLightingVar = group.GetVarListString( s_inGameConfigVar_RayTracedLighting );
				const Int32 rtLightingVar_i = rtLightingVar.GetIndex();

				if ( rtLightingVar_i == 0 ) // OFF
				{
					outSettings.RTX_EnableDiffuseIllumination = false;
					outSettings.RTX_EnableAmbientOcclusion = false;
					outSettings.RTX_EnableGlobalIllumination = false;
					outSettings.RTX_AmbientOcclusionRayNumber = video_settings::s_RTX_AmbientOcclusionRayNumber_OFF;
					outSettings.RTX_ImportanceSamplingTransitionMin = video_settings::s_RTX_ImportanceSamplingTransitionMin_OFF;
					outSettings.RTX_ImportanceSamplingTransitionMax = video_settings::s_RTX_ImportanceSamplingTransitionMax_OFF;
					return;
				}
				else if ( rtLightingVar_i == 1 ) // Medium
				{
					outSettings.RTX_EnableDiffuseIllumination = true;
					outSettings.RTX_EnableAmbientOcclusion = true;
					outSettings.RTX_EnableGlobalIllumination = false;
					outSettings.RTX_AmbientOcclusionRayNumber = video_settings::s_RTX_AmbientOcclusionRayNumber_MEDIUM;
					outSettings.RTX_ImportanceSamplingTransitionMin = video_settings::s_RTX_ImportanceSamplingTransitionMin_MEDIUM;
					outSettings.RTX_ImportanceSamplingTransitionMax = video_settings::s_RTX_ImportanceSamplingTransitionMax_MEDIUM;
					return;
				}
				else if ( rtLightingVar_i == 2 ) // Ultra
				{
					outSettings.RTX_EnableDiffuseIllumination = true;
					outSettings.RTX_EnableAmbientOcclusion = true;
					outSettings.RTX_EnableGlobalIllumination = false;
					outSettings.RTX_AmbientOcclusionRayNumber = video_settings::s_RTX_AmbientOcclusionRayNumber_ULTRA;
					outSettings.RTX_ImportanceSamplingTransitionMin = video_settings::s_RTX_ImportanceSamplingTransitionMin_ULTRA;
					outSettings.RTX_ImportanceSamplingTransitionMax = video_settings::s_RTX_ImportanceSamplingTransitionMax_ULTRA;
					return;
				}
				else if ( rtLightingVar_i == 3 ) // Psychol
				{
					outSettings.RTX_EnableDiffuseIllumination = true;
					outSettings.RTX_EnableAmbientOcclusion = true;
					outSettings.RTX_EnableGlobalIllumination = true;
					outSettings.RTX_AmbientOcclusionRayNumber = video_settings::s_RTX_AmbientOcclusionRayNumber_PSYCHO;
					outSettings.RTX_ImportanceSamplingTransitionMin = video_settings::s_RTX_ImportanceSamplingTransitionMin_PSYCHO;
					outSettings.RTX_ImportanceSamplingTransitionMax = video_settings::s_RTX_ImportanceSamplingTransitionMax_PSYCHO;
					return;
				}
				RED_HALT( "RayTracedLighting wrong index" );
			}

			RED_INLINE void RayTracedReflections( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				auto& rtReflectionsVar = group.GetVarBool( s_inGameConfigVar_RayTracedReflections );
				const Bool rtReflectionsVal = rtReflectionsVar.GetValue();

				outSettings.RTX_EnableReflections = rtReflectionsVal;
				outSettings.RTX_EnableTransparentReflections = rtReflectionsVal;
			}

			RED_INLINE void RayTracedSunShadows( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				auto& rtSunShadowsVar = group.GetVarBool( s_inGameConfigVar_RayTracedSunShadows );
				const Bool rtSunShadowsVal = rtSunShadowsVar.GetValue();

				outSettings.RTX_EnableDirectionalShadow = rtSunShadowsVal;
			}

			RED_INLINE void ContactShadows( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Bool contactShadowsVal = group.GetVarBool( s_inGameConfigVar_ContactShadows ).GetValue();
				outSettings.allowContactShadows_Local = contactShadowsVal;
				outSettings.allowContactShadows_Global = contactShadowsVal;
			}

			RED_INLINE void FacialTangentUpdates( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				outSettings.allowRuntimeTangentUpdate = group.GetVarBool( s_inGameConfigVar_FacialTangentUpdates ).GetValue();
			}


			RED_INLINE void DLSS( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				// TODO: Will go with next batch of 'dependent' settings. Hidden in the menu for now.
			}

			RED_INLINE void AntiAliasing( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				// TODO: Will go with next batch of 'dependent' settings. Hidden in the menu for now.
			}

			RED_INLINE void Anisotropy( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const auto anisotropyVal = group.GetVarListInt( s_inGameConfigVar_Anisotropy ).GetValue();
				RED_ASSERT( anisotropyVal == 1 || anisotropyVal == 4 || anisotropyVal == 8 || anisotropyVal == 16 );
				outSettings.AnisotropyLevel = anisotropyVal;
			}

			RED_INLINE void ShadowMeshQuality( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 smquality_i = group.GetVarListString( s_inGameConfigVar_ShadowMeshQuality ).GetIndex();
				if ( smquality_i == 0 )	// Low
				{
					outSettings.ShadowLODBias = UINT32_MAX;
					return;
				}
				else if ( smquality_i == 1 )	// Medium
				{
					outSettings.ShadowLODBias = 1;
					return;
				}
				else if ( smquality_i == 2 )	// High
				{
					outSettings.ShadowLODBias = 0;
					return;
				}

				RED_HALT( "[VideoSettings][FromInGameToGlobal][ShadowMeshQuality]: Unsuported index: %d", (Uint32) smquality_i );
			}

			RED_INLINE void LocalShadowsQuality( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				using namespace rend::settings;
				auto& var = group.GetVarListString( s_inGameConfigVar_LocalShadowsQuality );
				const Int32 var_i = var.GetIndex();

				if ( var_i == 0 )
					Set<  InGameEntry::LocalShadowsQuality, InGameOption::Off >( outSettings );
				else if( var_i == 1 )
					Set<  InGameEntry::LocalShadowsQuality, InGameOption::Low >( outSettings );
				else if ( var_i == 2 )
					Set<  InGameEntry::LocalShadowsQuality, InGameOption::Medium >( outSettings );
				else if ( var_i == 3 )
					Set<  InGameEntry::LocalShadowsQuality, InGameOption::High >( outSettings );
				else
					RED_FATAL( "Wrong index set for LocalShadowsQuality." );
			}

			RED_INLINE void CascadedShadowsRange( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 csRange_i = group.GetVarListString( s_inGameConfigVar_CascadedShadowsRange ).GetIndex();
				if ( csRange_i == 0 )	// Low
				{
					outSettings.NumShadowCascades = s_NumShadowCascades_LOW;
					return;
				}
				else if ( csRange_i == 1 )	// Medium
				{
					outSettings.NumShadowCascades = s_NumShadowCascades_MEDIUM;
					return;
				}
				else if ( csRange_i == 2 )	// High
				{
					outSettings.NumShadowCascades = s_NumShadowCascades_HIGH;
					return;
				}
				RED_HALT( "[VideoSettings][FromInGameToGlobal][CascadedShadowsRange]: Unsuported index: %d", ( Uint32 ) csRange_i );
			}

			RED_INLINE void CascadedShadowsResolution( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 csResolution_i = group.GetVarListString( s_inGameConfigVar_CascadedShadowsResolution ).GetIndex();
				if ( csResolution_i == 0 )	// Low
				{
					outSettings.ShadowCascadeResolution = s_ShadowCascadeResolution_LOW;
					return;
				}
				else if ( csResolution_i == 1 )	// Medium
				{
					outSettings.ShadowCascadeResolution = s_ShadowCascadeResolution_MEDIUM;
					return;
				}
				else if ( csResolution_i == 2 )	// High
				{
					outSettings.ShadowCascadeResolution = s_ShadowCascadeResolution_HIGH;
					return;
				}
				RED_HALT( "[VideoSettings][FromInGameToGlobal][ShadowCascadeResolution]: Unsuported index: %d", ( Uint32 ) csResolution_i );
			}

			RED_INLINE void DistantShadowsResolution( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 dsResolution_i = group.GetVarListString( s_inGameConfigVar_DistantShadowsResolution ).GetIndex();
				if ( dsResolution_i == 0 )	// Low
				{
					outSettings.DistantShadowmapResolution = s_DistantShadowmapResolution_LOW;
					return;
				}
				else if ( dsResolution_i == 1 )	// High
				{
					outSettings.DistantShadowmapResolution = s_DistantShadowmapResolution_HIGH;
					return;
				}

				RED_HALT( "[VideoSettings][FromInGameToGlobal][DistantShadowmapResolution]: Unsuported index: %d", ( Uint32 ) dsResolution_i );
			}

			RED_INLINE void VolumetricFogResolution( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 vfResolution_i = group.GetVarListString( s_inGameConfigVar_VolumetricFogResolution ).GetIndex();
				if ( vfResolution_i == 0 )	// Low
				{
					outSettings.VolFogTileSize = video_settings::s_VolumetricFogTileSize_LOW;
					outSettings.VolFogNumSlices = video_settings::s_VolumetricFogNumSlices_LOW;
					outSettings.VolFogNumSamples = video_settings::s_VolumetricFogNumSamples_LOW;
					return;
				}
				else if ( vfResolution_i == 1 )	// Medium
				{
					outSettings.VolFogTileSize = video_settings::s_VolumetricFogTileSize_MEDIUM;
					outSettings.VolFogNumSlices = video_settings::s_VolumetricFogNumSlices_MEDIUM;
					outSettings.VolFogNumSamples = video_settings::s_VolumetricFogNumSamples_MEDIUM;
					return;
				}
				else if ( vfResolution_i == 2 )	// High
				{
					outSettings.VolFogTileSize = video_settings::s_VolumetricFogTileSize_HIGH;
					outSettings.VolFogNumSlices = video_settings::s_VolumetricFogNumSlices_HIGH;
					outSettings.VolFogNumSamples = video_settings::s_VolumetricFogNumSamples_HIGH;
					return;
				}
				else if ( vfResolution_i == 3 )	// Ultra
				{
					outSettings.VolFogTileSize = video_settings::s_VolumetricFogTileSize_ULTRA;
					outSettings.VolFogNumSlices = video_settings::s_VolumetricFogNumSlices_ULTRA;
					outSettings.VolFogNumSamples = video_settings::s_VolumetricFogNumSamples_ULTRA;
					return;
				}

				RED_HALT( "[VideoSettings][FromInGameToGlobal][VolumetricFogResolution]: Unsuported index: %d", ( Uint32 ) vfResolution_i );
			}

			RED_INLINE void MaxDynamicDecals( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				using namespace rend::settings;
				auto& var = group.GetVarListString( s_inGameConfigVar_MaxDynamicDecals );
				const Int32 var_i = var.GetIndex();

				if ( var_i == 0 ) // Low
					Set< InGameEntry::MaxDynamicDecals, InGameOption::Low >( outSettings );
				else if ( var_i == 1 ) // Medium
					Set< InGameEntry::MaxDynamicDecals, InGameOption::Medium >( outSettings );
				else if ( var_i == 2 ) // High
					Set< InGameEntry::MaxDynamicDecals, InGameOption::High >( outSettings );
				else if ( var_i == 3 ) // Ultra
					Set< InGameEntry::MaxDynamicDecals, InGameOption::Ultra >( outSettings );
				else
					RED_HALT( "Invalid index for MaxDynamicDecals." );
			}

			RED_INLINE void VolumetricCloudsQuality( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				using namespace rend::settings;
				auto& var = group.GetVarListString( s_inGameConfigVar_VolumetricCloudsQuality );
				const Int32 var_i = var.GetIndex();

				if ( var_i == 0 ) // Off
					Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Off >( outSettings );
				else if ( var_i == 1 ) // Medium
					Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Medium >( outSettings );
				else if ( var_i == 2 ) // High
					Set< InGameEntry::VolumetricCloudsQuality, InGameOption::High >( outSettings );
				else if ( var_i == 3 ) // Ultra
					Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Ultra >( outSettings );
				else
					RED_HALT( "Invalid index for VolumetricCloudsQuality." );
			}

			RED_INLINE void ScreenSpaceReflectionsQuality( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 ssrQuality_i = group.GetVarListString( s_inGameConfigVar_ScreenSpaceReflectionsQuality ).GetIndex();
				if ( ssrQuality_i == 0 )	// Off
				{
					outSettings.allowSSR = false;
					outSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_OFF;
					return;
				}
				else if ( ssrQuality_i == 1 ) // Low
				{
					outSettings.allowSSR = true;
					outSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_LOW;
					return;
				}
				else if ( ssrQuality_i == 2 ) // Medium
				{
					outSettings.allowSSR = true;
					outSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_MEDIUM;
					return;
				}
				else if ( ssrQuality_i == 3 ) // High
				{
					outSettings.allowSSR = true;
					outSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_HIGH;
					return;
				}
				else if ( ssrQuality_i == 4 ) // Ultra
				{
					outSettings.allowSSR = true;
					outSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_ULTRA;
					return;
				}
				else if ( ssrQuality_i == 5 ) // Insane
				{
					outSettings.allowSSR = true;
					outSettings.ScreenSpaceReflectionLevel = video_settings::s_ScreenSpaceReflectionLevel_INSANE;
					return;
				}

				RED_HALT( "[VideoSettings][FromInGameToGlobal][ScreenSpaceReflectionsQuality]: Unsuported index: %d", ( Uint32 ) ssrQuality_i );
			}

			RED_INLINE void SubsurfaceScatteringQuality( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 ssQuality_i = group.GetVarListString( s_inGameConfigVar_SubsurfaceScatteringQuality ).GetIndex();
				if ( ssQuality_i == 0 )	// Low
				{
					outSettings.SubsurfaceQualityProfileIndex = s_SubsurfaceQualityProfileIndex_LOW;
					return;
				}
				else if ( ssQuality_i == 1 ) // Medium
				{
					outSettings.SubsurfaceQualityProfileIndex = s_SubsurfaceQualityProfileIndex_MEDIUM;
					return;
				}
				else if ( ssQuality_i == 2 ) // High
				{
					outSettings.SubsurfaceQualityProfileIndex = s_SubsurfaceQualityProfileIndex_HIGH;
					return;
				}

				RED_HALT( "[VideoSettings][FromInGameToGlobal][SubsurfaceScatteringQuality]: Unsuported index: %d", ( Uint32 ) ssQuality_i );
			}

			RED_INLINE void AmbientOcclusion( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 aoQuality_i = group.GetVarListString( s_inGameConfigVar_AmbientOcclusion ).GetIndex();
				if ( aoQuality_i == 0 )	// Off
				{
					outSettings.allowSSAO = false;
					outSettings.ConeAOQuality = s_ConeAOQuality_LOW;
					return;
				}
				else if ( aoQuality_i == 1 ) // Low
				{
					outSettings.allowSSAO = true;
					outSettings.ConeAOQuality = s_ConeAOQuality_LOW;
					return;
				}
				else if ( aoQuality_i == 2 ) // Medium 
				{
					outSettings.allowSSAO = true;
					outSettings.ConeAOQuality = s_ConeAOQuality_MEDIUM;
					return;
				}
				else if ( aoQuality_i == 3 ) // High
				{
					outSettings.allowSSAO = true;
					outSettings.ConeAOQuality = s_ConeAOQuality_HIGH;
					return;
				}
				RED_HALT( "[VideoSettings][FromInGameToGlobal][AmbientOcclusion]: Unsuported index: %d", ( Uint32 ) aoQuality_i );
			}

			RED_INLINE void ColorPrecision( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 colorPrecision_i = group.GetVarListString( s_inGameConfigVar_ColorPrecision ).GetIndex();
				if ( colorPrecision_i == 0 )	// Medium
				{
					outSettings.UseHighPrecisionRenderTargets = false;
					return;
				}
				else if ( colorPrecision_i == 1 ) // High
				{
					outSettings.UseHighPrecisionRenderTargets = true;
					return;
				}

				RED_HALT( "[VideoSettings][FromInGameToGlobal][ColorPrecision]: Unsuported index: %d", ( Uint32 ) colorPrecision_i );
			}

			RED_INLINE void MirrorQuality( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				const Int32 mirrorQiality_i = group.GetVarListString( s_inGameConfigVar_MirrorQuality ).GetIndex();
				if ( mirrorQiality_i == 0 )	// Low
				{
					outSettings.MirrorFinalResolutionScale = s_MirrorFinalResolutionScale_LOW;
					outSettings.MirrorInternalResolutionScale = s_MirrorInternalResolutionScale_LOW;
					return;
				}
				else if ( mirrorQiality_i == 1 ) // Medium
				{
					outSettings.MirrorFinalResolutionScale = s_MirrorFinalResolutionScale_MEDIUM;
					outSettings.MirrorInternalResolutionScale = s_MirrorInternalResolutionScale_MEDIUM;
					return;
				}
				else if ( mirrorQiality_i == 2 ) // High
				{
					outSettings.MirrorFinalResolutionScale = s_MirrorFinalResolutionScale_HIGH;
					outSettings.MirrorInternalResolutionScale = s_MirrorInternalResolutionScale_HIGH;
					return;
				}

				RED_HALT( "[VideoSettings][FromInGameToGlobal][MirrorQuality]: Unsuported index: %d", ( Uint32 ) mirrorQiality_i );
			}

			RED_INLINE void GlobaIlluminationRange( GlobalRenderingSettings &outSettings, InGameConfig::Group& group )
			{
				// TODO: Most probably needs restarting/reloading the game. To be decided.
			}
#endif
		}
	}

	void GlobalRenderingSettings::UpdateWithInGameVideoSettings()
	{
		auto& sys = InGameConfig::System::GetInstance();

		InGameConfig::Group& basicGroup		= sys.GetGroup( s_inGameConfigGroup_Basic );
		InGameConfig::Group& advancedGroup	= sys.GetGroup( s_inGameConfigGroup_Advanced );
		InGameConfig::Group& rtGroup		= sys.GetGroup( s_inGameConfigGroup_RayTracing );

		// Basic
		{
			video_settings::from_in_game_to_global::FilmGrain( *this, basicGroup );
			video_settings::from_in_game_to_global::ChromaticAberration( *this, basicGroup );
			video_settings::from_in_game_to_global::DepthOfField( *this, basicGroup );
			video_settings::from_in_game_to_global::MotionBlur( *this, basicGroup );
			video_settings::from_in_game_to_global::LensFlares( *this, basicGroup );
		}

#ifdef RED_PLATFORM_WINPC
		// Advanced
		{
			video_settings::from_in_game_to_global::ContactShadows( *this, advancedGroup );
			video_settings::from_in_game_to_global::FacialTangentUpdates( *this, advancedGroup );
			video_settings::from_in_game_to_global::Anisotropy( *this, advancedGroup );
			video_settings::from_in_game_to_global::ShadowMeshQuality( *this, advancedGroup );
			video_settings::from_in_game_to_global::LocalShadowsQuality( *this, advancedGroup );
			video_settings::from_in_game_to_global::CascadedShadowsRange( *this, advancedGroup );
			video_settings::from_in_game_to_global::CascadedShadowsResolution( *this, advancedGroup );
			video_settings::from_in_game_to_global::DistantShadowsResolution( *this, advancedGroup );
			video_settings::from_in_game_to_global::VolumetricFogResolution( *this, advancedGroup );
			video_settings::from_in_game_to_global::ScreenSpaceReflectionsQuality( *this, advancedGroup );
			video_settings::from_in_game_to_global::SubsurfaceScatteringQuality( *this, advancedGroup );
			video_settings::from_in_game_to_global::AmbientOcclusion( *this, advancedGroup );
			video_settings::from_in_game_to_global::ColorPrecision( *this, advancedGroup );
			video_settings::from_in_game_to_global::MirrorQuality( *this, advancedGroup );
			video_settings::from_in_game_to_global::MaxDynamicDecals( *this, advancedGroup );
			video_settings::from_in_game_to_global::VolumetricCloudsQuality( *this, advancedGroup );

			// Invisible/disabled for now. More info inside the functions
			video_settings::from_in_game_to_global::GlobaIlluminationRange( *this, advancedGroup );
			video_settings::from_in_game_to_global::DLSS( *this, advancedGroup );
			video_settings::from_in_game_to_global::AntiAliasing( *this, advancedGroup );
		}

		// RTX
		{
			video_settings::from_in_game_to_global::RayTracing( *this, rtGroup );
			video_settings::from_in_game_to_global::RayTracedReflections( *this, rtGroup );
			video_settings::from_in_game_to_global::RayTracedSunShadows( *this, rtGroup );
			video_settings::from_in_game_to_global::RayTracedLighting( *this, rtGroup );
			rend::settings::callbacks::OnProcessRayTracingTabVisibility();
		}
#endif
	}

	void MapRenderingSettingsToInGameSettings( const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source realSource )
	{
		auto& sys = InGameConfig::System::GetInstance();

		InGameConfig::Group& basicGroup		= sys.GetGroup( s_inGameConfigGroup_Basic );
		InGameConfig::Group& advancedGroup	= sys.GetGroup( s_inGameConfigGroup_Advanced );
		InGameConfig::Group& rtGroup		= sys.GetGroup( s_inGameConfigGroup_RayTracing );

		const InGameConfig::Source c_source =
			realSource == InGameConfig::Source::Code
			? InGameConfig::Source::PresetCode
			: realSource == InGameConfig::Source::Settings
			? InGameConfig::Source::PresetSettings
			: InGameConfig::Source::Invalid;

		RED_FATAL_ASSERT( c_source != InGameConfig::Source::Invalid );

		// NOTE: for WinPC QuickPresets are handled separately via listener in order to reflect its settings on
		//       rest of the entries and not create circular dependencies. For ORBIS and DURANGO we do not care about it.

		// Basic
		video_settings::from_global_to_in_game::FilmGrain( basicGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::ChromaticAberration( basicGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::DepthOfField( basicGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::LensFlares( basicGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::MotionBlur( basicGroup, renderingOptionsToMirror, c_source );

#ifdef RED_PLATFORM_WINPC
		// Advanced
		video_settings::from_global_to_in_game::ContactShadows( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::FacialTangentUpdates( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::Anisotropy( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::ShadowMeshQuality( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::LocalShadowsQuality( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::CascadedShadowsRange( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::CascadedShadowsResolution( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::DistantShadowsResolution( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::VolumetricFogResolution( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::VolumetricCloudsQuality( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::MaxDynamicDecals( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::ScreenSpaceReflectionsQuality( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::SubsurfaceQualityProfileIndex( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::AmbientOcclusion( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::ColorPrecision( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::MirrorQuality( advancedGroup, renderingOptionsToMirror, c_source );

		// Invisible/disabled for now. More info inside the functions
		video_settings::from_global_to_in_game::DLSS( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::AntiAliasing( advancedGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::GlobaIlluminationRange( advancedGroup, renderingOptionsToMirror, c_source );
		
		// RTX
		video_settings::from_global_to_in_game::RayTracing( rtGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::RayTracedReflections( rtGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::RayTracedSunShadows( rtGroup, renderingOptionsToMirror, c_source );
		video_settings::from_global_to_in_game::RayTracedLighting( rtGroup, renderingOptionsToMirror, c_source );
		rend::settings::callbacks::OnProcessRayTracingTabVisibility();
#endif
	}
}

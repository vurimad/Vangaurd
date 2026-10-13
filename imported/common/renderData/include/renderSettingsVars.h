/**;
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderDataApi.h"
#include "renderQualityLevels.h"
#include "renderPublicEnums.h"

namespace InGameConfig
{
	enum class Source : Int8;
}

namespace Config
{
	struct RENDER_DATA_API MemoryInfo
	{
		Uint64		MemoryBudget_RAM{0};	// You must provide proper memory here
		Uint64		MemoryBudget_GPU{0};	// You must provide proper memory here
	};

	// The only place to keep rendering settings in.
	// It is available in both engine and renderer projects, just include this file and go GGlobalRenderingSettings.m_whatHaveYou.
	// These settings correspond to a particular GraphicsQualityLevel.
	struct RENDER_DATA_API GlobalRenderingSettings
	{
		MemoryInfo	Memory;

		// Are we rendering in safe quality mode? This mode is essentially Low combined with EMM_BaseColor debug view.
		// A quick way to save developers from image readibility issues when they do their tasks.
		Bool	SafeMode;

		//////////////////////////////////////////////////////////////////////////
		// RTX Lighting 
		Bool RTX_EnableDiffuseIllumination;
		Bool RTX_EnableAmbientOcclusion;
		Bool RTX_EnableGlobalIllumination;
		Uint8 RTX_AmbientOcclusionRayNumber;
		Uint8 RTX_ImportanceSamplingTransitionMin;
		Uint8 RTX_ImportanceSamplingTransitionMax;

		//////////////////////////////////////////////////////////////////////////
		// RTX Reflections
		Bool RTX_EnableReflections;
		Bool RTX_EnableTransparentReflections;

		//////////////////////////////////////////////////////////////////////////
		// RTX Shadows
		Bool RTX_EnableDirectionalShadow;

		//////////////////////////////////////////////////////////////////////////
		// General resolution control
		Bool ForceInternalResolutionScaling;
		Float MirrorFinalResolutionScale;		// eg. 0.9 (90%) of main camera resolution
		Float MirrorInternalResolutionScale;	// eg. Use 0.833 (83.3% - 900p out of 1080p) internal resolution

		//////////////////////////////////////////////////////////////////////////
		// Texturing quality
		Uint32	AnisotropyLevel;
		Float	TextureMipBias;

		//////////////////////////////////////////////////////////////////////////
		// Shadows quality
		Uint32	ShadowLODBias;
		Uint32	LocalShadowsResolution;
		Uint8	LocalShadowsProcessedPerFrame;
		Uint32	ShadowCascadeResolution;
		Uint32	NumShadowCascades;
		Uint32	DistantShadowmapResolution;
		Uint32	DistantShadowsMaxLevels;
		Bool	UnrestrictedShadowCubeUpdate;

		//////////////////////////////////////////////////////////////////////////
		// Rain map parameters
		Uint32	RainMapShadowsResolution;
		Float	RainMapCascade0Size;
		Float	RainMapCascade1Size;

		//////////////////////////////////////////////////////////////////////////
		// Volumetric Fog quality
		Uint32 VolFogTileSize;
		Uint32 VolFogNumSlices;
		Uint32 VolFogNumSamples;

		//////////////////////////////////////////////////////////////////////////
		// Decals related
		Float	DecalChance;
		Float	DecalSpawnDistanceCutoff;
		Int32	DynamicDecalsLimit;
		Int32	DynamicDecalsChunkLimit;
		Int32	DynamicDecalsSpawnerLimit;
		Int32	DynamicDecalsChunkPerTargetLimit;

		//////////////////////////////////////////////////////////////////////////
		// Screen Space Reflections

		Uint8	ScreenSpaceReflectionLevel;

		//////////////////////////////////////////////////////////////////////////
		// Skin quality
		Uint32	SubsurfaceQualityProfileIndex; // [1..3]

		//////////////////////////////////////////////////////////////////////////
		// Other
		Uint8 ConeAOQuality;
		Uint8 MotionBlurQuality;
		Bool UseHighPrecisionRenderTargets : 1;

		//////////////////////////////////////////////////////////////////////////
		// Some quality levels (cinematicEXR) should never support certain features
		Bool AllowColorCorrection : 1;
		Bool AllowFilmGrain : 1;
		Bool allowSimpleFog : 1;
		Bool allowVolumetricFog : 1;
		Bool allowVolumetricClouds : 1;
		Bool allowDistantFog : 1;
		Bool allowDistantVolFog : 1;
		Bool allowFXAA : 1;
		Bool allowTXAA : 1;
		Bool allowSSR : 1;
		Bool allowSSR_Planar : 1;
		Bool allowSSR_UberQuality : 1;	// Bumps quality of screen-space reflections for quest Q202
		Bool allowBloom : 1;
		Bool allowImageBasedFlares : 1;
		Bool allowDOF;
		Bool allowSSAO : 1;
		Bool allowMotionBlur : 1;
		Bool allowChromaticAberration : 1;
		Bool allowDistantShadows : 1;
		Bool allowRainMap : 1;
		Bool allowLocalShadows : 1;
		Bool allowGI : 1;
		Bool allowDistantGI : 1;
		Bool allowContactShadows_Global : 1;
		Bool allowContactShadows_Local : 1;
		Bool allowAtmosphere : 1;
		Bool allowReflectionProbes : 1;
		Bool allowMirrorMaterial : 1;
		Bool allowRuntimeTangentUpdate : 1;
		Bool allowRaytracing : 1;
		Bool allowAutoSpawnSystemShadows : 1;
		Bool allowCyberspacePostProcess : 1;

		Bool forceHighestLOD : 1; // CinematicEXR ONLY

		//////////////////////////////////////////////////////////////////////////
		// Animation related
		Bool UseMaxSkeletalLODOnEverything : 1;
		Bool SyncLipsyncToAudio : 1;	// it's turned off when recording cinematic videos

		//////////////////////////////////////////////////////////////////////////
		// Volumetric Clouds quality settings
		Uint32	volumetricCloudsDownsample;
		Uint32	volumetricCloudsNumSteps;
		Float	volumetricCloudsStepScale;
		Uint32	volumetricCloudsNumShadowSteps;
		Float	volumetricCloudsShadowStepScale;
		Uint32	volumetricCloudsShadowResolution;


		//////////////////////////////////////////////////////////////////////////
		// Global Illumination quality settings
		// Multiplier is used to extend the GI volume on the ultra settings by 2x
		struct
		{
			Uint32	volumeDistance;
			Uint32	maxSectors;
			Float	nodeStreamingData;
			Uint32  tetraWorkspaceMaxSlots;
			Uint32  tetraWorkspaceBudget;
			Uint32  accelerationStructureResolution;
			Uint32  volumeResolution;
			Bool	ultraOn;
		} globalIllumination;

		GlobalRenderingSettings();

		Bool MatchForPreset( const GlobalRenderingSettings& rhs ) const
		{
			return
				AllowFilmGrain == rhs.AllowFilmGrain &&
				allowChromaticAberration == rhs.allowChromaticAberration &&
				allowDOF == rhs.allowDOF &&
				allowImageBasedFlares == rhs.allowImageBasedFlares &&
				allowMotionBlur == rhs.allowMotionBlur &&
				MotionBlurQuality == rhs.MotionBlurQuality &&
				allowContactShadows_Local == rhs.allowContactShadows_Local &&
				allowContactShadows_Global == rhs.allowContactShadows_Global &&
				allowRuntimeTangentUpdate == rhs.allowRuntimeTangentUpdate &&
				LocalShadowsResolution == rhs.LocalShadowsResolution &&
				LocalShadowsProcessedPerFrame == rhs.LocalShadowsProcessedPerFrame &&
				NumShadowCascades == rhs.NumShadowCascades &&
				ShadowCascadeResolution == rhs.ShadowCascadeResolution &&
				DistantShadowmapResolution == rhs.DistantShadowmapResolution &&
				VolFogTileSize == rhs.VolFogTileSize &&
				VolFogNumSlices == rhs.VolFogNumSlices &&
				allowSSR == rhs.allowSSR &&
				allowSSR_Planar == rhs.allowSSR_Planar &&
				ScreenSpaceReflectionLevel == rhs.ScreenSpaceReflectionLevel &&
				SubsurfaceQualityProfileIndex == rhs.SubsurfaceQualityProfileIndex &&
				ConeAOQuality == rhs.ConeAOQuality &&
				UseHighPrecisionRenderTargets == rhs.UseHighPrecisionRenderTargets &&
				MirrorFinalResolutionScale == rhs.MirrorFinalResolutionScale &&
				MirrorInternalResolutionScale == rhs.MirrorInternalResolutionScale &&
				AnisotropyLevel == rhs.AnisotropyLevel;
		}

		Bool MatchForDebug( const GlobalRenderingSettings& rhs) const
		{
			return
				SafeMode == rhs.SafeMode &&
				ForceInternalResolutionScaling == rhs.ForceInternalResolutionScaling &&
				MirrorFinalResolutionScale == rhs.MirrorFinalResolutionScale &&
				MirrorInternalResolutionScale == rhs.MirrorInternalResolutionScale &&
				AnisotropyLevel == rhs.AnisotropyLevel &&
				TextureMipBias == rhs.TextureMipBias &&
				LocalShadowsResolution == rhs.LocalShadowsResolution &&
				LocalShadowsProcessedPerFrame == rhs.LocalShadowsProcessedPerFrame &&
				ShadowCascadeResolution == rhs.ShadowCascadeResolution &&
				NumShadowCascades == rhs.NumShadowCascades &&
				DistantShadowmapResolution == rhs.DistantShadowmapResolution &&
				DistantShadowsMaxLevels == rhs.DistantShadowsMaxLevels &&
				ScreenSpaceReflectionLevel == rhs.ScreenSpaceReflectionLevel &&
				UnrestrictedShadowCubeUpdate == rhs.UnrestrictedShadowCubeUpdate &&
				RainMapShadowsResolution == rhs.RainMapShadowsResolution &&
				RainMapCascade0Size == rhs.RainMapCascade0Size &&
				RainMapCascade1Size == rhs.RainMapCascade1Size &&
				VolFogTileSize == rhs.VolFogTileSize &&
				VolFogNumSlices == rhs.VolFogNumSlices &&
				DecalChance == rhs.DecalChance &&
				DecalSpawnDistanceCutoff == rhs.DecalSpawnDistanceCutoff &&
				DynamicDecalsLimit == rhs.DynamicDecalsLimit &&
				DynamicDecalsChunkLimit == rhs.DynamicDecalsChunkLimit &&
				DynamicDecalsSpawnerLimit == rhs.DynamicDecalsSpawnerLimit &&
				DynamicDecalsChunkPerTargetLimit == rhs.DynamicDecalsChunkPerTargetLimit &&
				SubsurfaceQualityProfileIndex == rhs.SubsurfaceQualityProfileIndex &&
				ConeAOQuality == rhs.ConeAOQuality &&
				MotionBlurQuality == rhs.MotionBlurQuality &&
				UseHighPrecisionRenderTargets == rhs.UseHighPrecisionRenderTargets &&
				AllowColorCorrection == rhs.AllowColorCorrection &&
				AllowFilmGrain == rhs.AllowFilmGrain &&
				allowSimpleFog == rhs.allowSimpleFog &&
				allowVolumetricFog == rhs.allowVolumetricFog &&
				allowDistantFog == rhs.allowDistantFog &&
				allowFXAA == rhs.allowFXAA &&
				allowTXAA == rhs.allowTXAA &&
				allowSSR == rhs.allowSSR &&
				allowSSR_Planar == rhs.allowSSR_Planar &&
				allowSSR_UberQuality == rhs.allowSSR_UberQuality &&
				allowBloom == rhs.allowBloom &&
				allowImageBasedFlares == rhs.allowImageBasedFlares &&
				allowDOF == rhs.allowDOF &&
				allowSSAO == rhs.allowSSAO &&
				allowMotionBlur == rhs.allowMotionBlur &&
				allowChromaticAberration == rhs.allowChromaticAberration &&
				allowDistantShadows == rhs.allowDistantShadows &&
				allowRainMap == rhs.allowRainMap &&
				allowLocalShadows == rhs.allowLocalShadows &&
				allowGI == rhs.allowGI &&
				allowContactShadows_Global == rhs.allowContactShadows_Global &&
				allowContactShadows_Local == rhs.allowContactShadows_Local &&
				allowAtmosphere == rhs.allowAtmosphere &&
				allowReflectionProbes == rhs.allowReflectionProbes &&
				allowRaytracing == rhs.allowRaytracing &&
				allowAutoSpawnSystemShadows == rhs.allowAutoSpawnSystemShadows &&
				allowCyberspacePostProcess == rhs.allowCyberspacePostProcess &&
				forceHighestLOD == rhs.forceHighestLOD &&
				UseMaxSkeletalLODOnEverything == rhs.UseMaxSkeletalLODOnEverything &&
				SyncLipsyncToAudio == rhs.SyncLipsyncToAudio &&
				allowRuntimeTangentUpdate == rhs.allowRuntimeTangentUpdate &&
				allowMirrorMaterial == rhs.allowMirrorMaterial &&
				volumetricCloudsDownsample == rhs.volumetricCloudsDownsample &&
				volumetricCloudsNumSteps == rhs.volumetricCloudsNumSteps &&
				volumetricCloudsStepScale == rhs.volumetricCloudsStepScale &&
				volumetricCloudsNumShadowSteps == rhs.volumetricCloudsNumShadowSteps &&
				volumetricCloudsShadowStepScale == rhs.volumetricCloudsShadowStepScale &&
				globalIllumination.volumeDistance == rhs.globalIllumination.volumeDistance &&
				globalIllumination.maxSectors == rhs.globalIllumination.maxSectors &&
				globalIllumination.nodeStreamingData == rhs.globalIllumination.nodeStreamingData &&
				globalIllumination.tetraWorkspaceBudget == rhs.globalIllumination.tetraWorkspaceBudget &&
				globalIllumination.accelerationStructureResolution == rhs.globalIllumination.accelerationStructureResolution &&
				globalIllumination.volumeResolution == rhs.globalIllumination.volumeResolution &&
				globalIllumination.ultraOn == rhs.globalIllumination.ultraOn &&
				globalIllumination.tetraWorkspaceMaxSlots == rhs.globalIllumination.tetraWorkspaceMaxSlots;
		}

		//
		// This procedure will pull InGameConfig variables related to Video Settings, interpret them and update the struct's entries.
		//
		// NOTE: If you plan to call this function on GGlobalRenderingSettings variable please make sure that it is done after
		//       flushing the renderer, so that frames that are in-flight do not pull anything from the global variable.
		//
		void UpdateWithInGameVideoSettings();
	};

	//
	// Use provided rendering settings to update relevant/coupled InGameConfig variables.
	//
	void MapRenderingSettingsToInGameSettings( const GlobalRenderingSettings& renderingOptionsToMirror, const InGameConfig::Source source );

	// These settings correspond to a particular TextureQualityLevel.
	struct RENDER_DATA_API GlobalTextureSettings
	{
		// How much memory does the platform provide.
		MemoryInfo	Memory;

		// How many mips should be dropped from each texture before it gets uploaded to the GPU memory
		Uint8	NumMipsToDrop;

#ifndef RED_PLATFORM_CONSOLE
		// On PC, we want to be able to emulate the bias of texture resource being applied on console cooking time.
		// If it is enabled, then each time we upload the texture, we apply the value of m_platformMipBiasConsole property
		// thus allowing the PC build to represent the texture quality that the console build provides.
		Bool	UseConsoleBias;
#endif
		GlobalTextureSettings();

		Bool MatchForDebug(const GlobalTextureSettings& rhs) const
		{
			Bool isMatch = NumMipsToDrop == rhs.NumMipsToDrop;

#ifndef RED_PLATFORM_CONSOLE
			isMatch &= UseConsoleBias == rhs.UseConsoleBias;
#endif
			return isMatch;
		}

#ifdef RED_PLATFORM_WINPC
		//
		// This procedure will pull InGameConfig variables related to Video Settings, interpret them and update the struct's entries.
		//
		void UpdateWithInGameVideoSettings();
#endif
	};

	RENDER_DATA_API extern TConfigVar< Bool >  cvFilmGrain;
	RENDER_DATA_API extern TConfigVar< Bool >  cvVolumetricFog;
	RENDER_DATA_API extern TConfigVar< Bool >  cvVolumetricClouds;
	RENDER_DATA_API extern TConfigVar< Bool >  cvDistantFog;
	RENDER_DATA_API extern TConfigVar< Bool >  cvDistantVolFog;
	RENDER_DATA_API extern TConfigVar< Bool >  cvContrastAdaptiveSharpening;
	RENDER_DATA_API extern TConfigVar< Bool >  cvAntialiasing;
	RENDER_DATA_API extern TConfigVar< Bool >  cvAntialiasingSuppressed;
	RENDER_DATA_API extern TConfigVar< Bool >  cvSSR;
	RENDER_DATA_API extern TConfigVar< Bool >  cvSSR_Planar;
	RENDER_DATA_API extern TConfigVar< Bool >  cvBloom;
	RENDER_DATA_API extern TConfigVar< Bool >  cvImageBasedFlares;
	RENDER_DATA_API extern TConfigVar< Bool >  cvChromaticAberration;
	RENDER_DATA_API extern TConfigVar< Bool >  cvDepthOfField;
	RENDER_DATA_API extern TConfigVar< Bool >  cvSSAO;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingOverrideEnable;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingEnable;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingEnableAmbientOcclusion;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingEnableDiffuseIllumination;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingEnableReflection;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingEnableMirrorMaterialReflection;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingEnableTransparentReflection;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingEnableDirectionalShadow;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingEnableShadowCasadesOptimization;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRayTracingEnableNRD;
	RENDER_DATA_API extern TConfigVar< Int32 > cvRayTracingAmbientOcclusionRayNumber;
	RENDER_DATA_API extern TConfigVar< Float > cvRayTracingDiffuseIlluminationAOModulation;
	RENDER_DATA_API extern TConfigVar< Int32 > cvRayTracingDebugMode;
	RENDER_DATA_API extern TConfigVar< Bool >  cvMotionBlur;
	RENDER_DATA_API extern TConfigVar< Bool >  cvDistantShadows;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRainMap;
	RENDER_DATA_API extern TConfigVar< Bool >  cvLocalShadows;
	RENDER_DATA_API extern TConfigVar< Bool >  cvGlobalIllumination;
	RENDER_DATA_API extern TConfigVar< Bool >  cvDistantGI;
	RENDER_DATA_API extern TConfigVar< Bool >  cvContactShadows;
	RENDER_DATA_API extern TConfigVar< Bool >  cvRuntimeTangentUpdate;
	RENDER_DATA_API extern TConfigVar< Bool >  cvDynamicDecals;
	RENDER_DATA_API extern TConfigVar< Bool >  cvHair;
	RENDER_DATA_API extern TConfigVar< Bool >  cvCharacterSubsurface;
	RENDER_DATA_API extern TConfigVar< Bool >  cvCharacterSubsurfaceTranslucency;

	// Cascades mesh volume heuristic.

	RENDER_DATA_API extern TConfigVar< Bool >	cvForceVolumeHeuristicRefresh;
	RENDER_DATA_API extern TConfigVar< Bool >	cvEnableCascadesVolumeHeuristic;
	RENDER_DATA_API extern TConfigVar< Float >	cvCascadeShadowReceiverBiasAlongNormal;
	RENDER_DATA_API extern TConfigVar< Float >  cvCascadeRange[MAX_CASCADES];
	RENDER_DATA_API extern TConfigVar< Float >  cvCascadeDepthBias[MAX_CASCADES];

	// Dissolve phase time in seconds
	RENDER_DATA_API extern TConfigVar< Float >	cvDissolveTime;

    RENDER_DATA_API extern TConfigVar< Bool >  cvUseSkinningLOD;

	// hair BEGIN
	RENDER_DATA_API extern TConfigVar< Float >  cvHairRoughnessFactor;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairAlbedoMultiplier;
	RENDER_DATA_API extern TConfigVar< Float >  cvHair_AdditionalAreaRoughness;
	RENDER_DATA_API extern TConfigVar< Bool >	cvHair_useReferenceImpl;
	RENDER_DATA_API extern TConfigVar< Bool >	cvHair_useGlobalContactShadows;
	RENDER_DATA_API extern TConfigVar< Bool >	cvHair_useLocalContactShadows;
	RENDER_DATA_API extern TConfigVar< Float >	cvHair_contactShadowsClamp;

	RENDER_DATA_API extern TConfigVar< Bool >	cvHair_debugSwitch1;
	RENDER_DATA_API extern TConfigVar< Bool >	cvHair_debugSwitch2;

	RENDER_DATA_API extern TConfigVar< Float >  cvHairR_Local;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairTT_Local;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairTRT_Local;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatter_Local;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatterDepth_Local;

	RENDER_DATA_API extern TConfigVar< Float >  cvHairR_Global;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairTT_Global;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairTRT_Global;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatter_Global;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatterDepth_Global;

	RENDER_DATA_API extern TConfigVar< Float >  cvHairR_EnvProbe;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairTT_EnvProbe;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairTRT_EnvProbe;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatter_EnvProbe;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatterDepth_EnvProbe;

	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatter_Wrap;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatter_DiffuseScatterFactor;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatter_ShadowFactorExp;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairScatter_MaskIntensity;

	RENDER_DATA_API extern TConfigVar< Float >  cvHairSpec_Wrap;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairSpec_MaskIntensity;

	RENDER_DATA_API extern TConfigVar< Float >  cvHairAlphaShift_R;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairAlphaShift_TT;
	RENDER_DATA_API extern TConfigVar< Float >  cvHairAlphaShift_TRT;

	RENDER_DATA_API extern TConfigVar< Float >  cvHair_TRT_EXPSCALE;
	RENDER_DATA_API extern TConfigVar< Float >  cvHair_TRT_EXPBIAS;
	RENDER_DATA_API extern TConfigVar< Float >  cvHair_SpecRandomMin;
	RENDER_DATA_API extern TConfigVar< Float >  cvHair_SpecRandomMax;

	RENDER_DATA_API extern TConfigVar< Float >  cvHACK_hairFactor0;
	RENDER_DATA_API extern TConfigVar< Float >  cvHACK_hairFactor1;
	RENDER_DATA_API extern TConfigVar< Float >  cvHACK_hairFactor2;
	RENDER_DATA_API extern TConfigVar< Float >  cvHACK_hairFactor3;
	RENDER_DATA_API extern TConfigVar< Bool >	cvHACK_hairModifiedLocalLightIntensity;
	// hair END

	RENDER_DATA_API extern TConfigVar< Float >  cvVolFog_Exponent;

	RENDER_DATA_API extern TConfigVar< Float > cvVolumetricCloudsDetailNoiseScale;
	RENDER_DATA_API extern TConfigVar< Float > cvVolumetricCloudsNoiseScale;
	RENDER_DATA_API extern TConfigVar< Float > cvVolumetricCloudsCoverageScale;

	// foliage BEGIN
	RENDER_DATA_API extern TConfigVar< Float >	cvFoliage_contactShadowClamp;
	// foliage END

	// dismemberment decals BEGIN
	//RENDER_DATA_API	extern TConfigVar< Float > cvDismembermentDecalFadeOrigin;
	//RENDER_DATA_API	extern TConfigVar< Float > cvDismembermentDecalFadePower;
	// dismemberment decals END

	RENDER_DATA_API extern TConfigVar< Bool >  cvWeather;
	RENDER_DATA_API extern TConfigVar< Bool >  cvScreenSpaceRain;
	RENDER_DATA_API extern TConfigVar< Bool >  cvScreenSpaceUnderwater;
	RENDER_DATA_API extern TConfigVar< Bool >  cvScreenSpaceHeatHaze;

	/***** Rim Enhancement *****/
	RENDER_DATA_API extern TConfigVar< Bool >  cvCharacterRim;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Skin_Specular;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Skin_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Skin_ConstOffset;

	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Character_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_RoughnessFactor_Scale;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_RoughnessFactor_Bias;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_LightBlockerInfluence;

	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Foliage_Specular;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Foliage_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Foliage_ConstOffset;

	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Weapon_Specular;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Weapon_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Weapon_ConstOffset;

	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Standard_Specular;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Standard_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float > cvCharacterRim_Standard_ConstOffset;

	/***** Rim Enhancement RayTracing *****/
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Skin_Specular;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Skin_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Skin_ConstOffset;

	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Foliage_Specular;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Foliage_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Foliage_ConstOffset;

	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Weapon_Specular;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Weapon_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Weapon_ConstOffset;

	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Standard_Specular;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Standard_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Standard_ConstOffset;

	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_Character_Fresnel;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_RoughnessFactor_Scale;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_RoughnessFactor_Bias;
	RENDER_DATA_API extern TConfigVar< Float >  cvCharacterRim_RayTracing_LightBlockerInfluence;

	RENDER_DATA_API extern TConfigVar< Bool >  cvCharacterLightBlockers;
	RENDER_DATA_API extern TConfigVar< Int32 > cvCharacterLightBlockers_Limit;

	RENDER_DATA_API extern TConfigVar< Float > cvSkin_SpecularTint_R;
	RENDER_DATA_API extern TConfigVar< Float > cvSkin_SpecularTint_G;
	RENDER_DATA_API extern TConfigVar< Float > cvSkin_SpecularTint_B;
	RENDER_DATA_API extern TConfigVar< Float > cvSkin_SpecularTint_Weight;

	RENDER_DATA_API extern GlobalRenderingSettings GGlobalRenderingSettings;
	RENDER_DATA_API extern GlobalTextureSettings GGlobalTextureSettings;

	RENDER_DATA_API GraphicsQualityLevel QualityLevelNameToEnum( const red::StringView& name );
	RENDER_DATA_API red::StringView EnumToQualityLevelName( GraphicsQualityLevel qlevel );

	RENDER_DATA_API RED_INLINE Uint32 GetDistantShadowsResolution() { return GGlobalRenderingSettings.DistantShadowmapResolution; };
	RENDER_DATA_API RED_INLINE Uint32 GetDistantShadowsMaxLevels() { return Min( MAX_DISTANT_SHADOWS_LEVELS, (Int32) GGlobalRenderingSettings.DistantShadowsMaxLevels ); };
	RENDER_DATA_API RED_INLINE Uint32 GetCascadedShadowsResolution() { return GGlobalRenderingSettings.ShadowCascadeResolution; };
	RENDER_DATA_API RED_INLINE Uint32 GetCascadedShadowsMaxCount() { return Min( MAX_CASCADES, ( Int32 ) GGlobalRenderingSettings.NumShadowCascades ); };

	RENDER_DATA_API Bool GetSettingsForQualityLevel( Uint32 qualityLevel, GlobalRenderingSettings& outSettings );

	RENDER_DATA_API void SelectQualityLevel( Uint32 qualityLevel, const MemoryInfo& mem = MemoryInfo() );
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_LOW();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_MEDIUM();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_HIGH();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_ULTRA();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_RTXMEDIUM();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_RTXULTRA();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_CINEMATIC();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_CINEMATIC_RTX();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_CINEMATICEXR();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_CINEMATICEXR_RTX();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_SAFEMODE();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_CONSOLE();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_CONSOLEPRO();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_CONSOLEEARLYNEXTGEN_PERFORMANCE();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_CONSOLEEARLYNEXTGEN_QUALITY();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_GEFORCENOW();
	RENDER_DATA_API GlobalRenderingSettings GetRenderSettings_AUTO( const MemoryInfo& mem );



	RENDER_DATA_API TextureQualityLevel TextureQualityLevelNameToEnum( const red::StringView& name );
	RENDER_DATA_API red::StringView EnumToTextureQualityLevelName( TextureQualityLevel qlevel );

#ifdef RED_PLATFORM_WINPC
	//
	// Tries to load texture quality saved in user local settings coming from last game session.
	// In case of call from a setup that do not operates on InGameConfig system, the procedure
	// defaults to TextureQualityLevel::High.
	//
	RENDER_DATA_API TextureQualityLevel TextureQualityFromInGameSettings();
#endif

	RENDER_DATA_API Uint32 DetectQualityLevel( const MemoryInfo& mem );

	RENDER_DATA_API Bool GetSettingsForTextureQualityLevel( Uint32 qualityLevel, GlobalTextureSettings& outSettings );

	RENDER_DATA_API void SelectTextureQualityLevel( Uint32 qualityLevel, const MemoryInfo& mem = MemoryInfo() );

	RENDER_DATA_API GlobalTextureSettings GetTextureSettings_LOW();
	RENDER_DATA_API GlobalTextureSettings GetTextureSettings_MEDIUM();
	RENDER_DATA_API GlobalTextureSettings GetTextureSettings_HIGH();
	RENDER_DATA_API GlobalTextureSettings GetTextureSettings_SAFEMODE();
	RENDER_DATA_API GlobalTextureSettings GetTextureSettings_AUTO( const MemoryInfo& mem );

	/***** Shader Compiler *****/
	RENDER_DATA_API extern TConfigVar<Bool> cvShaderCompilerOptimize;
	RENDER_DATA_API extern TConfigVar<Bool> cvShaderCompilerDumpAssembly;
	RENDER_DATA_API extern TConfigVar<Bool> cvShaderCompilerForceRecompile;
	RENDER_DATA_API extern TConfigVar<Bool> cvShaderCompilerUseLocalCompiler;

	/***** Level of Detail *****/
	RENDER_DATA_API extern TConfigVar<Float> cvDecalsHideDistance;
	RENDER_DATA_API extern TConfigVar<Float> cvDynDecalsHideDistance;


	/***** Other *****/
	RENDER_DATA_API extern TConfigVar< Float >	cvGamma;
	RENDER_DATA_API extern TConfigVar< Float >	cvMidPoint;
	RENDER_DATA_API extern TConfigVar< Float >	cvMotionBlurScale;
	RENDER_DATA_API extern TConfigVar< Float >	cvDebugVelocityBufferScale;

	/***** DLSS *****/
	RENDER_DATA_API extern TConfigVar< Bool >	cvDLSSEnable;
	RENDER_DATA_API extern TConfigVar< Int32 >	cvDLSSQuality;
	RENDER_DATA_API extern TConfigVar< Float >	cvDLSSSharpness;

	namespace Helper
	{
		//! Convert "1600x900" to 1600 and 900
		RENDER_DATA_API Bool ExtractDisplayModeValues( const String& mode, Int32& width, Int32& height );

		//! Convert 1600 and 900 to "1600x900"
		RENDER_DATA_API String PackDisplayModeValues( const Int32 width, const Int32 height );

		//! One call to check if Antialias is on and not suppressed
		RENDER_DATA_API Bool IsAntialiasOn();
	}
}

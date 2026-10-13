/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once


/// Camera graph features
enum ERenderFeatureFlags : Uint32
{
	RFF_Tonemapping						,
	RFF_Bloom							,
	RFF_ImageBasedFlares				,
	RFF_AntialiasingFXAA				,
	RFF_ContrastAdaptiveSharpening		,
	RFF_DepthOfField					,
	RFF_CutsceneDOF						,
	RFF_Vignette						,
	RFF_Sharpen							,
	RFF_CascadeShadows					,
	RFF_LocalShadows					,
	RFF_DistantShadows					,
	RFF_RainMap							,
	RFF_ContactShadows_Global			,
	RFF_ContactShadows_Local			,
	RFF_SSAO							,
	RFF_EyeAdaptation					,
	RFF_MotionBlur						,
	RFF_ChromaticAberration				,
	RFF_AntialiasingTXAA				,
	RFF_ScreenSpaceReflections			,
	RFF_ScreenSpacePlanarReflections	,
	RFF_SimpleFog						,
	RFF_VolumetricFog					,
	RFF_VolumetricClouds				,
	RFF_DistantFog						,
	RFF_SubsurfaceScattering			,
	RFF_SkinTranslucency				,
	RFF_RuntimeTangentUpdate			,
	RFF_FilmGrain						,
	RFF_GlobalIlluminationCompute		,
	RFF_GlobalIllumination				,
	RFF_DistantGI						,
	RFF_DistantVolumetricFog			,
	RFF_ReflectionProbes				,
	RFF_Hair							,
	RFF_Selection						,
	RFF_Wrinklemaps						,
	RFF_ColorCorrection					,
	RFF_Atmosphere						,
	RFF_WeaponPlaneBlur					,
	RFF_RayTracedAmbientOcclusion		,
	RFF_RayTracedDiffuseIllumination	,
	RFF_RayTracedDirectionalShadow		,
	RFF_RayTracedReflection				,
	RFF_RayTracedTranparentReflection	,
	RFF_RayTracingNRD					,
	RFF_RayTracingDebug					,
	RFF_MirrorMaterial					,
	RFF_DynamicDecals					,
	RFF_CharacterRimEnhancement			,
	RFF_CharacterLightBlockers			,
	RFF_DepthExtraction					,
	RFF_GBufferExtraction				,
	RFF_ColorExtraction					,
	RFF_BackgroundDump					,
	RFF_HairReference					,
	RFF_ImmediateMode					,
	RFF_DebugColoring					,
	RFF_DLSS							,
	RFF_ScreenSpaceRain					,
	RFF_ScreenSpaceUnderwater			,
	RFF_ScreenSpaceHeatHaze				,
	RFF_FocusMode						,
	RFF_GlobalLight						,
	RFF_Particles						,
	RFF_HUD								,
	RFF_Simplified						,

	// #TODO: Do we want this?
	RFF_EnableAsyncCompute				, // Globally enable/disable use of async compute

	// Dependent features. Since we do not have any hierarchy in termsof RFFs, 
	// I will just separate it here. These flags will be set based on the ones above. 
	// In this way we have more or less coherent set of per-camera flags
	//  that we can use to build render graph.
	RFF_VelocityBuffer,
	RFF_ClearCompositionDuringCAS,

	RFF_Max
};

using RenderFeatureSet = red::BitSet64< RFF_Max >;

RENDER_DATA_API const RenderFeatureSet& AllowedRenderFeatureSet_DefaultOnScreen();
RENDER_DATA_API const RenderFeatureSet& AllowedRenderFeatureSet_DefaultOffScreen();

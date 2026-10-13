/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "renderFeatureFlags.h"


RENDER_DATA_API const RenderFeatureSet& AllowedRenderFeatureSet_DefaultOnScreen()
{
	static const RenderFeatureSet defaultOnScreen = []()
	{
		RenderFeatureSet rff;
		rff.SetAll();
		return rff;
	}();
	return defaultOnScreen;
}

RENDER_DATA_API const RenderFeatureSet& AllowedRenderFeatureSet_DefaultOffScreen()
{
	static const RenderFeatureSet defaultOffScreen = []()
	{
		RenderFeatureSet rff;
		rff.SetAll();

		// Turn off some features we don't necessarily need in offscreen renders.
		// #TODO: Can probably disable more things? Can always get re-enabled by caller for specific cases.
		rff.Clear( RFF_AntialiasingTXAA );
		rff.Clear( RFF_AntialiasingFXAA );
		rff.Clear( RFF_Vignette );
		rff.Clear( RFF_ChromaticAberration );
		rff.Clear( RFF_Bloom );
		rff.Clear( RFF_RayTracedAmbientOcclusion );
		rff.Clear( RFF_RayTracedDiffuseIllumination );
		rff.Clear( RFF_RayTracedDirectionalShadow );
		rff.Clear( RFF_RayTracedReflection );
		rff.Clear( RFF_RayTracedTranparentReflection );
		rff.Clear( RFF_RayTracingNRD );
		rff.Clear( RFF_RayTracingDebug );
		rff.Clear( RFF_ContrastAdaptiveSharpening );
		rff.Clear( RFF_DLSS );
		rff.Clear( RFF_ContactShadows_Global );
		rff.Clear( RFF_ContactShadows_Local );
		rff.Clear( RFF_LocalShadows );
		rff.Clear( RFF_DistantShadows );
		rff.Clear( RFF_MirrorMaterial );
		rff.Clear( RFF_HUD );
		rff.Clear( RFF_FocusMode );
		rff.Clear( RFF_FilmGrain );
		rff.Clear( RFF_VolumetricFog );
		rff.Clear( RFF_VolumetricClouds );
		rff.Clear( RFF_DistantFog );
		rff.Clear( RFF_DistantVolumetricFog );

		return rff;
	}();
	return defaultOffScreen;
}

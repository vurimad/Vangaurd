/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderingMemory.h"

namespace rend
{
	struct FoliageProfileData
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );

		//Make these constants the same like in speedtree_3d_v8.fx
		static constexpr Float MAX_TERRAIN_BLEND_SCALE	= 2.0f;
		static constexpr Float MAX_TERRAIN_BLEND_BIAS	= 3.0f;
		static constexpr Float MAX_BILLBOARD_DEPTH_SCALE = 4.0f;

		//Whenever adding a new param to a profile, remember to modify MAX_FLOAT4S_PER_PROFILE in CreateFoliageProfileTexture()
		Float	cutoffAlphaMinMip;
		Float	cutoffAlphaMaxMip;
		Float	billboardCutoffAlpha;
		Float	aoScale;
		Float	terrainBlendScale;
        Float   terrainBlendBias;
		Float	billboardDepthScale;
		Float	preserveOriginalColor;
		Float	billboardRoughnessBias;

		//Parameters not included directly in Foliage Texture 
		TRenderPtr<IRenderResource> colorGradient;
		Float colorGradientWeight;
		Float colorGradientDarkenWeight;
	
		FoliageProfileData() :
			cutoffAlphaMinMip( 0.45f )
			, cutoffAlphaMaxMip( 0.12f )
			, billboardCutoffAlpha( 0.6f )
			, aoScale( 0.5f )
			, terrainBlendScale( 0.2f )
			, terrainBlendBias( 0.f )
			, colorGradientWeight( 0.0f )
			, colorGradientDarkenWeight( 0.0f )
			, billboardDepthScale( 1.0f )
			, preserveOriginalColor( 0.0f )
			, billboardRoughnessBias( 0.1f )
		{}
	
		void Invalidate()
		{
			cutoffAlphaMinMip = 0.45f;
			cutoffAlphaMaxMip = 0.12f;
			billboardCutoffAlpha = 0.6f;
			aoScale = 0.5f;
			terrainBlendScale = 0.2f;
            terrainBlendBias = 0.f;
			colorGradientWeight = 0.0f;
			colorGradientDarkenWeight = 0.0f;
			billboardDepthScale = 1.0f;
			preserveOriginalColor = 0.0f;
			billboardRoughnessBias = 0.1f;
		}
	};


	struct AutoFoliageRuntimeMap
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );

		static constexpr Uint32 MAX_ITEMS = 64;
		struct Item
		{
			TRenderPtr<IRenderResource> meshResource;
			TRenderPtr<IRenderProxy> meshRenderProxy;
			Float proximity = 1.f;
			Float scale = 1.f;
			Float scaleVariation = 0.1f;

			Uint32 layerIndex = 0;
			// for debug
			CName materialPath;
		};
		red::StaticArray<Item, MAX_ITEMS> items; // if changed to DynArray, then need to make some changes also in RuntimeSystemFoliage::PrepareFoliageMapping to not assume that there's no reallocation
	};

} // rend

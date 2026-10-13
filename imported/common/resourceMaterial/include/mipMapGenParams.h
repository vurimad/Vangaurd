/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redReflection/include/resourceAsyncReference.h"

class RESOURCEMATERIAL_API CBitmapTexture;

//TODO: We should move STextureGroupSetup::m_alphaToCoverageThreshold and other mip related params from STextureGroupSetup at some point
struct RESOURCEMATERIAL_API MipMapGenParams
{
	RTTI_DECLARE_TYPE( MipMapGenParams );

	static const MipMapGenParams& NullParams()
	{
		static MipMapGenParams nullParams;
		return nullParams;
	}

	//Toksvig Roughness mip maps generation parameters.
	//The solution fits the mipmap value (roughness -> specular lobe) to the distribution of normals in the footprint of the given mipmap texel (normals from the base mip)
	Bool							m_applyToksvig_ShouldInvChannel{ false }; // In case of SpeedTree texture we store glossiness instead of roughness so in that case we have to invert the channel
	Uint8							m_applyToksvig_Channel{ 0 };// index of the channel containing roughness
	TResAsyncRef< CBitmapTexture >	m_applyToksvig_sourceNormalMap; // Must be uncompressed and contain 3 channels
};
/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "textureEnums.h"

RTTI_BEGIN_ENUM( ETextureRawFormat );
{
	RTTI_ENUM_OPTION( TRF_Invalid );
	RTTI_ENUM_OPTION( TRF_TrueColor );
	RTTI_ENUM_OPTION( TRF_DeepColor );
	RTTI_ENUM_OPTION( TRF_Grayscale );
	RTTI_ENUM_OPTION( TRF_HDRFloat );
	RTTI_ENUM_OPTION( TRF_HDRHalf );
	RTTI_ENUM_OPTION( TRF_HDRFloatGrayscale );
	RTTI_ENUM_OPTION( TRF_Grayscale_Font );
	RTTI_ENUM_OPTION( TRF_R8G8 );
	RTTI_ENUM_OPTION( TRF_R32UI );
	RTTI_ENUM_OPTION_ALIAS( "TRF_AlphaGrayscale", TRF_Invalid ); // that was removed
}
RTTI_END_ENUM();
static_assert(TRF_Max == 10, "ETextureRawFormat changed");

RTTI_BEGIN_ENUM( ETextureCompression );
{
	RTTI_ENUM_OPTION( TCM_None );
	RTTI_ENUM_OPTION( TCM_DXTNoAlpha );
	RTTI_ENUM_OPTION( TCM_DXTAlpha );
	RTTI_ENUM_OPTION( TCM_RGBE );
	RTTI_ENUM_OPTION( TCM_Normalmap );
	RTTI_ENUM_OPTION( TCM_Normals_DEPRECATED ).legacyName( "TCM_Normals" );
	RTTI_ENUM_OPTION( TCM_NormalsHigh_DEPRECATED ).legacyName( "TCM_NormalsHigh" );
	RTTI_ENUM_OPTION( TCM_NormalsGloss_DEPRECATED ).legacyName( "TCM_NormalsGloss" );
	RTTI_ENUM_OPTION( TCM_TileMap );
	RTTI_ENUM_OPTION( TCM_DXTAlphaLinear );
	RTTI_ENUM_OPTION( TCM_QualityR );
	RTTI_ENUM_OPTION( TCM_QualityRG );
	RTTI_ENUM_OPTION( TCM_QualityColor );
	RTTI_ENUM_OPTION( TCM_HalfHDR_Unsigned ).legacyName( "TCM_HalfHDR" );
	RTTI_ENUM_OPTION( TCM_HalfHDR_Signed );
	RTTI_ENUM_OPTION( TCM_Max );
}
RTTI_END_ENUM();
static_assert(TCM_Max == 15, "ETextureCompression changed");

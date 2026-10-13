/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "textureGroup.h"

RTTI_DECLARE_BITFIELD( ETextureChannel );

RTTI_BEGIN_BITFIELD( ETextureChannel, 1 );
	RTTI_BITFIELD_OPTION( TextureChannel_R );
	RTTI_BITFIELD_OPTION( TextureChannel_G );
	RTTI_BITFIELD_OPTION( TextureChannel_B );
	RTTI_BITFIELD_OPTION( TextureChannel_A );
RTTI_END_BITFIELD();

RTTI_BEGIN_TYPE( STextureGroupSetup );
{
	RTTI_PROPERTY( m_group ).editable();
	RTTI_PROPERTY( m_rawFormat ).editable();
	RTTI_PROPERTY( m_compression ).editable();
	RTTI_PROPERTY( m_isStreamable ).editable();
	RTTI_PROPERTY( m_hasMipchain ).editable();
	RTTI_PROPERTY( m_isGamma ).editable();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_alphaToCoverageThreshold ).editable();
	RTTI_PROPERTY( m_stretchHistogram ).editable();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_platformMipBiasPC ).editable();
	RTTI_PROPERTY( m_platformMipBiasConsole ).editable();
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY_BITFIELD( m_alphaToCoverageThreshold_Channel, ETextureChannel ).editable().notCooked();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_allowTextureDowngrade ).cookedOnly();
}
RTTI_END_TYPE();


//*****************************************************************************************************************************//
//*****************************************************************************************************************************//

STextureGroupSetup::STextureGroupSetup()
	: m_group( GpuWrapApi::TEXG_Generic_Color )
	, m_isStreamable( true )
	, m_compression( TCM_None )
	, m_rawFormat( TRF_TrueColor )
	, m_platformMipBiasPC( 0 )
	, m_platformMipBiasConsole( 0 )
	, m_hasMipchain( true )
	, m_isGamma( false )
#ifndef RED_CONFIGURATION_FINAL
	, m_stretchHistogram( false )
	, m_alphaToCoverageThreshold( 0 )
	, m_alphaToCoverageThreshold_Channel( (Uint8)ETextureChannel::TextureChannel_A ) // apply threshold to Alpha by default
#endif // RED_CONFIGURATION_FINAL
	, m_allowTextureDowngrade( true )
{}

STextureGroupSetup::~STextureGroupSetup()
{}

GpuWrapApi::eTextureImportFormat TexGroupHelpers::FileExtensionToFormat( const red::StringView extension )
{
	struct
	{
		GpuWrapApi::eTextureImportFormat	m_format;
		red::StringView						m_extension;
	} Mapper[] =
	{
		{ GpuWrapApi::TIF_DDS,	"dds", },
		{ GpuWrapApi::TIF_TGA,	"tga", },
		{ GpuWrapApi::TIF_EXR,	"exr", },
		{ GpuWrapApi::TIF_CUBE,	"cube", },
	};

	for( Int32 i = 0; i < RED_ARRAY_COUNT( Mapper ); i++ )
	{
		if( extension == Mapper[i].m_extension )
		{
			return Mapper[i].m_format;
		}
	}

	return GpuWrapApi::TIF_WIC;
}

void TexGroupHelpers::MapGpuToEngineTextureFormat( const GpuWrapApi::eTextureFormat texFormat, ETextureRawFormat& outRawFormat, ETextureCompression& outCompression, Uint32& outPixelSize )
{
	switch( texFormat )
	{
	case GpuWrapApi::TEXFMT_R32G32B32A32_Float:
		outRawFormat = TRF_HDRFloat;
		outCompression = TCM_HalfHDR_Unsigned;
		outPixelSize = 16;
		break;

	case GpuWrapApi::TEXFMT_R16G16B16A16_Float:
		outRawFormat = TRF_HDRHalf;
		outCompression = TCM_HalfHDR_Unsigned;
		outPixelSize = 8;
		break;

	case GpuWrapApi::TEXFMT_R16G16B16A16_Unorm:
		outRawFormat = TRF_DeepColor;
		outCompression = TCM_QualityColor;
		outPixelSize = 8;
		break;

	case GpuWrapApi::TEXFMT_R32_Float:
		outRawFormat = TRF_HDRFloatGrayscale;
		outCompression = TCM_QualityR;
		outPixelSize = 4;
		break;

	case GpuWrapApi::TEXFMT_A8_Unorm:
	case GpuWrapApi::TEXFMT_L8_Unorm:
		// Load as gray scale image
		outRawFormat = TRF_Grayscale;
		outCompression = TCM_QualityR;
		outPixelSize = 1;
		break;

	case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm:
		outRawFormat = TRF_TrueColor;
		outCompression = TCM_QualityColor;
		outPixelSize = 4;
		break;

	case GpuWrapApi::TEXFMT_R8G8_Unorm: //do I need this?
		outRawFormat = TRF_R8G8;
		outCompression = TCM_None;
		outPixelSize = 2;
		break;

	default:
		// Load as True Color image
		// TODO: L.Kadej - Check this!
		outRawFormat = TRF_TrueColor;
		outCompression = TCM_QualityColor;
		outPixelSize = 4;
		break;
	}
}

void TexGroupHelpers::CreateBaseTextureGroup( const GpuWrapApi::eTextureFormat texFormat, STextureGroupSetup& outSetup )
{
	switch( texFormat )
	{
	case GpuWrapApi::TEXFMT_R32G32B32A32_Float:
		outSetup.m_rawFormat	= TRF_HDRFloat;
		outSetup.m_compression = TCM_HalfHDR_Unsigned;
		break;

	case GpuWrapApi::TEXFMT_R16G16B16A16_Float:
		outSetup.m_rawFormat	= TRF_HDRHalf;
		outSetup.m_compression = TCM_HalfHDR_Unsigned;
		break;

	case GpuWrapApi::TEXFMT_R16G16B16A16_Unorm:
		outSetup.m_rawFormat	= TRF_DeepColor;
		outSetup.m_compression	= TCM_QualityColor;
		break;

	case GpuWrapApi::TEXFMT_R32_Float:
		outSetup.m_rawFormat	= TRF_HDRFloatGrayscale;
		outSetup.m_compression	= TCM_QualityR;
		break;

	case GpuWrapApi::TEXFMT_A8_Unorm:
	case GpuWrapApi::TEXFMT_L8_Unorm:
		// Load as gray scale image
		outSetup.m_rawFormat	= TRF_Grayscale;
		outSetup.m_compression	= TCM_QualityR;
		break;

	default:
		// Load as True Color image
		outSetup.m_rawFormat	= TRF_TrueColor;
		outSetup.m_compression	= TCM_DXTAlphaLinear;
		break;
	}
}

void TexGroupHelpers::CreateBaseTextureGroup( const GpuWrapApi::eTextureGroup textureGroup, STextureGroupSetup& outSetup )
{
	static_assert( GpuWrapApi::TEXG_MAX == 34, "Check new mapping here" );
	static_assert( GpuWrapApi::TEXG_Generic_End - GpuWrapApi::TEXG_Generic_Begin == 7, "Check new mapping here" );
	static_assert( GpuWrapApi::TEXG_Multilayer_End - GpuWrapApi::TEXG_Multilayer_Begin == 4, "Check new mapping here" );

	outSetup = STextureGroupSetup();

	outSetup.m_group = textureGroup;

	switch( textureGroup )
	{

	case GpuWrapApi::TEXG_Generic_Color:
	case GpuWrapApi::TEXG_Multilayer_Color:
	case GpuWrapApi::TEXG_Multilayer_Microblend:
		outSetup.m_rawFormat	= TRF_TrueColor;
		outSetup.m_compression	= TCM_QualityColor;
		outSetup.m_hasMipchain	= true;
		outSetup.m_isStreamable	= true;
		break;

	case GpuWrapApi::TEXG_Generic_Normal:
	case GpuWrapApi::TEXG_Multilayer_Normal:
		outSetup.m_rawFormat	= TRF_TrueColor;
		outSetup.m_compression	= TCM_Normalmap;
		outSetup.m_hasMipchain	= true;
		outSetup.m_isStreamable	= true;
		break;

	case GpuWrapApi::TEXG_Generic_Data:
		outSetup.m_rawFormat	= TRF_TrueColor;
		outSetup.m_compression	= TCM_None;
		outSetup.m_hasMipchain	= false;
		outSetup.m_isStreamable	= true;
		break;

	case GpuWrapApi::TEXG_Generic_UI:
		outSetup.m_rawFormat	= TRF_TrueColor;
		outSetup.m_compression	= TCM_DXTAlpha;
		outSetup.m_hasMipchain	= false;
		outSetup.m_isStreamable	= false;
		break;

	case GpuWrapApi::TEXG_Generic_Grayscale:
	case GpuWrapApi::TEXG_Multilayer_Grayscale:
		outSetup.m_rawFormat = TRF_Grayscale;
		outSetup.m_compression = TCM_QualityR;
		outSetup.m_hasMipchain = true;
		outSetup.m_isStreamable = true;
		break;

	case GpuWrapApi::TEXG_Generic_Font:
		outSetup.m_rawFormat	= TRF_Grayscale_Font;
		outSetup.m_compression	= TCM_None;
		outSetup.m_hasMipchain	= false;
		outSetup.m_isStreamable	= false;
		break;

	case GpuWrapApi::TEXG_Generic_LUT:
		outSetup.m_rawFormat = TRF_DeepColor;
		outSetup.m_compression = TCM_None;
		outSetup.m_hasMipchain = false;
		outSetup.m_isStreamable = false;
		break;

	default:
		RED_FATAL( "Unknown texture group" );
		break;

	}

}

//*****************************************************************************************************************************//
//*****************************************************************************************************************************//

/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderSimpleTypes.h"

//----------------------------------------

// Type of texture compression, keep it the same as in renderBackend project for cooking compatibility
enum ETextureCompression : Int32
{
	TCM_None,
	TCM_DXTNoAlpha,
	TCM_DXTAlpha,
	TCM_RGBE,
	TCM_Normalmap,
	TCM_Normals_DEPRECATED,
	TCM_NormalsHigh_DEPRECATED,
	TCM_NormalsGloss_DEPRECATED,
	TCM_TileMap,
	TCM_DXTAlphaLinear,
	TCM_QualityR,
	TCM_QualityRG,
	TCM_QualityColor,
	TCM_HalfHDR_Unsigned,
	TCM_HalfHDR_Signed,

	TCM_Max
};

RTTI_DECLARE_ENUM( ETextureCompression );

//----------------------------------------

// Texture raw data format ( editor data ), keep it the same as in renderBackend project for cooking compatibility
enum ETextureRawFormat : Int32
{
	TRF_Invalid = 0,
	TRF_TrueColor,			// 32-bit true color texture
	TRF_DeepColor,			// 64-bit deep color texture ( 64-bit unorm )
	TRF_Grayscale,			// Alpha only texture ( 8-bit )
	TRF_HDRFloat,			// HDR texture ( 128-bit )
	TRF_HDRHalf,			// HDR texture ( 64-bit float )
	TRF_HDRFloatGrayscale,	// HDR Grayscale ( 32-bit )

	TRF_Grayscale_Font,
	TRF_R8G8,
	TRF_R32UI,

	TRF_Max
};

RTTI_DECLARE_ENUM( ETextureRawFormat );

namespace Texture
{

	RED_INLINE Bool IsFormatFloat( const ETextureRawFormat rawFormat )
	{
		return rawFormat == TRF_HDRFloat || rawFormat == TRF_HDRHalf || rawFormat == TRF_HDRFloatGrayscale;
	}

	RED_INLINE Bool IsGamma( const ETextureRawFormat rawFormat )
	{
		return rawFormat == TRF_HDRFloat || rawFormat == TRF_HDRHalf || rawFormat == TRF_DeepColor || rawFormat == TRF_TrueColor;
	}

	RED_INLINE Uint8 GetPixelSizeInBits( ETextureRawFormat rawFormat )
	{
		static_assert(TRF_Max == 10, "Missing mapping");
		switch( rawFormat )
		{
		case TRF_TrueColor:			return 32;
		case TRF_DeepColor:			return 64;
		case TRF_Grayscale:			return 8;
		case TRF_Grayscale_Font:	return 8;
		case TRF_HDRHalf:			return 64;
		case TRF_HDRFloat:			return 128;
		case TRF_HDRFloatGrayscale: return 32;
		case TRF_R8G8:				return 16;
		case TRF_R32UI:				return 32;
		}

		RED_ASSERT( false, "Unknown texture raw format" );
		return 32;
	}

	//----------------------------------------

	RED_INLINE Uint8 GetPixelSizeInBytes( ETextureRawFormat rawFormat )
	{
		static_assert(TRF_Max == 10, "Missing mapping");
		switch( rawFormat )
		{
		case TRF_TrueColor:				return 4;
		case TRF_DeepColor:				return 8;
		case TRF_Grayscale:				return 1;
		case TRF_Grayscale_Font:		return 1;
		case TRF_HDRHalf:				return 8;
		case TRF_HDRFloat:				return 16;
		case TRF_HDRFloatGrayscale:		return 4;
		case TRF_R8G8:					return 2;
		case TRF_R32UI:					return 4;
		}

		RED_ASSERT( false, "Unknown texture raw format" );
		return 4;
	}

	//----------------------------------------

	RED_INLINE Uint8 GetNumChannels( ETextureRawFormat rawFormat )
	{
		static_assert(TRF_Max == 10, "Missing mapping");
		switch( rawFormat )
		{
		case TRF_TrueColor:			return 4;
		case TRF_DeepColor:			return 4;
		case TRF_Grayscale:			return 1;
		case TRF_Grayscale_Font:	return 1;
		case TRF_HDRHalf:			return 4;
		case TRF_HDRFloat:			return 4;
		case TRF_HDRFloatGrayscale:	return 1;
		case TRF_R8G8:				return 2;
		case TRF_R32UI:				return 1;
		}

		RED_ASSERT( false, "Unknown texture raw format" );
		return 4;
	}


	RED_INLINE GpuWrapApi::eTextureFormat GetCompressedFormat( ETextureRawFormat format, ETextureCompression compression, Bool sRGB = false )
	{
		static_assert(TRF_Max == 10, "Missing mapping");
		static_assert(TCM_Max == 15, "Missing mapping");

		switch( compression )
		{
		case TCM_None:
			switch( format )
			{
			case TRF_TrueColor:				return sRGB ? GpuWrapApi::TEXFMT_R8G8B8A8_Unorm_SRGB : GpuWrapApi::TEXFMT_R8G8B8A8_Unorm;
			case TRF_Grayscale: 			return GpuWrapApi::TEXFMT_L8_Unorm;
			case TRF_Grayscale_Font:		return GpuWrapApi::TEXFMT_A8_Unorm;
			case TRF_HDRFloat:				return GpuWrapApi::TEXFMT_R32G32B32A32_Float;
			case TRF_HDRHalf:				return GpuWrapApi::TEXFMT_R16G16B16A16_Float;
			case TRF_DeepColor:				return GpuWrapApi::TEXFMT_R16G16B16A16_Unorm;
			case TRF_HDRFloatGrayscale:		return GpuWrapApi::TEXFMT_R32_Float;
			case TRF_R8G8:					return GpuWrapApi::TEXFMT_R8G8_Unorm;
			case TRF_R32UI:					return GpuWrapApi::TEXFMT_R32_Uint;
			}
			RED_ASSERT( false, "Unknown source format" );
			break;

		case TCM_DXTNoAlpha:				return sRGB ? GpuWrapApi::TEXFMT_BC1_SRGB : GpuWrapApi::TEXFMT_BC1;
		case TCM_DXTAlpha:					return sRGB ? GpuWrapApi::TEXFMT_BC3_SRGB : GpuWrapApi::TEXFMT_BC3;
		case TCM_DXTAlphaLinear:			return sRGB ? GpuWrapApi::TEXFMT_BC3_SRGB : GpuWrapApi::TEXFMT_BC3; // TODO remove?
		case TCM_RGBE:						return GpuWrapApi::TEXFMT_R32G32B32A32_Float;
		case TCM_Normalmap:					return GpuWrapApi::TEXFMT_BC5;
		case TCM_Normals_DEPRECATED:		return GpuWrapApi::TEXFMT_BC1;
		case TCM_NormalsHigh_DEPRECATED:	return GpuWrapApi::TEXFMT_BC3;
		case TCM_NormalsGloss_DEPRECATED:	return GpuWrapApi::TEXFMT_BC3;
		case TCM_QualityR:					return GpuWrapApi::TEXFMT_BC4;
		case TCM_QualityRG:					return GpuWrapApi::TEXFMT_BC5;
		case TCM_QualityColor:				return sRGB ? GpuWrapApi::TEXFMT_BC7_SRGB : GpuWrapApi::TEXFMT_BC7;
		case TCM_HalfHDR_Unsigned:			return GpuWrapApi::TEXFMT_BC6H_UNSIGNED;
		case TCM_HalfHDR_Signed:			return GpuWrapApi::TEXFMT_BC6H_SIGNED;
		default: break;
		}

		RED_HALT( "Unknown compression/format: %i/%i", compression, format );
		return GpuWrapApi::TEXFMT_NULL;
	}


	RED_INLINE Uint32 GetNumRows( Uint32 width, Uint32 height, ETextureRawFormat format, ETextureCompression compression )
	{
		Uint32 rows = height;

		const GpuWrapApi::eTextureFormat gpuFormat = GetCompressedFormat( format, compression );
		if ( GpuWrapApi::IsTextureFormatDXT( gpuFormat ) )
		{
			// Round up the number of rows, because if this is being used as some mip>0 on a non-pow2 texture, there may be
			// an incomplete row in the BC data. In this case, the size should be padded up to a multiple of 4, not truncated.
			rows = Max< Uint32 >( 1, (height + 3) / 4 );
		}

		return rows;
	}

	RED_INLINE Uint32 GetPitch( Uint32 width, Uint32 height, ETextureRawFormat format, ETextureCompression compression )
	{
		static_assert( TCM_Max == 15, "Add missing mapping" );

		width = Max( width, 1u );

		// In the case of the block-compressed formats, we want the width in blocks, rounded up in case there is a partial block.
		const Uint32 widthInBCBlocks = (width + 3) / 4;

		switch( compression )
		{
		case TCM_DXTNoAlpha:				// BC1
		case TCM_Normals_DEPRECATED:		// BC1
		case TCM_QualityR:					// BC4
			return widthInBCBlocks * 8;

		case TCM_DXTAlphaLinear:			// BC3 Linear
		case TCM_DXTAlpha:					// BC3/7
		case TCM_Normalmap:					// BC5
		case TCM_NormalsHigh_DEPRECATED:	// BC3
		case TCM_NormalsGloss_DEPRECATED:	// BC3
		case TCM_QualityRG:					// BC5
		case TCM_QualityColor:				// BC7
		case TCM_HalfHDR_Unsigned:			// BC6
		case TCM_HalfHDR_Signed:			// BC6
			return widthInBCBlocks * 16;

		case TCM_None:
		case TCM_TileMap:
		case TCM_RGBE:
			return width * (GetPixelSizeInBytes( format ));

		default:
			RED_HALT( "Unknown compression: %d", compression );
			return width * (GetPixelSizeInBytes( format ));
		}
	}

	RED_INLINE GpuWrapApi::EImageCompressionHint GetImageCompressionHint( ETextureCompression compression )
	{
		static_assert(TCM_Max == 15, "Missing mapping");

		switch ( compression )
		{
		case TCM_Normalmap:
		case TCM_Normals_DEPRECATED:
		case TCM_NormalsHigh_DEPRECATED:
		case TCM_NormalsGloss_DEPRECATED:
			return GpuWrapApi::CIH_NormalmapRGB;

		default:
			return GpuWrapApi::CIH_None;
		}
	}

}

//----------------------------------------

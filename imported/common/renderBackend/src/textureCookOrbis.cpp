/**
 * Copyright (c) 2014-2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include <gnm.h>
// Disable warning that a function is deprecated
// This is also disabled in the Orbis build because they use a redgnmx.h file which does this 
// in the specific places that it occurs.
#pragma warning( disable : 4996 )
#include <gnmx.h>

#include "../../gpuApi/include/gpuApiPublic.h"
#include "../../gpuApi/include/gpuApiTypes.h"
#include "../../gpuApi/src/gnm/gpuApiGnmMapping.h"

#include "renderTextureCompilationSource.h"
#include "renderTextureMipMapLevel.h"

#include "../../renderData/include/renderTextureBlob.h"
#include "../../renderData/include/textureGroup.h"

#pragma comment( lib, "libSceGnm.lib" )
#pragma comment( lib, "libSceGnmx.lib" )
#pragma comment( lib, "libSceGpuAddress.lib" )
// #pragma optimize("",off)

namespace tools
{

Bool CookTexture_Orbis( const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& outHeader, red::UniqueBuffer& outBlobData )
{
	auto mipCount	= textureSource.GetMipCount();
	auto texCount	= textureSource.GetSliceCount();
	auto baseWidth	= textureSource.GetBaseWidth();
	auto baseHeight = textureSource.GetBaseHeight();
	auto baseDepth  = textureSource.GetBaseDepth();
	auto texType	= textureSource.GetTextureType();

	const auto& setup = textureSource.GetTextureSetup();

	auto texFormat	= Texture::GetCompressedFormat( setup.m_rawFormat, setup.m_compression, setup.m_isGamma );

	// Create texture, same as GpuApi Gnm, except we can assume that this isn't going to be a render target or anything like that.
	sce::Gnm::Texture tempTexture;
	sce::Gnm::DataFormat format = GpuApi::Map( texFormat );
	sce::GpuAddress::SurfaceType surfaceType = sce::GpuAddress::kSurfaceTypeTextureFlat;

	switch(texType)
	{
	case GpuWrapApi::eTextureType::TEXTYPE_CUBE:		surfaceType = sce::GpuAddress::kSurfaceTypeTextureCubemap;	break;
	case GpuWrapApi::eTextureType::TEXTYPE_3D:			surfaceType = sce::GpuAddress::kSurfaceTypeTextureVolume;	break;
	}

	// We always use base mode due to data compatibility on old and new PS4
	const sce::Gnm::GpuMode gpuMode = sce::Gnm::kGpuModeBase;

	sce::Gnm::TileMode tileMode;
	if ( sce::GpuAddress::computeSurfaceTileMode( gpuMode, &tileMode, surfaceType, format, 1 ) != sce::GpuAddress::kStatusSuccess )
	{
		RED_LOG_WARNING( "Cannot compute the tile mode for the texture." );
		tileMode = sce::Gnm::kTileModeDisplay_LinearAligned;
	}

	const sce::Gnm::NumFragments fragments = sce::Gnm::kNumFragments1;

	sce::Gnm::TextureSpec spec;
	spec.init();
	spec.m_width		= baseWidth;
	spec.m_height		= baseHeight;
	spec.m_depth		= baseDepth;
	spec.m_pitch		= 0;
	spec.m_numMipLevels	= mipCount;
	spec.m_numSlices	= texCount;
	spec.m_format		= format;
	spec.m_tileModeHint	= tileMode;
	spec.m_minGpuMode	= gpuMode;
	spec.m_numFragments	= fragments;


	switch ( texType )
	{
	case GpuWrapApi::TEXTYPE_2D:
		RED_ASSERT( texCount == 1, "2D Texture cannot have multiple slices" );
		spec.m_textureType = (fragments == sce::Gnm::kNumFragments1) ? sce::Gnm::kTextureType2d : sce::Gnm::kTextureType2dMsaa;
		break;

	case GpuWrapApi::TEXTYPE_ARRAY:
		spec.m_textureType = (fragments == sce::Gnm::kNumFragments1) ? sce::Gnm::kTextureType2dArray : sce::Gnm::kTextureType2dArrayMsaa;
		break;

	case GpuWrapApi::TEXTYPE_CUBE:
		spec.m_textureType = sce::Gnm::kTextureTypeCubemap;
		break;

	case GpuWrapApi::TEXTYPE_3D:
		spec.m_textureType = sce::Gnm::kTextureType3d;
		break;

	default:
		RED_HALT( "Invalid texture type: %d", texType );
		return false;
	}

	sce::Gnm::SizeAlign sizeAlign;
	Int32 result = tempTexture.init( &spec );
	if ( result != SCE_GNM_OK )
	{
		RED_HALT( "Failed to initialize gnm texture. result=0x%08x", result );
		sizeAlign = sce::Gnm::SizeAlign(0,0);
		return false;
	}

	sizeAlign = tempTexture.getSizeAlign();

	// Note:
	// This was specifying "sizeAlign.m_align" for the alignment, but according to the Documentation there is 
	// no alignment requirement on PC for a texture. In some cases the alignment was 256 bytes, which is incompatible
	// with the debug allocator.
	red::UniqueBuffer outputBuffer = red::CreateUniqueBuffer< rend::PoolTexture >( sizeAlign.m_size, sizeof( uintptr_t ) );
	if ( !outputBuffer )
	{
		RED_LOG_ERROR( "Failed to allocate temporary memory: %u, alignment: %u", sizeAlign.m_size, sizeAlign.m_align );
		return false;
	}

	for ( auto tex_i = 0; tex_i < texCount; ++tex_i )
	{
		// mipsArray may not contain full mipchain of the source texture, so recalculate starting mip based on the mips we have
		Uint32 startMip = textureSource.GetStartMip() - ( textureSource.GetMaxMipCount() - mipsArray[ tex_i ].Size() );

		for ( Uint32 mip_i = 0; mip_i < mipCount; ++mip_i )
		{
			Uint32 mipIndex = mip_i + startMip;

			const MipMap& mipMap = mipsArray[ tex_i ][ mipIndex ];

			Uint64 tiledOffset;
			Uint64 tiledSize;
			Int32 gnmRes = sce::GpuAddress::computeTextureSurfaceOffsetAndSize( &tiledOffset, &tiledSize, &tempTexture, mip_i, tex_i );
			if ( gnmRes != sce::GpuAddress::kStatusSuccess )
			{
				RED_LOG_ERROR( "Failed to get surface offset/size for mip %i (srcMip=%i) slice %i", mip_i, mipIndex, tex_i );
				return false;
			}

			sce::GpuAddress::TilingParameters tilingParams;
			gnmRes = tilingParams.initFromTexture( &tempTexture, mip_i, tex_i );
			if ( gnmRes != sce::GpuAddress::kStatusSuccess )
			{
				RED_LOG_ERROR( "Failed to initialize tiling params for mip %i (srcMip %i) slice %i", mip_i, mipIndex, tex_i );
				return false;
			}

			Uint64 untiledSize;
			sce::Gnm::AlignmentType untiledAlign;
			gnmRes = sce::GpuAddress::computeUntiledSurfaceSize( &untiledSize, &untiledAlign, &tilingParams );
			if ( gnmRes != sce::GpuAddress::kStatusSuccess )
			{
				RED_LOG_ERROR( "Failed to compute untiled surface size for mip %i (srcMip %i) slice %i", mip_i, mipIndex, tex_i );
				return false;
			}

			const red::UniqueBuffer& sourceMipData = mipMap.m_data;
			if ( !sourceMipData )
			{
				RED_LOG_ERROR( "No texture data for mip %i (srcMip %i) slice %i", mip_i, mipIndex, tex_i );
				return false;
			}

			void* tiledMipData = static_cast<Uint8*>( outputBuffer.Get() ) + tiledOffset;

			Uint32 mipDataSize = sourceMipData.GetSize();
			Uint32 mipDataPitch = mipMap.m_pitch;

			if ( untiledSize == mipDataSize )
			{
				gnmRes = sce::GpuAddress::tileSurface( tiledMipData, sourceMipData.Get(), &tilingParams );
			}
			else
			{
				// In some cases the size in the source can be different from what we're expecting. In particular, pitch is
				// clamped to 4 bytes, so a 2x2 8-bit texture for example will be larger. In these cases, we have to work around
				// it a bit so we don't end up with the wrong data in the tiled surface.
				sce::GpuAddress::SurfaceRegion destRegion;
				destRegion.m_left = 0;
				destRegion.m_right = tilingParams.m_linearWidth;
				destRegion.m_top = 0;
				destRegion.m_bottom = tilingParams.m_linearHeight;
				destRegion.m_front = 0;
				destRegion.m_back = 1;

				Uint32 bitsPerElement = format.getBitsPerElement();

				Uint32 srcPitchElems = mipDataPitch * 8 / bitsPerElement;
				Uint32 srcSizeElems = mipDataSize * 8 / bitsPerElement;
				gnmRes = sce::GpuAddress::tileSurfaceRegion( tiledMipData, sourceMipData.Get(), &tilingParams, &destRegion, srcPitchElems, srcSizeElems );
			}

			if ( gnmRes != sce::GpuAddress::kStatusSuccess )
			{
				RED_LOG_ERROR( "Failed to tile mip: %i (srcMip %i), slice %i", mip_i, mipIndex, tex_i );
				return false;
			}
		}
	}

	outHeader.m_mipMapInfo.Resize( mipCount );
	for ( Uint8 mip_i = 0; mip_i < mipCount; ++mip_i )
	{
		Uint64 mipOffset;
		Uint64 mipSize;
		Int32 gnmRes = sce::GpuAddress::computeTextureSurfaceOffsetAndSize( &mipOffset, &mipSize, &tempTexture, mip_i, 0 );
		RED_ASSERT(gnmRes == sce::GpuAddress::kStatusSuccess, "Failed to get surface offset and size, but this previous succeeded. Result: %x", gnmRes);

		Uint64 nextMipOffset;
		Uint64 nextMipSize;
		if ( mip_i < mipCount-1 )
		{
			Int32 gnmRes = sce::GpuAddress::computeTextureSurfaceOffsetAndSize( &nextMipOffset, &nextMipSize, &tempTexture, mip_i+1, 0 );
			RED_ASSERT( gnmRes == sce::GpuAddress::kStatusSuccess, "Failed to get surface offset and size, but this previous succeeded. Result: %x", gnmRes );
		}
		else
		{
			nextMipOffset = sizeAlign.m_size;
			nextMipSize = 0;
		}

		outHeader.m_mipMapInfo[mip_i].m_placement.m_offset = Uint32(mipOffset);
		outHeader.m_mipMapInfo[mip_i].m_placement.m_size = Uint32(nextMipOffset - mipOffset);
	}

	outHeader.m_textureInfo.m_dataAlignment = sizeAlign.m_align;
	outBlobData = std::move( outputBuffer );

	return true;
}

} // namespace tools

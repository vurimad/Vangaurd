/**
 * Copyright (c) 2014-2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include <d3d12_x.h>
#include <xg.h>
#include <wrl.h>

#include "../../gpuApi/include/gpuApiPublic.h"
#include "../../gpuApi/include/gpuApiInterface.h"
#include "../../gpuApi/src/dx12/gpuApiDX12Mapping.h"

#include "renderTextureCompilationSource.h"
#include "renderTextureMipMapLevel.h"
#include "../../renderData/include/renderTextureBlob.h"
#include "../../renderData/include/textureGroup.h"

#pragma comment( lib, "xg.lib" )
// #pragma optimize("",off)

using Microsoft::WRL::ComPtr;


namespace tools
{

Bool CookTexture_Durango( const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& outHeader, red::UniqueBuffer& outBlobData )
{
	auto mipCount	= textureSource.GetMipCount();
	auto texCount	= textureSource.GetSliceCount();
	auto baseWidth	= textureSource.GetBaseWidth();
	auto baseHeight = textureSource.GetBaseHeight();
	auto baseDepth = textureSource.GetBaseDepth();
	auto texType	= textureSource.GetTextureType();

	const auto& setup = textureSource.GetTextureSetup();

	auto texFormat	= Texture::GetCompressedFormat( setup.m_rawFormat, setup.m_compression, setup.m_isGamma );

	// Always use 1D tiling for resource textures. When we get optimal from XG, it can change depending on mips and size,
	// which causes problems if we need to stream in a subset of the mips.
	// #TODO: Do we want to use something different for 3D textures?
	XG_TILE_MODE tileMode = XG_TILE_MODE_1D_THIN;

	XG_RESOURCE_DESC texDesc;
	texDesc.Width = baseWidth;
	texDesc.Height = baseHeight;
	texDesc.DepthOrArraySize = 1;
	if ( texType == GpuWrapApi::TEXTYPE_3D )
	{
		if ( texCount > 1 )
		{
			RED_LOG_ERROR( "TexCompiler: [Durango] 3D texture has texCount %u > 1", texCount );
			return false;
		}
		texDesc.Dimension = XG_RESOURCE_DIMENSION_TEXTURE3D;
		texDesc.DepthOrArraySize = baseDepth;
	}
	else if( texType == GpuWrapApi::TEXTYPE_CUBE )
	{
		if( texCount != 6 )
		{
			RED_LOG_ERROR( "TexCompiler: [Durango] Cube texture has texCount %u != 6", texCount );
			return false;
		}
		texDesc.Dimension = XG_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.DepthOrArraySize = texCount;
	}
	else
	{
		if ( baseDepth > 1 )
		{
			RED_LOG_ERROR( "TexCompiler: [Durango] Non-3D texture has baseDepth %u > 1", baseDepth );
			return false;
		}
		texDesc.Dimension = XG_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.DepthOrArraySize = texCount;
	}

	texDesc.MipLevels = mipCount;
	texDesc.Format = static_cast<XG_FORMAT>(GpuApi::Map( texFormat ));
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = XG_TEXTURE_LAYOUT_TILE_MODE_1D_THIN;
	texDesc.MiscFlags = XG12_RESOURCE_MISC_NONE;
	texDesc.Alignment = 0;

	ComPtr<XGTextureAddressComputer> computer;
	HRESULT hr = XGCreateTextureComputer( &texDesc, &computer );

	if ( FAILED( hr ) )
	{
		RED_LOG_ERROR( "TexCompiler: [Durango] Failed to create texture computer" );
		return false;
	}

	XG_RESOURCE_LAYOUT layout;
	hr = computer->GetResourceLayout( &layout );
	if ( FAILED( hr ) )
	{
		RED_LOG_ERROR( "TexCompiler: [Durango] Failed to get resource layout" );
		return false;
	}

	auto layoutSizeInBytes = static_cast<Uint32>(layout.SizeBytes);
	auto layoutAlignment = static_cast<Uint32>(layout.BaseAlignmentBytes);
	red::UniqueBuffer outputBuffer = red::CreateUniqueBuffer< rend::PoolTexture >( layoutSizeInBytes, layoutAlignment );
	if ( !outputBuffer )
	{
		RED_LOG_ERROR( "Failed to allocate temporary memory: %u, alignment: %u", layoutSizeInBytes, layoutAlignment );
		return false;
	}

	for ( auto tex_i = 0; tex_i < texCount; ++tex_i )
	{
		// mipsArray may not contain full mipchain of the source texture, so recalculate starting mip based on the mips we have
		Uint32 startMip = textureSource.GetStartMip() - (textureSource.GetMaxMipCount() - mipsArray[tex_i].Size());

		for ( Uint32 mip_i = 0; mip_i < mipCount; ++mip_i )
		{
			Uint32 mipIndex = mip_i + startMip;
			RED_ASSERT( mipIndex < mipsArray[tex_i].Size(), "Invalid mip: %i, mipChainSize: %i", mipIndex, mipsArray[tex_i].Size() );
			const MipMap& mipMap = mipsArray[tex_i][mipIndex];

			const red::UniqueBuffer& sourceMipData = mipMap.m_data;
			if ( !sourceMipData )
			{
				RED_LOG_ERROR( "No texture data for mip %i (srcMip %i) slice %i", mip_i, mipIndex, tex_i );
				return false;
			}

			auto sourcePitch = mipMap.m_pitch;
			auto subResource = D3D11CalcSubresource( mip_i, tex_i, mipCount );
			hr = computer->CopyIntoSubresource( outputBuffer.Get(), 0, subResource, sourceMipData.Get(), sourcePitch, sourcePitch * mipMap.m_height );
			if ( FAILED( hr ) )
			{
				RED_LOG_ERROR( "Failed to copy mip: %i (srcMip %i) slice %i", mip_i, mipIndex, tex_i );
				return false;
			}
		}
	}

	outHeader.m_mipMapInfo.Resize( mipCount );
	for ( Uint8 mip_i = 0; mip_i < mipCount; ++mip_i )
	{
		Uint64 mipOffset = computer->GetMipLevelOffsetBytes( 0, mip_i );
		Uint64 nextMipOffset = ((mip_i < mipCount-1) ? computer->GetMipLevelOffsetBytes( 0, mip_i+1 ) : layout.SizeBytes);
		outHeader.m_mipMapInfo[mip_i].m_placement.m_offset = Uint32( mipOffset );
		outHeader.m_mipMapInfo[mip_i].m_placement.m_size = Uint32( nextMipOffset - mipOffset );
	}

	outHeader.m_textureInfo.m_dataAlignment = layoutAlignment;
	outBlobData = std::move( outputBuffer );

	return true;
}

} // namespace tools

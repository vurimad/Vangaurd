/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../renderData/include/renderTextureBlob.h"
#include "../../renderData/include/textureEnums.h"
#include "../../renderData/include/textureGroup.h"

#include "renderTextureCompiler.h"
#include "renderTextureCompilerUtils.h"
#include "../../redReflection/include/util.h"

#ifndef RED_PLATFORM_CONSOLE

// #pragma optimize("",off)

namespace hacks
{
	void ClonePCTextureToXboxAndPS4( rend::IRenderTextureBlob& pcTexture, THandle< rend::IRenderTextureBlob >& outXboxOneTexture, THandle< rend::IRenderTextureBlob >& outPS4Texture )
	{
		const auto& pcBufferData = pcTexture.m_textureData.Editor_LoadContentDirect();
		
		if( outXboxOneTexture = rend::IRenderTextureBlob::CreateRenderTextureBlob( PLATFORM_XboxOne ) )
		{
			outXboxOneTexture->m_header = pcTexture.m_header;
			outXboxOneTexture->m_textureData.Editor_SetContentDirect( pcBufferData );
		}

		if( outPS4Texture = rend::IRenderTextureBlob::CreateRenderTextureBlob( PLATFORM_PS4 ) )
		{
			outPS4Texture->m_header = pcTexture.m_header;
			outPS4Texture->m_textureData.Editor_SetContentDirect( pcBufferData );
		}
	}
}


namespace tools
{
	/*----------------------------------------------------------------------------------------------------------
		RenderTextureBlobCompiler
	----------------------------------------------------------------------------------------------------------*/
	static const Float st_naturalGammaExp = 2.2f;

	
	Bool RenderTextureBlobCompiler::Compile( const IGpuDataProcessor *gpuDataProcessor, const red::ArraySpan< const red::UniquePtr< ITextureCompilationSource > >& src, red::DynArray< THandle<rend::IRenderTextureBlob> >& outBlobs ) // in/out blob
	{
		Uint32 baseTextureIndex = 0;
		if ( src.Size() > 1 )
		{
			Uint32 texCount = src[ 0 ]->GetSliceCount();
			Uint32 maxMipsCount = src[ 0 ]->GetMipCount();
			for ( Uint32 i = 1; i < src.Size(); ++i )
			{
				if ( src[ i ]->GetSliceCount() != texCount )
				{
					return false;
				}
				if ( src[ i ]->GetMipCount() > maxMipsCount )
				{
					baseTextureIndex = i;
					maxMipsCount = src[ i ]->GetMipCount();
				}
			}
		}

		const ITextureCompilationSource& baseSourceTexture = *src[ baseTextureIndex ];

		Uint8 mipsCount = baseSourceTexture.GetMipCount();
		Uint16 texCount = baseSourceTexture.GetSliceCount();

		if ( mipsCount == 0 || mipsCount > rend::st_MaxMipMapsCount )
		{
			RED_LOG_WARNING( "TexCompiler: Wrong amount of mipmaps for texure %hs.", baseSourceTexture.GetTextureName().ToString().AsChar() );
			return false;
		}

		if ( texCount == 0 )
		{
			RED_LOG_WARNING( "TexCompiler: No slices in texture %hs.", baseSourceTexture.GetTextureName().ToString().AsChar() );
			return false;
		}

		red::DynArray< rend::HistogramBias > histogramData;

		// ------ Generate mip maps ------
		red::DynArray< MipArray > mipsArray; // [slice_i][mip_i]
		mipsArray.Grow( texCount );
		Bool hasInvalidData = false;
		for ( Uint16 tex_i = 0; tex_i < texCount; ++tex_i )
		{
			MipArray& mips = mipsArray[ tex_i ];
			HistogramBias bias;

			if ( GenerateMipMaps( gpuDataProcessor, baseSourceTexture, tex_i, mips, bias ) )
			{
				if ( mips.Size() != mipsCount )
				{
					RED_LOG_ERROR( "Mip maps generation problem. Should never happen. Got %u mips, should have %u", mips.Size(), mipsCount );

					mipsCount = mips.Size();
				}
				if ( baseSourceTexture.GetTextureSetup().m_stretchHistogram )
				{
					histogramData.PushBack( rend::HistogramBias( bias.m_mulCoef, bias.m_addCoef ) );
				}
			}
			else
			{
				RED_LOG_ERROR( "TexCompiler: No texture mip data for source %hs, slice %u.", baseSourceTexture.GetTextureName().ToString().AsChar(), tex_i );
				hasInvalidData = true;
			}
		}

		// Make sure sth got actually generated
		if ( mipsArray.Empty() )
		{
			RED_LOG_ERROR( "TexCompiler: No mips generated for source %hs.", baseSourceTexture.GetTextureName().ToString().AsChar() );
			return false;
		}

		// We have invalid data
		if ( hasInvalidData )
		{
			RED_LOG_ERROR( "TexCompiler: Texture array contains invalid data, no compilation possible %hs.", baseSourceTexture.GetTextureName().ToString().AsChar() );
			return false;
		}

		// ------ Set texture data ------
		Bool ret = true;

		for ( Uint32 i = 0; i < src.Size(); ++i )
		{
			const ITextureCompilationSource& srcTex = *src[i];

			rend::IRenderTextureBlob::Header header;
			red::UniqueBuffer blobBuffer;

			// histogram data
			header.m_histogramData = histogramData;

			auto platform = srcTex.GetPlatform();
			ret = CompileTexture( platform, srcTex, mipsArray, header, blobBuffer );

			if( ret )
			{
				if( outBlobs[i] = rend::IRenderTextureBlob::CreateRenderTextureBlob( platform ) )
				{
					outBlobs[i]->m_header = header;
					outBlobs[i]->m_textureData.Editor_SetContentDirect( std::move( blobBuffer ) );
				}
			}
			else
			{
				RED_LOG_WARNING( "TexCompiler: Texture (%hs) data blob compilation failed", srcTex.GetTextureName().ToString().AsChar() );
			}

		}

		return ret;
	}

	Bool RenderTextureBlobCompiler::GenerateMipMaps( const IGpuDataProcessor *gpuDataProcessor, const ITextureCompilationSource& textureSource, Uint16 slice, MipArray& outMipMaps, HistogramBias& outHistogramBias )
	{
		img::ImagePtr srcImage = textureSource.GetImage( slice );

		// No data in source texture
		if ( !srcImage )
		{
			RED_LOG_WARNING( "TexCompiler: Empty source data." );
			return false;
		}

		const auto& setup = textureSource.GetTextureSetup();

		Bool hasMipChain			= setup.m_hasMipchain;
		Bool isGamma				= setup.m_isGamma;
		Bool stretchHistogram		= setup.m_stretchHistogram;
		ETextureRawFormat format	= setup.m_rawFormat;
		Uint32 width				= srcImage->GetSize().m_width;
		Uint32 height				= srcImage->GetSize().m_height;
		Uint32 numChannels			= Texture::GetNumChannels( format );

		// Cube Texture: flip & rotate faces by convention
		if( textureSource.GetTextureType() == GpuWrapApi::TEXTYPE_CUBE )
		{
			Bool flipX, flipY, rotate;
			textureSource.GetFlipSettings( slice, flipX, flipY, rotate );

			srcImage = img::utils::FlipRect( srcImage, flipX, flipY, rotate );
			if( !srcImage )
			{
				RED_LOG_WARNING( "TexCompiler: Couldn't flip rect in face of the cube texture." );
				return false;
			}
		}

		// By convention float RGB values are linear, and integer RGB values are gamma-compressed. 
		// There is no particular benefit to gamma-compressing floats since the reason for gamma is to use more bits
		// where it is perceptually needed, and floats have sufficient (perhaps excessive) number of bits throughout 
		// and are already pseudo-log encoded (using the exponent). End user should use Linear texture group if he wanted to
		// use linear spaced encoding.
		Float gammaExp = 1.0f;
		if( isGamma && ( format == TRF_DeepColor || format == TRF_TrueColor ) )
		{
			gammaExp = st_naturalGammaExp;
		}

		img::ImagePtr colorData = img::Image::CreateEmpty( srcImage->GetPixelFormat(), srcImage->GetSize(), gammaExp );

		if( (format == TRF_Grayscale) && ( setup.m_compression == TCM_DXTNoAlpha ) )
		{
			img::Image::SizeInfo sizeInfo = srcImage->GetSize();
			sizeInfo.m_channels = 4;
			img::ImagePtr tmpSourceData = img::Image::CreateEmpty( img::PixelFormat::Format_Uint8, sizeInfo, srcImage->GetGammaExp() );
			if ( tmpSourceData == nullptr )
			{
				RED_LOG_WARNING( "TexCompiler: Can't allocate a continous buffer big enough for the texture." );
				return false;
			}

			img::utils::PropagateGrayscale( tmpSourceData, srcImage );

			colorData = tmpSourceData;
		}
		else
		{
			red::Memcpy( colorData->GetPixelBuffer(), srcImage->GetPixelBuffer(), srcImage->GetPixelBufferSize() );
		}

		// Convert color data to float32 so all processing operations below are done on floats 
		// we're bringing that back to source format just before compression
		colorData = img::utils::ConvertFormat( colorData, img::PixelFormat::Format_Float32 );
		if( !colorData )
		{
			RED_LOG_WARNING( "TexCompiler: Couldn't convert format to float32 during mip map generation." );
			return false;
		}

		// Color data processing (gamma transform + histogram stretching) supported formats
		if( Texture::IsGamma( format ) )
		{
			// Transform to gamma 2.2 space
			if( isGamma )
			{
				img::utils::TransformColorSpace( colorData, 2.2f );
			}

			if( stretchHistogram )
			{
				// Process histogram if needed
				img::utils::StretchHistogram( colorData, outHistogramBias.m_mulCoef, outHistogramBias.m_addCoef );
			}
		}

		// convert channels BGR to RGB -> PNG imports legacy
		//Int32 chMap[4] = { 2, 1, 0, 3 };
		//img::ImagePixel p(0);
		//colorData = img::utils::ConvertChannels( colorData, 4, chMap, p );

		const Uint8 startMip = hasMipChain ? textureSource.GetStartMip() : 0;
		const Uint8 mipCount = hasMipChain ? textureSource.GetMipCount() : 1;
		return CreateMips( gpuDataProcessor, colorData, textureSource.GetMipMapGenParams(), srcImage->GetPixelFormat(), outMipMaps, startMip, mipCount, setup );
	}

	Uint32 RenderTextureBlobCompiler::CalcMipChainSize( const MipArray& mipChain )
	{
		Uint32 dataSize = 0;
		for( Uint32 i = 0; i < mipChain.Size(); ++i )
		{
			dataSize += mipChain[i].m_data.GetSize();
		}
		return dataSize;
	}

	Bool RenderTextureBlobCompiler::CompileTextureFunc_PC( const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& outHeader, red::UniqueBuffer& outBlobData )
	{
		// On PC, we just copy the data over.
		const Uint16 texCount = textureSource.GetSliceCount(); 
		
		// Allocate buffer for the texture data blob
		red::UniqueBuffer data = red::CreateUniqueBuffer< rend::PoolTexture >( outHeader.m_textureInfo.m_textureDataSize, sizeof( uintptr_t ) );

		// Lay out in the same ordering as consoles (mip0tex0, mip0tex1, ... mip0texN, mip1tex0, ...)
		Uint32 blobOffset = 0;
		for ( Uint8 mip_i = 0; mip_i < textureSource.GetMipCount(); ++mip_i )
		{
			for ( Uint16 tex_i = 0; tex_i < texCount; ++tex_i )
			{
				// mipsArray may not contain full mipchain of the source texture, so recalculate starting mip based on the mips we have
				Uint32 startMip = textureSource.GetStartMip() - ( textureSource.GetMaxMipCount() - mipsArray[ tex_i ].Size() );

				const Uint32 mipIndex = mip_i + startMip;

				const MipMap& mipMap = mipsArray[ tex_i ][ mipIndex ];

				// Make sure the data is there
				const void* mipData = mipMap.m_data.Get();
				if ( mipData == nullptr )
				{
					RED_LOG_WARNING( "TexCompiler: No texture data for mip %u (srcMip %u) slice %u", mip_i, mipIndex, tex_i );
					return false;
				}

				const Uint32 mipSize = mipMap.m_data.GetSize();

				// Validate we've preallocated enough memory in the blob
				if( blobOffset + mipSize > data.GetSize() )
				{
					RED_LOG_WARNING( "TexCompiler: Not enough memory allocated in the render resource texture blob." );
					return false;
				}

				Uint8* mipXtexYPtr = static_cast<Uint8*>( data.Get() ) + blobOffset;
				
				red::Memcpy( mipXtexYPtr, mipData, mipSize );

				blobOffset += mipSize;
			}
		}

		// On PC, we can use practically any alignment, since we don't load in-place
		outHeader.m_textureInfo.m_dataAlignment = sizeof( uintptr_t );
		outBlobData = std::move( data );

		return true;
	}
	extern Bool CookTexture_Orbis( const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& outHeader, red::UniqueBuffer& outBlobData );

	Bool RenderTextureBlobCompiler::CompileTextureFunc_PS4( const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& outHeader, red::UniqueBuffer& outBlobData )
	{
#ifdef NO_CONSOLE_SDK
		return false;
#else
		return CookTexture_Orbis( textureSource, mipsArray, outHeader, outBlobData );
#endif
	}

	extern Bool CookTexture_Durango( const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& outHeader, red::UniqueBuffer& outBlobData );

	Bool RenderTextureBlobCompiler::CompileTextureFunc_XBoxOne( const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& outHeader, red::UniqueBuffer& outBlobData )
	{
#ifdef NO_CONSOLE_SDK
		return false;
#else
		return CookTexture_Durango( textureSource, mipsArray, outHeader, outBlobData );
#endif
	}

	Bool RenderTextureBlobCompiler::CompileTexture( ECookingPlatform platform, const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& header, red::UniqueBuffer& outBlobData )
	{
		// version
		header.m_version						= rend::IRenderTextureBlob::RenderTextureBlobVersions::VER_Current;
		header.m_flags							= 0;
		//size info
		header.m_sizeInfo.m_width				= textureSource.GetBaseWidth();
		header.m_sizeInfo.m_height				= textureSource.GetBaseHeight();
		header.m_sizeInfo.m_depth				= textureSource.GetBaseDepth();

		const auto& setup	= textureSource.GetTextureSetup();
		Uint8 mipCount		= textureSource.GetMipCount();
		Uint16 texCount		= textureSource.GetSliceCount();

		// texture info
		header.m_textureInfo.m_type				= textureSource.GetTextureType();

		// data size info
		header.m_textureInfo.m_sliceCount		= texCount;
		header.m_textureInfo.m_mipCount			= mipCount;

		//////////////////////////////////////////////////////////////////////////
		// NOTE: header/mip info below here will be set yet again for consoles...

		const MipArray& mipMapArray0 = mipsArray[0];

		// Fill blob texture info
		{
			const Uint32 textureDataSize = CalcMipChainSize( mipMapArray0 ) * texCount;
			header.m_textureInfo.m_textureDataSize = textureDataSize;
			header.m_textureInfo.m_sliceSize = textureDataSize / texCount;
		}

		// Fill blob mip map info
		Uint32 mipMapBlobOffset = 0;
		header.m_mipMapInfo.Reserve( mipCount );
		for( Uint32 mip = 0; mip < mipCount; ++mip )
		{
			Uint32 mip_i = mip + textureSource.GetStartMip() - textureSource.GetStartMip();
			rend::MipMapInfo mipMapInfo;
			mipMapInfo.m_placement.m_offset		= mipMapBlobOffset;
			mipMapInfo.m_placement.m_size		= mipMapArray0[mip_i].m_data.GetSize();
			mipMapInfo.m_layout.m_rowPitch		= mipMapArray0[mip_i].m_pitch;
			mipMapInfo.m_layout.m_slicePitch	= mipMapInfo.m_placement.m_size / texCount;
			header.m_mipMapInfo.PushBack( mipMapInfo );

			mipMapBlobOffset += mipMapInfo.m_placement.m_size;
		}

		Bool ret = false;

		switch( platform )
		{
		case PLATFORM_PC:
		case PLATFORM_WindowsServer:
								ret = CompileTextureFunc_PC( textureSource, mipsArray, header, outBlobData );		break;
		case PLATFORM_XboxOne:	ret = CompileTextureFunc_XBoxOne( textureSource, mipsArray, header, outBlobData );	break;
		case PLATFORM_PS4:		ret = CompileTextureFunc_PS4( textureSource, mipsArray, header, outBlobData );		break;
		default:
			RED_LOG_WARNING( "TexCompiler: Could not select appropriate texture blob compile function" );
			return false;

		}

		// Compiling the texture changes the mipMapInfos
		if( platform == PLATFORM_PS4 || platform == PLATFORM_XboxOne )
		{
			// Changes the mipmap infos
			Uint32 textureDataSize = 0;
			for( const auto& it : header.m_mipMapInfo )
			{
				textureDataSize += it.m_placement.m_size;
			}
			header.m_textureInfo.m_textureDataSize = textureDataSize;
			header.m_textureInfo.m_sliceSize = textureDataSize / textureSource.GetSliceCount();
		}

		if( ret )
		{
			header.m_flags |= rend::RTBHF_Optimized;
			if( platform == PLATFORM_PS4 || platform == PLATFORM_XboxOne )
			{
				header.m_flags |= rend::RTBHF_Tiled;
			}
		}

		return ret;
	}

}//tools

#endif //! RED_PLATFORM_CONSOLE
/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// Move to renderData ??
#include "renderTextureMipMapLevel.h"
#include "../../renderData/include/imageProcessing.h"
#include "../../renderData/include/gpuDataProcessor.h"
#include "../../renderData/include/textureEnums.h"

#define OUTPUT_DEBUG_MIPMAPS 0

#if OUTPUT_DEBUG_MIPMAPS
#	include "../../../common/redFileSystem/include/rawFileWriter.h"
#	include "../../redImage/include/imageUtils.inl"
#endif // OUTPUT_DEBUG_MIPMAPS
#include "../../resourceMaterial/include/bitmapTexture.h"

namespace tools
{
#if OUTPUT_DEBUG_MIPMAPS
	namespace FileWriter
	{
		struct Writer : public IFile
		{
			Writer()
				: IFile( FF_Writer | FF_MemoryBased )
				, m_pos( 0 )
			{}

			virtual Uint64 GetOffset() const override { return m_pos; }
			virtual Uint64 GetSize() const override { return m_data.Size(); }
			virtual void Seek( const Int64 offset ) override { RED_FATAL_ASSERT( offset <= m_data.Size(), "" ); m_pos = (Uint32)offset; }

			virtual void Serialize( /*const*/ void* data, const size_t size ) override
			{
				const Uint32 neededSize = Max< Uint32 >( m_data.Size(), m_pos + (Uint32)size );
				m_data.Resize( neededSize );
				red::Memcpy( m_data.TypedData() + m_pos, data, size );
				m_pos += (Uint32)size;
			}

			virtual void Flush() override {}

			Uint32 m_pos;
			red::DynArray< Uint8 > m_data;
		};

		static void SaveToFile( img::ImagePtr image, const red::AbsolutePath& path )
		{
			Writer writer;
			writer.m_data.Reserve( image->GetPixelBufferSize() );
			if ( img::BaseImageSaver::Save( writer, image, "png" ) )
			{
				RED_FATAL_ASSERT( writer.GetSize() == writer.m_pos );
				if ( writer.m_data.Size() > 0 )
				{
					if ( auto fileWriter = fs::RawFileWriter::Create( path, false ) )
					{
						fileWriter->Serialize( writer.m_data.TypedData(), writer.GetSize() );
					}
				}
			}
		}
	}
#endif // OUTPUT_DEBUG_MIPMAPS

	namespace DXTUtils
	{
		void CopyMemSlice( Uint8 * out, Uint32 dstPitch, const Rect & dstRct, const Uint8 * in, Uint32 srcPitch, const Rect & srcRct, Uint32 bytesPerPixel )
		{
			RED_ASSERT( (dstRct.m_right - dstRct.m_left) == (srcRct.m_right - srcRct.m_left) );
			RED_ASSERT( (dstRct.m_bottom - dstRct.m_top) == (srcRct.m_bottom - srcRct.m_top) );

			if( ((dstRct.m_right - dstRct.m_left) != (srcRct.m_right - srcRct.m_left)) || ((dstRct.m_bottom - dstRct.m_top) != (srcRct.m_bottom - srcRct.m_top)) )
			{
				return;
			}

			in	= &in[srcRct.m_top * srcPitch + srcRct.m_left * bytesPerPixel];
			out	= &out[dstRct.m_top * dstPitch + dstRct.m_left * bytesPerPixel];

			Uint32 size	= (dstRct.m_right - dstRct.m_left) * bytesPerPixel;

			for( Int32 y = srcRct.m_top; y < srcRct.m_bottom; ++y, in += srcPitch, out += dstPitch )
			{
				red::Memcpy( out, in, size );
			}
		}
	}

	static void SwizzlingStep( Uint8 * buf, Uint32 width, Uint32 height, ETextureRawFormat format, ETextureCompression compression )
	{
		// #TODO: Do any preprocessing, as appropriate for the texture format/compression.
	}

	Bool IsValidDX11Format( Uint32 width, Uint32 height, ETextureRawFormat format, ETextureCompression compression )
	{
		const GpuWrapApi::eTextureFormat gpuFormat = Texture::GetCompressedFormat( format, compression );
		const Bool isCompressed = GpuWrapApi::IsTextureFormatDXT( gpuFormat );
		if ( isCompressed )
		{
			return width >= 4 && height >= 4;
		}
		return true;
	}

	static void CreateMip( MipMap& destData, Uint32 width, Uint32 height, Uint32 depth, ETextureRawFormat destFormat, ETextureCompression destCompression )
	{
		RED_ASSERT( width > 0 && height > 0 && depth > 0, "Mip must not have dimension 0 (%ux%ux%u)", width, height, depth );
		RED_ASSERT( IsValidDX11Format( width, height, destFormat, destCompression ), "Format is not valid, cannot create mip" );

		// Create mipmap
		destData.m_width	= width;
		destData.m_height	= height;
		destData.m_depth	= depth;
		destData.m_pitch	= Texture::GetPitch( width, height, destFormat, destCompression );

		Uint32 numRows		= Texture::GetNumRows( width, height, destFormat, destCompression );

		// Calculate required data size
		Uint32 dataSize = depth * numRows * destData.m_pitch;

		destData.m_data = red::CreateUniqueBuffer< rend::PoolTexture >( dataSize, 16 );
	}

	static Bool CreateMips( const IGpuDataProcessor *gpuDataProcessor, img::ImagePtr colorData, const MipMapGenParams& mipMapGenParams, img::PixelFormat originalPixelFormat, MipArray& outMipMaps, Uint8 startMip, Uint8 mipCount, const STextureGroupSetup& setup )
	{
		const ETextureRawFormat format = setup.m_rawFormat;
		const ETextureCompression textureCompression = setup.m_compression;
		const Uint8 alphaToCoverageThreshold = setup.m_alphaToCoverageThreshold;

		Uint16 width = colorData->GetSize().m_width;
		Uint16 height = colorData->GetSize().m_height;
		Uint16 depth = colorData->GetSize().m_depth;
		Uint32 currentMip = 0;

		RED_ASSERT( width > 0 && height > 0 && depth > 0 );

		constexpr Uint8 channelCount = 4;

		red::StaticArray< Float, channelCount > baseMipAlphaCoverage( channelCount );
		for ( Uint32 i = 0; i < channelCount; ++i )
		{
			const Bool shouldCalculateBaseCoverage = ( alphaToCoverageThreshold > 0 ) && ( ( setup.m_alphaToCoverageThreshold_Channel & RED_FLAG( i ) ) != 0 );
			baseMipAlphaCoverage[ i ] = shouldCalculateBaseCoverage ? img::utils::GetChannelCoverage( colorData, i, alphaToCoverageThreshold / 255.0f ) : 1.0f;
		}

		img::ImagePtr toksvigSourceNormalMap = nullptr;
		Bool shouldAdjustMipBasedOnNormalMap = mipMapGenParams.m_applyToksvig_sourceNormalMap.IsValid();
		if ( shouldAdjustMipBasedOnNormalMap )
		{
			//load and validate normal map for toksvig
			auto loadToken = mipMapGenParams.m_applyToksvig_sourceNormalMap.IssueLoadingRequest();
			loadToken->WaitUntilLoaded();
			if ( loadToken->GetResource() )
			{
				toksvigSourceNormalMap = Cast<CBitmapTexture>( loadToken->GetResource() )->GetImage();
				if ( toksvigSourceNormalMap->GetSize().m_width != colorData->GetSize().m_width || toksvigSourceNormalMap->GetSize().m_height != colorData->GetSize().m_height )
				{
					RED_DATA_ERROR( red::LoggerCategory_Rendering, false, "Chose to Toksvig-adjust mipmap based on normal distribution in subsequent\
							mips but normal map and roughness texture sizes don't match." );
					return false;
				}
			}
			else
			{
				RED_DATA_ERROR( red::LoggerCategory_Rendering, false, "Chose to Toksvig-adjust mipmap based on normal distribution in subsequent\
							mips but cannot load source normal map" );
				return false;
			}


		}

		while ( true )
		{
			if ( currentMip >= startMip )
			{
				MipMap mip;
				GpuWrapApi::eTextureFormat sourceFormat = Texture::GetCompressedFormat( format, TCM_None );
				GpuWrapApi::eTextureFormat targetFormat = Texture::GetCompressedFormat( format, textureCompression );

				// #TODO: Probably should do the swizzle per-mip, so we don't get some sort of error propagation or
				// something.
				SwizzlingStep( (Uint8*)colorData->GetPixelBuffer(), width, height, format, textureCompression );

				if ( IsValidDX11Format( width, height, format, textureCompression ) )
				{
					CreateMip( mip, width, height, depth, format, textureCompression );

					// Since we were operating on Float32 in colorData, we need to get back to the source format here
					img::ImagePtr srcColorData = img::utils::ConvertFormat( colorData, originalPixelFormat );

					if ( !ConvertBuffer( width, height, depth, width * Texture::GetPixelSizeInBytes( format ),
										sourceFormat,
										srcColorData->GetPixelBuffer(),
										srcColorData->GetPixelBufferSize(),
										targetFormat,
										mip.m_data,
										Texture::GetImageCompressionHint( textureCompression )
					) )
					{
						if ( GpuWrapApi::IsTextureFormatDXT(targetFormat) )
						{
							RED_LOG_ERROR("TexCompiler: CreateMip: couldn't create mip. Unsupported format.");
							return false;
						}

						// No compression, direct copy
						const Uint32 sourceSize = depth * width * height * Texture::GetPixelSizeInBytes( format );
						RED_FATAL_ASSERT( mip.m_data.GetSize() <= sourceSize, "Cannot write to mip buffer if size is smaller than source." );
						red::Memcpy( mip.m_data.Get(), srcColorData->GetPixelBuffer(), sourceSize );
					}

					outMipMaps.PushBack( std::move( mip ) );
				}
				// Dxt constraint: width < 4 && height < 4
				else
				{
					CreateMip( mip, Max<Uint32>( width, 4 ), Max<Uint32>( height, 4 ), depth, format, textureCompression );

					Uint32 compressedWidth = Max<Uint32>( width, 4 );
					Uint32 compressedHeight = Max<Uint32>( height, 4 );
					Uint32 compressedDepth = depth;

					// Since we were operating on Float32 in colorData, we need to get back to the source format here
					img::ImagePtr srcColorData = img::utils::ConvertFormat( colorData, originalPixelFormat );


					auto paddedSize = srcColorData->GetSize();
					paddedSize.m_width = compressedWidth;
					paddedSize.m_height = compressedHeight;
					img::ImagePtr tempBufCompressed = img::Image::CreateEmpty( srcColorData->GetPixelFormat(), paddedSize, srcColorData->GetGammaExp() );
					RED_ASSERT( tempBufCompressed->GetPixelBufferSize() >= srcColorData->GetPixelBufferSize(), "CreateMip: dest buffer must be larger than source." );

					for ( Uint32 y = 0; y < srcColorData->GetSize().m_height; ++y )
					{
						const Uint8* src = (const Uint8*)srcColorData->GetPixelBuffer();
						Uint8* dst = (Uint8*)tempBufCompressed->GetPixelBuffer();
						red::Memcpy( dst + tempBufCompressed->GetLayout().m_rowPitch*y, src + srcColorData->GetLayout().m_rowPitch * y, srcColorData->GetLayout().m_rowPitch );
					}


					ConvertBuffer( compressedWidth, compressedHeight, compressedDepth, compressedWidth * Texture::GetPixelSizeInBytes( format ),
								   sourceFormat,
								   tempBufCompressed->GetPixelBuffer(),
								   tempBufCompressed->GetPixelBufferSize(),
								   targetFormat,
								   mip.m_data,
								   Texture::GetImageCompressionHint( textureCompression )
					);

					outMipMaps.PushBack( std::move( mip ) );
				}

				if( ( outMipMaps.Size() == mipCount ) || ( width == 1 && height == 1 && depth == 1 ) )
				{
					break;
				}
			}

			// downsample to have the data to start with in the next round
			Bool usedCustom = false;

			if ( gpuDataProcessor )
			{
				if( auto downscaleFunc = gpuDataProcessor->GetMipmapGenFunc() )
				{
					downscaleFunc( colorData );
					usedCustom = true;
				}
			}
			
			// If not used custom one, fallback to BOX-filter
			if( !usedCustom )
			{
				colorData = img::utils::DownscaleBox( colorData );
				if ( shouldAdjustMipBasedOnNormalMap )
				{
					toksvigSourceNormalMap = img::utils::DownscaleBox( toksvigSourceNormalMap );
				}
			}

			//Adjust mipmap based on footprint of normals distributed in given mipmap's texel
			if ( shouldAdjustMipBasedOnNormalMap )
			{
				img::ImageAccess srcMip( *colorData );
				img::ImageIterator srcIt = colorData->GetIterator();
				img::ImageAccess normalMip( *toksvigSourceNormalMap );
				img::ImageIterator normalMipIt = toksvigSourceNormalMap->GetIterator();
				auto func = [&mipMapGenParams, &toksvigSourceNormalMap, &normalMip, &normalMipIt]( img::ImageIterator& srcIt, img::ImagePixel& srcPixel )->Bool
				{
					Float roughness = mipMapGenParams.m_applyToksvig_ShouldInvChannel ? 1.0f - srcPixel[mipMapGenParams.m_applyToksvig_Channel] : srcPixel[mipMapGenParams.m_applyToksvig_Channel];

					img::ImagePixel normalPix;
					img::utils::UnpackPixel_SLOW( *toksvigSourceNormalMap, normalMip, normalMipIt, normalPix );
					normalMipIt.Next();
					//it's ok to do the DownscaleBox filter on Normal map in <0, 1> range and transform the final normal into <-1, 1> just when we need it here
					//so the normal that we grab here is already correct averaged normal from all of the normals up to the base mip
					const Vector3 avgNormal = Vector3( normalPix[0], normalPix[1], normalPix[2] ) * 2.0f - Vector3::ONES();

					Float avgNormalLength = avgNormal.Mag();

					// Based on The Order : 1886 SIGGRAPH course notes implementation
					if ( avgNormalLength < 1.0f )
					{
						const Float avgNormLen2 = avgNormalLength * avgNormalLength;
						const Float kappa = ( 3 * avgNormalLength - avgNormalLength * avgNormLen2 ) / ( 1 - avgNormLen2 );
						const Float variance = 1.0f / ( 2.0f * kappa );

						roughness = std::sqrt( roughness * roughness + variance );
					}

					// Converts a Beckmann roughness parameter to a Blinn-Phong specular power
					const Float s = 2.0f / ( roughness * roughness ) - 2.0f;

					const Float ft = avgNormalLength / math::Lerp( avgNormalLength, s, 1.0f );

					// Converts a Blinn-Phong specular power to a Beckmann roughness parameter
					const float blinnPhongSpec = ft * s;
					const Float toksvigRoughness = sqrtf( 2.0f / ( blinnPhongSpec + 2.0f ) );

					// Set the final toksvig adjusted roughness value
					srcPixel[mipMapGenParams.m_applyToksvig_Channel] = Min( toksvigRoughness, 1.0f );
					if ( mipMapGenParams.m_applyToksvig_ShouldInvChannel )
					{
						srcPixel[mipMapGenParams.m_applyToksvig_Channel] = 1.0f - srcPixel[mipMapGenParams.m_applyToksvig_Channel];
					}

					return true;
				};
				img::utils::ProcessImageInplace( srcMip, srcIt, func );
			}

			constexpr Float eps = 1e-5f;
			constexpr Uint32 atcMaxIter = 4;
			for ( Uint8 i = 0; i < channelCount; ++i )
			{
				if ( ( setup.m_alphaToCoverageThreshold_Channel & RED_FLAG( i ) ) == 0 )
				{
					continue;
				}
				
				// boost channel of the mipmap
				Uint32 atcIter = 0;
				while ( alphaToCoverageThreshold && atcIter < atcMaxIter )
				{
					const Float curMipAlphaCoverage = img::utils::GetChannelCoverage( colorData, i, alphaToCoverageThreshold / 255.0f );
					if ( curMipAlphaCoverage < eps )
					{
						RED_LOG_WARNING( "Didn't boost alpha in %d mipmap because all alpha values were below threshold", currentMip );
						break;
					}

					const Float boostFactor = baseMipAlphaCoverage[ i ] / curMipAlphaCoverage;
					if ( boostFactor <= 1.0f + eps )
					{
						break;
					}

					img::utils::MultiplyChannelValue( colorData, i, boostFactor );
					++atcIter;
				}
			}

			// This is simple mipmap dumping for debugging purposes
#if OUTPUT_DEBUG_MIPMAPS
			{
				static Uint32 tempInd = 0;
				red::AbsolutePath path = red::AbsolutePath::CreateFilePath( "Z:\\mips" );

				String mipC; ToString( mipC, currentMip );
				String t; ToString(t, tempInd);
				path.AppendFilePath( "\\" + t + "_" + mipC );
				path.AppendFilePath( ".png" );

				FileWriter::SaveToFile( img::utils::ConvertFormat( colorData, img::PixelFormat::Format_Uint8 ), path );
				tempInd++;
			}
#endif // OUTPUT_DEBUG_MIPMAPS

			constexpr Uint32 roundUp = 0;

			width	= Max< Uint16 >( ( width  + roundUp ) / 2, 1 );
			height	= Max< Uint16 >( ( height + roundUp ) / 2, 1 );
			depth	= Max< Uint16 >( ( depth  + roundUp ) / 2, 1 );

			currentMip++;
		}

		return true;
	}
}

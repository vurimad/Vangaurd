/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"

#include "image.h"
#include "imageIterator.h"
#include "imageAccess.h"
#include "imageUtils.h"

namespace img
{
	namespace utils
	{
		class BicubicResample
		{
		public:
			template< typename Format >
			RED_FORCE_INLINE static void Process( const ImageAccess& src, ImageAccess& dest )
			{
				const auto srcWidth = src.GetSize().m_width;
				const auto srcHeight = src.GetSize().m_height;
				const auto destWidth = dest.GetSize().m_width;;
				const auto destHeight = dest.GetSize().m_height;;

				auto sizeInfo = src.GetSize();			
				sizeInfo.m_height = destHeight;
				ImagePtr tmpImage( Image::CreateEmpty( img::PixelFormat::Format_Float32, sizeInfo, src.GetGammaExp() ) );
				RED_ASSERT( tmpImage, "Failed to create image for downscaled version" );
				if ( !tmpImage )
					return;

				ImageAccess tmp( *tmpImage );

				Float ratioX = (Float)srcWidth / (Float)destWidth;
				Float ratioY = (Float)srcHeight / (Float)destHeight;

				// BicubicResample Y pass
				for ( Uint32 y = 0; y < destHeight; ++y )
				{
					Float sourceY = (Float)y * ratioY;

					Int32 sampleY[4];
					sampleY[1] = (Int32)MFloor( sourceY );
					sampleY[0] = (sampleY[1] > 0) ? (sampleY[1] - 1) : 0;
					sampleY[2] = (sampleY[1] < (srcHeight - 1)) ? (sampleY[1] + 1) : srcHeight - 1;
					sampleY[3] = (sampleY[1] < (srcHeight - 2)) ? (sampleY[1] + 2) : srcHeight - 1;

					Float interpolation = sourceY - MFloor( sourceY );

					for ( Uint32 x = 0; x < srcWidth; ++x )
					{
						ImagePixel srcPixel[4];
						for( Uint32 i = 0; i < 4; ++i )
						{
							src.Unpack< Format >( x, sampleY[i], (Uint32)0, srcPixel[i] );
							
							if( src.IsInGammaSpace() )
								srcPixel[i].GammaToLinearInplace( src.GetGammaExp() );
						}

						ImagePixel bicubic = CubicInterpolation( srcPixel[0], srcPixel[1], srcPixel[2], srcPixel[3], interpolation, Format::CHANNELS );

						// store merged pixel
						ImageIterator it = ImageIterator( x, y, (Uint32)0 );
						tmp.Pack< ImageFormat32_DIRECT<Format::CHANNELS> >( it, bicubic );
					}
				}

				// BicubicResample X pass
				for ( Uint32 y = 0; y < destHeight; ++y )
				{
					for ( Uint32 x = 0; x < destWidth; ++x )
					{
						Float sourceX = (Float)x * ratioX;

						Int32 sampleX[4];
						sampleX[1] = (Int32)MFloor( sourceX );
						sampleX[0] = (sampleX[1] > 0) ? (sampleX[1] - 1) : 0;
						sampleX[2] = (sampleX[1] < (srcWidth - 1)) ? (sampleX[1] + 1) : srcWidth - 1;
						sampleX[3] = (sampleX[1] < (srcWidth - 2)) ? (sampleX[1] + 2) : srcWidth - 1;

						Float interpolation = sourceX - MFloor( sourceX );

						ImagePixel tmpPixel[4];
						for( Uint32 i = 0; i < 4; ++i )
						{
							tmp.Unpack< ImageFormat32_DIRECT<Format::CHANNELS> >( x, sampleX[i], (Uint32)0, tmpPixel[i] );
						}

						ImagePixel bicubic = CubicInterpolation( tmpPixel[0], tmpPixel[1], tmpPixel[2], tmpPixel[3], interpolation, src.GetSize().m_channels );

						if( src.IsInGammaSpace() )
							bicubic.LinearToGammaInplace( src.GetInvGammaExp() );

						// store merged pixel
						ImageIterator it = ImageIterator( x, y, (Uint32)0 );
						dest.Pack< Format >( it, bicubic );
					}
				}
			}

		private:
			RED_FORCE_INLINE static ImagePixel CubicInterpolation( const ImagePixel& p0, const ImagePixel& p1, const ImagePixel& p2, const ImagePixel& p3, const Float x, const Uint8 numChannels )
			{
				ImagePixel ret;
				for ( Uint32 i=0; i<numChannels; ++i )
					ret[i] = p1[i] + 0.5f * x*(p2[i] - p0[i] + x*(2.0f*p0[i] - 5.0f*p1[i] + 4.0f*p2[i] - p3[i] + x*(3.0f*(p1[i] - p2[i]) + p3[i] - p0[i])));
				return ret;
			}
		};

		REDIMAGE_API ImagePtr ResampleBicubic( const ImagePtr& source, Uint32 width, Uint32 height )
		{
			RED_FATAL_ASSERT( source != nullptr, "Invalid source image for resampling" );
			RED_FATAL_ASSERT( source->GetSize().m_depth == 1, "Invalid source image for resampling. Only 2D textures are supported with this function." );

			/// Get source image size
			const auto srcWidth = source->GetSize().m_width;
			const auto srcHeight = source->GetSize().m_height;
			
			// if ( ( srcWidth == 1 && srcHeight == 1 ) || source->GetSize().m_depth > 1 )
			// 	return ImagePtr(); // by definition the final 1x1x1 image cannot be downscaled and this returns NULL.

			/// Determine downscale width
			const auto destWidth = width;
			const auto destHeight = height;
			
			// No need to resample (hue-hue:)
			if( srcWidth == destWidth && srcHeight == destHeight )
				return source;

			// Create target image
			auto sizeInfoXPass = source->GetSize();
			sizeInfoXPass.m_width = destWidth;
			sizeInfoXPass.m_height = destHeight;
			ImagePtr destImage( Image::CreateEmpty( source->GetPixelFormat(), sizeInfoXPass, source->GetGammaExp() ) );
			RED_ASSERT( destImage, "Failed to create image for downscaled version" );
			if ( !destImage )
				return ImagePtr();

			// create access points to image
			ImageAccess srcAccess( *source );
			ImageAccess destAccess( *destImage );

			img::utils::FilterImage< BicubicResample >( srcAccess, destAccess );
			
			// return resampled image
			return destImage;
		}

	} // utils

} // img

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
		// SLOW TEST IMPLEMENTATION
		class BoxDownscaleClamp
		{
		public:
			template< typename Format >
			RED_FORCE_INLINE static void Process( const ImageAccess& src, ImageAccess& dest, const ImageIterator& destIt )
			{
				ImagePixel sumPixel( 0.0f );
				Float sumWeight = 0.0f;

				// iterate over source region summing up pixels
				ImageIterator srcIt = GetSourceRegion( src, destIt );
				do 
				{
					ImagePixel srcPixel;
					src.Unpack< Format >( srcIt, srcPixel );
					
					if( src.IsInGammaSpace() )
						srcPixel.GammaToLinearInplace( src.GetGammaExp() );
					
					sumPixel.AccumulateInplace( srcPixel );
					sumWeight += 1.0f;
				} while ( srcIt.Next() );

				// normalize
				sumPixel.MulInplace( 1.0f / sumWeight );

				if( src.IsInGammaSpace() )
					sumPixel.LinearToGammaInplace( src.GetInvGammaExp() );

				// store merged pixel
				dest.Pack< Format >( destIt, sumPixel );
			}

		private:
			static ImageIterator GetSourceRegion( const ImageAccess& src, const ImageIterator& destIt )
			{
				// get image iterate for source image
				const auto maxSrcX = src.GetSize().m_width;
				const auto maxSrcY = src.GetSize().m_height;
				const auto maxSrcZ = src.GetSize().m_depth;

				// calculate iteration range
				const auto minX = math::Min< Uint32 >( maxSrcX, destIt.GetX() * 2 );
				const auto minY = math::Min< Uint32 >( maxSrcY, destIt.GetY() * 2 );
				const auto minZ = math::Min< Uint32 >( maxSrcZ, destIt.GetZ() * 2 );
				const auto maxX = math::Min< Uint32 >( maxSrcX, destIt.GetX() * 2 + 2);
				const auto maxY = math::Min< Uint32 >( maxSrcY, destIt.GetY() * 2 + 2 );
				const auto maxZ = math::Min< Uint32 >( maxSrcZ, destIt.GetZ() * 2 + 2 );

				// create iterator
				return ImageIterator( minX, minY, minZ, maxX, maxY, maxZ );
			}
		};

		class BoxDownscaleWrap
		{
		public:
			template< typename Format >
			RED_FORCE_INLINE static void Process( const ImageAccess& src, ImageAccess& dest, const ImageIterator& destIt )
			{
				ImagePixel sumPixel( 0.0f );
				Float sumWeight = 0.0f;

				// get image iterate for source image
				const auto maxSrcX = src.GetSize().m_width;
				const auto maxSrcY = src.GetSize().m_height;
				const auto maxSrcZ = src.GetSize().m_depth;

				// iterate over source region summing up pixels
				ImageIterator srcIt = GetSourceRegion( src, destIt );
				do 
				{
					ImageIterator it( srcIt.GetX() % maxSrcX, srcIt.GetY() % maxSrcY, srcIt.GetZ() % maxSrcZ );

					ImagePixel srcPixel;
					src.Unpack< Format >( it, srcPixel );

					if( src.IsInGammaSpace() )
						srcPixel.GammaToLinearInplace( src.GetGammaExp() );

					sumPixel.AccumulateInplace( srcPixel );
					sumWeight += 1.0f;
				} while ( srcIt.Next() );
				
				
				// normalize
				sumPixel.MulInplace( 1.0f / sumWeight );

				if( src.IsInGammaSpace() )
					sumPixel.LinearToGammaInplace( src.GetInvGammaExp() );

				// store merged pixel
				dest.Pack< Format >( destIt, sumPixel );
			}

		private:
			static ImageIterator GetSourceRegion( const ImageAccess& src, const ImageIterator& destIt )
			{
				// calculate iteration range
				const auto minX = destIt.GetX() * 2;
				const auto minY = destIt.GetY() * 2;
				const auto minZ = destIt.GetZ() * 2;
				const auto maxX = destIt.GetX() * 2 + 2;
				const auto maxY = destIt.GetY() * 2 + 2;
				const auto maxZ = destIt.GetZ() * 2 + 2;

				// create iterator
				return ImageIterator( minX, minY, minZ, maxX, maxY, maxZ );
			}
		};

		REDIMAGE_API ImagePtr DownscaleBox( const ImagePtr& source, FilteringMode filerMode /*=FM_Clamp*/ )
		{
			RED_FATAL_ASSERT( source != nullptr, "Invalid source image for downscaling" );

			/// Get source image size
			const auto srcWidth = source->GetSize().m_width;
			const auto srcHeight = source->GetSize().m_height;
			const auto srcDepth = source->GetSize().m_depth;
			if ( srcWidth == 1 && srcHeight == 1 && srcDepth == 1 )
				return ImagePtr(); // by definition the final 1x1x1 image cannot be downscaled and this returns NULL. This makes the mip generation function way easier.

			/// Determine downscale width
			const auto destWidth = math::Max< Uint32 >( 1, srcWidth / 2 );
			const auto destHeight = math::Max< Uint32 >( 1, srcHeight / 2 );
			const auto destDepth = math::Max< Uint32 >( 1, srcDepth / 2 );

			/// Create target image
			auto sizeInfo = source->GetSize();
			sizeInfo.m_width = destWidth;
			sizeInfo.m_height = destHeight;
			sizeInfo.m_depth = destDepth;
			ImagePtr destImage( Image::CreateEmpty( source->GetPixelFormat(), sizeInfo, source->GetGammaExp() ) );
			RED_ASSERT( destImage, "Failed to create image for downscaled version" );
			if ( !destImage )
				return ImagePtr();

			// create access points to image
			ImageAccess srcAccess( *source );
			ImageAccess destAccess( *destImage );

			// iterate over the whole image processing pixels
			// NOTE: images are compatible so we can use the same iterator
			ImageIterator it = destImage->GetIterator();
			if( filerMode == FM_Clamp )
			{
				img::utils::FilterImage< BoxDownscaleClamp >( srcAccess, destAccess, it );
			}
			else
			{
				img::utils::FilterImage< BoxDownscaleWrap >( srcAccess, destAccess, it );
			}

			// return downscale image
			return destImage;
		}


		REDIMAGE_API ImagePtr Clip( const ImagePtr& source, Uint32 offsetX, Uint32 offsetY, Uint32 width, Uint32 height )
		{
			RED_FATAL_ASSERT( source != nullptr, "Invalid source image for downscaling" );

			const auto srcWidth		= source->GetSize().m_width;
			const auto srcHeight	= source->GetSize().m_height;
			const auto srcDepth		= source->GetSize().m_depth;
			if( srcWidth < offsetX + width || srcHeight < offsetY + height )
				return ImagePtr(); // by definition the final 1x1x1 image cannot be downscaled and this returns NULL. This makes the mip generation function way easier.

			auto sizeInfo = source->GetSize();
			sizeInfo.m_width	= width;
			sizeInfo.m_height	= height;
			ImagePtr destImage( Image::CreateEmpty( source->GetPixelFormat(), sizeInfo, source->GetGammaExp() ) );

			Uint8 * srcData = (Uint8*)source->GetPixelBuffer();
			Uint8 * dstData = (Uint8*)destImage->GetPixelBuffer();

			auto srcLay = source->GetLayout();
			auto dstLay = destImage->GetLayout();

			// Copy every depth
			for( Uint32 d = 0; d < srcDepth; ++d )
			{
				// Every destination row
				for( Uint32 y = 0; y < height; ++y )
				{
					// Copy line
					red::Memcpy( dstData + dstLay.CalcPixelOffset( 0, y, d ),
								 srcData + srcLay.CalcPixelOffset( offsetX, offsetY + y, d ),
								 dstLay.m_rowPitch ); // Whole row
				}
			}

			return destImage;
		}

	} // utils

} // img

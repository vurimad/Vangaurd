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
		class GuassDownscaleClamp
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
					const Int32 x = math::Abs( srcIt.GetX() - destIt.GetX() * 2 );
					const Int32 y = math::Abs( srcIt.GetY() - destIt.GetY() * 2 );
					const Int32 z = math::Abs( srcIt.GetZ() - destIt.GetZ() * 2 );

					const Float weight = 1.0f - (Float)std::sqrt( x*x + y*y + z*z ) / 3.0f;

					ImagePixel srcPixel;
					src.Unpack< Format >( srcIt, srcPixel );

					if( src.IsInGammaSpace() )
						srcPixel.GammaToLinearInplace( src.GetGammaExp() );

					sumPixel.AccumulateInplace( srcPixel, weight );
					sumWeight += weight;
				} while( srcIt.Next() );

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
				const auto minX = math::Clamp< Int32 >( destIt.GetX() * 2 - 2, 0, maxSrcX );
				const auto minY = math::Clamp< Int32 >( destIt.GetY() * 2 - 2, 0, maxSrcY );
				const auto minZ = math::Clamp< Int32 >( destIt.GetZ() * 2 - 2, 0, maxSrcZ );
				const auto maxX = math::Clamp< Int32 >( destIt.GetX() * 2 + 2 + 3, 0, maxSrcX );
				const auto maxY = math::Clamp< Int32 >( destIt.GetY() * 2 + 2 + 3, 0, maxSrcY );
				const auto maxZ = math::Clamp< Int32 >( destIt.GetZ() * 2 + 2 + 3, 0, maxSrcZ );

				// create iterator
				return ImageIterator( minX, minY, minZ, maxX, maxY, maxZ );
			}
		};

		class GuassDownscaleWrap
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
					const Int32 x = math::Abs( srcIt.GetX() - destIt.GetX() * 2 );
					const Int32 y = math::Abs( srcIt.GetY() - destIt.GetY() * 2 );
					const Int32 z = math::Abs( srcIt.GetZ() - destIt.GetZ() * 2 );

					const Float weight = 1.0f - (Float)std::sqrt( x*x + y*y + z*z ) / 3.0f;

					ImageIterator it( srcIt.GetX() % maxSrcX, srcIt.GetY() % maxSrcY, srcIt.GetZ() % maxSrcZ );

					ImagePixel srcPixel;
					src.Unpack< Format >( it, srcPixel );

					if( src.IsInGammaSpace() )
						srcPixel.GammaToLinearInplace( src.GetGammaExp() );

					sumPixel.AccumulateInplace( srcPixel, weight );
					sumWeight += weight;
				} while( srcIt.Next() );


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
				const auto minX = destIt.GetX() * 2 - 2;
				const auto minY = destIt.GetY() * 2 - 2;
				const auto minZ = destIt.GetZ() * 2 - 2;
				const auto maxX = destIt.GetX() * 2 + 2 + 3;
				const auto maxY = destIt.GetY() * 2 + 2 + 3;
				const auto maxZ = destIt.GetZ() * 2 + 2 + 3;

				// create iterator
				return ImageIterator( minX, minY, minZ, maxX, maxY, maxZ );
			}
		};

		REDIMAGE_API ImagePtr DownscaleGuass( const ImagePtr& source, FilteringMode filerMode /*=FM_Clamp*/ )
		{
			RED_FATAL_ASSERT( source != nullptr, "Invalid source image for downscaling" );

			/// Get source image size
			const auto srcWidth = source->GetSize().m_width;
			const auto srcHeight = source->GetSize().m_height;
			const auto srcDepth = source->GetSize().m_depth;
			if( srcWidth == 1 && srcHeight == 1 && srcDepth == 1 )
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
			if( !destImage )
				return ImagePtr();

			// create access points to image
			ImageAccess srcAccess( *source );
			ImageAccess destAccess( *destImage );

			// iterate over the whole image processing pixels
			// NOTE: images are compatible so we can use the same iterator
			ImageIterator it = destImage->GetIterator();
			if( filerMode == FM_Clamp )
			{
				img::utils::FilterImage< GuassDownscaleClamp >( srcAccess, destAccess, it );
			}
			else
			{
				img::utils::FilterImage< GuassDownscaleWrap >( srcAccess, destAccess, it );
			}

			// return downscale image
			return destImage;
		}
	} // utils

} // img

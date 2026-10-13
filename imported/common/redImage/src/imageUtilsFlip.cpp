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
		template< typename T >
		void FlipRect( const ImageAccess& src, ImageAccess&  dst, Bool faceFlipX, Bool faceFlipY, Bool faceRotate )
		{
			Uint8 channels = src.GetSize().m_channels;

			Uint32 dimension = src.GetSize().m_width;
			for ( Uint32 y=0; y<dimension; y++ )
			{
				for ( Uint32 x=0; x<dimension; x++ )
				{
					// Rotate
					Uint32 destX = faceRotate ? y : x;
					Uint32 destY = faceRotate ? x : y;

					// Flip
					destX = faceFlipX ? ( (dimension-1) - destX ) : destX;
					destY = faceFlipY ? ( (dimension-1) - destY ) : destY;

					// Copy
					const T* readPtr = (T*)src.GetPixelData( x, y, 0 );
					T* writePtr = (T*)dst.GetPixelData( destX, destY, 0 );
					if( channels >= 1 ) writePtr[0] = readPtr[0];
					if( channels >= 2 ) writePtr[1] = readPtr[1];
					if( channels >= 3 ) writePtr[2] = readPtr[2];
					if( channels >= 4 ) writePtr[3] = readPtr[3];
				}
			}
		}

		REDIMAGE_API ImagePtr FlipRect( const ImagePtr& source, Bool faceFlipX, Bool faceFlipY, Bool faceRotate )
		{
			if ( !source )
				return ImagePtr();

			auto& sizeInfo = source->GetSize();
			RED_ASSERT( sizeInfo.m_width == sizeInfo.m_height, "This must be a rectangle texture." );
			if( sizeInfo.m_width != sizeInfo.m_height )
			{
				return ImagePtr();
			}

			if( !faceFlipX && !faceFlipY && !faceRotate )
			{
				return source;
			}

			// Create transformed image
			ImagePtr destImage( Image::CreateEmpty( source->GetPixelFormat(), sizeInfo, source->GetGammaExp() ) );
			RED_ASSERT( destImage, "Failed to create image for downscaled version" );
			if ( !destImage )
				return ImagePtr();

			ImageAccess src( *source );
			ImageAccess dst( *destImage );

			switch( src.GetPixelFormat() )
			{
			case PixelFormat::Format_Uint8:
				FlipRect<Uint8>( src, dst, faceFlipX, faceFlipY, faceRotate );
				break;
			case PixelFormat::Format_Uint16:
			case PixelFormat::Format_Float16:
				FlipRect<Uint16>( src, dst, faceFlipX, faceFlipY, faceRotate );
				break;
			case PixelFormat::Format_Uint32:
			case PixelFormat::Format_Float32:
				FlipRect<Uint32>( src, dst, faceFlipX, faceFlipY, faceRotate );
				break;
			}

			return destImage;
		}

		template< typename T >
		void Flip( const ImageAccess& src, ImageAccess&  dst, Bool faceFlipX, Bool faceFlipY )
		{
			Uint8 channels = src.GetSize().m_channels;

			const Uint32 width = src.GetSize().m_width;
			const Uint32 height = src.GetSize().m_height;

			for ( Uint32 x=0; x<width; x++ )
			{
				for ( Uint32 y=0; y<height; y++ )
				{
					// Rotate
					Uint32 destX = x;
					Uint32 destY = y;

					// Flip
					destX = faceFlipX ? ( (width-1) - destX ) : destX;
					destY = faceFlipY ? ( (height-1) - destY ) : destY;

					// Copy
					const T* readPtr = (T*)src.GetPixelData( x, y, 0 );
					T* writePtr = (T*)dst.GetPixelData( destX, destY, 0 );
					if( channels >= 1 ) writePtr[0] = readPtr[0];
					if( channels >= 2 ) writePtr[1] = readPtr[1];
					if( channels >= 3 ) writePtr[2] = readPtr[2];
					if( channels >= 4 ) writePtr[3] = readPtr[3];
				}
			}
		}

		REDIMAGE_API ImagePtr Flip( const ImagePtr& source, Bool flipX, Bool flipY )
		{
			if ( !source )
				return ImagePtr();

			if( !flipX && !flipY )
			{
				return source;
			}

			auto& sizeInfo = source->GetSize();
			
			// Create transformed image
			ImagePtr destImage( Image::CreateEmpty( source->GetPixelFormat(), sizeInfo, source->GetGammaExp() ) );
			RED_ASSERT( destImage, "Failed to create image for downscaled version" );
			if ( !destImage )
				return ImagePtr();

			ImageAccess src( *source );
			ImageAccess dst( *destImage );

			switch( src.GetPixelFormat() )
			{
			case PixelFormat::Format_Uint8:
				Flip<Uint8>( src, dst, flipX, flipY );
				break;
			case PixelFormat::Format_Uint16:
			case PixelFormat::Format_Float16:
				Flip<Uint16>( src, dst, flipX, flipY );
				break;
			case PixelFormat::Format_Uint32:
			case PixelFormat::Format_Float32:
				Flip<Uint32>( src, dst, flipX, flipY );
				break;
			}

			return destImage;
		}

	} // utils

} // img
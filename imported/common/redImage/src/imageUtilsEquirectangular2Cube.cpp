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
		// [Yaw , Pitch]
		Float faceTransform[6][2] = 
		{ 
			{RED_PI / 2, 0},	// right
			{-RED_PI / 2, 0}, // left
			{0, 0},			// front
			{RED_PI, 0},		// back
			{0, -RED_PI / 2}, // top
			{0, RED_PI / 2}   // bottom
		};

		template< typename T >
		void ExtractFace( const ImageAccess& src, ImageAccess&  dst, Uint16 face )
		{
			Uint8 channels = src.GetSize().m_channels;

			Uint32 inWidth = src.GetSize().m_width;
			Uint32 inHeight = src.GetSize().m_height;
			
			Uint32 outWidth = dst.GetSize().m_width;
			Uint32 outHeight = dst.GetSize().m_height;

			const Float an = (Float)MSin( RED_PI / 4.0 );
			const Float ak = (Float)MCos( RED_PI / 4.0 );

			const Float ftu = faceTransform[face][0];
			const Float ftv = faceTransform[face][1];

			for( Uint32 y = 0; y < outHeight; ++y )
			{
				for( Uint32 x = 0; x < outWidth; ++x )
				{
					// Map face pixel coordinates to [1,1] on plane
					Float nx = (Float)x / (Float)outWidth - 0.5f;
					Float ny = (Float)y / (Float)outHeight- 0.5f;
					nx *= (2 * an);
					ny *= (2 * an);

					Float u,v;

					// Project from plane to sphere surface
					if( ftv == 0 )
					{
						// Center faces
						u = atan2( nx, ak );
						v = atan2( ny * MCos( u ), ak );
						u += ftu;
					}
					else if( ftv > 0 )
					{
						// Bottom face
						Float d = sqrt( nx * nx + ny * ny );
						v = RED_PI / 2 - atan2( d, ak );
						u = atan2( ny, nx );
					}
					else
					{
						// Top face
						Float d = sqrt( nx * nx + ny * ny );
						v = -RED_PI / 2 + atan2( d, ak );
						u = atan2( -ny, nx );
					}

					// Map from angular coordinates to [-1,1], respectively
					u = u / RED_PI;
					v = v / (RED_PI/2);

					// Wrap
					while( v < -1 )
					{
						v += 2;
						u += 1;
					}
					while( v > 1 )
					{
						v -=2;
						u += 1;
					}
					while( u < -1 )
					{
						u += 2;
					}
					while( u > 1 )
					{
						u -= 2;
					}

					// Map from [-1,1] to texture space
					u = u / 2.0f + 0.5f;
					v = v / 2.0f + 0.5f;

					u = u * (inWidth-1);
					v = v * (inHeight-1);

					// remap
					const T* srcPtr = (T*)src.GetPixelData( (Uint32)MRound(u), (Uint32)MRound(v), 0 );
					T* dstPtr = (T*)dst.GetPixelData( x, y, 0 );

					if( channels >= 1 ) dstPtr[0] += srcPtr[0];
					if( channels >= 2 ) dstPtr[1] += srcPtr[1];
					if( channels >= 3 ) dstPtr[2] += srcPtr[2];
					if( channels >= 4 ) dstPtr[3] += srcPtr[3];
				}
			}
		}

		REDIMAGE_API ImagePtr Equirectangular2Cube( const ImagePtr& source, Uint16 face, Uint32 destWidth, Uint32 destHeight )
		{
			if ( !source )
				return ImagePtr();

			RED_ASSERT( face < 6, "We're mapping to cube face [0-5]." );
			if( face >= 6 )
				return ImagePtr();
			
			// Create transformed image
			auto sizeInfo = source->GetSize();
			sizeInfo.m_width = destWidth;
			sizeInfo.m_height = destHeight;

			ImagePtr destImage( Image::CreateEmpty( source->GetPixelFormat(), sizeInfo, source->GetGammaExp() ) );
			RED_ASSERT( destImage, "Failed to create image for cube face." );
			if ( !destImage )
				return ImagePtr();

			ImageAccess src( *source );
			ImageAccess dst( *destImage );

			switch( src.GetPixelFormat() )
			{
			case PixelFormat::Format_Uint8:
				ExtractFace<Uint8>( src, dst, face );
				break;
			case PixelFormat::Format_Uint16:
			case PixelFormat::Format_Float16:
				ExtractFace<Uint16>( src, dst, face );
				break;
			case PixelFormat::Format_Uint32:
			case PixelFormat::Format_Float32:
				ExtractFace<Uint32>( src, dst, face );
				break;
			}

			return destImage;
		}
	} // utils

} // img
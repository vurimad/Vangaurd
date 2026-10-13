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
		REDIMAGE_API void UnpackPixel_SLOW( const Image& image, const ImageAccess& access, const ImageIterator& it, ImagePixel& outPixel )
		{
			if ( image.GetSize().m_channels == 1 )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: access.Unpack< ImageFormatUint8_UNORM<1> >( it, outPixel ); break;
					case PixelFormat::Format_Uint16: access.Unpack< ImageFormatUint16_UNORM<1> >( it, outPixel ); break;
					case PixelFormat::Format_Float16: access.Unpack< ImageFormatFloat16<1> >( it, outPixel ); break;
					case PixelFormat::Format_Uint32: access.Unpack< ImageFormat32_DIRECT<1> >( it, outPixel ); break;
					case PixelFormat::Format_Float32: access.Unpack< ImageFormat32_DIRECT<1> >( it, outPixel ); break;
				}
			}
			else if ( image.GetSize().m_channels == 2 )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: access.Unpack< ImageFormatUint8_UNORM<2> >( it, outPixel ); break;
					case PixelFormat::Format_Uint16: access.Unpack< ImageFormatUint16_UNORM<2> >( it, outPixel ); break;
					case PixelFormat::Format_Float16: access.Unpack< ImageFormatFloat16<2> >( it, outPixel ); break;
					case PixelFormat::Format_Uint32: access.Unpack< ImageFormat32_DIRECT<2> >( it, outPixel ); break;
					case PixelFormat::Format_Float32: access.Unpack< ImageFormat32_DIRECT<2> >( it, outPixel ); break;
				}
			}
			else if ( image.GetSize().m_channels == 3 )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: access.Unpack< ImageFormatUint8_UNORM<3> >( it, outPixel ); break;
					case PixelFormat::Format_Uint16: access.Unpack< ImageFormatUint16_UNORM<3> >( it, outPixel ); break;
					case PixelFormat::Format_Float16: access.Unpack< ImageFormatFloat16<3> >( it, outPixel ); break;
					case PixelFormat::Format_Uint32: access.Unpack< ImageFormat32_DIRECT<3> >( it, outPixel ); break;
					case PixelFormat::Format_Float32: access.Unpack< ImageFormat32_DIRECT<3> >( it, outPixel ); break;
				}
			}
			else if ( image.GetSize().m_channels == 4 )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: access.Unpack< ImageFormatUint8_UNORM<4> >( it, outPixel ); break;
					case PixelFormat::Format_Uint16: access.Unpack< ImageFormatUint16_UNORM<4> >( it, outPixel ); break;
					case PixelFormat::Format_Float16: access.Unpack< ImageFormatFloat16<4> >( it, outPixel ); break;
					case PixelFormat::Format_Uint32: access.Unpack< ImageFormat32_DIRECT<4> >( it, outPixel ); break;
					case PixelFormat::Format_Float32: access.Unpack< ImageFormat32_DIRECT<4> >( it, outPixel ); break;
				}
			}
		}

		REDIMAGE_API void PackPixel_SLOW( Image& image, ImageAccess& access, const ImageIterator& it, const ImagePixel& pixel )
		{
			if ( image.GetSize().m_channels == 1 )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: access.Pack< ImageFormatUint8_UNORM<1> >( it, pixel ); break;
					case PixelFormat::Format_Uint16: access.Pack< ImageFormatUint16_UNORM<1> >( it, pixel ); break;
					case PixelFormat::Format_Float16: access.Pack< ImageFormatFloat16<1> >( it, pixel ); break;
					case PixelFormat::Format_Uint32: access.Pack< ImageFormat32_DIRECT<1> >( it, pixel ); break;
					case PixelFormat::Format_Float32: access.Pack< ImageFormat32_DIRECT<1> >( it, pixel ); break;
				}
			}
			else if ( image.GetSize().m_channels == 2 )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: access.Pack< ImageFormatUint8_UNORM<2> >( it, pixel ); break;
					case PixelFormat::Format_Uint16: access.Pack< ImageFormatUint16_UNORM<2> >( it, pixel ); break;
					case PixelFormat::Format_Float16: access.Pack< ImageFormatFloat16<2> >( it, pixel ); break;
					case PixelFormat::Format_Uint32: access.Pack< ImageFormat32_DIRECT<2> >( it, pixel ); break;
					case PixelFormat::Format_Float32: access.Pack< ImageFormat32_DIRECT<2> >( it, pixel ); break;
				}
			}
			else if ( image.GetSize().m_channels == 3 )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: access.Pack< ImageFormatUint8_UNORM<3> >( it, pixel ); break;
					case PixelFormat::Format_Uint16: access.Pack< ImageFormatUint16_UNORM<3> >( it, pixel ); break;
					case PixelFormat::Format_Float16: access.Pack< ImageFormatFloat16<3> >( it, pixel ); break;
					case PixelFormat::Format_Uint32: access.Pack< ImageFormat32_DIRECT<3> >( it, pixel ); break;
					case PixelFormat::Format_Float32: access.Pack< ImageFormat32_DIRECT<3> >( it, pixel ); break;
				}
			}
			else if ( image.GetSize().m_channels == 4 )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: access.Pack< ImageFormatUint8_UNORM<4> >( it, pixel ); break;
					case PixelFormat::Format_Uint16: access.Pack< ImageFormatUint16_UNORM<4> >( it, pixel ); break;
					case PixelFormat::Format_Float16: access.Pack< ImageFormatFloat16<4> >( it, pixel ); break;
					case PixelFormat::Format_Uint32: access.Pack< ImageFormat32_DIRECT<4> >( it, pixel ); break;
					case PixelFormat::Format_Float32: access.Pack< ImageFormat32_DIRECT<4> >( it, pixel ); break;
				}
			}
		}

	} // utils

} // img
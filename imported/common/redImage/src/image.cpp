/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"

#include "image.h"
#include "imageLoader.h"
#include "imageSaver.h"
#include "imageMemory.h"
#include "../../redSystem/include/log.h"

namespace img
{
	//---------------------------------------------------------------------------

	Image::SizeInfo::SizeInfo( const Uint8 channels/*=1*/, const Uint16 width/*=1*/, const Uint16 height/*=1*/, const Uint16 depth/*=1*/ )
		: m_channels( channels )
		, m_width( width )
		, m_height( height )
		, m_depth( depth )
	{
	}

	const Bool Image::SizeInfo::IsValid() const
	{
		return m_channels >= 1 && m_width >= 1 && m_height >= 1 && m_depth >= 1;
	}


	bool Image::SizeInfo::operator==( const SizeInfo & other )
	{
		return m_channels == other.m_channels 
			&& m_width == other.m_width
			&& m_height == other.m_height
			&& m_depth == other.m_depth;
	}

	//---------------------------------------------------------------------------

	ImagePtr Image::CreateEmpty( PixelFormat format, const SizeInfo& size, Float gammaExp /*=1.0f*/ )
	{
		// calculate memory layout for the created image
		MemoryLayout layout;
		if ( !CalcMemoryLayout( format, size, layout ) )
		{
			RED_LOG_ERROR( "Invalid image size or format (%dx%dx%d %dC). Image not created.",
				size.m_width, size.m_height, size.m_depth, size.m_channels );
			return ImagePtr();
		}

		// calculate amount of memory to allocate for the image
		const auto memorySize = CalcImageSize( size, layout );

		// allocate image image
		void* memory = memory::Allocate( memorySize );// "Image" , layout.m_rowAlignment );
		if ( !memory )
		{
			RED_LOG_ERROR( "Not enough memory to allocate %1.2fMB for image (%dx%dx%d %dC). Image not created.",
				memorySize / (1024.0*1024.0), size.m_width, size.m_height, size.m_depth, size.m_channels );
			return ImagePtr();
		}

		// create image wrapper
		ImagePtr ret( RED_NEW( Image )() );
		ret->m_format = format;
		ret->m_pixels = memory;
		ret->m_size = size;
		ret->m_layout = layout;
		ret->m_gammaExp = gammaExp;
		return ret;
	}

	//---------------------------------------------------------------------------

	Image::Image()
		: m_pixels( nullptr )
		, m_format( PixelFormat::Format_Uint8 )
		, m_gammaExp( 1.0f )
		, m_userData( 0ull )
	{
		memset( &m_layout, 0, sizeof(m_layout) );
		memset( &m_size, 0, sizeof(m_size) );
	}

	Image::~Image()
	{
		// free image memory
		if ( m_pixels )
		{
			memory::Free( m_pixels );
			m_pixels = nullptr;
		}
	}

	ImageIterator Image::GetIterator() const
	{
		return ImageIterator( 
			0, 0, 0,  // min
			(Int32)m_size.m_width, // maxX
			(Int32)m_size.m_height, // maxY
			(Int32)m_size.m_depth // maxZ
		);
	}

	const Bool Image::CalcFormatData( const PixelFormat format, Uint32& outPixelSize, Uint32& outFormatAlignment )
	{
		switch ( format )
		{
			case PixelFormat::Format_Uint8: outPixelSize = 1; outFormatAlignment = 4; return true;
			case PixelFormat::Format_Uint16: outPixelSize = 2; outFormatAlignment = 4; return true;
			case PixelFormat::Format_Float16: outPixelSize = 2; outFormatAlignment = 16; return true;
			case PixelFormat::Format_Uint32: outPixelSize = 4; outFormatAlignment = 4; return true;
			case PixelFormat::Format_Float32: outPixelSize = 4; outFormatAlignment = 16; return true;
		}

		return false;
	}

	const Bool Image::CalcMemoryLayout( const PixelFormat format, const SizeInfo& size, MemoryLayout& outLayout )
	{
		// size is not valid
		if ( !size.IsValid() )
			return false;

		// compute pixel size
		Uint32 channelSize = 1;
		Uint32 rowAlignment = 1;
		if ( !CalcFormatData( format, channelSize, rowAlignment ) )
			return false;

		// compute row pitch (aligned to format size)
		outLayout.m_pixelPitch = size.m_channels * channelSize;
		outLayout.m_rowPitch = red::AlignUp( size.m_width * outLayout.m_pixelPitch, rowAlignment );
		outLayout.m_layerPitch = outLayout.m_rowPitch * size.m_height;
		outLayout.m_rowAlignment = rowAlignment;

		// layout valid
		return true;
	}

	const Uint32 Image::CalcImageSize( const SizeInfo& size, const MemoryLayout& memoryLayout )
	{
		return memoryLayout.m_layerPitch * size.m_depth;
	}

} // img
/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"

#include "imageMemory.h"
#include "imageSaverPNG.h"
#include "png/png.h"
#include "../../redSystem/include/log.h"
#include "../../redContainers/include/string/string.h"
#include "../../redContainers/include/string/stringBuilder.h"

namespace png
{
	static void WriteToFile( png_structp png_ptr, png_bytep data, png_size_t length )
	{
		auto* file = static_cast< IFile* >( png_get_io_ptr( png_ptr ) );
		file->Serialize( data, static_cast< Uint32 >( length ) );
	}

	static png_voidp AllocMemory( png_structp png_ptr, png_size_t size )
	{
		return img::memory::Allocate( size );
	}

	static void FreeMemory( png_structp png_ptr, png_voidp ptr )
	{
		return img::memory::Free( ptr );
	}

} // PNG

namespace img
{
	namespace prv
	{
		ImageSaverPNG::ImageSaverPNG() = default;

		Bool ImageSaverPNG::DoSave( IFile& writer, const Image& image, const AnsiChar* typeHint ) const
		{
			const auto basePos = writer.GetOffset();
			RED_UNUSED( basePos );//! to satisfied clang on Final build

			// only PNG file can be saved
			if ( 0 != red::StrcmpNC( typeHint, "png" ) )
				return false;

			// only 8 and 16 bit files can be saved
			if ( image.GetPixelFormat() != PixelFormat::Format_Uint8 && image.GetPixelFormat() != PixelFormat::Format_Uint16 )
				return false; // unsupported format

			// Initialize write structure
			png_structp png_ptr = png_create_write_struct_2( PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr, nullptr, &png::AllocMemory, &png::FreeMemory );
			if ( !png_ptr )
			{
				RED_LOG_ERROR( "ImageSaverPng: Failed to create write structure" );
				return false;
			}

			// Initialize info structure
			png_infop info_ptr = png_create_info_struct( png_ptr );
			if ( !info_ptr )
			{
				png_destroy_write_struct( &png_ptr, static_cast< png_infopp >( nullptr ) );
				RED_LOG_ERROR( "ImageSaverPng: : Failed to create info structure" );
				return false;
			}

			// Setup Exception handling
			if ( setjmp( png_jmpbuf( png_ptr ) ) )
			{
				RED_LOG_ERROR( "ImageSaverPng: Fatal error occured" );

				if (info_ptr)
					png_free_data( png_ptr, info_ptr, PNG_FREE_ALL, -1 );

				if (png_ptr)
					png_destroy_write_struct( &png_ptr, static_cast< png_infopp >( nullptr ) );

				return false;
			}

			// Setup IO
			png_set_write_fn( png_ptr, &writer, &png::WriteToFile, nullptr );

			// Determine format
			Uint8 pngFormat = 0;
			if ( image.GetSize().m_channels == 1 )
			{
				pngFormat = PNG_COLOR_TYPE_GRAY;
			}
			else if ( image.GetSize().m_channels == 2 )
			{
				pngFormat = PNG_COLOR_TYPE_GRAY_ALPHA;
			}
			else if ( image.GetSize().m_channels == 3 )
			{
				pngFormat = PNG_COLOR_TYPE_RGB;
			}
			else if ( image.GetSize().m_channels == 4 )
			{
				pngFormat = PNG_COLOR_TYPE_RGB_ALPHA;
			}

			// Determine bit range
			auto bits = 8; 

			switch(image.GetPixelFormat())
			{
			case PixelFormat::Format_Uint8:	
				bits = 8;
				break;
			case PixelFormat::Format_Uint16:
			case PixelFormat::Format_Float16:
				bits = 16;
				break;
			case PixelFormat::Format_Uint32:
			case PixelFormat::Format_Float32:
				bits = 32;
				break;
			}

			// Write header (8 bit colour depth)
			const auto width = image.GetSize().m_width;
			const auto height = image.GetSize().m_height;
			png_set_IHDR( png_ptr, info_ptr, width, height, bits, pngFormat, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE );
			RED_LOG( "ImageSaverPng: Saving image %dx%d, bits %d, format %d", width, height, bits, pngFormat );

			
			png_text texts[2];

			// Set title
			png_text& title_text = texts[0];
			title_text.compression = PNG_TEXT_COMPRESSION_NONE;
			title_text.key = ( png_charp )"Title";
			title_text.text = ( png_charp )"ImageSaverPNG";

			// Set user data
			png_text& userData_text = texts[1];
			userData_text.compression = PNG_TEXT_COMPRESSION_NONE;
			userData_text.key = ( png_charp )"UserData";

			// Saving as a string instead of Uint64 here for simplicity and debugging's sake. 
			// One would need some kind of coding to ensure that there are no null bytes in the middle of saved cstring.
			// Also, data debugging would be more difficult with the number encoded. It's just a few bytes anyway.
			red::StringBuilder< red::String > builder;
			builder.Appendf( "%lld", image.GetUserData() );
			userData_text.text = const_cast< png_charp >( builder.AsChar() );

			png_set_text( png_ptr, info_ptr, texts, RED_ARRAY_COUNT_U32( texts ) );
		
			// Write image info
			png_write_info( png_ptr, info_ptr );

			// Allocate row pointers and prepare them
			auto * rows = static_cast< png_bytep* >( img::memory::Allocate( sizeof( png_bytep ) * height ) );
			ImageAccess imageAccess( image );
			for(Uint32 i = 0; i < height; ++i)
			{
				rows[i] = static_cast< png_bytep >( imageAccess.GetPixelData( 0, i, 0 ) );
			}

			// Byte swap to big-endian.
			if ( image.GetPixelFormat() == PixelFormat::Format_Uint16 )
			{
				png_set_swap( png_ptr );
			}

			// Write rows
			png_write_rows( png_ptr, rows, height );

			// End write
			png_write_end(png_ptr, nullptr);

			// Free info
			png_free_data( png_ptr, info_ptr, PNG_FREE_ALL, -1 );

			// Close write
			png_destroy_write_struct( &png_ptr, static_cast< png_infopp >( nullptr ) );
			img::memory::Free( rows );

			RED_LOG( "ImageSaverPng: Written %1.2f KB", (writer.GetOffset() - basePos) / 1024.0f );
			return true;
		}


	}
}

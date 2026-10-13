/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"

#include "imageLoaderPNG.h"
#include "imageMemory.h"

#include "png/png.h"
#include "../../redSystem/include/log.h"
#include "../src/png/pngpriv.h"

namespace
{
	void ReadFromFile( png_structp png_ptr, png_bytep data, png_size_t length )
	{
		auto* file = static_cast< IFile* >( png_get_io_ptr( png_ptr ) );
		file->Serialize( data, length );
		// TODO signal error? will be detected through CRC anyways
	}

	png_voidp AllocMemory( png_structp png_ptr, png_alloc_size_t size )
	{
		return img::memory::Allocate( size );
	}

	void FreeMemory( png_structp png_ptr, png_voidp ptr )
	{
		img::memory::Free( ptr );
	}

	void HandleOnError( png_structp png_ptr, png_const_charp message )
	{
		RED_LOG_ERROR( "ImageLoaderPng: %s", message );
	}

	void HandleOnWarning( png_structp png_ptr, png_const_charp message )
	{
		RED_LOG_WARNING( "ImageLoaderPng: %s", message );
	}

	void HandleOnErrorDefault( png_structp png_ptr, png_const_charp message )
	{
		RED_LOG_ERROR( "ImageLoaderPng: %s", message );
	}

	void HandleOnWarningDefault( png_structp png_ptr, png_const_charp message )
	{
		RED_LOG_WARNING( "ImageLoaderPng: %s", message );
	}

	struct ReadState final
	{
		ReadState()
			: state{ png_create_read_struct_2( PNG_LIBPNG_VER_STRING, nullptr, &HandleOnError, &HandleOnWarning, nullptr, &AllocMemory, &FreeMemory ) }
		{
			RED_ASSERT( IsAllocated(), "Failed to allocate libpng read struct." );
		}

		~ReadState()
		{
			if( IsAllocated() )
			{
				png_destroy_read_struct( &state, nullptr, nullptr );
				RED_ASSERT( !IsAllocated(), "State not destroyed." );
			}
		}

		bool IsAllocated() const
		{
			return state != nullptr;
		}

		png_structp state;
	};

	struct InfoState final
	{
		explicit InfoState( const png_structp& readState )
			: read{ readState }
			, state{ png_create_info_struct( read ) }
		{
			RED_ASSERT( IsAllocated(), "Failed to allocate libpng info struct." );
		}

		~InfoState()
		{
			if( IsAllocated() )
			{
				png_destroy_info_struct( read, &state );
				RED_ASSERT( !IsAllocated(), "State not destroyed." );
			}
		}

		bool IsAllocated() const
		{
			return state != nullptr;
		}

		const png_structp& read;
		png_infop state;
	};

#pragma pack(push, 1)
	struct Header
	{
		Uint8 data[8];
	};
#pragma pack(pop)
	static_assert( sizeof( Header ) == 8, "" );
}

namespace img
{
	namespace prv
	{
		ImageLoaderPNG::ImageLoaderPNG() = default;

		ImagePtr ImageLoaderPNG::DoLoad( IFile& reader, const AnsiChar* typeHint ) const
		{
			// load header
			Header header{};
			reader.Serialize( &header, sizeof( header ) );
			if( reader.HasErrors() )
			{
				RED_LOG_ERROR( "ImageLoaderPng: Failed reading header." );
				return {};
			}

			// check signature
			if( png_sig_cmp( header.data, 0, sizeof( header ) ) != 0 )
			{
				RED_LOG_ERROR( "ImageLoaderPng: Failed signature check." );
				return {};
			}

			// initialize PNGlib
			ReadState read{};
			if( !read.IsAllocated() )
			{
				RED_LOG_ERROR( "ImageLoaderPng: Failed to initialize PNG lib" );
				return {};
			}

			// NOTE set error handling behaviour
			png_set_error_fn( read.state, nullptr, &HandleOnErrorDefault, &HandleOnWarningDefault );
			png_set_crc_action( read.state, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT );

			// create info
			InfoState info{ read.state };
			if( !info.IsAllocated() )
			{
				RED_LOG_ERROR( "ImageLoaderPng: Failed to initialize PNG read info" );
				return {};
			}

			// error handler, on error the execution will jump here
			// NOTE keep setjmp in final builds otherwise libpng will abort application execution
			if( setjmp( png_jmpbuf( read.state ) ) )
			{
				RED_LOG_ERROR( "ImageLoaderPng: Internal error" );
				return {};
			}

			// load data
			png_set_sig_bytes( read.state, sizeof( header ) );
			png_set_read_fn( read.state, &reader, &ReadFromFile );
			png_read_info( read.state, info.state );

			// get image size
			const Uint32 width = png_get_image_width( read.state, info.state );
			const Uint32 height = png_get_image_height( read.state, info.state );
			Uint32 bitDepth = png_get_bit_depth( read.state, info.state );
			RED_LOG_SPAM( "ImageLoaderPng: Loaded image %dx%d, bits: %d", width, height, bitDepth );

			Int64 readUserData = 0;

			// get text
			{
				png_textp pptr = nullptr;
				Int32 numTexts = 0;
				png_get_text( read.state, info.state, &pptr, &numTexts );

				for( Int32 i = 0; i < numTexts; ++i )
				{
					const auto& nth = pptr[i];
					if( std::strcmp( nth.key, "UserData" ) == 0 )
					{
						const red::StringView text{ nth.text };
						if( !text.Empty() )
						{
							// See imageSaverPNG.cpp for more details.
							red::StringToInt( readUserData, &text.Front(), nullptr, red::Base::BaseTen );
						}
						break;
					}
				}
			}

			Uint32 numChannels = 4;

			// transform color type into the number of channels
			{
				const Uint32 colorType = png_get_color_type( read.state, info.state );
				switch( colorType )
				{
					case PNG_COLOR_TYPE_GRAY:
					{
						if( bitDepth < 8 )
						{
							png_set_expand_gray_1_2_4_to_8( read.state );
							bitDepth = 8;
						}

						RED_LOG_SPAM( "ImageLoaderPng: Color type: Grayscale" );
						numChannels = 1;
					}
					break;

					case PNG_COLOR_TYPE_PALETTE:
					{
						RED_LOG_SPAM( "ImageLoaderPng: Color type: RGB Palette" );
						png_set_palette_to_rgb( read.state );
						numChannels = 3;
					}
					break;

					case PNG_COLOR_TYPE_RGB:
					{
						RED_LOG_SPAM( "ImageLoaderPng: Color type: RGB" );
						numChannels = 3;
					}
					break;

					case PNG_COLOR_TYPE_RGB_ALPHA:
					{
						RED_LOG_SPAM( "ImageLoaderPng: Color type: RGBA" );
						numChannels = 4;
					}
					break;

					case PNG_COLOR_TYPE_GRAY_ALPHA:
					{
						RED_LOG_SPAM( "ImageLoaderPng: Color type: Gray + Alpha" );
						png_set_gray_to_rgb( read.state );
						numChannels = 4; // expand
					}
					break;

					default:
					{
						RED_LOG_ERROR( "ImageLoaderPng: Unsupported PNG color type %d", colorType );
						return {};
					}
				}
			}

			// choose pixel format
			PixelFormat pixelFormat;
			switch( bitDepth )
			{
				case 8:
				{
					pixelFormat = PixelFormat::Format_Uint8;
				}
				break;

				case 16:
				{
					pixelFormat = PixelFormat::Format_Uint16;
					png_set_swap( read.state );
				}
				break;

				default:
				{
					RED_LOG_ERROR( "ImageLoaderPng: Unsupported PNG bitdepth: %d", bitDepth );
					return {};
				}
			}

			// update info
			png_read_update_info( read.state, info.state );

			// allocate image
			const Image::SizeInfo sizeInfo( numChannels, width, height );
			auto result = Image::CreateEmpty( pixelFormat, sizeInfo );
			if( !result )
			{
				RED_LOG_ERROR( "ImageLoaderPng: Failed to allocate target image" );
				return {};
			}

			// read pixel data
			{
				// prepare pointer buffer
				auto rows = static_cast< png_bytep* >( img::memory::Allocate( sizeof( png_bytep ) * height ) );
				for( Uint32 y = 0; y < height; y++ )
				{
					// compute target offset for each row
					const auto offset = result->GetLayout().CalcPixelOffset( 0, y, 0 );
					rows[y] = static_cast< png_bytep >( result->GetPixelBuffer() ) + offset;
				}

				// load image
				png_read_image( read.state, rows );

				// release pointer buffer
				img::memory::Free( rows );
				rows = nullptr;
			}

			// finish
			png_read_end( read.state, nullptr );

			// set user data
			result->SetUserData( readUserData );

			// return loaded image
			return result;
		}

	} // prv
} // img

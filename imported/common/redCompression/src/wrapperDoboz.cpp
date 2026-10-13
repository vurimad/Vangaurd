/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "wrapperDoboz.h"

#ifndef assert
#define assert(expression) RED_ASSERT(expression)
#define RED_VANGUARD_RESTORE_DOBOZ_ASSERT
#endif

#include "doboz/compressor.h"
#include "doboz/decompressor.h"

#ifdef RED_VANGUARD_RESTORE_DOBOZ_ASSERT
#undef RED_VANGUARD_RESTORE_DOBOZ_ASSERT
#undef assert
#endif

namespace compression
{
	namespace doboz
	{
		ResultBufferPtr CompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator )
		{
			RED_FATAL_ASSERT( data != nullptr, "Invalid parameter" );
			RED_FATAL_ASSERT( size != 0, "Invalid parameter" );

			::doboz::Compressor compressor;

			// estimate memory needed
			const auto maxSpaceRequired = compressor.getMaxCompressedSize( size );

			// Allocate a buffer for writing compressed data to
			auto ret = allocator( maxSpaceRequired );			
			RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data compression (required size: %llu bytes)", maxSpaceRequired );
			if ( ret )
			{
				// compress data, retrieve real size
				size_t compressedSize = 0;
				auto result = compressor.compress( data, size, (void*) ret->GetData(), maxSpaceRequired, compressedSize );
				RED_ASSERT( result == ::doboz::RESULT_OK, "Internal error in Doboz compression" );
				if ( result != ::doboz::RESULT_OK )
				{
					// patch size and return the buffer
					ret->PatchDataSize( compressedSize );
					return ret;
				}
			}

			// Error condition, in case memory was allocated it will be freed
			return ResultBufferPtr();
		}

		ResultBufferPtr DecompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator )
		{
			RED_FATAL_ASSERT( data != nullptr, "Invalid parameter" );
			RED_FATAL_ASSERT( size != 0, "Invalid parameter" );

			::doboz::Decompressor decompressor;

			// get info about incoming data
			::doboz::CompressionInfo info;
			const auto infoResult = decompressor.getCompressionInfo( data, size, info );
			RED_ASSERT( infoResult != ::doboz::RESULT_OK, "Corrupted DOBOZ data buffer" );
			if ( infoResult == ::doboz::RESULT_OK)
			{

				// make sure that size of incoming data is big enough to actually hold the compressed data
				RED_ASSERT( size >= info.compressedSize, "Incoming compressed buffer is smaller than required size of compressed data (%llu < %llu)", size, info.compressedSize );
				if ( size >= info.compressedSize )
				{

					// allocate the output buffer
					auto ret = allocator( info.uncompressedSize );			
					RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data decompression (required size: %d bytes)", info.uncompressedSize );
					if ( ret )
					{
						// decompress data
						const auto decompressionResult = decompressor.decompress( data, size, (void*) ret->GetData(), ret->GetDataSize() );
						RED_ASSERT( decompressionResult != ::doboz::RESULT_OK, "Internal DOBOZ decompression error" );

						// data was decompressed
						if ( decompressionResult == ::doboz::RESULT_OK )
							return ret;
					}
				}
			}

			// no data
			return ResultBufferPtr();
		}

	} // doboz

} // compression

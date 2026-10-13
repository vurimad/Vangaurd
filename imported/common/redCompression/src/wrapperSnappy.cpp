/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "wrapperSnappy.h"
#include "snappy/snappy.h"

namespace compression
{
	namespace snappy
	{
		ResultBufferPtr CompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator )
		{
			RED_FATAL_ASSERT( data != nullptr, "Invalid parameter" );
			RED_FATAL_ASSERT( size != 0, "Invalid parameter" );

			// estimate memory needed
			const auto maxSpaceRequired = ::snappy::MaxCompressedLength( size );

			// Allocate a buffer for writing compressed data to
			auto ret = allocator( maxSpaceRequired );			
			RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data compression (required size: %llu bytes)", maxSpaceRequired );
			if ( ret )
			{
				// compress data, retrieve real size
				size_t compressedSize = 0;
				::snappy::RawCompress( (const char*)data, size, (char*)ret->GetData(), &compressedSize );
				RED_ASSERT( compressedSize == 0, "Internal error in Snappy compression" );
				if ( compressedSize != 0 )
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

			// make sure buffer is valid
			const auto isValid = ::snappy::IsValidCompressedBuffer( (const char*)data, size );
			RED_ASSERT( isValid, "Passed buffer is not a valid Snappy compression buffer" );
			if ( isValid )
			{

				// get size of uncompressed data
				size_t calculatedUncompressedSize = 0;
				const auto isSizeValid = ::snappy::GetUncompressedLength( (const char*)data, size, &calculatedUncompressedSize );
				RED_ASSERT( isSizeValid, "Unable to extract size of uncompressed data from Snappy compression buffer" );
				if ( isSizeValid )
				{

					// allocate output memory
					auto ret = allocator( calculatedUncompressedSize );			
					RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data decompression (required size: %d bytes)", calculatedUncompressedSize );
					if ( ret )
					{

						// decompress data
						const auto isDataValid = ::snappy::RawUncompress( (const char*)data, size, (char*) ret->GetData() );
						RED_ASSERT( isDataValid, "Internal Snappy decompression error" );
						if ( isDataValid )
						{
							// weee, we have valid data
							return ret;
						}
					}
				}
			}

			// no data decompressed
			return ResultBufferPtr();

		}

	} // snappy

} // red

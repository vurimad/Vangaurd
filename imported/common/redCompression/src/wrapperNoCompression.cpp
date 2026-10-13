/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "wrapperNoCompression.h"

namespace compression
{
	namespace none
	{

		ResultBufferPtr CompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator )
		{
			RED_FATAL_ASSERT( data != nullptr, "Invalid parameter" );
			RED_FATAL_ASSERT( size != 0, "Invalid parameter" );

			// Allocate a buffer for writing compressed data to
			auto ret = allocator( size );			
			RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data compression (required size: %llu bytes)", size );

			// copy the data
			if ( ret )
				red::Memcpy( (void*) ret->GetData(), data, size );

			return ret;
		}

		ResultBufferPtr DecompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator )
		{
			RED_FATAL_ASSERT( data != nullptr, "Invalid parameter" );
			RED_FATAL_ASSERT( size != 0, "Invalid parameter" );

			// Allocate a buffer for writing compressed data to
			auto ret = allocator( size );			
			RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data decompression (required size: %llu bytes)", size );

			// copy the data
			if ( ret )
				red::Memcpy( (void*) ret->GetData(), data, size );

			return ret;
		}

	} // none

} // compression

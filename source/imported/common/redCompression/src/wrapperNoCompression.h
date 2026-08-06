/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

/// NOTE: this is private header

#include "compression.h"

namespace compression
{
	namespace none
	{
		// copies the data using the allocator (to mimic the normal path), inefficient to but in some cases it's ok
		extern ResultBufferPtr CompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator );
		extern ResultBufferPtr DecompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator );

	} // snappy

} // red
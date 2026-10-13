/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

/// NOTE: this is private header

#include "compression.h"

namespace compression
{
	namespace snappy
	{
		extern ResultBufferPtr CompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator );
		extern ResultBufferPtr DecompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator );

	} // snappy

} // red
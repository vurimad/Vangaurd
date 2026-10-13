/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

/// NOTE: this is private header

#include "compression.h"

namespace compression
{
	namespace lz4
	{
		extern Uint32 GetMagic();
		extern Uint32 GetCompressBound( Uint32 size );
		extern ResultBufferPtr CompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator ); // LZ4
		extern ResultBufferPtr CompressDataHC( const void* data, const red::Uint64 size, TCompressionAllocator allocator ); // LZ4HC

		extern ResultBufferPtr DecompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator ); // handles both LZ versions

	} // snappy

} // red
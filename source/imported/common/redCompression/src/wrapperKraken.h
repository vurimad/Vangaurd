/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

/// NOTE: this is private header

#include "compression.h"

namespace compression
{
	namespace kraken
	{
		extern Uint32 GetMagic();

		extern ResultBufferPtr CompressData(const void* data, const red::Uint64 size, TCompressionAllocator allocator); // Kraken with everyday, less optimal compression
		extern ResultBufferPtr CompressDataHC(const void* data, const red::Uint64 size, TCompressionAllocator allocator); // Kraken with higher compression

		extern ResultBufferPtr DecompressData(const void* data, const red::Uint64 size, TCompressionAllocator allocator); // handles all Kraken compressed versions

	} // snappy

} // red
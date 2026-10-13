/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

/// NOTE: this is private header

namespace compression
{
	namespace zlib
	{
		extern Uint32 GetMagic();

		// the created z-lib compressed buffer can (or not) contain our size header (so we know how much memory to allocated on decompression)
		extern ResultBufferPtr CompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator, const Bool addSizeHeader );

		// if the z-lib data does not contain our special header that specifies the size of the output data we will (unfortunately) do a dry decompression just to determien that (this is very rare though)
		extern ResultBufferPtr DecompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator );

	} // prv

} // red
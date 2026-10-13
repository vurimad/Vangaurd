/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redMemory/include/sharedPtr.h"
#include "resultBuffer.h"

namespace compression
{
	/// compression type
	enum ECompressionType : Uint8
	{
		// no compression, the compression utils will STILL WORK although this is a waste of memory because both the compress and decompress operation will create a copy of the buffer
		CT_Uncompressed = 0,

		// zlib compression with internal header to store the size of the compressed data (THIS IS MAKING THE DATA INCOMPATBILE WITH NORMAL ZLIB)
		CT_Zlib,

		// raw zlib compression, no special headers. It's compatible with normal zlib but will be slower to decompress.
		CT_ZlibRaw,

		// snappy, nothing special, may be suitable for some types of data
		CT_Snappy,

		// doboz, nothing special, may be suitable for some types of data
		CT_Doboz,

		// LZ4, fast decompression
		CT_LZ4,

		// High-compression LZ4, fast decompression
		CT_LZ4HC,

		// Kraken, fast decompression
		CT_Kraken,

		// High-compression Kraken, fast decompression
		CT_KrakenHC,

		// Last compression type
		CT_MAX,
	};

#if defined(RED_PLATFORM_DURANGO) && defined(USE_PROFILER)
	static const char* compressionTypeNames[ECompressionType::CT_MAX]
	{
		"Uncompressed",
		"Zlib",
		"ZlibRaw",
		"Snappy",
		"Doboz",
		"LZ4",
		"LZ4HC",
		"Kraken",
		"KrakenHC"
	};
#endif

	const ECompressionType c_defaultCompressionType = CT_KrakenHC;
	const ECompressionType c_defaultQuickCompressionType = CT_Kraken;

	extern REDCOMPRESSION_API void InitializeMemoryPools();;

	extern REDCOMPRESSION_API Uint32 GetLZ4CompressBound( Uint32 dataSize );
	extern REDCOMPRESSION_API Uint32 GetLZ4MaxRequiredSize( Uint32 dataSize );

	/// memory allocation for compression/decompression engine
	typedef red::FixedSizeFunction< red::SharedPtr<ResultBuffer>(const red::Uint64 size) > TCompressionAllocator;

	/// Get default allocator for compression buffer, allocates dynamic memory from internal compression pool routed via the default allocator
	/// NOTE: This pool may be constrained a lot on the retail/final builds
	extern REDCOMPRESSION_API TCompressionAllocator GetDefaultCompressionAllocator();

	/// get INPLACE allocator for compression buffer, you need to pass memory pointer and the maximum size of the memory that can be allocated from it
	/// NOTE: the size of the requested allocation cannot be larger than the size of the provided memory (obviously)
	extern REDCOMPRESSION_API TCompressionAllocator GetInplaceCompressionAllocator( void* ptr, const Uint64 maxSize );

	/// generic compression interface
	/// returns valid ResultBuffer if compression was successful or null if it wasn't
	/// all the data is allocated from the provided (can be in place allocator though)
	/// NOTE: empty input buffer creates a VALID empty output buffer
	/// The operation can fail if there's not enough memory or there's internal error in the compressor (very rare).
	/// Source data is never modified
	extern REDCOMPRESSION_API red::SharedPtr<ResultBuffer> CompressData( const ECompressionType ct, const void* data, const red::Uint64 size, TCompressionAllocator allocator );

	/// generic decompression interface
	/// returns valid ResultBuffer if compression was successful or null if it wasn't
	/// all the data is allocated from the provided (can be in place allocator though)
	/// NOTE: empty input buffer creates a VALID empty output buffer
	/// The operation can fail if there's not enough memory, the data buffer is corrupted or there's internal error in the decompresser (very rare).
	/// Source data is never modified
	extern REDCOMPRESSION_API red::SharedPtr<ResultBuffer> DecompressData( const ECompressionType ct, const void* data, const red::Uint64 size, TCompressionAllocator allocator );

	/// Get whether data was compressing using Zlib, LZ4, or Kraken compression (LZ4HC or KrakenHC have the same header as the non-HC versions)
	/// Fatal assert if the buffer is empty or doesn't have valid data - e.g., too small to even have a header.
	/// NOTE: Can't detect uncompressed or raw (headerless) compressions - will likely fatal assert on non-matching magic in header.
	extern REDCOMPRESSION_API ECompressionType GetCompressionTypeFromData(const void* data, const red::Uint64 size);

} // red

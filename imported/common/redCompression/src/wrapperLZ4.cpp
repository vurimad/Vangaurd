/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "wrapperLZ4.h"

#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

namespace compression
{
	namespace lz4
	{
		const static red::Uint32 LZ4_MAGIC = 'XLZ4';

		Uint32 GetMagic()
		{
			return LZ4_MAGIC;
		}

		Uint32 GetCompressBound( Uint32 size )
		{
			return LZ4_compressBound( static_cast< Int32 >( size ) );
		}

		ResultBufferPtr CompressData(const void* data, const red::Uint64 size, TCompressionAllocator allocator)
		{
			RED_FATAL_ASSERT( data != nullptr, "Invalid parameter" );
			RED_FATAL_ASSERT( size != 0, "Invalid parameter" );
			RED_FATAL_ASSERT( size < LZ4_MAX_INPUT_SIZE, "Size of the compression buffer is out of range" );
			// estimate memory needed
			const auto headerSize = 2 * sizeof(red::Uint32);
			const auto maxSpaceRequired = headerSize + LZ4_compressBound( (int)size );

			// Allocate a buffer for writing compressed data to
			auto ret = allocator( maxSpaceRequired );			
			RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data compression (required size: %llu bytes)", maxSpaceRequired );
			if ( ret )
			{
				// compress the data
				const auto compressedSize = LZ4_compress( (const char*)data, (char*) ret->GetData() + headerSize, (int)size );
				RED_ASSERT( compressedSize != 0, "Internal error in LZ4 compression" );
				if ( compressedSize != 0 )
				{
					// write the data header (LZ4 is not tracking size of the uncompressed data by itself)
					auto* headerPtr = (red::Uint32*) ret->GetData();
					headerPtr[0] = LZ4_MAGIC;
					headerPtr[1] = static_cast< Uint32 >( size );

					// patch size and return the buffer
					const auto totalSize = headerSize + compressedSize;
					ret->PatchDataSize( totalSize );
					return ret;
				}
			}

			// Error condition, in case memory was allocated it will be freed
			return ResultBufferPtr();
		}

		ResultBufferPtr CompressDataHC( const void* data, const red::Uint64 size, TCompressionAllocator allocator )
		{
			RED_FATAL_ASSERT( data != nullptr, "Invalid parameter" );
			RED_FATAL_ASSERT( size != 0, "Invalid parameter" );
			RED_FATAL_ASSERT( size < LZ4_MAX_INPUT_SIZE, "Size of the compression buffer is out of range" );
			// estimate memory needed
			const auto headerSize = 2 * sizeof(red::Uint32);
			const auto maxSpaceRequired = headerSize + LZ4_compressBound( (int)size );

			// Allocate a buffer for writing compressed data to
			auto ret = allocator( maxSpaceRequired );			
			RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data compression (required size: %llu bytes)", maxSpaceRequired );
			if ( ret )
			{
				void* lzState = RED_ALLOCA( LZ4_sizeofStateHC() );

				// compress the data
				const auto compressedSize = LZ4_compressHC_withStateHC( lzState, (const char*)data, (char*) ret->GetData() + headerSize, (int)size );
				RED_ASSERT( compressedSize != 0, "Internal error in LZ4HC compression" );
				if ( compressedSize != 0 )
				{
					// write the data header (LZ4 is not tracking size of the uncompressed data by itself)
					auto* headerPtr = (red::Uint32*) ret->GetData();
					headerPtr[0] = LZ4_MAGIC;
					headerPtr[1] = static_cast< Uint32 >( size );

					// patch size and return the buffer
					const auto totalSize = headerSize + compressedSize;
					ret->PatchDataSize( totalSize );
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

			// we need at least the header :)
			const auto headerSize = 2 * sizeof(red::Uint32);
			if ( size >= headerSize )
			{
				// validate header
				const auto* headerPtr = (const red::Uint32*) data;
				if ( headerPtr[0] == LZ4_MAGIC )
				{
					// make sure we have enough data in the incoming buffer
					const auto decompressedSize = headerPtr[1];

					// allocate output buffer
					auto ret = allocator( decompressedSize );
					RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data decompression (required size: %llu bytes)", decompressedSize );

					// decompress
					if ( ret != nullptr )
					{
						const auto compressedSize = size - headerSize;
						const auto compressedBytesConsumed = LZ4_decompress_fast( (const char*)data + headerSize, (char*)ret->GetData(), (int)decompressedSize );
						RED_ASSERT( compressedBytesConsumed == compressedSize, "Interal LZ4 decompression error, code: %d", compressedBytesConsumed );
						if ( compressedBytesConsumed == compressedSize )
						{
							return ret;
						}
					}
				}
			}

			// no data decompressed
			return ResultBufferPtr();
		}

	} // lz4

} // red


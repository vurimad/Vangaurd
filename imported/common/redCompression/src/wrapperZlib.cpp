/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "wrapperDoboz.h"

#define ZLIB_CONST
#include "zlib/zlib.h"

namespace compression
{
	// internal allocator
	extern void* InternalAlloc( const Uint32 size );
	extern void InternalFree( void* ptr );

	namespace zlib
	{
		// allocator for the ZLib-internals
		namespace prv
		{
			const Uint32 ZLIB_HEADER_SIZE = sizeof(Uint32)*2;
			const Uint32 ZLIB_MAGIC = 'ZLIB';

			static voidpf ZlibAlloc( voidpf, uInt items, uInt size )
			{
				return InternalAlloc( size * items );
			}

			static void ZlibFree( voidpf, voidpf address )
			{
				InternalFree( address );
			}
		} // prv

		Uint32 GetMagic()
		{
			return prv::ZLIB_MAGIC;
		}

		ResultBufferPtr CompressData(const void* data, const red::Uint64 size, TCompressionAllocator allocator, const Bool addSizeHeader)
		{
			RED_FATAL_ASSERT( data != nullptr, "Invalid parameter" );
			RED_FATAL_ASSERT( size != 0, "Invalid parameter" );
			RED_FATAL_ASSERT( size <= 0xFFFFFFFF, "Zlib supports only 32-bit data size" );

			z_stream zstr;
			red::Memzero( &zstr, sizeof(zstr) );

			zstr.next_in = static_cast< z_const Bytef* >( data );
			zstr.avail_in = (Uint32) size;
			zstr.zalloc = &prv::ZlibAlloc;
			zstr.zfree = &prv::ZlibFree;

			// start compression
			const auto initRet = deflateInit( &zstr, Z_BEST_COMPRESSION );
			RED_ASSERT( initRet == Z_OK, "Failed to initialize z-lib deflate" );
			if ( initRet == Z_OK )
			{
				// estimate the size of output memory
				const auto neededSize = deflateBound( &zstr, (Uint32)size );

				// allocate the output buffer of at least equal size as the input
				auto ret = allocator( neededSize + prv::ZLIB_HEADER_SIZE );
				RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data compression (required size: %llu bytes)", neededSize );
				if ( ret )
				{
					zstr.next_out = static_cast< Bytef* >( const_cast< void* >( ret->GetData() ) ) + (addSizeHeader ? prv::ZLIB_HEADER_SIZE : 0);
					zstr.avail_out = neededSize;

					// single step DEFLATE
					auto result = deflate( &zstr, Z_FINISH );
					RED_ASSERT( result == Z_STREAM_END, "Failed to finish z-lib compression in one step, more memory is needed than what deflateBound returned" );
					if ( result == Z_STREAM_END )
					{
						// write the special header
						if ( addSizeHeader )
						{
							red::Uint32* headerPtr = (red::Uint32*) ret->GetData();
							headerPtr[0] = prv::ZLIB_MAGIC;
							headerPtr[1] = static_cast<Uint32>( size );
						}
						ret->PatchDataSize( zstr.total_out + (addSizeHeader ? prv::ZLIB_HEADER_SIZE : 0) );

						deflateEnd( &zstr );
						return ret;
					}
				}

				// failed to compress but it was still initialized
				deflateEnd( &zstr );
			}

			// no data compressed
			return ResultBufferPtr();
		}

		static const red::Uint32 MeasureSizeOfDecompressedData( const void* data, const red::Uint64 size, Uint8* buffer, const Uint32 bufferSize )
		{
			z_stream zstr;
			red::Memzero( &zstr, sizeof(zstr) );

			zstr.next_in = static_cast< z_const Bytef* >( data );
			zstr.avail_in = (red::Uint32) size;

			red::Uint32 totalSize = 0;

			const auto initRet = inflateInit( &zstr );
			if ( initRet == Z_OK )
			{
				while ( 1 )
				{
					zstr.next_out = buffer;
					zstr.avail_out = bufferSize;

					auto result = inflate( &zstr, Z_NO_FLUSH );
					if ( result == Z_DATA_ERROR || result == Z_MEM_ERROR || result == Z_NEED_DICT )
						return 0;
					
					const auto have = bufferSize - zstr.avail_out;
					totalSize += have;

					if ( result == Z_STREAM_END )
						break;
				}

				inflateEnd( &zstr );
				return totalSize;
			}

			// size unknown
			return 0;
		}

		static const red::Uint32 GetSizeOfDecompressedData( const void* data, const red::Uint64 size, Uint8* buffer, const Uint32 bufferSize )
		{
			// if we have the special header with the size use it to get the exact size of the output buffer
			if ( size >= prv::ZLIB_HEADER_SIZE )
			{
				const auto* specialHeader = (const red::Uint32*) data;

				if ( specialHeader[0] == prv::ZLIB_MAGIC )
					return specialHeader[1];
			}

			// size not provided directly, calculate via dry decompression
			return MeasureSizeOfDecompressedData( data, size, buffer, bufferSize );
		}


		ResultBufferPtr DecompressData( const void* data, const red::Uint64 size, TCompressionAllocator allocator )
		{
			RED_FATAL_ASSERT( data != nullptr, "Invalid parameter" );
			RED_FATAL_ASSERT( size != 0, "Invalid parameter" );
			RED_FATAL_ASSERT( size <= 0xFFFFFFFF, "Zlib supports only 32-bit data size" );

			red::Uint8 tempBuffer[ 16 << 10 ]; // 16K - the chunk size

			// calculate/extract the size of the output buffer
			const auto outputSize = GetSizeOfDecompressedData( data, size, tempBuffer, sizeof(tempBuffer) );
			RED_ASSERT( outputSize != 0, "Unable to get size of decompression buffer" );
			if ( outputSize != 0 )
			{
				const auto* specialHeader = (const red::Uint32*) data;
				const Uint32 dataOffset = ( size >= prv::ZLIB_HEADER_SIZE && specialHeader[0] == prv::ZLIB_MAGIC ) ? prv::ZLIB_HEADER_SIZE : 0;

				// allocate output buffer
				auto ret = allocator( outputSize );
				RED_ASSERT( ret != nullptr, "Out of memory when allocating buffer for data decompression (required size: %d bytes)", outputSize );
				if ( ret != nullptr )
				{
					z_stream zstr;
					red::Memzero( &zstr, sizeof(zstr) );

					zstr.next_in = static_cast< z_const Bytef* >( static_cast<const Uint8*>( data ) + dataOffset );
					zstr.avail_in = (Uint32) size;
					zstr.zalloc = &prv::ZlibAlloc;
					zstr.zfree = &prv::ZlibFree;

					// initialize
					const auto initRet = inflateInit( &zstr );
					RED_ASSERT( initRet == Z_OK, "Failed to initialize z-lib inflate" );
					if ( initRet == Z_OK )
					{

						// decompress in chunk-size blocks
						red::Uint32 writePos = 0;
						while ( 1 )
						{
							zstr.next_out = tempBuffer;
							zstr.avail_out = sizeof(tempBuffer);

							auto result = inflate( &zstr, Z_NO_FLUSH );
							if ( result == Z_DATA_ERROR || result == Z_MEM_ERROR || result == Z_NEED_DICT )
								break; // failed

							const auto have = sizeof(tempBuffer) - zstr.avail_out;							
							RED_FATAL_ASSERT( writePos + have <= outputSize, "Allocated output buffer too small!" );
							red::Memcpy( (red::Uint8*)ret->GetData() + writePos, tempBuffer, have );
							writePos += (Uint32) have;

							// end of data
							if ( result == Z_STREAM_END )
							{
								inflateEnd( &zstr );
								return ret;
							}
						}

						// failed but the stream was still initailized, close it so we won't leak stuff
						inflateEnd( &zstr );
					}
				}
			}

			// nothing decompressed
			return ResultBufferPtr();
		}

	} // zlib

} // compression



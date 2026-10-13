#include "build.h"
#include "resultBuffer.h"

// internal wrappers for each third-party compression formats, NOT PUBLIC
#include "wrapperDoboz.h"
#include "wrapperZlib.h"
#include "wrapperSnappy.h"
#include "wrapperLZ4.h"
#include "wrapperNoCompression.h"
#include "wrapperKraken.h"

// internal compression pool, visible only here
RED_MEMORY_POOL_STATIC( PoolCompression, red::memory::DefaultAllocator );

namespace compression
{

	//-----------------------------------
	void InitializeMemoryPools()
	{
		RED_INITIALIZE_MEMORY_POOL( PoolCompression, red::memory::PoolCPU, red::memory::AcquireDefaultAllocator(), RED_KILO_BYTE( 1 ) );
	}

	// internal use only
	void* InternalAlloc( const Uint32 size )
	{
		return RED_ALLOCATE( PoolCompression, size );
	}

	// internal use only
	void InternalFree( void* ptr )
	{
		RED_FREE( PoolCompression, ptr );
	}

	//-----------------------------------

	Uint32 GetLZ4CompressBound( Uint32 dataSize )
	{
		return lz4::GetCompressBound( dataSize );
	}

	Uint32 GetLZ4MaxRequiredSize( Uint32 dataSize )
	{
		return 2 * sizeof( Uint32 ) + lz4::GetCompressBound( dataSize );
	}

	/// This defines a function that will allocate memory from internal Compression pool which is usueful as a default.
	/// NOTE: This pool may be constrained a lot on the retail/final builds
	TCompressionAllocator GetDefaultCompressionAllocator()
	{
		return [](const red::Uint64 size)
		{
			ResultBufferPtr ret;

			void* mem = RED_ALLOCATE( PoolCompression, size ); // TODO: add "no OOM" allocation flag
			if ( nullptr != mem )
			{
				ret.Reset( RED_NEW(ResultBuffer)( mem, size, [](void* ptr) { RED_FREE( PoolCompression, ptr ); } ) );
			}

			return ret;
		};
	}

	/// This defines a special pass-through allocator that will NOT allocate any memory but just use the provided pointer
	/// NOTE: the size of the requested allocation cannot be larger than the size of the provided memory (obviously)
	TCompressionAllocator GetInplaceCompressionAllocator( void* inplaceMem, const Uint64 maxSize )
	{
		return [inplaceMem, maxSize](const red::Uint64 size)
		{
			ResultBufferPtr ret;

			if ( (nullptr != inplaceMem) && (size <= maxSize) )
			{
				ret.Reset( RED_NEW(ResultBuffer) ( inplaceMem, size, [](void*) { /* inpalce memory is not freed*/ } ) );
			}

			return ret;
		};
	}

	//-----------------------------------

	red::SharedPtr<ResultBuffer> CompressData( const ECompressionType ct, const void* data, const red::Uint64 size, TCompressionAllocator allocator )
	{
		// handle empty buffer case regardless of compression
		// NOTE: this is not very optimal but makes the interface support all corner cases nicely
		if ( !data || !size )
			return ResultBufferPtr( RED_NEW(ResultBuffer) );

		// demux by type (let's not bother to make this automatic, we only have a few options here)
		switch ( ct )
		{
			case CT_Uncompressed:
				return none::CompressData( data, size, allocator );

			case CT_Doboz: 
				return doboz::CompressData( data, size, allocator );

			case CT_Zlib:
				return zlib::CompressData( data, size, allocator, true  /* size header */ );

			case CT_ZlibRaw:
				return zlib::CompressData( data, size, allocator, false /* size header */ );

			case CT_Snappy:
				return snappy::CompressData( data, size, allocator );

			case CT_LZ4:
				return lz4::CompressData( data, size, allocator );

			case CT_LZ4HC:
				return lz4::CompressDataHC( data, size, allocator );

			case CT_Kraken:
				return kraken::CompressData( data, size, allocator );

			case CT_KrakenHC:
				return kraken::CompressDataHC(data, size, allocator);

			case CT_MAX:
				break;
		}

		// shouldn't happen
		RED_FATAL( "Invalid compression type: %d", (int)ct );
		return ResultBufferPtr();
	}

	red::SharedPtr<ResultBuffer> DecompressData( const ECompressionType ct, const void* data, const red::Uint64 size, TCompressionAllocator allocator )
	{
		// empty buffer case
		// NOTE: this is not very optimal but makes the interface support all corner cases nicely
		if ( !data || !size )
			return ResultBufferPtr( RED_NEW(ResultBuffer) );

		// demux by type (let's not bother to make this automatic, we only have a few options here)
		switch ( ct )
		{
			case CT_Uncompressed:
				return none::DecompressData( data, size, allocator );

			case CT_Doboz: 
				return doboz::DecompressData( data, size, allocator );

			case CT_Zlib:
				return zlib::DecompressData( data, size, allocator );

			case CT_ZlibRaw:
				return zlib::DecompressData( data, size, allocator );

			case CT_Snappy:
				return snappy::DecompressData( data, size, allocator );

			case CT_LZ4:
			case CT_LZ4HC:
				return lz4::DecompressData( data, size, allocator );
				
			case CT_Kraken:
			case CT_KrakenHC:
				return kraken::DecompressData(data, size, allocator);

			case CT_MAX:
				break;
		}

		// shouldn't happen
		RED_FATAL( "Invalid compression type: %d", (int)ct );
		return ResultBufferPtr();
	}

	extern REDCOMPRESSION_API ECompressionType GetCompressionTypeFromData(const void* data, const red::Uint64 size)
	{
		const auto headerSize = 2 * sizeof(red::Uint32);
		RED_FATAL_ASSERT(size >= headerSize);
		const auto* headerPtr = (const red::Uint32*)data;
		const Uint32 magic = headerPtr[0];
		
		if (magic == kraken::GetMagic())
		{
			return CT_Kraken;
		}
		
		if (magic == lz4::GetMagic())
		{
			return CT_LZ4;
		}
		
		if (magic == zlib::GetMagic())
		{
			return CT_Zlib;
		}

		RED_FATAL("Unexpected magic: %u, fileSize=%llu", magic, size);
		return CT_MAX;
	}

	} // red
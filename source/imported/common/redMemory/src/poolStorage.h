/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_POOL_STORAGE_H_
#define _RED_MEMORY_POOL_STORAGE_H_

#include "../include/poolTypes.h"
#include "../include/proxyTypeId.h"
#include "../include/block.h"

namespace red
{
namespace memory
{
	class PoolOOMHandler;

	struct PoolStorage
	{
		/**
		 * The allocator storage.
		 *
		 * We use the fact that allocators are always aligned in memory to at least 8 byte and use
		 * first 3 bits to store additional flags.
		 *
		 * Contains pointer to the pool allocator and additional flags (c_AllocatorPointerFlag_*) stored in the empty bits.
		 * From least significant bit:
		 * 0 - isUsingDebugAllocator flag
		 * 1 - shouldNeverUseDebugAllocator flag
		 * 2 - unused flag
		 * 3-64 - pointer
		 *
		 * If you will be ever in need to get pointer from this use red::memory::DecodeAllocatorPointer(u64)
		 */
		u64 allocatorStorage;
		atomic::TAtomic64 bytesAllocated;
		atomic::TAtomic64 maxBytesAllocated;
		PoolOOMHandler * oomHandler;
		PoolHandle handle;
		ProxyTypeId allocatorId;
	};

	static_assert( sizeof( PoolStorage ) == 40, "PoolStorage size must be 32." );

	template< typename PoolType >
	struct PoolStorageProxy
	{
		typedef typename PoolType::AllocatorType AllocatorType;

		static Block Allocate( u32 size );
		static Block AllocateAligned( u32 size, u32 alignment );
		static Block Reallocate( Block & block, u32 size );
		static Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		static void Free( Block & block );
		static u64 GetBlockSize( u64 address );

		static AllocatorType & GetAllocator();
		static Bool IsUsingDebugAllocator();
		static PoolHandle GetHandle();
		static u64 GetTotalBytesAllocated();
		static void ResetTotalBytesAllocated();
		static u64 GetFlags();
		static void EnableDebugAllocator();
		static void ForceNoDebugAllocator();

		static void SetAllocator( AllocatorType & allocator );
		static void SetOutOfMemoryHandler( PoolOOMHandler * handler );
	};
}
}

#include "poolStorage.hpp"

#endif

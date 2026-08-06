/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_DEBUG_ALLOCATOR_H_
#define _RED_MEMORY_DEBUG_ALLOCATOR_H_

#include "allocator.h"
#include "../include/block.h"

namespace red
{
namespace memory
{
	struct DebugAllocatorMetrics {};

	
	class RED_MEMORY_API DebugAllocator
	{
	public:

		RED_MEMORY_DECLARE_ALLOCATOR( DebugAllocator, DebugAllocatorMetrics, 16 );

		DebugAllocator();
		~DebugAllocator();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		void Free( Block & block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

	private:

		DebugAllocator( const DebugAllocator& );
		DebugAllocator & operator=( const DebugAllocator& );

		// Aligned to 8 as we store additional flags with pointer in PoolStorage::allocatorStorage.
		RED_ALIGN( 8 ) char padding[ 8 ];
	};

	RED_MEMORY_API DebugAllocator & AcquireDebugAllocator();
}
}

#endif

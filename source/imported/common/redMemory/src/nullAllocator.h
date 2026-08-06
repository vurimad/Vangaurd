/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_NULL_ALLOCATOR_H_
#define _RED_MEMORY_NULL_ALLOCATOR_H_

#include "allocator.h"
#include "../include/block.h"

namespace red
{
namespace memory
{
	struct NullAllocatorMetrics {};

	class RED_MEMORY_API NullAllocator
	{
	public:

		RED_MEMORY_DECLARE_ALLOCATOR( NullAllocator, NullAllocatorMetrics, 1 );

		NullAllocator();
		~NullAllocator();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block & block );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

	private:

		NullAllocator( const NullAllocator& );
		NullAllocator & operator=( const NullAllocator& );

		// Aligned to 8 as we store additional flags with pointer in PoolStorage::allocatorStorage.
		RED_ALIGN( 8 ) char padding[ 8 ];
	};

	RED_MEMORY_API NullAllocator & AcquireNullAllocator();
}
}

#endif

/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef RED_MEMORY_STATIC_BUDDY_ALLOCATOR_H_
#define RED_MEMORY_STATIC_BUDDY_ALLOCATOR_H_

#include "buddyAllocator.h"

namespace red
{
namespace memory
{

	struct StaticBuddyAllocatorParameter
	{
		SystemAllocator* systemAllocator;
		u32 bufferSize;
		u32 alignment;
		u32 flags;
	};

	class RED_MEMORY_API StaticBuddyAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( StaticBuddyAllocator, BuddyAllocatorMetrics, 16 );

		StaticBuddyAllocator();
		~StaticBuddyAllocator();

		void Initialize( const StaticBuddyAllocatorParameter& parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );
		void Free( Block& block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 block ) const;

	private:
		BuddyAllocator m_allocator;
	};

}
}

#endif
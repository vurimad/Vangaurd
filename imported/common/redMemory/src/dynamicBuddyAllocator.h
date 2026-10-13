/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef RED_MEMORY_DYNAMIC_BUDDY_ALLOCATOR_H_
#define RED_MEMORY_DYNAMIC_BUDDY_ALLOCATOR_H_

#include "buddyAllocator.h"

namespace red
{
namespace memory
{

	struct DynamicBuddyAllocatorParameter
	{
		SystemAllocator* systemAllocator;
		u32 maxBufferSize;
		u32 initBufferSize;
		u32 alignment;
		u32 flags;
	};

	class RED_MEMORY_API DynamicBuddyAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( DynamicBuddyAllocator, BuddyAllocatorMetrics, 16 );

		DynamicBuddyAllocator();
		~DynamicBuddyAllocator();

		void Initialize( const DynamicBuddyAllocatorParameter& parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );
		void Free( Block& block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 block ) const;

		u32 GetSystemMemoryUsage() const;
		void ShrinkBuffer();

		bool IsInitialized() const { return m_allocator.IsInitialized(); }

	private:
		BuddyAllocator m_allocator;
	};

}
}

#endif
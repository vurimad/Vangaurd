/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef RED_MEMORY_DYNAMIC_BUDDY_ALLOCATOR_H_
#define RED_MEMORY_LOCKING_DYNAMIC_BUDDY_ALLOCATOR_H_

#include "dynamicBuddyAllocator.h"
#include "../../redSystem/include/readWriteSpinLock.h"

namespace red
{
namespace memory
{
	class RED_MEMORY_API LockingDynamicBuddyAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( LockingDynamicBuddyAllocator, BuddyAllocatorMetrics, 16 );

		LockingDynamicBuddyAllocator();
		~LockingDynamicBuddyAllocator();

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
		using LockPrimitive = RWSpinLock;
		DynamicBuddyAllocator m_allocator;
		mutable LockPrimitive m_lock;
	};

}
}

#endif
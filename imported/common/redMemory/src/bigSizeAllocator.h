/**
 * Copyright © 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_BIG_SIZE_ALLOCATOR_H_
#define _RED_MEMORY_BIG_SIZE_ALLOCATOR_H_

#include "../include/utils.h"
#include "allocator.h"
#include "allocatorMetrics.h"
#include "../include/virtualRange.h"
#include "../include/systemBlock.h"
#include "spinLock.h"

namespace red
{
namespace memory
{
	class SystemAllocator;
	class Serializer;

	struct BigSizeAllocatorMetrics
	{
		AllocatorMetrics metrics;
		u64 virtualRangeSize;
		u32 allocationListCount;
		u32 allocationListMaxCount;
		u32 freeListCount;
		u32 freeListMaxCount;
		u64 freeListSize;
	};

	struct RED_MEMORY_API BigSizeAllocatorParameter
	{
		SystemAllocator * systemAllocator;
		u64 virtualRangeSize;
		u32 flags;
	};

	class RED_MEMORY_API BigSizeAllocator : NonCopyable
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( BigSizeAllocator, BigSizeAllocatorMetrics, 16 );

		BigSizeAllocator();
		~BigSizeAllocator();

		void Initialize( const BigSizeAllocatorParameter & parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block & block );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 block ) const;

		void BuildMetrics( BigSizeAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );

		u32 GetPageSize() const;

		// UNIT TEST ONLY
		void InternalMarkBigSizeAllocatorAsFull();

	private:

		SystemBlock AllocateBlock( u32 size, u32 alignment );
		void DeallocateBlock( Block & block );
		VirtualRange ReserveRange( u32 size, u32 alignment );
		void ReleaseRange( const VirtualRange & range );

		VirtualRange m_virtualRange;
		VirtualRange m_bookkeepingListVirtualRange;

		SystemAllocator * m_systemAllocator;

		u32 m_pageSize;
		u32 m_flags;

		mutable SpinLock m_lock;

		Block * m_allocationList;
		u32 m_allocationListCount;
		u32 m_allocationListMaxCount;

		VirtualRange * m_freeList;
		u32 m_freeListCount;
		u32 m_freeListMaxCount;

		bool m_allocatorIsFull;

		u8 m_padding[39];
	};

	static_assert( sizeof( BigSizeAllocator ) == 128, "Big size allocator has to fit in two cache lines" );

	RED_MEMORY_API BigSizeAllocator& AcquireBigSizeAllocator();
}
}

#endif
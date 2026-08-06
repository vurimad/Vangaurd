/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef RED_MEMORY_BUDDY_ALLOCATOR_H_
#define RED_MEMORY_BUDDY_ALLOCATOR_H_

#include "allocator.h"
#include "allocatorMetrics.h"
#include "../include/block.h"
#include "../include/virtualRange.h"
#include <vector>

namespace red
{
namespace memory
{
	class SystemAllocator;
	class Serializer;
	struct BuddyBlockInfo;

	struct BuddyAllocatorMetrics
	{};

	struct BuddyAllocatorParamater
	{
		SystemAllocator* systemAllocator;
		u32 maxBufferSize;
		u32 initBufferSize;
		u32 alignment; // constant for all allocations (it is also the minimum size for the block)
		u32 flags;
	};

	// Optimization ideas taken from: http://bitsquid.blogspot.com/2015/08/allocation-adventures-3-buddy-allocator.html

	class RED_MEMORY_API BuddyAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( BuddyAllocator, BuddyAllocatorMetrics, 16 );

		BuddyAllocator();
		~BuddyAllocator();

		void Initialize( const BuddyAllocatorParamater& parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );
		void Free( Block& block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		bool IsInitialized() const { return m_systemAllocator != nullptr; }

		u32 GetAlignment() const { return m_minAllocation; }
		u32 GetSystemMemoryUsage() const;

		bool CanGrowBuffer( u32 size ) const;
		bool GrowBlock( u32 size );
		u32 ShrinkBuffer();

	private:
		u32 SizeToLevel( u32 size ) const;
		u32 IndexOf( const void* ptr, u32 level ) const;
		u32 FreeIndex( u32 index ) const;
		u32 SplitIndex( u32 index ) const;
		BuddyBlockInfo* ToBuddy( u64 ptr, u32 level ) const;

		Block BuddyAllocFromLevel( i32 level );
		void BuddyReleaseAtLevel( void* ptr, i32 level );

		SystemAllocator* m_systemAllocator;
		VirtualRange m_bufferVirtualRange;
		VirtualRange m_metadataVirtualRange;

		void* m_buffer;
		u64 m_nextVirtualAddress;

		BuddyBlockInfo* m_freeBlocks;
		u32* m_blockIndex;

		u32 m_size;
		u32 m_maxSize;

		u32 m_minAllocation;

		u32 m_maxIndexes;
		u32 m_totalLevels;
		u32 m_maxLevel;
		u32 m_currentMaxLevel;

		u32 m_flags;

		bool m_firstAllocation;
	};
}
}

#endif
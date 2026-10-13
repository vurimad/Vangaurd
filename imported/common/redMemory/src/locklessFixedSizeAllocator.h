/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_LOCKLESS_FIXED_SIZE_ALLOCATOR_H_
#define _RED_MEMORY_LOCKLESS_FIXED_SIZE_ALLOCATOR_H_

#include "allocatorMetrics.h"
#include "../include/virtualRange.h"
#include "../include/systemBlock.h"
#include "../include/block.h"

namespace red
{
namespace memory
{
	class Serializer;

	struct LocklessFixedSizeAllocatorParameter
	{
		u32 blockSize;
		u32 blockAlignment;
		VirtualRange range;
		SystemBlock block;
	};

	struct FixedSizeAllocatorMetrics
	{
		AllocatorMetrics metrics;
	};

	class RED_MEMORY_API LocklessFixedSizeAllocator
	{
	public:

		LocklessFixedSizeAllocator();
		~LocklessFixedSizeAllocator();

		void Initialize( const LocklessFixedSizeAllocatorParameter & param );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block & block );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );

		bool OwnBlock( u64 block ) const;

		u32 GetBlockSize() const;
		u32 GetTotalBlockCount() const;

		void UpdateEndAddress( u64 endAddress );

		void BuildMetrics( FixedSizeAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );

	private:

		LocklessFixedSizeAllocator( const LocklessFixedSizeAllocator & );
		LocklessFixedSizeAllocator & operator=( const LocklessFixedSizeAllocator & );

		bool IsInitialized() const;

		atomic::TAtomic64 m_firstFree;
		u64 m_endAddress;
		u32 m_blockSize;
		u32 m_blockAlignment;
		VirtualRange m_range;
	};
}
}

#endif

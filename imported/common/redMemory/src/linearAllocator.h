/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_LINEAR_ALLOCATOR_H_
#define _RED_MEMORY_LINEAR_ALLOCATOR_H_

#include "allocatorMetrics.h"
#include "allocator.h"
#include "../include/block.h"
#include "../include/virtualRange.h"
#include "../include/systemBlock.h"

namespace red
{
namespace memory
{
	class Serializer;

	struct LinearAllocatorParameter
	{
		SystemBlock block;
	};

	struct LinearAllocatorMetrics
	{
		AllocatorMetrics metrics;
	};

	class RED_MEMORY_API LinearAllocator : NonCopyable
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( LinearAllocator, LinearAllocatorMetrics, 4 );

		LinearAllocator();
		~LinearAllocator();

		void Initialize( const LinearAllocatorParameter& param );

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block& block );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void Reset();
		void UpdateBuffer( const Block& block );

		void BuildMetrics( LinearAllocatorMetrics& metrics );
		void SerializeMetrics( Serializer& serializer );

	private:
		Block GrowBlock( const Block & block, u32 size );
		
		// start address of the buffer
		u64 m_startAddress;
		u64 m_endAddress;
		atomic::TAtomic64 m_position;
	};
}
}

#endif

/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_STACK_ALLOCATOR_H_
#define _RED_MEMORY_STACK_ALLOCATOR_H_

#include "../../redSystem/include/utility.h"
#include "../include/systemBlock.h"
#include "../include/block.h"
#include "allocatorMetrics.h"
#include "allocator.h"

namespace red
{
namespace memory
{
	class Serializer;

	struct RED_MEMORY_API StackAllocatorParameter
	{
		Block block;
		u32 defaultAlignment;
	};

	struct StackAllocatorMetrics
	{
		AllocatorMetrics metrics;
	};

	class RED_MEMORY_API StackAllocator : NonCopyable
	{
	public:

		StackAllocator();

		void Initialize( const StackAllocatorParameter& parameter );

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block& block );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );
		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 block ) const;

		void Reset();

		void UpdateBuffer( const Block& block );

		void BuildMetrics( StackAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );

	private:

		bool IsInitialized() const;

		// start address of the buffer
		u64 m_start;
		// size of the buffer
		u64 m_size;
		// current size of the buffer
		u32 m_offset;
		// default alignment
		u32 m_defaultAlignment;
	};

}
}

#endif
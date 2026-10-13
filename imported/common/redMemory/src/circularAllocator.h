/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_CIRCULAR_ALLOCATOR_H_
#define _RED_MEMORY_CIRCULAR_ALLOCATOR_H_

#include "allocatorMetrics.h"
#include "allocator.h"
#include "../include/block.h"
#include "../include/systemBlock.h"

namespace red
{
namespace memory
{
	class Serializer;

	const u8 c_circularAllocatorUnitTestAllocFiller = 0xa6;
	const u8 c_circularAllocatorUnitTestFreeFiller = 0xf6;

	struct CircularAllocatorParameter
	{
		SystemBlock block;
	};

	struct CircularAllocatorMetrics
	{
		AllocatorMetrics metrics;
	};

	class RED_MEMORY_API CircularAllocator : NonCopyable
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( CircularAllocator, CircularAllocatorMetrics, 4 );

		CircularAllocator();
		~CircularAllocator();

		void Initialize( const CircularAllocatorParameter& param );

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block& block );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );

		Bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void BuildMetrics( CircularAllocatorMetrics& metrics );
		void SerializeMetrics( Serializer& serializer );

	private:
		Block GrowBlock( const Block& block, u32 size );

		Uint64 m_startAddress;
		Uint64 m_endAddress;
		Atomic< u64 > m_position;
	};
}
}

#endif
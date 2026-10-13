/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_STATIC_STACK_ALLOCATOR_H_
#define _RED_MEMORY_STATIC_STACK_ALLOCATOR_H_

#include "../include/utils.h"
#include "allocator.h"
#include "stackAllocator.h"
#include "../../redSystem/include/utility.h"

namespace red
{
namespace memory
{
	class Serializer;

	struct RED_MEMORY_API StaticStackAllocatorParameter
	{
		void* buffer;
		u32 bufferSize;
	};

	class RED_MEMORY_API StaticStackAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( StaticStackAllocator, StackAllocatorMetrics, 8 );

		StaticStackAllocator();

		void Initialize( const StaticStackAllocatorParameter& parameter );

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block& block );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );

		bool OwnBlock( u64 block) const;
		u64 GetBlockSize( u64 block ) const;

		void Reset();

		void BuildMetrics( StackAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );

	private:

		RED_ALIGN( 64 ) StackAllocator m_allocator;
#if !defined( RED_CONFIGURATION_FINAL )
		bool m_initialized;
#endif
	};

	RED_STATIC_ASSERT( sizeof( StaticStackAllocator ) == 64 );
}
}

#endif
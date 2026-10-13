/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_LOCKLESS_STATIC_LINEAR_ALLOCATOR_H_
#define _RED_MEMORY_LOCKLESS_STATIC_LINEAR_ALLOCATOR_H_

#include "allocator.h"
#include "linearAllocator.h"

namespace red
{
namespace memory
{
	class Serializer;

	struct StaticLinearAllocatorParameter
	{
		void* buffer;
		u32 bufferSize;
	};

	class RED_MEMORY_API LocklessStaticLinearAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( LocklessStaticLinearAllocator, LinearAllocatorMetrics, LinearAllocator::DefaultAlignmentType::value );

		LocklessStaticLinearAllocator();

		void Initialize( const StaticLinearAllocatorParameter& parameter );

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block& block );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );

		Bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void Reset();

		void BuildMetrics( LinearAllocatorMetrics& metrics );
		void SerializeMetrics( Serializer& serializer );

	private:
		RED_ALIGN( 64 ) LinearAllocator m_allocator;
#if !defined( RED_CONFIGURATION_FINAL )
		bool m_initialized;
#endif
	};

	RED_STATIC_ASSERT( sizeof( LocklessStaticLinearAllocator ) == 64 );
}
}

#endif

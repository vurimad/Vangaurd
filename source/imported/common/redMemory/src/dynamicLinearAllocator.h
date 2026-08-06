/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_DYNAMIC_LINEAR_ALLOCATOR_H_
#define _RED_MEMORY_DYNAMIC_LINEAR_ALLOCATOR_H_

#include "allocator.h"
#include "linearAllocator.h"
#include "../include/virtualRange.h"

namespace red
{
namespace memory
{
	class Serializer;
	class SystemAllocator;

	struct DynamicLinearAllocatorParameter
	{
		SystemAllocator* systemAllocator;
		u32 chunkSize;
		u32 flags;
	};

	class RED_MEMORY_API DynamicLinearAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( DynamicLinearAllocator, LinearAllocatorMetrics, LinearAllocator::DefaultAlignmentType::value );

		DynamicLinearAllocator();
		~DynamicLinearAllocator();

		void Initialize( const DynamicLinearAllocatorParameter& parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block& block );
		Block Reallocate( Block& block, u32 size );
		Block ReallocateAligned( Block& block, u32 size, u32 alignment );

		Bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;
		u32 GetChunkSize() const;

		void Reset();

		void Reserve( u32 size );

		void BuildMetrics( LinearAllocatorMetrics& metrics );
		void SerializeMetrics( Serializer& serializer );

	private:
		bool IsInitialized() const;
		SystemBlock AllocateBlock( u32 size );
		bool CanCreateMoreBlock() const;

		RED_ALIGN( 64 ) LinearAllocator m_allocator;
		VirtualRange m_virtualRange;
		u64 m_nextVirtualAddress;
		u64 m_minimalAllocationSize;
		SystemAllocator* m_systemAllocator;
		u32 m_chunkSize;
		u32 m_flags;
	};

	RED_STATIC_ASSERT( sizeof( DynamicLinearAllocator ) == 128 );
}
}

#endif
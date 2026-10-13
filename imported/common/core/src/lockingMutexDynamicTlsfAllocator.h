/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../redMemory/include/block.h"
#include "../../redMemory/include/types.h"

namespace red
{
	class LockingMutexDynamicTLSFAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( LockingMutexDynamicTLSFAllocator, red::memory::TLSFAllocatorMetrics, 16 );

		LockingMutexDynamicTLSFAllocator();
		~LockingMutexDynamicTLSFAllocator();

		void Initialize( const red::memory::DynamicTLSFAllocatorParameter & parameter );
		void Uninitialize();	

		red::memory::Block Allocate( red::memory::u32 size );
		red::memory::Block AllocateAligned( red::memory::u32 size, red::memory::u32 alignment );
		red::memory::Block Reallocate( red::memory::Block & block, red::memory::u32 size );
		red::memory::Block ReallocateAligned( red::memory::Block & block, red::memory::u32 size, red::memory::u32 alignment );
		void Free( red::memory::Block & block );

		bool OwnBlock( red::memory::u64 block ) const;
		red::memory::u64 GetBlockSize( red::memory::u64 address ) const;

		void BuildMetrics( red::memory::TLSFAllocatorMetrics & metrics );
		void SerializeMetrics( red::memory::Serializer & serializer );
		void SerializerMetricsWithoutAllocatorIdentifiers( red::memory::Serializer & serializer );

		red::memory::Block GetAllocatedPages() const;

	private:

		typedef red::Mutex LockPrimitive;

		red::memory::DynamicTLSFAllocator m_allocator;
		LockPrimitive m_lock;
		bool m_allocatorIsFull;
	};
}
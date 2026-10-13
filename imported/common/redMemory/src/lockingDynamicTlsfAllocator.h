/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_LOCKING_DYNAMIC_TLSF_ALLOCATOR_H_
#define _RED_MEMORY_LOCKING_DYNAMIC_TLSF_ALLOCATOR_H_

#include "allocator.h"
#include "dynamicTlsfAllocator.h"
#include "spinLock.h"
#include "mutex.h"

namespace red
{
namespace memory
{
	class RED_MEMORY_API LockingDynamicTLSFAllocator
	{
	public:

		RED_MEMORY_DECLARE_ALLOCATOR( LockingDynamicTLSFAllocator, TLSFAllocatorMetrics, DynamicTLSFAllocator::DefaultAlignmentType::value );

		LockingDynamicTLSFAllocator();
		~LockingDynamicTLSFAllocator();

		void Initialize( const DynamicTLSFAllocatorParameter & parameter );
		void Uninitialize();	

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		void Free( Block & block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void BuildMetrics( TLSFAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );
		void SerializerMetricsWithoutAllocatorIdentifiers( Serializer & serializer );

		// UNIT TEST ONLY
		void InternalMarkLockingDynamicTLSFAllocatorAsFull();

		Block GetAllocatedPages() const;

	private:

		typedef SpinLock LockPrimitive;
		
		DynamicTLSFAllocator m_allocator;
		LockPrimitive m_lock;
		bool m_allocatorIsFull;
	};
}
}

#endif

/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_LOCKLESS_STATIC_FIXED_SIZE_ALLOCATOR_H_
#define _RED_MEMORY_LOCKLESS_STATIC_FIXED_SIZE_ALLOCATOR_H_

#include "../include/utils.h"
#include "allocator.h"
#include "locklessFixedSizeAllocator.h"

namespace red
{
namespace memory
{
	class Serializer;

	struct LocklessStaticFixedSizeAllocatorParameter
	{
		void * buffer;
		u32 bufferSize;
		u32 blockSize;
		u32 blockAlignment;
	};

	class RED_MEMORY_API LocklessStaticFixedSizeAllocator
	{
	public:

		RED_MEMORY_DECLARE_ALLOCATOR( LocklessStaticFixedSizeAllocator, FixedSizeAllocatorMetrics, 8 );

		LocklessStaticFixedSizeAllocator();
		~LocklessStaticFixedSizeAllocator();

		void Initialize( const  LocklessStaticFixedSizeAllocatorParameter & param );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		void Free( Block & block );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );

		bool OwnBlock( u64 block ) const; 
		u64 GetBlockSize( u64 block ) const;
		u32 GetTotalBlockCount() const;

		void BuildMetrics( FixedSizeAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );

	private:

		RED_ALIGN( 64 ) LocklessFixedSizeAllocator m_allocator;
#if !defined( RED_CONFIGURATION_FINAL )
		bool m_initialized;
#endif
	};
}
}

#endif

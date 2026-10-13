/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "slabHeader.h"
#include "slabConstant.h"
#include "slabAllocator.h"
#include "utils.h"
#include "../include/block.h"
#include "../../redSystem/include/redThreadsAtomic.h"

namespace red
{
namespace memory
{
	u32 GetSlabBlockSize( u64 block )
	{
		return GetSlabHeader( block )->blockSize;
	}

	u32 GetSlabBlockSize( const void * block )
	{
		return GetSlabBlockSize( AddressOf( block ) );
	}

	void PushBlockToFreeList( u64 block, SlabHeader * header )
	{
		IntrusiveSingleLinkedList * node = reinterpret_cast< IntrusiveSingleLinkedList *>( block );
		node->SetNext( nullptr );
		IntrusiveSingleLinkedList ** slabFreeListTail = header->freeListTail;
		
		atomic::TAtomicPtr atomicTail = atomic::ExchangePtr( 
			reinterpret_cast< atomic::TAtomicPtr* >( slabFreeListTail ),  
			reinterpret_cast< atomic::TAtomicPtr >( block ) );

		IntrusiveSingleLinkedList * tail = static_cast< IntrusiveSingleLinkedList* >( atomicTail );
		
		tail->SetNext( node );
	}

	void PushBlockToFreeList( u64 block )
	{
		SlabHeader * blockHeader = GetSlabHeader( block );
		PushBlockToFreeList( block, blockHeader );
	}

	void PushBlockToFreeList( Block & block )
	{
		SlabHeader * blockHeader = GetSlabHeader( block );
		block.size = blockHeader->blockSize;
		PushBlockToFreeList( block.address, blockHeader );
	}

	SlabAllocator* GetSlabAllocatorFromHeader( const SlabHeader * header )
	{
		return reinterpret_cast< SlabAllocator* >( reinterpret_cast< intptr_t >( header->freeListTail ) - offsetof( SlabAllocator, m_freeListTail ) );
	}
}
}

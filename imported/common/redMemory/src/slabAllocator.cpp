/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "slabAllocator.h"
#include "slabChunkAllocatorInterface.h"
#include "slabHeader.h"
#include "slabChunk.h"
#include "assert.h"
#include "../../redSystem/include/redThreadsAtomic.h"
#include "../../redSystem/include/unitTestMode.h"

RED_DISABLE_WARNING_CLANG( "-Wunused-private-field" ) // Ignore warning due to padding not being used.

namespace red
{
namespace memory
{
namespace
{
	const u32 c_extraMemorySize = RED_MEGA_BYTE( 256 );

	void MarkAllocatedBlock( const Block& block )
	{
#ifdef RED_MEMORY_UNIT_TEST
		if( UnitTestMode() )
		{
			MemsetBlock( block, c_slabUnitTestAllocFiller );
		}
#endif
		RED_UNUSED( block );
	}

	void MarkFreeBlock( const Block & block )
	{
#ifdef RED_MEMORY_UNIT_TEST
		if( UnitTestMode() )
		{
			MemsetBlock( block, c_slabUnitTestFreeFiller );
		}
#endif
		RED_UNUSED( block );
	}

	u32 GetBlockSize( u32 size )
	{
		return size <= 128 ? RoundUp( size, 8 ) : RoundUp( size, 16 );
	}

	u32 GetSlabIndex( u32 size )
	{
		// power of 2 division, size / 16 == size >> 4. 
		// Compiler did not optimize ... Still need to profile why it did not.
		const u32 index = size <= 128 ? ( ( size + 7 )  >> 3 ) - 1  : ( ( size + 15 )  >> 4 ) + 7; 
		return index;
	}
}

	SlabAllocator::SlabAllocator()
		:	m_chunk( nullptr ),
			m_freeListHead( nullptr ),
			m_memoryReserved( 0 ),
			m_extraMemoryAddress( 0 ),
			m_memoryUsed( 0 ),
#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
			m_memoryFreed( 0 ),
			m_memoryFreedAfterLastProcess( 0 ),
#endif
			m_freeListTail( nullptr ),
			m_chunkAllocator( nullptr ),
			m_id( 0 ),
			m_flags( Flags_CPU_Read_Write )
	{
		// Head point to sentinel at first so I don't have to check that list is empty and add more thread safety layer.
		m_freeListHead = &m_freeListSentinel;
		m_freeListTail = m_freeListHead;

		static_assert( sizeof( SlabAllocator ) % 64 == 0, "SlabAllocator size is not multiple of cache line size (64 bytes). Update m_padding2." );
		static_assert( offsetof( SlabAllocator, m_freeListHead ) / 64 != offsetof( SlabAllocator, m_freeListTail ) / 64, "Free list tail must not be on same cache line than head to reduce false sharing." );
	}

	SlabAllocator::~SlabAllocator()
	{}

	void SlabAllocator::Initialize( const SlabAllocatorParameter & parameter )
	{
		RED_MEMORY_ASSERT( parameter.chunkAllocator, "SlabAllocator need access to a SlabChunkAllocator." );

		m_reservedMemoryRange = parameter.reservedVirtualRange;
		m_chunkAllocator = parameter.chunkAllocator;
		m_extraMemoryAddress = parameter.extraMemoryAddress;
		m_flags = parameter.flags;
	}

	void SlabAllocator::Uninitialize()
	{}

	Block SlabAllocator::Allocate( u32 size )
	{
		return AllocateAligned( size, c_slabDefaultAlignment );
	}

	Block SlabAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "SlabAllocator was not initialized" );
		RED_MEMORY_ASSERT( alignment <= 64, "SlabAllocator do not support alignment bigger than 64" );
		RED_MEMORY_ASSERT( size <= c_slabMaxAllocSize, "Slab allocator support size between 0 and %d but %d was requested.", c_slabMaxAllocSize, size );
		RED_MEMORY_ASSERT( alignment <= 16 || alignment == 64, "Alignment supported are 8, 16 or 64." );

		size = std::max( 1u, RoundUp( size, alignment ) );

		SlabList & slabList = GetSlabList( size );

		if( slabList.Empty() )
		{
			ProcessFreeList();

			if( slabList.Empty() )
			{
				AllocateSlab( slabList, size );
			
				if( slabList.Empty() )
				{
					// OOM!
					return NullBlock();
				}
			}
		}

		const Block block = TakeBlockFromSlab( slabList );

		MarkAllocatedBlock( block );

		m_memoryUsed += static_cast< u32 >( block.size );

#if defined( RED_MEMORY_ENABLE_ADDITIONAL_SLAB_FREE_LIST_PROCESSING )
		ProcessFreeList();
#endif
		RED_MEMORY_ASSERT( OwnBlock( block ), "SlabAllocator failed to allocate %llu bytes.", block.size );
		
		RED_MEMORY_ASSERT( !block.address || IsAligned(block.address, alignment), "SlabAllocator block is of wrong alignment." );
		return block;
	}

	SlabList & SlabAllocator::GetSlabList( u32 size )
	{
		static_assert( sizeof( SlabList ) == 16, "Slab structure need to be of size 16. Fast lookup require this!" );
		const u32 index = GetSlabIndex( size );
		return m_slabs[ index ];
	}

	void SlabAllocator::ForceProcessFreeList()
	{
		// A bit complicated code here. So I'll try to describe my reasoning as much as possible.
		// Remember that if this function is called from the LocklessSlabAllocator, it means a bunch of thread 
		// could be pushing block into the free list at same time!
		// Also, this function is not thread safe. Like ProcessFreeList, it must be called from bound thread, 
		// or need you need to implement an external locking mechanism. 
		// (I.E other thread can push to list, only bound thread can process it.)

		// 1) I push a dummy sentinel in the free list. ProcessFreeList always leaves one block. 
		// I want to be sure that the m_freeListSentinel is not in the freelist as I'm going to push it back there.
		IntrusiveSingleLinkedList localSentinel;
		IntrusiveSingleLinkedList * currentTail = static_cast< IntrusiveSingleLinkedList* >( atomic::ExchangePtr( reinterpret_cast< atomic::TAtomicPtr* >( &m_freeListTail ), &localSentinel ) );
		currentTail->SetNext( &localSentinel );

		// 2) Free everything from the free list to at least the localSentinel. 
		// This guaranty than even if some other thread push some block to free list, at least the m_freeListSentinel is gone.
		ProcessFreeList();

		// 3) Push the m_freeListSentinel. 
		m_freeListSentinel.SetNext( nullptr );
		currentTail = static_cast< IntrusiveSingleLinkedList* >( atomic::ExchangePtr( reinterpret_cast< atomic::TAtomicPtr* >( &m_freeListTail ), &m_freeListSentinel ) );
		currentTail->SetNext( &m_freeListSentinel );

		// 4) Free Everything from the free list to at least the m_freeListSentinel.
		ProcessFreeList();

		// At this point, the free list is completely empty up to everything that was push before putting back the sentinel.
	}

	void SlabAllocator::ProcessFreeList()
	{
		IntrusiveSingleLinkedList * queue = m_freeListHead;
		IntrusiveSingleLinkedList * last = nullptr;
		IntrusiveSingleLinkedList * next = nullptr;

		for( IntrusiveSingleLinkedList* node = queue; node; node = next )
		{
			const u64 nodeAddress = reinterpret_cast< u64 >( node );
			RED_MEMORY_ASSERT( OwnBlock( nodeAddress ) || InternalBlock( nodeAddress ), "INTERNAL ERROR. Block 0x%p is invalid. Memory Stomp?", nodeAddress );

			next = node->GetNext();
			last = node;

			const u64 nextAddress = reinterpret_cast< u64 >( next );
			RED_MEMORY_ASSERT( !nextAddress || OwnBlock( nextAddress ) || InternalBlock( nextAddress ), "INTERNAL ERROR. Block 0x%p is invalid. Memory Stomp?", nextAddress );

			// If this is the last node in list, do not free it !!!! 
			// Last node might be manipulated by other thread that want to give back memory.

			if( next && OwnBlock( nodeAddress ) )
			{
				SlabHeader * header = GetSlabHeader( nodeAddress ); 
				RED_MEMORY_ASSERT( OwnSlabHeader( header ), "INTERNAL ERROR. Block not own by Allocator. Memory Stomp?" );

#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
				SubFreedMemory( header->blockSize );
#endif
				FreeLocal( nodeAddress, header );
			}
		}

		// if "last" is null, free list is corrupted. ProcessFreeList should always leave at least one item in it.
		RED_MEMORY_ASSERT( last, "SLAB ALLOCATOR INTERNAL STATE ERROR. DEBUG THIS NOW." );

#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
		ClearFreedMemory();
#endif
		m_freeListHead = last;
	}

#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
	void SlabAllocator::SubFreedMemory( u32 size )
	{
		const u64 sizeU64 = static_cast< u64 >( static_cast< i64 >( -static_cast< i32 >( size ) ) );
		atomic::ExchangeAdd( &m_memoryFreed, sizeU64 );
	}

	void SlabAllocator::AddFreedMemory( u32 size )
	{
		const u64 sizeU64 = static_cast< u64 >( static_cast< i64 >( static_cast< i32 >( size ) ) );
		atomic::ExchangeAdd( &m_memoryFreed, sizeU64 );
		atomic::ExchangeAdd( &m_memoryFreedAfterLastProcess, sizeU64 );
	}

	void SlabAllocator::ClearFreedMemory()
	{
		m_memoryFreedAfterLastProcess = 0;
	}
#endif

	void SlabAllocator::AllocateSlab( SlabList & slab, u32 size )
	{
		if( !m_chunk )
		{
			m_chunk = m_chunkAllocator->Allocate( m_id, m_flags );
			if( !m_chunk )
			{
				return;
			}

			m_memoryReserved += m_chunk->slabCount * c_slabSize + c_slabSize;
		}

		SlabHeader * header = TakeSlabFromChunk();
		InitializeSlab( slab, size, header );
		header->freeListTail = &m_freeListTail;
	}

	SlabHeader * SlabAllocator::TakeSlabFromChunk()
	{
		SlabHeader * header = nullptr;

		if( !m_chunk->slabCount )
		{
			// No Slab ? no problem. We can reuse Chunk memory. 
			// Next time, we'll need a new chunk.
			header = reinterpret_cast< SlabHeader* >( m_chunk );
			header->blockSize = 0;
			m_chunk = nullptr;
		}
		else
		{
			--m_chunk->slabCount;
			header = m_chunk->slabs[ m_chunk->slabCount ];
		}

		return header;
	}

	void SlabAllocator::InitializeSlab( SlabList & slab, u32 size, SlabHeader * header )
	{
		slab.PushFront( &header->listNode );

		size = GetBlockSize( size );

		header->lastBlock = reinterpret_cast< u64 >( header ) + c_slabSize - size;
		header->blockSize = size;
		header->blockUsed = 0;

		u64 offset = 0;
		if( size >= 64 && IsAligned( size, 64 ) )
		{
			// Align on 64. Perfect fit for cache line. 
			// Why + 128? Header are already align correctly, but its size is bigger than 64 and less than 128. 
			offset = reinterpret_cast< u64 >( header ) + 128; 
		}
		else
		{
			offset = reinterpret_cast< u64 >( &header->data );
			offset = AlignAddress( offset, 16 );
		}

		offset += 1; // Magic bit to know if memory was previously allocate or not.
		header->firstFree = offset;
	}

	Block SlabAllocator::TakeBlockFromSlab( SlabList & slab )
	{
		const u64 headerOffset = reinterpret_cast< u64 >( slab.GetNext() ) - offsetof( SlabHeader, listNode );
		SlabHeader * header = reinterpret_cast< SlabHeader * >( headerOffset );
		++header->blockUsed;

		u64 blockValue = header->firstFree;

		if( blockValue & 1 ) // See InitializeSlab where this magic bit is added.
		{
			// Memory was not previously allocated. Return next contiguous memory block
			blockValue -= 1; // remove magic bit.
			header->firstFree += header->blockSize;
			if( header->firstFree > header->lastBlock )
			{
				header->firstFree = 0;
			}
		}
		else
		{
			// Memory was freed by user. Return directly. 
			// We always return last freed block to improve localized memory.

			// Push next available block to free list.
			header->firstFree = *reinterpret_cast< u64 * >( blockValue );
		}

		if( !header->firstFree )
		{
			PushSlabToEmptyList( header ); // nothing left... Push slab to empty list
		}

		const Block block = { blockValue, header->blockSize };
		RED_MEMORY_ASSERT( OwnBlock( block ), "SlabAllocator failed to allocate %llu bytes.", block.size );
		return block;
	}

	void SlabAllocator::PushSlabToEmptyList( SlabHeader * header )
	{
		header->emptyFlag = 1;
		header->listNode.Remove();
		m_emptySlabList.PushFront( &header->listNode );
	}

	void SlabAllocator::Free( Block & block )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "SlabAllocator was not intialized." );

		if( block.address == 0 )
			return;

		RED_MEMORY_ASSERT( OwnBlock( block ), "SlabAllocator do not own provided memory." );

		SlabHeader * header = GetSlabHeader( block );
		block.size = header->blockSize;

		MarkFreeBlock( block );

		if( OwnSlabHeader( header ) )
		{
			// Yes, correctly free.
			FreeLocal( block.address, header );
		}
		else
		{
			// No, then postpone until correct thread need the memory.
#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
			GetSlabAllocatorFromHeader( header )->AddFreedMemory( header->blockSize );
#endif
			PushBlockToFreeList( block.address, header );
		}

#if defined( RED_MEMORY_ENABLE_ADDITIONAL_SLAB_FREE_LIST_PROCESSING )
		ProcessFreeList();
#endif
	}

	bool SlabAllocator::OwnSlabHeader( SlabHeader * header )
	{
		return header->freeListTail == &m_freeListTail;
	}

	void SlabAllocator::FreeLocal( u64 block, SlabHeader * header )
	{
		*reinterpret_cast< u64* >( block ) = header->firstFree;
		header->firstFree = block;

		const u32 useCount = --header->blockUsed;

		m_memoryUsed -= header->blockSize;

		if( !useCount )
		{
			// slab is completely free.
			// Give it back only if we have more than 1 slab partially or fully free available for this specific size.
			
			// ctremblay. Commented this line to maximize reuse of slab. 
			// TODO profile if we can remove this altogether,
			//if( header->listNode.GetNext() != header->listNode.GetPrevious() )
			{
				header->listNode.Remove();
				FreeSlab( header );
			}
		}
		else if( header->emptyFlag )
		{
			// Slab was previously empty, remove from empty list back to partial list

			SlabList & slab = GetSlabList( header->blockSize );
			header->listNode.Remove();
			slab.PushFront( &header->listNode );
			header->emptyFlag = 0;
		}
	}

	void SlabAllocator::FreeSlab( SlabHeader * header )
	{
		SlabChunk * chunk = reinterpret_cast< SlabChunk* >( header );

		if( m_chunk )
		{
			if( IsSlabChunkInUse( m_chunk ) )
			{
				// Chunk is used, push this slab to available block list
				m_chunk->slabs[ m_chunk->slabCount ] = header;
				++m_chunk->slabCount;

				if( m_memoryUsed == 0 )
				{
					FreeChunk( m_chunk );
					m_chunk = nullptr;
				}
			}
			else
			{
				// Chunk is not being use. Release it and keep slab as active one.
				SlabChunk * chunkToFree = m_chunk;
				chunk->slabCount = 0;
				m_chunk = chunk;
				FreeChunk( chunkToFree );
			}
		}
		else
		{
			// No chunk ? take this one.
			chunk->slabCount = 0;
			m_chunk = chunk;
		}
	}

	void SlabAllocator::FreeChunk( SlabChunk * chunk )
	{
		m_memoryReserved -= chunk->slabCount * c_slabSize + c_slabSize;
		m_chunkAllocator->Free( m_id, chunk );
	}

	Block SlabAllocator::Reallocate( Block & block, u32 size )
	{
		RED_MEMORY_ASSERT( IsInitialized(), "SlabAllocator was not intialized." );

		if( block.address == 0 )
		{
			return Allocate( size );
		}

		RED_MEMORY_ASSERT( OwnBlock( block ), "SlabAllocator do not own provided memory." );

		if( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		SlabHeader * slabHeader = GetSlabHeader( block );
		block.size = slabHeader->blockSize;
		if( block.size == GetBlockSize( size ) )
		{
			return block;
		}

		// SlabAllocator can't grow or shrink a block. Need to allocate/memcpy/free 
		Block newBlock = Allocate( size );
		if( newBlock.address )
		{
			MemcpyBlock( newBlock, block );
		}

		Free( block );

		return newBlock;
	}

	bool SlabAllocator::OwnBlock( u64 block ) const
	{
		RED_MEMORY_ASSERT( IsInitialized(), "SlabAllocator was not intialized." );
		// Ok, this is "correct" but also incorrect. If virtual memory is not committed it will return true.
		// However if the memory was not committed then any access to this address will make cpu fire exception.
		return ( block - m_reservedMemoryRange.start < GetVirtualRangeSize( m_reservedMemoryRange ) ) 
			|| ( m_extraMemoryAddress && ( block - m_extraMemoryAddress < c_extraMemorySize ) ); 
	}

	bool SlabAllocator::OwnBlock( const Block& block ) const
	{
		return block.address && OwnBlock( block.address ) && OwnBlock( block.address + block.size );
	}

	bool SlabAllocator::InternalBlock( u64 block ) const
	{
		u64 localVariable = 0;

		return block - reinterpret_cast< u64 >( this ) < sizeof( *this ) //< Is a member?
			|| block - reinterpret_cast< u64 >( &localVariable ) < 0x10000ll; //< Is a stack variable?
	}

	bool SlabAllocator::IsInitialized() const
	{
		return m_chunkAllocator != 0;
	}

	void SlabAllocator::SetId( u32 id )
	{
		m_id = id;
	}

	void SlabAllocator::BuildMetrics( SlabAllocatorMetrics & metrics )
	{
		metrics.metrics.smallestBlockSize = c_slabMinAllocSize;
		metrics.metrics.largestBlockSize = c_slabMaxAllocSize;
		metrics.metrics.consumedMemoryBytes = m_memoryUsed;
		metrics.metrics.consumedSystemMemoryBytes = m_memoryReserved;
		metrics.metrics.bookKeepingBytes = 0;

#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
		metrics.freedMemoryBytes = m_memoryFreed;
		metrics.freedAfterLastProcessMemoryBytes = m_memoryFreedAfterLastProcess;
#endif
	}

	u32 SlabAllocator::InternalGetAvailableSlabCount()
	{
		return m_chunk ? m_chunk->slabCount : 0;
	}

	const SlabChunk * SlabAllocator::InternalGetChunk()
	{
		return m_chunk;
	}

	void SlabAllocator::InternalMarkChunkAsEmpty()
	{
		m_chunk->slabCount = 0;
	}

	void SlabAllocator::InternalMarkSlabAsEmpty( u32 allocSize )
	{
		SlabList & slab = GetSlabList( allocSize );
		
		while( !slab.Empty() )
		{
			const u64 headerOffset = reinterpret_cast< u64 >( slab.GetNext() ) - offsetof( SlabHeader, listNode );
			SlabHeader * header = reinterpret_cast< SlabHeader * >( headerOffset );

			header->firstFree = 0;
			header->blockUsed = static_cast< u32 >( header->lastBlock / header->blockSize );
			header->emptyFlag = 1;
			header->listNode.Remove();
			m_emptySlabList.PushFront( &header->listNode );
		}
	}

	const SlabFreeList * SlabAllocator::InternalGetFreeListTail() const
	{
		return m_freeListTail;
	}
}
}

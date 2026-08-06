/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "stackAllocator.h"
#include "assert.h"
#include "utils.h"

namespace red
{
namespace memory
{
	// bytes used to save the allocation header
	const u32 c_stackAllocatorHeaderSize = 8;

	StackAllocator::StackAllocator()
		: m_start( 0 )
		, m_size( 0 )
		, m_offset( 0 )
		, m_defaultAlignment( 1 )
	{
	}

	void StackAllocator::Initialize( const StackAllocatorParameter& parameter )
	{
		RED_MEMORY_ASSERT( parameter.block.size, "Buffer size needs to be at least 1." );
		RED_MEMORY_ASSERT( parameter.block.address, "Buffer needs to be valid." );
		RED_MEMORY_ASSERT( IsPowerOf2( parameter.defaultAlignment ), "Default alignment must be power of two." );

		m_start = parameter.block.address;
		m_size = parameter.block.size;
		m_defaultAlignment = parameter.defaultAlignment;
	}

	Block StackAllocator::Allocate( u32 size )
	{
		return AllocateAligned( size, m_defaultAlignment );
	}

	Block StackAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		size += c_stackAllocatorHeaderSize;
		alignment = std::max( alignment, m_defaultAlignment );
		size = RoundUp( size, alignment );
		u64 address = RoundUp( m_start + m_offset, static_cast< u64 >( alignment ) );
		const u32 newOffset = static_cast< u32 >( ( address + size ) - m_start );

		if ( newOffset > m_size )
			return NullBlock();

		// select the current offset
		void* addressAsVoid = reinterpret_cast< void* >( address );

		const u32 blockSizeWithoutHeaderSize = size - c_stackAllocatorHeaderSize;

		// build an allocation header
		// store the offset previous to allocation in the first 4 bytes
		// ( this offset is used for bookkeeping the previous top of the stack to be able to properly restore it in Free method )
		// store block size in the next 4 bytes of the allocation header
		const u64 allocationHeader = ( ( ( static_cast< u64 >( m_offset ) << 32 ) & 0xffffffff00000000 ) | ( blockSizeWithoutHeaderSize & 0xffffffff ) );
		std::memcpy( addressAsVoid, &allocationHeader, sizeof( u64 ) );

		// then move forward to the user memory
		const u64 blockAddress = address + c_stackAllocatorHeaderSize;

		// update the buffer's offset
		m_offset += size;

		return { blockAddress, static_cast< u64 >( blockSizeWithoutHeaderSize ) };
	}

	void StackAllocator::Free( Block& block )
	{
		if (block.address)
		{
			RED_MEMORY_ASSERT( OwnBlock( block.address ), "Block is not owned by this StackAllocator." );

			const u64 headerPreviousToAllocationAddress = block.address - c_stackAllocatorHeaderSize;
			void* addressAsVoid = reinterpret_cast< void* >( headerPreviousToAllocationAddress );

			// read allocation header
			u64 allocationHeader;
			std::memcpy( &allocationHeader, addressAsVoid, sizeof( u64 ) );

			// read the offset previous to this allocation
			const u32 offset = ( allocationHeader >> 32 ) & 0xffffffff;
			RED_MEMORY_ASSERT( m_start + offset == headerPreviousToAllocationAddress, "Read offset does not match expected offset." );

			// read block size
			const u32 blockSize = allocationHeader & 0x00000000ffffffff;
			block.size = blockSize;

			// update the buffer's offset
			m_offset = offset;
		}
	}

	Block StackAllocator::Reallocate( Block& block, u32 size )
	{
		return ReallocateAligned( block, size, m_defaultAlignment );
	}

	Block StackAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		if ( block == NullBlock() )
		{
			// allocate new block when null block is provided
			return AllocateAligned( size, alignment );
		}

		RED_MEMORY_ASSERT( OwnBlock( block.address ), "Block is not owned by this StackAllocator." );

		if ( size == 0 )
		{
			// free when new size is 0
			Free(block);
			return NullBlock();
		}

		block.size = GetBlockSize( block.address );

		if ( block.size >= size )
		{
			// do nothing when new size is equal or less than old size
			return block;
		}
		else
		{
			const u64 headerPreviousToAllocationAddress = block.address - c_stackAllocatorHeaderSize;
			if ( m_start + m_offset == headerPreviousToAllocationAddress + block.size + c_stackAllocatorHeaderSize )
			{
				// reallocate on last allocate block

				Block result = block;
				size += c_stackAllocatorHeaderSize;
				alignment = std::max( alignment, m_defaultAlignment );
				size = RoundUp( size, alignment );
				const u32 newOffset = static_cast< u32 >( ( headerPreviousToAllocationAddress + size ) - m_start );

				if (newOffset > m_size)
					return NullBlock();

				// update offset
				m_offset = newOffset;

				// update block size
				result.size = size - c_stackAllocatorHeaderSize;
				return result;
			}
			else
			{
				// reallocate block from the middle of the stack

				auto newBlock = AllocateAligned( size, alignment );
				if ( newBlock == NullBlock() )
					return NullBlock();

				// copy previous data to new allocation storage
				MemcpyBlock( newBlock, block );

				// leave old allocation as it was and return new block
				// @todo consider keeping track of free blocks
				return newBlock;
			}
		}
	}

	bool StackAllocator::OwnBlock( u64 block ) const
	{
		RED_MEMORY_ASSERT( IsInitialized(), "StackAllocator was not initialized." );
		return block >= m_start && block <= ( m_start + m_offset );
	}

	u64 StackAllocator::GetBlockSize( u64 block ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( block ), "Block is not owned by this StackAllocator." );

		const u64 headerPreviousToAllocationAddress = block - c_stackAllocatorHeaderSize;
		void* addressAsVoid = reinterpret_cast< void* >( headerPreviousToAllocationAddress );

		// read allocation header
		u64 allocationHeader;
		std::memcpy( &allocationHeader, addressAsVoid, sizeof( u64 ) );

		// read block size
		return allocationHeader & 0x00000000ffffffff;
	}

	void StackAllocator::Reset()
	{
		m_offset = 0;
	}

	void StackAllocator::UpdateBuffer( const Block& block )
	{
		if ( m_start != block.address )
			m_start = block.address;

		if ( m_size != block.size )
			m_size = block.size;
	}

	void StackAllocator::BuildMetrics( StackAllocatorMetrics & metrics )
	{
		Memzero( &metrics, sizeof( metrics ) );

		metrics.metrics.bookKeepingBytes = c_stackAllocatorHeaderSize;
		metrics.metrics.consumedSystemMemoryBytes = m_size;
		metrics.metrics.consumedMemoryBytes = m_offset;

		const auto freeBlockSize = m_size - m_offset;
		metrics.metrics.smallestBlockSize = freeBlockSize >= c_stackAllocatorHeaderSize ? c_stackAllocatorHeaderSize : 0;
		metrics.metrics.largestBlockSize = freeBlockSize;
	}

	void StackAllocator::SerializeMetrics( Serializer & serializer )
	{
		StackAllocatorMetrics metrics;
		BuildMetrics( metrics );

		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

	bool StackAllocator::IsInitialized() const
	{
		return m_start != 0;
	}

}
}
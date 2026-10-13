/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "buddyAllocator.h"
#include "buddyAllocatorUtils.h"
#include "flags.h"
#include "systemAllocator.h"

namespace red
{
namespace memory
{
	BuddyAllocator::BuddyAllocator()
		: m_systemAllocator( nullptr )
		, m_buffer( nullptr )
		, m_nextVirtualAddress( 0 )
		, m_freeBlocks( nullptr )
		, m_blockIndex( nullptr )
		, m_size( 0 )
		, m_maxSize( 0 )
		, m_minAllocation( c_buddyMinLeafSize )
		, m_maxIndexes( 0 )
		, m_totalLevels( 0 )
		, m_maxLevel( 0 )
		, m_currentMaxLevel( 0 )
		, m_flags( 0 )
		, m_firstAllocation( true )
	{
	}

	BuddyAllocator::~BuddyAllocator()
	{
		Uninitialize();
	}

	void BuddyAllocator::Initialize( const BuddyAllocatorParamater& parameter ) 
	{
		RED_MEMORY_ASSERT( IsPowerOf2( parameter.maxBufferSize ), "Given max buffer size has to be power of 2" );
		RED_MEMORY_ASSERT( IsPowerOf2( parameter.initBufferSize ), "Given init buffer size has to be power of 2" );
		RED_MEMORY_ASSERT( IsPowerOf2( parameter.alignment ), "Given allocation alignment has to be power of 2" );
		RED_MEMORY_ASSERT( std::numeric_limits< u32 >::max() >= parameter.maxBufferSize, "Given max buffer size must be less or equal to max(u32)" );
		RED_MEMORY_ASSERT( parameter.maxBufferSize >= parameter.initBufferSize, "Given max buffer size must be greater or equal to init buffer size" );

		m_systemAllocator = parameter.systemAllocator;
		m_flags = parameter.flags;

		m_minAllocation = Max( parameter.alignment, c_buddyMinLeafSize );
		m_size = parameter.initBufferSize;
		m_maxSize = parameter.maxBufferSize;

		// adjust virtual buffer size, so it can properly align on m_minAllocation
		const u32 virtualBufferSize = parameter.maxBufferSize + m_minAllocation - 1;

		// set up buffer and calculate it's size
		m_bufferVirtualRange = m_systemAllocator->ReserveVirtualRange( virtualBufferSize, m_flags );
		m_nextVirtualAddress = AlignAddress( m_bufferVirtualRange.start, m_minAllocation );

		SystemBlock bufferBlock = m_systemAllocator->Commit( { m_nextVirtualAddress, m_size }, m_flags );
		RED_MEMORY_ASSERT( IsAligned( bufferBlock.address, m_minAllocation ), "Internal buffer is not properly aligned" );
		m_buffer = reinterpret_cast< void* >( bufferBlock.address );
		m_nextVirtualAddress += bufferBlock.size;

		// initialize metadata
		m_totalLevels = BuddyILog2( m_maxSize );
		m_maxIndexes = BuddyMaxIndexes( m_maxSize, m_minAllocation );
		m_maxLevel = BuddyMaxLevels( m_maxSize, m_minAllocation );
		m_currentMaxLevel = BuddyMaxLevels( m_size, m_minAllocation );

		// set up free blocks and block indexes
		const u32 freeListSize = sizeof( BuddyBlockInfo ) * ( m_maxLevel + 1 );
		const u32 blockIndexSize = sizeof( u32 ) * ( ( m_maxIndexes + ( c_buddyNumBits - 1 ) ) / c_buddyNumBits );
		m_metadataVirtualRange = m_systemAllocator->ReserveVirtualRange( freeListSize + blockIndexSize, Flags_CPU_Read_Write );
		SystemBlock metadataBlock = m_systemAllocator->Commit( { m_metadataVirtualRange.start, freeListSize + blockIndexSize }, Flags_CPU_Read_Write );

		RED_MEMORY_ASSERT( IsAligned( metadataBlock.address, std::alignment_of< BuddyBlockInfo >::value ), "Free blocks buffer is not properly aligned" );
		m_freeBlocks = (BuddyBlockInfo*)metadataBlock.address;
		RED_MEMORY_ASSERT( IsAligned( metadataBlock.address + freeListSize, std::alignment_of< u32 >::value ), "Block indexes buffer is not properly aligned" );
		m_blockIndex = (u32*)( metadataBlock.address + freeListSize );

		// initialize block levels
		for ( u32 i = 0; i < m_maxLevel + 1; ++i )
		{
			BuddyListInit( &m_freeBlocks[i] );
		}

		// initialize block indexes
		for ( u32 i = 0; i < ( m_maxIndexes + ( c_buddyNumBits - 1 ) ) / c_buddyNumBits; ++i )
		{
			m_blockIndex[i] = 0;
		}

		// add memory to highest level (the biggest block)
		const u32 currentTopLevel = m_maxLevel - m_currentMaxLevel;
		BuddyListAdd( &m_freeBlocks[currentTopLevel], reinterpret_cast< BuddyBlockInfo* >( m_buffer ) );
	}

	void BuddyAllocator::Uninitialize()
	{
		if ( m_systemAllocator )
		{
			m_systemAllocator->ReleaseVirtualRange( m_metadataVirtualRange );
			m_systemAllocator->ReleaseVirtualRange( m_bufferVirtualRange );
			m_systemAllocator = nullptr;
		}
	}

	Block BuddyAllocator::Allocate( u32 size )
	{
		return AllocateAligned( size, m_minAllocation ); // align on minimal allocation size
	}

	Block BuddyAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Buddy allocator wasn't initialized" );
		RED_MEMORY_ASSERT( IsPowerOf2( alignment ), "Given alignment (%u) must be a power of 2", alignment );
		RED_MEMORY_ASSERT( alignment == m_minAllocation, "Given alignment (%u) must be the same as default alignment (%lu)", alignment, m_minAllocation );

		size = RoundUp( size, alignment );
		const u32 level = SizeToLevel( size );
		return BuddyAllocFromLevel( level );
	}

	Block BuddyAllocator::Reallocate( Block& block, u32 size )
	{
		return ReallocateAligned( block, size, m_minAllocation );
	}

	Block BuddyAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Buddy allocator wasn't initialized" );

		if ( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		if ( block.address == 0 )
		{
			return AllocateAligned( size, alignment );
		}

		block.size = GetBlockSize( block.address );
		if ( size == block.size )
		{
			return block;
		}
		else if ( size < block.size && block.size == m_minAllocation )
		{
			return block;
		}
		else
		{
			Block newBlock = AllocateAligned( size, alignment );
			MemcpyBlock( newBlock.address, block.address, block.size );
			Free( block );
			return newBlock;
		}
	}

	void BuddyAllocator::Free( Block& block )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Buddy allocator wasn't initialized" );

		if ( !block.address )
		{
			return;
		}

		// determine level and release
		u32 index = IndexOf( reinterpret_cast< void* >( block.address ), m_maxLevel );
		const i32 currentTopLevel = m_maxLevel - m_currentMaxLevel;
		for ( i32 level = (i32)m_maxLevel; level > currentTopLevel; --level )
		{
			index = ( index - 1 ) >> 1;
			if ( IsBuddyBitArraySet( m_blockIndex, SplitIndex( index ) ) )
			{
				BuddyReleaseAtLevel( reinterpret_cast< void* >( block.address ), level );
				block.size = ( 1ull << m_maxLevel ) * m_minAllocation / ( 1ull << level );
				return;
			}
		}

		// must be released from the root
		if ( block.address == reinterpret_cast< u64 >( m_buffer ) )
		{
			BuddyReleaseAtLevel( reinterpret_cast< void* >( block.address ), 0 );
			block.size = ( 1ull << m_maxLevel ) * m_minAllocation;
			return;
		}

		RED_MEMORY_ASSERT( 0, "Trying to release block (%llu) which is not owned by this allocator.", block.address );
	}

	bool BuddyAllocator::OwnBlock( u64 block ) const
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Buddy allocator wasn't initialized" );
		return reinterpret_cast< u64 >( m_buffer ) <= block && ( block - reinterpret_cast< u64 >( m_buffer ) ) <= m_size;
	}

	u64 BuddyAllocator::GetBlockSize( u64 block ) const
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Buddy allocator wasn't initialized" );

		if ( !block )
		{
			return 0;
		}

		// determine level and return block size
		u32 index = IndexOf( reinterpret_cast< void* >( block ), m_maxLevel );
		for ( i32 level = (i32)m_currentMaxLevel; level > 0; --level )
		{
			index = ( index - 1 ) >> 1;
			if ( IsBuddyBitArraySet( m_blockIndex, SplitIndex( index ) ) )
			{
				return ( 1ull << m_currentMaxLevel ) * m_minAllocation / ( 1ull << level );
			}
		}

		return ( 1ull << m_currentMaxLevel ) * m_minAllocation;
	}

	u32 BuddyAllocator::GetSystemMemoryUsage() const
	{
		return static_cast< u32 >( m_nextVirtualAddress - reinterpret_cast< u64 >( m_buffer ) );
	}

	bool BuddyAllocator::CanGrowBuffer( u32 size ) const
	{
		return m_firstAllocation ? m_maxSize >= size : ( m_maxSize - m_size ) >= size;
	}

	bool BuddyAllocator::GrowBlock( u32 size )
	{
		u32 bytesToCommit = 0;

		if ( !m_firstAllocation )
		{
			bytesToCommit = size >= m_size ? size + ( size - m_size ) : m_size;
		}
		else
		{
			RED_MEMORY_ASSERT( size > m_size, "Invalid data" );
			bytesToCommit = size - m_size;
		}

		RED_MEMORY_ASSERT( ( m_nextVirtualAddress + bytesToCommit ) <= m_bufferVirtualRange.end, "Not enough memory" );
		RED_MEMORY_ASSERT( IsAligned( m_nextVirtualAddress + bytesToCommit, static_cast< u32 >( m_systemAllocator->GetPageSize() ) ), "Invalid data" );

		SystemBlock systemBlock = m_systemAllocator->Commit( { m_nextVirtualAddress, bytesToCommit }, m_flags );
		if ( systemBlock == NullSystemBlock() )
		{
			return false;
		}

		m_nextVirtualAddress += bytesToCommit;
		u32 expectedSize = m_size + bytesToCommit;

		// adjust metadata
		while ( m_size < expectedSize )
		{
			m_size <<= 1;

			const i32 prevTopLevel = m_maxLevel - m_currentMaxLevel;

			m_currentMaxLevel = BuddyMaxLevels( m_size, m_minAllocation );

			const i32 currentTopLevel = m_maxLevel - m_currentMaxLevel;

			if ( m_firstAllocation )
			{
				BuddyListRemove( &m_freeBlocks[ prevTopLevel ] );
				BuddyListAdd( &m_freeBlocks[ currentTopLevel ], reinterpret_cast< BuddyBlockInfo* >( m_buffer ) );
			}
			else
			{
				// mark highest block as split
				u32 highestBlockIndex = IndexOf( m_buffer, currentTopLevel );
				BuddyBitArraySet( m_blockIndex, SplitIndex( highestBlockIndex ) );

				// get the buddy pointer (right block on previously highest level)
				BuddyBlockInfo* buddyBlockPtr = ToBuddy( reinterpret_cast< u64 >( m_buffer ), currentTopLevel + 1 );

				// mark left block on previously highest level as allocated
				u32 prevHighestBlockIndex = IndexOf( buddyBlockPtr, currentTopLevel + 1 );
				// prevHighestBlockIndex points to right block, so we need to subtract 1
				BuddyBitArrayNot( m_blockIndex, FreeIndex( prevHighestBlockIndex - 1 ) );

				// add right block to the free list
				BuddyListAdd( &m_freeBlocks[ currentTopLevel + 1 ], buddyBlockPtr );
			}
		}

		return true;
	}

	u32 BuddyAllocator::ShrinkBuffer()
	{
		u32 releasedBytes = 0;
		u64 currentNextVirtualAddress = m_nextVirtualAddress;
		// adjust metadata
		while ( m_currentMaxLevel > 0 )
		{
			const u32 currentTopLevel = m_maxLevel - m_currentMaxLevel;
			u32 level = currentTopLevel + 1;

			// get the buddy pointer (right block on previously highest level)
			BuddyBlockInfo* buddyBlockPtr = ToBuddy( reinterpret_cast< u64 >( m_buffer ), level );

			// check if previous highest level contains free block
			if ( IsBuddyListEmpty( &m_freeBlocks[level] ) )
			{
				break;
			}

			// search for a buddy in the free list
			if ( !BuddyListContains( &m_freeBlocks[level], buddyBlockPtr ) )
			{
				break;
			}

			// remove buddy from free list
			BuddyListRemove( buddyBlockPtr );

			// reset bits for highest block
			u32 highestBlockIndex = IndexOf( m_buffer, currentTopLevel );
			m_blockIndex[highestBlockIndex] = 0;

			// reset bits for previously highest free block (right block)
			u32 preHighestBlockIndex = IndexOf( buddyBlockPtr, level );
			m_blockIndex[preHighestBlockIndex] = 0;

			m_size >>= 1;
			releasedBytes += m_size;
			currentNextVirtualAddress -= m_size;

			m_currentMaxLevel = BuddyMaxLevels(m_size, m_minAllocation);
		}

		if ( releasedBytes > 0 )
		{
			m_nextVirtualAddress -= releasedBytes;
			m_systemAllocator->Decommit( { m_nextVirtualAddress, releasedBytes } );
		}

		return releasedBytes;
	}

	u32 BuddyAllocator::SizeToLevel( u32 size ) const
	{
		// floor the minimum allocation size
		if ( size < m_minAllocation )
		{
			return m_maxLevel;
		}

		// round up to next power of two
		size = 1 << ( c_buddyNumBits - BUILTIN_CLZ( size - 1 ) );

		// delta between the number of levels and the first set bit in the size
		return m_totalLevels - BuddyILog2( size );
	}

	u32 BuddyAllocator::IndexOf( const void* ptr, u32 level ) const
	{
		return (u32)( ( 1ull << level ) + ( ( (u64)ptr - (u64)m_buffer ) >> ( m_totalLevels - level ) ) - 1 );
	}

	u32 BuddyAllocator::FreeIndex( u32 index ) const
	{
		return ( index - 1 ) >> 1;
	}

	u32 BuddyAllocator::SplitIndex( u32 index ) const
	{
		return index + ( m_maxIndexes >> 1 );
	}

	BuddyBlockInfo* BuddyAllocator::ToBuddy( u64 ptr, u32 level ) const
	{
		return (BuddyBlockInfo*)( ( ( ptr - (u64)m_buffer ) ^ ( m_maxSize >> level ) ) + (u64)m_buffer );
	}

	Block BuddyAllocator::BuddyAllocFromLevel( i32 level )
	{
		// search backwards up the levels
		i32 blockAtLevel = 0;
		BuddyBlockInfo* blockPtr = nullptr;
		BuddyBlockInfo* buddyBlockPtr = nullptr;
		const i32 currentTopLevel = m_maxLevel - m_currentMaxLevel;
		for ( blockAtLevel = level; blockAtLevel >= currentTopLevel; --blockAtLevel )
		{
			if ( !IsBuddyListEmpty( &m_freeBlocks[blockAtLevel] ) )
			{
				blockPtr = BuddyListPop( &m_freeBlocks[blockAtLevel] );
				break;
			}
		}

		if ( !blockPtr )
		{
			return NullBlock();
		}

		u32 index = IndexOf( blockPtr, blockAtLevel );

		// do we need to split blocks?
		if ( blockAtLevel != level )
		{
			// split the block until we reach the requested level
			while ( blockAtLevel < level )
			{
				// mark block as split
				BuddyBitArraySet( m_blockIndex, SplitIndex( index ) );

				// mark as allocated, level zero does not use a allocation flags
				//if ( blockAtLevel > 0 )
				if ( blockAtLevel > currentTopLevel )
				{
					BuddyBitArrayNot( m_blockIndex, FreeIndex( index ) );
				}

				// get the buddy pointer
				buddyBlockPtr = ToBuddy( reinterpret_cast< u64 >( blockPtr ), blockAtLevel + 1 );

				// add right side to the free list
				BuddyListAdd( &m_freeBlocks[ blockAtLevel + 1 ], buddyBlockPtr );

				// adjust index to left child
				index = ( index << 1 ) + 1;

				++blockAtLevel;
			}
		}

		// mark as allocated
		if ( level > currentTopLevel )
		{
			BuddyBitArrayNot( m_blockIndex, FreeIndex( index ) );
		}

		if ( blockPtr )
		{
			if ( m_firstAllocation )
			{
				m_firstAllocation = false;
			}

			return { (u64)blockPtr, ( 1ull << m_maxLevel ) * m_minAllocation / ( 1ull << level ) };
		}
		else
		{
			return NullBlock();
		}
	}

	void BuddyAllocator::BuddyReleaseAtLevel( void* ptr, i32 level )
	{
		void* buddyPtr = ToBuddy( reinterpret_cast< u64 >( ptr ), level );
		u32 index = IndexOf( ptr, level );
		const i32 currentTopLevel = m_maxLevel - m_currentMaxLevel;

		// flip the allocation bit, highest level does not use a allocation bit
		if ( level > currentTopLevel )
		{
			BuddyBitArrayNot( m_blockIndex, FreeIndex( index ) );
		}

		// coalesce the blocks
		while ( level > currentTopLevel && !IsBuddyBitArraySet( m_blockIndex, FreeIndex( index ) ) )
		{
			// clear the split bit
			BuddyBitArrayClear( m_blockIndex, SplitIndex( index ) );

			// remove it from the list
			BuddyListRemove( reinterpret_cast< BuddyBlockInfo* >( buddyPtr ) );

			// adjust the index and level
			index = ( index - 1 ) >> 1;
			--level;

			// make sure that we are using the left buddy
			if ( buddyPtr < ptr )
			{
				ptr = buddyPtr;
			}

			// get the new buddy
			buddyPtr = ToBuddy( reinterpret_cast< u64 >( ptr ), level );

			// flip the allocation bit
			if ( level > currentTopLevel )
			{
				BuddyBitArrayNot( m_blockIndex, FreeIndex( index ) );
			}
		}

		// clear the split bit
		BuddyBitArrayClear( m_blockIndex, SplitIndex( index ) );

		// add combined block to it's free list
		if ( level > (i32)currentTopLevel )
		{
			RED_MEMORY_ASSERT( !BuddyListContains( &m_freeBlocks[level], reinterpret_cast< BuddyBlockInfo* >( ptr ) ), "Buddy internal error. Possible double free detected." );
			BuddyListAdd( &m_freeBlocks[level], reinterpret_cast< BuddyBlockInfo* >( ptr ) );
		}
		else
		{
			RED_MEMORY_ASSERT( !BuddyListContains( &m_freeBlocks[currentTopLevel], reinterpret_cast< BuddyBlockInfo* >( m_buffer ) ), "Buddy internal error. Possible double free detected." );
			BuddyListAdd( &m_freeBlocks[currentTopLevel], reinterpret_cast< BuddyBlockInfo* >( m_buffer ) );
			m_firstAllocation = true;
		}
	}

}
}
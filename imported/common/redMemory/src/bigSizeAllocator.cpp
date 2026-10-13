/**
 * Copyright © 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "bigSizeAllocator.h"
#include "utils.h"
#include "flags.h"
#include "systemAllocator.h"
#include "scopedLock.h"
#include <algorithm>
#include <limits>
#include "vault.h"

namespace red
{
namespace memory
{
	BigSizeAllocator::BigSizeAllocator()
		: m_virtualRange( NullVirtualRange() )
		, m_bookkeepingListVirtualRange( NullVirtualRange() )
		, m_systemAllocator( nullptr )
		, m_pageSize( 0 )
		, m_flags( Flags_CPU_Read_Write | Flags_No_Coalesce_Blocks )
		, m_allocationList( nullptr )
		, m_allocationListCount( 0 )
		, m_allocationListMaxCount( 0 )
		, m_freeList( nullptr )
		, m_freeListCount( 0 )
		, m_freeListMaxCount( 0 )
		, m_allocatorIsFull( false )
	{
		RED_TOUCH( m_padding );
	}

	BigSizeAllocator::~BigSizeAllocator()
	{}

	void BigSizeAllocator::Initialize( const BigSizeAllocatorParameter & parameter )
	{
		RED_MEMORY_ASSERT( parameter.systemAllocator, "Given system allocator does not exist." );
		RED_MEMORY_ASSERT( parameter.virtualRangeSize, "Virtual range size needs to be greater then 0." );

		m_systemAllocator = parameter.systemAllocator;
		m_flags = parameter.flags | Flags_No_Coalesce_Blocks;
		m_virtualRange = m_systemAllocator->ReserveVirtualRange( parameter.virtualRangeSize, m_flags );
		m_pageSize = static_cast< u32 >( m_systemAllocator->GetPageSize() );

		// commit pages for allocation & free list
		const u32 allocationListSize = RoundUp( static_cast< u32 >( sizeof( Block ) ), m_pageSize );
		m_allocationListMaxCount = allocationListSize / sizeof( Block );

		const u32 freeListSize = RoundUp( static_cast< u32 >( sizeof( VirtualRange ) ), m_pageSize );
		m_freeListMaxCount = freeListSize / sizeof( VirtualRange );

		m_bookkeepingListVirtualRange = m_systemAllocator->ReserveVirtualRange( allocationListSize + freeListSize, Flags_CPU_Read_Write | Flags_No_Coalesce_Blocks );
		SystemBlock block = m_systemAllocator->Commit( { m_bookkeepingListVirtualRange.start, allocationListSize + freeListSize }, Flags_CPU_Read_Write | Flags_No_Coalesce_Blocks );

		const u64 freeListAddress = block.address + allocationListSize;
		RED_MEMORY_ASSERT( IsAligned( block.address, alignof( Block ) ), "Allocation list has to be aligned as Block" );
		RED_MEMORY_ASSERT( IsAligned( freeListAddress, alignof( VirtualRange ) ), "Free list has to be aligned as VirtualRange" );

		m_allocationList = reinterpret_cast< Block* >( block.address );
		m_freeList = reinterpret_cast<VirtualRange*>( freeListAddress );
		*m_freeList = m_virtualRange;
		++m_freeListCount;
	}

	void BigSizeAllocator::Uninitialize()
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
		m_systemAllocator->ReleaseVirtualRange( m_bookkeepingListVirtualRange );
		m_systemAllocator->ReleaseVirtualRange( m_virtualRange );
	}

	Block BigSizeAllocator::Allocate( u32 size )
	{
		return AllocateAligned( size, DefaultAlignmentType::value );
	}

	Block BigSizeAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		if ( RED_UNLIKELY( m_allocatorIsFull ) )
		{
			return NullBlock();
		}

		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );
		RED_MEMORY_ASSERT( IsPowerOf2( alignment ), "Given alignment is not power of 2." );

		alignment = std::max( alignment, static_cast< u32 >( DefaultAlignmentType::value ) );
		size = RoundUp( size, m_pageSize );
		size = RoundUp( size, alignment );

		RED_MEMORY_ASSERT( IsAligned( size, m_pageSize ), "Requested allocation size is not rounded up to page size." );

		SystemBlock block = AllocateBlock( size, alignment );
		if ( block != NullSystemBlock() )
		{
			RED_MEMORY_ASSERT( IsAligned( block.address, alignment ), "Allocated block is not properly aligned." );
			return Block{ block.address, block.size };
		}
		else
		{
			return NullBlock();
		}
	}

	void BigSizeAllocator::Free( Block & block )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );

		if ( block.address )
		{
			block.size = GetBlockSize( block.address );
			DeallocateBlock( block );
		}
	}

	Block BigSizeAllocator::Reallocate( Block & block, u32 size )
	{
		return ReallocateAligned( block, size, DefaultAlignmentType::value );
	}

	Block BigSizeAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );

		if ( block == NullBlock() )
		{
			return AllocateAligned( size, alignment );
		}

		RED_MEMORY_ASSERT( OwnBlock( block.address ), "Block is not owned by this big size allocator." );

		if ( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		block.size = GetBlockSize( block.address );

		const u32 multiplier = static_cast< u32 >( block.size ) / m_pageSize;
		if ( size <= m_pageSize && multiplier == 1 )
		{
			return block;
		}

		size = RoundUp( size, m_pageSize );

		if ( block.size == size )
		{
			return block;
		}

		if ( block.address && static_cast< u64 >( size ) < block.size )
		{
			Block result = block;

			// shrink allocated block to new size
			const u64 freeBlockSize = result.size - size;

			m_systemAllocator->PartialDecommit({ result.address, result.size }, { result.address + size, freeBlockSize });

			RED_SCOPE_LOCK( m_lock );

			ReleaseRange( { result.address + size, result.address + size + freeBlockSize } );

			auto it = std::lower_bound( m_allocationList, m_allocationList + m_allocationListCount, result );
			RED_MEMORY_ASSERT( it != m_allocationList + m_allocationListCount, "Allocation list does not contain given allocation." );
			result.size = size;
			it->size = result.size;
			return result;
		}

#if !defined( RED_PLATFORM_ORBIS )
		// Growing policy does not work on Orbis and generates multiple issues:
		//	- it leaks the memory (sceKernelReleaseDirectMemory doesn't work properly with NO_COALESCE flag and release just first matching range
		//	- sceKernelReleaseDirectMemory also stomps on other committed ranges when given block size is bigger than (committed) range block size (all due to invalid mapping?)
		// Possible fix: call sceKernelVirtualQuery in the loop and release direct memory based on SceKernelVirtualQueryInfo

		else if ( block.address && static_cast< u64 >( size ) > block.size )
		{
			Block result = block;

			// try to extend block to new size if proper, contiguous, free block is available
			const u64 requiredFreeBlockSize = size - result.size;
			const VirtualRange range = { result.address, result.address + result.size };

			RED_SCOPE_LOCK( m_lock );

			auto beginIter = m_freeList;
			auto endIter = m_freeList + m_freeListCount;
			auto iter = std::lower_bound( beginIter, endIter, range );
			if ( iter != endIter )
			{
				auto & closestFreeRange = *iter;
				if ( range.end == closestFreeRange.start && requiredFreeBlockSize <= GetVirtualRangeSize( closestFreeRange ) )
				{
					const auto systemBlock = m_systemAllocator->Commit( { range.end, requiredFreeBlockSize }, m_flags );
					if ( systemBlock != NullSystemBlock() )
					{
						const u64 commitedBlockSize = systemBlock.size;
						RED_MEMORY_ASSERT( systemBlock.address == range.end, "Extended block has to be contiguous to current block." );
						RED_MEMORY_ASSERT( closestFreeRange.start + commitedBlockSize <= closestFreeRange.end, "Out of virtual range." );

						closestFreeRange.start += commitedBlockSize;
						if ( !GetVirtualRangeSize( closestFreeRange ) )
						{
							u32 index = static_cast< u32 >( std::distance( beginIter, iter ) );
							std::move( m_freeList + index + 1,
								m_freeList + m_freeListCount,
								m_freeList + index );
							--m_freeListCount;
						}

						auto it = std::lower_bound( m_allocationList, m_allocationList + m_allocationListCount, result );
						RED_MEMORY_ASSERT( it != m_allocationList + m_allocationListCount, "Allocation list does not contain given allocation." );
						it->size = result.size + systemBlock.size;
						result.size += systemBlock.size;
						return result;
					}
				}
			}
		}
#endif

		// otherwise fallback to normal allocate & free
		Block allocatedBlock = AllocateAligned( size, alignment );
		if ( allocatedBlock != NullBlock() )
		{
			MemcpyBlock( allocatedBlock.address, block.address, std::min( allocatedBlock.size, block.size ) );
			Free( block );
		}

		return allocatedBlock;
	}

	bool BigSizeAllocator::OwnBlock( u64 block ) const
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );

		return block >= m_virtualRange.start && block <= m_virtualRange.end;
	}

	u64 BigSizeAllocator::GetBlockSize( u64 block ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( block ), "Block is not owned by this big size allocator." );

		RED_SCOPE_SHARED_LOCK( m_lock );

		RED_MEMORY_ASSERT( m_allocationListCount != 0, "Allocation list is empty." );
		auto it = std::lower_bound( m_allocationList, m_allocationList + m_allocationListCount, Block{ block, 0 } );
		RED_MEMORY_ASSERT( it != m_allocationList + m_allocationListCount, "Allocation list does not contain given allocation." );
		return it->size;
	}

	void BigSizeAllocator::BuildMetrics( BigSizeAllocatorMetrics & metrics )
	{
		Memzero( &metrics, sizeof( metrics ) );

		u64 & consumedMemoryBytes = metrics.metrics.consumedMemoryBytes;
		u64 & largestBlockSize = metrics.metrics.largestBlockSize;
		u64 & smallestBlockSize = metrics.metrics.smallestBlockSize;
		u64 & freeListSize = metrics.freeListSize;

		if ( m_freeListCount > 0 )
		{
			smallestBlockSize = std::numeric_limits< u64 >::max();
		}

		for ( u32 i = 0; i < m_allocationListCount; ++i )
		{
			const Block & block = m_allocationList[i];
			consumedMemoryBytes += block.size;
		}

		for ( u32 i = 0; i < m_freeListCount; ++i )
		{
			const VirtualRange & range = m_freeList[i];
			const u64 rangeSize = GetVirtualRangeSize( range );

			if ( largestBlockSize < rangeSize )
			{
				largestBlockSize = rangeSize;
			}

			if ( smallestBlockSize > rangeSize )
			{
				smallestBlockSize = rangeSize;
			}

			freeListSize += rangeSize;
		}

		metrics.metrics.bookKeepingBytes = GetVirtualRangeSize( m_bookkeepingListVirtualRange );
		metrics.metrics.consumedSystemMemoryBytes = consumedMemoryBytes + metrics.metrics.bookKeepingBytes;
		metrics.virtualRangeSize = GetVirtualRangeSize( m_virtualRange );
		metrics.allocationListCount = m_allocationListCount;
		metrics.allocationListMaxCount = m_allocationListMaxCount;
		metrics.freeListCount = m_freeListCount;
		metrics.freeListMaxCount = m_freeListMaxCount;
	}

	void BigSizeAllocator::SerializeMetrics( Serializer & serializer )
	{
		BigSizeAllocatorMetrics metrics;
		BuildMetrics( metrics );

		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

	u32 BigSizeAllocator::GetPageSize() const
	{
		RED_MEMORY_ASSERT( m_systemAllocator, "Allocator was not initialized." );

		return m_pageSize;
	}

	void BigSizeAllocator::InternalMarkBigSizeAllocatorAsFull()
	{
		m_allocatorIsFull = true;
	}

	SystemBlock BigSizeAllocator::AllocateBlock( u32 size, u32 alignment )
	{
		RED_SCOPE_LOCK( m_lock );

		VirtualRange range = ReserveRange( size, alignment );
		if ( range == NullVirtualRange() )
		{
			return NullSystemBlock();
		}

		SystemBlock block = { range.start, size };
		block = m_systemAllocator->Commit( block, m_flags );
		if( block == NullSystemBlock() )
		{
			ReleaseRange( range );
		}
		else
		{
			RED_MEMORY_ASSERT( block.size == size, "Failed to allocate requested amount of memory." );
			RED_MEMORY_ASSERT( m_allocationListCount < m_allocationListMaxCount, "Out of bound allocation list." );
			Block & allocationBlock = m_allocationList[ m_allocationListCount++ ];
			allocationBlock.address = block.address;
			allocationBlock.size = block.size;
			std::sort( m_allocationList, m_allocationList + m_allocationListCount );
		}

		return block;
	}

	void BigSizeAllocator::DeallocateBlock( Block & block )
	{
		RED_MEMORY_ASSERT( OwnBlock( block.address ), "Block is not owned by this big size allocator." );

		m_systemAllocator->Decommit( { block.address, block.size } );

		RED_SCOPE_LOCK( m_lock );

		ReleaseRange( { block.address, block.address + block.size } );

		RED_MEMORY_ASSERT( m_allocationListCount > 0, "Allocation list is empty." );
		auto it = std::lower_bound( m_allocationList, m_allocationList + m_allocationListCount, block );
		RED_MEMORY_ASSERT( it != m_allocationList + m_allocationListCount, "Allocation list does not contain given allocation." );
		const u64 freeMarker = std::numeric_limits< u64 >::max();
		it->address = freeMarker;
		it->size = freeMarker;
		std::sort( m_allocationList, m_allocationList + m_allocationListCount );
		--m_allocationListCount;
	}

	VirtualRange BigSizeAllocator::ReserveRange( u32 size, u32 alignment )
	{
		VirtualRange result = NullVirtualRange();

		{
			VirtualRange newFreeRange = NullVirtualRange();
			VirtualRange* matchedRange = nullptr;

			for ( u32 index = 0; index < m_freeListCount; ++index )
			{
				VirtualRange & range = m_freeList[ index ];
				RED_MEMORY_ASSERT( range.start <= range.end, "Invalid memory range" );

				const u64 allocationAddress = RoundUp( range.start, static_cast< u64 >( alignment ) );
				const u64 rangeSize = range.end > allocationAddress ? range.end - allocationAddress : 0;
				if ( size <= rangeSize )
				{
					result = { allocationAddress, allocationAddress + size };
					if ( allocationAddress == range.start )
					{
						// range.start has the same alignment as allocationAddress

						range.start = result.end;
						RED_MEMORY_ASSERT( range.start <= range.end, "Invalid memory range" );

						if ( !GetVirtualRangeSize( range ) )
						{
							std::move( m_freeList + index + 1,
								m_freeList + m_freeListCount,
								m_freeList + index );
							--m_freeListCount;
						}
					}
					else
					{
						// range.start has different alignment than allocationAddress

						const u64 newFreeRangeSize = range.end - result.end;
						if ( newFreeRangeSize > 0 )
						{
							matchedRange = index + 1 < m_freeListCount ? &m_freeList[index + 1] : nullptr;
							newFreeRange = { result.end, range.end };
						}

						range.end = allocationAddress;
						RED_MEMORY_ASSERT( range.start <= range.end, "Invalid memory range" );
					}

					break;
				}
			}

			if ( newFreeRange != NullVirtualRange() )
			{
				RED_MEMORY_ASSERT( m_freeListCount < m_freeListMaxCount, "Out of bound allocation list." );
				if ( matchedRange )
				{
					std::move_backward( matchedRange, m_freeList + m_freeListCount, m_freeList + m_freeListCount + 1 );
					*matchedRange = newFreeRange;
					++m_freeListCount;
				}
				else
				{
					m_freeList[m_freeListCount++] = newFreeRange;
				}
			}
		}

		return result;
	}

	void BigSizeAllocator::ReleaseRange( const VirtualRange & range )
	{
		auto beginIter = m_freeList;
		auto endIter = m_freeList + m_freeListCount;
		auto iter = std::lower_bound( beginIter, endIter, range );

		RED_MEMORY_ASSERT( iter != endIter, "Out of bound virtual range. Last free range is the absolute limit." );

		VirtualRange & previous = iter == beginIter ? *iter : *( iter - 1 );
		VirtualRange & next = iter == beginIter ? *beginIter : *iter;

		if ( iter != beginIter && previous.end == range.start )
		{
			// Can merge with previous
			previous.end = range.end;
			// Can merge also with next?
			if ( next.start == previous.end )
			{
				previous.end = next.end;
				std::move( iter + 1, endIter, iter );
				--m_freeListCount;
			}
		}
		else if ( next.start == range.end )
		{
			// Can't merge with previous, but can merge with next
			next.start = range.start;
		}
		else
		{
			RED_MEMORY_ASSERT( m_freeListCount < m_freeListMaxCount, "Too many free ranges." );

			// Can't merge with previous or next.
			std::move_backward( iter, endIter, endIter + 1 );
			*iter = range;
			++m_freeListCount;
		}
	}


	BigSizeAllocator& AcquireBigSizeAllocator()
	{
		return AcquireVault().GetBigSizeAllocator();
	}
}
}
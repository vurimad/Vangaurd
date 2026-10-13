/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "linearAllocator.h"
#include "assert.h"

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
#include <mat.h>
#endif

namespace red
{
namespace memory
{
	struct LinearAllocatorHeader
	{
		u32 blockSize;
	};

	const u32 c_linearAllocatorHeaderSize = sizeof( LinearAllocatorHeader );

	LinearAllocator::LinearAllocator()
		: m_startAddress( 0 )
		, m_endAddress( 0 )
		, m_position( 0 )
	{}

	LinearAllocator::~LinearAllocator()
	{
		Reset();
	}

	void LinearAllocator::Initialize( const LinearAllocatorParameter& param )
	{
		RED_MEMORY_ASSERT( param.block.size, "Buffer size needs to be at least 1." );
		RED_MEMORY_ASSERT( param.block.address, "Buffer needs to be valid." );

		m_startAddress = param.block.address;
		m_endAddress = m_startAddress + param.block.size;
		m_position = m_startAddress;
	}

	Block LinearAllocator::Allocate( u32 size )
	{
		return AllocateAligned( size, DefaultAlignmentType::value );
	}

	Block LinearAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		alignment = std::max( alignment, static_cast< u32 >( DefaultAlignmentType::value ) );
		size = RoundUp( size, alignment );

		u64 address = 0;

		while( 1 )
		{
			atomic::TAtomic64 position = atomic::Or64( &m_position, atomic::TAtomic64( 0 ) );

			address = RoundUp( position + c_linearAllocatorHeaderSize, static_cast< u64 >( alignment ) );

			const u64 newPosition = ( address + size );
			if ( newPosition > m_endAddress )
			{
				return NullBlock();
			}

			if( atomic::CompareExchange64( &m_position, newPosition, position ) == position )
			{
				// ctremblay: if we get here, address is reserved for this thread, move along !
				break;
			}
			else
			{ /* ctremblay: if we get here another thread beat us into acquiring address. Try again. */}
		}

		LinearAllocatorHeader * header = reinterpret_cast< LinearAllocatorHeader* >( address - c_linearAllocatorHeaderSize );
		header->blockSize = size;
		return { address, size };
	}

	void LinearAllocator::Free( Block& block )
	{
		RED_MEMORY_ASSERT( OwnBlock( block.address ), "Block is not owned by this LinearAllocator." );
		LinearAllocatorHeader * header = reinterpret_cast< LinearAllocatorHeader* >( block.address - c_linearAllocatorHeaderSize );
		block.size = header->blockSize;
	}

	Block LinearAllocator::Reallocate( Block& block, u32 size )
	{
		return ReallocateAligned( block, size, DefaultAlignmentType::value );
	}

	Block LinearAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		if ( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		if( block.address == 0 )
		{
			return AllocateAligned( size, alignment );
		}

		alignment = std::max( alignment, static_cast< u32 >( DefaultAlignmentType::value ) );
		size = RoundUp( size, alignment );
		block.size = GetBlockSize( block.address );

		RED_FATAL_ASSERT( IsAligned( block.address, alignment ), "Linear Allocator do not support Realloc with different alignment." );

		if( size == block.size )
		{
			return block;
		}
		else if( size < block.size )
		{
			return block;
		}
		else
		{
			Block grownBlock = GrowBlock( block, size );

			if( grownBlock != NullBlock() )
			{
				return grownBlock;	
			}
			else
			{
				// GrowBlock failed, go through regular allocate
				Block allocBlock = AllocateAligned( size, alignment );
				if( allocBlock != NullBlock() )
				{
					MemcpyBlock( allocBlock.address, block.address, block.size );
					Free( block );
					return allocBlock;
				}
			}
		}

		// Everything failed. Most likely OOM.
		return NullBlock();
	}

	Block LinearAllocator::GrowBlock( const Block & block, u32 size )
	{
		while( 1 )
		{
			atomic::TAtomic64 position = atomic::Or64( &m_position, atomic::TAtomic64( 0 ) );
			
			if( position == static_cast< atomic::TAtomic64 >( block.address + block.size ) )
			{
				// current position is contiguous to block address. Try to grow block
				const u64 newPosition = ( block.address + size );

				if ( newPosition > m_endAddress )
				{
					// OOM, cannot grow! 
					return NullBlock();
				}

				if( atomic::CompareExchange64( &m_position, newPosition, position ) == position )
				{
					// Grow succeeded !
					LinearAllocatorHeader * header = reinterpret_cast< LinearAllocatorHeader* >( block.address - c_linearAllocatorHeaderSize );
					header->blockSize = size;
					return { block.address, size };
				}
				else
				{ /* Grow failed. Try again. */ }
			}
			else
			{
				// Cannot Grow block. exit loop
				break;
			}
		}

		return NullBlock();
	}

	bool LinearAllocator::OwnBlock( u64 block ) const
	{
		return block >= m_startAddress && block < m_endAddress;
	}

	u64 LinearAllocator::GetBlockSize( u64 address ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( address ), "Block is not owned by this LinearAllocator." );
		LinearAllocatorHeader * header = reinterpret_cast< LinearAllocatorHeader* >( address - c_linearAllocatorHeaderSize );
		return header->blockSize;
	}

	void LinearAllocator::Reset()
	{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		if ( m_position != m_startAddress )
		{
			sceMatFreeRange( reinterpret_cast< void* >( m_startAddress ), m_position - m_startAddress );
		}
#endif
		m_position = m_startAddress;
	}

	void LinearAllocator::UpdateBuffer( const Block& block )
	{
		if ( m_startAddress != block.address )
		{
			m_startAddress = block.address;
		}

		m_endAddress = m_startAddress + block.size;
	}

	void LinearAllocator::BuildMetrics( LinearAllocatorMetrics& metrics )
	{
		Memzero( &metrics, sizeof( metrics ) );

		metrics.metrics.bookKeepingBytes = c_linearAllocatorHeaderSize;
		metrics.metrics.consumedSystemMemoryBytes = m_endAddress - m_startAddress;
		metrics.metrics.consumedMemoryBytes = m_position - m_startAddress;

		const u32 freeBlockSize = static_cast< u32 >( m_endAddress - m_position );
		metrics.metrics.smallestBlockSize = freeBlockSize >= DefaultAlignmentType::value ? DefaultAlignmentType::value : 0;
		metrics.metrics.largestBlockSize = freeBlockSize;
	}

	void LinearAllocator::SerializeMetrics( Serializer& serializer )
	{
		LinearAllocatorMetrics metrics;
		BuildMetrics( metrics );

		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}
}
}

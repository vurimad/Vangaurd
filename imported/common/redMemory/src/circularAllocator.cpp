/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "circularAllocator.h"
#include "assert.h"
#include "../../redSystem/include/unitTestMode.h"

namespace red
{
namespace memory
{
namespace
{
	void MarkAllocatedBlock( const Block & block )
	{
#ifdef RED_MEMORY_UNIT_TEST
		if ( UnitTestMode() )
		{
			MemsetBlock( block, c_circularAllocatorUnitTestAllocFiller );
		}
#endif

		RED_UNUSED( block );
	}

	void MarkFreeBlock( const Block & block )
	{
#ifdef RED_MEMORY_UNIT_TEST
		if ( UnitTestMode() )
		{
			MemsetBlock( block, c_circularAllocatorUnitTestFreeFiller );
		}
#endif

		RED_UNUSED( block );
	}
}

	struct CircularAllocatorHeader
	{
		u32 blockSize;
		u32 marker;
	};

	const u32 c_circularAllocatorHeaderSize = sizeof( CircularAllocatorHeader );
	const u32 c_circularAllocatorHeaderAllocateMarker = 0xBEEFC0DE;
	const u32 c_circularAllocatorHeaderFreeMarker = 0xDEADC0DE;

	CircularAllocator::CircularAllocator()
		: m_startAddress( 0 )
		, m_endAddress( 0 )
		, m_position( 0 )
	{}

	CircularAllocator::~CircularAllocator()
	{}

	void CircularAllocator::Initialize( const CircularAllocatorParameter& param )
	{
		RED_MEMORY_ASSERT( param.block.size, "Buffer size needs to be at least 1." );
		RED_MEMORY_ASSERT( param.block.address, "Buffer needs to be valid." );

		m_startAddress = param.block.address;
		m_endAddress = m_startAddress + param.block.size;
		m_position.SetValue( m_startAddress );
	}

	Block CircularAllocator::Allocate( u32 size )
	{
		return AllocateAligned( size, DefaultAlignmentType::value );
	}

	Block CircularAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( m_startAddress, "Allocator was not Initialized." );

		alignment = std::max( alignment, static_cast< u32 >( DefaultAlignmentType::value ) );
		size = RoundUp( size, alignment );

		u64 address = 0;

		while ( 1 )
		{
			u64 position = m_position.GetValue();

			address = RoundUp( position + c_circularAllocatorHeaderSize, static_cast< u64 >( alignment ) );

			u64 newPosition = address + size;
			if ( newPosition > m_endAddress )
			{
				// handle loop around
				address = RoundUp( m_startAddress + c_circularAllocatorHeaderSize, static_cast< u64 >( alignment ) );
				newPosition = address + size;

				if ( newPosition > m_endAddress )
				{
					// not enough space for requested allocation
					return NullBlock();
				}
			}

			if ( m_position.CompareExchange( newPosition, position ) == position )
			{
				// if we get here, address is reserved for this thread, move along
				break;
			}
			else
			{ /* if we get here another thread beat us into acquiring address. Try again. */
			}
		}

		CircularAllocatorHeader* header = reinterpret_cast< CircularAllocatorHeader* >( address - c_circularAllocatorHeaderSize );
		header->blockSize = size;
		header->marker = c_circularAllocatorHeaderAllocateMarker;
		const Block block = { address, size };
		MarkAllocatedBlock( block );
		return block;
	}

	void CircularAllocator::Free( Block& block )
	{
		RED_MEMORY_ASSERT( OwnBlock( block.address ), "Block is not owned by this CircularAllocator." );

		CircularAllocatorHeader* header = reinterpret_cast< CircularAllocatorHeader* >( block.address - c_circularAllocatorHeaderSize );
		RED_MEMORY_ASSERT( header->marker == c_circularAllocatorHeaderAllocateMarker, "Block was already deleted, or underrun occured." );
		block.size = header->blockSize;
		header->marker = c_circularAllocatorHeaderFreeMarker;
		MarkFreeBlock( block );
	}

	Block CircularAllocator::Reallocate( Block& block, u32 size )
	{
		return ReallocateAligned( block, size, DefaultAlignmentType::value );
	}

	Block CircularAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		if ( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		if ( block.address == 0 )
		{
			return AllocateAligned( size, alignment );
		}

		alignment = std::max( alignment, static_cast< u32 >( DefaultAlignmentType::value ) );
		size = RoundUp( size, alignment );
		block.size = GetBlockSize( block.address );

		RED_MEMORY_ASSERT( IsAligned( block.address, alignment ), "ReallocateAligned do not support changing original block alignment." );

		if ( size <= block.size )
		{
			return block;
		}
		else
		{
			Block grownBlock = GrowBlock( block, size );
			if ( grownBlock != NullBlock() )
			{
				return grownBlock;
			}
			else
			{
				// GrowBlock failed go through regular allocate
				Block allocBlock = AllocateAligned( size, alignment );
				if ( allocBlock != NullBlock() )
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

	Block CircularAllocator::GrowBlock( const Block& block, u32 size )
	{
		while ( 1 )
		{
			u64 position = m_position.GetValue();
			if ( position == block.address + block.size )
			{
				// current position is contiguous to block address. Try to grow block
				const u64 newPosition = block.address + size;

				if ( newPosition > m_endAddress )
				{
					// OOM, cannot grow
					return NullBlock();
				}

				if ( m_position.CompareExchange( newPosition, position ) == position )
				{
					// Grow succeeded
					
					CircularAllocatorHeader* header = reinterpret_cast< CircularAllocatorHeader* >( block.address - c_circularAllocatorHeaderSize );
					header->blockSize = size;
					return { block.address, size };
				}
				else
				{ /* Grow failed. Try again. */ }
			}
			else
			{
				// Cannot grow block. Exit loop
				break;
			}
		}

		return NullBlock();
	}

	Bool CircularAllocator::OwnBlock( u64 block ) const
	{
		RED_MEMORY_ASSERT( m_startAddress, "Allocator was not Initialized." );
		return block >= m_startAddress && block < m_endAddress;
	}

	u64 CircularAllocator::GetBlockSize( u64 address ) const
	{
		RED_MEMORY_ASSERT( OwnBlock( address ), "Block is not owned by this CircularAllocator." );
		CircularAllocatorHeader* header = reinterpret_cast< CircularAllocatorHeader* >( address - c_circularAllocatorHeaderSize );
		return header->blockSize;
	}

	void CircularAllocator::BuildMetrics( CircularAllocatorMetrics& metrics )
	{
		Memzero( &metrics, sizeof( metrics ) );

		metrics.metrics.consumedSystemMemoryBytes = 0;
		metrics.metrics.consumedMemoryBytes = m_endAddress - m_startAddress;
		const auto freeBlockSize = m_position.GetValue() - m_startAddress;
		metrics.metrics.smallestBlockSize = freeBlockSize;
		metrics.metrics.largestBlockSize = freeBlockSize;
	}

	void CircularAllocator::SerializeMetrics( Serializer& serializer )
	{
		CircularAllocatorMetrics metrics;
		BuildMetrics( metrics );

		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

}
}
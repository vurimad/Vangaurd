/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "fixedSizeAllocator.h"
#include "assert.h"
#include "utils.h"

namespace red
{
namespace memory
{

	LocklessFixedSizeAllocator::LocklessFixedSizeAllocator()
		:	m_firstFree( 0 ),
			m_endAddress( 0 ),
			m_blockSize( 0 ),
			m_blockAlignment( 0 ),
			m_range()
	{}

	LocklessFixedSizeAllocator::~LocklessFixedSizeAllocator()
	{}

	void LocklessFixedSizeAllocator::Initialize( const LocklessFixedSizeAllocatorParameter & param )
	{
		RED_MEMORY_ASSERT( IsPowerOf2( param.blockAlignment ), "Alignment must be power of two." );
		RED_MEMORY_ASSERT( param.block.address, "No buffer provided." );
		RED_MEMORY_ASSERT( param.block.size, "Buffer size can't be 0." );

		m_range = param.range;
		m_blockAlignment = param.blockAlignment;

#ifdef RED_MEMORY_ENABLE_HOOKS
		// Overrun Hooks will need 8 more bytes.
		m_blockSize = RoundUp( param.blockSize + 8, m_blockAlignment );
#else
		m_blockSize = RoundUp( param.blockSize, m_blockAlignment );
#endif
		
		m_firstFree = RoundUp( param.block.address, static_cast< u64 >( m_blockAlignment ) );
		m_firstFree += 1;  // Magic bit to know if memory was previously allocate or not.
		m_endAddress = param.block.address + param.block.size;
	}
	
	void LocklessFixedSizeAllocator::Uninitialize()
	{}

	Block LocklessFixedSizeAllocator::Allocate( u32 size )
	{
		RED_MEMORY_ASSERT( size <= m_blockSize, "Cannot allocate %d byte. Fix size is %d.", size, m_blockSize );
		RED_UNUSED( size );

		u64 address = 0;

		while ( 1 )
		{
			atomic::TAtomic64 value = atomic::Or64( &m_firstFree, atomic::TAtomic64( 0 ) );
			RED_MEMORY_ASSERT( value != 0, "First free block address can not be 0." );

			u64 newFirstFree = 0;

			if ( value & 1 )
			{
				address = value - 1;

				if ( address + m_blockSize > m_endAddress )
				{
					return NullBlock();
				}

				newFirstFree = value + m_blockSize;
			}
			else
			{
				newFirstFree = *reinterpret_cast< u64 * >( value );
				address = value;
			}

			if ( atomic::CompareExchange64( &m_firstFree, newFirstFree, value ) == value )
			{
				// if we get here, address is reserved for this thread, move along !
				break;
			}
			else
			{ /* if we get here another thread beat us into acquiring address. Try again. */ }
		}

		Block block = { address, m_blockSize };

		return block;
	}

	Block LocklessFixedSizeAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( alignment <= m_blockAlignment, "Alignment can't be more than default one." );
		RED_UNUSED( alignment );
		return Allocate( size );
	}

	void LocklessFixedSizeAllocator::Free( Block & block )
	{
		if( block.address )
		{
			RED_MEMORY_ASSERT( OwnBlock( block.address ), "Block is owned by this FixedSizeAllocator." );

			block.size = m_blockSize;

			while ( 1 )
			{
				atomic::TAtomic64 firstFree = atomic::Or64( &m_firstFree, atomic::TAtomic64( 0 ) );
				RED_MEMORY_ASSERT( firstFree != 0, "First free block address can not be 0." );

				*reinterpret_cast< u64* >( block.address ) = firstFree;

				if ( atomic::CompareExchange64( &m_firstFree, block.address, firstFree ) == firstFree )
				{
					// if we get here, address is reserved for this thread, move along !
					break;
				}
				else
				{ /* if we get here another thread beat us into acquiring address. Try again. */ }
			}
		}
	}

	Block LocklessFixedSizeAllocator::Reallocate( Block & block, u32 size )
	{
		RED_MEMORY_ASSERT( size <= m_blockSize, "Cannot allocate %d byte. Fix size is %d.", size, m_blockSize );
	
		if( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		if( !block.address )
		{
			return Allocate( size );
		}
		
		block.size = m_blockSize;
		return block;
	}

	Block LocklessFixedSizeAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		RED_MEMORY_ASSERT( alignment <= m_blockAlignment, "Alignment can't be more than default one." );
		RED_UNUSED( alignment );
		return Reallocate( block, size );
	}

	bool LocklessFixedSizeAllocator::OwnBlock( u64 block ) const
	{
		RED_MEMORY_ASSERT( IsInitialized(), "FixedSizeAllocator was not initialized." );
		return block - m_range.start < GetVirtualRangeSize( m_range ); 
	}

	bool LocklessFixedSizeAllocator::IsInitialized() const
	{
		return m_firstFree != 0;
	}

	u32 LocklessFixedSizeAllocator::GetBlockSize() const
	{
		return m_blockSize;
	}

	u32 LocklessFixedSizeAllocator::GetTotalBlockCount() const
	{
		return m_blockSize ? static_cast< u32 >( GetVirtualRangeSize( m_range ) / m_blockSize ) : 0; 
	}

	void LocklessFixedSizeAllocator::UpdateEndAddress( u64 endAddress )
	{
		m_endAddress = endAddress;
	}

	void LocklessFixedSizeAllocator::BuildMetrics( FixedSizeAllocatorMetrics & metrics )
	{
		Memzero( &metrics, sizeof( metrics ) );

#ifdef RED_MEMORY_ENABLE_HOOKS
		metrics.metrics.bookKeepingBytes = 8;
#endif

		metrics.metrics.consumedSystemMemoryBytes = m_endAddress - m_range.start;

		const u64 value = m_firstFree & 1 ? m_firstFree - 1 : m_firstFree;
		if ( value + m_blockSize <= m_range.end )
		{
			metrics.metrics.smallestBlockSize = m_blockSize;
			metrics.metrics.largestBlockSize =  m_blockSize ? ( static_cast< u64 >( ( m_range.end - value ) / m_blockSize ) * m_blockSize ) : 0;
		}

		metrics.metrics.consumedMemoryBytes = value - m_range.start;
	}

	void LocklessFixedSizeAllocator::SerializeMetrics( Serializer & serializer )
	{
		FixedSizeAllocatorMetrics metrics;
		BuildMetrics( metrics );

		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}
}
}
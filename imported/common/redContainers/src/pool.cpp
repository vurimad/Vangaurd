/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "pool.h"

namespace red
{

//////////////////////////////////////////////////////////////////////////

Pool::Pool( Pool&& other )
	: m_buffer( other.m_buffer )
	, m_capacity( other.m_capacity )
	, m_blockSize( other.m_blockSize )
	, m_firstFreeBlockIndex( other.m_firstFreeBlockIndex )
	, m_numberOfInitialized( other.m_numberOfInitialized )
{
	other.m_buffer = nullptr;
	other.m_capacity = 0;
	other.m_blockSize = 0;
	other.m_firstFreeBlockIndex = INVALID_INDEX;
	other.m_numberOfInitialized = 0;
}

Pool& Pool::operator=( Pool&& other )
{
	Pool( std::move( other ) ).Swap( *this );
	return *this;
}

void Pool::Swap( Pool& other )
{
	using std::swap;
	swap( m_buffer, other.m_buffer );
	swap( m_capacity, other.m_capacity );
	swap( m_blockSize, other.m_blockSize );
	swap( m_firstFreeBlockIndex, other.m_firstFreeBlockIndex );
	swap( m_numberOfInitialized, other.m_numberOfInitialized );
}

//////////////////////////////////////////////////////////////////////////

void Pool::Init( void* buffer, Uint32 bufferSize, Uint32 blockSize )
{
	RED_FATAL_ASSERT( ( (uintptr_t)buffer & 3 ) == 0 , "Pool memory buffer must be at least 4-bytes aligned" );
	RED_FATAL_ASSERT( ( blockSize & 3 ) == 0 , "Pool element size must be at least 4-bytes aligned" );
	RED_FATAL_ASSERT( blockSize >= 4, "Block has to be at least 4-bytes long" );
	RED_FATAL_ASSERT( buffer != nullptr || bufferSize == 0, "Size > 0 requires valid memory" );

	m_buffer = buffer;
	m_blockSize = blockSize;
	m_capacity = bufferSize / blockSize;
	m_firstFreeBlockIndex = ( m_buffer != nullptr ) ? 0 : INVALID_INDEX;
	m_numberOfInitialized = 0;
}

void Pool::Deinit()
{
	m_buffer = nullptr;
	m_blockSize = 0;
	m_capacity = 0;
	m_firstFreeBlockIndex = INVALID_INDEX;
	m_numberOfInitialized = 0;
}

void Pool::Clear()
{
	m_firstFreeBlockIndex = ( m_buffer != nullptr ) ? 0 : INVALID_INDEX;
	m_numberOfInitialized = 0;
}

//////////////////////////////////////////////////////////////////////////

void* Pool::AllocateBlock()
{
	if ( Full() )
	{
		return nullptr;
	}
	Entry* allocatedBlock = reinterpret_cast< Entry* >( GetBlock( m_firstFreeBlockIndex ) );
	UpdateNextFreeBlock( allocatedBlock );
	return allocatedBlock;
}

Uint32 Pool::AllocateBlockIndex()
{
	if ( Full() )
	{
		return INVALID_INDEX;
	}
	const Uint32 allocatedBlockIndex = m_firstFreeBlockIndex;
	Entry* allocatedBlock = reinterpret_cast< Entry* >( GetBlock( m_firstFreeBlockIndex ) );
	UpdateNextFreeBlock( allocatedBlock );
	return allocatedBlockIndex;
}

void Pool::UpdateNextFreeBlock( const Entry* allocatedBlock )
{
	// Idea behind updating next free block is simple:
	// - If next free block index is equal to the number of already initialized blocks,
	//   it means that ALL blocks before the index are already allocated,
	//   so we need to "initialize" another one and increase next free block index.
	// - If on the other hand the index is lower than number of already initialized blocks
	//   it means there's a free block before. Since it had to be previously freed,
	//   it is also linked into free blocks list, so the next free block should be next block from the list.
	if ( m_firstFreeBlockIndex == m_numberOfInitialized )
	{
		m_numberOfInitialized++;
		m_firstFreeBlockIndex = m_numberOfInitialized < m_capacity ? m_firstFreeBlockIndex + 1 : INVALID_INDEX;
	}
	else
	{
		m_firstFreeBlockIndex = allocatedBlock->m_next;
	}
}

void Pool::FreeBlock( const void* block )
{
	RED_FATAL_ASSERT( m_buffer <= block && block < red::OffsetPtr( m_buffer, DataCapacity() ), "Block doesn't belong to the pool" );	
	Entry* releasedBlock = reinterpret_cast< Entry* >( const_cast< void* >( block ) );
	releasedBlock->m_next = m_firstFreeBlockIndex;
	m_firstFreeBlockIndex = GetBlockIndex( block );
}

void Pool::FreeBlockIndex( Uint32 index )
{
	FreeBlock( GetBlock( index ) );
}

//////////////////////////////////////////////////////////////////////////

Uint32 Pool::CountFreeBlocks() const
{
	Uint32 count = 0;
	Uint32 nextFreeIndex = m_firstFreeBlockIndex;
	while ( nextFreeIndex != INVALID_INDEX && nextFreeIndex < m_numberOfInitialized )
	{
		count++;
		const Entry* entry = reinterpret_cast< const Entry* >( GetBlock( nextFreeIndex ) );
		nextFreeIndex = entry->m_next;
	}
	return ( m_capacity - m_numberOfInitialized ) + count;
}

} // red

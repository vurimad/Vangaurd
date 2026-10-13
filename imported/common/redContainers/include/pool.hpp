/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redSystem/include/assert.h"

namespace red
{

//////////////////////////////////////////////////////////////////////////

constexpr Pool::Pool()
	: m_buffer( nullptr )
	, m_capacity( 0 )
	, m_blockSize( 0 )
	, m_firstFreeBlockIndex( static_cast< Uint32 >( INVALID_INDEX ) )
	, m_numberOfInitialized( 0 )
{}

//////////////////////////////////////////////////////////////////////////

RED_INLINE void* Pool::GetBlock( Uint32 index )
{
	RED_FATAL_ASSERT( index < m_capacity, "Index out of bounds" );
	return red::OffsetPtr( m_buffer, index * m_blockSize );
}

RED_INLINE const void* Pool::GetBlock( Uint32 index ) const
{
	RED_FATAL_ASSERT( index < m_capacity, "Index out of bounds" );
	return red::OffsetPtr( m_buffer, index * m_blockSize );
}

RED_INLINE Uint32 Pool::GetBlockIndex( const void* block ) const
{
	RED_FATAL_ASSERT( m_buffer <= block && block < red::OffsetPtr( m_buffer, DataCapacity() ), "Block doesn't belong to the pool" );
	return red::ByteDistance< Uint32 >( m_buffer, block ) / m_blockSize;
}

//////////////////////////////////////////////////////////////////////////

template < typename T >
void Pool::Upsize( void* newBuffer, Uint32 newCapacity )
{
	typedef typename red::policies::MoveConstructorExecutorSelector< T >::Type	MoveConstructorExecutor;
	typedef typename red::policies::DestructorExecutorSelector< T >::Type		DestructorExecutor;

	if ( newCapacity > m_capacity )
	{
		RED_ASSERT( m_firstFreeBlockIndex == INVALID_INDEX ); // For now only supporting upsizing when full

		// Move old elements into new memory
		MoveConstructorExecutor::Execute( reinterpret_cast< T* >( newBuffer ), reinterpret_cast< T* >( m_buffer ), m_capacity );
		DestructorExecutor::Execute( reinterpret_cast< T* >( m_buffer ), m_capacity );

		const Uint32 oldCapacity = m_capacity;
		m_buffer = newBuffer;
		m_capacity = newCapacity;
		m_firstFreeBlockIndex = oldCapacity;
		m_numberOfInitialized = oldCapacity;	// this actually is redundant, but let's leave it for the sake of clarity
	}
}

} // red

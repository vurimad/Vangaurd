/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "arrayImplUtils.h"

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE ArrayWrapperBase< TElement, RuntimeDataLocation >::ArrayWrapperBase( void* buffer, Uint32 capacity )
	: RuntimeDataLocation( 0 )
	, m_buffer( static_cast< TElement* >( buffer ) )
	, m_capacity( capacity )
{}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE ArrayWrapperBase< TElement, RuntimeDataLocation >::ArrayWrapperBase( void* buffer, Uint32 capacity, Uint32& sizeRef )
	: RuntimeDataLocation( sizeRef )
	, m_buffer( static_cast< TElement* >( buffer ) )
	, m_capacity( capacity )
{}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE TElement& ArrayWrapperBase< TElement, RuntimeDataLocation >::operator[]( Uint32 i )
{
	RED_FATAL_ASSERT( i < m_size, "Array: Out of bounds. Cannot access item %i as the array is only size %u", i, m_size );
	return m_buffer[ i ];
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE const TElement& ArrayWrapperBase< TElement, RuntimeDataLocation >::operator[]( Uint32 i ) const
{
	RED_FATAL_ASSERT( i < m_size, "Array: Out of bounds. Cannot access item %i as the array is only size %u", i, m_size );
	return m_buffer[ i ];
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE TElement& ArrayWrapperBase< TElement, RuntimeDataLocation >::Front()
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access frist item - array is empty!" );
	return m_buffer[ 0 ];
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE const TElement& ArrayWrapperBase< TElement, RuntimeDataLocation >::Front() const
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access frist item - array is empty!" );
	return m_buffer[ 0 ];
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE TElement& ArrayWrapperBase< TElement, RuntimeDataLocation >::Back()
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access last item - array is empty!" );
	return m_buffer[ m_size - 1 ];
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE const TElement& ArrayWrapperBase< TElement, RuntimeDataLocation >::Back() const
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access last item - array is empty!" );
	return m_buffer[ m_size - 1 ];
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE Bool ArrayWrapperBase< TElement, RuntimeDataLocation >::operator==( const ArrayWrapperBase& other  ) const
{
	return ( m_size == other.m_size && red::Memcmp( m_buffer, other.m_buffer, m_size * sizeof( TElement ) ) == 0 );
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE Bool ArrayWrapperBase< TElement, RuntimeDataLocation >::operator!=( const ArrayWrapperBase& other ) const
{
	return !( *this == other );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE void ArrayWrapperBase< TElement, RuntimeDataLocation >::PushBack( const TElement& element )
{
	RED_FATAL_ASSERT( m_size + 1 <= m_capacity, "Cannot resize ArrayWrapper over its maxium capacity" );
	m_buffer[ m_size++ ] = element;
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE TElement ArrayWrapperBase< TElement, RuntimeDataLocation >::PopBack()
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access last item - array is empty!" );	
	return m_buffer[ --m_size ];
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE void ArrayWrapperBase< TElement, RuntimeDataLocation >::InsertAt( Uint32 index, const TElement& element )
{
	RED_FATAL_ASSERT( index <= m_size, "Array: Out of bounds. Cannot access item %i as the array is only size %u", index, m_size );
	RED_FATAL_ASSERT( m_size + 1 <= m_capacity, "Cannot resize ArrayWrapper over its maxium capacity" );
	
	if ( index < m_size )
	{
		ArrayImplUtils::MoveForwards( m_buffer + index, 1, m_size - index );
	}
	m_buffer[ index ] = element;
	m_size++;
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE void ArrayWrapperBase< TElement, RuntimeDataLocation >::RemoveAt( Uint32 index )
{
	RED_FATAL_ASSERT( index < m_size, "Array: Out of bounds. Cannot access item %i as the array is only size %u", index, m_size );

	ArrayImplUtils::MoveBackwards( m_buffer + index, 1, m_size - index - 1 );
	m_size--;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE Int32 ArrayWrapperBase< TElement, RuntimeDataLocation >::GetIndex( const TElement& element ) const
{
	const_iterator i = std::find( Begin(), End(), element );
	if ( i != End() )
	{
		return static_cast< Int32 >( i - Begin() );
	}
	return INVALID_INDEX;
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE Bool ArrayWrapperBase< TElement, RuntimeDataLocation >::Exist( const TElement& element ) const
{
	return std::find( Begin(), End(), element ) != End();
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE TElement* ArrayWrapperBase< TElement, RuntimeDataLocation >::FindPtr( const TElement& element )
{
	iterator it = std::find( Begin(), End(), element );
	if ( it != End() )
	{
		return it;
	}
	return nullptr;
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE const TElement* ArrayWrapperBase< TElement, RuntimeDataLocation >::FindPtr( const TElement& element ) const
{
	const_iterator it = std::find( Begin(), End(), element );
	if ( it != End() )
	{
		return it;
	}
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE void ArrayWrapperBase< TElement, RuntimeDataLocation >::Clear()
{
	Resize( 0 );
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE void ArrayWrapperBase< TElement, RuntimeDataLocation >::Resize( Uint32 size )
{
	RED_FATAL_ASSERT( size <= m_capacity, "Cannot resize ArrayWrapper over its maxium capacity" );
	m_size = size;
}

template < typename TElement, typename RuntimeDataLocation >
RED_INLINE void ArrayWrapperBase< TElement, RuntimeDataLocation >::Grow( Uint32 amount )
{
	Resize( m_size + amount );
}

} // red
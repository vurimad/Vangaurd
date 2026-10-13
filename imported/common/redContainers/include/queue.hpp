/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE Queue< TElement >::Queue( const red::memory::Pool& pool /* = DefaultPool() */ )
	: m_data( pool )
{
}

template < typename TElement >
RED_INLINE Queue< TElement >::Queue( const Queue& other )
	: m_data( other.m_data )
{
}

template < typename TElement >
RED_INLINE Queue< TElement >::Queue( Queue&& other )
	: m_data( std::forward< StorageType >( other.m_data ) )
{
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE Queue< TElement >& Queue< TElement >::operator=( const Queue& other )
{
	m_data = other.m_data;
	return *this;
}

template < typename TElement >
RED_INLINE Queue< TElement >& Queue< TElement >::operator=( Queue&& other )
{
	m_data = std::forward< StorageType >( other.m_data );
	return *this;
}

template < typename TElement >
RED_INLINE void Queue< TElement >::Swap( Queue& other )
{
	m_data.Swap( other.m_data );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE TElement Queue< TElement >::Pop()
{
	TElement element = std::move( m_data.Front() );
	m_data.PopFront();
	return element;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE void Queue< TElement >::RemoveFront()
{
	m_data.PopFront();
}

template < typename TElement >
RED_INLINE void Queue< TElement >::RemoveBack()
{
	m_data.PopBack();
}

} // red
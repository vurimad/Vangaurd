/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE Heap< TElement, TSortPredicate >::Heap( const red::memory::Pool& pool /* = DefaultPool() */ )
	: m_data( pool )
{
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Heap< TElement, TSortPredicate >::Heap( const Heap& other )
	: m_data( other.m_data )
{
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Heap< TElement, TSortPredicate >::Heap( Heap&& other )
	: m_data( std::move( other.m_data ) )
{
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE Heap< TElement, TSortPredicate >& Heap< TElement, TSortPredicate >::operator=( const Heap& other )
{
	m_data = other.m_data;
	return *this;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Heap< TElement, TSortPredicate >& Heap< TElement, TSortPredicate >::operator=( Heap&& other )
{
	m_data = std::move( other.m_data );
	return *this;
}

template < typename TElement, typename TCompareFunc >
RED_INLINE void Heap< TElement, TCompareFunc >::Swap( Heap& other )
{
	m_data.Swap( other.m_data );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE void Heap< TElement, TSortPredicate >::PushHeap( const TElement& element )
{
	m_data.PushBack( element );
	std::push_heap( m_data.Begin(), m_data.End(), SortPredicate() );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void Heap< TElement, TSortPredicate >::PushHeap( TElement&& element )
{
	m_data.PushBack( std::forward< TElement >( element ) );
	std::push_heap( m_data.Begin(), m_data.End(), SortPredicate() );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE TElement Heap< TElement, TSortPredicate >::PopHeap()
{
	std::pop_heap( m_data.Begin(), m_data.End(), SortPredicate() );
	return m_data.PopBack();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename Heap< TElement, TSortPredicate >::Result Heap< TElement, TSortPredicate >::RemoveAndHeapify( iterator it )
{
	Result result = Remove_NoHeapify( it );
	Heapify();
	return result;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename Heap< TElement, TSortPredicate >::Result Heap< TElement, TSortPredicate >::Remove_NoHeapify( iterator it )
{
	return m_data.Remove( it );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void Heap< TElement, TSortPredicate >::Heapify()
{
	std::make_heap( m_data.Begin(), m_data.End(), SortPredicate() );
}

} // red
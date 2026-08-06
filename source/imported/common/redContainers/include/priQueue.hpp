/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE PriQueue< TElement, TSortPredicate >::PriQueue( const red::memory::Pool& pool /* = DefaultPool() */ )
	: m_data( pool )
{
}

template < typename TElement, typename TSortPredicate >
RED_INLINE PriQueue< TElement, TSortPredicate >::PriQueue( const PriQueue& other )
	: m_data( other.m_data )
{
}

template < typename TElement, typename TSortPredicate >
RED_INLINE PriQueue< TElement, TSortPredicate >::PriQueue( PriQueue&& other )
	: m_data( std::forward< StorageType >( other.m_data ) )
{
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE PriQueue< TElement, TSortPredicate >& PriQueue< TElement, TSortPredicate >::operator=( const PriQueue& other )
{
	m_data = other.m_data;
	return *this;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE PriQueue< TElement, TSortPredicate >& PriQueue< TElement, TSortPredicate >::operator=( PriQueue&& other )
{
	m_data = std::forward< StorageType >( other.m_data );
	return *this;
}

template < typename TElement, typename TCompareFunc >
RED_INLINE void PriQueue< TElement, TCompareFunc >::Swap( PriQueue& other )
{
	m_data.Swap( other.m_data );
}

} // red
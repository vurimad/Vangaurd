/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "policies.h"

namespace red {

template < typename TElement, Uint32 NumElements >
TElement& FixedArray< TElement, NumElements >::operator[]( Uint32 i )
{
	RED_FATAL_ASSERT( i < NumElements, "FixedArray: Out of bounds. Cannot access item %u as the array is only size %u", i, NumElements );
	return m_elements[ i ];
}

template < typename TElement, Uint32 NumElements >
const TElement& FixedArray< TElement, NumElements >::operator[]( Uint32 i ) const
{
	RED_FATAL_ASSERT( i < NumElements, "FixedArray: Out of bounds. Cannot access item %u as the array is only size %u", i, NumElements );
	return m_elements[ i ];
}

template < typename TElement, Uint32 NumElements >
void FixedArray< TElement, NumElements >::Fill( const TElement& el, Uint32 count )
{
	for ( Uint32 i = 0; i < count; ++i )
	{
		m_elements[ i ] = el;
	}
}

template < typename TElement, Uint32 NumElements >
Bool FixedArray< TElement, NumElements >::operator==( const FixedArray& other ) const
{
	typedef typename policies::ComparePolicySelector< TElement >::Type ComparePolicy;

	return ComparePolicy::Equal( m_elements, other.m_elements, NumElements );
}

template < typename TElement, Uint32 NumElements >
Bool FixedArray< TElement, NumElements >::operator!=( const FixedArray& other ) const
{
	return !( *this == other );
}

template < typename TElement, Uint32 NumElements >
Int32 FixedArray< TElement, NumElements >::GetIndex( const TElement& element ) const
{
	const_iterator i = std::find( Begin(), End(), element );
	if ( i != End() )
	{
		return static_cast< Int32 >( i - Begin() );
	}
	return INVALID_INDEX;
}

template < typename TElement, Uint32 NumElements >
Bool FixedArray< TElement, NumElements >::Exist( const TElement& element ) const
{
	return std::find( Begin(), End(), element ) != End();
}

template < typename TElement, Uint32 NumElements >
TElement* FixedArray< TElement, NumElements >::FindPtr( const TElement& element )
{
	iterator it = std::find( Begin(), End(), element );
	if ( it != End() )
	{
		return it.operator->();
	}
	return nullptr;
}

template< typename TElement, Uint32 NumElements >
const TElement* FixedArray< TElement, NumElements >::FindPtr( const TElement& element ) const
{
	const_iterator it = std::find( Begin(), End(), element );
	if ( it != End() )
	{
		return it.operator->();
	}
	return nullptr;
}

} // red
/*
 * Copyright (c) 2020 CD PROJEKT RED. All Rights Reserved.
 */

#pragma once

#include "../src/assert.h"

namespace red
{
namespace memory
{

	template < typename TElement, Uint32 NumElements >
	void SimpleArray< TElement, NumElements >::Fill( const TElement& el )
	{
		for( Uint32 i = 0; i < NumElements; ++i )
		{
			m_elements[ i ] = el;
		}
	}

	template < typename TElement, Uint32 NumElements >
	TElement& SimpleArray< TElement, NumElements >::operator[]( Uint32 i )
	{
		RED_MEMORY_ASSERT( i < NumElements, "SimpleArray: Out of bounds. Cannot access item %u as the array is only size %u", i, NumElements );
		return m_elements[ i ];
	}

	template < typename TElement, Uint32 NumElements >
	const TElement& SimpleArray< TElement, NumElements >::operator[]( Uint32 i ) const
	{
		RED_MEMORY_ASSERT( i < NumElements, "SimpleArray: Out of bounds. Cannot access item %u as the array is only size %u", i, NumElements );
		return m_elements[ i ];
	}

} // memory
} // red

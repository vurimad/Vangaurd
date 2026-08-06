/*
 * Copyright (c) 2019 CD PROJEKT RED. All Rights Reserved.
 */

#pragma once

namespace red
{
namespace memory
{

	/*
	 * Simple replacement of std::array with a minimal subset of functionalities.
	 * This class was introduced only for redMemory project usage.
	 * In all other projects please use red::FixedArray which is the proper replacement of std::array in RED Engine.
	 */
	template< typename TElement, Uint32 NumElements >
	class SimpleArray
	{
	public:

		typedef TElement									ElementType;

		typedef TElement*									iterator;
		typedef const TElement*								const_iterator;

		// std compatibility
		typedef TElement									value_type;

		// Get typed pointer to the buffer
		RED_FORCE_INLINE TElement* Data() { return m_elements; }
		// Get typed const pointer to the buffer
		RED_FORCE_INLINE const TElement* Data() const { return m_elements; }
		// Get number of elements in the array
		RED_FORCE_INLINE static constexpr Uint32 Size() { return NumElements; }

		// Get iterator pointing to the first element in array
		RED_FORCE_INLINE iterator Begin() { return m_elements; }
		// Get iterator pointing to the element next after the last one
		RED_FORCE_INLINE iterator End() { return m_elements + NumElements; }
		// Get const_iterator pointing to the first element in array
		RED_FORCE_INLINE const_iterator Begin() const { return m_elements; }
		// Get const_iterator pointing to the element next after the last one
		RED_FORCE_INLINE const_iterator End() const { return m_elements + NumElements; }

		// fill the container with specified value 
		RED_INLINE void Fill( const TElement& el );

		// Get reference to the i-th element of the array
		RED_INLINE TElement& operator[]( Uint32 i );
		// Get const reference to the i-th element of the array
		RED_INLINE const TElement& operator[]( Uint32 i ) const;
		// Get reference to the first element
		RED_FORCE_INLINE TElement& Front() { return m_elements[ 0 ]; }
		// Get const reference to the first element
		RED_FORCE_INLINE const TElement& Front() const { return m_elements[ 0 ]; }
		// Get reference to the last element
		RED_FORCE_INLINE TElement& Back() { return m_elements[ NumElements - 1 ]; }
		// Get const reference to the last element
		RED_FORCE_INLINE const TElement& Back() const { return m_elements[ NumElements - 1 ]; }

		TElement m_elements[ NumElements ];
	};

	// Enable c++11 range-based for loop

	template< typename TElement, Uint32 NumElements >
	RED_INLINE typename SimpleArray< TElement, NumElements >::iterator begin( SimpleArray< TElement, NumElements >& arr )
	{
		return arr.Begin();
	}

	template< typename TElement, Uint32 NumElements >
	RED_INLINE typename SimpleArray< TElement, NumElements >::iterator end( SimpleArray< TElement, NumElements >& arr )
	{
		return arr.End();
	}

	template< typename TElement, Uint32 NumElements >
	RED_INLINE typename SimpleArray< TElement, NumElements >::const_iterator begin( const SimpleArray< TElement, NumElements >& arr )
	{
		return arr.Begin();
	}

	template< typename TElement, Uint32 NumElements >
	RED_INLINE typename SimpleArray< TElement, NumElements >::const_iterator end( const SimpleArray< TElement, NumElements >& arr )
	{
		return arr.End();
	}

} // memory
} // red

#include "simpleArray.hpp"

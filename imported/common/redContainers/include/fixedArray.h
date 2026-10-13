/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "containersCommon.h"
#include "fixedBuffer.h"
#include "arrayIterator.h"
#include "checkedIterator.h"
#include "indexRange.h"
#include "containerOpResult.h"
#include "arraySpan.h"

namespace red {

template< typename TElement, Uint32 NumElements >
class FixedArray
{
public:

	typedef TElement									ElementType;
#ifdef RED_CHECKED_ITERATORS
	typedef CheckedIterator< FixedArray >				iterator;
	typedef CheckedConstIterator< FixedArray >			const_iterator;
#else
	typedef ArrayIterator< TElement >					iterator;
	typedef ArrayConstIterator< TElement >				const_iterator;
#endif
	typedef std::reverse_iterator< iterator >			reverse_iterator;
	typedef std::reverse_iterator< const_iterator >		reverse_const_iterator;
	typedef ArrayReverseIteration< FixedArray >		ReverseIteration;
	typedef ArrayReverseConstIteration< FixedArray >	ReverseConstIteration;

	// std compatibility
	typedef TElement									value_type;

	// Get typed pointer to the buffer
	RED_FORCE_INLINE TElement* Data() { return m_elements; }
	RED_FORCE_INLINE TElement* TypedData() { return m_elements; }
	// Get typed const pointer to the buffer
	RED_FORCE_INLINE const TElement* Data() const { return m_elements; }
	RED_FORCE_INLINE const TElement* TypedData() const { return m_elements; }
	// Get number of elements in the array
	RED_FORCE_INLINE static constexpr Uint32 Size() { return NumElements; }
	// Get size of stored data in bytes
	RED_FORCE_INLINE static constexpr Uint32 DataSize() { return NumElements * sizeof( TElement ); }

	// Get iterator pointing to the first element in array
	RED_FORCE_INLINE iterator Begin() { return iterator( RED_CHECKED_ITERATOR_THIS m_elements ); }
	// Get iterator pointing to the element next after the last one
	RED_FORCE_INLINE iterator End() { return iterator( RED_CHECKED_ITERATOR_THIS m_elements + NumElements ); }
	// Get const_iterator pointing to the first element in array
	RED_FORCE_INLINE const_iterator Begin() const { return const_iterator( RED_CHECKED_ITERATOR_THIS m_elements ); }
	// Get const_iterator pointing to the element next after the last one
	RED_FORCE_INLINE const_iterator End() const { return const_iterator( RED_CHECKED_ITERATOR_THIS m_elements + NumElements ); }

	// Get reverse_iterator pointing to the last element in the array
	RED_FORCE_INLINE reverse_iterator RBegin() { return reverse_iterator( End() ); }
	// Get reverse_iterator pointing to the element before the first one
	RED_FORCE_INLINE reverse_iterator REnd() { return reverse_iterator( Begin() ); }
	// Get reverse_const_iterator pointing to the last element in the array
	RED_FORCE_INLINE reverse_const_iterator RBegin() const { return reverse_const_iterator( End() ); }
	// Get reverse_const_iterator pointing to the element before the first one
	RED_FORCE_INLINE reverse_const_iterator REnd() const { return reverse_const_iterator( Begin() ); }
	// Get range which allows to perform reverse range-based for loop
	RED_FORCE_INLINE ReverseIteration Reverse() { return ReverseIteration( *this ); }
	// Get range which allows to perform const reverse range-based for loop
	RED_FORCE_INLINE ReverseConstIteration Reverse() const { return ReverseConstIteration( *this ); }

	// fill the container with specified value 
	RED_INLINE void Fill( const TElement& el, Uint32 count = NumElements );

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

	// Returns true if 'other' contains the same elements
	RED_INLINE Bool operator==( const FixedArray< TElement, NumElements >& other ) const;
	// Returns true if 'other' does not contain the same elements
	RED_INLINE Bool operator!=( const FixedArray< TElement, NumElements >& other ) const;

	// Get index of the first occurrence of 'element' in the array; returns -1 if 'element' was not found
	RED_INLINE Int32 GetIndex( const TElement& element ) const;
	// Returns true if 'element' is present in the array
	RED_INLINE Bool Exist( const TElement& element ) const;
	// Get pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	RED_INLINE TElement* FindPtr( const TElement& element );
	// Get const pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	RED_INLINE const TElement* FindPtr( const TElement& element ) const;


	TElement m_elements[ NumElements ];
};

//////////////////////////////////////////////////////////////////////////
// Enable c++11 range-based for loop

template< typename TElement, Uint32 NumElements >
RED_INLINE typename FixedArray< TElement, NumElements >::iterator begin( FixedArray< TElement, NumElements >& arr )
{
	return arr.Begin();
}

template< typename TElement, Uint32 NumElements >
RED_INLINE typename FixedArray< TElement, NumElements >::iterator end( FixedArray< TElement, NumElements >& arr )
{
	return arr.End();
}

template< typename TElement, Uint32 NumElements >
RED_INLINE typename FixedArray< TElement, NumElements >::const_iterator begin( const FixedArray< TElement, NumElements >& arr )
{
	return arr.Begin();
}

template< typename TElement, Uint32 NumElements >
RED_INLINE typename FixedArray< TElement, NumElements >::const_iterator end( const FixedArray< TElement, NumElements >& arr )
{
	return arr.End();
}

//////////////////////////////////////////////////////////////////////////
// ArraySpan compatibility

template< typename TElement, Uint32 NumElements >
RED_INLINE TElement* GetStartPtr( FixedArray< TElement, NumElements >& arr )
{
	return arr.TypedData();
}

template< typename TElement, Uint32 NumElements >
RED_INLINE TElement* GetEndPtr( FixedArray< TElement, NumElements >& arr )
{
	return arr.TypedData() + NumElements;
}

template< typename TElement, Uint32 NumElements >
RED_INLINE const TElement* GetStartPtr( const FixedArray< TElement, NumElements >& arr )
{
	return arr.TypedData();
}

template< typename TElement, Uint32 NumElements >
RED_INLINE const TElement* GetEndPtr( const FixedArray< TElement, NumElements >& arr )
{
	return arr.TypedData() + NumElements;
}

} // red

#include "fixedArray.hpp"

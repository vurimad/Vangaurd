/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dynArray.h"

namespace red {

template < typename TElement, typename TSortPredicate = std::less< TElement > >
class Heap
{
public:

	typedef DynArray< TElement >								StorageType;
	typedef TElement											ElementType;
	typedef typename StorageType::const_iterator				iterator;
	typedef TSortPredicate										SortPredicate;
	typedef typename StorageType::Result						Result;

	// Construct heap which uses specified 'pool' for memory allocation
	RED_INLINE Heap( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Copy constructor
	RED_INLINE Heap( const Heap& other );
	// Move constructor
	RED_INLINE Heap( Heap&& other );

	// Copy assignment
	RED_INLINE Heap& operator=( const Heap& other );
	// Move assignment
	RED_INLINE Heap& operator=( Heap&& other );
	// Swap data with 'other'
	RED_INLINE void Swap( Heap& other );

	// Get typed pointer to stored data
	RED_INLINE const TElement* TypedData() const { return m_data.TypedData(); }
	// Get number of elements in the heap
	RED_INLINE Uint32 Size() const { return m_data.Size(); }
	// Get size of stored data in bytes
	RED_INLINE Uint32 DataSize() const { return m_data.DataSize(); }
	// Get maximum number of elements that can be stored in the heap without need of reallocation
	RED_INLINE Uint32 Capacity() const { return m_data.Capacity(); }
	// Get maximum size of data which can be stored in the heap without need of reallocation, in bytes
	RED_INLINE Uint32 DataCapacity() const { return m_data.DataCapacity(); }
	// Returns true if heap contains 0 elements
	RED_INLINE Bool Empty() const { return m_data.Empty(); }

	// Get iterator pointing to the first element on the heap
	RED_INLINE iterator Begin() const { return m_data.Begin(); }
	// Get iterator pointing to the element next after the last one
	RED_INLINE iterator End() const { return m_data.End(); }

	// Get const reference to the i-th element on the heap
	RED_INLINE const TElement& operator[]( Uint32 i ) const { return m_data[ i ]; }
	// Get reference to the first element
	RED_INLINE const TElement& Front() const { return m_data.Front(); }

	// Returns true if 'othe'r contains the same elements; both heaps need to have the same structure
	RED_INLINE Bool operator==( const Heap& other ) const { return m_data == other.m_data; }
	// Returns true if 'other' does not contain the same elements; returns true even if 'other' has the same elements but different structure
	RED_INLINE Bool operator!=( const Heap& other ) const { return m_data != other.m_data; }

	// Add 'element' to the heap
	RED_INLINE void PushHeap( const TElement& element );
	// Add 'element' to the heap
	RED_INLINE void PushHeap( TElement&& element );
	// Remove and return the least element (in terms of TSortPredicate)
	RED_INLINE TElement PopHeap();

	// Remove element specified by iterator 'it'; 'Result' contains iterator pointing to the element next after removed one
	// Ensures heap invariant after element gets removed
	RED_INLINE Result RemoveAndHeapify( iterator it );
	// Remove element specified by iterator 'it'; 'Result' contains iterator pointing to the element next after removed one
	// Heap invariant might be invalidated after calling this method
	RED_INLINE Result Remove_NoHeapify( iterator it );

	// Returns true if 'element' is present in the heap
	RED_INLINE Bool Exist( const TElement& element ) const { return m_data.Exist( element ); }

	// Remove all elements from the heap
	RED_INLINE void Clear() { m_data.Clear(); }
	// Change internal buffer capacity
	RED_INLINE void Reserve( Uint32 capacity ) { m_data.Reserve( capacity); }
	// Change internal buffer capacity so that it is equal to heap size
	RED_INLINE void Shrink() { m_data.Shrink(); }
	// Run heap predicate over the stored data. Useful after data was added/modified/removed directly on the underlying container
	RED_INLINE void Heapify();
	// Set 'pool' that will be used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool ) { m_data.SetPool( pool ); }
	// Get memory pool used for memory allocation
	RED_INLINE const red::memory::Pool& GetPool() const { return m_data.GetPool(); }

	// Get TSortPredicate object
	RED_INLINE TSortPredicate GetSortPredicate() const { return TSortPredicate(); }

private:	

	StorageType	m_data;
};

//////////////////////////////////////////////////////////////////////////
// ArraySpan compatibility

template < typename TElement >
RED_INLINE const TElement* GetStartPtr( const Heap< TElement >& heap )
{
	return heap.TypedData();
}

template < typename TElement >
RED_INLINE const TElement* GetEndPtr( const Heap< TElement >& heap )
{
	return heap.TypedData() + heap.Size();
}

//////////////////////////////////////////////////////////////////////////
// Enable c++11 range-based for loop

template < typename TElement, typename TSortPredicate >
RED_INLINE typename Heap< TElement, TSortPredicate >::iterator begin( const Heap< TElement, TSortPredicate >& heap )
{
	return heap.Begin();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename Heap< TElement, TSortPredicate >::iterator end( const Heap< TElement, TSortPredicate >& heap )
{
	return heap.End();
}

} // red

#include "heap.hpp"

/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "heap.h"

//////////////////////////////////////////////////////////////////////////
// Priority queue using Heap internally to store and organize elements.
// Push/Pop is O( log( n ) )
//////////////////////////////////////////////////////////////////////////

namespace red {

template < typename TElement, typename TSortPredicate = std::less< TElement > >
class PriQueue
{
public:

	typedef Heap< TElement, TSortPredicate >					StorageType;
	typedef TElement											ElementType;
	typedef TSortPredicate										SortPredicate;

	// Construct queue which uses specified 'pool' for memory allocation
	RED_INLINE PriQueue( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE);
	// Copy constructor
	RED_INLINE PriQueue( const PriQueue& other );
	// Move constructor
	RED_INLINE PriQueue( PriQueue&& other );

	// Copy assignment
	RED_INLINE PriQueue& operator=( const PriQueue& other );
	// Move assignment
	RED_INLINE PriQueue& operator=( PriQueue&& other );
	// Swap data with 'other'
	RED_INLINE void Swap( PriQueue& other );

	// Get number of elements in the queue
	RED_INLINE Uint32 Size() const { return m_data.Size(); }
	// Get size of stored data in bytes
	RED_INLINE Uint32 DataSize() const { return m_data.DataSize(); }
	// Get maximum number of elements that can be stored in the queue without need of reallocation
	RED_INLINE Uint32 Capacity() const { return m_data.Capacity(); }
	// Get maximum size of data which can be stored in queue without need of reallocation, in bytes
	RED_INLINE Uint32 DataCapacity() const { return m_data.DataCapacity(); }
	// Returns true if queue contains 0 elements
	RED_INLINE Bool Empty() const { return m_data.Empty(); }

	// Get const reference to the first element
	RED_INLINE const TElement& Front() const { return m_data.Front(); }

	// Returns true if 'other' contains the same elements; both queues need to have exactly the same heap structure
	RED_INLINE Bool operator==( const PriQueue& other ) const { return m_data == other.m_data; }
	// Returns true if 'other' does not contain the same elements; returns true even if 'othe'r has the same elements but different heap structure
	RED_INLINE Bool operator!=( const PriQueue& other ) const { return m_data != other.m_data; }

	// Add 'element' to the queue
	RED_INLINE void Push( const TElement& element ) { m_data.PushHeap( element ); }
	// Add 'element' to the queue
	RED_INLINE void Push( TElement&& element ) { m_data.PushHeap( std::forward< TElement >( element ) ); }
	// Remove and return first element on the queue (the least one in terms of TSortPredicate)
	RED_INLINE TElement Pop() { return m_data.PopHeap(); }

	// Returns true if 'element' is present in the queue
	RED_INLINE Bool Exist( const TElement& element ) const { return m_data.Exist( element ); }

	// Remove all elements from the queue
	RED_INLINE void Clear() { m_data.Clear(); }
	// Change internal buffer capacity
	RED_INLINE void Reserve( Uint32 capacity ) { m_data.Reserve( capacity ); }
	// Change internal buffer capacity so that it is equal to queue size
	RED_INLINE void Shrink() { m_data.Shrink(); }

	// Set 'pool' that will be used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool ) { m_data.SetPool( pool ); }
	// Get memory pool used for memory allocation
	RED_INLINE const red::memory::Pool& GetPool() const { return m_data.GetPool(); }

	// Get TSortPredicate object
	RED_INLINE TSortPredicate GetSortPredicate() const { return TSortPredicate(); }

private:	

	StorageType	m_data;
};

} // red

#include "priQueue.hpp"

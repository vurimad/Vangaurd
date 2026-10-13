/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "circularBuffer.h"

//////////////////////////////////////////////////////////////////////////
// Double ended FIFO queue using CircullarBuffer internally to store elements.
// Push/Pop is O( 1 )
//////////////////////////////////////////////////////////////////////////

namespace red {

template < typename TElement >
class Queue
{
public:

	typedef CircularBuffer< TElement >							StorageType;
	typedef TElement											ElementType;

	// Construct queue which uses specified 'pool' for memory allocation
	RED_INLINE Queue( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Copy constructor
	RED_INLINE Queue( const Queue& other );
	// Move constructor
	RED_INLINE Queue( Queue&& other );

	// Copy assignment
	RED_INLINE Queue& operator=( const Queue& other );
	// Move assignment
	RED_INLINE Queue& operator=( Queue&& other );
	// Swap data with 'other'
	RED_INLINE void Swap( Queue& other );

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

	// Get reference to the i-th element in the queue
	RED_INLINE TElement& operator[]( Uint32 i ) { return m_data[ i ]; }
	// Get const reference to the i-th element in the queue
	RED_INLINE const TElement& operator[]( Uint32 i ) const { return m_data[ i ]; }
	// Get reference to the first element
	RED_INLINE TElement& Front() { return m_data.Front(); }
	// Get const reference to the first element
	RED_INLINE const TElement& Front() const { return m_data.Front(); }
	// Get reference to the last element
	RED_INLINE TElement& Back() { return m_data.Back(); }
	// Get const reference to the last element
	RED_INLINE const TElement& Back() const { return m_data.Back(); }

	// Returns true if 'other' contains the same elements
	RED_INLINE Bool operator==( const Queue& other ) const { return m_data == other.m_data; }
	RED_INLINE Bool operator!=( const Queue& other ) const { return m_data != other.m_data; }

	// Add 'element' to the end of the queue
	RED_INLINE void Push( const TElement& element ) { m_data.PushBack( element ); }
	// Add 'element' to the end of the queue
	RED_INLINE void Push( TElement&& element ) { m_data.PushBack( std::forward< TElement >( element ) ); }
	// Remove and return first element on the queue
	RED_INLINE TElement Pop();


	// Add 'element' to the end of the queue
	RED_INLINE void PushBack( const TElement& element ) { m_data.PushBack( element ); }
	// Add 'element' to the end of the queue
	RED_INLINE void PushBack( TElement&& element ) { m_data.PushBack( std::forward< TElement >( element ) ); }
	// Remove and return first element on the queue

	// Add 'element' to the end of the queue
	RED_INLINE void PushFront( const TElement& element ) { m_data.PushFront( element ); }
	// Add 'element' to the end of the queue
	RED_INLINE void PushFront( TElement&& element ) { m_data.PushFront( std::forward< TElement >( element ) ); }
	// Remove and return first element on the queue

	// Remove the first element on the queue
	RED_INLINE void RemoveFront();
	// Remove the first element on the queue
	RED_INLINE void RemoveBack();

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

private:	

	StorageType	m_data;
};

} // red

#include "queue.hpp"

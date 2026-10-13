/*
* Copyright (c) 2015-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dynamicBuffer.h"

namespace red {

	template< typename TElement >
	class CircularBuffer;

	template < typename TElement >
	struct CircularArrayConstIterator
	{
		typedef TElement						ElementType;
		typedef const ElementType* PtrType;
		typedef const ElementType& RefType;
		typedef Int32							DiffType;
		typedef ArrayIteratorTag				Tag;

		// STL Compatability
		typedef std::random_access_iterator_tag	iterator_category;
		typedef ElementType						value_type;
		typedef DiffType						difference_type;
		typedef PtrType							pointer;
		typedef RefType							reference;

		CircularArrayConstIterator() = default;
		~CircularArrayConstIterator() = default;
		CircularArrayConstIterator(const CircularArrayConstIterator& it) : m_buffer(it.m_buffer), m_index(it.m_index) {}
		explicit CircularArrayConstIterator(CircularBuffer< TElement >* ptr, Int32 idx) : m_buffer(ptr), m_index(idx) {}

		RefType operator*() const { return (*this->m_buffer)[m_index]; }
		PtrType operator->() const { return &(*this->m_buffer)[m_index]; }
		RefType operator[](DiffType count) const { return (*this->m_buffer)[m_index + count]; }

		CircularArrayConstIterator& operator++() { ++m_index; return *this; }
		CircularArrayConstIterator operator++(int) { return CircularArrayConstIterator(m_buffer, m_index++); }

		CircularArrayConstIterator& operator--() { --m_index; return *this; }
		CircularArrayConstIterator operator--(int) { return CircularArrayConstIterator(m_buffer, m_index--); }

		CircularArrayConstIterator operator+(DiffType count) const { return CircularArrayConstIterator(m_buffer, m_index + count); }
		CircularArrayConstIterator operator-(DiffType count) const { return CircularArrayConstIterator(m_buffer, m_index - count); }

		friend CircularArrayConstIterator operator+(DiffType count, const CircularArrayConstIterator& iter) { return CircularArrayConstIterator(iter.m_buffer, iter.m_index + count); }

		CircularArrayConstIterator& operator+=(DiffType count) { m_index += count; return *this; }
		CircularArrayConstIterator& operator-=(DiffType count) { m_index -= count; return *this; }

		DiffType operator-(const CircularArrayConstIterator& it) const { RED_ASSERT(m_buffer == it.m_buffer); return m_index - it.m_index; }

		Bool operator==(const CircularArrayConstIterator& it) const { RED_ASSERT(m_buffer == it.m_buffer); return (m_index == it.m_index); }
		Bool operator!=(const CircularArrayConstIterator& it) const { RED_ASSERT(m_buffer == it.m_buffer); return (m_index != it.m_index); }

		Bool operator<(const CircularArrayConstIterator& it) const { RED_ASSERT(m_buffer == it.m_buffer); return (m_index < it.m_index); }

		Bool operator<=(const CircularArrayConstIterator& it) const { RED_ASSERT(m_buffer == it.m_buffer); return (m_index <= it.m_index); }
		Bool operator>(const CircularArrayConstIterator& it) const { RED_ASSERT(m_buffer == it.m_buffer); return (m_index > it.m_index); }
		Bool operator>=(const CircularArrayConstIterator& it) const { RED_ASSERT(m_buffer == it.m_buffer); return (m_index >= it.m_index); }

	protected:
		CircularBuffer< TElement >* m_buffer = nullptr;
		Int32 m_index = 0;
	};


	template < typename TElement >
	struct CircularArrayIterator : public CircularArrayConstIterator< TElement >
	{
		typedef TElement						ElementType;
		typedef ElementType* PtrType;
		typedef ElementType& RefType;
		typedef Int32							DiffType;

		// STL Compatability
		typedef std::random_access_iterator_tag	iterator_category;
		typedef ElementType						value_type;
		typedef DiffType						difference_type;
		typedef PtrType							pointer;
		typedef RefType							reference;

		using BaseClass = CircularArrayConstIterator< TElement >;

		using BaseClass::m_buffer;
		using BaseClass::m_index;

		CircularArrayIterator() : BaseClass() {}
		CircularArrayIterator(const CircularArrayIterator& it) : BaseClass(it) {}

		explicit CircularArrayIterator(CircularBuffer< TElement >* ptr, Int32 idx) : BaseClass(ptr, idx) {}

		RefType operator*() const { return (*m_buffer)[m_index]; }
		PtrType operator->() const { return &(*m_buffer)[m_index]; }
		RefType operator[](DiffType count) const { return (*this->m_buffer)[m_index + count]; }

		CircularArrayIterator& operator++() { ++this->m_index; return *this; }
		CircularArrayIterator operator++(int) { return CircularArrayIterator(m_buffer, m_index++); }

		CircularArrayIterator& operator--() { --m_index; return *this; }
		CircularArrayIterator operator--(int) { return CircularArrayIterator(m_buffer, m_index--); }

		CircularArrayIterator operator+(DiffType count) const { return CircularArrayIterator(m_buffer, m_index + count); }
		CircularArrayIterator operator-(DiffType count) const { return CircularArrayIterator(m_buffer, m_index - count); }

		friend CircularArrayIterator operator+(DiffType count, const CircularArrayIterator& iter) { return CircularArrayIterator(m_buffer, m_index + count); }

		CircularArrayIterator& operator+=(DiffType count) { m_index += count; return *this; }
		CircularArrayIterator& operator-=(DiffType count) { m_index -= count; return *this; }

		DiffType operator-(const CircularArrayIterator& it) const { RED_ASSERT(m_buffer == it.m_buffer); return m_index - it.m_index; }
		DiffType operator-(const CircularArrayConstIterator< TElement >& it) const { RED_ASSERT(m_buffer == it.m_buffer); return m_index - it.m_index; }



	};
}

namespace red {
	
template< typename TElement >
class CircularBuffer : public DynamicBuffer
{
public:

	typedef TElement								ElementType;
	typedef DynamicBuffer							BufferType;
	typedef red::PoolDefault				DefaultPool;

	typedef CircularArrayIterator< TElement >							iterator;
	typedef CircularArrayConstIterator< TElement >						const_iterator;
	typedef std::reverse_iterator< iterator >							reverse_iterator;
	typedef std::reverse_iterator< const_iterator >						reverse_const_iterator;

	// Default constructor
	RED_INLINE CircularBuffer( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Copy constructor
	RED_INLINE CircularBuffer( const CircularBuffer& other );
	// Move constructor
	RED_INLINE CircularBuffer( CircularBuffer&& other );
	// Construct buffer with initial capacity
	RED_INLINE explicit CircularBuffer( Uint32 initialCapacity, const red::memory::Pool& );
	// Destructor
	RED_INLINE ~CircularBuffer();

	// Copy assignment
	RED_INLINE CircularBuffer& operator=( const CircularBuffer& other );
	// Move assignment
	RED_INLINE CircularBuffer& operator=( CircularBuffer&& other );
	// Swap content with other buffer
	RED_INLINE void Swap( CircularBuffer& other );

	// Number of elements pushed into the buffer
	RED_INLINE Uint32 Size() const	{ return m_size; }
	// Sum of size of elements pushed into the buffer
	RED_INLINE Uint32 DataSize() const { return m_size * sizeof( TElement ); }
	// Number of elements that can be stored in buffer without a need of reallocation
	RED_INLINE Uint32 Capacity() const { return m_capacity; }
	// Size of data that can be stored in buffer without a need of reallocation
	RED_INLINE Uint32 DataCapacity() const { return m_capacity * sizeof( TElement ); }
	// Is the buffer empty
	RED_INLINE Bool Empty() const { return m_size == 0; }

	// Get iterator pointing to the first element in array
	iterator Begin();
	// Get iterator pointing to the element next after the last one
	iterator End();
	// Get const_iterator pointing to the first element in array
	const_iterator Begin() const;
	// Get const_iterator pointing to the element next after the last one
	const_iterator End() const;

	// Get reverse_iterator pointing to the last element in the array
	reverse_iterator RBegin();
	// Get reverse_iterator pointing to the element before the first one
	reverse_iterator REnd();
	// Get reverse_const_iterator pointing to the last element in the array
	reverse_const_iterator RBegin() const;
	// Get reverse_const_iterator pointing to the element before the first one
	reverse_const_iterator REnd() const;

	// Access i-th element
	RED_INLINE TElement& operator[]( Uint32 i );
	// Access i-th element
	RED_INLINE const TElement& operator[]( Uint32 i ) const;
	// Get the front element
	RED_INLINE TElement& Front();
	// Get the front element - const
	RED_INLINE const TElement& Front() const;
	// Get the back element
	RED_INLINE TElement& Back();
	// Get the back element - const
	RED_INLINE const TElement& Back() const;

	// Check if buffers content is equal
	RED_INLINE Bool operator==( const CircularBuffer& other ) const;
	// Check if buffers content is not equal
	RED_INLINE Bool operator!=( const CircularBuffer& other ) const;

	// Push one element to the front
	RED_INLINE void PushFront( const TElement& element );
	// Push one element to the front
	RED_INLINE void PushFront( TElement&& element );
	// Push one element to the back
	RED_INLINE void PushBack( const TElement& element );
	// Push one element to the back
	RED_INLINE void PushBack( TElement&& element );
	// Pop one element from the front
	RED_INLINE void PopFront();
	// Pop one element from the back
	RED_INLINE void PopBack();

	// Check if element is present in the buffer
	RED_INLINE Bool Exist( const TElement& element ) const;

	// Clear the buffer, it doesn't free any memory
	RED_INLINE void Clear();
	// Reserve the Grow the buffer by desired number of elements
	void Reserve( const Uint32 newCapacity );

	// Shrink to the number of elements in the buffer
	RED_INLINE void Shrink();

	// Set pool used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool );
	// Get pool used for memory allocation
	RED_INLINE const red::memory::Pool& GetPool() const;

	template< typename SortFunction >
	RED_INLINE void Sort(SortFunction&& sortFunction);

	// Sorts the last N elements from the Back() of the circular buffer, leaving size-N elements at the Front() untouched.
	// numElements must be <= Size()
	template< typename SortFunction >
	RED_INLINE void SortLastN(SortFunction&& sortFunction, Uint32 numElements);

private:
	RED_INLINE void MakeContiguous();

	RED_INLINE TElement* TypedData() { return reinterpret_cast< TElement* >( DynamicBuffer::Data() ); }
	RED_INLINE const TElement* TypedData() const { return reinterpret_cast< const TElement* >( DynamicBuffer::Data() ); }

	RED_INLINE void InternalCopy( const CircularBuffer& other );
	RED_INLINE void InternalDestruct();

	RED_INLINE void ResizeBuffer( Uint32 capacity );
	static void MoveAfterReallocation( void* dst, void* src, Uint32 num, const void* context );

	// should be called when new element is pushed at the front
	RED_INLINE void ExtendFront();
	// should be called when new element is pushed at the back
	RED_INLINE void ExtendBack();
	// should be called when an element is popped from the front
	RED_INLINE void ReleaseFront();
	// should be called when an element is popped from the back
	RED_INLINE void ReleaseBack();

	RED_INLINE Uint32 Wrap( Uint32 index ) const;

	//////////////////////////////////////////////////////////////////////////
	// NOTE: m_front and m_back are indexes in the dynamic array where front and back elements are placed.
	// They move on PushBack/PopBack ( m_back++/m_back-- ) and PushFront/PopFront ( m_front--/m_front++)
	// and wrap inside bounds of the dynamic array.
	//////////////////////////////////////////////////////////////////////////
	Uint32		m_size;				// number of stored elements
	Uint32		m_front;			// front index
	Uint32		m_back;				// back index
};

} // red

#include "circularBuffer.hpp"

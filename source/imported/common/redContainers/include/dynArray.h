/*
* Copyright (c) 2015-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/poolUtils.h"

#include "containersCommon.h"
#include "dynamicBuffer.h"
#include "arrayIterator.h"
#include "checkedIterator.h"
#include "indexRange.h"
#include "containerOpResult.h"

namespace red {

template < typename TElement > class ArraySpan;

template < typename TElement >
RED_ALIGNED_CLASS( DynArray, 8 ) : public DynamicBuffer
{
	// Avoid wasting time on cascading template errors because of things like converting red::ArraySpan<const T> to red::DynArray<T> but leaving in the const by accident.
	static_assert(!std::is_const<TElement>::value, "Const value type in array, unlikely to be intentional. Remove this static_assert if truly necessary.");

public:
	typedef TElement													ElementType;
#ifdef RED_CHECKED_ITERATORS
	typedef CheckedIterator< DynArray >									iterator;
	typedef CheckedConstIterator< DynArray >							const_iterator;
#else
	typedef ArrayIterator< TElement >									iterator;
	typedef ArrayConstIterator< TElement >								const_iterator;
#endif
	typedef std::reverse_iterator< iterator >							reverse_iterator;
	typedef std::reverse_iterator< const_iterator >						reverse_const_iterator;
	typedef ArrayReverseIteration< DynArray >							ReverseIteration;
	typedef ArrayReverseConstIteration< DynArray >						ReverseConstIteration;
	typedef ContainerOpResult< iterator >								Result;
	typedef DynamicBuffer												BufferType;
	
	// std compatibility
	typedef TElement													value_type;

	// Construct array which uses specified 'pool' for memory allocation
	DynArray( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Copy constructor
	DynArray( const DynArray& other );
	// Copy-construct array with use of specified 'pool' for memory allocation
	DynArray( const DynArray& other, const red::memory::Pool& pool );
	// Move constructor
	DynArray( DynArray&& other );
	// Construct array containing elements from initializer list, using specified 'pool' for memory allocation
	DynArray( std::initializer_list< TElement > initializerList, const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE);
	// Construct array of given 'size' which uses specified 'pool' for memory allocation; elements will be set to default value
	explicit DynArray( Uint32 size, const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Construct array by copying from span
	DynArray( const red::ArraySpan< const TElement >& span, const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Destructor
	~DynArray();

	// Copy assignment
	DynArray& operator=( const DynArray& other );
	// Move assignment
	DynArray& operator=( DynArray&& other );
	// Assign elements from span
	DynArray& operator=( const red::ArraySpan< const TElement >& span );
	// Assign elements from initializer list
	DynArray& operator=( std::initializer_list< TElement > initializerList );
	// Swap data and pool with 'other'
	void Swap( DynArray& other );

	// Get pointer to the allocated buffer
	using BufferType::Data;
	// Get maximum number of elements that can be stored in the array without need of reallocation
	using BufferType::Capacity;
	// Get typed pointer to the allocated buffer
	TElement* TypedData();
	// Get typed const pointer to the allocated buffer
	const TElement* TypedData() const;
	// Get number of elements in the array
	Uint32 Size() const;
	// Get maximum number of elements in the array
	constexpr Uint32 MaxSize() const { return std::numeric_limits< Uint32 >::max() / sizeof( TElement ); }
	// Get size of stored data in bytes
	Uint32 DataSize() const;
	// Get maximum size of data which can be stored in array without need of reallocation, in bytes
	Uint32 DataCapacity() const;
	// Returns true if array contains 0 elements
	Bool Empty() const;

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
	// Get range which allows to perform reverse range-based for loop
	ReverseIteration Reverse();
	// Get range which allows to perform const reverse range-based for loop
	ReverseConstIteration Reverse() const;

	// Get range which allows to perform range-based for loop through array indices
	IndexRange Indices() const;
	// Get range which allows to perform reverse range-based for loop through array indices
	ReverseIndexRange ReverseIndices() const;

	// Get reference to the i-th element of the array
	TElement& operator[]( Uint32 i );
	// Get const reference to the i-th element of the array
	const TElement& operator[]( Uint32 i ) const;
	// Get reference to the first element
	TElement& Front();
	// Get const reference to the first element
	const TElement& Front() const;
	// Get reference to the last element
	TElement& Back();
	// Get const reference to the last element
	const TElement& Back() const;

	// Returns true if 'other' contains the same elements
	Bool operator==( const DynArray& other ) const;
	// Returns true if 'other' does not contain the same elements
	Bool operator!=( const DynArray& other ) const;

	// Add 'element' to the back of the array
	void PushBack( const TElement& element );
	// Add 'element' to the back of the array
	void PushBack( TElement&& element );
	// Adds all elements from 'arr' to the back of the array
	void PushBack( const DynArray& arr );
	// Adds all elements from 'arra' to the back of the array
	void PushBack( const ArraySpan< const TElement >& arr );
	// Add 'element' to the back of the array; WARNING! this method assumes there's enough space in the allocated buffer and added element doesn't belong to the array
	void PushBackUnchecked( const TElement& element );
	// Add 'element' to the back of the array; WARNING! this method assumes there's enough space in the allocated buffer and added element doesn't belong to the array
	void PushBackUnchecked( TElement&& element );
	// Remove and return the last element from the array
	TElement PopBack();
	// Insert 'element' at the position specified by iterator 'it'; 'Result' contains iterator pointing to the inserted element
	Result Insert( const_iterator it, const TElement& element );
	// Insert 'element' at the position specified by iterator 'it'; 'Result' contains iterator pointing to the inserted element
	Result Insert( const_iterator it, TElement&& element );
	// Insert 'elements' at the position specified by iterator 'it'; 'Result' contains iterator pointing to the inserted element
	Result Insert( const_iterator it, const red::ArraySpan< TElement >& elements );
	// Insert 'element' at the specified 'index'; 'Result' contains iterator pointing to the inserted element
	Result InsertAt( const Uint32 index, const TElement& element );
	// Insert 'element' at the specified 'index'; 'Result' contains iterator pointing to the inserted element
	Result InsertAt( const Uint32 index, TElement&& element );
	// Insert 'elements' at the position specified by iterator 'it'; 'Result' contains iterator pointing to the inserted element
	Result InsertAt( const Uint32 index, const red::ArraySpan< TElement >& elements );
	// Construct element at the end of the array, using constructor arguments; returns reference to constructed element
	template < typename... Args >
	TElement& EmplaceBack( Args&&... args );
	// Construct element at the position specified by iterator 'it', using constructor arguments; 'Result' contains iterator pointing to the constructed element
	template < typename... Args >
	Result Emplace( const_iterator it, Args&&... args );
	// Construct element at the specified 'index', using constructor arguments; 'Result' contains iterator pointing to the constructed element
	template < typename... Args >
	Result EmplaceAt( const Uint32 index, Args&&... args );
	// Remove element specified by iterator 'it'; 'Result' contains iterator pointing to the element next after removed one
	Result Remove( const_iterator it );
	// Remove elements from the range [ first, last ) specified by iterators; Result contains iterator pointing to the element next after the last removed one
	Result Remove( const_iterator first, const_iterator last );
	// Remove element specified by iterator 'it'; reorders array such that the last element is moved to the position of removed element; 'Result' contains iterator pointing to the element next after removed one
	Result RemoveReorder( const_iterator it );
	// Remove 'element from the array (if found); 'Result' contains iterator pointing to the element next after removed one
	Result Remove( const TElement& element );
	// Remove 'element' from the array (if found); reorders array such that the last element is moved to the position of removed element; 'Result' contains iterator pointing to the element next after removed one
	Result RemoveReorder( const TElement& element );
	// Remove element specified by 'index'; 'Result' contains iterator pointing to the element next after removed one
	Result RemoveAt( const Uint32 index );
	// Remove elements from the range [ first, last ) specified by indices; 'Result' contains iterator pointing to the element next after the last removed one
	Result RemoveAt( const Uint32 first, const Uint32 last );
	// Remove element specified by 'index'; reorders array such that the last element is moved to the position of removed element; 'Result' contains iterator pointing to the element next after removed one
	Result RemoveAtReorder( const Uint32 index );

	// Get index of the first occurrence of 'element' in the array; returns -1 if 'element' was not found
	Int32 GetIndex( const TElement& element ) const;
	// Returns true if 'element' is present in the array
	Bool Exist( const TElement& element ) const;
	// Get pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	TElement* FindPtr( const TElement& element );
	// Get const pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	const TElement* FindPtr( const TElement& element ) const;

	// Remove all elements from the array
	void Clear();
	// Change number of elements stored in the array; if new size is bigger, new elements are set to default value; if new size is bigger than capacity, new capacity is equal to size
	void Resize( Uint32 size );
	// Change number of elements stored in the array; if new size is bigger, new elements are set to passed value; if new size is bigger than capacity, new capacity is equal to size
	void Resize( Uint32 size, const TElement& element );

	// Insert 'amount' number of elements at the end of the array; new elements are set to default value; if new size is bigger than capacity, capacity grows by 1.5 factor
	void Grow( Uint32 amount );
	// Change array capacity (up)
	void Reserve( Uint32 capacity );
	// Change array capacity so that it is equal to array size
	void Shrink();

	// Set 'pool' that will be used for memory allocation
	void SetPool( const red::memory::Pool& pool );
	// Get memory pool used for memory allocation
	const red::memory::Pool& GetPool() const;

protected:

	void EnsureBufferSize( Uint32 desiredSize );
	void GrowNoConstruct( Uint32 amount );
	void ResizeBuffer( Uint32 capacity );

	static void MoveAfterReallocation( void* dst, void* src, Uint32 memorySize, const void* context );

	Uint32	m_size;

	friend class ArrayImplUtils;
};

static_assert( sizeof( DynArray< Uint32 > ) == 16, "DynArray should be 16 bytes long" );
static_assert( __alignof( DynArray< Uint32 > ) == 8, "DynArray should be aligned to 8 bytes" );

//////////////////////////////////////////////////////////////////////////
// Enable c++11 range-based for loop

template < typename TElement >
typename DynArray< TElement >::iterator begin( DynArray< TElement >& arr );

template < typename TElement >
typename DynArray< TElement >::iterator end( DynArray< TElement >& arr );

template < typename TElement >
typename DynArray< TElement >::const_iterator begin( const DynArray< TElement >& arr );

template < typename TElement >
typename DynArray< TElement >::const_iterator end( const DynArray< TElement >& arr );

//////////////////////////////////////////////////////////////////////////
// ArraySpan compatibility

template < typename TElement >
TElement* GetStartPtr( DynArray< TElement >& arr );

template < typename TElement >
TElement* GetEndPtr( DynArray< TElement >& arr );

template < typename TElement >
const TElement* GetStartPtr( const DynArray< TElement >& arr );

template < typename TElement >
const TElement* GetEndPtr( const DynArray< TElement >& arr );

} // red

#include "dynArray.hpp"

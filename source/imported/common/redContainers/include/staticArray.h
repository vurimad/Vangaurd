/*
* Copyright (c) 2015-16 CD Projekt Red. All Rights Reserved.
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

template< typename TElement, Uint32 MaxSize >
class StaticArray : public FixedBuffer< MaxSize * sizeof( TElement ), __alignof( TElement ) >
{
	// Avoid wasting time on cascading template errors because of things like converting red::ArraySpan<const T> to red::StaticArray<T> but leaving in the const by accident.
	static_assert(!std::is_const<TElement>::value, "Const value type in array, unlikely to be intentional. Remove this static_assert if truly necessary.");

public:
	typedef TElement									ElementType;
#ifdef RED_CHECKED_ITERATORS
	typedef CheckedIterator< StaticArray >				iterator;
	typedef CheckedConstIterator< StaticArray >			const_iterator;
#else
	typedef ArrayIterator< TElement >					iterator;
	typedef ArrayConstIterator< TElement >				const_iterator;
#endif
	typedef std::reverse_iterator< iterator >			reverse_iterator;
	typedef std::reverse_iterator< const_iterator >		reverse_const_iterator;
	typedef ArrayReverseIteration< StaticArray >		ReverseIteration;
	typedef ArrayReverseConstIteration< StaticArray >	ReverseConstIteration;
	typedef ContainerOpResult< iterator >				Result;
	typedef FixedBuffer< MaxSize * sizeof( TElement ), __alignof( TElement ) >	BufferType;

	// std compatibility
	typedef TElement									value_type;

	// Default constructor
	RED_INLINE StaticArray();
	// Copy constructor
	RED_INLINE StaticArray( const StaticArray& other );
	// Copy constructor
	template < Uint32 OtherMaxSize >
	RED_INLINE StaticArray( const StaticArray< TElement, OtherMaxSize >& other );
	// Move constructor
	RED_INLINE StaticArray( StaticArray&& other );
	// Construct array containing elements from initializer list
	RED_INLINE StaticArray( std::initializer_list< TElement > initializerList );
	// Construct array of given 'size'; elements will be set to default value
	RED_INLINE explicit StaticArray( Uint32 size );
	template< typename DefaultValueType >
	RED_INLINE explicit StaticArray( Uint32 size, const DefaultValueType & value ); 

	// Destructor
	RED_INLINE ~StaticArray();

	// Copy assignment
	RED_INLINE StaticArray& operator=( const StaticArray& other );
	// Copy assignment
	template < Uint32 OtherMaxSize >
	RED_INLINE StaticArray& operator=( const StaticArray< TElement, OtherMaxSize >& other );
	// Move assignment
	RED_INLINE StaticArray& operator=( StaticArray&& other );
	// Assign elements from initializer list
	RED_INLINE StaticArray& operator=( std::initializer_list< TElement > initializerList );

	// Get pointer to the buffer
	using BufferType::Data;
	// Get typed pointer to the buffer
	RED_INLINE TElement* TypedData() { return reinterpret_cast< TElement* >( Data() ); }
	// Get typed const pointer to the buffer
	RED_INLINE const TElement* TypedData() const { return reinterpret_cast< const TElement* >( Data() ); }
	// Get number of elements in the array
	RED_INLINE Uint32 Size() const { return m_size; }
	// Get size of stored data in bytes
	RED_INLINE Uint32 DataSize() const { return m_size * sizeof( TElement ); }
	// Get maximum number of elements that can be stored in the array
	RED_INLINE static constexpr Uint32 Capacity() { return MaxSize; }
	// Get maximum size of data which can be stored in array
	RED_INLINE static constexpr Uint32 DataCapacity() { return Capacity() * sizeof( TElement ); }
	// Returns true if array contains 0 elements
	RED_INLINE Bool Empty() const { return m_size == 0; }
	// Returns true if number of stored elements is equal to 'MaxSize'
	RED_INLINE Bool Full() const { return m_size == MaxSize; }

	// Get iterator pointing to the first element in array
	RED_INLINE iterator Begin() { return iterator( RED_CHECKED_ITERATOR_THIS TypedData() ); }
	// Get iterator pointing to the element next after the last one
	RED_INLINE iterator End() { return iterator( RED_CHECKED_ITERATOR_THIS TypedData() + m_size ); }
	// Get const_iterator pointing to the first element in array
	RED_INLINE const_iterator Begin() const { return const_iterator( RED_CHECKED_ITERATOR_THIS TypedData() ); }
	// Get const_iterator pointing to the element next after the last one
	RED_INLINE const_iterator End() const { return const_iterator( RED_CHECKED_ITERATOR_THIS TypedData() + m_size ); }

	// Get reverse_iterator pointing to the last element in the array
	RED_INLINE reverse_iterator RBegin() { return reverse_iterator( End() ); }
	// Get reverse_iterator pointing to the element before the first one
	RED_INLINE reverse_iterator REnd() { return reverse_iterator( Begin() ); }
	// Get reverse_const_iterator pointing to the last element in the array
	RED_INLINE reverse_const_iterator RBegin() const { return reverse_const_iterator( End() ); }
	// Get reverse_const_iterator pointing to the element before the first one
	RED_INLINE reverse_const_iterator REnd() const { return reverse_const_iterator( Begin() ); }
	// Get range which allows to perform reverse range-based for loop
	RED_INLINE ReverseIteration Reverse() { return ReverseIteration( *this ); }
	// Get range which allows to perform const reverse range-based for loop
	RED_INLINE ReverseConstIteration Reverse() const { return ReverseConstIteration( *this ); }

	// Get range which allows to perform range-based for loop through array indices
	RED_INLINE IndexRange Indices() const { return IndexRange( 0, m_size ); }
	// Get range which allows to perform reverse range-based for loop through array indices
	RED_INLINE ReverseIndexRange ReverseIndices() const { return ReverseIndexRange( m_size, 0 ); }

	// Get reference to the i-th element of the array
	RED_INLINE TElement& operator[]( Uint32 i );
	// Get const reference to the i-th element of the array
	RED_INLINE const TElement& operator[]( Uint32 i ) const;
	// Get reference to the first element
	RED_INLINE TElement& Front();
	// Get const reference to the first element
	RED_INLINE const TElement& Front() const;
	// Get reference to the last element
	RED_INLINE TElement& Back();
	// Get const reference to the last element
	RED_INLINE const TElement& Back() const;

	// Returns true if 'other' contains the same elements
	template < Uint32 OtherMaxSize >
	RED_INLINE Bool operator==( const StaticArray< TElement, OtherMaxSize >& other ) const;
	// Returns true if 'other' does not contain the same elements
	template < Uint32 OtherMaxSize >
	RED_INLINE Bool operator!=( const StaticArray< TElement, OtherMaxSize >& other ) const;

	// Add 'element' to the back of the array
	RED_INLINE void PushBack( const TElement& element );
	// Add 'element' to the back of the array
	RED_INLINE void PushBack( TElement&& element );
	// Adds all elements from 'arr' to the back of the array
	template < Uint32 OtherMaxSize >
	RED_INLINE void PushBack( const StaticArray< TElement, OtherMaxSize >& arr );
	// Adds all elements from 'arr' to the back of the array
	RED_INLINE void PushBack( const ArraySpan< const TElement >& arr );
	// Remove and return the last element from the array
	RED_INLINE TElement PopBack();
	// Insert 'element' at the position specified by iterator 'it'; 'Result' contains iterator pointing to the inserted element
	RED_INLINE Result Insert( const_iterator it, const TElement& element );
	// Insert 'element' at the position specified by iterator 'it'; 'Result' contains iterator pointing to the inserted element
	RED_INLINE Result Insert( const_iterator it, TElement&& element );
	// Insert 'element' at the specified 'index'; 'Result' contains iterator pointing to the inserted element
	RED_INLINE Result InsertAt( const Uint32 index, const TElement& element );
	// Insert 'element' at the specified 'index'; 'Result' contains iterator pointing to the inserted element
	RED_INLINE Result InsertAt( const Uint32 index, TElement&& element );
	// Construct element at the end of the array, using constructor arguments; returns reference to constructed element
	template < typename... Args >
	RED_INLINE TElement& EmplaceBack( Args&&... args );
	// Construct element at the position specified by iterator 'it', using constructor arguments; 'Result' contains iterator pointing to the constructed element
	template < typename... Args >
	RED_INLINE Result Emplace( const_iterator it, Args&&... args );
	// Construct element at the specified 'index', using constructor arguments; 'Result' contains iterator pointing to the constructed element
	template < typename... Args >
	RED_INLINE Result EmplaceAt( const Uint32 index, Args&&... args );
	// Remove element specified by iterator 'it'; 'Result' contains iterator pointing to the element next after removed one
	RED_INLINE Result Remove( const_iterator it );
	// Remove elements from the range [ first, last ) specified by iterators; Result contains iterator pointing to the element next after the last removed one
	RED_INLINE Result Remove( const_iterator first, const_iterator last );
	// Remove element specified by iterator 'it'; reorders array such that the last element is moved to the position of removed element; 'Result' contains iterator pointing to the element next after removed one
	RED_INLINE Result RemoveReorder( const_iterator it );
	// Remove 'element from the array (if found); 'Result' contains iterator pointing to the element next after removed one
	RED_INLINE Result Remove( const TElement& element );
	// Remove 'element' from the array (if found); reorders array such that the last element is moved to the position of removed element; 'Result' contains iterator pointing to the element next after removed one
	RED_INLINE Result RemoveReorder( const TElement& element );
	// Remove element specified by 'index'; 'Result' contains iterator pointing to the element next after removed one
	RED_INLINE Result RemoveAt( const Uint32 index );
	// Remove elements from the range [ first, last ) specified by indices; 'Result' contains iterator pointing to the element next after the last removed one
	RED_INLINE Result RemoveAt( const Uint32 first, const Uint32 last );
	// Remove element specified by 'index'; reorders array such that the last element is moved to the position of removed element; 'Result' contains iterator pointing to the element next after removed one
	RED_INLINE Result RemoveAtReorder( const Uint32 index );

	// Get index of the first occurrence of 'element' in the array; returns -1 if 'element' was not found
	RED_INLINE Int32 GetIndex( const TElement& element ) const;
	// Returns true if 'element' is present in the array
	RED_INLINE Bool Exist( const TElement& element ) const;
	// Get pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	RED_INLINE TElement* FindPtr( const TElement& element );
	// Get const pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	RED_INLINE const TElement* FindPtr( const TElement& element ) const;

	// Remove all elements from the array
	RED_INLINE void Clear();
	// Change number of elements stored in the array; if new size is bigger, new elements are set to default value
	RED_INLINE void Resize( Uint32 size );
	// Change number of elements stored in the array; if new size is bigger, new elements are set to passed value
	RED_INLINE void Resize( Uint32 size, const TElement& element );
	// Insert 'amount' number of elements at the end of the array; new elements are set to default value
	RED_INLINE void Grow( Uint32 amount );

private:

	enum { _Capacity = MaxSize }; // the only way(?) to access MaxSize in .natvis

	void MoveInternal( StaticArray&& other );
	
	// The following "dummy" methods are required by some of ArrayImplUtils
	void GrowNoConstruct( Uint32 amount );
	void ResizeBuffer( Uint32 capacity );

	Uint32	m_size;

	friend class ArrayImplUtils;
};

//////////////////////////////////////////////////////////////////////////
// Enable c++11 range-based for loop

template< typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::iterator begin( StaticArray< TElement, MaxSize >& arr )
{
	return arr.Begin();
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::iterator end( StaticArray< TElement, MaxSize >& arr )
{
	return arr.End();
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::const_iterator begin( const StaticArray< TElement, MaxSize >& arr )
{
	return arr.Begin();
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::const_iterator end( const StaticArray< TElement, MaxSize >& arr )
{
	return arr.End();
}

//////////////////////////////////////////////////////////////////////////
// ArraySpan compatibility

template< typename TElement, Uint32 MaxSize >
RED_INLINE TElement* GetStartPtr( StaticArray< TElement, MaxSize >& arr )
{
	return arr.TypedData();
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE TElement* GetEndPtr( StaticArray< TElement, MaxSize >& arr )
{
	return arr.TypedData() + arr.Size();
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE const TElement* GetStartPtr( const StaticArray< TElement, MaxSize >& arr )
{
	return arr.TypedData();
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE const TElement* GetEndPtr( const StaticArray< TElement, MaxSize >& arr )
{
	return arr.TypedData() + arr.Size();
}

} // red

#include "staticArray.hpp"

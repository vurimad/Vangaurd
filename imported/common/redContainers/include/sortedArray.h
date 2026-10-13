/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dynArray.h"

namespace red {

// TSortPredicate defaults to std::less<> to allow for different type comparisons
template < typename TElement, typename TSortPredicate = std::less<> >
class SortedArray : public DynArray< TElement >
{
public:

	typedef DynArray< TElement >						BaseClass;
	typedef TElement									ElementType;
	typedef typename BaseClass::iterator				iterator;
	typedef typename BaseClass::const_iterator			const_iterator;
	typedef typename BaseClass::reverse_iterator		reverse_iterator;
	typedef typename BaseClass::reverse_const_iterator	reverse_const_iterator;
	typedef typename BaseClass::Result					Result;
	typedef TSortPredicate								SortPredicate;

	// Construct array which uses specified 'pool' for memory allocation
	RED_INLINE SortedArray( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Copy constructor
	RED_INLINE SortedArray( const SortedArray& other );
	// Move constructor
	RED_INLINE SortedArray( SortedArray&& other );
	// Construct array containing elements from initializer list
	RED_INLINE SortedArray( std::initializer_list< TElement > initializerList, const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Construct array of given 'size' which uses specified 'pool' for memory allocation; elements will be set to default value
	RED_INLINE explicit SortedArray( Uint32 size, const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );

	// Copy assignment
	RED_INLINE SortedArray& operator=( const SortedArray& other );
	// Move assignment
	RED_INLINE SortedArray& operator=( SortedArray&& other );
	// Assign elements from initializer list
	RED_INLINE SortedArray& operator=( std::initializer_list< TElement > initializerList );
	// Swap data and pool with 'other'
	RED_INLINE void Swap( SortedArray& other );

	// Get pointer to the allocated buffer
	RED_INLINE void* Data();
	// Get const pointer to the allocated buffer
	RED_INLINE const void* Data() const;
	// Get typed pointer to the allocated buffer
	RED_INLINE TElement* TypedData();
	// Get typed const pointer to the allocated buffer
	RED_INLINE const TElement* TypedData() const;

	// Get iterator pointing to the first element in array
	RED_INLINE iterator Begin();
	// Get iterator pointing to the element next after the last one
	RED_INLINE iterator End();
	// Get const_iterator pointing to the first element in array
	RED_INLINE const_iterator Begin() const;
	// Get const_iterator pointing to the element next after the last one
	RED_INLINE const_iterator End() const;
	// Get reverse_iterator pointing to the last element in the array
	RED_INLINE reverse_iterator RBegin();
	// Get reverse_iterator pointing to the element before the first one
	RED_INLINE reverse_iterator REnd();
	// Get reverse_const_iterator pointing to the last element in the array
	RED_INLINE reverse_const_iterator RBegin() const;
	// Get reverse_const_iterator pointing to the element before the first one
	RED_INLINE reverse_const_iterator REnd() const;

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
	RED_INLINE Bool operator==( const SortedArray& other ) const;
	// Returns true if 'other' does not contain the same elements
	RED_INLINE Bool operator!=( const SortedArray& other ) const;

	// Add 'element' to the back of the array
	RED_INLINE void PushBack( const TElement& element );
	// Add 'element' to the back of the array
	RED_INLINE void PushBack( TElement&& element );
	// Adds all elements from 'arr' to the back of the array
	RED_INLINE void PushBack( const DynArray< TElement >& arr );
	// Adds all elements from 'arr' to the back of the array
	RED_INLINE void PushBack( const ArraySpan< const TElement >& arr );
	// Remove and return the last element from the array
	RED_INLINE TElement PopBack();
	// Insert 'element' to the array without breaking its order; 'Result' contains iterator to the inserted element
	RED_INLINE Result Insert( const TElement& element );
	// Insert 'element' to the array without breaking its order; 'Result' contains iterator to the inserted element
	RED_INLINE Result Insert( TElement&& element );
	// Insert unique element to the array without breaking its order; if successful 'Result' contains iterator to the inserted element; if failure 'Result' contains iterator to the element already present in the array
	RED_INLINE Result InsertUnique( const TElement& element );
	// Insert unique element to the array without breaking its order; if successful 'Result' contains iterator to the inserted element; if failure 'Result' contains iterator to the element already present in the array
	RED_INLINE Result InsertUnique( TElement&& element );
	// Insert 'element' at the position specified by iterator 'it'; 'Result' contains iterator to the inserted element; may break the order
	RED_INLINE Result Insert( const_iterator it, const TElement& element );
	// Insert 'element' at the position specified by iterator 'it'; 'Result' contains iterator to the inserted element; may break the order
	RED_INLINE Result Insert( const_iterator it, TElement&& element );
	// Insert 'element' at the specified 'index'; 'Result' contains iterator to the inserted element; may break the order
	RED_INLINE Result InsertAt( const Uint32 index, const TElement& element );
	// Insert 'element' at the specified 'index'; 'Result' contains iterator to the inserted element; may break the order
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
	// Remove element specified by iterator 'it'; reorders array such that the last element is moved to the position of removed element; 'Result' contains iterator to the element next after removed one
	RED_INLINE Result RemoveReorder( const_iterator it );
	// Remove 'element from the array (if found); 'Result' contains iterator to the element next after removed one
	RED_INLINE Result Remove( const TElement& element );
	// Remove 'element' from the array (if found); reorders array such that the last element is moved to the position of removed element; 'Result' contains iterator to the element next after removed one
	RED_INLINE Result RemoveReorder( const TElement& element );
	// Remove element specified by 'index'; 'Result' contains iterator to the element next after removed one
	RED_INLINE Result RemoveAt( const Uint32 index );
	// Remove elements from the range [ first, last ) specified by indices; 'Result' contains iterator to the element next after the last removed one
	RED_INLINE Result RemoveAt( const Uint32 first, const Uint32 last );
	// Remove element specified by 'index'; reorders array such that the last element is moved to the position of removed element; 'Result' contains iterator to the element next after removed one
	RED_INLINE Result RemoveAtReorder( const Uint32 index );
	using BaseClass::Remove;

	// Get index of the first occurrence of 'element' in the array; returns -1 if 'element' was not found
	RED_INLINE Int32 GetIndex( const TElement& element ) const;
	// Returns true if 'element' is present in the array
	RED_INLINE Bool Exist( const TElement& element ) const;
	// Get iterator to the first occurrence of 'element' in the array; returns End() if 'element' was not found
	RED_INLINE iterator Find( const TElement& element );
	// Get const_iterator to the first occurrence of 'element' in the array; returns End() if 'element' was not found
	RED_INLINE const_iterator Find( const TElement& element ) const;
	// Get pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	RED_INLINE TElement* FindPtr( const TElement& element );
	// Get const pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	RED_INLINE const TElement* FindPtr( const TElement& element ) const;

	// Get index of the first occurrence of 'element' in the array; returns -1 if 'element' was not found
	// NOTE: Using this function will only use the global operator< for TElement and TComparableType
	template < typename TComparableType >
	RED_INLINE Int32 GetIndex( const TComparableType& element ) const;
	// Returns true if 'element' is present in the array
	// NOTE: Using this function will only use the global operator< for TElement and TComparableType
	template < typename TComparableType >
	RED_INLINE Bool Exist( const TComparableType& element ) const;
	// Get iterator to the first occurrence of 'element' in the array; returns End() if 'element' was not found
	// NOTE: Using this function will only use the global operator< for TElement and TComparableType
	template < typename TComparableType >
	RED_INLINE iterator Find( const TComparableType& element );
	// Get const_iterator to the first occurrence of 'element' in the array; returns End() if 'element' was not found
	// NOTE: Using this function will only use the global operator< for TElement and TComparableType
	template < typename TComparableType >
	RED_INLINE const_iterator Find( const TComparableType& element ) const;
	// Get pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	// NOTE: Using this function will only use the global operator< for TElement and TComparableType
	template < typename TComparableType >
	RED_INLINE TElement* FindPtr( const TComparableType& element );
	// Get const pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	// NOTE: Using this function will only use the global operator< for TElement and TComparableType
	template < typename TComparableType >
	RED_INLINE const TElement* FindPtr( const TComparableType& element ) const;

	// Remove all elements from the array
	RED_INLINE void Clear();
	// Change number of elements stored in the array; if new size is bigger, new elements are set to default value; if new size is bigger than capacity, new capacity is equal to size
	RED_INLINE void Resize( Uint32 size );
	void Resize( Uint32 size, const TElement& element );
	// Insert 'amount' number of elements at the end of the array; new elements are set to default value; if new size is bigger than capacity, capacity grows by 1.5 factor
	RED_INLINE void Grow( Uint32 amount );

	// Sort array
	RED_INLINE void Sort();
	// Sort array using stable sort algorithm (insertion sort)
	RED_INLINE void StableSort();
	// Returns true if array is sorted
	RED_INLINE Bool IsSorted() const;
	// Returns true if array is sorted, updates the internal dirty flag
	RED_INLINE bool TestSorted();
	// Get TSortPredicate object
	RED_INLINE TSortPredicate GetSortPredicate() const { return TSortPredicate(); }
	// Returns true if array is in 'dirty' state
	RED_INLINE Bool IsDirty() const { return ( m_flags & Flag_IsDirty ) != 0; }
	// Sort array (using StableSort) and make its state 'clean'
	RED_INLINE void MakeClean() const;

	// Sort array and get rid of duplicate entries, also mark the state as clean
	RED_INLINE void MakeUnique();
	// Sort array and get rid of duplicate entries, also mark the state as clean
	RED_INLINE void MakeUnique() const;

private:

	enum Flags : Uint32
	{
		Flag_IsDirty	= RED_FLAG( 0 ),
	};

	Uint32	m_flags;

	RED_INLINE void SetIsDirty( Bool isDirty );
	RED_INLINE void MakeDirty( const_iterator it, Bool afterRemove = false );
};

#include "sortedArray.hpp"

} // red

/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "sortedArray.h"
#include "arraySpan.h"

class SetSerializer;

//////////////////////////////////////////////////////////////////////////
// Set implementation based on sorted array.
// - insertion/removal - time O(n)
// - access - time O(log(n))
// - iteration same as for array
// - iterated elements are sorted
// - iterators are constant
// - no additional memory overhead for storing elements
// - union/intersection/difference - time/memory O(n)
//////////////////////////////////////////////////////////////////////////

namespace red {

template < typename TElement, typename TSortPredicate = std::less< TElement > >
class Set
{
	typedef SortedArray< TElement, TSortPredicate > ElementsType;

public:

	typedef TElement ElementType;
	typedef typename ElementsType::const_iterator iterator;
	typedef ContainerOpResult< iterator > Result;
	typedef TSortPredicate SortPredicate;

	// Construct set which uses specified 'pool' for memory allocation
	RED_INLINE Set( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Construct set having initial capacity set to 'initialCapacity', using specified 'pool' for memory allocation
	RED_INLINE explicit Set( Uint32 initialCapacity, const red::memory::Pool& pool );
	// Copy constructor
	RED_INLINE Set( const Set& other );
	// Move constructor
	RED_INLINE Set( Set&& other );
	// Construct set containing elements from initializer list, using specified 'pool' for memory allocation
	RED_INLINE Set( std::initializer_list< TElement > initializerList, const red::memory::Pool& pool );
	// Destructor
	RED_INLINE ~Set();

	// Copy assignment
	RED_INLINE Set& operator=( const Set& other );
	// Move assignment
	RED_INLINE Set& operator=( Set&& other );
	// Assign elements from initializer list
	RED_INLINE Set& operator=( std::initializer_list< TElement > initializerList );
	// Swap data with 'other'
	RED_INLINE void Swap( Set& other );

	// Get number of elements in the set
	RED_INLINE Uint32 Size() const { return m_elements.Size(); }
	// Get size of stored data in bytes
	RED_INLINE Uint32 DataSize() const { return m_elements.DataSize(); }
	// Get maximum number of elements that can be stored in the set without need of reallocation
	RED_INLINE Uint32 Capacity() const { return m_elements.Capacity(); }
	// Get maximum size of data which can be stored in set without need of reallocation, in bytes
	RED_INLINE Uint32 DataCapacity() const { return m_elements.DataCapacity(); }
	// Returns true if set contains 0 elements
	RED_INLINE Bool Empty() const { return m_elements.Empty(); }

	// Get iterator pointing to the first element in the set
	RED_INLINE iterator Begin() const { return m_elements.Begin(); }
	// Get iterator pointing to the element next after the last one
	RED_INLINE iterator End() const { return m_elements.End(); }

	// Get reference to the entry that is equal (in terms of TSortPredicate) to specified element; if entry equal to element is not present in the set, new entry will be added with value set to 'defaultValue'
	template < typename TComparableType >
	const TElement& GetRef( const TComparableType& element, const TElement& defaultValue = TElement() );
	// Get DynArray of elements
	RED_INLINE void GetElements( DynArray< TElement >& elements ) const;
	// Access a view of the elements
	RED_INLINE ArraySpan< const TElement > Elements() const;

	// Returns true if 'other' contains the same elements
	RED_INLINE Bool operator==( const Set& other ) const;
	// Returns true if 'other' does not contain the same elements
	RED_INLINE Bool operator!=( const Set& other ) const;

	// Insert 'element' to the set; if element was already present in the set, returned 'Result' denotes failure; 'Result' contains iterator pointing to the 'element'
	RED_INLINE Result Insert( const TElement& element );
	// Insert 'element' to the set; if element was already present in the set, returned 'Result' denotes failure; 'Result' contains iterator pointing to the 'element'
	RED_INLINE Result Insert( TElement&& element );
	// Insert 'element' to the set; may break keys order and doesn't keep elements uniqueness
	RED_INLINE void InsertUnsorted( const TElement& element );
	// Insert 'element' to the set; may break keys order and doesn't keep elements uniqueness
	RED_INLINE void InsertUnsorted( TElement&& element );
	// Insert 'elements' to the set; may break keys order and doesn't keep elements uniqueness
	RED_INLINE void InsertUnsorted( const red::DynArray< TElement >& elements );
	// Insert 'elements' to the set; may break keys order and doesn't keep elements uniqueness
	RED_INLINE void InsertUnsorted( const red::ArraySpan< TElement >& elements );
	// Insert element = TElement( args... ) to the set; may break keys order and doesn't keep elements uniqueness;
	template < typename... Args >
	RED_INLINE void EmplaceUnsorted( Args&&... args );
	// Remove 'element' from the set; if there's no such 'element', returned 'Result' denotes failure; if successful 'Result' contains iterator to the element next after removed one
	template < typename TComparableType >
	RED_INLINE Result Remove( const TComparableType& element );
	// Remove element specified by iterator 'it'; 'Result' contains iterator to the element next after removed one
	RED_INLINE Result Remove( iterator it );
	template < typename TPredicate >
	void RemoveIf( TPredicate predicate );

	// Get iterator pointing to the specified 'element'; returns End() if 'element' was not found
	template < typename TComparableType >
	RED_INLINE iterator Find( const TComparableType& element ) const;
	// Get const pointer to the 'element' stored in the set; returns nullptr if 'element' was not found
	template < typename TComparableType >
	RED_INLINE const TElement* FindPtr( const TComparableType& element ) const;
	// Returns true if specified 'element' is present in the set
	template < typename TComparableType >
	RED_INLINE Bool Exist( const TComparableType& element ) const;

	// Remove all elements from the set
	RED_INLINE void Clear();
	// Change set capacity to 'newCapacity'
	RED_INLINE void Reserve( Uint32 newCapacity );
	// Change set capacity so that it is equal to set size
	RED_INLINE void Shrink();

	// Add all elements from the 'other' set
	void Union( const Set& other );
	// Remove all elements not present in the 'other' set
	void Intersection( const Set& other );
	// Remove all elements present in the 'other' set
	void Difference( const Set& other );

	// Set 'pool' that will be used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool );
	// Get memory pool used for memory allocation
	RED_INLINE const red::memory::Pool& GetPool() const;

	// Returns true if set is in 'dirty' state
	RED_INLINE Bool IsDirty() const { return m_elements.IsDirty(); }
	// Change set state to clean (sort and remove non-unqiue items)
	RED_INLINE void MakeClean() const { m_elements.MakeUnique(); }
	// Tests if the set is sorted and marks the flag appropriately
	RED_INLINE bool TestSorted() { return m_elements.TestSorted(); }
	RED_INLINE bool IsSorted() const { return m_elements.IsSorted(); }

private:

	ElementsType m_elements;

	friend class ::SetSerializer;
};

template <typename TElement>
Set< TElement > MakeSetFromSortedArray( const red::DynArray< TElement >& elements, const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE )
{
	Set< TElement > result{ elements.Size(), pool };
	result.InsertUnsorted( elements );
	result.TestSorted();
	return result;
}

template <typename TElement>
Set< TElement > MakeSetFromSortedArray( const red::ArraySpan< TElement >& elements, const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE )
{
	Set< TElement > result{ elements.Size(), pool };
	result.InsertUnsorted( elements );
	result.TestSorted();
	return result;
}

//////////////////////////////////////////////////////////////////////////
// Enable C++11 range-based for loop

template < typename TElement, typename TSortPredicate >
typename Set< TElement, TSortPredicate >::iterator begin( const Set< TElement, TSortPredicate >& set )
{
	return set.Begin();
}

template < typename TElement, typename TSortPredicate >
typename Set< TElement, TSortPredicate >::iterator end( const Set< TElement, TSortPredicate >& set )
{
	return set.End();
}

} // red

#include "set.hpp"


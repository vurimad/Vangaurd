/*
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "pool.h"
#include "hash.h"
#include "containerOpResult.h"

class HashSetSerializer;

namespace red {

//////////////////////////////////////////////////////////////////////////
// Notes on implementation:
// - fast insert/remove/find
// - fast iteration (as fast as iterating an array)
// - overhead per element: sizeof( Bucket ) + sizeof( BucketElement ) = 24 bytes
//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename THashPolicy = DefaultHashPolicy< TElement > >
class HashSet
{
public:

	class iterator
	{
	public:

		typedef ArrayIteratorTag Tag;
		typedef const TElement* PtrType;
		typedef const TElement& RefType;
		typedef Uint32 DiffType;

		// std compatibility
		typedef std::forward_iterator_tag iterator_category;
		typedef TElement value_type;
		typedef DiffType difference_type;
		typedef PtrType pointer;
		typedef RefType reference;

		RED_INLINE iterator() : m_current( nullptr ) {}
		RED_INLINE iterator( const iterator& it ) { m_current = it.m_current; }
		RED_INLINE iterator& operator=( const iterator& it ) { if ( this != &it ) { m_current = it.m_current; } return *this; }
		RED_INLINE RefType operator*() const { return *m_current; }
		RED_INLINE PtrType operator->() const { return m_current; }
		RED_INLINE iterator& operator++() { ++m_current; return *this; }
		RED_INLINE Bool operator==( const iterator& it ) const { return m_current == it.m_current; }
		RED_INLINE Bool operator!=( const iterator& it ) const { return m_current != it.m_current; }
		RED_INLINE Bool operator<( const iterator& it ) const { return m_current < it.m_current; }
		RED_INLINE Bool operator<=( const iterator& it ) const { return m_current <= it.m_current; }
		RED_INLINE Bool operator>( const iterator& it ) const { return m_current > it.m_current; }
		RED_INLINE Bool operator>=( const iterator& it ) const { return m_current >= it.m_current; }

	private:

		RED_INLINE iterator( const TElement* current ) : m_current( current ) {}

		const TElement* m_current;

		friend class HashSet;
	};

	typedef TElement ElementType;
	typedef ContainerOpResult< iterator > Result;
	typedef typename THashPolicy::HashFunc HashFunc;
	typedef typename THashPolicy::EqualFunc EqualFunc;

	// Construct set which uses specified 'pool' for memory allocation
	RED_INLINE HashSet( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Construct set having initial capacity set to 'initialCapacity', using specified 'pool' for memory allocation
	RED_INLINE explicit HashSet( Uint32 initialCapacity, const red::memory::Pool& pool );
	// Copy constructor
	RED_INLINE HashSet( const HashSet& other );
	// Move constructor
	RED_INLINE HashSet( HashSet&& other );
	// Construct set containing elements from initializer list, using specified 'pool' for memory allocation
	RED_INLINE HashSet( std::initializer_list< TElement > initializerList, const red::memory::Pool& pool );
	// Destructor
	RED_INLINE ~HashSet();

	// Copy assignment
	RED_INLINE HashSet& operator=( const HashSet& other );
	// Move assignment
	RED_INLINE HashSet& operator=( HashSet&& other );
	// Assign elements from initializer list
	RED_INLINE HashSet& operator=( std::initializer_list< TElement > initializerList );
	// Swap data with 'other'
	RED_INLINE void Swap( HashSet& other );

	// Get number of elements in the set
	RED_INLINE Uint32 Size() const { return m_size; }
	// Get size of stored data in bytes
	RED_INLINE Uint32 DataSize() const { return m_size * ( sizeof( BucketElement ) + sizeof( Bucket ) + sizeof( TElement ) ); }
	// Get maximum number of elements that can be stored in the set without need of reallocation
	RED_INLINE Uint32 Capacity() const { return m_capacity; }
	// Get maximum size of data which can be stored in set without need of reallocation, in bytes
	RED_INLINE Uint32 DataCapacity() const { return m_capacity * ( sizeof( BucketElement ) + sizeof( Bucket ) + sizeof( TElement ) ); }
	// Returns true if set contains 0 elements
	RED_INLINE Bool Empty() const { return m_size == 0; }

	// Get iterator pointing to the first element in the set
	RED_INLINE iterator Begin() const { return iterator( m_elements ); }
	// Get iterator pointing to the element next after the last one
	RED_INLINE iterator End() const { return iterator( m_elements + m_size ); }

	// Get reference to the entry that is equal (in terms of HashFunc/EqualFunc) to specified element; if entry equal to element is not present in the set, new entry will be added with value set to 'defaultValue'
	template < typename TCompatibleType >
	RED_INLINE const TElement& GetRef( const TCompatibleType& element, const TElement& defaultValue = TElement() );
	// Get DynArray of elements
	RED_INLINE void GetElements( DynArray< TElement >& elements ) const;
	// Access a view of the elements
	RED_INLINE ArraySpan< const TElement > Elements() const;

	// Returns true if 'other' contains the same elements
	RED_INLINE Bool operator==( const HashSet& other ) const;
	// Returns true if 'other' does not contain the same elements
	RED_INLINE Bool operator!=( const HashSet& other ) const;

	// Insert 'element' to the set; if element was already present in the set, returned 'Result' denotes failure; 'Result' contains iterator pointing to the 'element'
	RED_INLINE Result Insert( const TElement& element );
	// Insert 'element' to the set; if element was already present in the set, returned 'Result' denotes failure; 'Result' contains iterator pointing to the 'element'
	RED_INLINE Result Insert( TElement&& element );
	// Insert element = TElement( args... ) to the set; destructor will be called for newly constructed element if it was already present in the set; 'Result' contains iterator pointing to the 'element'
	template < typename... Args >
	RED_INLINE Result Emplace( Args&&... args );
	// Remove 'element' from the set; if there's no such 'element', returned 'Result' denotes failure; if successful 'Result' contains iterator to the element next after removed one
	template < typename TCompatibleType >
	Result Remove( const TCompatibleType& element );
	// Remove element specified by iterator 'it'; 'Result' contains iterator to the element next after removed one
	RED_INLINE Result Remove( iterator it );

	// Get iterator pointing to the specified 'element'; returns End() if 'element' was not found
	template < typename TCompatibleType >
	RED_INLINE iterator Find( const TCompatibleType& element ) const;
	// Get const pointer to the 'element' stored in the set; returns nullptr if 'element' was not found
	template < typename TCompatibleType >
	RED_INLINE const TElement* FindPtr( const TCompatibleType& element ) const;
	// Returns true if specified 'element' is present in the set
	template < typename TCompatibleType >
	RED_INLINE Bool Exist( const TCompatibleType& element ) const;

	// Remove all elements from the set
	RED_INLINE void Clear();
	// Change set capacity to 'newCapacity'
	RED_INLINE void Reserve( Uint32 newCapacity );
	// Change set capacity so that it is equal to set size
	RED_INLINE void Shrink();

	// Add all elements from the 'other' set
	RED_INLINE void Union( const HashSet& other );
	// Remove all elements not present in the 'other' set
	RED_INLINE void Intersection( const HashSet& other );
	// Remove all elements present in the 'other' set
	RED_INLINE void Difference( const HashSet& other );

	// Set 'pool' that will be used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool );
	// Get memory pool used for memory allocation
	RED_INLINE const red::memory::Pool& GetPool() const;

private:

	static const Uint32 UNUSED_ELEMENT_INDEX = 0xFFFFFFFF;

	struct BucketElement
	{
		Uint32 m_hash;		// Cached hash of the element
		Uint32 m_index;		// Index of the element in m_elements; UNUSED_ELEMENT_INDEX if element is unused
		Uint32 m_nextId;	// Id of the next bucket element in the bucket; INVALID_INDEX if there's no next element
	};

	// Each bucket stores fixed number of in-place elements; every next element is on the linked list
	struct Bucket : BucketElement
	{
		RED_INLINE Uint32 IsUsed() const { return BucketElement::m_index != UNUSED_ELEMENT_INDEX; }
		RED_INLINE void SetUnused() { BucketElement::m_index = UNUSED_ELEMENT_INDEX; }
	};

	template < typename TElementRef >
	TElement* InsertNoFail( TElementRef&& element, Uint32 hash );
	void RemoveElementAt( Uint32 index );
	RED_INLINE void Upsize();
	void Rehash( Uint32 newCapacity );

	RED_INLINE red::memory::Pool& GetPoolRef();

	template < typename TCompatibleType >
	RED_INLINE TElement* FindInternal( const TCompatibleType& element, Uint32* outHash = nullptr );
	template < typename TCompatibleType >
	const TElement* FindInternal( const TCompatibleType& element, Uint32* outHash = nullptr ) const;

	TElement* m_elements;		// An array of stored elements
	Bucket* m_buckets;			// Buckets with lists of elements with matching hashes modulo m_capacity
	Uint32 m_size;				// Number of elements stored
	Uint32 m_capacity;			// Capacity
	Pool m_elementsPool;		// Pool of BucketElements for use in linked lists
	struct { Uint8 m_buffer[ 8 ]; }	m_pool; // Pool used for memory allocation

	friend class ::HashSetSerializer;
};

static_assert( sizeof( red::memory::Pool ) <= 8, "HashSet cannot be used with memory pool objects bigger than 8 bytes" );

//////////////////////////////////////////////////////////////////////////
// Enable C++11 range-based for loop

template < typename TElement, typename THashPolicy >
typename HashSet< TElement, THashPolicy >::iterator begin( const HashSet< TElement, THashPolicy >& set )
{
	return set.Begin();
}

template < typename TElement, typename THashPolicy >
typename HashSet< TElement, THashPolicy >::iterator end( const HashSet< TElement, THashPolicy >& set )
{
	return set.End();
}

} // red

#include "hashSet.hpp"


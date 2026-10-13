/*
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class HashMapSerializer;

namespace red {

//////////////////////////////////////////////////////////////////////////
// Notes on performance:
// - expect FAST insert()/remove()/find() of O(1) on average (average bucket size across various hash maps used in the engine was between 1.5 and 2)
// - per element memory overhead of 12 bytes
//
// Notes on implementation:
// - only uses single memory allocation for all of its internal data
// - only reallocates its memory on rehash
// - attempts to maintain bucket count such as to make insert/remove/find operations fast:
// - when inserting: no less than N and no more than 1.5N capacity
// - caches hash value per element to speed up look ups
//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy = DefaultHashPolicy< TKey > >
class HashMap
{
	struct BucketElement;

public: 

	typedef TKey KeyType;
	typedef TValue ValueType;

	struct ElementType
	{
	public:

		RED_INLINE ElementType( const KeyType& key, ValueType& value );
		RED_INLINE const KeyType& Key() const { return m_key; }
		RED_INLINE const ValueType& Value() const { return m_value; }
		RED_INLINE ValueType& Value() { return m_value; }

	private:

		const KeyType& m_key;
		ValueType& m_value;
	};

	// std compatibility
	typedef KeyType key_type;
	typedef ValueType mapped_type;
	typedef ElementType value_type;

	class const_iterator
	{
	public:

		typedef MapIteratorTag Tag;
		typedef const ElementType* PtrType;
		typedef const ElementType& RefType;
		typedef Uint32 DiffType;

		// std compatibility
		typedef std::forward_iterator_tag iterator_category;
		typedef ElementType value_type;
		typedef DiffType difference_type;
		typedef PtrType pointer;
		typedef RefType reference;

		RED_INLINE const_iterator();
		RED_INLINE const_iterator( const const_iterator& it );
		RED_INLINE const_iterator& operator=( const const_iterator& it );
		RED_INLINE const TKey& Key() const;
		RED_INLINE const TValue& Value() const;
		RED_INLINE const ElementType operator*() const;
		RED_INLINE Bool operator==( const const_iterator& it ) const;
		RED_INLINE Bool operator!=( const const_iterator& it ) const;
		RED_INLINE const_iterator& operator++();

	private:

		RED_INLINE const_iterator( const HashMap* map, Uint32 bucketIndex, const BucketElement* element );
		RED_INLINE const_iterator( const HashMap* map );

		HashMap* m_map;
		Uint32 m_bucketIndex;
		BucketElement* m_element;

		friend class HashMap;
	};

	class iterator : public const_iterator
	{
	public:

		typedef MapIteratorTag Tag;
		typedef ElementType* PtrType;
		typedef ElementType& RefType;
		typedef Uint32 DiffType;

		// std compatibility
		typedef std::forward_iterator_tag iterator_category;
		typedef ElementType value_type;
		typedef DiffType difference_type;
		typedef PtrType pointer;
		typedef RefType reference;

		RED_INLINE iterator();
		RED_INLINE iterator( const iterator& it );
		RED_INLINE iterator& operator=( const iterator&it );
		RED_INLINE TValue& Value() const;
		RED_INLINE ElementType operator*() const;
		RED_INLINE iterator& operator++();

	private:

		RED_INLINE iterator( HashMap* map, Uint32 bucketIndex, BucketElement* element );
		RED_INLINE iterator( HashMap* map );

		friend class HashMap;
	};

	typedef ContainerOpResult< iterator > Result;
	typedef typename THashPolicy::HashFunc HashFunc;
	typedef typename THashPolicy::EqualFunc EqualFunc;

	// Construct map which uses specified 'pool' for memory allocation
	RED_INLINE HashMap( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Construct map having initial capacity set to 'initialCapacity', using specified 'pool' for memory allocation
	RED_INLINE explicit HashMap( Uint32 initialCapacity, const red::memory::Pool& pool );
	// Copy constructor
	RED_INLINE HashMap( const HashMap& other );
	// Move constructor
	RED_INLINE HashMap( HashMap&& other );
	// Destructor
	RED_INLINE ~HashMap();

	// Copy assignment
	RED_INLINE HashMap& operator=( const HashMap& other );
	// Move assignment
	RED_INLINE HashMap& operator=( HashMap&& other );
	// Swap data with 'other'
	RED_INLINE void Swap( HashMap& other );

	// Get number of elements in the map
	RED_INLINE Uint32 Size() const { return m_size; }
	// Get size of stored data in bytes
	RED_INLINE Uint32 DataSize() const { return m_size * ( sizeof( BucketElement ) + sizeof( BucketElement* ) ); }
	// Get maximum number of elements that can be stored in the map without need of reallocation
	RED_INLINE Uint32 Capacity() const { return m_capacity;	}
	// Get maximum size of data which can be stored in map without need of reallocation, in bytes
	RED_INLINE Uint32 DataCapacity() const { return m_capacity * ( sizeof( BucketElement ) + sizeof( BucketElement* ) ); }
	// Returns true if map contains 0 elements
	RED_INLINE Bool Empty() const {	return m_size == 0;	}	

	// Get iterator pointing to the first element in the map
	RED_INLINE iterator Begin() { return iterator( this ); }
	// Get iterator pointing to the element next after the last one
	RED_INLINE iterator End() { return iterator( this, m_capacity, nullptr ); }
	// Get const_iterator pointing to the first element in the map
	RED_INLINE const_iterator Begin() const { return const_iterator( this ); }
	// Get const_iterator pointing to the element next after the last one
	RED_INLINE const_iterator End() const	{ return const_iterator( this, m_capacity, nullptr ); }

	// Get reference to the value associated with specified 'key'; if 'key' is not present in the map, new entry will be added with value set to TValue default
	RED_INLINE TValue& operator[]( const TKey& key );
	// Get reference to the value associated with specified 'key'; if 'key' is not present in the map, fatal assert is reported
	RED_INLINE const TValue& operator[]( const TKey& key ) const;
	// Get reference to the value associated with specified 'key'; if 'key' is not present in the map, new entry will be added with value set to 'defaultValue'
	template < typename TCompatibleType >
	RED_INLINE TValue& GetRef( const TCompatibleType& key, const TValue& defaultValue = TValue() );
	// Get DynArray of keys
	RED_INLINE void GetKeys( DynArray< TKey >& keys ) const;
	// Return a DynArray of keys
	RED_INLINE DynArray< TKey > GetKeys() const;
	// Get DynArray of (possibly duplicated) values
	RED_INLINE void GetValues( DynArray< TValue >& values ) const;
	// Return a DynArray of values
	RED_INLINE DynArray< TValue > GetValues() const;

	// Returns true if 'other' contains the same elements
	RED_INLINE Bool operator==( const HashMap& other ) const;
	// Returns true if 'other' does not contain the same elements
	RED_INLINE Bool operator!=( const HashMap& other ) const;

	// Insert ( key, value ) pair to the map; if key was already present in the map, returned 'Result' denotes failure; returns iterator to ( key, value ) pair
	RED_INLINE Result Insert( const TKey& key, const TValue& value );
	// Insert ( key, value ) pair to the map; if key was already present in the map, returned 'Result' denotes failure; returns iterator to ( key, value ) pair
	RED_INLINE Result Insert( const TKey& key, TValue&& value );
	// Set 'value' for specified 'key'; if key wasn't present in the map, new ( key, value ) pair is added; returns iterator to ( key, value ) pair
	RED_INLINE Result Set( const TKey& key, const TValue& value );
	// Set 'value' for specified 'key'; if key wasn't present in the map, new ( key, value ) pair is added; returns iterator to ( key, value ) pair
	RED_INLINE Result Set( const TKey& key, TValue&& value );
	// Construct value = TValue( args... ) element under 'key' entry; if key was already present in the map, returned 'Result' denotes failure; returns iterator to ( key, value ) pair
	template < typename... Args >
	RED_INLINE Result Emplace( const TKey& key, Args&&... args );
	// Remove entry for a given 'key'; if there's no such element, returned 'Result' denotes failure; if successful 'Result' contains iterator to the element next after removed one
	Result Remove( const TKey& key );
	// Remove element specified by iterator 'it'; if there's no such element, returned 'Result' denotes failure; if successful 'Result' contains iterator to the element next after removed one
	Result Remove( const_iterator it );
	// Remove first entry with given 'value'; if there's no such element, returned 'Result' denotes failure; if successful 'Result' contains iterator to the element next after removed one
	RED_INLINE Result RemoveValue( const TValue& value );

	// Get iterator to the entry with specified 'key'; returns End() if 'key' was not found
	template < typename TCompatibleType >
	RED_INLINE iterator Find( const TCompatibleType& key );
	// Get const_iterator to the entry with specified 'key'; returns End() if 'key' was not found
	template < typename TCompatibleType >
	RED_INLINE const_iterator Find( const TCompatibleType& key ) const;
	// Get 'value' associated with specified 'key'; returns true iff specified 'key' was found;
	template < typename TCompatibleType >
	RED_INLINE Bool Find( const TCompatibleType& key, TValue& value ) const;
	// Get pointer to the value associated with specified 'key'; returns nullptr if 'key' was not found
	template < typename TCompatibleType >
	RED_INLINE TValue* FindPtr( const TCompatibleType& key );
	// Get const pointer to the value associated with specified 'key'; returns nullptr if 'key' was not found
	template < typename TCompatibleType >
	RED_INLINE const TValue* FindPtr( const TCompatibleType& key ) const;
	// Get iterator to the first entry with given 'value'; returns End() if 'value' was not found
	RED_INLINE iterator FindValue( const TValue& value );
	// Get const_iterator to the first entry with given 'value'; returns End() if 'value' was not found
	RED_INLINE const_iterator FindValue( const TValue& value ) const;
	// Returns true if specified 'key' is present in the map
	template < typename TCompatibleType >
	RED_INLINE Bool KeyExist( const TCompatibleType& key ) const;

	// Remove all elements from the map
	RED_INLINE void Clear();
	// Change map capacity to 'newCapacity'
	RED_INLINE void Reserve( Uint32 newCapacity );
	// Change map capacity so that it is equal to map size
	RED_INLINE void Shrink();

	// Set 'pool' that will be used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool );
	// Get memory pool used for memory allocation
	RED_INLINE const red::memory::Pool& GetPool() const;

private:

	using ElementIndex = Uint32;
	static constexpr ElementIndex InvalidElementIndex = std::numeric_limits< ElementIndex >::max();

	struct BucketElement
	{
		ElementIndex m_nextIndex;	// Index to the next element in the bucket
		Uint32 m_hash;				// Cached hash of the key
		TKey m_key;
		TValue m_value;

		template < typename... Args >
		RED_INLINE BucketElement( const TKey& key, Args&&... args );
		RED_INLINE BucketElement( BucketElement&& element );
		RED_INLINE void Destroy() { m_key.~TKey(); m_value.~TValue(); }
	};

	struct InsertPolicy
	{
		template < typename TValueRef >
		RED_INLINE static Result Execute( HashMap* map, Uint32 bucketIndex, BucketElement* element, TValueRef&& value );
	};

	struct SetPolicy
	{
		template < typename TValueRef >
		RED_INLINE static Result Execute( HashMap* map, Uint32 bucketIndex, BucketElement* element, TValueRef&& value );
	};

	template < typename TCompatbileType, typename TValueProxy >
	TValue& GetRefOrInsert( const TCompatbileType& key, const TValueProxy& valueProxy );

	RED_INLINE Bool PreInsert();
	template < typename TDuplicatePolicy, typename TValueRef, typename... Args >
	Result InsertInternal( const TKey& key, TValueRef&& value, Args&&... args );
	void Rehash( Uint32 newCapacity );

	RED_INLINE red::memory::Pool& GetPoolRef();

	template < typename TCompatibleType >
	RED_INLINE Bool FindInternal( const TCompatibleType& key, Uint32& bucketIndexOut, BucketElement*& elementOut );
	template < typename TCompatibleType >
	Bool FindInternal( const TCompatibleType& key, Uint32& bucketIndexOut, const BucketElement*& elementOut ) const;

	ElementIndex* m_buckets;	// Buckets with lists of elements with matching hashes
	Uint32 m_size;				// Number of elements stored
	Uint32 m_capacity;			// Capacity
	Pool m_bucketsPool;			// Fixed size memory block pool
	struct { Uint8 m_buffer[ 8 ]; }	m_pool; // Pool used for memory allocation

	friend class ::HashMapSerializer;
};

static_assert( sizeof( red::memory::Pool ) <= 8, "HashMap cannot be used with memory pool objects bigger than 8 bytes" );

//////////////////////////////////////////////////////////////////////////
// Enable c++11 range-based for loop

template < typename TKey, typename TValue, typename THashPolicy >
typename HashMap< TKey, TValue, THashPolicy >::iterator begin( HashMap< TKey, TValue, THashPolicy >& map )
{
	return map.Begin();
}

template < typename TKey, typename TValue, typename THashPolicy >
typename HashMap< TKey, TValue, THashPolicy >::iterator end( HashMap< TKey, TValue, THashPolicy >& map )
{
	return map.End();
}

template < typename TKey, typename TValue, typename THashPolicy >
typename HashMap< TKey, TValue, THashPolicy >::const_iterator begin( const HashMap< TKey, TValue, THashPolicy >& map )
{
	return map.Begin();
}

template < typename TKey, typename TValue, typename THashPolicy >
typename HashMap< TKey, TValue, THashPolicy >::const_iterator end( const HashMap< TKey, TValue, THashPolicy >& map )
{
	return map.End();
}

} // red

#include "hashMap.hpp"


/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "serializable.h"

namespace rtti
{
	class ClassType;
}

namespace obj
{
	typedef red::HashMap< Uint64, SerializableWeakHandle > SerializableRegistry;

	// Iterating through all objects stored in SerializableMap filtered by specified type
	template < typename T >
	class SerializableMapIterator
	{
	public:

		RED_INLINE SerializableMapIterator( SerializableRegistry::iterator iter, SerializableRegistry::iterator end );

		RED_INLINE Bool operator!=( const SerializableMapIterator& other );
		RED_INLINE void operator++();
		RED_INLINE WeakHandle< T > operator*() const;
		RED_INLINE Bool IsValid() const;

	private:

		SerializableRegistry::iterator m_iterator;
		SerializableRegistry::iterator m_end;
		const rtti::ClassType* m_classFilter;
	};

	//-------------------------------------------------

	// ISerializable objects registry.
	// All objects which class is a derivative of ISerializable and which can be accessed by a handle are registered to SerializableMap.
	// Objects are identified by SerializableID, which can be globally unique (so stored in SerializableStorageGlobal) or runtime
	// (so stored in SerializableStorageRuntime).
	class RED_REFLECTION_API SerializableMap
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		SerializableMap();

		template < typename T >
		struct TypedIteration
		{
			SerializableMap*	m_map;

			RED_INLINE TypedIteration( SerializableMap* map )
				: m_map( map )
			{}
		};

		// Register ISerializable with given id to map
		void Register( SerializableID id, SerializableWeakHandle serializable );

		// Unregister ISerializable with given id from map
		void Unregister( SerializableID id );

		// Find ISerializable object with given SerializableID. Returns false if failed to find object with given id.
		Bool FindSerializable( const SerializableID id, SerializableHandle& outSerializable ) const;

		// Get all objects stored in map
		void GetAll( red::DynArray< SerializableWeakHandle >& objects ) const;

		// Iterator pointing to the first element in map
		template < typename T >
		RED_INLINE SerializableMapIterator< T > Begin();

		// Iterator pointing to the element next after the last one
		template < typename T >
		RED_INLINE SerializableMapIterator< T > End();

		// Get structure defining iteration over elements of type T
		template < typename T >
		RED_INLINE TypedIteration< T > OfType() { return TypedIteration< T >( this ); }

		void Disable(); 

	private:

		typedef red::RWSpinLock LockPrimitive;
		typedef red::ScopedLock< LockPrimitive > SerializableMapLock;
		typedef red::ScopedSharedLock< LockPrimitive > SharedSerializableMapLock;
		
		mutable LockPrimitive m_storageMutex;
		SerializableRegistry m_registry;
		bool m_isEnabled;
	};

	//-------------------------------------------------
}

extern RED_REFLECTION_API obj::SerializableMap* GSerializableMap;		// Globally accessed register, it's not a perfect solution, but right now it will work.

#include "serializableMap.hpp"

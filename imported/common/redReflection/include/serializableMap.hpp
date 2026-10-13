/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once 

namespace obj
{

namespace impl
{

// to avoid redundant cast for ISerializables
template < typename T >
struct FastCast
{
	// TODO! is there a way to make it faster? with no weak->strong->weak conversion?
	RED_INLINE static WeakHandle< T > Execute( SerializableWeakHandle obj ) { return Cast< T >( obj.ToHandle() ); }
};

template <>
struct FastCast< ISerializable >
{
	RED_INLINE static SerializableWeakHandle Execute( SerializableWeakHandle obj ) { return obj; }
};

}

//////////////////////////////////////////////////////////////////////////

template < typename T >
RED_INLINE SerializableMapIterator< T > SerializableMap::Begin()
{
	SerializableMapIterator< T > it( m_registry.Begin(), m_registry.End() );
 	if ( it.IsValid() )
	{
		// go to the next non-null object of type T
		WeakHandle< T > firstObj = *it;
		if ( firstObj.Expired() )
		{    
			++it;
		}
	}
	return it;
}

template < typename T >
RED_INLINE SerializableMapIterator< T > SerializableMap::End()
{
	// End() iterator points to { -1, -1 }
	return SerializableMapIterator< T >( m_registry.End(), m_registry.End() );
}

//////////////////////////////////////////////////////////////////////////

template < typename T >
RED_INLINE SerializableMapIterator< T >::SerializableMapIterator( SerializableRegistry::iterator iter, SerializableRegistry::iterator end )
	: m_iterator( iter )
	, m_end( end )
	, m_classFilter( ClassID< T >() )
{
	// reset filter for ISerializable
	if ( m_classFilter == ClassID< ISerializable >() )
	{
		m_classFilter = nullptr;
	}
}

template < typename T >
RED_INLINE Bool SerializableMapIterator< T >::operator!=( const SerializableMapIterator& other )
{
	return m_iterator != other.m_iterator;
}

template < typename T >
RED_INLINE void SerializableMapIterator< T >::operator++()
{
	while( ++m_iterator != m_end )
	{
		SerializableWeakHandle wh = m_iterator.Value();
	
		if ( m_classFilter == nullptr )
		{
			// if no class filter was specified we only require handle to be valid
			if ( !wh.Expired() )
			{
				break;
			}
		}
		else 
		{
			// otherwise, handle type needs to match
			SerializableHandle h = wh.ToHandle();
			if ( h && h->IsA( m_classFilter ) )
			{
				break;
			}
		}
	}
}

template < typename T >
RED_INLINE WeakHandle< T > SerializableMapIterator< T >::operator*() const
{
	RED_ASSERT( IsValid() );
	return impl::FastCast< T >::Execute( m_iterator.Value() );
}

template < typename T >
RED_INLINE Bool SerializableMapIterator< T >::IsValid() const
{
	return m_iterator != m_end;
}

//////////////////////////////////////////////////////////////////////////

RED_INLINE SerializableMapIterator< ISerializable > begin( SerializableMap* map )
{
	return map->Begin< ISerializable >();
}

RED_INLINE SerializableMapIterator< ISerializable > end( SerializableMap* map )
{
	return map->End< ISerializable >();
}

template < typename T >
RED_INLINE SerializableMapIterator< T > begin( SerializableMap::TypedIteration< T >& ti )
{
	return ti.m_map->template Begin< T >();
}

template < typename T >
RED_INLINE SerializableMapIterator< T > end( SerializableMap::TypedIteration< T >& ti )
{
	return ti.m_map->template End< T >();
}

} // obj
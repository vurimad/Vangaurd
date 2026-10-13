/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "serializableMap.h"

namespace obj
{
	SerializableMap::SerializableMap()
		: m_registry{ red::PoolDebug() }
		, m_isEnabled( true )
	{
		m_registry.Reserve( 16 * 1024 );
	}

	void SerializableMap::Register( SerializableID id, SerializableWeakHandle serializable )
	{
		if ( m_isEnabled && !serializable.Expired() )
		{
			SerializableMapLock lock( m_storageMutex );
			m_registry.Insert( id.Get(), serializable );	
		}
	}

	void SerializableMap::Unregister( SerializableID id )
	{
		if( m_isEnabled )
		{
			SerializableMapLock lock( m_storageMutex );
			m_registry.Remove( id.Get() );
		}

	}

	Bool SerializableMap::FindSerializable( const SerializableID id, SerializableHandle& outSerializable ) const
	{
		RED_FATAL_ASSERT( m_isEnabled, "Serializable Dictionary is not enabled." );

		SerializableWeakHandle handle;
		Bool ret = false;
		{
			SharedSerializableMapLock lock( m_storageMutex );
			ret = m_registry.Find( id.Get(), handle );
		}
		if ( ret )
		{
			outSerializable = handle.ToHandle();
            ret = outSerializable != nullptr;
		}
		return ret;
	}

	void SerializableMap::GetAll( red::DynArray< SerializableWeakHandle >& objects ) const
	{
		RED_FATAL_ASSERT( m_isEnabled, "Serializable Dictionary is not enabled." );

		SharedSerializableMapLock lock( m_storageMutex );
		m_registry.GetValues( objects );
	}

	void SerializableMap::Disable()
	{
		m_isEnabled = false;
	}
}

obj::SerializableMap* GSerializableMap = RED_NEW( obj::SerializableMap );

/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_SCOPED_LOCK_HPP_
#define _RED_MEMORY_SCOPED_LOCK_HPP_

namespace red
{
namespace memory
{
	template< typename T >
	ScopedLock< T >::ScopedLock( T& syncObject )
		: m_object( &syncObject )
	{
		m_object->Acquire();
	}
	
	template< typename T >
	ScopedLock< T >::~ScopedLock()
	{
		m_object->Release();
	}

	template< typename T >
	ScopedSharedLock< T >::ScopedSharedLock( T& syncObject )
		: m_object( &syncObject )
	{
		m_object->AcquireShared();
	}
	
	template< typename T >
	ScopedSharedLock< T >::~ScopedSharedLock()
	{
		m_object->ReleaseShared();
	}
	
}
}

#endif

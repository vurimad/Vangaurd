/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SHARED_FROM_THIS_H_
#define _RED_MEMORY_SHARED_FROM_THIS_H_

#include "atomicSharedFromThis.h"
#include "sharedPtrUtils.h"

namespace red
{
	template< typename T, typename PoolType = void >
	using EnableSharedFromThis = EnableAtomicSharedFromThis< T, PoolType >;

	template< typename T >
	RED_INLINE AtomicSharedPtr< T, typename T::_AtomicSharedFromThisPoolType > SharedFromPtr( const T* ptr )
	{
		return ptr ? red::StaticCast< T >( ptr->SharedFromThis() ) : AtomicSharedPtr< T, typename T::_AtomicSharedFromThisPoolType >();
	}
}

#endif

/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_ATOMIC_SHARED_PTR_H_
#define _RED_MEMORY_ATOMIC_SHARED_PTR_H_

#include "sharedStorage.h"
#include "atomicSharedStorage.h"

namespace red
{
	template< typename T, typename PoolType = void >
	using AtomicSharedPtr = SharedStorage< T, internal::AtomicSharedStorage, PoolType >;

	template< typename T, typename... Args >
	AtomicSharedPtr< T > CreateAtomicSharedPtr(Args && ... args);

	template< typename T, typename PoolType, typename... Args >
	AtomicSharedPtr< T, PoolType > CreateAtomicSharedPtr( Args && ... args );
}

#include "atomicSharedPtr.hpp"


#endif

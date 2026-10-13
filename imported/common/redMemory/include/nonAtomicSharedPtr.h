/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_NON_ATOMIC_SHARED_PTR_H_
#define _RED_MEMORY_NON_ATOMIC_SHARED_PTR_H_

#include "sharedStorage.h"
#include "nonAtomicSharedStorage.h"

namespace red
{
	template< typename T, typename PoolType = void >
	using NonAtomicSharedPtr = SharedStorage< T, internal::NonAtomicSharedStorage, PoolType >;

	template< typename T, typename... Args >
	NonAtomicSharedPtr< T > CreateNonAtomicSharedPtr( Args && ... args );

	template< typename T, typename PoolType, typename... Args >
	NonAtomicSharedPtr< T, PoolType > CreateNonAtomicSharedPtr( Args && ... args );
}

#include "nonAtomicSharedPtr.hpp"

#endif

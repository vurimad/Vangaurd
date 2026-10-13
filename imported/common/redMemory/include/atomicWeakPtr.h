/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_ATOMIC_WEAK_PTR_H_
#define _RED_MEMORY_ATOMIC_WEAK_PTR_H_

#include "weakStorage.h"
#include "atomicSharedStorage.h"

namespace red
{
	template< typename T, typename PoolType = void >
	using AtomicWeakPtr = WeakStorage< T, internal::AtomicSharedStorage, PoolType >;
}

#endif
/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_NON_ATOMIC_WEAK_PTR_H_
#define _RED_MEMORY_NON_ATOMIC_WEAK_PTR_H_

#include "weakStorage.h"
#include "nonAtomicSharedStorage.h"

namespace red
{
	template< typename T, typename PoolType = void >
	using NonAtomicWeakPtr = WeakStorage< T, internal::NonAtomicSharedStorage, PoolType >;
}

#endif 

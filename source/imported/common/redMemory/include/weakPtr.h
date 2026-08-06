/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_WEAK_PTR_H_
#define _RED_MEMORY_WEAK_PTR_H_

#include "atomicWeakPtr.h"

namespace red
{
	template< typename T, typename PoolType = void >
	using WeakPtr = AtomicWeakPtr< T, PoolType >;
}

#endif 

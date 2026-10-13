/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SHARED_PTR_H_
#define _RED_MEMORY_SHARED_PTR_H_

#include "atomicSharedPtr.h"

namespace red
{
	template< typename T, typename PoolType = void >
	using SharedPtr = AtomicSharedPtr< T, PoolType >;

	template< typename T, typename... Args >
	SharedPtr< T > CreateSharedPtr( Args && ... args );

	template< typename T, typename PoolType, typename... Args >
	SharedPtr< T, PoolType > CreateSharedPtr( Args && ... args );
}

#include "sharedPtr.hpp"

#endif

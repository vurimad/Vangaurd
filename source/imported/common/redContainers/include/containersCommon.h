/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redContainersApi.h"

#if defined( RED_CONFIGURATION_DEBUG )

// Enables checked iterators to be used in the containers in DEBUG
#define RED_CHECKED_ITERATORS

#endif

#ifdef RED_CHECKED_ITERATORS
#define RED_CHECKED_ITERATOR_THIS this, 
#else
#define RED_CHECKED_ITERATOR_THIS
#endif

namespace red 
{
	class PoolString;

	template<>
	struct RED_CONTAINERS_API memory::StaticPoolStorage< PoolString >
	{
		static memory::PoolStorage storage RED_STATIC_PRIORITY( 107 );
	};

	RED_MEMORY_POOL_EXPLICIT( PoolString, red::memory::DefaultAllocator, RED_CONTAINERS_API );

	RED_CONTAINERS_API void InitializeContainerMemoryPools();

	const Int32 INVALID_INDEX = -1;

	struct ArrayIteratorTag
	{
	};

	struct MapIteratorTag
	{
	};

} // red

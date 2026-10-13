/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace res
{
namespace json
{
	RED_MEMORY_POOL( PoolJSON, red::memory::DefaultAllocator, RED_RESOURCE_API );

	class RED_RESOURCE_API JSONAllocator
	{
	public:
	
		RED_FORCE_INLINE void* Malloc( size_t size )
		{
			return RED_ALLOCATE( PoolJSON, size );
		}

		RED_FORCE_INLINE void* Realloc( void* originalPtr, size_t originalSize, size_t newSize )
		{
			return RED_REALLOCATE( PoolJSON, originalPtr, newSize );
		}

		RED_FORCE_INLINE static void Free( void *ptr )
		{
			RED_FREE( PoolJSON, ptr );  
		}
	};

	void InitializeJsonMemoryPool();
}
}

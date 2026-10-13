/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"
#include "imageMemory.h"

// internal image pool, visible only here so not a DLL pool
RED_MEMORY_POOL_STATIC( PoolImage, red::memory::DefaultAllocator );

namespace img
{
	namespace memory
	{
		void InitializeMemoryPools()
		{
			RED_INITIALIZE_MEMORY_POOL( PoolImage, red::memory::PoolCPU, red::memory::AcquireDefaultAllocator(), RED_KILO_BYTE( 1 ) );
		}

		extern void* Allocate( const Uint64 size, const Uint32 alignment /*= 8*/ )
		{
			return RED_ALLOCATE_ALIGNED( PoolImage, size, alignment );
		}

		extern void Free( void* ptr )
		{
			RED_FREE( PoolImage, ptr );
		}

	} // memory 

} // img

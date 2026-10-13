/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "redConfigPool.h"

namespace InGameConfig
{
	void InitializeMemoryPools()
	{
		RED_INITIALIZE_MEMORY_POOL( PoolInGameConfig, red::PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 16 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolInGameConfigResource, red::PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 4 ) );
	}
}
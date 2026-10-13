/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "reflectionPool.h"
#include "../../redCore/include/corePool.h"

namespace red
{
	void InitializeReflectionMemoryPools()
	{
		RED_INITIALIZE_MEMORY_POOL( PoolRTTI, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 16 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolRTTIFunction, PoolRTTI, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 2 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolRTTIProperty, PoolRTTI, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 6 ) );

		RED_INITIALIZE_MEMORY_POOL( PoolTriggers, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 12 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolSpline, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 4 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolCurves, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 4 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolAreas, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 4 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolEffect, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 1 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolIDRegistry, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 16 ) );
        RED_INITIALIZE_MEMORY_POOL( PoolEvents, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 16 ) );
        RED_INITIALIZE_MEMORY_POOL( PoolEvent, PoolEvents, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 1 ) );
        RED_INITIALIZE_MEMORY_POOL( PoolEventBroker, PoolEvents, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 15 ) );

		RED_INITIALIZE_MEMORY_POOL( PoolSerializable, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 16 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolResource, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 64 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolResourceLoadingJobs, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 1 ) );

		RED_INITIALIZE_MEMORY_POOL( PoolInterop, PoolBackend, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 64 ) );
	
	}
}

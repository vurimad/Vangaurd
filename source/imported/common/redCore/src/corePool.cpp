/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "corePool.h"

namespace red
{
	void InitializeCoreMemoryPools()
	{
		RED_INITIALIZE_MEMORY_POOL( PoolCore, PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 128 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolScript, PoolCore, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 16 ) );
	
		RED_INITIALIZE_MEMORY_POOL( PoolStreaming, PoolCore, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 80 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolStreamingNodeProxy, PoolStreaming, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 20 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolStreamingResource, PoolStreaming, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 60 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolStreamingData, PoolCore, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 64 ) );

		RED_INITIALIZE_MEMORY_POOL( PoolScriptCompiler, PoolBackend, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 256 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolScriptDebugger, PoolBackend, red::memory::AcquireDefaultAllocator(), RED_KILO_BYTE( 1) );
	}
}

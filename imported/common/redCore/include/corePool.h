/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
	RED_MEMORY_POOL( PoolCore, red::memory::DefaultAllocator, REDCORE_API );
	RED_MEMORY_POOL( PoolScript, red::memory::DefaultAllocator, REDCORE_API );
	RED_MEMORY_POOL( PoolScriptDebugger, red::memory::DefaultAllocator, REDCORE_API );
	RED_MEMORY_POOL( PoolScriptCompiler, red::memory::DefaultAllocator, REDCORE_API );
	
	RED_MEMORY_POOL( PoolStreaming, red::memory::DefaultAllocator, REDCORE_API );
	RED_MEMORY_POOL( PoolStreamingResource, red::memory::DefaultAllocator, REDCORE_API );
	RED_MEMORY_POOL( PoolStreamingNodeProxy, red::memory::DefaultAllocator, REDCORE_API );
	RED_MEMORY_POOL( PoolStreamingData, red::memory::DefaultAllocator, REDCORE_API );

	REDCORE_API void InitializeCoreMemoryPools();
}

/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
	
	RED_MEMORY_POOL( PoolRTTI, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolRTTIFunction, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolRTTIProperty, red::memory::DefaultAllocator, RED_REFLECTION_API );
	
	// ctremblay: Some of those should be move to their own project. (Spline, triggers, area etc.. )
	// Others are here because where they should reside are not yet existing, or are marked as deprecated.
	RED_MEMORY_POOL( PoolSerializable, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolTriggers, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolSpline, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolCurves, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolAreas, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolEffect, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolIDRegistry, red::memory::DefaultAllocator, RED_REFLECTION_API );
    RED_MEMORY_POOL( PoolEvents, red::memory::DefaultAllocator, RED_REFLECTION_API );
    RED_MEMORY_POOL( PoolEvent, red::memory::DefaultAllocator, RED_REFLECTION_API );
    RED_MEMORY_POOL( PoolEventBroker, red::memory::DefaultAllocator, RED_REFLECTION_API );


	RED_MEMORY_POOL( PoolBackendTerrain, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolBackendTerrainCells, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolBackendTerrainPatches, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolBackendTerrainDataSource, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolBackendTerrainNodes, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolBackendTerrainRoads, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolBackendTerrainUndo, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolBackendTerrainOps, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolBackendTerrainTemp, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolInterop, red::memory::DefaultAllocator, RED_REFLECTION_API );


	RED_MEMORY_POOL( PoolResource, red::memory::DefaultAllocator, RED_REFLECTION_API );
	RED_MEMORY_POOL( PoolResourceLoadingJobs, red::memory::DefaultAllocator, RED_REFLECTION_API ); // ctremblay: This should use a LocklessFixedSizeAllocator

	void InitializeReflectionMemoryPools();
}

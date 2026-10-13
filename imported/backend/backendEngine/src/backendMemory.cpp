/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendMemory.h"

namespace tools
{
	namespace
	{
		const Uint32 BackendTerrainFull = RED_MEGA_BYTE( 2560 );
		const Uint32 BackendTerrainCellsFull = RED_MEGA_BYTE( 1536 );
		const Uint32 BackendTerrainPatchesFull = RED_MEGA_BYTE( 8 );
		const Uint32 BackendTerrainDataSource = RED_MEGA_BYTE( 1024 );
		const Uint32 BackendTerrainNodesFull = RED_MEGA_BYTE( 1800 );
		const Uint32 BackendTerrainRoadsFull = RED_MEGA_BYTE( 128 );
		const Uint32 BackendTerrainUndoFull = RED_MEGA_BYTE( 256 );
		const Uint32 BackendTerrainOpsFull = RED_MEGA_BYTE( 256 );
		const Uint32 BackendTerrainTempFull = RED_MEGA_BYTE( 2048 );
	}

	void InitializeMemoryPools()
	{
		RED_INITIALIZE_MEMORY_POOL( red::PoolBackendTerrain, red::PoolBackend, red::memory::AcquireDefaultAllocator(), BackendTerrainFull );
		RED_INITIALIZE_MEMORY_POOL( red::PoolBackendTerrainCells, red::PoolBackend, red::memory::AcquireDefaultAllocator(), BackendTerrainCellsFull );
		RED_INITIALIZE_MEMORY_POOL( red::PoolBackendTerrainPatches, red::PoolBackend, red::memory::AcquireDefaultAllocator(), BackendTerrainPatchesFull );
		RED_INITIALIZE_MEMORY_POOL( red::PoolBackendTerrainDataSource, red::PoolBackend, red::memory::AcquireDefaultAllocator(), BackendTerrainDataSource );
		RED_INITIALIZE_MEMORY_POOL( red::PoolBackendTerrainNodes, red::PoolBackend, red::memory::AcquireDefaultAllocator(), BackendTerrainNodesFull );
		RED_INITIALIZE_MEMORY_POOL( red::PoolBackendTerrainRoads, red::PoolBackend, red::memory::AcquireDefaultAllocator(), BackendTerrainRoadsFull );
		RED_INITIALIZE_MEMORY_POOL( red::PoolBackendTerrainUndo, red::PoolBackend, red::memory::AcquireDefaultAllocator(), BackendTerrainUndoFull );
		RED_INITIALIZE_MEMORY_POOL( red::PoolBackendTerrainOps, red::PoolBackend, red::memory::AcquireDefaultAllocator(), BackendTerrainOpsFull );
		RED_INITIALIZE_MEMORY_POOL( red::PoolBackendTerrainTemp, red::PoolBackend, red::memory::AcquireDefaultAllocator(), BackendTerrainTempFull );
	}
}
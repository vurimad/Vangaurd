/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "jobScopeMemoryAllocator.h"
#include "../../redMemory/include/pool.h"
#include "../../redMemory/src/locklessSlabAllocator.h"

namespace job
{

REDJOBS2_API void InitializeJobMemoryPools();
REDJOBS2_API void ShutdownJobMemoryPools();

RED_MEMORY_POOL( PoolJobs2Debug, red::memory::DefaultAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2, red::memory::DefaultAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2Dispatcher, red::memory::DefaultAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2QueueLowPriority, red::memory::DefaultAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2QueueNormalPriority, red::memory::DefaultAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2QueueHighPriority, red::memory::DefaultAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2QueueBackgroundPriority, red::memory::DefaultAllocator, REDJOBS2_API);
RED_MEMORY_POOL( PoolJobs2QueueLocal, red::memory::DefaultAllocator, REDJOBS2_API);

RED_MEMORY_POOL( PoolJobs2Counters, red::memory::LocklessSlabAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2WaitingListEntries, red::memory::LocklessSlabAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2ParallelForSharedCounterEntries, red::memory::LocklessSlabAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2ParallelForJobEntries, red::memory::LocklessSlabAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobs2Data, red::memory::LocklessSlabAllocator, REDJOBS2_API );
RED_MEMORY_POOL( PoolJobScope, prv::JobScopeMemoryAllocator, REDJOBS2_API );

}

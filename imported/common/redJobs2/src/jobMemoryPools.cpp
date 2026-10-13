/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../redMemory/include/poolRoot.h"
#include "jobMemoryPools.h"

namespace job
{
namespace
{
	red::memory::ThreadMonitor s_threadMonitor;
	prv::JobScopeMemoryAllocator s_jobScopeMemoryAllocator;
	bool s_initialized = false;
}
	REDJOBS2_API void InitializeJobMemoryPools()
	{
		if ( s_initialized )
		{
			// this check is required due to UTs
			return;
		}

		s_initialized = true;

		job::prv::JobScopeMemoryAllocatorParameter parameter
		{
			&red::memory::AcquireSystemAllocator(),
			&s_threadMonitor
		};

		s_jobScopeMemoryAllocator.Initialize( parameter );

		RED_INITIALIZE_MEMORY_POOL( PoolJobs2,											red::PoolEngine,		red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 4 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2Debug,										red::PoolDebug,			red::memory::AcquireDefaultAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2Dispatcher,								PoolJobs2,				red::memory::AcquireDefaultAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2QueueLowPriority,							PoolJobs2,				red::memory::AcquireDefaultAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2QueueNormalPriority,						PoolJobs2,				red::memory::AcquireDefaultAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2QueueHighPriority,							PoolJobs2,				red::memory::AcquireDefaultAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2QueueBackgroundPriority,					PoolJobs2,				red::memory::AcquireDefaultAllocator(), 0 );

		RED_INITIALIZE_MEMORY_POOL( PoolJobs2Counters,									PoolJobs2,				red::memory::AcquireLocklessSlabAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2WaitingListEntries,						PoolJobs2,				red::memory::AcquireLocklessSlabAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2ParallelForSharedCounterEntries,			PoolJobs2,				red::memory::AcquireLocklessSlabAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2ParallelForJobEntries,						PoolJobs2,				red::memory::AcquireLocklessSlabAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobs2Data,										PoolJobs2,				red::memory::AcquireLocklessSlabAllocator(), 0 );
		RED_INITIALIZE_MEMORY_POOL( PoolJobScope,										PoolJobs2,				s_jobScopeMemoryAllocator, 0, &prv::LogJobScopeMemoryAllocatorMetrics );
	}

	REDJOBS2_API void ShutdownJobMemoryPools()
	{
		if ( s_initialized )
		{
			// Tear down the per-thread job-scope allocators before the root
			// SystemAllocator is destroyed. The original engine guarantees this
			// through its global module shutdown ordering.
			s_jobScopeMemoryAllocator.Uninitialize();
		}
	}
}

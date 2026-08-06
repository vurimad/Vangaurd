/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "jobStackTrace.h"
#include "jobDispatcherInitParam.h"

#ifdef RED_PLATFORM_CONSOLE
	#if defined(RED_PLATFORM_ORBIS)
		#ifdef RED_CONSOLE_CORE_7_SUPPORT
			#define RED_MAX_JOB_THREADS 6
		#else
			#define RED_MAX_JOB_THREADS 5
		#endif
	#elif defined(RED_PLATFORM_DURANGO)
		#ifdef RED_CONSOLE_CORE_7_SUPPORT
			// We need this value to be different based on whether it is running on Xbox Series X or not
			// This value is used to create static arrays at compile time, so we have to set it to the maximum
			// value it can be on Xbox Series X, which is 7, instead of the default 6 on other Xbox platforms.
			// This means some arrays are over-sized in code, but this only adds up to around a few hundred KB's
			// and seems like a good trade off for much better CPU performance on Xbox Series X.
			#define RED_MAX_JOB_THREADS 7
		#else
			#define RED_MAX_JOB_THREADS 5
		#endif
	#else
		#error Unknown Console
	#endif
#else // PC
	#ifdef RED_MEMORY_ENABLE_EXTENDED_THREAD_REGISTRATION
		// 27 workers + main thread. Still have to cap because of slab allocator, but we have fewer threads registering now
		#define RED_MAX_JOB_THREADS 27
	#else
		// 11 workers + main thread. Still have to cap because of slab allocator, but we have fewer threads registering now
		#define RED_MAX_JOB_THREADS 11
	#endif
#endif // RED_PLATFORM_CONSOLE

namespace job
{
	struct RunContext;

	// Init/shutdown the job system
	REDJOBS2_API void Initialize( const InitParam& setup );
	REDJOBS2_API void Shutdown();

	// Get the number of threads that can run jobs; excludes the main thread or any other threads not explicitly owned by the job system.
	// Useful for integration with 3rd party job systems; generally shouldn't be used otherwise as parallel-for tries to create the optimal number of jobs internally.
	REDJOBS2_API Uint32 GetNumDispatcherThreads(Bool includeCore7 = false);

	// Get calling's thread dispatcher thread index.
	// Will return 0 for main thread, UINT32_MAX for thread outside the job system or >= 1 for a dispatcher thread.
	REDJOBS2_API Uint32 GetDispatcherThreadIndex();

	// Concurrent diagnostic snapshot. Values can change immediately after return.
	REDJOBS2_API Uint32 GetApproximateQueueDepth( Priority priority );

	REDJOBS2_API void DumpToLog();

	REDJOBS2_API StackTraceHandle DebugTraceCall();

	REDJOBS2_API void FatalAssertIfDispatcherThread();
}

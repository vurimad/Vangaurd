/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobCounterFunctions.h"
#include "jobDecl.h"
#include "jobDispatcher.h"
#include "jobCounterOwner.h"

namespace job
{
	namespace prv
	{
		extern Dispatcher* gDispatcher;
	}

	void RunJob( const JobDecl& job, const Counter& waitForZeroCounter, Counter& accumulateCounter )
	{
		RED_FATAL_ASSERT( prv::gDispatcher );
		prv::gDispatcher->RunJob( job, waitForZeroCounter.Internal_GetCounter(), accumulateCounter.Internal_GetCounter() );
	}

	void RunParallelForJob( const JobDeclParallelFor& job, const Counter& waitForZeroCounter, Counter& accumulateCounter)
	{
		RED_FATAL_ASSERT( prv::gDispatcher );
		prv::gDispatcher->RunParallelForJob( job, waitForZeroCounter.Internal_GetCounter(), accumulateCounter.Internal_GetCounter() );
	}

	CompletionDeferral CreateDeferral( const char* debugName, const void* debugUserData, prv::CounterEntry& counter )
	{
		RED_FATAL_ASSERT( prv::gDispatcher );
		return prv::gDispatcher->CreateDeferral( debugName, debugUserData, counter );
	}

	Bool FlushCounter( const Counter& counter, Bool processLatent, Int32 timeoutMillseconds )
	{
		RED_FATAL_ASSERT( prv::gDispatcher );
		return prv::gDispatcher->FlushCounter( counter.Internal_GetCounter(), processLatent, timeoutMillseconds );
	}

	Bool FlushCounter(Counter&& counter, Bool processLatent, Int32 timeoutMillseconds )
	{
		RED_FATAL_ASSERT(prv::gDispatcher);
		return prv::gDispatcher->FlushCounter(counter.Internal_GetCounter(), processLatent, timeoutMillseconds );
	}

	Bool FlushCounterOnProcessFrame( const Counter& counter  )
	{
		RED_FATAL_ASSERT( prv::gDispatcher );

		const Uint32 c_minDispatcherThreadToSkipLargeOnFlush = 3;
		const Bool processLargeJobs = ( prv::gDispatcher->GetNumDispatcherThreads( false ) < c_minDispatcherThreadToSkipLargeOnFlush );

		return prv::gDispatcher->FlushCounter( counter.Internal_GetCounter(), job::Priority::RenderPath, -1, processLargeJobs );
	}

	void AnalyzeCounter( const Counter& counter )
	{
		RED_FATAL_ASSERT( prv::gDispatcher );
		RED_FATAL_ASSERT( counter.Internal_GetCounter() );
		prv::gDispatcher->AnalyzeCounter( *counter.Internal_GetCounter() );
	}

}

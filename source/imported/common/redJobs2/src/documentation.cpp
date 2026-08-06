/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"


#if 0

#include "jobSystem.h"
#include "jobCounter.h"
#include "jobDecl.h"
#include "jobPriority.h"
#include "jobDeferral.h"
#include "jobSyncObjectView.h"
#include "jobRunner.h"
#include "jobCounterChain.h"

//////////////////////////////////////////////////////////////////////////
// Table of Contents:
// NOTE: if you want to use wrappers around these basic job functions, see jobRunner.h.
static void SimplestJob();
static void SimplestJobFlushed();
static void ParallelJobs();
static void SerialJobs();
static void ParallelForJobs();
static void JobContinuations();
static void JobSyncObjectView();
static void JobSyncObjectViewWithParallelSection();
//////////////////////////////////////////////////////////////////////////
// TODO: more advanced usage like : CompletionDeferral, ParallelFor epilogue/virtual arrays, priorities

// For profiling
static red::InstrumentationObject s_testInstrumentationObject( "MyObjectName" );

//////////////////////////////////////////////////////////////////////////

// Just a simple example job
struct JobHelper
{
	RED_USE_MEMORY_POOL( red::PoolFrame );

	static void JobFunc( void* jobData, const job::RunContext& runContext )
	{
		auto* self = static_cast< JobHelper* >( jobData );
		self->DoSomeWork();
		RED_DELETE( self );
	}

	void DoSomeWork()
	{
		red::SleepOnCurrentThread(1);
	}
};

// Just a simple example parallel-for job
template< typename TElement >
struct JobParallelForHelper
{
	RED_USE_MEMORY_POOL( red::PoolFrame );

	// Called in parallel for each element in the array
	static void ParallelForJobFunc( void* sharedData, void* elements, Uint32 elementIndex, const job::RunContext& runContext )
	{
		auto* self = static_cast<JobParallelForHelper<TElement>*>( sharedData );
		TElement& element = static_cast< TElement* >( elements )[ elementIndex ];
		self->DoSomeWork( element );
	}

	// Called after finished, so safe to delete sharedData
	static void EpilogueFunc( void* sharedData, void* elements, Uint32 numElements, const job::RunContext& runContext )
	{
		auto* self = static_cast<JobParallelForHelper<TElement>*>( sharedData );
		RED_DELETE( self );
	}

	void DoSomeWork( TElement& element )
	{
		red::SleepOnCurrentThread( 1 );
	}
};

//////////////////////////////////////////////////////////////////////////

// Create a test job for the documentation samples
static void InitJob( job::JobDecl& outJobDecl )
{
	JobHelper* helper = RED_NEW( JobHelper );
	outJobDecl.instrumentationObject = &s_testInstrumentationObject;
	outJobDecl.jobData = helper;
	outJobDecl.jobFunc = &JobHelper::JobFunc;
}

//////////////////////////////////////////////////////////////////////////

// Totally fire and forget job; you could crash in a real use case!
static void SimplestJob()
{
	job::JobDecl jobDecl;
	InitJob( jobDecl );
	job::RunJob( jobDecl, job::NoWaitNoAccumView() );
}

// Use counters to accumulate jobs
static void SimplestJobFlushed()
{
	job::JobDecl jobDecl;
	InitJob( jobDecl );

	job::CounterChain counter;
	job::RunJob( jobDecl, counter );

	// Necessary in some key places to make sure all jobs finished
	// But generally you should avoid calling this yourself
	job::FlushCounter( counter );
}

// You can reuse a counter and accumulate into it for each call to RunJob()
static void ParallelJobs()
{
	const Uint32 numJobs = 3;
	job::JobDecl jobDecl;
	InitJob( jobDecl );

	// All these jobs run in parallel
	job::CounterChain counter;
	for ( Uint32 i = 0; i < numJobs; ++i )
	{
		job::RunJob( jobDecl, counter );
	}

	// Necessary in some key places to make sure all jobs finished
	// But generally you should avoid calling this yourself
	job::FlushCounter( counter );
}

// Jobs run before counter.NextCounter() will finish before jobs run afterwards
static void SerialJobs()
{
	const Uint32 numJobs = 3;
	job::JobDecl jobDecl;
	InitJob( jobDecl );

	job::CounterChain counter;
	
	for ( Uint32 i  = 0; i < numJobs; ++i )
	{
		for ( Uint32 j = 0; j < numJobs; ++j )
		{
			// These 'j' batch of jobs jobs run in parallel
			// since there's no call to NextCounter() between them
			job::RunJob( jobDecl, counter );
		}

		// Makes the previous 'i' batch of jobs always finish before the next one
		counter.NextCounter();
	}

	// Necessary in some key places to make sure all jobs finished
	// But generally you should avoid calling this yourself
	job::FlushCounter( counter );
}

//////////////////////////////////////////////////////////////////////////

// Create a test parallel-for job for the documentation samples
template< typename TElement >
static void InitParallelForJob( job::JobDeclParallelFor& outJobDecl, TElement* elements, Uint32 numElements )
{
	outJobDecl.instrumentationObject = &s_testInstrumentationObject;
	outJobDecl.sharedData = RED_NEW( JobParallelForHelper< TElement > );
	outJobDecl.jobFunc = &JobParallelForHelper<TElement>::ParallelForJobFunc;
	outJobDecl.epilogueFunc = &JobParallelForHelper<TElement>::EpilogueFunc;
	outJobDecl.elements = elements;
	outJobDecl.numElements = numElements;
}

static void ParallelForJobs()
{
	Int32 elements[] = { 1, 2, 3 };
	job::JobDeclParallelFor jobDecl;
	InitParallelForJob< Int32 >( jobDecl, elements, RED_ARRAY_COUNT_U32( elements ) );

	job::CounterChain counter;
	job::RunParallelForJob( jobDecl, counter );

	// Necessary in some key places to make sure all jobs finished
	// But generally you should avoid calling this yourself
	job::FlushCounter( counter );
}

//////////////////////////////////////////////////////////////////////////

// If you need to run jobs from another job, then you can make it so the original job doesn't count as finished
// until all its children jobs are finished. Create a new CounterChain using RunContext::continuationContext.
static void JobContinuations()
{
	job::JobDecl jobDecl;
	jobDecl.instrumentationObject = &s_testInstrumentationObject;
	jobDecl.jobData = nullptr;
	jobDecl.jobFunc = []( void*, const job::RunContext& runContext )
	{
		// Adds to the counter this job was run on; the first job won't be finished now until the job defined by 
		//jobDecl2 finishes
		job::CounterChain depCounter{ runContext.continuationContext };

		job::JobDecl jobDecl2;
		//... setup jobDecl2

		job::RunJob( jobDecl2, depCounter );
	};

	job::CounterChain counter;
	job::RunJob( jobDecl, counter );

	// Necessary in some key places to make sure all jobs finished
	// But generally you should avoid calling this yourself
	job::FlushCounter( counter );
}

//////////////////////////////////////////////////////////////////////////

// SyncObjectView is just a simple wrapper around CounterChains
// It helps making serialized sections a lot easier, especially when passing it around to other functions by reference
static void JobSyncObjectView()
{
	job::JobDecl jobDecl;
	InitJob( jobDecl );

	job::CounterChain counter;
	job::SyncObjectView sync{ counter };

	// Each job is run one after the other here.
	const Uint32 numJobs = 3;
	for ( Uint32 i = 0; i < numJobs; ++i )
	{
		job::RunJob( jobDecl, sync );
	}

	// Necessary in some key places to make sure all jobs finished
	// But generally you should avoid calling this yourself
	job::FlushCounter( sync );
}

// If you use a SyncObjectView, it's still possible to create sections where jobs are run in parallel
static void JobSyncObjectViewWithParallelSection()
{
	job::JobDecl jobDecl;
	InitJob( jobDecl );

	job::CounterChain counter;
	job::SyncObjectView sync{ counter };

	job::RunJob( jobDecl, sync );

	// The jobs in this section are all run in parallel
	job::WithCounter( sync, [&]( job::CounterChain& forkCounter ) {
		// Each job is run in parallel
		const Uint32 numJobs = 3;
		for ( Uint32 i = 0; i < numJobs; ++i )
		{
			job::RunJob( jobDecl, forkCounter );
		}	
	} );

	job::RunJob( jobDecl, sync );

	// Necessary in some key places to make sure all jobs finished
	// But generally you should avoid calling this yourself
	job::FlushCounter( sync );
}

#endif

RED_NO_EMPTY_FILE();

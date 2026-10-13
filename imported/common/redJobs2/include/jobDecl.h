/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/threads.h"

#include "jobPriority.h"

namespace red
{
	struct InstrumentationObject;
}

namespace job
{
	class Counter;
	struct RunContext;

	enum class JobHint : Uint8
	{
		None,
		Trivial,
		Large,
		PhysX, // REMOVE THIS
		AudioEvent,
	};

	// Advanced use only
	enum class JobDebugFlags : Uint8
	{
		// Bypass internal checks if it's safe to use the frame allocator. Only use if certain that the jobs are actually sync'd to the frame. 
		// E.g., used for PhysX currently since dependencies are managed in PhysX itself and synchronized at the end with tick group.
		AllowFrameAllocator = RED_FLAG( 0 ),
	};

	// A job: simply a function and its data.
	struct JobDecl
	{
		// The job function to run; must be a valid function pointer.
		typedef void( TJobFunc )( void* jobData, const RunContext& runContext );
		TJobFunc* jobFunc{nullptr};

		// Pointer passed into jobFunc as the jobData argument; memory not owned by the job system.
		// Can be nullptr if not necessary.
		void* jobData{nullptr};

		// Profiling object unique for this class of jobs (e.g., jobFunc/debugName the same)
		// Object not copied by the job system
		red::InstrumentationObject* instrumentationObject{ nullptr };

		// Optimization setting; e.g., whether the job execution is generally smaller than the scheduling cost.
		// Such as a job that just sets a bool.
		// Used internally to run this job in cases where its counter's priority would normally otherwise disallow it.
		JobHint hint{ JobHint::None };

		Uint8 debugFlags{ 0 };
	};

	struct JobDeclParallelFor
	{
		// The job function to run; must be a valid function pointer.
		typedef void( TJobFunc )( void* sharedData, void* elements, Uint32 elementStartIndex, Uint32 elementEndIndex, const RunContext& runContext );
		TJobFunc* jobFunc{ nullptr };

		// Optional - can be nullptr. Can be used to init and set sharedData depending on the actual number of parallelForJobs that will be spawned.
		// Callback occurs immediately when calling job::RunParallelForJob().
		// Strongly consider using an epilogueFunc to clean up afterwards.
		// parallelForTeamSize - the number of parallelfor jobs that will be spawned internally. Can be zero if numElements was zero.
		// prevSharedData - can be read in order to determine how to create shared data. E.g., could store user context in sharedData, which will be overwritten by the actual sharedData.
		// Note: You can get the index between 0 and numParallelForJobs-1 during RunContext.parallelForJobIndex
		// Note: The epilogue function will not have a valid parallelForJobIndex.
		typedef void*( TSharedDataInitCallback )( Uint32 parallelForTeamSize, void* prevSharedData );
		TSharedDataInitCallback* initSharedDataCallback{ nullptr };

		// The job function to be called after parallel for finishes; optional - can be nullptr.
		// Also guaranteed that all processing is finished, so can even delete sharedData as well.
		typedef void( TEpilogueFunc )( void* sharedData, void* elements, Uint32 numElements, const RunContext& runContext );
		TEpilogueFunc* epilogueFunc{ nullptr };

		// Pointer passed into jobFunc as the sharedData argument; memory not owned by the job system.
		// Can be nullptr if not necessary.
		void* sharedData{ nullptr };

		// Elements array; optional - can be nullptr. Nullptr can make sense in the case of "virtual arrays"
		// where the elements are accessed by elementIndex some special other way: e.g., a function taking an index.
		// Passed into the jobFunc callback
		void* elements{ nullptr };

		// The number of elements; a value between [0, and numElements) will be passed into the jobFunc callback.
		Uint32 numElements{ 0 };

		// The maximal number of elements in batch. If 0 uses auto calculated value.
		Uint32 maxBatchSize{ 0 };

		// Profiling object unique for this class of jobs (e.g., jobFunc/debugName the same)
		// Object not copied by the job system
		red::InstrumentationObject* instrumentationObject{ nullptr };

		Uint8 debugFlags{ 0 };
	};

	// Private data; do not cache - only valid while the job is running
	struct ContinuationContext
	{
		Counter* counter{nullptr};
		red::InstrumentationObject* instrumentationObject{ nullptr };
		ScheduleParam param;
	}; 

	// Currently running job's context
	struct RunContext
	{
		const char* debugName{""};

		red::ArraySpan< const char* const > debugStackTraces{};

		// Set to a index between [0, numParallelForJobs), where numParallelForJobs varies according to the dispatcher.
		// Can be used for things like parallel-for job local storage lookup.
		// Invalid if job wasn't created with JobDeclParallelFor.
		Int32 parallelForTeamIndex{ -1 };

		// dispatcherThreadIndex is valid in any job created by dispatcher system. Contrary to parallelForTeamIndex,
		// dispatcherThreadIndex can have values in range[0, numberOfDispatcherThreads+1).
		// 0 index is reserved for the main thread
		Uint32 dispatcherThreadIndex{ 0 };

		ContinuationContext continuationContext;
	};
}

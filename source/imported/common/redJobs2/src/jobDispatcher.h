/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redThreadsThread.h"
#include "../../redSystem/include/redThreadsAtomic.h"
#include "../../redMemory/include/uniquePtr.h"
#include "../../redMemory/include/function.h"
#include "../../redContainers/include/arraySpan.h"
#include "../../redContainers/include/dynArray.h"
#include "jobMemoryPools.h"
#include "jobDispatcherQueue.h"
#include "jobDispatcherEntries.h"
#include "jobDispatcherInitParam.h"
#include "jobDeferral.h"
#include "jobDispatcherCounterValue.h"

namespace job
{

struct JobDecl;
struct JobDeclParallelFor;
struct RunContext;
enum class Priority : Uint8;

namespace prv
{

class CounterEntry;
class DispatcherThread;
class Debugger;
class JobScopeMemoryAllocator;

class Dispatcher : red::NonCopyable
{
	RED_USE_MEMORY_POOL( PoolJobs2Dispatcher );

public:

	explicit Dispatcher( const InitParam& setup );
	~Dispatcher();

	void RunParallelForJob( const JobDeclParallelFor& job, const CounterEntry* waitForZeroCounter, CounterEntry* accumulateCounter );
	
	void RunJob( const JobDecl& job, const CounterEntry* waitForZeroCounterEntry, CounterEntry* accumulateCounterEntry );
	void InitWithEmptyJob( JobDecl& outJob );

	Uint32 GetNumDispatcherThreads(Bool includeCore7) const;
	Uint32 GetApproximateQueueDepth( Priority priority ) const
	{
		return m_dispatcherQueue.GetApproximateDepth( priority );
	}
	Bool FlushCounter( const CounterEntry* entry, Bool processLatent, Int32 timeoutMillseconds );
	Bool FlushCounter( const CounterEntry* entry, Priority processPriority, Int32 timeoutMillseconds, Bool processLarge );

	void AddRefJobCounterInternal( CounterEntry* entry );
	void ReleaseJobCounterInternal( CounterEntry* entry );

	// Functions used by DispatcherThread
	void PopJobQueueEntry( JobQueueEntry& outEntry, Priority& outPriority );
	void PopJobQueueEntry( JobQueueEntry& outEntry, Priority& outPriority, Affinity affinity );
	void DoRunJobQueueEntry( const JobQueueEntry& entry, const Uint32 dispatcherThreadIndex, Priority priority, prv::JobScopeMemoryAllocator& jobScopeAllocator, TLocalQueue* optLocalQueue );
	Bool IsExitRequested() const { return m_isExitRequested.GetValue(); }

	CounterEntry* InitJobCounter( const char* debugName, ScheduleParam param, const void* debugUserData );

	CompletionDeferral CreateDeferral( const char* debugName, const void* debugUserData, CounterEntry& counterEntry );

	Bool IsZero_Snapshot( const CounterEntry* counterEntry );

	void DecrementCounterEntryInternal( CounterEntry* counterEntry, TLocalQueue* optLocalQueue );
		
	Debugger* GetDebugger() const;
	
	void AnalyzeCounter(const CounterEntry& counter);

private:
	void Init();
	void InitJobQueue( const InitParam& setup );

	void Shutdown();

	Bool TryPopJobQueueEntry( JobQueueEntry& outEntry, Priority& outPriority );
	void QueueJobAndSignal( const JobDecl& job, CounterEntry* accumulateCounterEntry, TLocalQueue* optLocalQueue );
	Bool TryPutOnWaitingList( const JobDecl& job, const CounterEntry& waitForZeroCounterEntry, CounterEntry* accumulateCounterEntry, const StackTraceCacheEntryPath* debugTrace );
	void QueueJobOrWait( const JobDecl& job, const CounterEntry* waitForZeroCounterEntry, CounterEntry* accumulateCounterEntry );
	Priority MapPriority( Priority priority ) const;

	InitParam m_setup;

	Priority m_priorityMap[ static_cast< Uint32 >( Priority::COUNT ) ];

	DispatcherQueue< JobQueueEntry > m_dispatcherQueue;

	red::DynArray< red::UniquePtr< DispatcherThread > > m_dispatcherThreads{ PoolJobs2() };

	red::UniquePtr< Debugger > m_debugger;
	JobScopeMemoryAllocator* m_jobScopeAllocator;

	red::Atomic< Bool > m_isExitRequested;
	red::Atomic< Bool > m_shouldPumpMessagesForDXGI;
	Bool m_isFlushingCounter;
};

} } // prv/jobs

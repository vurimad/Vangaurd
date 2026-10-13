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
	void InitWithDependencyRelayJob( JobDecl& outJob );

	Uint32 GetNumDispatcherThreads(Bool includeCore7) const;
	Uint32 GetApproximateQueueDepth( Priority priority ) const
	{
		return m_dispatcherQueue.GetApproximateDepth( priority );
	}
	Uint32 GetPendingRecruitmentControls() const { return m_pendingRecruitmentControls.GetValue(); }
	Uint64 GetRecruitmentControlsAdmitted() const { return m_recruitmentControlsAdmitted.GetValue(); }
	Uint64 GetRecruitmentControlsPopped() const { return m_recruitmentControlsPopped.GetValue(); }
	Uint64 GetRecruitmentControlsDroppedBound() const { return m_recruitmentControlsDroppedBound.GetValue(); }
	Uint64 GetRecruitmentControlsDroppedQueueFull() const { return m_recruitmentControlsDroppedQueueFull.GetValue(); }
	Uint64 GetRecruitmentControlsAbandoned() const { return m_recruitmentControlsAbandoned.GetValue(); }
	Uint64 GetDependencyRelaysInlined() const { return m_dependencyRelaysInlined.GetValue(); }
	Uint64 GetDependencyRelaysQueuedFallback() const { return m_dependencyRelaysQueuedFallback.GetValue(); }
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
	struct ContinuationReleaseContext
	{
		Priority releasePriority{ Priority::COUNT };
		Uint32 inlineRelayDepth{ 0 };
		Uint32 inlineRelayTransitions{ 0 };
	};

	void Init();
	void InitJobQueue( const InitParam& setup );

	void Shutdown();

	Bool TryPopJobQueueEntry( JobQueueEntry& outEntry, Priority& outPriority );
	void QueueJobAndSignal( const JobDecl& job, CounterEntry* accumulateCounterEntry, TLocalQueue* optLocalQueue,
		ContinuationReleaseContext* optContinuationContext = nullptr, Bool singleWaiterRelease = false );
	void DecrementCounterEntryInternal( CounterEntry* counterEntry, TLocalQueue* optLocalQueue, ContinuationReleaseContext* optContinuationContext );
	Bool TryQueueRecruitmentControl( const JobQueueEntry& relayEntry );
	void OnQueueEntryPopped( const JobQueueEntry& entry );
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
	red::Atomic< Uint32 > m_pendingRecruitmentControls{ 0 };
	red::Atomic< Uint64 > m_recruitmentControlsAdmitted{ 0 };
	red::Atomic< Uint64 > m_recruitmentControlsPopped{ 0 };
	red::Atomic< Uint64 > m_recruitmentControlsDroppedBound{ 0 };
	red::Atomic< Uint64 > m_recruitmentControlsDroppedQueueFull{ 0 };
	red::Atomic< Uint64 > m_recruitmentControlsAbandoned{ 0 };
	red::Atomic< Uint64 > m_dependencyRelaysInlined{ 0 };
	red::Atomic< Uint64 > m_dependencyRelaysQueuedFallback{ 0 };
	Bool m_isFlushingCounter;
};

} } // prv/jobs

/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redThreadsAtomic.h"
#include "../../redSystem/include/utility.h"
#include "jobLockFreeQueueMPMC.h"

#include "jobPriority.h"
#include "jobDispatcherSemaphore.h"

namespace job { namespace prv {

struct DispatcherQueueSetup
{
	Uint32 numLowPriorityJobs = 0;
	Uint32 numNormalPriorityJobs = 0;
	Uint32 numHighPriorityJobs = 0;
#ifdef RED_CONSOLE_CORE_7_SUPPORT
	Uint32 numCore7Jobs = 0;
#endif
};

// The spinCount of last resort to try and avoid deadlock when the queue is full
// and all threads are trying to push into it.
const Uint32 c_dispatcherQueuePushSpinCount = 1;// 6553600 or some large number, currently matching the Xbox rwspinlock count

template< typename TEntry >
class DispatcherQueue : red::NonCopyable
{
public:
	static const Uint32 c_numQueues = static_cast< Uint32 >( job::Priority::COUNT );

	typedef LockFreeQueueMPMCExternalBuffer< TEntry > TJobQueue;

	DispatcherQueue();

	void Initialize( const DispatcherQueueSetup& setup );
    void Shutdown( Uint32 numResourceThrottlerThreads );

	RED_NODISCARD Bool TryPush( const TEntry& entry, Priority priority, Affinity affinity, Uint32 spinCount = c_dispatcherQueuePushSpinCount);
	void Pop( TEntry& outEntry, Priority& outPriority );
	void Pop( TEntry& outEntry, Priority& outPriority, Affinity affinity );
	Bool TryPop( TEntry& outEntry, Priority& outPriority );
	Uint32 GetApproximateDepth( Priority priority ) const;

private:
	TJobQueue& GetJobQueue( Priority priority, Affinity affinity = Affinity::All );

	red::StaticArray< red::DynArray< TEntry >, c_numQueues > m_entryStorage;
	TJobQueue m_jobQueue[c_numQueues];
	red::Atomic< Uint32 > m_approximateDepth[c_numQueues];
	DispatcherSemaphore m_jobQueueSema;

#ifdef RED_CONSOLE_CORE_7_SUPPORT
	red::DynArray< TEntry > m_consoleCore7EntryStorage;
	TJobQueue m_consoleCore7Queue;
	DispatcherSemaphore m_consoleCore7QueueSema;
#endif

#ifdef USE_RESOURCE_THROTTLER_THREADS
	red::DynArray< TEntry > m_resourceThrottlerEntryStorage;
	TJobQueue m_resourceThrottlerQueue;
	DispatcherSemaphore m_resourceThrottlerQueueSema;
#endif

    // to fix unit tests
    red::Atomic<Bool> m_isShuttingDown{ false };
};

} } //prv/job22

#include "jobDispatcherQueue.hpp"

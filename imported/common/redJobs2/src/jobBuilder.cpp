/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobBuilder.h"

namespace job
{

Builder::Builder(ScheduleParam param, const void* debugUserData)
	: m_debugName("")
	, m_debugUserData(debugUserData)
	, m_waitForZeroCounter(param, debugUserData)
	, m_accumulateCounter(param, debugUserData)
	, m_continuationCounter(nullptr)
	, m_param( param ) 
	, m_isExtracted(false)
	, m_debugNeedsExplicitFence(false)
{
	RED_TOUCH(m_threadOwner);

#ifdef RED_ASSERTS_ENABLED
	m_threadOwner.InitWithCurrentThread();
#endif

	if ( m_param.ioPriority == io::eAsyncPriority_INVALID )
	{
		m_param.ioPriority = io::GetThreadLocalIOPriority();
		m_waitForZeroCounter.Internal_SetIOPriority( m_param.ioPriority );
		m_accumulateCounter.Internal_SetIOPriority( m_param.ioPriority );
	}
}

Builder::Builder(const RunContext& runContext)
	: Builder(runContext.continuationContext.param, runContext.continuationContext.counter->Internal_GetDebugUserData())
{
	m_continuationCounter = runContext.continuationContext.counter;
}

void Builder::Sync_NoGuard()
{
	RED_FATAL_ASSERT(!m_isExtracted, "Cannot use Builder once final counter extracted from it");

	if (m_accumulateCounter.Internal_IsZeroSnapshot())
	{
		// Must re-use counter; otherwise will break the chain of dependencies
		// OK if was used before and hit zero again.
		return;
	}

	m_waitForZeroCounter.Reset( std::move(m_accumulateCounter) );
	m_accumulateCounter.Reset( Counter(m_param, m_debugUserData) );
}

// Similar to Sync_NoGuard(), only no more accumulateCounterEntry afterwards
void Builder::FinalSync_NoGuard()
{
	RED_FATAL_ASSERT(!m_isExtracted, "Cannot use Builder once final counter extracted from it");
	RED_FATAL_ASSERT(!m_debugNeedsExplicitFence, "DispatchFenceExplicitly() should have been called after using Fence::None");

	// Note: guarded against adding new jobs during NextCounter().
	// OK if the accumulator asynchronously hits zero any time here:
	// if accumulator is zero then either 
	// 1) no jobs were run against it
	// 2) or waitForZeroCounterEntry is zero in order for jobs dependent on it to run,
	// therefore no more chain of dependencies anyway
	if (m_accumulateCounter.Internal_IsZeroSnapshot())
	{
		auto counterForDestruction = std::move(m_accumulateCounter);
	}
	else
	{
		m_waitForZeroCounter.Reset( std::move(m_accumulateCounter) );
	}

	RED_FATAL_ASSERT(!m_accumulateCounter.Internal_IsValid()); // discarded
	RED_FATAL_ASSERT(m_waitForZeroCounter.Internal_IsValid()); // retained

	// Not owned, but now we need to link it up so the continuation won't finish until our last accumulate counter hits zero
	if (m_continuationCounter)
	{
		*m_continuationCounter += m_waitForZeroCounter;
	}
}

void Builder::DispatchFenceExplicitly()
{
	GuardThread();
	Sync_NoGuard();

#ifdef RED_ASSERTS_ENABLED
	m_debugNeedsExplicitFence = false;
#endif
}

void Builder::DispatchWait(const Counter& externalWaitCounter)
{
	GuardThread();

	RED_FATAL_ASSERT(!m_debugNeedsExplicitFence, "DispatchFenceExplicitly() should have been called after using Fence::None");

	// Or basically the equiv of emptyjob with dep, but don't need an actual empty job.
	// Note: accumulates with a fence.
	m_waitForZeroCounter += externalWaitCounter;
}

Counter Builder::ExtractWaitCounter()
{
	GuardThread();

	RED_FATAL_ASSERT(!m_isExtracted, "Already extracted counter");

	FinalSync_NoGuard();
	RED_FATAL_ASSERT(m_waitForZeroCounter.Internal_IsValid(), "Sanity check failed: no wait counter - bug in FinalSync?");
	RED_FATAL_ASSERT(!m_accumulateCounter.Internal_IsValid(), "Sanity check failed: still have accumulate counter - bug in FinalSync?");

	Counter extractedCounter = std::move(m_waitForZeroCounter);

	m_isExtracted = true;

	if (m_continuationCounter)
	{
		Counter finalCounter{ m_param };
		finalCounter += *m_continuationCounter;
		extractedCounter.Reset( std::move(finalCounter) );
	}

	return extractedCounter;
}

Builder::~Builder()
{
	GuardThread();

	if (!m_isExtracted)
	{
		FinalSync_NoGuard();
	}
}

} // job

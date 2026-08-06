/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobDispatcherSemaphore.h"

namespace job
{ 
namespace prv
{

#ifdef RED_PLATFORM_ORBIS
	const Int32 c_spinCount = 1000 * 1024;
#else
	const Int32 c_spinCount = 10 * 1024;
#endif


DispatcherSemaphore::DispatcherSemaphore()
	: m_count( 0 )
	, m_numThreadsWaiting( 0, std::numeric_limits<Int32>::max() )
	, m_spinCount( c_spinCount )
{
	red::profiler::SyncCreateMutex( this, "DispatcherSemaphore" );
}

DispatcherSemaphore::~DispatcherSemaphore()
{
	red::profiler::SyncDestroyMutex( this );
}

void DispatcherSemaphore::SetSpinCount( Int32 spinCount )
{
	m_spinCount = spinCount;
}

void DispatcherSemaphore::Acquire()
{
	red::profiler::SyncPrepare( this );

	Int32 spinCount = m_spinCount;
	do
	{
		const Int32 curVal = m_count.GetValue();
		if ( curVal > 0 && m_count.CompareExchange( curVal - 1, curVal ) == curVal )
		{
			red::profiler::SyncAcquired( this );
			return;
		}
	}
	while ( spinCount-- > 0 );

	const Int32 curVal = m_count.Decrement();
	if ( curVal < 0 )
	{
		// Slow path
		m_numThreadsWaiting.Acquire();
	}

	red::profiler::SyncAcquired( this );
}

Bool DispatcherSemaphore::TryAcquire()
{
	red::profiler::SyncPrepare( this );
	const Int32 curVal = m_count.GetValue();
	if ( curVal > 0 &&  m_count.CompareExchange( curVal - 1, curVal ) == curVal )
	{
		red::profiler::SyncAcquired( this );
		return true;
	}

	red::profiler::SyncCancel( this );
	return false;
}

void DispatcherSemaphore::Release( Int32 releaseCount /*= 1 */ )
{
	red::profiler::SyncReleasing( this );

	// Any releaseCount has to first be used for waking threads trying to Acquire() the kernel-semaphore, since once they wake they won't check the count again
	// This is handled naturally by having m_count go negative, so the releaseCount will go towards brining it back above zero before new threads can acquire it
	// Then the releaseCount (up to the number of waiting threads) can be used to wake waiting threads
	// Use all of releaseCount to wake threads (capped to the numWaitingThreads, after which m_count became non-negative)
	const Int32 oldVal = m_count.ExchangeAdd( releaseCount );
	if ( oldVal < 0 )
	{
		const Int32 numWaitingThreads = -oldVal;
		const Int32 numThreadsToWake = std::min< Int32 >( releaseCount, numWaitingThreads );
		m_numThreadsWaiting.Release( numThreadsToWake );
	}
}

}

}

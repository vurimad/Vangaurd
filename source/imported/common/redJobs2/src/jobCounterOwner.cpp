/*
* Copyright © 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobCounterOwner.h"
#include "jobDispatcher.h"
#include "jobDispatcherEntries.h"
#include "jobCounterFunctions.h"
#include "jobCounter.h"

namespace job
{

namespace prv
{
	extern Dispatcher* gDispatcher;
}

Counter::Counter( ScheduleParam param, const void* debugUserData /*= nullptr*/ )
{
	RED_FATAL_ASSERT( prv::gDispatcher );
	m_counter = prv::gDispatcher->InitJobCounter( "", param, debugUserData );
}

Counter::Counter( Counter&& other )
	: m_counter( other.m_counter )
{
	other.m_counter = nullptr;
}

Counter::Counter( prv::CounterEntry& counterEntry )
	: m_counter( &counterEntry )
{
	prv::gDispatcher->AddRefJobCounterInternal( m_counter );
}

void Counter::Reset(Counter&& rhs)
{
	if (this != &rhs)
	{
		// #tbd: test this
		//RED_FATAL_ASSERT(m_counter->GetPriority() == rhs.m_counter->GetPriority());
		Release();
		m_counter = rhs.m_counter;
		rhs.m_counter = nullptr;
	}
}

Counter::~Counter()
{
	Release();
}

namespace helper
{
	static void RunWaitOnCounterJob(const prv::CounterEntry* waitForZeroCounterEntry, prv::CounterEntry* accumulateCounterEntry)
	{
		JobDecl jobDecl;
		prv::gDispatcher->InitWithEmptyJob(jobDecl);
		prv::gDispatcher->RunJob(jobDecl, waitForZeroCounterEntry, accumulateCounterEntry);
	}
}

void Counter::operator+=( const Counter& waitForZeroCounter )
{
	// Queue optimization: skip unnecessary empty jobs below
	if (waitForZeroCounter.Internal_IsZeroSnapshot())
	{
		return;
	}
	helper::RunWaitOnCounterJob(waitForZeroCounter.Internal_GetCounter(), Internal_GetCounter());
}

CompletionDeferral Counter::CreateDeferral( const void* debugUserData, const char* debugName )
{
	RED_FATAL_ASSERT( m_counter, "Trying to use a moved counter?" );
	return prv::gDispatcher->CreateDeferral( debugName, debugUserData, *m_counter );
}

void Counter::Release()
{
	if ( m_counter )
	{
		prv::gDispatcher->ReleaseJobCounterInternal( m_counter );
		m_counter = nullptr;
	}
}

Bool Counter::Internal_IsZeroSnapshot() const
{
	RED_FATAL_ASSERT(m_counter);
	return prv::gDispatcher->IsZero_Snapshot(m_counter);
}

job::Priority Counter::Internal_GetPriority() const
{
	RED_FATAL_ASSERT(m_counter);
	return m_counter->GetPriority();
}

io::EAsyncPriority Counter::Internal_GetIOPriority() const
{
	RED_FATAL_ASSERT(m_counter);
	return m_counter->GetIOPriority();
}

void Counter::Internal_SetIOPriority(io::EAsyncPriority prio)
{
	RED_FATAL_ASSERT(m_counter);
	return m_counter->SetIOPriority( prio );
}

const void* Counter::Internal_GetDebugUserData() const
{
	RED_FATAL_ASSERT(m_counter);
	return m_counter->GetDebugUserData();
}

job::Affinity Counter::Internal_GetAffinity() const
{
	return m_counter->GetAffinity();
}

}

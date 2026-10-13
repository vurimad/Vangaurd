/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../redSystem/include/dbgUtils.h"
#include "../../redSystem/include/log.h"
#include "../../redCore/include/instrumentationObject.h"

#include "jobDebuggerStackTraceCache.h"
#include "jobDebugger.h"
#include "jobDispatcherEntries.h"
#include "jobRunner.h"
#include "jobDeferral.h"
#include "jobCounter.h"
#include "../redContainers/include/queue.h"

namespace job { namespace prv {

const StackTraceCacheEntryPath* Debugger::TraceCall()
{
	return nullptr;// helper::CaptureCallstackHistory( m_stackTraceCache );
}

// #tbd: could trace callstack too, but better to recompile with RED_DEBUG_NAME macro getting the __FILE__ __LINE__
void Debugger::RegisterCounter( const CounterEntry& counter )
{
	RED_SCOPE_LOCK(m_registerLock);
	m_registeredCounters.Insert(&counter);
}

void Debugger::UnregisterCounter( const CounterEntry& counter )
{
	RED_SCOPE_LOCK(m_registerLock);
	m_registeredCounters.Remove(&counter);
}

void Debugger::RegisterDeferral( const CompletionDeferral& deferral )
{
	RED_SCOPE_LOCK(m_registerLock);
	m_registeredDeferrals.Insert(&deferral);
}

void Debugger::UnregisterDeferral( const CompletionDeferral& deferral )
{
	RED_SCOPE_LOCK(m_registerLock);
	m_registeredDeferrals.Remove(&deferral);
}

void Debugger::CollectDependentJobs_NoWaitingListLock( const CounterEntry& counter, const CounterEntry& dependentCounter, red::DynArray< JobDecl >& outJobDecls )
{
	const WaitingListEntry* entriesHead = counter.waitingJobsHead;
	for ( const WaitingListEntry* entry = entriesHead; entry; entry = entry->next )
	{
		if (&dependentCounter == entry->accumulateCounterEntry)
		{
			outJobDecls.PushBack(entry->job);
		}
	}
}

static void CollectDeferrals_NoLock(const CounterEntry& counter, const red::ArraySpan<const CompletionDeferral* const> deferrals, red::DynArray<const CompletionDeferral* >& outDeferrals)
{
	for (const auto& it : deferrals)
	{
		if (it->GetDebugCounterMemAddr() == reinterpret_cast<Uint64>(&counter))
		{
			outDeferrals.PushBack(it);
		}
	}
}


void Debugger::AnalyzeCounter(const CounterEntry& counter)
{
	RED_SCOPE_SHARED_LOCK(m_registerLock);

	const Bool isZero = counter.counterValue.IsZero_Snapshot();
	RED_LOG_ERROR("Analyzing counter: %p: isZero=%d", &counter, isZero );
	if (isZero)
	{
		return;
	}

	RED_LOG_ERROR("||| Total registered counters: %u", m_registeredCounters.Size());
	RED_LOG_ERROR("||| Total registered deferrals: %u", m_registeredDeferrals.Size());

	RED_LOG_FLUSH_AND_WAIT();

	red::DynArray<const CounterEntry* > derpCounters{ PoolJobs2Debug() };

	for (auto& it : m_registeredCounters)
	{
		it->waitingListLock.Acquire();
	}

	red::HashSet<const CounterEntry* > seenCounters{ PoolJobs2Debug() };
	seenCounters.Reserve(m_registeredCounters.Size());
	seenCounters.Insert(&counter);

	struct QueueEntry
	{
		const CounterEntry* counter;
		Uint32 depth;
	};

	struct BlockerEntry
	{
		const CounterEntry* counter;
		Uint32 depth;
		red::DynArray<const CompletionDeferral* > deferrals{ PoolJobs2Debug() };
		red::DynArray<JobDecl> jobDecls{ PoolJobs2Debug() };
	};

	red::DynArray< BlockerEntry > blockers{ PoolJobs2Debug() };
	red::DynArray<JobDecl> jobDecls{ PoolJobs2Debug() };
	red::DynArray<const CompletionDeferral* > deferrals{ PoolJobs2Debug() };
	red::Queue< QueueEntry > countersToCheck{ PoolJobs2Debug() };
	countersToCheck.Push({ &counter, 0 });
	while (!countersToCheck.Empty())
	{
		deferrals.Clear();
		jobDecls.Clear();

		QueueEntry entry = countersToCheck.Pop();
//		RED_LOG_ERROR("Counter depth=%u: %hs [%p]", entry.depth, entry.counter->debugName, entry.counter);

		CollectDeferrals_NoLock(*entry.counter, m_registeredDeferrals.Elements(), deferrals);

		for (const CounterEntry* counter : m_registeredCounters)
		{
			if (counter == entry.counter)
			{
				continue;
			}

			const Uint32 oldSize = jobDecls.Size();
			CollectDependentJobs_NoWaitingListLock(*counter, *entry.counter, jobDecls);
			const Uint32 newSize = jobDecls.Size();
			RED_FATAL_ASSERT(newSize >= oldSize);
			if ( newSize > oldSize && seenCounters.Insert(counter).IsSuccessful())
			{
				countersToCheck.Push({ counter, entry.depth + 1 });
			}
		}

		blockers.PushBack({ entry.counter, entry.depth, deferrals, jobDecls });
	}

	for (const BlockerEntry& entry : blockers)
	{
		const Bool isRunnable = entry.deferrals.Empty() && entry.jobDecls.Empty();
		RED_LOG_ERROR("[%hs] Counter depth=%u: %hs [%p]", (isRunnable ? "RUNNABLE" : "WAITING"), entry.depth, entry.counter->debugName, entry.counter);

		const auto& deferrals = entry.deferrals;
		if (!deferrals.Empty())
		{
			RED_LOG_ERROR("\tBlocked by %u deferrals", deferrals.Size());
			for (const auto& deferral : deferrals)
			{
				RED_LOG_ERROR("\t\tDeferral: %hs", deferral->GetDebugName());
				RED_LOG_ERROR("\t\tDeferral data addr: 0x%p", deferral->GetDebugUserData());

				// #tbd: memcpy into null terminated buffer and print
				//deferral->GetDebugUserData() ? (const char*)deferral->GetDebugUserData() : deferral->GetDebugName());
			}
		}

		const auto& jobDecls = entry.jobDecls;
		if (!jobDecls.Empty())
		{
			RED_LOG_ERROR("\tBlocked by %u jobs on counter '%hs' (%p)", jobDecls.Size(), entry.counter->debugName, entry.counter);
			for (const JobDecl& job : jobDecls)
			{
				RED_LOG_ERROR("\t\tJob: %hs", job.instrumentationObject->m_name);
			}
		}
	}

	for (auto& it : m_registeredCounters)
	{
		it->waitingListLock.Release();
	}
	   
	RED_LOG_ERROR( ">>> Job Debugger Dump: END<<<" );
	RED_LOG_FLUSH();
}

//////////////////////////////////////////////////////////////////////////

Debugger::Debugger()
	: m_registeredCounters( job::PoolJobs2Debug() )
	, m_registeredDeferrals( job::PoolJobs2Debug() )
{
}

Debugger::~Debugger()
{
}

} }

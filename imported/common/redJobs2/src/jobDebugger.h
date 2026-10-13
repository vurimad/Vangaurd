/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../redSystem/include/threads.h"
#include "../../redSystem/include/readWriteSpinLock.h"
#include "../../redSystem/include/redThreadsThread.h"
#include "jobMemoryPools.h"
#include "jobDebuggerStackTraceCache.h"
#include "jobPriority.h"
#include "jobDecl.h"

namespace job
{
class CounterChain;
struct JobDecl;
struct StackTraceCacheEntryPath;
class CompletionDeferral;

namespace prv
{

class CounterEntry;

class Debugger : red::NonCopyable
{
	RED_USE_MEMORY_POOL( PoolJobs2Debug );

public:
	Debugger();
	~Debugger();

	void AnalyzeCounter(const CounterEntry& counter);

	void RegisterCounter( const CounterEntry& counter );
	void UnregisterCounter( const CounterEntry& counter );

	void RegisterDeferral( const CompletionDeferral& deferral );
	void UnregisterDeferral( const CompletionDeferral& deferral );

	const StackTraceCacheEntryPath* TraceCall();

private:
	void CollectDependentJobs_NoWaitingListLock(const CounterEntry& counter, const CounterEntry& dependentCounter, red::DynArray< JobDecl >& outJobDecls);

	mutable red::RWSpinLock m_registerLock;
	red::HashSet<const CounterEntry*> m_registeredCounters;
	red::HashSet<const CompletionDeferral*> m_registeredDeferrals;

	static const Uint32 c_maxRegisteredThreads = 64;

	// Deferrals are moveable, so need to also update under lock...

};

} // prv
} // job

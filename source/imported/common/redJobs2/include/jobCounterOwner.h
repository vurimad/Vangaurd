/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "jobPriority.h"
#include "jobDeferral.h"
#include "jobCounterFunctions.h"

namespace job
{

namespace prv
{
	class Dispatcher;
	class CounterEntry;
}

enum class Priority : Uint8;

// A synchronization primitive used by the job system.
// It tracks a subset of the number of jobs currently running.
// The counter is atomically decremented as each job finishes.
//
// Jobs are run taking two arguments: 1) a dependency counter, ready when it's zero, 
// and 2) a counter to increment - decremented automatically as the job finishes.
// This makes arbitrary job dependencies possible.
//
// NOTE: The job system will keep the counter's data alive as long as needed.
// It's OK to start running jobs against this counter and then let it destruct.
class REDJOBS2_API Counter : red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

	friend class prv::Dispatcher;

public:
	CompletionDeferral CreateDeferral( const void* debugUserData = nullptr, const char* debugName = nullptr );
	
	explicit Counter( ScheduleParam param = ScheduleParam(), const void* debugUserData = nullptr );

	// Note: other/rhs invalid after move.
	// More applicable to moved member variables: can be valid again if assigned to (i.e., on lhs).
	Counter( Counter&& other );

	// Moves the rhs into this one.
	// NOTE: This is NOT thread safe. Use only when sure when this counter will not be in use.
	void Reset(Counter&& rhs);

	~Counter();

	// Make waitForZeroCounter a dependency.
	// This Counter will not reach zero before waitForZeroCounter.
	// NOTE: This function is thread safe, but doesn't magically prevent you from creating your own race conditions.
	void operator+=( const Counter& waitForZeroCounter );

	prv::CounterEntry* Internal_GetCounter() const { return m_counter; }

	Bool Internal_IsZeroSnapshot() const;

	Bool Internal_IsValid() const { return m_counter; }

	job::Priority Internal_GetPriority() const;
	job::Affinity Internal_GetAffinity() const;
	io::EAsyncPriority Internal_GetIOPriority() const;
	void Internal_SetIOPriority( io::EAsyncPriority prio );

	const void* Internal_GetDebugUserData() const;

private:
	explicit Counter(prv::CounterEntry& counterEntry);

	void Release();

	mutable prv::CounterEntry* m_counter;
};

}

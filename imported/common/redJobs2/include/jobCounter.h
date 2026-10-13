/**
* Copyright (c) 2018 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once
#include "../../redSystem/include/utility.h"

#include "redJobs2Api.h"
#include "jobMemoryPools.h"
#include "jobDispatcherCounterValue.h"
#include "jobDispatcherRefCountMask.h"
#include "jobPriority.h"

namespace job
{

namespace prv
{

class Dispatcher; 
class DispatcherHelper;
class Debugger;
struct WaitingListEntry;

class REDJOBS2_API CounterEntry : red::NonCopyable
{
	RED_USE_MEMORY_POOL( PoolJobs2Counters );

	friend class prv::Dispatcher;
	friend class prv::DispatcherHelper;
	friend class prv::Debugger;

public:
	CounterEntry( CounterEntry&& ) = delete;
	void operator=( CounterEntry&& ) = delete;

	const void* GetDebugUserData() const { return debugUserData;  }
	Priority GetPriority() const { return param.priority; }
	Affinity GetAffinity() const { return param.affinity; }
	io::EAsyncPriority GetIOPriority() const { return param.ioPriority; }
	void SetIOPriority( io::EAsyncPriority prio ) { param.ioPriority = prio; }

private:
	CounterEntry() = default;

	RED_INLINE explicit CounterEntry( const ScheduleParam& inParam )
		: CounterEntry()
	{
		param = inParam;
	}

	mutable prv::WaitingListEntry* waitingJobsHead{ nullptr };
	const char* debugName{ "" };

	union
	{
		const void* debugUserData{ nullptr };
		const char* __debugUserDataAsChar;
	};

	prv::DispatcherCounterValue counterValue{ 0 };
	mutable prv::DispatcherRefCountMask refCountMask{ 1 };
	mutable red::SpinLock waitingListLock;
	ScheduleParam param;
};

static_assert( sizeof( CounterEntry ) <= 64, "Unexpected Counter size" );
#ifdef RED_MEMORY_ENABLE_HOOKS
static_assert( sizeof( CounterEntry ) <= ( 64 - 8 ), "Unexpected Counter size" );
#endif

}
}
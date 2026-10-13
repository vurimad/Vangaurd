/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redThreadsThread.h"

namespace job { namespace prv
{

class Dispatcher;
class JobScopeMemoryAllocator;

struct DispatcherThreadSetup
{
	Uint32 stackSizeKB{0};
	red::TAffinityMask affinityMask{0};
	Uint32 dispatcherThreadIndex{0};
};

class DispatcherThread final : public red::Thread
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	DispatcherThread( const char* threadName, Dispatcher& dispatcher, const DispatcherThreadSetup& setup );

	Bool IsReady() const { return m_isReady.GetValue(); }

private:

	virtual void ThreadFunc() override;
	void SetupDispatcher();
	void DoWorkLoop();
#ifdef RED_CONSOLE_CORE_7_SUPPORT
	void DoConsoleCore7WorkLoop();
#endif
#ifdef USE_RESOURCE_THROTTLER_THREADS
	void DoResourceThrottlerWorkLoop();
#endif

	Dispatcher& m_dispatcher;
	JobScopeMemoryAllocator* m_jobScopeAllocator;
	DispatcherThreadSetup m_setup;
	TLocalQueue m_localQueue;
	red::Atomic< Bool > m_isReady;
};

}
} // prv/job

/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../redSystem/include/threads.h"
#include "../../redCore/include/profilerManager.h"
#include "../../redCore/include/profiler.h"

#include "jobPriority.h"
#include "jobDispatcher.h"
#include "jobDispatcherThread.h"

namespace job { namespace prv
{

extern thread_local Bool g_tls_IsDispatcherThread;
extern thread_local Uint32 g_tls_DispatcherThreadIndex;

DispatcherThread::DispatcherThread( const char* threadName, Dispatcher& dispatcher, const DispatcherThreadSetup& setup )
	: red::Thread( threadName, red::ThreadMemParams{ setup.stackSizeKB * 1024 } )
	, m_dispatcher( dispatcher )
	, m_jobScopeAllocator( nullptr )
	, m_setup( setup )
	, m_localQueue(PoolJobs2QueueLocal())
{
	m_localQueue.Reserve(c_localQueueDefaultCapacity);

	RED_ASSERT( m_setup.dispatcherThreadIndex > 0 );
}

void DispatcherThread::ThreadFunc()
{
	SetupDispatcher();

#ifdef RED_CONSOLE_CORE_7_SUPPORT
	
	if( m_setup.affinityMask == RED_FLAG( 6 ) ) // ctremblay: dispatcher on Core 7 operate on only one special job queue.
	{
		// Check that this is the dispatcher for the 7th core
		RED_ASSERT( m_setup.dispatcherThreadIndex == 6 );
		DoConsoleCore7WorkLoop();
	}
#ifdef USE_RESOURCE_THROTTLER_THREADS
	else if ( m_setup.dispatcherThreadIndex > m_dispatcher.GetNumDispatcherThreads(false) )
	{
		DoResourceThrottlerWorkLoop();
	}
#endif
	else
	{
		DoWorkLoop();
	}
	
#else // RED_CONSOLE_CORE_7_SUPPORT

	DoWorkLoop();

#endif // RED_CONSOLE_CORE_7_SUPPORT

}

void DispatcherThread::DoWorkLoop()
{
	for( ;; )
	{
		Priority priority = Priority::CriticalPath;
		JobQueueEntry entry;
		if (!m_localQueue.Empty())
		{
			// Release memory if queue grew to accommodate spikes but now has settled down 
			if (m_localQueue.Size() == c_localQueueDefaultCapacity)
			{
				m_localQueue.Shrink();
			}

			auto& next = m_localQueue.Front();
			entry = next.first;
			priority = next.second;
			m_localQueue.PopFront();
		}
		else
		{
			m_dispatcher.PopJobQueueEntry( entry, priority );
		}

		if ( m_dispatcher.IsExitRequested() )
		{
			break;
		}

		m_dispatcher.DoRunJobQueueEntry( entry, m_setup.dispatcherThreadIndex, priority, *m_jobScopeAllocator, &m_localQueue );
	}
}

#ifdef RED_CONSOLE_CORE_7_SUPPORT
void DispatcherThread::DoConsoleCore7WorkLoop()
{
	for( ;; )
	{
		Priority priority = Priority::Latent;
		JobQueueEntry entry;
		m_dispatcher.PopJobQueueEntry( entry, priority, Affinity::ConsoleCore7 );
		if( m_dispatcher.IsExitRequested() )
		{
			break;
		}
		m_dispatcher.DoRunJobQueueEntry( entry, m_setup.dispatcherThreadIndex, priority, *m_jobScopeAllocator, nullptr );
	}
}
#endif

#ifdef USE_RESOURCE_THROTTLER_THREADS
void DispatcherThread::DoResourceThrottlerWorkLoop()
{
	for( ;; )
	{
		Priority priority = Priority::Latent;
		JobQueueEntry entry;
		m_dispatcher.PopJobQueueEntry( entry, priority, Affinity::ResourceThrottler );
		if( m_dispatcher.IsExitRequested() )
		{
			break;
		}
		m_dispatcher.DoRunJobQueueEntry( entry, m_setup.dispatcherThreadIndex, priority, *m_jobScopeAllocator, nullptr );
	}
}
#endif

void DispatcherThread::SetupDispatcher()
{
	g_tls_IsDispatcherThread = true;
	g_tls_DispatcherThreadIndex = m_setup.dispatcherThreadIndex;

	if( m_setup.affinityMask != 0 )
	{
		SetAffinityMask( m_setup.affinityMask );
	}

	red::memory::RegisterCurrentThread( GetThreadName() );

	m_jobScopeAllocator = &job::PoolJobScope::GetAllocator();
	RED_FATAL_ASSERT( m_jobScopeAllocator, "Job scope allocator does not exist" );

#if !defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
	m_jobScopeAllocator->RegisterCurrentThread();
#endif

	PROFILER_InitThread( GetThreadName(), 16384 );

	m_isReady.SetValue( true );
}

} } // job/prv

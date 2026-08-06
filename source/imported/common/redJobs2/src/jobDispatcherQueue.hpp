/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#define USE_JOB_QUEUE_SEMA

namespace job { namespace prv {
template< typename TEntry >
RED_INLINE DispatcherQueue<TEntry>::DispatcherQueue()
	: m_entryStorage( c_numQueues, PoolJobs2() )
#ifdef RED_CONSOLE_CORE_7_SUPPORT
	, m_consoleCore7EntryStorage( PoolJobs2QueueBackgroundPriority() )
#endif
#ifdef USE_RESOURCE_THROTTLER_THREADS
	, m_resourceThrottlerEntryStorage( PoolJobs2QueueBackgroundPriority() )
#endif
{
}

template< typename TEntry >
RED_INLINE void DispatcherQueue<TEntry>::Initialize( const DispatcherQueueSetup& setup )
{
	m_entryStorage[ (Uint32)Priority::Latent ].SetPool( PoolJobs2QueueLowPriority() );
	m_entryStorage[ (Uint32)Priority::RenderPath ].SetPool( PoolJobs2QueueNormalPriority() );
	m_entryStorage[ (Uint32)Priority::CriticalPath ].SetPool( PoolJobs2QueueNormalPriority() );
	m_entryStorage[ (Uint32)Priority::Immediate ].SetPool( PoolJobs2QueueHighPriority() );

	m_entryStorage[ (Uint32)Priority::Latent ].Resize( setup.numLowPriorityJobs );
	m_entryStorage[ (Uint32)Priority::RenderPath ].Resize( setup.numNormalPriorityJobs );
	m_entryStorage[ (Uint32)Priority::CriticalPath ].Resize( setup.numNormalPriorityJobs );
	m_entryStorage[ (Uint32)Priority::Immediate ].Resize( setup.numHighPriorityJobs );

	m_jobQueue[ (Uint32)Priority::Latent ].Initialize( m_entryStorage[ (Uint32)Priority::Latent ] );
	m_jobQueue[ (Uint32)Priority::RenderPath ].Initialize( m_entryStorage[ (Uint32)Priority::RenderPath ] );
	m_jobQueue[ (Uint32)Priority::CriticalPath ].Initialize( m_entryStorage[ (Uint32)Priority::CriticalPath ] );
	m_jobQueue[ (Uint32)Priority::Immediate ].Initialize( m_entryStorage[ (Uint32)Priority::Immediate ] );

#ifdef RED_CONSOLE_CORE_7_SUPPORT
	m_consoleCore7EntryStorage.Resize( setup.numCore7Jobs );
	m_consoleCore7Queue.Initialize( m_consoleCore7EntryStorage );
#endif
#ifdef USE_RESOURCE_THROTTLER_THREADS
	m_resourceThrottlerEntryStorage.Resize( setup.numCore7Jobs );
	m_resourceThrottlerQueue.Initialize( m_resourceThrottlerEntryStorage );
#endif
}

template< typename TEntry >
void job::prv::DispatcherQueue<TEntry>::Shutdown( Uint32 numResourceThrottlerThreads )
{
    m_isShuttingDown.SetValue( true );
#ifdef RED_CONSOLE_CORE_7_SUPPORT
    m_consoleCore7QueueSema.Release();
#endif

#ifdef USE_RESOURCE_THROTTLER_THREADS
    m_resourceThrottlerQueueSema.Release( numResourceThrottlerThreads );
#endif
}

template< typename TEntry >
RED_INLINE Bool DispatcherQueue<TEntry>::TryPush( const TEntry& entry, Priority priority, Affinity affinity, Uint32 spinCount )
{
	Uint32 failCount = 0;
	auto& jobQueue = GetJobQueue( priority, affinity );
	const Bool tracksMainQueue = affinity == Affinity::All;
	if ( tracksMainQueue )
	{
		m_approximateDepth[ static_cast< Uint32 >( priority ) ].Increment();
	}
	while ( !jobQueue.Push( entry ) )
	{
		failCount += 1;
		if (failCount == spinCount)
		{
			if ( tracksMainQueue )
			{
				m_approximateDepth[ static_cast< Uint32 >( priority ) ].Decrement();
			}
			return false;
		}
	}
#ifdef USE_JOB_QUEUE_SEMA
#ifdef RED_CONSOLE_CORE_7_SUPPORT
	if( affinity == Affinity::All )
	{
		m_jobQueueSema.Release();
	}
	else if( affinity == Affinity::ConsoleCore7 )
	{
		m_consoleCore7QueueSema.Release();
	}
#ifdef USE_RESOURCE_THROTTLER_THREADS
	else if( affinity == Affinity::ResourceThrottler )
	{
		m_resourceThrottlerQueueSema.Release();
	}
#endif 
#else // RED_CONSOLE_CORE_7_SUPPORT
	m_jobQueueSema.Release();
#endif
#endif

	return true;
}

template< typename TEntry >
RED_INLINE typename DispatcherQueue<TEntry>::TJobQueue& DispatcherQueue<TEntry>::GetJobQueue( Priority priority, Affinity affinity )
{
	static_assert( static_cast<Uint32>( Priority::Latent ) == 0, "" );
	static_assert( static_cast<Uint32>( Priority::RenderPath ) == 1, "" );
	static_assert( static_cast<Uint32>( Priority::CriticalPath ) == 2, "" );
	static_assert( static_cast<Uint32>( Priority::Immediate ) == 3, "" );
#ifdef RED_CONSOLE_CORE_7_SUPPORT
	switch ( affinity )
	{
		case Affinity::All :
			return m_jobQueue[ static_cast<Uint32>( priority ) ];
		case Affinity::ConsoleCore7 :
			return m_consoleCore7Queue;
#ifdef USE_RESOURCE_THROTTLER_THREADS
		case Affinity::ResourceThrottler :
			return m_resourceThrottlerQueue;
#endif
	}
#endif // RED_CONSOLE_CORE_7_SUPPORT

	return m_jobQueue[ static_cast<Uint32>( priority ) ];
}

template< typename TEntry >
RED_INLINE void DispatcherQueue<TEntry>::Pop( TEntry& outEntry, Priority& outPriority )
{
#ifdef USE_JOB_QUEUE_SEMA
	m_jobQueueSema.Acquire();
#endif
	RED_TOUCH( m_jobQueueSema );

	// If using the semaphore, we don't know what job queue is ready
	// and there's nothing stopping other threads to keep taking a job
	// from a queue we haven't checked yet, so have to keep looping
	// and trust that the semaphore was released correctly
	for ( ;; )
	{
		if ( m_jobQueue[ static_cast<Uint32>( Priority::Immediate ) ].Pop( outEntry ) )
		{
			outPriority = Priority::Immediate;
			break;
		}

		if ( m_jobQueue[ static_cast<Uint32>( Priority::CriticalPath ) ].Pop( outEntry ))
		{
			outPriority = Priority::CriticalPath;
			break;
		}

		if( m_jobQueue[ static_cast< Uint32 >( Priority::RenderPath ) ].Pop( outEntry ) )
		{
			outPriority = Priority::RenderPath;
			break;
		}

		if ( m_jobQueue[ static_cast<Uint32>( Priority::Latent ) ].Pop( outEntry ) )
		{
			outPriority = Priority::Latent;
			break;
		}
	}
	m_approximateDepth[ static_cast< Uint32 >( outPriority ) ].Decrement();
}

template< typename TEntry >
RED_INLINE void DispatcherQueue<TEntry>::Pop( TEntry& outEntry, Priority& outPriority, Affinity affinity )
{
#ifdef RED_CONSOLE_CORE_7_SUPPORT

	if( affinity == Affinity::ConsoleCore7 )
	{	
		m_consoleCore7QueueSema.Acquire();

		for( ;; )
		{
			if( m_consoleCore7Queue.Pop( outEntry ) )
			{
				outPriority = Priority::Latent;
				break;
			}
		}
	}
#ifdef USE_RESOURCE_THROTTLER_THREADS
	else if ( affinity == Affinity::ResourceThrottler )
	{
		m_resourceThrottlerQueueSema.Acquire();

		for( ;; )
		{
            if( m_isShuttingDown.GetValue() )
                break;

			if( m_resourceThrottlerQueue.Pop( outEntry ) )
			{
				outPriority = Priority::Latent;
				break;
			}
		}
	}
#endif
	else 
	{
		Pop( outEntry, outPriority ); 
	}

#else // RED_CONSOLE_CORE_7_SUPPORT

	Pop( outEntry, outPriority ); 

#endif // RED_CONSOLE_CORE_7_SUPPORT
}

template< typename TEntry >
RED_INLINE Bool DispatcherQueue<TEntry>::TryPop( TEntry& outEntry, Priority& outPriority )
{
#ifdef USE_JOB_QUEUE_SEMA
	if ( !m_jobQueueSema.TryAcquire() )
	{
		return false;
	}
#endif
	RED_TOUCH( m_jobQueueSema );

	Bool hasEntry = false;

	// If using the semaphore, we don't know what job queue is ready
	// and there's nothing stopping other threads to keep taking a job
	// from a queue we haven't checked yet, so have to keep looping
	// and trust that the semaphore was released correctly
	do
	{
		if ( GetJobQueue( Priority::Immediate ).Pop( outEntry ) )
		{
			outPriority = Priority::Immediate;
			hasEntry = true;
		}
		else if ( GetJobQueue( Priority::CriticalPath ).Pop( outEntry ) )
		{
			outPriority = Priority::CriticalPath;
			hasEntry = true;
		}
		else if( GetJobQueue( Priority::RenderPath ).Pop( outEntry ) )
		{
			outPriority = Priority::RenderPath;
			hasEntry = true;
		}
		else if ( GetJobQueue( Priority::Latent ).Pop( outEntry ) )
		{
			outPriority = Priority::Latent;
			hasEntry = true;
		}
	}
#ifdef USE_JOB_QUEUE_SEMA
	while ( !hasEntry );
#else
	while( false );
#endif

	if ( hasEntry )
	{
		m_approximateDepth[ static_cast< Uint32 >( outPriority ) ].Decrement();
	}
	return hasEntry;
}

template< typename TEntry >
RED_INLINE Uint32 DispatcherQueue<TEntry>::GetApproximateDepth( Priority priority ) const
{
	return m_approximateDepth[ static_cast< Uint32 >( priority ) ].GetValue();
}

} } // job/prv

/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../redSystem/include/log.h"
#include "../../redCore/include/settings.h"
#include "../../redCore/include/profiler.h"

#if defined(RED_PLATFORM_DURANGO)
// Extra defines for new Console Types
#define CONSOLE_TYPE_XBOX_SERIES_S        ((CONSOLE_TYPE)5)
#define CONSOLE_TYPE_XBOX_SERIES_X        ((CONSOLE_TYPE)6)
#define CONSOLE_TYPE_XBOX_SERIES_X_DEVKIT ((CONSOLE_TYPE)7)
#endif

#include "jobPriority.h"
#include "jobDispatcher.h"
#include "jobDispatcherThread.h"
#include "jobCounter.h"
#include "jobDecl.h"
#include "jobDebugger.h"
#include "jobCounterOwner.h"
#include "../../redSystem/include/unitTestMode.h"

// At least leave this in until the system is given a decent soak
#ifndef RED_CONFIGURATION_FINAL
# define USE_JOB_DEBUGGER
#endif

#ifdef RED_PLATFORM_WINPC
# define MUST_PUMP_WIN32_MESSAGES_FOR_DXGI
#endif

#ifdef USE_JOB_DEBUGGER
static const constexpr Bool c_useJobDebugger = true;
#else
static const constexpr Bool c_useJobDebugger = false;
#endif

// #todo: warn once vs spam
#ifndef NO_EDITOR
static const constexpr Bool c_warnOnPriorityInversion = false;
#else
static const constexpr Bool c_warnOnPriorityInversion = true;
#endif

#ifdef USE_RESOURCE_THROTTLER_THREADS
	static const Uint32	numAdditionalResourceThrottlerThreads = 2;
#else
	static const Uint32	numAdditionalResourceThrottlerThreads = 0;
#endif

namespace dd
{
	static red::CrashData< Uint32 > numDispatcherThreads{ "Jobs/Dispatcher", "NumDispatcherThreads" };
	static red::CrashData< Uint32 > maxHWConcurrency{ "Jobs/Dispatcher", "MaxHWConcurrency" };
}

namespace job { namespace prv
{

thread_local Bool g_tls_IsDispatcherThread = false;
thread_local Uint32 g_tls_DispatcherThreadIndex = UINT32_MAX;

namespace hacks
{
	REDJOBS2_API Bool IsDispatcherThread()
	{
		return g_tls_IsDispatcherThread;
	}
}

prv::Dispatcher* gDispatcher;

class DispatcherHelper
{
public:
	static CounterEntry* AllocCounterEntry( const char* debugName, const ScheduleParam& param, const void* debugUserData )
	{
		auto* counter = RED_NEW_WITHOUT_HOOKS( CounterEntry,  red::memory::HookType_Memory_Marking )();
		counter->debugName = debugName;
		counter->debugUserData = debugUserData;
		counter->param = param;
		if ( c_useJobDebugger && gDispatcher->GetDebugger() )
		{
			gDispatcher->GetDebugger()->RegisterCounter( *counter );
		}
		return counter;
	}

	static void FreeEntry( const CounterEntry* entry )
	{
		RED_FATAL_ASSERT( entry );
		RED_FATAL_ASSERT( entry->refCountMask.IsZero_Snapshot() );
		RED_FATAL_ASSERT( entry->counterValue.IsZero_Snapshot() );

		// Since a counter can be used and drop to zero multiple times, the waitingList should have been flushed
		// each and every time before finally being freed
		RED_FATAL_ASSERT( entry->waitingJobsHead == nullptr );

		if ( c_useJobDebugger && gDispatcher->GetDebugger() )
		{
			gDispatcher->GetDebugger()->UnregisterCounter( *entry );
		}

		RED_DELETE( entry );
	}

	static WaitingListEntry* FlushWaitingList( CounterEntry& counterEntry )
	{
		auto& lock = counterEntry.waitingListLock;
		RED_SCOPE_LOCK( lock );
		
		// Have to check counter value again under lock.
		// Otherwise you can have this (rare) scenario where you violate the sequentially-consistent ordering of events.
		// 1) J1 finishes running on thread T1, and T1 decrements counter W to zero. But does not call FlushWaitingList() yet.
		// 2) Then a new job J2 is created on thread T2, and increments counter W to 1. We reused counter W because it was zero (that's how CounterChain operates to avoid breaking the chain of dependencies).
		// 3) Then yet another new job J3 is created (on any thread except T1; doesn't matter) and increments new counter A, waiting for counter W.
		// 4) J3 goes on counter W's waiting list because W > 0.
		// 5) Thread T1 now finally calls FlushWaitingList() without a lock. We flush J3 from counter W's waiting list and queue it to run before J2 finishes running (or even starts).
		//
		// NOTE: if T1 checked the counter value again without a lock, then it's wrong because T1 could check in step (1.5) and then have the same situation again.
		// If T1 is holding this lock, then step (4) can't complete until we release the lock, at which point step (2) has already completed, incrementing W > 0.
		//
		// NOTE: it's always possible some counter *already* has waiting entries, then hits zero, and then gets incremented again, preventing it from flushing the waiting list.
		// However, the waiting list will eventually be flushed -- but not prematurely. And you can't deadlock unless your accumulate and wait counters are the same, or create some other cycle.
		//
		// CounterChain indirectly prevents this "denial of flushing" by not accumulating new jobs on the waitForZero counter entry.
		// (The only exception being the continuation, but that counter cannot have dropped to zero yet anyway).
		if ( !counterEntry.counterValue.IsZero_Snapshot() )
		{
			return nullptr;
		}

		auto* ret = counterEntry.waitingJobsHead;
		counterEntry.waitingJobsHead = nullptr;
		return ret;
	}
};

namespace helper
{
	static void FreeWaitingListEntryLinkedList( const WaitingListEntry* entryHead )
	{
		auto* entryIter = entryHead;
		while ( entryIter )
		{
			auto* next = entryIter->next;
			RED_FATAL_ASSERT( entryIter != next );
			RED_DELETE( entryIter );
			entryIter = next;
		}
	}

	RED_FORCE_INLINE void FreeEntry( const WaitingListEntry* entry )
	{
		FreeWaitingListEntryLinkedList( entry );
	}

	RED_FORCE_INLINE void FreeEntry( const ParallelForSharedCounterEntry* entry )
	{
		RED_DELETE( entry );
	}

	RED_FORCE_INLINE void FreeEntry( const ParallelForJobEntry* entry )
	{
		RED_DELETE( entry );
	}

	template <typename T>
	T* AllocEntry()
	{
		return RED_NEW_WITHOUT_HOOKS( T, red::memory::HookType_Memory_Marking )();
	}
	   
	static void EmptyJob( void* jobData, const RunContext& )
	{
		// Do nothing
	}
	
	static red::InstrumentationObject s_emptyJobInstrumentationObject{ "EmptyJob" };

	static Uint32 GetNumDispatcherThreads( const InitParam& setup )
	{
#if defined( RED_PLATFORM_WINPC )
		RED_FATAL_ASSERT( red::GetMaxHardwareConcurrency() > 0 );
		const Uint32 numThreads = std::max< Uint32 >( 1, (Int32)red::GetMaxHardwareConcurrency() - 1 );
#elif defined( RED_PLATFORM_LINUX )
		// #tbd: this should be constrained depending on how many instances per machine we want to run
		RED_FATAL_ASSERT( red::GetMaxHardwareConcurrency() > 0 );
		const Uint32 numThreads = std::max< Uint32 >( 1, ( Int32 )red::GetMaxHardwareConcurrency() - 1 );
#elif defined( RED_PLATFORM_ORBIS )
		RED_FATAL_ASSERT( red::GetMaxHardwareConcurrency() == 7, "7CPU mode not enabled in PS4 param.sfo/Xbox manifest!" );
	#ifdef RED_CONSOLE_CORE_7_SUPPORT
			const Uint32 numThreads = 6;
	#else
			const Uint32 numThreads = 5;
	#endif
#elif defined( RED_PLATFORM_DURANGO )

	#ifdef RED_CONSOLE_CORE_7_SUPPORT
		Uint32 numThreads = 0;

		// We need this value to be flexible based on the console its being run on (only called once during setup)
		CONSOLE_TYPE consoleType = ::GetConsoleType();
		if ( consoleType == CONSOLE_TYPE_XBOX_SERIES_X || consoleType == CONSOLE_TYPE_XBOX_SERIES_X_DEVKIT )
		{
			numThreads = 7;
		}
		else
		{
			numThreads = 6;
		}
	#else
		const Uint32 numThreads = 5;
	#endif
#else
#error Unsupported platform!
#endif

#ifdef RED_PLATFORM_CONSOLE
		if( red::UnitTestMode()  )
		{
			return std::min<Uint32>( setup.maxThreads, numThreads ); 
		}

		return numThreads;
#else 
		return std::min<Uint32>( setup.maxThreads, numThreads ); 
#endif
	}

	static red::TAffinityMask GetDispatcherThreadAffinityMask( Uint32 threadIndex )
	{
		// #tbd: let's explore hard affinities once we don't have to fight on the second cluster against things like I/O and other threads
		RED_UNUSED( threadIndex );

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX )

		RED_FATAL_ASSERT( red::GetMaxHardwareConcurrency() > 0 );
		const red::TAffinityMask affinityMask{ 0 };

#elif defined( RED_PLATFORM_ORBIS )

		red::TAffinityMask affinityMask = 0;

		switch (threadIndex)
		{
		case 0:
			affinityMask = RED_FLAG64( 0 ) | RED_FLAG64( 1 );
			break;
		case 1:
			affinityMask = RED_FLAG64( 2 ) | RED_FLAG64( 3 );
			break;
		case 2:
			affinityMask = RED_FLAG64( 2 ) | RED_FLAG64( 3 );
			break;
		case 3:
			affinityMask = RED_FLAG64( 4 ) | RED_FLAG64( 5 );
			break;
		case 4:
			affinityMask = RED_FLAG64( 4 ) | RED_FLAG64( 5 );
			break;
#ifdef RED_CONSOLE_CORE_7_SUPPORT
		case 5:
			affinityMask = RED_FLAG64( 6 );
			break;
#endif 
		default:
			RED_FATAL( "Out of Bound Thread Index" );
			break;
		}
#elif defined( RED_PLATFORM_DURANGO )

		red::TAffinityMask affinityMask = 0;

		// This is just to allow us to dynamically assert based on the #define
	#ifdef USE_RESOURCE_THROTTLER_THREADS
		const bool usingResourceThrottlerThreads = true;
	#else
		const bool usingResourceThrottlerThreads = false ;
	#endif

		switch (threadIndex)
		{
		case 0:
			affinityMask = RED_FLAG64( 0 ) | RED_FLAG64( 1 );
			break;
		case 1:
			affinityMask = RED_FLAG64( 2 ) | RED_FLAG64( 3 );
			break;
		case 2:
			affinityMask = RED_FLAG64( 2 ) | RED_FLAG64( 3 );
			break;
		case 3:
			affinityMask = RED_FLAG64( 4 ) | RED_FLAG64( 5 );
			break;
		case 4:
			affinityMask = RED_FLAG64( 4 ) | RED_FLAG64( 5 );
			break;
	#ifdef RED_CONSOLE_CORE_7_SUPPORT
		case 5:
			affinityMask = RED_FLAG64( 6 );
			break;
		case 6:
			{
				// Detect which console we are running on
				CONSOLE_TYPE consoleType = ::GetConsoleType();

				// If we are running on SERIES_X then we have an additional redDispatcher thread, so set the affinity for that
				if ( consoleType == CONSOLE_TYPE_XBOX_SERIES_X || consoleType == CONSOLE_TYPE_XBOX_SERIES_X_DEVKIT )
				{
					// Allow it to float a bit, just in case it starts, gets bumped by Audio and other threads are waiting on it
					affinityMask = RED_FLAG64( 5 ) | RED_FLAG64( 6 ); 
				}
				else
				{
					// Check we should be able to get here !
					RED_ASSERT( usingResourceThrottlerThreads );
					// Otherwise, this is an additional ResourceThrottler Thread, so set Affinity based on that
					affinityMask = 0x7F; // Resource Throttler : All cores, but at a background priority
				}
			}
			break;
		case 7:
			{
				// Check we should be able to get here !
				RED_ASSERT( usingResourceThrottlerThreads );
				affinityMask = 0x7F; // Resource Throttler : All cores, but at a background priority
			}
			break;
		case 8:
			{
				// Detect which console we are running on for the Assert
				CONSOLE_TYPE consoleType = ::GetConsoleType();

				// Check we should be able to get here !
				RED_ASSERT( usingResourceThrottlerThreads && ( consoleType == CONSOLE_TYPE_XBOX_SERIES_X || consoleType == CONSOLE_TYPE_XBOX_SERIES_X_DEVKIT ) );
				affinityMask = 0x7F; // Resource Throttler : All cores, but at a background priority
			}
			break;
	#endif
		default:
			RED_FATAL( "Out of Bound Thread Index" );
			break;
		}
#else

	#error Unsupported platform!

#endif
		return affinityMask;
	}

	static red::TAffinityMask GetMainThreadAffinityMask()
	{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX )
		const red::TAffinityMask affinityMask{ 0 };
#elif defined( RED_PLATFORM_CONSOLE )
		const red::TAffinityMask affinityMask = RED_FLAG64( 0 ) | RED_FLAG64( 1 );
#else
#error Unsupported platform!
# endif
		return affinityMask;
	}
}

Dispatcher::Dispatcher( const InitParam& setup )
	: m_setup( setup )
	, m_priorityMap()
	, m_jobScopeAllocator( nullptr )
	, m_isFlushingCounter( false )
{
	InitParam::SetCrashData(setup);

	RED_FATAL_ASSERT( m_setup.maxThreads > 0 && m_setup.maxThreads <= 64 );
	Init();
}

Dispatcher::~Dispatcher()
{
	Shutdown();
}

void Dispatcher::Init()
{
	if ( c_useJobDebugger && m_setup.useJobDebugger )
	{
		m_debugger = red::CreateUniquePtr< prv::Debugger >();
	}

	InitJobQueue( m_setup );

	const Uint32 numThreads = helper::GetNumDispatcherThreads( m_setup );
	dd::numDispatcherThreads.Set( numThreads + numAdditionalResourceThrottlerThreads );
	dd::maxHWConcurrency.Set( red::GetMaxHardwareConcurrency() );
	m_dispatcherThreads.Reserve( numThreads + numAdditionalResourceThrottlerThreads );

	RED_FATAL_ASSERT( ::SIsMainThread() );
	red::SetCurrentThreadAffinity( helper::GetMainThreadAffinityMask() );
	red::SetCurrentThreadName( "GameThread" ); //#tbd: only because "MainThread" on Xbox is already taken by default

	m_jobScopeAllocator = &job::PoolJobScope::GetAllocator();
	RED_FATAL_ASSERT( m_jobScopeAllocator, "Job scope allocator does not exist" );
#if !defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
	m_jobScopeAllocator->RegisterCurrentThread();
#endif

	// Tad: Add additional ResourceThrottler job threads
	for ( Uint32 i = 0; i < numThreads + numAdditionalResourceThrottlerThreads; ++i )
	{
		const Uint32 dispatcherThreadIndex = i + 1; // 0 index reserved for the main thread
		char threadName[ 64 ];
		if ( i < numThreads )
		{
			red::SNPrintFUnsafe( threadName, RED_ARRAY_COUNT_U32( threadName ), "redDispatcher%u", dispatcherThreadIndex );
		}
#ifdef USE_RESOURCE_THROTTLER_THREADS
		else
		{
			red::SNPrintFUnsafe( threadName, RED_ARRAY_COUNT_U32( threadName ), "resourceThrottler%u", (i + 1) - numThreads );
		}
#endif
		DispatcherThreadSetup setup;
		setup.stackSizeKB = m_setup.workerThreadStackSizeKB;
		setup.affinityMask = helper::GetDispatcherThreadAffinityMask( i );
		setup.dispatcherThreadIndex = dispatcherThreadIndex;
		m_dispatcherThreads.EmplaceBack( red::CreateUniquePtr< DispatcherThread >( threadName, *this, setup ) );
	}

	RED_FATAL_ASSERT( !m_dispatcherThreads.Empty() );

	for ( auto& thread : m_dispatcherThreads )
	{
		thread->InitThread();
	}

#ifdef USE_RESOURCE_THROTTLER_THREADS
	for ( Uint32 rtThreads = m_dispatcherThreads.Size() - 1; rtThreads >= ( m_dispatcherThreads.Size() - numAdditionalResourceThrottlerThreads ); rtThreads-- )
	{
		// Set the resourceThrottler Threads to low priority
		m_dispatcherThreads[rtThreads]->SetPriority( red::TP_Lowest );
	
#if defined(RED_PLATFORM_DURANGO)
		// Setting this flag means that the threads never get a priority boost, which is what we want as
		// these are designed to be background threads and should never interrupt higher priority threads
		m_dispatcherThreads[rtThreads]->DisablePriorityBoost( true );
#endif
	}
#endif // USE_RESOURCE_THROTTLER_THREADS

	// Apparently needed in unit tests to give the dispatcher threads time to actually finish initializing!
	// Or else we take too long doing this test, because we're using maybe one or two threads and the system doesn't
	// finish initializing the threads in time.
	red::Timer initTimer;
	for ( ;; )
	{
		Uint32 numThreadsReady = 0;
		for ( auto& thread : m_dispatcherThreads )
		{
			if ( thread->IsReady() )
			{
				numThreadsReady += 1;
			}
		}
		if ( numThreadsReady == m_dispatcherThreads.Size() )
		{
			break;
		}

		red::SleepOnCurrentThread( 100 );
		RED_LOG_WARNING( "Waiting for DispatcherThreads: %u/%u ready...", numThreadsReady, m_dispatcherThreads.Size() );
		if ( initTimer.GetSeconds() > 1. )
		{
			RED_LOG_WARNING( "Skipped waiting for DispatcherThreads: %u/%u ready...", numThreadsReady, m_dispatcherThreads.Size() );
			break;
		}
	}
}

void Dispatcher::InitJobQueue( const InitParam& setup )
{
	RED_FATAL_ASSERT( setup.maxLatentJobs > 0 );
	RED_FATAL_ASSERT( setup.maxCriticalPathJobs > 0 );
	RED_FATAL_ASSERT( setup.maxImmediateJobs > 0 );

	DispatcherQueueSetup queueSetup;
	queueSetup.numLowPriorityJobs = m_setup.maxLatentJobs;
	queueSetup.numNormalPriorityJobs = m_setup.maxCriticalPathJobs;
	queueSetup.numHighPriorityJobs = m_setup.maxImmediateJobs;
#ifdef RED_CONSOLE_CORE_7_SUPPORT
	RED_FATAL_ASSERT( setup.maxCore7Jobs > 0 );
	queueSetup.numCore7Jobs = m_setup.maxCore7Jobs;
#endif

	if ( setup.allJobsCriticalPath )
	{
		queueSetup.numLowPriorityJobs = m_setup.maxCriticalPathJobs;
		std::fill( std::begin( m_priorityMap ), std::end( m_priorityMap ), Priority::CriticalPath );
	}
	else
	{
		static_assert( RED_ARRAY_COUNT_U32( m_priorityMap ) == static_cast< Uint32 >( Priority::COUNT ), "" );
		for ( Uint32 i = 0; i < RED_ARRAY_COUNT_U32( m_priorityMap ); ++i )
		{
			m_priorityMap[ i ] = static_cast<Priority>( i );
		}
	}

	m_dispatcherQueue.Initialize( queueSetup );
}

// Note: Shutdown() can be called multiple times, since it will have no further effect
// subsequent times.
void Dispatcher::Shutdown()
{
	m_isExitRequested.SetValue( true );
    m_dispatcherQueue.Shutdown( numAdditionalResourceThrottlerThreads );

	auto* lowPriCounter = InitJobCounter( "shutdownLowPri", Priority::Latent, nullptr );
	auto* renderPriCounter = InitJobCounter( "shutdownLowPri", Priority::RenderPath, nullptr );
	auto* normalPriCounter = InitJobCounter( "shutdownNormalPri", Priority::CriticalPath, nullptr );
	auto* highPriCounter = InitJobCounter( "shutdownHighPri", Priority::Immediate, nullptr );

	const JobDecl nullJob;
	for ( int i : m_dispatcherThreads.Indices() )
	{
		QueueJobAndSignal( nullJob, lowPriCounter, nullptr );
		QueueJobAndSignal( nullJob, renderPriCounter, nullptr );
		QueueJobAndSignal( nullJob, normalPriCounter, nullptr );
		QueueJobAndSignal( nullJob, highPriCounter, nullptr );
	}

	for ( auto& thread : m_dispatcherThreads )
	{
		thread->JoinThread();
	}

	ReleaseJobCounterInternal( lowPriCounter );
	ReleaseJobCounterInternal( renderPriCounter );
	ReleaseJobCounterInternal( normalPriCounter );
	ReleaseJobCounterInternal( highPriCounter );
	m_dispatcherThreads.Clear();
}

void Dispatcher::AddRefJobCounterInternal(CounterEntry* entry)
{
	RED_FATAL_ASSERT(entry);
	entry->refCountMask.AddRef();
}

void Dispatcher::ReleaseJobCounterInternal( CounterEntry* entry )
{
	RED_FATAL_ASSERT( entry );
	const auto result = entry->refCountMask.Release();
	if ( result.isZero )
	{
		DispatcherHelper::FreeEntry( entry );
	}
}

namespace helper
{
	static const StackTraceCacheEntryPath* GetDebugStackTraces( const JobQueueEntry& entry )
	{
		return nullptr; // TO REDO
	}

	static const char* debugHelperMessage[] = { "Run with '-jobDebugger' on the commandline to get stacktraces" };

	static void InitRunContext( const JobQueueEntry& entry, const Uint32 dispatcherThreadIndex, ScheduleParam param, job::Counter& continuationCounter, RunContext& outRunContext )
	{
		const char* debugName = "<Unknown>";
		if ( entry.jobDecl.instrumentationObject && entry.jobDecl.instrumentationObject->m_name )
		{
			debugName = entry.jobDecl.instrumentationObject->m_name;
		}
		outRunContext.debugName = debugName;

		if ( GetDebugStackTraces( entry ) )
		{
			outRunContext.debugStackTraces = GetDebugStackTraces( entry )->debugStringView;
		}
		else
		{
			if ( c_useJobDebugger )
			{
				outRunContext.debugStackTraces = debugHelperMessage;
			}
		}

		outRunContext.dispatcherThreadIndex = dispatcherThreadIndex;

		//FIXME:
		outRunContext.continuationContext.counter = &continuationCounter;
		outRunContext.continuationContext.instrumentationObject = entry.jobDecl.instrumentationObject;
		outRunContext.continuationContext.param = param;
	}

	static void VerifyJobDecl( const JobDecl& jobDecl )
	{
		RED_FATAL_ASSERT( jobDecl.jobFunc );

#ifdef USE_PROFILER
		auto* const instrumentationObject = jobDecl.instrumentationObject;
		RED_FATAL_ASSERT( instrumentationObject && instrumentationObject->m_name && instrumentationObject->m_name[ 0 ], "Must provide a proper instrumentation object" );
#endif
	}

	struct ScopedDebugFunction : red::NonCopyable
	{
		explicit ScopedDebugFunction( Debugger* debugger, const JobQueueEntry& entry )
			: m_debugger( debugger )
			, m_entry( entry )
		{
			if ( c_useJobDebugger && m_debugger )
			{
				auto* stackTrace = GetDebugStackTraces( entry );
//				m_debugger->BeginJobFunction( m_entry.jobDecl, stackTrace );
			}
		}

		~ScopedDebugFunction()
		{
			if ( c_useJobDebugger && m_debugger )
			{
	//			m_debugger->EndJobFunction( m_entry.jobDecl );			
			}
		}

		Debugger* m_debugger;
		const JobQueueEntry& m_entry;
	};

	static void RunJobFunctionWithInstrumentation( const JobQueueEntry& entry, const RunContext& runContext )
	{
		PC_SCOPE_INST_OBJ( *entry.jobDecl.instrumentationObject, entry.jobDecl.instrumentationObject->m_name );
		entry.jobDecl.jobFunc( entry.jobDecl.jobData, runContext );
	}
}

/*
namespace helper
{
	// #tbd: should also ban in IO callbacks, but this should catch most real frame allocator misuse
	// #tbd: should also ban if have no wait counter and not a continuation... 
	// ... which could be tricker to do correctly: the accum counter could get flushed so it's safe.
	// For the continuation case, would need to know if the job was pushed into the queue after waiting for another non-latent
	// counter.
	// And "immediate" jobs might also be non-frame sync'd.
	struct ScopedBanFrameAllocator : red::NonCopyable
	{
		explicit ScopedBanFrameAllocator( const JobQueueEntry& entry )
			: m_isBanned( false )
		{
			Init( entry );
		}

		~ScopedBanFrameAllocator()
		{
			if ( m_isBanned )
			{
				red::memory::DebugBanFrameAllocator( false );
			}
		}

	private:
		void Init( const JobQueueEntry& entry )
		{
			if ( red::memory::c_checkDebugBanFrameAllocator )
			{
				if ( !entry.accumulateCounterEntry || entry.accumulateCounterEntry->GetPriority() == Priority::Latent )
				{
					if ( (entry.jobDecl.debugFlags & static_cast<Uint8>( JobDebugFlags::AllowFrameAllocator ) ) == 0 )
					{
						red::memory::DebugBanFrameAllocator( true );
						m_isBanned = true;
					}
				}
			}
		}

		Bool m_isBanned;
	};
}
*/

void Dispatcher::DoRunJobQueueEntry( const JobQueueEntry& entry, const Uint32 dispatcherThreadIndex, Priority priority, prv::JobScopeMemoryAllocator& jobScopeAllocator, TLocalQueue* optLocalQueue )
{
	RunContext runContext;
	
	// FIXME:
	//////////////////////////////////////////////////////////////////////////
	//helper::ScopedBanFrameAllocator scopedBanFrameAllocator{ entry };
	
	RED_FATAL_ASSERT(entry.accumulateCounterEntry);
	job::Counter continuationCounter{ *entry.accumulateCounterEntry };
	helper::InitRunContext( entry, dispatcherThreadIndex, { priority, Affinity::All, continuationCounter.Internal_GetIOPriority() }, continuationCounter, runContext );
	helper::RunJobFunctionWithInstrumentation( entry, runContext );
	
	if ( entry.accumulateCounterEntry )
	{
		DecrementCounterEntryInternal( entry.accumulateCounterEntry, optLocalQueue );
	}

#if !defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
	jobScopeAllocator.Reset();
#endif
}

Bool Dispatcher::TryPopJobQueueEntry( JobQueueEntry& outEntry, Priority& outPriority )
{
	return m_dispatcherQueue.TryPop( outEntry, outPriority );
}

void Dispatcher::PopJobQueueEntry( JobQueueEntry& outEntry, Priority& outPriority )
{
	m_dispatcherQueue.Pop( outEntry, outPriority );
}

void Dispatcher::PopJobQueueEntry( JobQueueEntry& outEntry, Priority& outPriority, Affinity affinity )
{
	m_dispatcherQueue.Pop( outEntry, outPriority, affinity );
}

Priority Dispatcher::MapPriority( Priority priority ) const
{
	return m_priorityMap[ static_cast<Uint32>( priority ) ];
}

CounterEntry* Dispatcher::InitJobCounter( const char* debugName, ScheduleParam param, const void* debugUserData )
{
#ifdef USE_RESOURCE_THROTTLER_THREADS
	RED_FATAL_ASSERT( param.affinity == Affinity::All || ( (param.affinity == Affinity::ConsoleCore7 || param.affinity == Affinity::ResourceThrottler) && param.priority == Priority::Latent ), "Invalid Priority & Affinity combination." );
#else
	RED_FATAL_ASSERT( param.affinity == Affinity::All || (param.affinity == Affinity::ConsoleCore7 && param.priority == Priority::Latent ), "Invalid Priority & Affinity combination." );
#endif
	auto mappedPriority = MapPriority( param.priority );
	return DispatcherHelper::AllocCounterEntry( debugName, { mappedPriority, param.affinity, param.ioPriority }, debugUserData );
}

CompletionDeferral Dispatcher::CreateDeferral( const char* debugName, const void* debugUserData, CounterEntry& counterEntry )
{
	const Uint32 numFakeJobs = 1;
	const auto result = counterEntry.counterValue.ExchangeAdd( numFakeJobs );
	if ( result.wasZero )
	{
		counterEntry.refCountMask.AddRef();
	}
	return CompletionDeferral( debugName, debugUserData, counterEntry );
}

Bool Dispatcher::IsZero_Snapshot( const CounterEntry* counterEntry )
{
	RED_FATAL_ASSERT( counterEntry );
	return counterEntry->counterValue.IsZero_Snapshot();
}

Debugger* Dispatcher::GetDebugger() const
{
	return m_debugger.Get();
}

void Dispatcher::AnalyzeCounter(const CounterEntry& counter)
{
	if ( !c_useJobDebugger )
	{
		RED_LOG_ERROR( "Job debugger not compiled. Recompile with c_useJobDebugger true" );
		return;
	}

	if ( !m_debugger )
	{
		RED_LOG_ERROR( "Job debugger not enabled. Run with '-jobDebugger'" );
		return;
	}

	m_debugger->AnalyzeCounter(counter);
}

namespace helper
{
	static void CopyJobToWaitingListEntry( const JobDecl& job, CounterEntry* accumulateCounterEntry, const StackTraceCacheEntryPath* debugTrace, WaitingListEntry* entry )
	{
		RED_FATAL_ASSERT( entry );
		entry->job = job;
		entry->accumulateCounterEntry = accumulateCounterEntry;
		entry->debugTrace = debugTrace;
	}
}

Bool Dispatcher::TryPutOnWaitingList( const JobDecl& job, const CounterEntry& waitForZeroCounterEntry, CounterEntry* accumulateCounterEntry, const StackTraceCacheEntryPath* debugTrace )
{
	// check if we should try to lock the waitForZeroCounter to avoid writing/allocating waint list entries unnecesarily.
	// If this counter is bumped back above zero asynchronously during this function call, then it's a race condition anyway
	// whether this job would have to wait or not
	if ( waitForZeroCounterEntry.counterValue.IsZero_Snapshot() )
	{
		return false;
	}

	// #tbd: should be quick anyway, but allocate/free without holding lock; keep the upcoming locked section as quick as possible
	// to avoid tying up other threads needlessly and potentially have them start backing off/sleeping on the spinlock
	WaitingListEntry* const entry = helper::AllocEntry< WaitingListEntry >();
	helper::CopyJobToWaitingListEntry( job, accumulateCounterEntry, debugTrace, entry );
		
	Bool isOnList = false;

	// locked section
	{
		auto& lock = waitForZeroCounterEntry.waitingListLock;
		RED_SCOPE_LOCK( lock );

		// Must check value again under lock, or might never get run
		if ( !waitForZeroCounterEntry.counterValue.IsZero_Snapshot() )
		{
			entry->next = waitForZeroCounterEntry.waitingJobsHead;
			waitForZeroCounterEntry.waitingJobsHead = entry;
			isOnList = true;
		}
	}

	if ( !isOnList )
	{
		helper::FreeEntry( entry );
		return false;
	}

	return true;
}

void Dispatcher::DecrementCounterEntryInternal( CounterEntry* counterEntry, TLocalQueue* optLocalQueue )
{
	RED_FATAL_ASSERT( counterEntry );
	
	if ( !counterEntry->counterValue.Decrement().isZero )
	{
		return;
	}

	// We hit zero counter value, time to release any waiting entries
	auto* const entriesHead = DispatcherHelper::FlushWaitingList( *counterEntry );
	auto* entryIter = entriesHead;

	// See RunJob() for AddRef() logic.
	if ( counterEntry->refCountMask.Release().isZero )
	{
		// Free now before resuming any suspended jobs. Make it easier to hit any issues that might arise from accessing the counter.
		// If any dependencies or suspended jobs were still going to use the counter again, it would have been kept alive by an AddRef().
		DispatcherHelper::FreeEntry( counterEntry );
	}

	// Queue up the waiting jobs
	if ( entryIter )
	{
		for ( ;; )
		{
			QueueJobAndSignal( entryIter->job, entryIter->accumulateCounterEntry, optLocalQueue );
			RED_FATAL_ASSERT( entryIter != entryIter->next );
			if ( !entryIter->next )
			{
				break;
			}
			entryIter = entryIter->next;
		}
	}

	helper::FreeEntry( entriesHead );
}

void Dispatcher::QueueJobAndSignal( const JobDecl& job, CounterEntry* accumulateCounterEntry, TLocalQueue* optLocalQueue )
{
	Priority priority = Priority::RenderPath;
	Affinity affinity = Affinity::All;

	// all physics job will have normal priority so they are not blocked by loading jobs
	if ( job.hint != JobHint::PhysX )
	{
		priority = MapPriority( Priority::Latent );

		if ( accumulateCounterEntry )
		{
			priority = accumulateCounterEntry->param.priority;
			affinity = accumulateCounterEntry->param.affinity;
		}

		// If no accumulate counter, then assume low priority because no dependencies can sync on it anyway
		// Always possible that could be sync'd on in some ad-hoc way though.
		// #fixme: priority and path are separate now.
#ifndef NO_EDITOR
		if ( job.hint == JobHint::Large && m_setup.allJobsCriticalPath )
		{
			priority = Priority::Latent;
		}
#endif
	}

	JobQueueEntry entry;
	entry.jobDecl = job;
	entry.accumulateCounterEntry = accumulateCounterEntry;

	const Bool useLocalQueue = entry.jobDecl.hint == JobHint::Trivial && optLocalQueue && optLocalQueue->Size() < c_localQueueDefaultCapacity;
	if (false)//useLocalQueue)
	{
		optLocalQueue->PushBack(std::make_pair(entry, priority));
	}
	else
	{
		for (;;)
		{
			if (m_dispatcherQueue.TryPush(entry, priority, affinity))
			{
				break;
			}
			else if (optLocalQueue)
			{
				// Emergency push back to try and avoid deadlocks
				optLocalQueue->PushBack(std::make_pair(entry, priority));
				break;
			}
		}
	}
}

void Dispatcher::InitWithEmptyJob( JobDecl& outJob )
{
	outJob.jobFunc = &helper::EmptyJob;
	outJob.hint = JobHint::Trivial;
	outJob.instrumentationObject = &helper::s_emptyJobInstrumentationObject;
}

Uint32 Dispatcher::GetNumDispatcherThreads(Bool includeCore7) const
{
#ifdef RED_CONSOLE_CORE_7_SUPPORT
	if (includeCore7)
	{
		// Minus one for the 7th Core Latent job queue which isn't used for Parallel Jobs
		return m_dispatcherThreads.Size() - 1 - numAdditionalResourceThrottlerThreads;
	}
#endif

	return m_dispatcherThreads.Size() - numAdditionalResourceThrottlerThreads;
}

void Dispatcher::RunJob( const JobDecl& job, const CounterEntry* waitForZeroCounterEntry, CounterEntry* accumulateCounterEntry )
{
	helper::VerifyJobDecl( job );
	RED_FATAL_ASSERT( ( waitForZeroCounterEntry != accumulateCounterEntry ) || !waitForZeroCounterEntry, "This will create a deadlock - how can jobs run after themselves?" );

	// Bump value here and NOT in QueueJobsOrWait. If we suspend a fiber and queue a fake job to resume it later, we don't want to bump its accumulateCounterEntry again
	if ( accumulateCounterEntry )
	{
		// #fixme: if a continuation counter and useContinuationPriorityBoost true, then no real priority inversion exists since our wait counter is already zero
		// but need to know at this level
		if ( c_warnOnPriorityInversion && waitForZeroCounterEntry && waitForZeroCounterEntry->param.priority < accumulateCounterEntry->param.priority )
		{
		RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Jobs, "Priority inversion for job '%hs'. priority %u < priority %u waitForZeroCounterEntry:\n%hs\naccumulateCounterEntry:\n%hs",
				( job.instrumentationObject ? job.instrumentationObject->m_name : "<No instrumentation object>" ),
				(Uint32)waitForZeroCounterEntry->param.priority, accumulateCounterEntry->param.priority,
				waitForZeroCounterEntry->debugName, accumulateCounterEntry->debugName );			
		}

		const Int32 numJobs = 1; // NOTE: if go back to array of jobs, then DO NOT AddRefForDispatcher() if numJobs == 0
		const auto result = accumulateCounterEntry->counterValue.ExchangeAdd( numJobs );

		// Increment for every expected future "return to zero" - when the dispatcher will see if it can free the counter. But we haven't actually run these jobs yet, so
		// no race condition having to AddRef() next.
		//
		// We must add to the flag in order to set it, and subtract to try and clear it. This avoids a race where oldValue is zero,
		// but zero because another thread just finished decrementing counterValue and wants to clear c_inUseDispatcherMask from refCountMask.
		if ( result.wasZero )
		{
			accumulateCounterEntry->refCountMask.AddRef();
		}
	}

	QueueJobOrWait( job, waitForZeroCounterEntry, accumulateCounterEntry );
}

namespace helper
{
	static void ParallelForFunc( void* jobData, const RunContext& runContext )
	{
		auto* entry = static_cast<ParallelForJobEntry*>( jobData );
		RED_FATAL_ASSERT( entry );
		auto* sharedCounter = entry->sharedCounterEntry;
		RED_FATAL_ASSERT( sharedCounter );

		auto* const sharedData = entry->sharedData;
		void* const elements = entry->elements;
		const Uint32 numElements = entry->numElements;
		const Uint32 realTeamSize = entry->teamSize;
		const Uint32 teamSize = entry->maxBatchSize == 0 ? entry->teamSize : math::Max( numElements / entry->maxBatchSize, 1u );
		const Uint32 batchSize = ( numElements + teamSize - 1 ) / teamSize;

		// Not used in the epilogue, since should be agnostic which job finished
		RunContext parallelForRunContext = runContext;
		parallelForRunContext.parallelForTeamIndex = entry->teamIndex;
		for ( ;; )
		{
			const auto index = sharedCounter->counter.Increment() - 1;
			if ( index < teamSize )
			{
				const Uint32 elementStartInd = index * batchSize;
				const Uint32 elementEndIndex = Min( elementStartInd + batchSize, numElements );
				entry->jobFunc( sharedData, elements, elementStartInd, elementEndIndex, parallelForRunContext );
			}
			else
			{
				// Can release the sharedCounter after every job in the team has incremented the counter
				// and seen that it's been exceeded, so won't touch the counter again
				// One thread will see index as "numElements" and stop, then "teamSize - 1" more threads will increment it past that value, then stop too.
				const Bool mustReleaseCounterNow = ( index == teamSize + realTeamSize - 1 );
				if ( mustReleaseCounterNow )
				{
					helper::FreeEntry( sharedCounter );

					// #tbd: could even copy then release entry here before calling function to free the entry even sooner
					if ( entry->epilogueFunc )
					{
						entry->epilogueFunc( sharedData, elements, numElements, runContext );
					}
				}

				break;
			}
		}

		helper::FreeEntry( entry );
	}

	static void ParallelForFuncEpilogueOnly( void* jobData, const RunContext& runContext )
	{
		auto* entry = static_cast<ParallelForJobEntry*>( jobData );
		RED_FATAL_ASSERT( entry );
		RED_FATAL_ASSERT( !entry->sharedCounterEntry );
		RED_FATAL_ASSERT( entry->epilogueFunc );

		auto* const sharedData = entry->sharedData;
		void* const elements = entry->elements;
		const Uint32 numElements = entry->numElements;
		entry->epilogueFunc( sharedData, elements, numElements, runContext );

		helper::FreeEntry( entry );
	}

	static void ParallelForFuncSingleTeam( void* jobData, const RunContext& runContext )
	{
		auto* entry = static_cast<ParallelForJobEntry*>( jobData );
		RED_FATAL_ASSERT( entry );
		RED_FATAL_ASSERT( !entry->sharedCounterEntry );

		auto* const sharedData = entry->sharedData;
		void* const elements = entry->elements;
		const Uint32 numElements = entry->numElements;
		// Not used in the epilogue, since should be agnostic which job finished
		RunContext parallelForRunContext = runContext;
		parallelForRunContext.parallelForTeamIndex = entry->teamIndex;
		entry->jobFunc( sharedData, elements, 0, numElements, parallelForRunContext );

		// #tbd: could even copy then release entry here before calling function to free the entry even sooner
		if ( entry->epilogueFunc )
		{
			entry->epilogueFunc( sharedData, elements, numElements, runContext );
		}

		helper::FreeEntry( entry );
	}

	static Uint32 CalcParallelForTeamSize( Uint32 numElements, Uint32 numElementsPerBatch, Uint32 numWorkers )
	{
		// If each paralell-for job will exclusively process a chunk, don't create more jobs than there are chunks
		// Otherwise it's just extra thread contention
	 	const Uint32 numBatches = ( numElements + numElementsPerBatch - 1 ) / numElementsPerBatch;
		return std::min( numBatches, numWorkers );	
	}
}

namespace helper
{
#ifdef MUST_PUMP_WIN32_MESSAGES_FOR_DXGI
	static void PumpWindowsMessagesForDXGI()
	{
		// Not bothering to filter as experimentally makes no difference and supported by technical reference. Quit message is posted, so won't miss processing 
		// it here. Doubtful PM_NOYIELD makes any practical difference here if some other process is waiting for the launcher to be initialized with WaitForInputIdle,
		// but using it as we're not processing all messages in the main loop.

		// See "GetMessage and PeekMessage Internals" by Bob Gunderson.
		/*
		Applications that do not desire window-handle filtering may pass a NULL value to GetMessageand PeekMessage in the hwnd parameter.
		Similarly, passing NULL values in bothuMsgFilterMin and uMsgFilterMax parameters disables message-range filtering.

		It is important to realize that only mouse and keyboard hardware messages, posted messages, WM_PAINT messages, and timer messages can be filtered.
		Most messages a window procedure receives are sent directly to the window using SendMessage.
		*/

		// mainthread check mainly for the sake of not making GPumpingMessagesOutsideMainLoop TLS, since PeekMessage would be a no-op anyway
		if ( ::SIsMainThread() )
		{
			MSG msg;
			PeekMessage( &msg, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE | PM_NOYIELD );
		}
	}
#endif

#ifdef MUST_PUMP_WIN32_MESSAGES_FOR_DXGI
	static void PumpWindowsMessagesForDXGIReminderJob( void* jobData, const RunContext& )
	{
		auto* const flag = static_cast< red::Atomic< Bool >* >( jobData );
		flag->SetValue( true );
	}
#endif

	class MessagePumper : red::NonCopyable
	{
	public:
		MessagePumper( Dispatcher& dispatcher, red::Atomic< Bool >& shouldPumpMessagesForDXGI )
			: m_dispatcher( dispatcher )
			, m_nextUpdateTick( 0 )
			, m_tickFrequency( 0 )
			, m_shouldPumpMessagesForDXGI( shouldPumpMessagesForDXGI )
			, m_sentPumpMessagesReminder( false )
		{
			RED_TOUCH( m_dispatcher );
			RED_TOUCH2( m_nextUpdateTick, m_tickFrequency );
			RED_TOUCH3( m_shouldPumpMessagesForDXGI, m_sentPumpMessagesReminder, m_isMainThread );

#ifdef MUST_PUMP_WIN32_MESSAGES_FOR_DXGI
			m_isMainThread = ::SIsMainThread();

			// Reset any previously abandoned value
			if ( m_isMainThread )
			{
				m_shouldPumpMessagesForDXGI.SetValue( false );
			}
			red::Clock::GetInstance().GetTimer().GetFrequency( m_tickFrequency );
#endif
		}

		void Update()
		{
#ifdef MUST_PUMP_WIN32_MESSAGES_FOR_DXGI
			if ( !m_isMainThread )
				return;

#ifdef VG_INTERACTIVE_DXGI_WAIT
			// Interactive native windows need prompt sent-message service while workers present.
			// Do not consume queued input or dispatch another application update here.
			const Uint64 tickNow = red::Timer::GetTicks();
			if ( tickNow >= m_nextUpdateTick )
			{
				m_nextUpdateTick = tickNow + std::max< Uint64 >( 1, m_tickFrequency / 1000 );
				PumpWindowsMessagesForDXGI();
			}
#else
			// If we couldn't pop any more jobs, maybe the other threads are hung in Win32 API calls where messages are posted the HWND
			// So we need to pump messages. We queue this job because where this occurs on PC we should be able to tolerate a momentary FPS hitch
			// where the main thread has to drain the job queue to get to this: e.g., going from windowed to fullscreen mode etc.
			// And then we don't pump messages too often if it's simply the case that other threads are beating us to job entries.
			// If we kick the reminder job and finish, it doesn't matter if we abandon it. We reset the flag before entrering this loop.
			const Uint64 tickNow = red::Timer::GetTicks();
			if ( !m_sentPumpMessagesReminder && tickNow >= m_nextUpdateTick )
			{
				// One second until next pump
				m_nextUpdateTick = tickNow + m_tickFrequency;

				m_sentPumpMessagesReminder = true;
				JobDecl jobDecl;
				jobDecl.instrumentationObject = &s_instrumentationObject;
				jobDecl.jobFunc = &PumpWindowsMessagesForDXGIReminderJob;
				jobDecl.jobData = &m_shouldPumpMessagesForDXGI;
				jobDecl.hint = JobHint::Trivial;
				
				auto* dummyWaitCounter = m_dispatcher.InitJobCounter("PumpWindowsMessagesForDXGI", Priority::Latent, nullptr);
				auto* dummyAccumCounter = m_dispatcher.InitJobCounter("PumpWindowsMessagesForDXGI", Priority::Latent, nullptr);
				m_dispatcher.RunJob( jobDecl, dummyWaitCounter, dummyAccumCounter );

				m_dispatcher.ReleaseJobCounterInternal(dummyWaitCounter);
				m_dispatcher.ReleaseJobCounterInternal(dummyAccumCounter);
			}
			else if ( m_shouldPumpMessagesForDXGI.GetValue() )
			{
				m_sentPumpMessagesReminder = false;
				PumpWindowsMessagesForDXGI();
			}
#endif
#endif
		}

	private:
		static red::InstrumentationObject s_instrumentationObject;
		Dispatcher& m_dispatcher;
		Uint64 m_nextUpdateTick;
		Uint64 m_tickFrequency;
		red::Atomic< Bool >& m_shouldPumpMessagesForDXGI;
		Bool m_sentPumpMessagesReminder;
		Bool m_isMainThread;
	};

	red::InstrumentationObject MessagePumper::s_instrumentationObject{ "MessagePumper" };
}

Bool Dispatcher::FlushCounter( const CounterEntry* counterEntry, Priority processPriority, Int32 timeoutMillseconds, Bool processLarge )
{
	ALWAYSENABLED_RED_FATAL_ASSERT( ::SIsMainThread() && !g_tls_IsDispatcherThread, 
		"Can only FlushCounter() from the main thread "
		"BUT NOT from any job (regardless of it coincidentally or cleverly running on the main thread)" );

	RED_FATAL_ASSERT( counterEntry );

	red::ScopedFlag< Bool > scopedFlag { g_tls_IsDispatcherThread = true, false };

	// #tbd: if try to shutdown dispatcher while still has jobs waiting
	// generally shouldn't be supported, and shutdown should happen at a stable time
	// Can't infinitely nest waits or mitigation like stack too deep.
	RED_FATAL_ASSERT( !m_isFlushingCounter, "Reentrantly flushing counter!" );
	red::ScopedFlag< Bool > flushFlag( m_isFlushingCounter = true, false );

	red::ProfileTimer timeoutTimer;

	helper::MessagePumper pumper { *this, m_shouldPumpMessagesForDXGI };
	while ( !counterEntry->counterValue.IsZero_Snapshot() )
	{
		if ( timeoutMillseconds > -1 && timeoutTimer.GetDeltaMsec() >= timeoutMillseconds )
		{		
			if (c_useJobDebugger && m_debugger)
			{
				AnalyzeCounter(*counterEntry);
			}
			return false;
		}

		JobQueueEntry entry;
		Priority priority = Priority::RenderPath;
		if ( TryPopJobQueueEntry( entry, priority ) )
		{
			priority = MapPriority( priority );
			const Bool isTrivialJob = entry.jobDecl.hint == JobHint::Trivial;
			const Bool isLargeJob = ( entry.jobDecl.hint == JobHint::Large || entry.jobDecl.hint == JobHint::AudioEvent );
			const Bool canRunJob = ( processLarge || !isLargeJob );
			if ( entry.accumulateCounterEntry == counterEntry || ( priority >= processPriority && canRunJob ) || isTrivialJob )
			{
				// Main thread is excluded from dispatcher threads but at the beginning of this function there's RED_FATAL_ASSERT( ::SIsMainThread() );
				// Therefore 0 index is reserved for the Main Thread 
				DoRunJobQueueEntry( entry, 0, priority, *m_jobScopeAllocator, nullptr );
			}
			else
			{
				// Throw back onto the queue
				QueueJobAndSignal( entry.jobDecl, entry.accumulateCounterEntry, nullptr );
			}
		}
		else
		{
			// #tbd: if no more jobs to run and the dispatcher threads are finishing up the remaining jobs, we may pump messages a bit too frequently?
			pumper.Update();
		}
	}
	return true;
}


//#tbd: could assert that counterEntry value never increases while flushing.
//then could also stop trying to pop job entries once gone through the entire queue
Bool Dispatcher::FlushCounter( const CounterEntry* counterEntry, Bool processLatent, Int32 timeoutMillseconds )
{
	return FlushCounter( counterEntry, processLatent ? Priority::Latent : counterEntry->param.priority, timeoutMillseconds, true );
}

namespace helper
{
	static void InitParallelForEntry( prv::ParallelForJobEntry& entry, const JobDeclParallelFor& jobDeclParallelFor, void* resolvedSharedData, Uint32 teamSize, Uint32 teamIndex, ParallelForSharedCounterEntry* sharedCounterEntry )
	{
		entry.jobFunc = jobDeclParallelFor.jobFunc;
		entry.epilogueFunc = jobDeclParallelFor.epilogueFunc;
		entry.sharedData = resolvedSharedData;
		entry.elements = jobDeclParallelFor.elements;
		entry.sharedCounterEntry = sharedCounterEntry;
		entry.numElements = jobDeclParallelFor.numElements;
		entry.teamSize = teamSize;
		entry.teamIndex = teamIndex;
		entry.maxBatchSize = jobDeclParallelFor.maxBatchSize;
	}

	static void InitParallelForJobDecl( JobDecl& jobDecl, const JobDeclParallelFor& jobDeclParallelFor, prv::ParallelForJobEntry& entry, JobDecl::TJobFunc* parallelForFunc  )
	{
		jobDecl.jobFunc = parallelForFunc;
		jobDecl.jobData = &entry;
		jobDecl.instrumentationObject = jobDeclParallelFor.instrumentationObject;
		jobDecl.debugFlags = jobDeclParallelFor.debugFlags;
	}
}

void Dispatcher::RunParallelForJob( const JobDeclParallelFor& job, const CounterEntry* waitForZeroCounterEntry, CounterEntry* accumulateCounterEntry )
{
	RED_FATAL_ASSERT( job.jobFunc );

	Uint32 maxParallelForTeamSize = GetNumDispatcherThreads(false) + 1; // +1 for main thread as worker

#ifdef RED_CONSOLE_CORE_7_SUPPORT
	if (accumulateCounterEntry && accumulateCounterEntry->GetAffinity() == job::Affinity::ConsoleCore7)
	{
		// Only one core7, so no point splitting into further jobs
		maxParallelForTeamSize = 1;
	}
#endif

	const Uint32 teamSize = helper::CalcParallelForTeamSize( job.numElements, 1, maxParallelForTeamSize );
	RED_FATAL_ASSERT( teamSize > 0 || job.numElements == 0, "Sanity check failed: invalid zero teamSize calculation!" );

	ParallelForSharedCounterEntry* sharedCounterEntry = nullptr;
	JobDecl::TJobFunc* parallelForFunc = nullptr;
	Uint32 numJobs = 0;

	// Choose how to run the parallel-for function
	if ( teamSize == 0 )
	{
		if ( job.epilogueFunc )
		{
			// Always run epilogueFunc, regardless of whether any elements. Run it in a job too, to make sure it's in the proper order relative to other jobs.
			numJobs = 1;
			parallelForFunc = &helper::ParallelForFuncEpilogueOnly;
		}
	}
	else
	{
		numJobs = teamSize;
		if ( teamSize == 1 )
		{
			parallelForFunc = &helper::ParallelForFuncSingleTeam;
		}
		else
		{
			parallelForFunc = &helper::ParallelForFunc;
			sharedCounterEntry = helper::AllocEntry< ParallelForSharedCounterEntry>();
			RED_FATAL_ASSERT( sharedCounterEntry );
			sharedCounterEntry->counter.SetValue( 0 ); // can be atomic relaxed here
		}
	}
	
	RED_FATAL_ASSERT( parallelForFunc || numJobs == 0, "Did not select a parallel-for function to run!" );

	void* resolvedSharedData = job.sharedData;
	if ( job.initSharedDataCallback )
	{
		resolvedSharedData = job.initSharedDataCallback( teamSize, job.sharedData );
	}

	// Init the jobDecl for each parallel for worker
	for ( Uint32 i = 0; i < numJobs; ++i )
	{
		auto* parallelForEntry = helper::AllocEntry<ParallelForJobEntry>();
		RED_FATAL_ASSERT( parallelForEntry );
		helper::InitParallelForEntry( *parallelForEntry, job, resolvedSharedData, teamSize, i, sharedCounterEntry );

		JobDecl jobDecl;
		helper::InitParallelForJobDecl( jobDecl, job, *parallelForEntry, parallelForFunc );
		RunJob( jobDecl, waitForZeroCounterEntry, accumulateCounterEntry );
	}

	// Avoid any surprises breaking a dependency chain if the parallel-for was empty.
	// That is, always increment the counter even if nothing to do.
	if (numJobs == 0)
	{
		JobDecl jobDecl;
		InitWithEmptyJob(jobDecl);
		RunJob(jobDecl, waitForZeroCounterEntry, accumulateCounterEntry);
	}
}

void Dispatcher::QueueJobOrWait( const JobDecl& job, const CounterEntry* waitForZeroCounterEntry, CounterEntry* accumulateCounterEntry )
{
	const StackTraceCacheEntryPath* debugTrace = nullptr;

	if ( c_useJobDebugger && m_debugger )
	{
		debugTrace = m_debugger->TraceCall();
	}

	if ( waitForZeroCounterEntry )
	{
		if ( TryPutOnWaitingList( job, *waitForZeroCounterEntry, accumulateCounterEntry, debugTrace ) )
		{
			return;
		}
	}

	QueueJobAndSignal( job, accumulateCounterEntry, nullptr );
}

} } // prv/jobs

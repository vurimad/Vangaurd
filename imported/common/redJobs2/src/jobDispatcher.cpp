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

#if defined(VG_BUILD_PROFILE)
#include "../../redSystem/include/profileTimer.h"
#endif

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

static constexpr Uint32 c_inlineDependencyRelayBudget = 2;
static constexpr Uint32 c_inlineDependencyRelayDepthLimit = 32;
static constexpr Bool c_recruitForInlinedDependencyRelay = true;

#if defined(VG_BUILD_PROFILE)
	struct DependencyProbeRenderNodeEvent
	{
		const void* parentDebugUserData{ nullptr };
		const void* childDebugUserData{ nullptr };
		Uint64 relayPublicationKey{ 0 };
		Uint64 childPublicationKey{ 0 };
		Uint64 parentOriginTicks{ 0 };
		Uint64 relayQueuedTicks{ 0 };
		Uint64 relayPoppedTicks{ 0 };
		Uint64 relayConsumerAcquireEndTicks{ 0 };
		Uint64 relayStartTicks{ 0 };
		Uint64 relayBodyEndTicks{ 0 };
		Uint64 childQueuedTicks{ 0 };
		Uint64 childPoppedTicks{ 0 };
		Uint64 childConsumerAcquireEndTicks{ 0 };
		Uint64 childStartTicks{ 0 };
		Uint64 childInvokeTicks{ 0 };
		Uint64 childBodyEndTicks{ 0 };
		Uint32 producerRunnableDepthAtRelayDecision{ UINT32_MAX };
		Uint32 relayQueueDepthAtEnqueue{ 0 };
		Uint32 childQueueDepthAtEnqueue{ 0 };
		Uint32 relayDispatcherThreadIndex{ UINT32_MAX };
		Uint32 childDispatcherThreadIndex{ UINT32_MAX };
		Uint32 relayParentWaitingListOrdinal{ 0 };
		Uint32 childReleaseCascadePublicationOrdinal{ UINT32_MAX };
		Bool relayConsumerUsedKernelWait{ false };
		Bool childConsumerUsedKernelWait{ false };
	};

	struct DependencyProbePublicationEvent
	{
		Uint64 publicationKey{ 0 };
		Uint64 queuedTicks{ 0 };
		const void* parentDebugUserData{ nullptr };
		const void* childDebugUserData{ nullptr };
		Uint32 producerThreadIndex{ UINT32_MAX };
		Uint32 queueDepthAtEnqueue{ 0 };
		Uint32 producerRunnableDepthAtRelayDecision{ UINT32_MAX };
		Uint64 queuePushTicks{ 0 };
		Uint64 semaphoreReleaseTicks{ 0 };
		Uint32 numThreadsWoken{ 0 };
		Uint8 role{ 0 };
	};

thread_local Uint64 g_tls_dependencyProbeCompletedJobTicks = 0;
thread_local Uint64 g_tls_dependencyProbeRelayOriginTicks = 0;
thread_local Uint64 g_tls_dependencyProbeRelayBodyEndTicks = 0;
thread_local Uint64 g_tls_dependencyProbeRelayQueuedTicks = 0;
thread_local Uint64 g_tls_dependencyProbeRelayPoppedTicks = 0;
thread_local Uint64 g_tls_dependencyProbeRelayConsumerAcquireEndTicks = 0;
thread_local Uint64 g_tls_dependencyProbeRelayStartTicks = 0;
thread_local Uint64 g_tls_dependencyProbeParentBodyToCounterDecrementTicks = 0;
thread_local Uint64 g_tls_dependencyProbeParentCounterDecrementTicks = 0;
thread_local Uint64 g_tls_dependencyProbeParentWaitingListFlushTicks = 0;
thread_local Uint64 g_tls_dependencyProbeParentCounterRefReleaseTicks = 0;
thread_local Uint64 g_tls_dependencyProbeParentRefReleaseEndTicks = 0;
thread_local const void* g_tls_dependencyProbeParentDebugUserData = nullptr;
thread_local Uint32 g_tls_dependencyProbeParentWaitingListOrdinal = 0;
thread_local Uint32 g_tls_dependencyProbeReleaseCascadeDepth = 0;
thread_local Uint32 g_tls_dependencyProbeReleaseCascadeGlobalPublications = 0;
thread_local Uint32 g_tls_dependencyProbeRelayDecisionRunnableDepth = UINT32_MAX;
thread_local Uint32 g_tls_dependencyProbeRelayQueueDepthAtEnqueue = 0;
thread_local Uint32 g_tls_dependencyProbeRelayDispatcherThreadIndex = UINT32_MAX;
thread_local Uint32 g_tls_dependencyProbeRelayParentWaitingListOrdinal = 0;
thread_local Bool g_tls_dependencyProbeRelayConsumerUsedKernelWait = false;
thread_local const void* g_tls_dependencyProbeRelayParentDebugUserData = nullptr;
thread_local Uint64 g_tls_dependencyProbeRelayPublicationKey = 0;
thread_local Uint32 g_tls_dependencyProbePublicationOrdinal = 0;
thread_local Bool g_tls_dependencyProbeReleasingFromRelay = false;
thread_local Uint32 g_tls_dependencyProbeInlineRelayDepth = 0;
thread_local Uint32 g_tls_dependencyProbeInlineRelayCount = 0;
thread_local Priority g_tls_dependencyProbeReleasePriority = Priority::COUNT;
thread_local Bool g_tls_dependencyProbeSingleWaiterRelease = false;

	static constexpr Bool c_dependencyProbeBoostSingleWaiterRelayPriority = false;
#endif

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

	static void DependencyRelayJob( void*, const RunContext& )
	{
		// Intentionally empty. The distinct function identity lets Profile builds
		// measure dependency relay scheduling without changing dispatch behavior.
	}

	static red::InstrumentationObject s_dependencyRelayJobInstrumentationObject{ "DependencyRelayJob" };

#if defined(VG_BUILD_PROFILE)
	enum class DependencyProbeStage : Uint8
	{
		ParentReleaseToRelayQueue,
		RelayQueueToStart,
		RelayRun,
		RelayReleaseToChildQueue,
		ChildQueueToStart,
		ParentReleaseToChildStart,
		GlobalQueuePush,
		GlobalSemaphoreRelease,
		GlobalSemaphoreReleaseWake,
			GlobalSemaphoreReleaseNoWake,
			ParentBodyEndToCounterDecrement,
			ParentCounterDecrement,
			ParentWaitingListFlush,
			ParentCounterRefRelease,
			ParentRefReleaseToQueueCall,
			ParentRefReleaseToFirstQueueCall,
			ParentRefReleaseToLaterQueueCall,
			RelayQueueSetup,
		ConsumerAcquireFast,
		ConsumerAcquireKernel,
		ConsumerQueueScan,
		RelayQueueToPop,
		ConsumerPopToDispatcherEntry,
		DispatcherEntryToJobInvoke,
		RelayQueueToStartFastConsumer,
		RelayQueueToStartKernelConsumer,
		ChildQueueToStartFastConsumer,
		ChildQueueToStartKernelConsumer,
		ChildQueueToPopDepth0,
		ChildQueueToPopDepth1To3,
		ChildQueueToPopDepth4To7,
		ChildQueueToPopDepth8Plus,
		ChildDepth0KernelQueueToAcquireReturn,
		ChildDepth0FastQueueToAcquireReturn,
		ChildDepth0AcquireReturnToPop,
		ChildQueueToStartDepth0,
			ChildQueueToStartDepth1To3,
			ChildQueueToStartDepth4To7,
			ChildQueueToStartDepth8Plus,
			ChildQueueToStartCascadePublicationFirst,
			ChildQueueToStartCascadePublication2To4,
			ChildQueueToStartCascadePublication5To8,
			ChildQueueToStartCascadePublication9Plus,
			ChildQueueToStartProducerQueueEmpty,
			ChildQueueToStartProducerQueueOccupied,
			ParentReleaseToChildStartProducerQueueEmpty,
			ParentReleaseToChildStartProducerQueueOccupied,
			InlineRelayPropagationProducerQueueEmpty,
			InlineRelayPropagationProducerQueueOccupied,
			RelaySemaphoreReleaseWake,
			RelaySemaphoreReleaseNoWake,
			ChildSemaphoreReleaseWake,
			ChildSemaphoreReleaseNoWake,
			Count,
		};

		enum class DependencyProbeValueMetric : Uint8
		{
			ReleaseCascadeGlobalPublications,
			ChildQueueDepthCascadePublicationFirst,
			ChildQueueDepthCascadePublication2To4,
			ChildQueueDepthCascadePublication5To8,
			ChildQueueDepthCascadePublication9Plus,
			Count,
		};

		static constexpr Uint32 c_dependencyProbeThreadCount = 65;
		static constexpr Uint32 c_dependencyProbeHistogramBins = 512;
		static constexpr Uint32 c_dependencyProbeRenderEventCapacityPerThread = 8192;
		static constexpr Uint32 c_dependencyProbePublicationEventCapacityPerThread = 32768;

	struct DependencyProbeStageStats
	{
		Uint64 count{ 0 };
		Uint64 sumTicks{ 0 };
		Uint64 minTicks{ UINT64_MAX };
		Uint64 maxTicks{ 0 };
		Uint32 histogram[c_dependencyProbeHistogramBins]{};
	};

		struct alignas(64) DependencyProbeThreadStats
		{
			DependencyProbeStageStats stages[static_cast<Uint32>( DependencyProbeStage::Count )];
			DependencyProbeStageStats values[static_cast<Uint32>( DependencyProbeValueMetric::Count )];
		};

		static DependencyProbeThreadStats s_dependencyProbeStats[c_dependencyProbeThreadCount];
		static DependencyProbeRenderNodeEvent s_dependencyProbeRenderEvents[c_dependencyProbeThreadCount][c_dependencyProbeRenderEventCapacityPerThread];
		static DependencyProbePublicationEvent s_dependencyProbePublicationEvents[c_dependencyProbeThreadCount][c_dependencyProbePublicationEventCapacityPerThread];
		static Uint32 s_dependencyProbeRenderEventCounts[c_dependencyProbeThreadCount]{};
		static Uint32 s_dependencyProbePublicationEventCounts[c_dependencyProbeThreadCount]{};
		static Uint32 s_dependencyProbeRenderEventOverflow[c_dependencyProbeThreadCount]{};
		static Uint32 s_dependencyProbePublicationEventOverflow[c_dependencyProbeThreadCount]{};
		static Bool s_dependencyProbeDumped = false;

	static Uint32 GetDependencyProbeThreadIndex()
	{
		if ( g_tls_DispatcherThreadIndex < c_dependencyProbeThreadCount )
		{
			return g_tls_DispatcherThreadIndex;
		}
		if ( ::SIsMainThread() )
		{
			return 0;
		}
			return UINT32_MAX;
		}

		static void RecordDependencyProbePublicationEvent( const JobQueueEntry& entry, const DispatcherQueuePushProfile& profile )
		{
			if ( entry.dependencyProbeRole == JobQueueEntry::DependencyProbeRole::None || entry.dependencyProbeQueuedTicks == 0 )
			{
				return;
			}

			const Uint32 threadIndex = GetDependencyProbeThreadIndex();
			if ( threadIndex >= c_dependencyProbeThreadCount )
			{
				return;
			}

			Uint32& count = s_dependencyProbePublicationEventCounts[threadIndex];
			if ( count >= c_dependencyProbePublicationEventCapacityPerThread )
			{
				++s_dependencyProbePublicationEventOverflow[threadIndex];
				return;
			}

			auto& event = s_dependencyProbePublicationEvents[threadIndex][count++];
			event.publicationKey = entry.dependencyProbePublicationKey;
			event.queuedTicks = entry.dependencyProbeQueuedTicks;
			event.parentDebugUserData = entry.dependencyProbeParentDebugUserData;
			event.childDebugUserData = entry.dependencyProbeChildDebugUserData;
			event.producerThreadIndex = threadIndex;
			event.queueDepthAtEnqueue = entry.dependencyProbeQueueDepthAtEnqueue;
			event.producerRunnableDepthAtRelayDecision = entry.dependencyProbeProducerRunnableDepthAtRelayDecision;
			event.queuePushTicks = profile.queuePushTicks;
			event.semaphoreReleaseTicks = profile.semaphoreReleaseTicks;
			event.numThreadsWoken = static_cast<Uint32>( profile.numThreadsWoken );
			event.role = static_cast<Uint8>( entry.dependencyProbeRole );
		}

		static void RecordDependencyProbeRenderNodeEvent( const JobQueueEntry& entry, const Uint64 dispatcherEntryTicks,
			const Uint64 jobInvokeTicks, const Uint64 jobBodyEndTicks, const Uint32 dispatcherThreadIndex )
		{
			if ( entry.dependencyProbeRole != JobQueueEntry::DependencyProbeRole::RelayReleasedChild ||
				entry.dependencyProbeChildDebugUserData == nullptr )
			{
				return;
			}

			const Uint32 threadIndex = GetDependencyProbeThreadIndex();
			if ( threadIndex >= c_dependencyProbeThreadCount )
			{
				return;
			}

			Uint32& count = s_dependencyProbeRenderEventCounts[threadIndex];
			if ( count >= c_dependencyProbeRenderEventCapacityPerThread )
			{
				++s_dependencyProbeRenderEventOverflow[threadIndex];
				return;
			}

			auto& event = s_dependencyProbeRenderEvents[threadIndex][count++];
			event.parentDebugUserData = entry.dependencyProbeParentDebugUserData;
			event.childDebugUserData = entry.dependencyProbeChildDebugUserData;
			event.relayPublicationKey = entry.dependencyProbeOriginRelayPublicationKey;
			event.childPublicationKey = entry.dependencyProbePublicationKey;
			event.parentOriginTicks = entry.dependencyProbeOriginTicks;
			event.relayQueuedTicks = entry.dependencyProbeRelayQueuedTicks;
			event.relayPoppedTicks = entry.dependencyProbeRelayPoppedTicks;
			event.relayConsumerAcquireEndTicks = entry.dependencyProbeRelayConsumerAcquireEndTicks;
			event.relayStartTicks = entry.dependencyProbeRelayStartTicks;
			event.relayBodyEndTicks = entry.dependencyProbeReleaseOriginTicks;
			event.childQueuedTicks = entry.dependencyProbeQueuedTicks;
			event.childPoppedTicks = entry.dependencyProbePoppedTicks;
			event.childConsumerAcquireEndTicks = entry.dependencyProbeConsumerAcquireEndTicks;
			event.childStartTicks = dispatcherEntryTicks;
			event.childInvokeTicks = jobInvokeTicks;
			event.childBodyEndTicks = jobBodyEndTicks;
			event.producerRunnableDepthAtRelayDecision = entry.dependencyProbeProducerRunnableDepthAtRelayDecision;
			event.relayQueueDepthAtEnqueue = entry.dependencyProbeRelayQueueDepthAtEnqueue;
			event.childQueueDepthAtEnqueue = entry.dependencyProbeQueueDepthAtEnqueue;
			event.relayDispatcherThreadIndex = entry.dependencyProbeRelayDispatcherThreadIndex;
			event.childDispatcherThreadIndex = dispatcherThreadIndex;
			event.relayParentWaitingListOrdinal = entry.dependencyProbeRelayParentWaitingListOrdinal;
			event.childReleaseCascadePublicationOrdinal = entry.dependencyProbeReleaseCascadePublicationOrdinal;
			event.relayConsumerUsedKernelWait = entry.dependencyProbeRelayConsumerUsedKernelWait;
			event.childConsumerUsedKernelWait = entry.dependencyProbeConsumerUsedKernelWait;
		}

	static void RecordDependencyProbeSample( const DependencyProbeStage stage, const Uint64 startTicks, const Uint64 endTicks )
	{
		if ( startTicks == 0 || endTicks < startTicks )
		{
			return;
		}

		const Uint32 threadIndex = GetDependencyProbeThreadIndex();
		if ( threadIndex >= c_dependencyProbeThreadCount )
		{
			return;
		}

		const Uint64 deltaTicks = endTicks - startTicks;
		auto& stats = s_dependencyProbeStats[threadIndex].stages[static_cast<Uint32>( stage )];
		++stats.count;
		stats.sumTicks += deltaTicks;
		stats.minTicks = std::min( stats.minTicks, deltaTicks );
		stats.maxTicks = std::max( stats.maxTicks, deltaTicks );

		const Uint64 usec = red::ProfileTimer::GetUsec( deltaTicks );
		const Uint32 bin = static_cast<Uint32>( std::min<Uint64>( usec, c_dependencyProbeHistogramBins - 1 ) );
		++stats.histogram[bin];
	}

		static void RecordDependencyProbeDelta( const DependencyProbeStage stage, const Uint64 deltaTicks )
	{
		const Uint32 threadIndex = GetDependencyProbeThreadIndex();
		if ( threadIndex >= c_dependencyProbeThreadCount )
		{
			return;
		}

		auto& stats = s_dependencyProbeStats[threadIndex].stages[static_cast<Uint32>( stage )];
		++stats.count;
		stats.sumTicks += deltaTicks;
		stats.minTicks = std::min( stats.minTicks, deltaTicks );
		stats.maxTicks = std::max( stats.maxTicks, deltaTicks );

		const Uint64 usec = red::ProfileTimer::GetUsec( deltaTicks );
		const Uint32 bin = static_cast<Uint32>( std::min<Uint64>( usec, c_dependencyProbeHistogramBins - 1 ) );
			++stats.histogram[bin];
		}

		static void RecordDependencyProbeValue( const DependencyProbeValueMetric metric, const Uint32 value )
		{
			const Uint32 threadIndex = GetDependencyProbeThreadIndex();
			if ( threadIndex >= c_dependencyProbeThreadCount )
			{
				return;
			}

			auto& stats = s_dependencyProbeStats[threadIndex].values[static_cast<Uint32>( metric )];
			++stats.count;
			stats.sumTicks += value;
			stats.minTicks = std::min<Uint64>( stats.minTicks, value );
			stats.maxTicks = std::max<Uint64>( stats.maxTicks, value );
			const Uint32 bin = std::min<Uint32>( value, c_dependencyProbeHistogramBins - 1 );
			++stats.histogram[bin];
		}

	static void RecordDependencyProbePoppedEntry( JobQueueEntry& entry, const DispatcherQueuePopProfile& profile )
	{
		if ( entry.dependencyProbeRole == JobQueueEntry::DependencyProbeRole::None )
		{
			return;
		}

		entry.dependencyProbeConsumerUsedKernelWait = profile.usedKernelWait;
		entry.dependencyProbeConsumerScanPasses = profile.scanPasses;
		entry.dependencyProbeConsumerAcquireTicks = profile.semaphoreAcquireTicks;
		entry.dependencyProbeConsumerAcquireEndTicks = profile.semaphoreAcquireEndTicks;
		entry.dependencyProbeConsumerQueueScanTicks = profile.queueScanTicks;
		entry.dependencyProbePoppedTicks = red::ProfileTimer::GetTicks();
	}

		static void RecordDependencyProbeCompletedEntry( const JobQueueEntry& entry, const Uint64 dispatcherEntryTicks,
			const Uint64 jobInvokeTicks, const Uint64 jobBodyEndTicks, const Uint32 dispatcherThreadIndex )
	{
		if ( entry.dependencyProbeRole == JobQueueEntry::DependencyProbeRole::None )
		{
			return;
		}

		RecordDependencyProbeDelta(
			entry.dependencyProbeConsumerUsedKernelWait ? DependencyProbeStage::ConsumerAcquireKernel : DependencyProbeStage::ConsumerAcquireFast,
			entry.dependencyProbeConsumerAcquireTicks );
		RecordDependencyProbeDelta( DependencyProbeStage::ConsumerQueueScan, entry.dependencyProbeConsumerQueueScanTicks );
		RecordDependencyProbeSample( DependencyProbeStage::ConsumerPopToDispatcherEntry, entry.dependencyProbePoppedTicks, dispatcherEntryTicks );
		RecordDependencyProbeSample( DependencyProbeStage::DispatcherEntryToJobInvoke, dispatcherEntryTicks, jobInvokeTicks );

		if ( entry.dependencyProbeRole == JobQueueEntry::DependencyProbeRole::Relay )
		{
			RecordDependencyProbeSample( DependencyProbeStage::ParentReleaseToRelayQueue, entry.dependencyProbeOriginTicks, entry.dependencyProbeQueuedTicks );
			RecordDependencyProbeDelta( DependencyProbeStage::ParentBodyEndToCounterDecrement, entry.dependencyProbeParentBodyToCounterDecrementTicks );
			RecordDependencyProbeDelta( DependencyProbeStage::ParentCounterDecrement, entry.dependencyProbeParentCounterDecrementTicks );
				RecordDependencyProbeDelta( DependencyProbeStage::ParentWaitingListFlush, entry.dependencyProbeParentWaitingListFlushTicks );
				RecordDependencyProbeDelta( DependencyProbeStage::ParentCounterRefRelease, entry.dependencyProbeParentCounterRefReleaseTicks );
				RecordDependencyProbeDelta( DependencyProbeStage::ParentRefReleaseToQueueCall, entry.dependencyProbeParentRefReleaseToQueueCallTicks );
				RecordDependencyProbeDelta(
					entry.dependencyProbeParentWaitingListOrdinal == 0
						? DependencyProbeStage::ParentRefReleaseToFirstQueueCall
						: DependencyProbeStage::ParentRefReleaseToLaterQueueCall,
					entry.dependencyProbeParentRefReleaseToQueueCallTicks );
				RecordDependencyProbeDelta( DependencyProbeStage::RelayQueueSetup, entry.dependencyProbeQueueSetupTicks );
			RecordDependencyProbeSample( DependencyProbeStage::RelayQueueToPop, entry.dependencyProbeQueuedTicks, entry.dependencyProbePoppedTicks );
			RecordDependencyProbeSample( DependencyProbeStage::RelayQueueToStart, entry.dependencyProbeQueuedTicks, dispatcherEntryTicks );
			RecordDependencyProbeSample(
				entry.dependencyProbeConsumerUsedKernelWait ? DependencyProbeStage::RelayQueueToStartKernelConsumer : DependencyProbeStage::RelayQueueToStartFastConsumer,
				entry.dependencyProbeQueuedTicks, dispatcherEntryTicks );
			RecordDependencyProbeSample( DependencyProbeStage::RelayRun, jobInvokeTicks, jobBodyEndTicks );
			return;
		}

		RecordDependencyProbeSample( DependencyProbeStage::RelayReleaseToChildQueue, entry.dependencyProbeReleaseOriginTicks, entry.dependencyProbeQueuedTicks );
		RecordDependencyProbeSample( DependencyProbeStage::ChildQueueToStart, entry.dependencyProbeQueuedTicks, dispatcherEntryTicks );
		RecordDependencyProbeSample(
			entry.dependencyProbeConsumerUsedKernelWait ? DependencyProbeStage::ChildQueueToStartKernelConsumer : DependencyProbeStage::ChildQueueToStartFastConsumer,
			entry.dependencyProbeQueuedTicks, dispatcherEntryTicks );

		DependencyProbeStage popDepthStage;
		DependencyProbeStage startDepthStage;
		if ( entry.dependencyProbeQueueDepthAtEnqueue == 0 )
		{
			popDepthStage = DependencyProbeStage::ChildQueueToPopDepth0;
			startDepthStage = DependencyProbeStage::ChildQueueToStartDepth0;
			RecordDependencyProbeSample(
				entry.dependencyProbeConsumerUsedKernelWait ? DependencyProbeStage::ChildDepth0KernelQueueToAcquireReturn
					: DependencyProbeStage::ChildDepth0FastQueueToAcquireReturn,
				entry.dependencyProbeQueuedTicks, entry.dependencyProbeConsumerAcquireEndTicks );
			RecordDependencyProbeSample( DependencyProbeStage::ChildDepth0AcquireReturnToPop,
				entry.dependencyProbeConsumerAcquireEndTicks, entry.dependencyProbePoppedTicks );
		}
		else if ( entry.dependencyProbeQueueDepthAtEnqueue <= 3 )
		{
			popDepthStage = DependencyProbeStage::ChildQueueToPopDepth1To3;
			startDepthStage = DependencyProbeStage::ChildQueueToStartDepth1To3;
		}
		else if ( entry.dependencyProbeQueueDepthAtEnqueue <= 7 )
		{
			popDepthStage = DependencyProbeStage::ChildQueueToPopDepth4To7;
			startDepthStage = DependencyProbeStage::ChildQueueToStartDepth4To7;
		}
		else
		{
			popDepthStage = DependencyProbeStage::ChildQueueToPopDepth8Plus;
			startDepthStage = DependencyProbeStage::ChildQueueToStartDepth8Plus;
		}

			RecordDependencyProbeSample( popDepthStage, entry.dependencyProbeQueuedTicks, entry.dependencyProbePoppedTicks );
			RecordDependencyProbeSample( startDepthStage, entry.dependencyProbeQueuedTicks, dispatcherEntryTicks );
				if ( entry.dependencyProbeReleaseCascadePublicationOrdinal != UINT32_MAX )
				{
				DependencyProbeStage cascadePublicationStage;
				if ( entry.dependencyProbeReleaseCascadePublicationOrdinal == 0 )
				{
					cascadePublicationStage = DependencyProbeStage::ChildQueueToStartCascadePublicationFirst;
				}
				else if ( entry.dependencyProbeReleaseCascadePublicationOrdinal <= 3 )
				{
					cascadePublicationStage = DependencyProbeStage::ChildQueueToStartCascadePublication2To4;
				}
				else if ( entry.dependencyProbeReleaseCascadePublicationOrdinal <= 7 )
				{
					cascadePublicationStage = DependencyProbeStage::ChildQueueToStartCascadePublication5To8;
				}
				else
				{
					cascadePublicationStage = DependencyProbeStage::ChildQueueToStartCascadePublication9Plus;
				}
					RecordDependencyProbeSample( cascadePublicationStage, entry.dependencyProbeQueuedTicks, dispatcherEntryTicks );
				}
				if ( entry.dependencyProbeProducerRunnableDepthAtRelayDecision != UINT32_MAX )
				{
					const Bool producerQueueWasOccupied = entry.dependencyProbeProducerRunnableDepthAtRelayDecision != 0;
					RecordDependencyProbeSample(
						producerQueueWasOccupied ? DependencyProbeStage::ChildQueueToStartProducerQueueOccupied
							: DependencyProbeStage::ChildQueueToStartProducerQueueEmpty,
						entry.dependencyProbeQueuedTicks, dispatcherEntryTicks );
					RecordDependencyProbeSample(
						producerQueueWasOccupied ? DependencyProbeStage::ParentReleaseToChildStartProducerQueueOccupied
							: DependencyProbeStage::ParentReleaseToChildStartProducerQueueEmpty,
						entry.dependencyProbeOriginTicks, dispatcherEntryTicks );
				}
						RecordDependencyProbeSample( DependencyProbeStage::ParentReleaseToChildStart, entry.dependencyProbeOriginTicks, dispatcherEntryTicks );
				RecordDependencyProbeRenderNodeEvent( entry, dispatcherEntryTicks, jobInvokeTicks, jobBodyEndTicks, dispatcherThreadIndex );
			}

		static Uint32 GetDependencyProbePercentileBin( const Uint64* const histogram, const Uint64 count, const Uint32 percentile )
	{
		if ( count == 0 )
		{
			return 0;
		}

		const Uint64 target = ( count * percentile + 99 ) / 100;
		Uint64 accumulated = 0;
		for ( Uint32 bin = 0; bin < c_dependencyProbeHistogramBins; ++bin )
		{
			accumulated += histogram[bin];
			if ( accumulated >= target )
			{
				return bin;
			}
		}
		return c_dependencyProbeHistogramBins - 1;
	}

	static void DumpDependencyProbeStats()
	{
		if ( s_dependencyProbeDumped )
		{
			return;
		}
		s_dependencyProbeDumped = true;

		static const char* const stageNames[] = {
			"parent_release_to_relay_queue",
			"relay_queue_to_start",
			"relay_run",
			"relay_release_to_child_queue",
			"child_queue_to_start",
			"parent_release_to_child_start",
			"global_queue_push",
			"global_semaphore_release",
			"global_semaphore_release_wake",
			"global_semaphore_release_no_wake",
			"parent_body_end_to_counter_decrement",
			"parent_counter_decrement",
				"parent_waiting_list_flush",
				"parent_counter_ref_release",
				"parent_ref_release_to_queue_call",
				"parent_ref_release_to_first_queue_call",
				"parent_ref_release_to_later_queue_call",
				"relay_queue_setup",
			"consumer_acquire_fast",
			"consumer_acquire_kernel",
			"consumer_queue_scan",
			"relay_queue_to_pop",
			"consumer_pop_to_dispatcher_entry",
			"dispatcher_entry_to_job_invoke",
			"relay_queue_to_start_fast_consumer",
			"relay_queue_to_start_kernel_consumer",
			"child_queue_to_start_fast_consumer",
			"child_queue_to_start_kernel_consumer",
			"child_queue_to_pop_depth_0",
			"child_queue_to_pop_depth_1_3",
			"child_queue_to_pop_depth_4_7",
			"child_queue_to_pop_depth_8_plus",
			"child_depth0_kernel_queue_to_acquire_return",
			"child_depth0_fast_queue_to_acquire_return",
			"child_depth0_acquire_return_to_pop",
			"child_queue_to_start_depth_0",
				"child_queue_to_start_depth_1_3",
				"child_queue_to_start_depth_4_7",
				"child_queue_to_start_depth_8_plus",
				"child_queue_to_start_cascade_publication_first",
				"child_queue_to_start_cascade_publication_2_4",
				"child_queue_to_start_cascade_publication_5_8",
				"child_queue_to_start_cascade_publication_9_plus",
				"child_queue_to_start_producer_queue_empty",
				"child_queue_to_start_producer_queue_occupied",
				"parent_release_to_child_start_producer_queue_empty",
				"parent_release_to_child_start_producer_queue_occupied",
				"inline_relay_propagation_producer_queue_empty",
				"inline_relay_propagation_producer_queue_occupied",
				"relay_semaphore_release_wake",
				"relay_semaphore_release_no_wake",
				"child_semaphore_release_wake",
				"child_semaphore_release_no_wake",
			};
		static_assert( RED_ARRAY_COUNT_U32( stageNames ) == static_cast<Uint32>( DependencyProbeStage::Count ), "Dependency probe stage table mismatch" );

		for ( Uint32 stageIndex = 0; stageIndex < static_cast<Uint32>( DependencyProbeStage::Count ); ++stageIndex )
		{
			Uint64 count = 0;
			Uint64 sumTicks = 0;
			Uint64 minTicks = UINT64_MAX;
			Uint64 maxTicks = 0;
			Uint64 histogram[c_dependencyProbeHistogramBins]{};

			for ( Uint32 threadIndex = 0; threadIndex < c_dependencyProbeThreadCount; ++threadIndex )
			{
				const auto& stats = s_dependencyProbeStats[threadIndex].stages[stageIndex];
				count += stats.count;
				sumTicks += stats.sumTicks;
				if ( stats.count != 0 )
				{
					minTicks = std::min( minTicks, stats.minTicks );
					maxTicks = std::max( maxTicks, stats.maxTicks );
				}
				for ( Uint32 bin = 0; bin < c_dependencyProbeHistogramBins; ++bin )
				{
					histogram[bin] += stats.histogram[bin];
				}
			}

			if ( count == 0 )
			{
				continue;
			}

			const Double meanUsec = static_cast<Double>( red::ProfileTimer::GetNsec( sumTicks ) ) / ( 1000.0 * static_cast<Double>( count ) );
			const Double minUsec = static_cast<Double>( red::ProfileTimer::GetNsec( minTicks ) ) / 1000.0;
			const Double maxUsec = static_cast<Double>( red::ProfileTimer::GetNsec( maxTicks ) ) / 1000.0;
			RED_LOG( "Jobs2DependencyProbe stage=%hs n=%llu mean_us=%.3f p50_us=%u p95_us=%u p99_us=%u min_us=%.3f max_us=%.3f",
				stageNames[stageIndex], static_cast<unsigned long long>( count ), meanUsec,
				GetDependencyProbePercentileBin( histogram, count, 50 ), GetDependencyProbePercentileBin( histogram, count, 95 ),
				GetDependencyProbePercentileBin( histogram, count, 99 ), minUsec, maxUsec );
		}

			static const char* const valueNames[] = {
				"release_cascade_global_publications",
				"child_queue_depth_cascade_publication_first",
				"child_queue_depth_cascade_publication_2_4",
				"child_queue_depth_cascade_publication_5_8",
				"child_queue_depth_cascade_publication_9_plus",
			};
			static_assert( RED_ARRAY_COUNT_U32( valueNames ) == static_cast<Uint32>( DependencyProbeValueMetric::Count ), "Dependency probe value table mismatch" );

			for ( Uint32 metricIndex = 0; metricIndex < static_cast<Uint32>( DependencyProbeValueMetric::Count ); ++metricIndex )
			{
				Uint64 count = 0;
				Uint64 sumValue = 0;
				Uint64 minValue = UINT64_MAX;
				Uint64 maxValue = 0;
				Uint64 histogram[c_dependencyProbeHistogramBins]{};
				for ( Uint32 threadIndex = 0; threadIndex < c_dependencyProbeThreadCount; ++threadIndex )
				{
					const auto& stats = s_dependencyProbeStats[threadIndex].values[metricIndex];
					count += stats.count;
					sumValue += stats.sumTicks;
					if ( stats.count != 0 )
					{
						minValue = std::min( minValue, stats.minTicks );
						maxValue = std::max( maxValue, stats.maxTicks );
					}
					for ( Uint32 bin = 0; bin < c_dependencyProbeHistogramBins; ++bin )
					{
						histogram[bin] += stats.histogram[bin];
					}
				}
				if ( count == 0 )
				{
					continue;
				}

				const Double meanValue = static_cast<Double>( sumValue ) / static_cast<Double>( count );
			RED_LOG( "Jobs2DependencyProbe value=%hs n=%llu mean=%.3f p50=%u p95=%u p99=%u min=%llu max=%llu",
				valueNames[metricIndex], static_cast<unsigned long long>( count ), meanValue,
				GetDependencyProbePercentileBin( histogram, count, 50 ), GetDependencyProbePercentileBin( histogram, count, 95 ),
				GetDependencyProbePercentileBin( histogram, count, 99 ),
				static_cast<unsigned long long>( minValue ), static_cast<unsigned long long>( maxValue ) );
		}

		Uint64 renderEventCount = 0;
		Uint64 publicationEventCount = 0;
		Uint64 renderEventOverflow = 0;
		Uint64 publicationEventOverflow = 0;
		for ( Uint32 threadIndex = 0; threadIndex < c_dependencyProbeThreadCount; ++threadIndex )
		{
			renderEventCount += s_dependencyProbeRenderEventCounts[threadIndex];
			publicationEventCount += s_dependencyProbePublicationEventCounts[threadIndex];
			renderEventOverflow += s_dependencyProbeRenderEventOverflow[threadIndex];
			publicationEventOverflow += s_dependencyProbePublicationEventOverflow[threadIndex];
		}
		RED_LOG( "Jobs2DependencyProbe raw_render_events=%llu raw_publications=%llu render_event_overflow=%llu publication_overflow=%llu",
			static_cast<unsigned long long>( renderEventCount ), static_cast<unsigned long long>( publicationEventCount ),
			static_cast<unsigned long long>( renderEventOverflow ), static_cast<unsigned long long>( publicationEventOverflow ) );
		}
#endif

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

	const Uint32 abandonedRecruitmentControls = m_pendingRecruitmentControls.Exchange( 0 );
	if ( abandonedRecruitmentControls != 0 )
	{
	#if defined(VG_BUILD_PROFILE)
		m_recruitmentControlsAbandoned.ExchangeAdd( abandonedRecruitmentControls );
	#endif
	}

#if defined(VG_BUILD_PROFILE)
	helper::DumpDependencyProbeStats();
#endif

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

	static Uint64 RunJobFunctionWithInstrumentation( const JobQueueEntry& entry, const RunContext& runContext )
	{
		PC_SCOPE_INST_OBJ( *entry.jobDecl.instrumentationObject, entry.jobDecl.instrumentationObject->m_name );
#if defined(VG_BUILD_PROFILE)
		const Uint64 dependencyProbeInvokeTicks = red::ProfileTimer::GetTicks();
#else
		const Uint64 dependencyProbeInvokeTicks = 0;
#endif
		entry.jobDecl.jobFunc( entry.jobDecl.jobData, runContext );
		return dependencyProbeInvokeTicks;
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
	if ( entry.kind == JobQueueEntry::Kind::RecruitmentControl )
	{
		return;
	}
	if ( entry.kind == JobQueueEntry::Kind::DependencyRelay )
	{
		RED_FATAL_ASSERT( entry.accumulateCounterEntry );

#if defined(VG_BUILD_PROFILE)
		const Uint64 dependencyProbeJobStartTicks = red::ProfileTimer::GetTicks();
		const Uint64 dependencyProbeJobBodyEndTicks = red::ProfileTimer::GetTicks();
		const Uint64 previousCompletedJobTicks = g_tls_dependencyProbeCompletedJobTicks;
		const Uint64 previousRelayOriginTicks = g_tls_dependencyProbeRelayOriginTicks;
		const Uint64 previousRelayBodyEndTicks = g_tls_dependencyProbeRelayBodyEndTicks;
		const Uint64 previousRelayQueuedTicks = g_tls_dependencyProbeRelayQueuedTicks;
		const Uint64 previousRelayPoppedTicks = g_tls_dependencyProbeRelayPoppedTicks;
		const Uint64 previousRelayConsumerAcquireEndTicks = g_tls_dependencyProbeRelayConsumerAcquireEndTicks;
		const Uint64 previousRelayStartTicks = g_tls_dependencyProbeRelayStartTicks;
		const Uint32 previousRelayDecisionRunnableDepth = g_tls_dependencyProbeRelayDecisionRunnableDepth;
		const Uint32 previousRelayQueueDepthAtEnqueue = g_tls_dependencyProbeRelayQueueDepthAtEnqueue;
		const Uint32 previousRelayDispatcherThreadIndex = g_tls_dependencyProbeRelayDispatcherThreadIndex;
		const Uint32 previousRelayParentWaitingListOrdinal = g_tls_dependencyProbeRelayParentWaitingListOrdinal;
		const Bool previousRelayConsumerUsedKernelWait = g_tls_dependencyProbeRelayConsumerUsedKernelWait;
		const void* const previousRelayParentDebugUserData = g_tls_dependencyProbeRelayParentDebugUserData;
		const Uint64 previousRelayPublicationKey = g_tls_dependencyProbeRelayPublicationKey;
		const Bool previousReleasingFromRelay = g_tls_dependencyProbeReleasingFromRelay;
		const Priority previousReleasePriority = g_tls_dependencyProbeReleasePriority;

		g_tls_dependencyProbeCompletedJobTicks = dependencyProbeJobBodyEndTicks;
		g_tls_dependencyProbeRelayOriginTicks = entry.dependencyProbeOriginTicks;
		g_tls_dependencyProbeRelayBodyEndTicks = dependencyProbeJobBodyEndTicks;
		g_tls_dependencyProbeRelayQueuedTicks = entry.dependencyProbeQueuedTicks;
		g_tls_dependencyProbeRelayPoppedTicks = entry.dependencyProbePoppedTicks;
		g_tls_dependencyProbeRelayConsumerAcquireEndTicks = entry.dependencyProbeConsumerAcquireEndTicks;
		g_tls_dependencyProbeRelayStartTicks = dependencyProbeJobStartTicks;
		g_tls_dependencyProbeRelayDecisionRunnableDepth = entry.dependencyProbeProducerRunnableDepthAtRelayDecision;
		g_tls_dependencyProbeRelayQueueDepthAtEnqueue = entry.dependencyProbeQueueDepthAtEnqueue;
		g_tls_dependencyProbeRelayDispatcherThreadIndex = dispatcherThreadIndex;
		g_tls_dependencyProbeRelayParentWaitingListOrdinal = entry.dependencyProbeParentWaitingListOrdinal;
		g_tls_dependencyProbeRelayConsumerUsedKernelWait = entry.dependencyProbeConsumerUsedKernelWait;
		g_tls_dependencyProbeRelayParentDebugUserData = entry.dependencyProbeParentDebugUserData;
		g_tls_dependencyProbeRelayPublicationKey = entry.dependencyProbePublicationKey;
		g_tls_dependencyProbeReleasingFromRelay = true;
		g_tls_dependencyProbeReleasePriority = priority;
#endif

		ContinuationReleaseContext continuationContext;
		continuationContext.releasePriority = priority;
		DecrementCounterEntryInternal( entry.accumulateCounterEntry, optLocalQueue, &continuationContext );

#if defined(VG_BUILD_PROFILE)
		g_tls_dependencyProbeCompletedJobTicks = previousCompletedJobTicks;
		g_tls_dependencyProbeRelayOriginTicks = previousRelayOriginTicks;
		g_tls_dependencyProbeRelayBodyEndTicks = previousRelayBodyEndTicks;
		g_tls_dependencyProbeRelayQueuedTicks = previousRelayQueuedTicks;
		g_tls_dependencyProbeRelayPoppedTicks = previousRelayPoppedTicks;
		g_tls_dependencyProbeRelayConsumerAcquireEndTicks = previousRelayConsumerAcquireEndTicks;
		g_tls_dependencyProbeRelayStartTicks = previousRelayStartTicks;
		g_tls_dependencyProbeRelayDecisionRunnableDepth = previousRelayDecisionRunnableDepth;
		g_tls_dependencyProbeRelayQueueDepthAtEnqueue = previousRelayQueueDepthAtEnqueue;
		g_tls_dependencyProbeRelayDispatcherThreadIndex = previousRelayDispatcherThreadIndex;
		g_tls_dependencyProbeRelayParentWaitingListOrdinal = previousRelayParentWaitingListOrdinal;
		g_tls_dependencyProbeRelayConsumerUsedKernelWait = previousRelayConsumerUsedKernelWait;
		g_tls_dependencyProbeRelayParentDebugUserData = previousRelayParentDebugUserData;
		g_tls_dependencyProbeRelayPublicationKey = previousRelayPublicationKey;
		g_tls_dependencyProbeReleasingFromRelay = previousReleasingFromRelay;
		g_tls_dependencyProbeReleasePriority = previousReleasePriority;
		helper::RecordDependencyProbeCompletedEntry(
			entry, dependencyProbeJobStartTicks, dependencyProbeJobStartTicks, dependencyProbeJobBodyEndTicks, dispatcherThreadIndex );
#endif
		return;
	}
	RED_FATAL_ASSERT( entry.kind == JobQueueEntry::Kind::Job );

#if defined(VG_BUILD_PROFILE)
		const Uint64 dependencyProbeJobStartTicks = red::ProfileTimer::GetTicks();
#endif

	RunContext runContext;
	
	// FIXME:
	//////////////////////////////////////////////////////////////////////////
	//helper::ScopedBanFrameAllocator scopedBanFrameAllocator{ entry };
	
	RED_FATAL_ASSERT(entry.accumulateCounterEntry);
	job::Counter continuationCounter{ *entry.accumulateCounterEntry };
	helper::InitRunContext( entry, dispatcherThreadIndex, { priority, Affinity::All, continuationCounter.Internal_GetIOPriority() }, continuationCounter, runContext );
	const Uint64 dependencyProbeJobInvokeTicks = helper::RunJobFunctionWithInstrumentation( entry, runContext );

#if defined(VG_BUILD_PROFILE)
	const Uint64 dependencyProbeJobBodyEndTicks = red::ProfileTimer::GetTicks();
#endif
	
	if ( entry.accumulateCounterEntry )
	{
		#if defined(VG_BUILD_PROFILE)
				const Uint64 previousCompletedJobTicks = g_tls_dependencyProbeCompletedJobTicks;
				const Uint64 previousRelayOriginTicks = g_tls_dependencyProbeRelayOriginTicks;
				const Uint64 previousRelayBodyEndTicks = g_tls_dependencyProbeRelayBodyEndTicks;
				const Uint64 previousRelayQueuedTicks = g_tls_dependencyProbeRelayQueuedTicks;
				const Uint64 previousRelayPoppedTicks = g_tls_dependencyProbeRelayPoppedTicks;
				const Uint64 previousRelayConsumerAcquireEndTicks = g_tls_dependencyProbeRelayConsumerAcquireEndTicks;
				const Uint64 previousRelayStartTicks = g_tls_dependencyProbeRelayStartTicks;
				const Uint32 previousRelayDecisionRunnableDepth = g_tls_dependencyProbeRelayDecisionRunnableDepth;
				const Uint32 previousRelayQueueDepthAtEnqueue = g_tls_dependencyProbeRelayQueueDepthAtEnqueue;
				const Uint32 previousRelayDispatcherThreadIndex = g_tls_dependencyProbeRelayDispatcherThreadIndex;
				const Uint32 previousRelayParentWaitingListOrdinal = g_tls_dependencyProbeRelayParentWaitingListOrdinal;
				const Bool previousRelayConsumerUsedKernelWait = g_tls_dependencyProbeRelayConsumerUsedKernelWait;
				const void* const previousRelayParentDebugUserData = g_tls_dependencyProbeRelayParentDebugUserData;
					const Uint64 previousRelayPublicationKey = g_tls_dependencyProbeRelayPublicationKey;
					const Bool previousReleasingFromRelay = g_tls_dependencyProbeReleasingFromRelay;
					const Priority previousReleasePriority = g_tls_dependencyProbeReleasePriority;

				g_tls_dependencyProbeCompletedJobTicks = dependencyProbeJobBodyEndTicks;
				g_tls_dependencyProbeReleasePriority = priority;
#endif

			ContinuationReleaseContext continuationContext;
			continuationContext.releasePriority = priority;
			DecrementCounterEntryInternal( entry.accumulateCounterEntry, optLocalQueue, &continuationContext );

#if defined(VG_BUILD_PROFILE)
				g_tls_dependencyProbeCompletedJobTicks = previousCompletedJobTicks;
				g_tls_dependencyProbeRelayOriginTicks = previousRelayOriginTicks;
				g_tls_dependencyProbeRelayBodyEndTicks = previousRelayBodyEndTicks;
				g_tls_dependencyProbeRelayQueuedTicks = previousRelayQueuedTicks;
				g_tls_dependencyProbeRelayPoppedTicks = previousRelayPoppedTicks;
				g_tls_dependencyProbeRelayConsumerAcquireEndTicks = previousRelayConsumerAcquireEndTicks;
				g_tls_dependencyProbeRelayStartTicks = previousRelayStartTicks;
				g_tls_dependencyProbeRelayDecisionRunnableDepth = previousRelayDecisionRunnableDepth;
				g_tls_dependencyProbeRelayQueueDepthAtEnqueue = previousRelayQueueDepthAtEnqueue;
				g_tls_dependencyProbeRelayDispatcherThreadIndex = previousRelayDispatcherThreadIndex;
				g_tls_dependencyProbeRelayParentWaitingListOrdinal = previousRelayParentWaitingListOrdinal;
				g_tls_dependencyProbeRelayConsumerUsedKernelWait = previousRelayConsumerUsedKernelWait;
				g_tls_dependencyProbeRelayParentDebugUserData = previousRelayParentDebugUserData;
					g_tls_dependencyProbeRelayPublicationKey = previousRelayPublicationKey;
					g_tls_dependencyProbeReleasingFromRelay = previousReleasingFromRelay;
					g_tls_dependencyProbeReleasePriority = previousReleasePriority;
		#endif
	}

#if defined(VG_BUILD_PROFILE)
		helper::RecordDependencyProbeCompletedEntry( entry, dependencyProbeJobStartTicks, dependencyProbeJobInvokeTicks, dependencyProbeJobBodyEndTicks,
			dispatcherThreadIndex );
#endif

#if !defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
	jobScopeAllocator.Reset();
#endif
}

Bool Dispatcher::TryPopJobQueueEntry( JobQueueEntry& outEntry, Priority& outPriority )
{
#if defined(VG_BUILD_PROFILE)
	DispatcherQueuePopProfile profile;
	const Bool popped = m_dispatcherQueue.TryPop( outEntry, outPriority, &profile );
	if ( popped )
	{
		OnQueueEntryPopped( outEntry );
		helper::RecordDependencyProbePoppedEntry( outEntry, profile );
	}
	return popped;
#else
	const Bool popped = m_dispatcherQueue.TryPop( outEntry, outPriority );
	if ( popped )
	{
		OnQueueEntryPopped( outEntry );
	}
	return popped;
#endif
}

void Dispatcher::PopJobQueueEntry( JobQueueEntry& outEntry, Priority& outPriority )
{
#if defined(VG_BUILD_PROFILE)
	DispatcherQueuePopProfile profile;
	m_dispatcherQueue.Pop( outEntry, outPriority, &profile );
	OnQueueEntryPopped( outEntry );
	helper::RecordDependencyProbePoppedEntry( outEntry, profile );
#else
	m_dispatcherQueue.Pop( outEntry, outPriority );
	OnQueueEntryPopped( outEntry );
#endif
}

void Dispatcher::OnQueueEntryPopped( const JobQueueEntry& entry )
{
	if ( entry.kind != JobQueueEntry::Kind::RecruitmentControl )
	{
		return;
	}

	const Uint32 remainingPendingControls = m_pendingRecruitmentControls.Decrement();
	RED_FATAL_ASSERT( remainingPendingControls != UINT32_MAX );
	#if defined(VG_BUILD_PROFILE)
	m_recruitmentControlsPopped.Increment();
	#endif
}

Bool Dispatcher::TryQueueRecruitmentControl( const JobQueueEntry& relayEntry )
{
	if ( m_isExitRequested.GetValue() )
	{
		return false;
	}

	const Uint32 ordinaryWorkerCount = GetNumDispatcherThreads( true );
	const Uint32 maxPendingControls = ordinaryWorkerCount > 1 ? ordinaryWorkerCount - 1 : 0;
	Uint32 pendingControls = m_pendingRecruitmentControls.GetValue();
	for ( ;; )
	{
		if ( pendingControls >= maxPendingControls )
		{
		#if defined(VG_BUILD_PROFILE)
			m_recruitmentControlsDroppedBound.Increment();
		#endif
			return false;
		}

		if ( m_pendingRecruitmentControls.CompareExchange( pendingControls + 1, pendingControls ) == pendingControls )
		{
			break;
		}
		pendingControls = m_pendingRecruitmentControls.GetValue();
	}

	if ( m_isExitRequested.GetValue() )
	{
		m_pendingRecruitmentControls.Decrement();
		return false;
	}

	JobQueueEntry recruitmentControl;
	recruitmentControl.kind = JobQueueEntry::Kind::RecruitmentControl;

#if defined(VG_BUILD_PROFILE)
	recruitmentControl.dependencyProbeRole = JobQueueEntry::DependencyProbeRole::RecruitmentHint;
	recruitmentControl.dependencyProbeParentDebugUserData = relayEntry.dependencyProbeParentDebugUserData;
	recruitmentControl.dependencyProbeChildDebugUserData = relayEntry.dependencyProbeChildDebugUserData;
	recruitmentControl.dependencyProbeProducerRunnableDepthAtRelayDecision = relayEntry.dependencyProbeProducerRunnableDepthAtRelayDecision;
	recruitmentControl.dependencyProbeQueueDepthAtEnqueue = m_dispatcherQueue.GetApproximateDepth( Priority::Latent );
	recruitmentControl.dependencyProbeQueuedTicks = red::ProfileTimer::GetTicks();
	const Uint32 producerThreadIndex = helper::GetDependencyProbeThreadIndex();
	if ( producerThreadIndex < helper::c_dependencyProbeThreadCount )
	{
		recruitmentControl.dependencyProbePublicationKey =
			( static_cast<Uint64>( producerThreadIndex ) << 32u ) | static_cast<Uint64>( ++g_tls_dependencyProbePublicationOrdinal );
	}

	DispatcherQueuePushProfile pushProfile;
	const Bool pushed = m_dispatcherQueue.TryPushNonBlocking( recruitmentControl, Priority::Latent, Affinity::All, &pushProfile );
#else
	RED_TOUCH( relayEntry );
	const Bool pushed = m_dispatcherQueue.TryPushNonBlocking( recruitmentControl, Priority::Latent, Affinity::All );
#endif

	if ( !pushed )
	{
		m_pendingRecruitmentControls.Decrement();
	#if defined(VG_BUILD_PROFILE)
		m_recruitmentControlsDroppedQueueFull.Increment();
	#endif
		return false;
	}

	#if defined(VG_BUILD_PROFILE)
	m_recruitmentControlsAdmitted.Increment();
	#endif

#if defined(VG_BUILD_PROFILE)
	helper::RecordDependencyProbeDelta( helper::DependencyProbeStage::GlobalQueuePush, pushProfile.queuePushTicks );
	helper::RecordDependencyProbeDelta( helper::DependencyProbeStage::GlobalSemaphoreRelease, pushProfile.semaphoreReleaseTicks );
	helper::RecordDependencyProbeDelta(
		pushProfile.numThreadsWoken > 0 ? helper::DependencyProbeStage::GlobalSemaphoreReleaseWake
			: helper::DependencyProbeStage::GlobalSemaphoreReleaseNoWake,
		pushProfile.semaphoreReleaseTicks );
	helper::RecordDependencyProbePublicationEvent( recruitmentControl, pushProfile );
#endif

	return true;
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
	DecrementCounterEntryInternal( counterEntry, optLocalQueue, nullptr );
}

void Dispatcher::DecrementCounterEntryInternal( CounterEntry* counterEntry, TLocalQueue* optLocalQueue, ContinuationReleaseContext* optContinuationContext )
{
	RED_FATAL_ASSERT( counterEntry );

#if defined(VG_BUILD_PROFILE)
	const Uint64 dependencyProbeDecrementStartTicks = red::ProfileTimer::GetTicks();
#endif
	
		if ( !counterEntry->counterValue.Decrement().isZero )
		{
			return;
		}

#if defined(VG_BUILD_PROFILE)
		const Bool dependencyProbeTrackReleaseCascade = g_tls_IsDispatcherThread && optLocalQueue != nullptr;
		const Bool dependencyProbeIsReleaseCascadeRoot = dependencyProbeTrackReleaseCascade && g_tls_dependencyProbeReleaseCascadeDepth == 0;
		if ( dependencyProbeTrackReleaseCascade )
		{
				if ( dependencyProbeIsReleaseCascadeRoot )
				{
					g_tls_dependencyProbeReleaseCascadeGlobalPublications = 0;
					g_tls_dependencyProbeInlineRelayCount = 0;
				}
			++g_tls_dependencyProbeReleaseCascadeDepth;
			}
			const Uint64 dependencyProbeDecrementEndTicks = red::ProfileTimer::GetTicks();
			const void* const dependencyProbeParentDebugUserData = counterEntry->GetDebugUserData();
	#endif

	// We hit zero counter value, time to release any waiting entries
#if defined(VG_BUILD_PROFILE)
	const Uint64 dependencyProbeFlushStartTicks = red::ProfileTimer::GetTicks();
#endif
	auto* const entriesHead = DispatcherHelper::FlushWaitingList( *counterEntry );
#if defined(VG_BUILD_PROFILE)
	const Uint64 dependencyProbeFlushEndTicks = red::ProfileTimer::GetTicks();
	const Uint64 dependencyProbeRefReleaseStartTicks = dependencyProbeFlushEndTicks;
#endif
	auto* entryIter = entriesHead;

	// See RunJob() for AddRef() logic.
	if ( counterEntry->refCountMask.Release().isZero )
	{
		// Free now before resuming any suspended jobs. Make it easier to hit any issues that might arise from accessing the counter.
		// If any dependencies or suspended jobs were still going to use the counter again, it would have been kept alive by an AddRef().
		DispatcherHelper::FreeEntry( counterEntry );
	}

#if defined(VG_BUILD_PROFILE)
	const Uint64 dependencyProbeRefReleaseEndTicks = red::ProfileTimer::GetTicks();
	const Uint64 previousParentBodyToCounterDecrementTicks = g_tls_dependencyProbeParentBodyToCounterDecrementTicks;
	const Uint64 previousParentCounterDecrementTicks = g_tls_dependencyProbeParentCounterDecrementTicks;
		const Uint64 previousParentWaitingListFlushTicks = g_tls_dependencyProbeParentWaitingListFlushTicks;
			const Uint64 previousParentCounterRefReleaseTicks = g_tls_dependencyProbeParentCounterRefReleaseTicks;
				const Uint64 previousParentRefReleaseEndTicks = g_tls_dependencyProbeParentRefReleaseEndTicks;
				const void* const previousParentDebugUserData = g_tls_dependencyProbeParentDebugUserData;
				const Uint32 previousParentWaitingListOrdinal = g_tls_dependencyProbeParentWaitingListOrdinal;
				const Bool previousSingleWaiterRelease = g_tls_dependencyProbeSingleWaiterRelease;
	g_tls_dependencyProbeParentBodyToCounterDecrementTicks =
		g_tls_dependencyProbeCompletedJobTicks != 0 && dependencyProbeDecrementStartTicks >= g_tls_dependencyProbeCompletedJobTicks
			? dependencyProbeDecrementStartTicks - g_tls_dependencyProbeCompletedJobTicks
			: 0;
	g_tls_dependencyProbeParentCounterDecrementTicks = dependencyProbeDecrementEndTicks - dependencyProbeDecrementStartTicks;
	g_tls_dependencyProbeParentWaitingListFlushTicks = dependencyProbeFlushEndTicks - dependencyProbeFlushStartTicks;
			g_tls_dependencyProbeParentCounterRefReleaseTicks = dependencyProbeRefReleaseEndTicks - dependencyProbeRefReleaseStartTicks;
			g_tls_dependencyProbeParentRefReleaseEndTicks = dependencyProbeRefReleaseEndTicks;
				g_tls_dependencyProbeParentDebugUserData = dependencyProbeParentDebugUserData;
				g_tls_dependencyProbeParentWaitingListOrdinal = 0;
				g_tls_dependencyProbeSingleWaiterRelease = entriesHead != nullptr && entriesHead->next == nullptr;
#endif

			const Bool singleWaiterRelease = entriesHead != nullptr && entriesHead->next == nullptr;

			// Queue up the waiting jobs
			if ( entryIter )
			{
				for ( ;; )
				{
					QueueJobAndSignal( entryIter->job, entryIter->accumulateCounterEntry, optLocalQueue, optContinuationContext, singleWaiterRelease );
#if defined(VG_BUILD_PROFILE)
				++g_tls_dependencyProbeParentWaitingListOrdinal;
#endif
				RED_FATAL_ASSERT( entryIter != entryIter->next );
				if ( !entryIter->next )
			{
				break;
			}
			entryIter = entryIter->next;
			}
		}

#if defined(VG_BUILD_PROFILE)
		if ( dependencyProbeTrackReleaseCascade )
		{
			RED_FATAL_ASSERT( g_tls_dependencyProbeReleaseCascadeDepth > 0 );
			--g_tls_dependencyProbeReleaseCascadeDepth;
			if ( dependencyProbeIsReleaseCascadeRoot && g_tls_dependencyProbeReleaseCascadeGlobalPublications != 0 )
			{
				helper::RecordDependencyProbeValue(
					helper::DependencyProbeValueMetric::ReleaseCascadeGlobalPublications,
					g_tls_dependencyProbeReleaseCascadeGlobalPublications );
			}
		}
		g_tls_dependencyProbeParentBodyToCounterDecrementTicks = previousParentBodyToCounterDecrementTicks;
		g_tls_dependencyProbeParentCounterDecrementTicks = previousParentCounterDecrementTicks;
		g_tls_dependencyProbeParentWaitingListFlushTicks = previousParentWaitingListFlushTicks;
			g_tls_dependencyProbeParentCounterRefReleaseTicks = previousParentCounterRefReleaseTicks;
			g_tls_dependencyProbeParentRefReleaseEndTicks = previousParentRefReleaseEndTicks;
				g_tls_dependencyProbeParentDebugUserData = previousParentDebugUserData;
				g_tls_dependencyProbeParentWaitingListOrdinal = previousParentWaitingListOrdinal;
				g_tls_dependencyProbeSingleWaiterRelease = previousSingleWaiterRelease;
#endif

	helper::FreeEntry( entriesHead );
}

void Dispatcher::QueueJobAndSignal( const JobDecl& job, CounterEntry* accumulateCounterEntry, TLocalQueue* optLocalQueue,
	ContinuationReleaseContext* optContinuationContext, Bool singleWaiterRelease )
{
#if defined(VG_BUILD_PROFILE)
	const Uint64 dependencyProbeQueueSetupStartTicks = red::ProfileTimer::GetTicks();
#endif
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
		entry.accumulateCounterEntry = accumulateCounterEntry;
		const Bool isDependencyRelay = job.jobFunc == &helper::DependencyRelayJob;
		entry.kind = isDependencyRelay ? JobQueueEntry::Kind::DependencyRelay : JobQueueEntry::Kind::Job;
		if ( !isDependencyRelay )
		{
			entry.jobDecl = job;
		}

			#if defined(VG_BUILD_PROFILE)
					entry.dependencyProbeChildDebugUserData = accumulateCounterEntry ? accumulateCounterEntry->GetDebugUserData() : nullptr;
					if ( c_dependencyProbeBoostSingleWaiterRelayPriority && isDependencyRelay &&
						g_tls_dependencyProbeSingleWaiterRelease && affinity == Affinity::All )
				{
					if ( priority == Priority::Latent )
					{
						priority = Priority::RenderPath;
					}
					else if ( priority == Priority::RenderPath )
					{
						priority = Priority::CriticalPath;
					}
					else if ( priority == Priority::CriticalPath )
					{
						priority = Priority::Immediate;
					}
				}
					if ( isDependencyRelay )
			{
				entry.dependencyProbeRole = JobQueueEntry::DependencyProbeRole::Relay;
				entry.dependencyProbeParentDebugUserData = g_tls_dependencyProbeParentDebugUserData;
				entry.dependencyProbeOriginTicks = g_tls_dependencyProbeCompletedJobTicks;
			Uint32 producerRunnableDepth = 0;
			for ( Uint32 priorityIndex = 0; priorityIndex < static_cast<Uint32>( Priority::COUNT ); ++priorityIndex )
			{
				producerRunnableDepth += m_dispatcherQueue.GetApproximateDepth( static_cast<Priority>( priorityIndex ) );
			}
			entry.dependencyProbeProducerRunnableDepthAtRelayDecision = producerRunnableDepth;
			entry.dependencyProbeParentBodyToCounterDecrementTicks = g_tls_dependencyProbeParentBodyToCounterDecrementTicks;
		entry.dependencyProbeParentCounterDecrementTicks = g_tls_dependencyProbeParentCounterDecrementTicks;
		entry.dependencyProbeParentWaitingListFlushTicks = g_tls_dependencyProbeParentWaitingListFlushTicks;
			entry.dependencyProbeParentCounterRefReleaseTicks = g_tls_dependencyProbeParentCounterRefReleaseTicks;
			entry.dependencyProbeParentRefReleaseToQueueCallTicks =
				g_tls_dependencyProbeParentRefReleaseEndTicks != 0 && dependencyProbeQueueSetupStartTicks >= g_tls_dependencyProbeParentRefReleaseEndTicks
					? dependencyProbeQueueSetupStartTicks - g_tls_dependencyProbeParentRefReleaseEndTicks
					: 0;
			entry.dependencyProbeParentWaitingListOrdinal = g_tls_dependencyProbeParentWaitingListOrdinal;
		}
			else if ( g_tls_dependencyProbeReleasingFromRelay )
			{
				entry.dependencyProbeRole = JobQueueEntry::DependencyProbeRole::RelayReleasedChild;
					entry.dependencyProbeParentDebugUserData = g_tls_dependencyProbeRelayParentDebugUserData;
					entry.dependencyProbeOriginRelayPublicationKey = g_tls_dependencyProbeRelayPublicationKey;
					entry.dependencyProbeOriginTicks = g_tls_dependencyProbeRelayOriginTicks;
				entry.dependencyProbeReleaseOriginTicks = g_tls_dependencyProbeRelayBodyEndTicks;
				entry.dependencyProbeRelayQueuedTicks = g_tls_dependencyProbeRelayQueuedTicks;
				entry.dependencyProbeRelayPoppedTicks = g_tls_dependencyProbeRelayPoppedTicks;
				entry.dependencyProbeRelayConsumerAcquireEndTicks = g_tls_dependencyProbeRelayConsumerAcquireEndTicks;
				entry.dependencyProbeRelayStartTicks = g_tls_dependencyProbeRelayStartTicks;
				entry.dependencyProbeProducerRunnableDepthAtRelayDecision = g_tls_dependencyProbeRelayDecisionRunnableDepth;
				entry.dependencyProbeRelayQueueDepthAtEnqueue = g_tls_dependencyProbeRelayQueueDepthAtEnqueue;
				entry.dependencyProbeRelayDispatcherThreadIndex = g_tls_dependencyProbeRelayDispatcherThreadIndex;
				entry.dependencyProbeRelayParentWaitingListOrdinal = g_tls_dependencyProbeRelayParentWaitingListOrdinal;
				entry.dependencyProbeRelayConsumerUsedKernelWait = g_tls_dependencyProbeRelayConsumerUsedKernelWait;
			if ( g_tls_dependencyProbeReleaseCascadeDepth != 0 )
			{
				entry.dependencyProbeReleaseCascadePublicationOrdinal = g_tls_dependencyProbeReleaseCascadeGlobalPublications++;
				}
			}

	#endif

		const Bool canInlineDependencyRelay =
			isDependencyRelay &&
			g_tls_IsDispatcherThread &&
			optLocalQueue != nullptr &&
			optContinuationContext != nullptr &&
			accumulateCounterEntry != nullptr &&
			accumulateCounterEntry->GetAffinity() == Affinity::All &&
			optContinuationContext->releasePriority == priority &&
			singleWaiterRelease &&
			optContinuationContext->inlineRelayDepth < c_inlineDependencyRelayDepthLimit &&
			optContinuationContext->inlineRelayTransitions < c_inlineDependencyRelayBudget;

		if ( canInlineDependencyRelay )
		{
			const Priority previousReleasePriority = optContinuationContext->releasePriority;
			optContinuationContext->releasePriority = priority;
			++optContinuationContext->inlineRelayDepth;
			++optContinuationContext->inlineRelayTransitions;
	#if defined(VG_BUILD_PROFILE)
			m_dependencyRelaysInlined.Increment();
	#endif

	#if defined(VG_BUILD_PROFILE)
			const Uint64 previousCompletedJobTicks = g_tls_dependencyProbeCompletedJobTicks;
			entry.dependencyProbeQueueDepthAtEnqueue = m_dispatcherQueue.GetApproximateDepth( priority );
			const Uint64 inlineRelayTicks = red::ProfileTimer::GetTicks();
			const Uint64 previousRelayOriginTicks = g_tls_dependencyProbeRelayOriginTicks;
			const Uint64 previousRelayBodyEndTicks = g_tls_dependencyProbeRelayBodyEndTicks;
			const Uint64 previousRelayQueuedTicks = g_tls_dependencyProbeRelayQueuedTicks;
			const Uint64 previousRelayPoppedTicks = g_tls_dependencyProbeRelayPoppedTicks;
			const Uint64 previousRelayConsumerAcquireEndTicks = g_tls_dependencyProbeRelayConsumerAcquireEndTicks;
			const Uint64 previousRelayStartTicks = g_tls_dependencyProbeRelayStartTicks;
			const Uint32 previousRelayDecisionRunnableDepth = g_tls_dependencyProbeRelayDecisionRunnableDepth;
			const Uint32 previousRelayQueueDepthAtEnqueue = g_tls_dependencyProbeRelayQueueDepthAtEnqueue;
			const Uint32 previousRelayDispatcherThreadIndex = g_tls_dependencyProbeRelayDispatcherThreadIndex;
			const Uint32 previousRelayParentWaitingListOrdinal = g_tls_dependencyProbeRelayParentWaitingListOrdinal;
			const Bool previousRelayConsumerUsedKernelWait = g_tls_dependencyProbeRelayConsumerUsedKernelWait;
			const void* const previousRelayParentDebugUserData = g_tls_dependencyProbeRelayParentDebugUserData;
			const Uint64 previousRelayPublicationKey = g_tls_dependencyProbeRelayPublicationKey;
			const Bool previousReleasingFromRelay = g_tls_dependencyProbeReleasingFromRelay;
			const Priority previousProbeReleasePriority = g_tls_dependencyProbeReleasePriority;

			g_tls_dependencyProbeRelayOriginTicks = entry.dependencyProbeOriginTicks;
			g_tls_dependencyProbeRelayBodyEndTicks = inlineRelayTicks;
			g_tls_dependencyProbeRelayQueuedTicks = inlineRelayTicks;
			g_tls_dependencyProbeRelayPoppedTicks = inlineRelayTicks;
			g_tls_dependencyProbeRelayConsumerAcquireEndTicks = inlineRelayTicks;
			g_tls_dependencyProbeRelayStartTicks = inlineRelayTicks;
			g_tls_dependencyProbeRelayDecisionRunnableDepth = entry.dependencyProbeProducerRunnableDepthAtRelayDecision;
			g_tls_dependencyProbeRelayQueueDepthAtEnqueue = entry.dependencyProbeQueueDepthAtEnqueue;
			g_tls_dependencyProbeRelayDispatcherThreadIndex = g_tls_DispatcherThreadIndex;
			g_tls_dependencyProbeRelayParentWaitingListOrdinal = entry.dependencyProbeParentWaitingListOrdinal;
			g_tls_dependencyProbeRelayConsumerUsedKernelWait = false;
			g_tls_dependencyProbeRelayParentDebugUserData = entry.dependencyProbeParentDebugUserData;
			g_tls_dependencyProbeRelayPublicationKey = 0;
			g_tls_dependencyProbeReleasingFromRelay = true;
			g_tls_dependencyProbeReleasePriority = priority;
			g_tls_dependencyProbeCompletedJobTicks = inlineRelayTicks;
			++g_tls_dependencyProbeInlineRelayDepth;
			++g_tls_dependencyProbeInlineRelayCount;
	#endif

				DecrementCounterEntryInternal( accumulateCounterEntry, optLocalQueue, optContinuationContext );

				if ( c_recruitForInlinedDependencyRelay )
				{
					TryQueueRecruitmentControl( entry );
				}

	#if defined(VG_BUILD_PROFILE)
			--g_tls_dependencyProbeInlineRelayDepth;
			const Uint64 inlineRelayEndTicks = red::ProfileTimer::GetTicks();
			helper::RecordDependencyProbeSample(
				entry.dependencyProbeProducerRunnableDepthAtRelayDecision == 0
					? helper::DependencyProbeStage::InlineRelayPropagationProducerQueueEmpty
					: helper::DependencyProbeStage::InlineRelayPropagationProducerQueueOccupied,
				inlineRelayTicks,
				inlineRelayEndTicks );

			g_tls_dependencyProbeRelayOriginTicks = previousRelayOriginTicks;
			g_tls_dependencyProbeRelayBodyEndTicks = previousRelayBodyEndTicks;
			g_tls_dependencyProbeRelayQueuedTicks = previousRelayQueuedTicks;
			g_tls_dependencyProbeRelayPoppedTicks = previousRelayPoppedTicks;
			g_tls_dependencyProbeRelayConsumerAcquireEndTicks = previousRelayConsumerAcquireEndTicks;
			g_tls_dependencyProbeRelayStartTicks = previousRelayStartTicks;
			g_tls_dependencyProbeRelayDecisionRunnableDepth = previousRelayDecisionRunnableDepth;
			g_tls_dependencyProbeRelayQueueDepthAtEnqueue = previousRelayQueueDepthAtEnqueue;
			g_tls_dependencyProbeRelayDispatcherThreadIndex = previousRelayDispatcherThreadIndex;
			g_tls_dependencyProbeRelayParentWaitingListOrdinal = previousRelayParentWaitingListOrdinal;
			g_tls_dependencyProbeRelayConsumerUsedKernelWait = previousRelayConsumerUsedKernelWait;
			g_tls_dependencyProbeRelayParentDebugUserData = previousRelayParentDebugUserData;
			g_tls_dependencyProbeRelayPublicationKey = previousRelayPublicationKey;
			g_tls_dependencyProbeReleasingFromRelay = previousReleasingFromRelay;
			g_tls_dependencyProbeReleasePriority = previousProbeReleasePriority;
			g_tls_dependencyProbeCompletedJobTicks = previousCompletedJobTicks;
	#endif

			--optContinuationContext->inlineRelayDepth;
			optContinuationContext->releasePriority = previousReleasePriority;
			return;
		}

		if ( isDependencyRelay )
		{
	#if defined(VG_BUILD_PROFILE)
			m_dependencyRelaysQueuedFallback.Increment();
	#endif
		}

	#if defined(VG_BUILD_PROFILE)

			if ( entry.dependencyProbeRole != JobQueueEntry::DependencyProbeRole::None )
			{
				const Uint32 producerThreadIndex = helper::GetDependencyProbeThreadIndex();
				if ( producerThreadIndex < helper::c_dependencyProbeThreadCount )
				{
					entry.dependencyProbePublicationKey =
						( static_cast<Uint64>( producerThreadIndex ) << 32u ) | static_cast<Uint64>( ++g_tls_dependencyProbePublicationOrdinal );
				}
				entry.dependencyProbeQueueDepthAtEnqueue = m_dispatcherQueue.GetApproximateDepth( priority );
			entry.dependencyProbeQueuedTicks = red::ProfileTimer::GetTicks();
			entry.dependencyProbeQueueSetupTicks = entry.dependencyProbeQueuedTicks - dependencyProbeQueueSetupStartTicks;
			if ( entry.dependencyProbeRole == JobQueueEntry::DependencyProbeRole::RelayReleasedChild &&
				entry.dependencyProbeReleaseCascadePublicationOrdinal != UINT32_MAX )
			{
				helper::DependencyProbeValueMetric depthMetric;
				if ( entry.dependencyProbeReleaseCascadePublicationOrdinal == 0 )
				{
					depthMetric = helper::DependencyProbeValueMetric::ChildQueueDepthCascadePublicationFirst;
				}
				else if ( entry.dependencyProbeReleaseCascadePublicationOrdinal <= 3 )
				{
					depthMetric = helper::DependencyProbeValueMetric::ChildQueueDepthCascadePublication2To4;
				}
				else if ( entry.dependencyProbeReleaseCascadePublicationOrdinal <= 7 )
				{
					depthMetric = helper::DependencyProbeValueMetric::ChildQueueDepthCascadePublication5To8;
				}
				else
				{
					depthMetric = helper::DependencyProbeValueMetric::ChildQueueDepthCascadePublication9Plus;
				}
				helper::RecordDependencyProbeValue( depthMetric, entry.dependencyProbeQueueDepthAtEnqueue );
			}
		}
#endif

	const Bool useLocalQueue = entry.jobDecl.hint == JobHint::Trivial && optLocalQueue && optLocalQueue->Size() < c_localQueueDefaultCapacity;
	if (false)//useLocalQueue)
	{
		optLocalQueue->PushBack(std::make_pair(entry, priority));
	}
	else
	{
		for (;;)
		{
#if defined(VG_BUILD_PROFILE)
			DispatcherQueuePushProfile pushProfile;
			DispatcherQueuePushProfile* const optPushProfile = entry.dependencyProbeRole != JobQueueEntry::DependencyProbeRole::None ? &pushProfile : nullptr;
			if (m_dispatcherQueue.TryPush(entry, priority, affinity, c_dispatcherQueuePushSpinCount, optPushProfile))
			{
				if ( optPushProfile )
				{
					helper::RecordDependencyProbeDelta( helper::DependencyProbeStage::GlobalQueuePush, pushProfile.queuePushTicks );
					helper::RecordDependencyProbeDelta( helper::DependencyProbeStage::GlobalSemaphoreRelease, pushProfile.semaphoreReleaseTicks );
					helper::RecordDependencyProbeDelta(
						pushProfile.numThreadsWoken > 0 ? helper::DependencyProbeStage::GlobalSemaphoreReleaseWake : helper::DependencyProbeStage::GlobalSemaphoreReleaseNoWake,
						pushProfile.semaphoreReleaseTicks );
					if ( entry.dependencyProbeRole == JobQueueEntry::DependencyProbeRole::Relay )
					{
						helper::RecordDependencyProbeDelta(
							pushProfile.numThreadsWoken > 0 ? helper::DependencyProbeStage::RelaySemaphoreReleaseWake
								: helper::DependencyProbeStage::RelaySemaphoreReleaseNoWake,
							pushProfile.semaphoreReleaseTicks );
					}
						else if ( entry.dependencyProbeRole == JobQueueEntry::DependencyProbeRole::RelayReleasedChild )
						{
						helper::RecordDependencyProbeDelta(
							pushProfile.numThreadsWoken > 0 ? helper::DependencyProbeStage::ChildSemaphoreReleaseWake
								: helper::DependencyProbeStage::ChildSemaphoreReleaseNoWake,
								pushProfile.semaphoreReleaseTicks );
						}
						helper::RecordDependencyProbePublicationEvent( entry, pushProfile );
					}
				break;
			}
#else
			if (m_dispatcherQueue.TryPush(entry, priority, affinity))
			{
				break;
			}
#endif
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

void Dispatcher::InitWithDependencyRelayJob( JobDecl& outJob )
{
	outJob.jobFunc = &helper::DependencyRelayJob;
	outJob.hint = JobHint::Trivial;
	outJob.instrumentationObject = &helper::s_dependencyRelayJobInstrumentationObject;
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
			// Keep this check unfiltered. Restricting it to PM_QS_SENDMESSAGE services sent messages, but does not
			// count as a general queue check for Win32 responsiveness while the main thread spins in FlushCounter.
			// PM_NOREMOVE preserves ownership of queued input/paint for the normal application pump.
			PeekMessage( &msg, nullptr, 0, 0, PM_NOREMOVE | PM_NOYIELD );
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
				if ( entry.kind != JobQueueEntry::Kind::Job )
				{
					DoRunJobQueueEntry( entry, 0, priority, *m_jobScopeAllocator, nullptr );
					continue;
				}

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

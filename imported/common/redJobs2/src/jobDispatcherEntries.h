/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redContainers/include/circularBuffer.h"
#include "jobMemoryPools.h"
#include "jobDecl.h"

namespace job {

class Dispatcher;
struct StackTraceCacheEntryPath;

namespace prv
{

class CounterEntry;

struct WaitingListEntry : red::NonCopyable
{
	RED_USE_MEMORY_POOL( PoolJobs2WaitingListEntries );

	// Dynarray adapter; never resized just for mempool visibility
	WaitingListEntry( WaitingListEntry&& ) { RED_FATAL( "Dynarray adapter; not actually movable" ); }
	void operator=( WaitingListEntry&& ) { RED_FATAL( "Dynarray adapter; not actually movable" ); }
	WaitingListEntry() = default;

	JobDecl job{};
	CounterEntry* accumulateCounterEntry{ nullptr };
	WaitingListEntry* next{ nullptr };
	const StackTraceCacheEntryPath* debugTrace{ nullptr };
};

static_assert( sizeof( WaitingListEntry ) <= 64, "Unexpected WaitingListEntry size" );
#ifdef RED_MEMORY_ENABLE_HOOKS
static_assert( sizeof( WaitingListEntry ) <= ( 64 - 8 ), "Unexpected WaitingListEntry size" );
#endif

struct JobQueueEntry
{
	enum class Kind : Uint8
	{
		Job,
		DependencyRelay,
		RecruitmentControl,
	};

	JobDecl jobDecl{};
	CounterEntry* accumulateCounterEntry{ nullptr };
	Kind kind{ Kind::Job };

#if defined(VG_BUILD_PROFILE)
		enum class DependencyProbeRole : Uint8
		{
			None,
			Relay,
			RelayReleasedChild,
			RecruitmentHint,
		};

		Uint64 dependencyProbeQueuedTicks{ 0 };
		Uint64 dependencyProbeOriginTicks{ 0 };
		Uint64 dependencyProbeReleaseOriginTicks{ 0 };
		Uint64 dependencyProbePoppedTicks{ 0 };
		Uint64 dependencyProbeConsumerAcquireEndTicks{ 0 };
		const void* dependencyProbeParentDebugUserData{ nullptr };
		const void* dependencyProbeChildDebugUserData{ nullptr };
		Uint64 dependencyProbePublicationKey{ 0 };
		Uint64 dependencyProbeOriginRelayPublicationKey{ 0 };
		Uint64 dependencyProbeRelayQueuedTicks{ 0 };
		Uint64 dependencyProbeRelayPoppedTicks{ 0 };
		Uint64 dependencyProbeRelayConsumerAcquireEndTicks{ 0 };
		Uint64 dependencyProbeRelayStartTicks{ 0 };
		Uint64 dependencyProbeParentBodyToCounterDecrementTicks{ 0 };
	Uint64 dependencyProbeParentCounterDecrementTicks{ 0 };
		Uint64 dependencyProbeParentWaitingListFlushTicks{ 0 };
		Uint64 dependencyProbeParentCounterRefReleaseTicks{ 0 };
		Uint64 dependencyProbeParentRefReleaseToQueueCallTicks{ 0 };
		Uint64 dependencyProbeQueueSetupTicks{ 0 };
	Uint64 dependencyProbeConsumerAcquireTicks{ 0 };
	Uint64 dependencyProbeConsumerQueueScanTicks{ 0 };
		Uint32 dependencyProbeQueueDepthAtEnqueue{ 0 };
		Uint32 dependencyProbeConsumerScanPasses{ 0 };
		Uint32 dependencyProbeParentWaitingListOrdinal{ 0 };
			Uint32 dependencyProbeReleaseCascadePublicationOrdinal{ UINT32_MAX };
			Uint32 dependencyProbeProducerRunnableDepthAtRelayDecision{ UINT32_MAX };
			Uint32 dependencyProbeRelayQueueDepthAtEnqueue{ 0 };
			Uint32 dependencyProbeRelayDispatcherThreadIndex{ UINT32_MAX };
			Uint32 dependencyProbeRelayParentWaitingListOrdinal{ 0 };
			Bool dependencyProbeRelayConsumerUsedKernelWait{ false };
			Bool dependencyProbeConsumerUsedKernelWait{ false };
		DependencyProbeRole dependencyProbeRole{ DependencyProbeRole::None };
#endif
};

const Uint32 c_localQueueDefaultCapacity = 256;

using TLocalQueue = red::CircularBuffer< std::pair<JobQueueEntry, Priority> >;

struct ParallelForSharedCounterEntry : red::NonCopyable
{
	RED_USE_MEMORY_POOL( PoolJobs2ParallelForSharedCounterEntries );

	// Dynarray adapter; never resized just for mempool visibility
	ParallelForSharedCounterEntry( ParallelForSharedCounterEntry&& ) { RED_FATAL( "Dynarray adapter; not actually movable" ); }
	void operator=( ParallelForSharedCounterEntry&& ) { RED_FATAL( "Dynarray adapter; not actually movable" ); }
	ParallelForSharedCounterEntry() = default;
	red::Atomic< Uint32 > counter{ 0 };
};

static_assert( sizeof( ParallelForSharedCounterEntry ) <= 64, "Unexpected ParallelForSharedCounterEntry size" );
#ifdef RED_MEMORY_ENABLE_HOOKS
static_assert( sizeof( ParallelForSharedCounterEntry) <= ( 64 - 8 ), "Unexpected ParallelForSharedCounterEntry size" );
#endif

struct ParallelForJobEntry
{
	RED_USE_MEMORY_POOL( PoolJobs2ParallelForJobEntries );

	JobDeclParallelFor::TJobFunc* jobFunc{ nullptr };
	JobDeclParallelFor::TEpilogueFunc* epilogueFunc{ nullptr };
	void* sharedData{ nullptr };
	void* elements{ nullptr };
	ParallelForSharedCounterEntry* sharedCounterEntry{ nullptr };
	Uint32 numElements{ 0 };
	Uint32 teamSize{ 0 };
	Uint32 teamIndex{ 0 };
	Uint32 maxBatchSize{ 0 };
};

static_assert( sizeof( ParallelForJobEntry ) <= 64, "Unexpected ParallelForJobEntry size" );
#ifdef RED_MEMORY_ENABLE_HOOKS
static_assert( sizeof( ParallelForJobEntry ) <= ( 64 - 8 ), "Unexpected ParallelForJobEntry size");
#endif

} } // job/prv

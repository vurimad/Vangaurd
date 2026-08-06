/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "redIOCommon.h"
#include "../../../common/redMemory/include/sharedPtr.h"
#include "redIOAsyncReadToken.h"

namespace io
{

struct RuntimeIOMemoryMetrics
{
	RuntimeIOMemoryMetrics()
		: m_totalBytesAllocated(0)
		, m_peakTotalBytesAllocated(0)
		, m_maxRequestBytes(0)
		, m_systemMemoryConsumed(0)
		, m_numberGuardBytes(0)
		, m_memoryBudgetTotal(0)
		, m_numAllocsInUse(0)
	{
	}

	// #fixme: more from red::memory
	Uint64 m_totalBytesAllocated;
	Uint64 m_peakTotalBytesAllocated;
	Uint64 m_maxRequestBytes;
	Uint64 m_systemMemoryConsumed;
	Uint64 m_numberGuardBytes;
	Uint64 m_memoryBudgetTotal;
	Uint64 m_numAllocsInUse;
};

struct AsyncOpRequestStats
{
	Double createdAt;
};

struct AsyncOpStats
{
	String typeName;
	Uint32 opCount;

	Uint32 opFinishedCountTotal;
	Double opFinishedTimeTotal;

	Uint32 opCanceledCountTotal;
	Double opCanceledTimeTotal;

	Uint64 bytesReadTotal;
	red::SharedPtr< red::RWSpinLock, red::PoolDebug > lock = red::CreateSharedPtr< red::RWSpinLock, red::PoolDebug >();
	red::Map< Uint32, AsyncOpRequestStats > opPending{ red::PoolDebug() };
};

struct SortByBytesTotalRead
{
	RED_INLINE Bool operator()( const AsyncOpStats& entry1, const AsyncOpStats& entry2 ) const
	{
		return entry1.opCount != entry2.opCount ? entry1.opCount > entry2.opCount : entry1.bytesReadTotal > entry2.bytesReadTotal;
	}
};

struct AsyncIOStats
{
    using QueueString = red::StaticArray<Uint8, 512>;
    using ReadyOpsString = red::StaticArray<Uint8, 128>;
	AsyncIOStats()
		: bytesReadTotal(0)
		, numAsyncOpsInFlight()
		, numAsyncOpsQueued()
	{}

	RuntimeIOMemoryMetrics memoryMetrics;
	Uint64 bytesReadTotal;
	Uint32 numAsyncOpsInFlight[eAsyncPriority_COUNT];
	Uint32 numAsyncOpsQueued[eAsyncPriority_COUNT];
    QueueString gameQueueString;
    ReadyOpsString readyOpsString;
};

struct AsyncIOStatsAvailableInFinal
{
	Uint32 numTotalAsyncOpsQueued;
};

}

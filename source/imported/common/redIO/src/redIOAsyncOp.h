/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redIOCommon.h"
#include "redIOProfilerInterface.h"
#include "redIOAsyncReadToken.h"

namespace io
{

namespace prv
{
	struct AsyncOp
	{
		AsyncReadToken		m_asyncReadToken;
		Uint64				m_ticket;
		Uint32				m_asyncFileHandle;
#ifdef RED_PROFILE_FILE_SYSTEM
		Uint32				m_operationId;
#endif

        Uint16              m_secondPassSortKey = 0;
        Uint8               m_critical = 0;
        EAsyncPriority		m_priority{ eAsyncPriority_COUNT };

		// #TODO: async op was patched out of the queue and moved elsewhere. E.g., fixing priority inversion.
		// This op should be silently discarded.
		struct Flags
		{
			Flags()
				: m_tombstoneMarker(false)
				, m_cachedIsCancelled(false)
			{}

			Bool				m_tombstoneMarker : 1;

			Bool				m_cachedIsCancelled : 1; // cache async cancellation results for things like sorting purposes, where you can't have values changing mid-sort
			Bool : 0;
		};

		Flags m_flags;
	};

	struct ReadyAsyncOp
	{
		AsyncOp				m_internalAsyncOp;
		io::ShareableIOMemory m_internalMemoryForIO;
		red::UniqueBuffer	m_internalMemoryForDecompression;

		static const Uint64 c_invalidMergedAsyncOpId = UINT64_MAX;
		Uint64 m_mergedAsyncOpID{ c_invalidMergedAsyncOpId };
	};

	struct FillBuffers
	{
		red::CircularBuffer< prv::ReadyAsyncOp > readyAsyncOps{ red::PoolEngine() };
		red::CircularBuffer< prv::ReadyAsyncOp > mergedAsyncOps{ red::PoolEngine() };
		red::CircularBuffer< prv::ReadyAsyncOp > cachedAsyncOps{ red::PoolEngine() };
		red::CircularBuffer< prv::ReadyAsyncOp > cancelledAsyncOps{ red::PoolEngine() };

		Bool HasWork() const
		{
			return 
				readyAsyncOps.Size() > 0 ||
				mergedAsyncOps.Size() > 0 ||
				cachedAsyncOps.Size() > 0 ||
				cancelledAsyncOps.Size() > 0;
		}
	};

	using FillAsyncOpsFunc = void(void* context, Uint32 maxReadyAsyncOps, FillBuffers& fillBuffers);
	using FillAudioAsyncOpsFunc = void(void* context, FillBuffers& fillBuffers, Uint32 maxReadyAsyncOps);

	namespace helper
	{
		RED_INLINE 
		prv::ReadyAsyncOp PopFront(red::CircularBuffer<prv::ReadyAsyncOp>& buffer)
		{
			auto temp = std::move(buffer.Front());
			buffer.PopFront();
			return temp;
		}
	}

	struct StatsNumAsyncOpsInFlight
	{
		Uint32 value[eAsyncPriority_COUNT] = {};
	};
}

}
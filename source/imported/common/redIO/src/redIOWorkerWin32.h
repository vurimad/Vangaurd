/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redIOCommon.h"

#include "../../redSystem/include/redThreadsThread.h"
#include "../../redContainers/include/staticArray.h"
#include "../../redContainers/include/circularBuffer.h"
#include "redIOAsyncOp.h"
#include "../redContainers/include/fixedArray.h"

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )

namespace io
{

class AsyncFileHandleCache;

namespace prv
{

struct IOWorkerWin32Setup
{
	FillAsyncOpsFunc* fillAsyncOpsFunc{ nullptr };
	void* context{ nullptr };
	AsyncFileHandleCache* asyncFileHandleCache{ nullptr };
	red::Atomic< Bool >* shutdownFlag{ nullptr };
};

class IOWorkerWin32 final : public red::Thread
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	explicit IOWorkerWin32( const IOWorkerWin32Setup& setup );
	virtual ~IOWorkerWin32();

	virtual void ThreadFunc() override;

	red::ManualResetEvent& GetWakeEvent() const;

	Uint64 GetStatsBytesReadTotal() const { return const_cast<volatile Uint64&>(m_statsBytesReadTotal); }
	StatsNumAsyncOpsInFlight GetStatsNumAsyncOpsInFlight() const;

	void EnableLoadingMode(Bool value);
	Bool IsLoadingMode() const { return m_loadingMode.GetValue(); }

	void EnableSavingMode(Bool value);

	void HACK_ThrottleStreamingForCutsceneAudio(Bool throttle);
	Bool HACK_IsThrottleStreamingForCutsceneAudio() const;

	void HACK_AudioThreadStreamingForCutsceneAudio(Bool throttle);
	Bool HACK_IsAudioThreadStreamingForCutsceneAudio() const;

	struct MergedAsyncOps
	{
		red::DynArray< ReadyAsyncOp > m_asyncOps{ red::PoolEngine() };
	};

	struct TempProcessingAsyncOps
	{
		red::DynArray< ReadyAsyncOp > m_asyncOps{ red::PoolEngine() };
	};

private:
	void ProcessAio();
	void SubmitNewAioRequests();
	void FinishAllReadyAioRequests();
	Bool FinishNextReadyAioRequest( Bool wait );
	void FillAsyncOpsBuffer();
	void UpdateNumAsyncOpsInFlight();

	static const Uint32 c_maxRequests = 64; //#tbd: find out how many we can support
	static const Uint32 c_queueDrainLimit = 16;
	static_assert(c_queueDrainLimit <= c_maxRequests, "");

	IOWorkerWin32Setup m_setup;
	OVERLAPPED m_overlapped[ c_maxRequests ];
	red::StaticArray< OVERLAPPED*, c_maxRequests > m_overlappedPool;

	struct PendingState
	{
		red::StaticArray< OVERLAPPED*, c_maxRequests > overlapped;
		red::StaticArray< ReadyAsyncOp, c_maxRequests > asyncOps;
		red::BitSet64< c_maxRequests > cancelledMask;
	};

	PendingState m_pendingState;

	FillBuffers m_fillBuffers;
	MergedAsyncOps m_mergedAsyncOpsBuffer;
	TempProcessingAsyncOps m_tempProcessingAsyncOps;

	mutable red::ManualResetEvent m_wakeEvent;

	StatsNumAsyncOpsInFlight m_statsNumAsyncOpsInFlight;
	Uint64 m_statsBytesReadTotal;
	red::Atomic<Bool> m_loadingMode{ false };
	red::Atomic<Bool> m_HACK_throttleStreamingForCutsceneAudio{ false };
	red::Atomic<Bool> m_HACK_audioThreadStreamingForCutsceneAudio{ false };
};

}
}

#endif
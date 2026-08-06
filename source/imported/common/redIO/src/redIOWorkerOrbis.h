/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redIOCommon.h"

#ifdef RED_PLATFORM_ORBIS

#include <kernel.h>

#include "../../redSystem/include/redThreadsThread.h"
#include "../../redContainers/include/dynArray.h"
#include "../../redContainers/include/staticArray.h"
#include "../../redContainers/include/circularBuffer.h"
#include "redIOAsyncOp.h"

namespace io
{

class AsyncFileHandleCache;

namespace prv
{
	
struct IOWorkerOrbisSetup
{
	FillAsyncOpsFunc* fillAsyncOpsFunc{ nullptr };
	FillAudioAsyncOpsFunc* fillAudioAsyncOpsFunc{ nullptr };
	void* context{ nullptr };
	AsyncFileHandleCache* asyncFileHandleCache{ nullptr };
	red::Atomic< Bool >* shutdownFlag{ nullptr };
};

class IOWorkerOrbis final : public red::Thread
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	explicit IOWorkerOrbis( const IOWorkerOrbisSetup& setup );
	virtual ~IOWorkerOrbis();

	virtual void ThreadFunc() override;

	red::ManualResetEvent& GetWakeEvent() const { return m_wakeEvent; }

	Uint64 GetStatsBytesReadTotal() const;
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
	void FillAsyncOpsBuffer();
	void ProcessAio();
	void FillAudioAsyncOpsBuffer();
	// #TBD: possibly should change some sceAio scheduling params to adjust to half-max, but may just make things worse right now
	static const Uint32 c_maxAudioRequests = 16;
	static const Uint32 c_maxRequests = SCE_KERNEL_AIO_REQUEST_NUM_MAX - c_maxAudioRequests;
	static const Uint32 c_queueDrainLimit = 16;
	static_assert(c_queueDrainLimit <= c_maxRequests, "");

	struct PendingState
	{
		red::StaticArray< SceKernelAioRWRequest, c_maxRequests > requests;
		red::StaticArray< ReadyAsyncOp, c_maxRequests > asyncOps;
		red::StaticArray< SceKernelAioSubmitId, c_maxRequests > aioSubmitIds;
		red::BitSet64< c_maxRequests > cancelledMask;
	};

	struct IOState
	{
		PendingState m_pendingState;
		FillBuffers m_fillBuffers;
		MergedAsyncOps m_mergedAsyncOps;
		TempProcessingAsyncOps m_tempProcessingAsyncOps;
		StatsNumAsyncOpsInFlight m_statsNumAsyncOpsInFlight;
		red::StaticArray< SceKernelAioResult*, c_maxRequests > m_resultsPool;
		SceKernelAioResult m_aioResults[ c_maxRequests ];
	};

	void FinishReadyAioRequests( IOState &ioState );
	void SubmitNewAioRequests( IOState &ioState) ;
	void UpdateNumAsyncOpsInFlight( IOState &ioState );

	IOState m_ioState;
	IOState m_audioIOState;
	IOWorkerOrbisSetup m_setup;
	Uint64 m_statsBytesReadTotal;

	mutable red::ManualResetEvent m_wakeEvent;

	red::Atomic<Bool> m_loadingMode{ false };
	red::Atomic<Bool> m_savingMode{ false };
	red::Atomic<Bool> m_HACK_throttleStreamingForCutsceneAudio{ false };
	red::Atomic<Bool> m_HACK_audioThreadStreamingForCutsceneAudio{ false };
};

}
}

#endif // RED_PLATFORM_ORBIS
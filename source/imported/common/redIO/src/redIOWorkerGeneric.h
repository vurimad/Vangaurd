/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redIOCommon.h"

#include "../../redSystem/include/redThreadsThread.h"
#include "../../redContainers/include/circularBuffer.h"
#include "redIOAsyncOp.h"
#include "../redContainers/include/fixedArray.h"
#include "redIOAsyncReadToken.h"

namespace io
{

class AsyncFileHandleCache;

namespace prv
{

struct IOWorkerGenericSetup
{
	FillAsyncOpsFunc* fillAsyncOpsFunc{ nullptr };
	void* context{ nullptr };
	AsyncFileHandleCache* asyncFileHandleCache{ nullptr };
	red::Atomic< Bool >* shutdownFlag{ nullptr };
};

class IOWorkerGeneric final : public red::Thread
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	explicit IOWorkerGeneric( const IOWorkerGenericSetup& setup );
	virtual ~IOWorkerGeneric();

	virtual void ThreadFunc() override;
	
	red::ManualResetEvent& GetWakeEvent() const;

	Uint64 GetStatsBytesReadTotal() const;
	StatsNumAsyncOpsInFlight GetStatsNumAsyncOpsInFlight() const;

	void EnableLoadingMode(Bool value);
	Bool IsLoadingMode() const { return m_isLoadingMode.GetValue(); }
	void EnableSavingMode(Bool value);
	void HACK_ThrottleStreamingForCutsceneAudio(Bool throttle);
	Bool HACK_IsThrottleStreamingForCutsceneAudio() const;

	void HACK_AudioThreadStreamingForCutsceneAudio(Bool throttle);
	Bool HACK_IsAudioThreadStreamingForCutsceneAudio() const;

private:
	void ProcessAsyncOp( ReadyAsyncOp& readyAsyncOp );
	void ProcessBeginRead( ReadyAsyncOp&& readyAsyncOp );
	void FillAsyncOpBuffers();

	IOWorkerGenericSetup m_setup;

	FillBuffers m_fillBuffers;
	red::DynArray< prv::ReadyAsyncOp > m_mergedAsyncOpsBuffer{ red::PoolEngine() };
	red::DynArray< prv::ReadyAsyncOp > m_tempProcessingAsyncOps{ red::PoolEngine() };

	mutable red::ManualResetEvent m_wakeEvent;
	Uint64 m_statsBytesReadTotal;
	StatsNumAsyncOpsInFlight m_statsNumAsyncOpsInFlight;
	red::Atomic<Bool> m_isLoadingMode{ false };
	red::Atomic<Bool> m_HACK_throttleStreamingForCutsceneAudio{ false };
	red::Atomic<Bool> m_HACK_audioThreadStreamingForCutsceneAudio{ false };
};

} // prv
} // io

/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "redIOWorkerOrbis.h"

#ifdef RED_PLATFORM_ORBIS

#include "../../redCore/include/profiler.h"
#include "redIOFile.h"
#include "redIOAsyncFileHandleCache.h"
#include "redIOAsyncIO.h"

#include <razorcpu.h>

#ifdef RED_FATAL_ASSERT
#undef RED_FATAL_ASSERT
#endif

#ifdef RED_FATAL
#undef RED_FATAL
#endif

#define RED_FATAL_ASSERT ALWAYSENABLED_RED_FATAL_ASSERT
#define RED_FATAL ALWAYSENABLED_RED_FATAL

#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
#define QUICK_N_DIRTY_STATS
#endif

namespace io
{

extern const Uint32 c_HACK_maxAsyncOpsThrottleStreamingForCutsceneAudio;
extern const Uint32 c_uncacheableMemoryTag;

// FIXME: should return a struct, but then I'll have to recompile everything!!! And clang is sloooow.
#ifdef QUICK_N_DIRTY_STATS
	static Int32 GNumStreamingLoading{ 0 };
	static Int32 GMaxStreamingDistanceLoading{ -INT_MAX };
	static Int32 GMinStreamingDistanceLoading{ INT_MAX };
	static Int32 GNumProxiesLoading{ 0 };
	static Int32 GNumOtherLoading{ 0 };
	static Int32 GNumIOContextlessLoading{ 0 };
	static Int64 GNumStreamingOpsTotal{ 0 };
	static Int64 GNumStreamingOpsCancelled{ 0 };
	static Int64 GNumStreamingOpsPrematurelyPopped{ 0 };
	static Bool GStreamingStalled{ false };

REDIO_API void GetStreamingDistanceLoading(
	Int32& outNumStreamingLoading,
	Int32& outMinDistance,
	Int32& outMaxDistance,
	Bool& outStreamingStalled,
	Int32& outNumProxiesLoading,
	Int32& outNumOtherLoading,
	Int32& outNumContextlessLoading,
	Int64& outNumStreamingOpsTotal,
	Int64& outNumStreamingOpsCancelled,
	Int64& outNumStreaingOpsPrematurelyPopped
)
{
	outNumStreamingLoading = GNumStreamingLoading;
	outMinDistance = GMinStreamingDistanceLoading;
	outMaxDistance = GMaxStreamingDistanceLoading;
	outStreamingStalled = GStreamingStalled;
	outNumProxiesLoading = GNumProxiesLoading;
	outNumOtherLoading = GNumOtherLoading;
	outNumContextlessLoading = GNumIOContextlessLoading;
	outNumStreamingOpsTotal = GNumStreamingOpsTotal;
	outNumStreamingOpsCancelled = GNumStreamingOpsCancelled;
	outNumStreaingOpsPrematurelyPopped = GNumStreamingOpsPrematurelyPopped;
}
#endif

extern thread_local bool GIsCallbackThread;

namespace prv
{

constexpr red::TAffinityMask c_threadAffinityMask = ( 1ULL << 4 | 1ULL << 5 );

namespace helper
{
	static const char* GetThreadName()
	{
		return "redIOWorker";
	}

	static const char* GetPlotNameNumNewSubmitRequestsStarted()
	{
		return "IOWorker: NumSubmitRequestsStarted";
	}
	
	static const char* GetPlotNameNumSubmitRequestsWaiting()
	{
		return "IOWorker: NumSubmitRequestsWaiting";
	}

	static const char* GetPlotNameNumSubmitRequestsFinished()
	{
		return "IOWorker: NumSubmitRequestsFinished";
	}

	static int GetAioPriority( Bool savingMode )
	{
		return savingMode ? SCE_KERNEL_AIO_PRIORITY_LOW : SCE_KERNEL_AIO_PRIORITY_MID;
	}

	static void InitAioRequest( SceKernelAioRWRequest& outRequest, AsyncFile* asyncFile, const AsyncOp& asyncOp, SceKernelAioResult* result )
	{
		outRequest.buf = asyncOp.m_asyncReadToken.m_buffer;
		outRequest.fd = asyncFile->GetFileDescriptor();
		outRequest.nbyte = asyncOp.m_asyncReadToken.m_numberOfBytesToRead;
		outRequest.offset = asyncOp.m_asyncReadToken.m_offset;
		outRequest.result = result;
	}

	static void ExtractMergedAsyncOps(Uint64 mergedAsyncOpID, IOWorkerOrbis::MergedAsyncOps& mergedAsyncOps, IOWorkerOrbis::TempProcessingAsyncOps& dest)
	{
		auto& mergedOps = mergedAsyncOps.m_asyncOps;

		Uint32 i = 0;
		while (i < mergedOps.Size())
		{
			if (mergedOps[i].m_mergedAsyncOpID == mergedAsyncOpID)
			{
				dest.m_asyncOps.PushBack(std::move(mergedOps[i]));
				mergedOps.RemoveAt(i);
			}
			else
			{
				i += 1;
			}
		}
	}

	template<typename TFunc>
	void ForEachAsyncOp(red::DynArray<ReadyAsyncOp>& asyncOps, TFunc func)
	{
		for (auto& asyncOp : asyncOps)
		{
			func(asyncOp);
		}
	}

	class ScopedCleanupAsyncOps
	{
	public:
		ScopedCleanupAsyncOps(AsyncFileHandleCache* asyncFileHandleCache, red::DynArray<ReadyAsyncOp>& asyncOps)
			: m_asyncFileHandleCache(asyncFileHandleCache)
			, m_asyncOps(asyncOps)
		{}

		~ScopedCleanupAsyncOps()
		{
			helper::ForEachAsyncOp(m_asyncOps, [this](const auto& asyncOp) {
				const Uint32 asyncFileHandle = asyncOp.m_internalAsyncOp.m_asyncFileHandle;
				m_asyncFileHandleCache->Release(asyncFileHandle);
			});
			m_asyncOps.Clear();
		}

	private:
		AsyncFileHandleCache* m_asyncFileHandleCache;
		red::DynArray<ReadyAsyncOp>& m_asyncOps;
	};

	struct ProcessResultStats
	{
		Uint64 statsBytesReadTotal{ 0 };
		Uint32 numFinished{ 0 };
		Uint32 numCancelled{ 0 };
		Uint32 numError{ 0 };
	};

	static void ProcessResult( const SceKernelAioResult& aioResult, ReadyAsyncOp&& finishedAsyncOp, ProcessResultStats& outStats, AsyncFileHandleCache* asyncFileHandleCache, IOWorkerOrbis::MergedAsyncOps& mergedAsyncOps, IOWorkerOrbis::TempProcessingAsyncOps& tempProcessingAsyncOps)
	{
		const auto& activeToken = finishedAsyncOp.m_internalAsyncOp.m_asyncReadToken;

		auto asyncResult = red::eAsyncResult_Unknown;
		Uint32 numBytesRead = 0;

		if ( aioResult.state == SCE_KERNEL_AIO_STATE_ABORTED )
		{
			outStats.numCancelled += 1;
			asyncResult = red::eAsyncResult_Canceled;
		}
		else if ( aioResult.state == SCE_KERNEL_AIO_STATE_COMPLETED )
		{
			if ( aioResult.returnValue >= 0 )
			{
				RED_FATAL_ASSERT( aioResult.returnValue == static_cast<Int64>( activeToken.m_numberOfBytesToRead ), "Unexpected partial read" );
				numBytesRead = aioResult.returnValue;

				outStats.statsBytesReadTotal += numBytesRead;
				outStats.numFinished += 1;
				asyncResult = red::eAsyncResult_Success;
			}
			else
			{
				RED_FATAL( "SceKernelAioResult failed with error code: 0x016%llX", aioResult.returnValue );
				outStats.numError += 1;
				asyncResult = red::eAsyncResult_Error;
			}
		}

		RED_FATAL_ASSERT( asyncResult != red::eAsyncResult_Unknown, "Unexpected result state: %u", aioResult.state );
		RED_FATAL_ASSERT( asyncResult != red::eAsyncResult_Canceled || finishedAsyncOp.m_mergedAsyncOpID == ReadyAsyncOp::c_invalidMergedAsyncOpId, "No support for mergedAsyncOp cancellation");

		///---

		const Uint64 baseOffset = activeToken.m_offset;

		RED_FATAL_ASSERT(tempProcessingAsyncOps.m_asyncOps.Empty());
		if (finishedAsyncOp.m_mergedAsyncOpID != ReadyAsyncOp::c_invalidMergedAsyncOpId)
		{
			ExtractMergedAsyncOps(finishedAsyncOp.m_mergedAsyncOpID, mergedAsyncOps, tempProcessingAsyncOps);
		}
		else
		{
			tempProcessingAsyncOps.m_asyncOps.PushBack(std::move(finishedAsyncOp));
		}

		helper::ScopedCleanupAsyncOps scopedCleanup(asyncFileHandleCache, tempProcessingAsyncOps.m_asyncOps);

		for (auto& readyAsyncOp : tempProcessingAsyncOps.m_asyncOps)
		{
			const auto& asyncReadToken = readyAsyncOp.m_internalAsyncOp.m_asyncReadToken;
			auto callback = asyncReadToken.m_callback;

			io::ShareableIOMemory memoryForIO;
			red::UniqueBuffer memoryForDecompression;
			Uint32 shareableIOMemoryOffset = 0;
			Uint32 callbackNumberOfBytesRead = 0;
			if (asyncResult == red::eAsyncResult_Success)
			{
				RED_FATAL_ASSERT(asyncReadToken.m_offset >= 0 && (Uint64)asyncReadToken.m_offset >= baseOffset);
				RED_FATAL_ASSERT(numBytesRead >= asyncReadToken.m_numberOfBytesToRead);
				callbackNumberOfBytesRead = asyncReadToken.m_numberOfBytesToRead;
				shareableIOMemoryOffset = (Uint32)((Uint64)asyncReadToken.m_offset - baseOffset);
				memoryForIO = std::move(readyAsyncOp.m_internalMemoryForIO);
				memoryForDecompression = std::move(readyAsyncOp.m_internalMemoryForDecompression);
			}
			else if (asyncResult == red::eAsyncResult_Canceled)
			{
				//NOTE: would need to tag the main mergedOp if cancel becomes supported there.
				// I/O memory is allocated to ensure it's really at least sizeof(Uint32)
				Uint32* tag = (Uint32*)readyAsyncOp.m_internalMemoryForIO.Data();
				if (tag)
				{
					*tag = c_uncacheableMemoryTag;
				}
			}

			if ( callback )
			{
				callback(asyncReadToken, asyncResult, callbackNumberOfBytesRead, std::move(memoryForIO), shareableIOMemoryOffset, std::move(memoryForDecompression));
			}
		}
	}
}

IOWorkerOrbis::IOWorkerOrbis( const IOWorkerOrbisSetup& setup )
	: red::Thread( helper::GetThreadName(), red::ThreadMemParams( SCE_PTHREAD_STACK_MIN ) )
	, m_setup( setup )
	, m_statsBytesReadTotal( 0 )
{
	for ( auto& it : m_ioState.m_aioResults )
	{
		m_ioState.m_resultsPool.PushBack( &it );
	}

	for ( auto& it : m_audioIOState.m_aioResults )
	{
		m_audioIOState.m_resultsPool.PushBack( &it );
	}

#if 1
	SceKernelAioParam param{};
	sceKernelAioInitializeParam(&param); // init with defaults. Tweak as necessary to experiment.
    param.mid.enableSplit = SCE_KERNEL_AIO_DISABLE_SPLIT;

	const Int32 result = sceKernelAioInitialize(&param);
	ALWAYSENABLED_RED_FATAL_ASSERT(result == SCE_OK, "sceKernelAioInitialize failed: %u", result);
#endif
}

IOWorkerOrbis::~IOWorkerOrbis()
{
}

void IOWorkerOrbis::ThreadFunc()
{
	red::memory::RegisterCurrentThread( GetThreadName() );

	GIsCallbackThread = true;

	SetAffinityMask( c_threadAffinityMask );
	SetPriority( red::TP_TimeCritical );
	
	RED_FATAL_ASSERT( m_setup.shutdownFlag );

	for ( ;; )
	{
		m_wakeEvent.Wait();
		m_wakeEvent.ResetEvent();
		if ( m_setup.shutdownFlag->GetValue() )
		{
			break;
		}

		ProcessAio();		
	}
}

void IOWorkerOrbis::SubmitNewAioRequests( IOState &ioState )
{
	PC_SCOPE_FUNC();

	RED_FATAL_ASSERT(ioState.m_pendingState.requests.Size() + ioState.m_fillBuffers.readyAsyncOps.Size() <= ioState.m_pendingState.requests.Capacity(),
		"Internal error: m_asyncOpBuffer shouldn't collect more ops than can currently issue");

	while (!ioState.m_fillBuffers.mergedAsyncOps.Empty())
	{
		ioState.m_mergedAsyncOps.m_asyncOps.PushBack(helper::PopFront(ioState.m_fillBuffers.mergedAsyncOps));
	}

	const Uint32 numNewRequests = ioState.m_fillBuffers.readyAsyncOps.Size();
	while (!ioState.m_fillBuffers.readyAsyncOps.Empty())
	{
		ioState.m_pendingState.asyncOps.PushBack(helper::PopFront(ioState.m_fillBuffers.readyAsyncOps));

		const AsyncOp& asyncOp = ioState.m_pendingState.asyncOps.Back().m_internalAsyncOp;
		auto* result = ioState.m_resultsPool.PopBack();
		ioState.m_pendingState.requests.Grow( 1 );

		AsyncFile* asyncFile = m_setup.asyncFileHandleCache->GetAsyncFile( asyncOp.m_asyncFileHandle );
		RED_FATAL_ASSERT( asyncFile, "File does not exist! Should not have been open deferred on Orbis" );
		helper::InitAioRequest( ioState.m_pendingState.requests.Back(), asyncFile, asyncOp, result );
	}

#ifdef USE_RAZOR_PROFILER
	//sceRazorCpuPlotValue(helper::GetPlotNameNumNewSubmitRequestsStarted(m_setup.priority), numNewRequests);
#endif

	if ( numNewRequests > 0 )
	{
		int prio = helper::GetAioPriority( m_savingMode.GetValue() );
		const Uint32 newRequestsIndex = ioState.m_pendingState.requests.Size() - numNewRequests;
		ioState.m_pendingState.aioSubmitIds.Grow( numNewRequests );
		if (ioState.m_pendingState.asyncOps[newRequestsIndex].m_internalAsyncOp.m_asyncReadToken.m_requestSource == RequestSource::AudioSystem_WwiseLowLevelIO  )
		{
			prio = prio == SCE_KERNEL_AIO_PRIORITY_MID ? SCE_KERNEL_AIO_PRIORITY_HIGH : SCE_KERNEL_AIO_PRIORITY_LOW + 1;
		}
		const Int32 ret = sceKernelAioSubmitReadCommandsMultiple( &ioState.m_pendingState.requests[ newRequestsIndex ], numNewRequests, prio, &ioState.m_pendingState.aioSubmitIds[ newRequestsIndex ] );
		RED_FATAL_ASSERT( ret == SCE_OK, "sceKernelAioSubmitReadCommandsMultiple failed: 0x%08X", ret );

		UpdateNumAsyncOpsInFlight( ioState );
	}

	// #tbd: throttle the number of cancels, depending on how much submitted to the queue? Probably then need an emergency flush if the array grows too large.
	// Process cancels after kicking off new I/O above
	helper::ProcessResultStats unusedStats;
	while (!ioState.m_fillBuffers.cancelledAsyncOps.Empty())
	{
		SceKernelAioResult cancelledResult = {};
		cancelledResult.state = SCE_KERNEL_AIO_STATE_ABORTED;
		cancelledResult.returnValue = 0;
		helper::ProcessResult(cancelledResult, helper::PopFront(ioState.m_fillBuffers.cancelledAsyncOps), unusedStats, m_setup.asyncFileHandleCache, ioState.m_mergedAsyncOps, ioState.m_tempProcessingAsyncOps);
	}

	while (!ioState.m_fillBuffers.cachedAsyncOps.Empty())
	{
		auto cachedOp = helper::PopFront(ioState.m_fillBuffers.cachedAsyncOps);

		SceKernelAioResult result = {};
		result.state = SCE_KERNEL_AIO_STATE_COMPLETED;
		result.returnValue = cachedOp.m_internalAsyncOp.m_asyncReadToken.m_numberOfBytesToRead;
		helper::ProcessResult(result, std::move(cachedOp), unusedStats, m_setup.asyncFileHandleCache, ioState.m_mergedAsyncOps, ioState.m_tempProcessingAsyncOps);
	}
}

void IOWorkerOrbis::UpdateNumAsyncOpsInFlight( IOState &ioState)
{
	StatsNumAsyncOpsInFlight newStats;

	for (const auto& it : ioState.m_pendingState.asyncOps)
	{
		newStats.value[it.m_internalAsyncOp.m_priority] += 1;
	}
	ioState.m_statsNumAsyncOpsInFlight = newStats;
}


void IOWorkerOrbis::FinishReadyAioRequests( IOState &ioState )
{
	RED_FATAL_ASSERT( ioState.m_pendingState.aioSubmitIds.Size() == ioState.m_pendingState.requests.Size(), "Internal error: submitID/request array size mismatch" );

	if ( ioState.m_pendingState.requests.Empty() )
	{
		RED_FATAL_ASSERT( ioState.m_fillBuffers.readyAsyncOps.Empty(), "Internal error: Should have submitted some available asyncops" );
		return;
	}

#ifdef USE_RAZOR_PROFILER
	//sceRazorCpuPlotValue(helper::GetPlotNameNumSubmitRequestsWaiting(m_setup.priority), m_pendingState.requests.Size());
#endif

	// could move aioStates into member var... avoid possible chkstck...
	int cancelIds[ c_maxRequests ] = {};
	int aioStates[ c_maxRequests ] = {};

	Int32 ret = SCE_OK;

	Uint32 numToCancel = 0;

	for (Uint32 i : ioState.m_pendingState.asyncOps.Indices())
	{
		const auto& asyncOp = ioState.m_pendingState.asyncOps[i].m_internalAsyncOp;
		if (asyncOp.m_asyncReadToken.IsCancelRequested() && !ioState.m_pendingState.cancelledMask.Get(i))
		{
			ioState.m_pendingState.cancelledMask.Set(i);
			cancelIds[numToCancel++] = ioState.m_pendingState.aioSubmitIds[i];
		}
	}

	if (numToCancel > 0)
	{
		ret = sceKernelAioCancelRequests(cancelIds, numToCancel, aioStates);
		RED_FATAL_ASSERT(ret == SCE_OK, "sceKernelAioCancelRequests failed: 0x%08X", ret);
		
		// Don't care about the results here, will check them normally in sceKernelAioWaitRequests below
		// Clear mainly for debugging purposes
		red::Memzero(&aioStates, sizeof(aioStates));
	}

	SceKernelUseconds* infiniteTimeout = nullptr;
	ret = sceKernelAioWaitRequests( ioState.m_pendingState.aioSubmitIds.TypedData(), ioState.m_pendingState.requests.Size(), aioStates, SCE_KERNEL_AIO_WAIT_OR, infiniteTimeout );
	RED_FATAL_ASSERT( ret == SCE_OK, "sceKernelAioWaitRequests failed: 0x%08X", ret );

	// Profile after blocking wait.
	PC_SCOPE( FinishReadyAioRequests_PostWaitRequests );

	{
		const int prio = helper::GetAioPriority( m_savingMode.GetValue() );
		GAsyncIO.ProfileIOWorkerBegin( prio );
	}

	static_assert(c_maxRequests <= SCE_KERNEL_AIO_ID_NUM_MAX, "");
	red::StaticArray< SceKernelAioSubmitId, c_maxRequests > submitIdsToDelete;
	helper::ProcessResultStats procesResultStats;
	for ( Uint32 i : ioState.m_pendingState.requests.ReverseIndices() )
	{
		if ( aioStates[ i ] == SCE_KERNEL_AIO_STATE_COMPLETED || aioStates[ i ] == SCE_KERNEL_AIO_STATE_ABORTED )
		{
			helper::ProcessResult( *ioState.m_pendingState.requests[ i ].result, std::move(ioState.m_pendingState.asyncOps[ i ]), procesResultStats, m_setup.asyncFileHandleCache, ioState.m_mergedAsyncOps, ioState.m_tempProcessingAsyncOps);

			submitIdsToDelete.PushBack(ioState.m_pendingState.aioSubmitIds[i]);

			// put result back into pool
			ioState.m_resultsPool.PushBack( ioState.m_pendingState.requests[ i ].result );

			// cleanup
			RED_FATAL_ASSERT(ioState.m_pendingState.requests.Size() > 0);
			if (i != ioState.m_pendingState.requests.Size() - 1)
			{
				const Uint32 lastIndex = ioState.m_pendingState.requests.Size() - 1;
				const Bool val = ioState.m_pendingState.cancelledMask.Get(lastIndex);
				ioState.m_pendingState.cancelledMask.Clear(lastIndex);
				ioState.m_pendingState.cancelledMask.Set(i, val);
			}
			else
			{
				ioState.m_pendingState.cancelledMask.Clear( i );
			}
			ioState.m_pendingState.requests.RemoveAtReorder( i );
			ioState.m_pendingState.asyncOps.RemoveAtReorder( i );
			ioState.m_pendingState.aioSubmitIds.RemoveAtReorder( i );
		}
	}

	{
		//#tbd: why two ret vals...
		int uselessRets[c_maxRequests] = {};
		const int deleteRet = sceKernelAioDeleteRequests(submitIdsToDelete.TypedData(), submitIdsToDelete.Size(), uselessRets);
		RED_FATAL_ASSERT(deleteRet == SCE_OK, "sceKernelAioDeleteRequest failed: 0x%08X", deleteRet);
		for (Uint32 i = 0, len = submitIdsToDelete.Size(); i < len; ++i)
		{
			RED_FATAL_ASSERT(uselessRets[i] == SCE_OK, "sceKernelAioDeleteRequest failed: result 0x%08X", uselessRets[i]);
		}
	}

	{
		const int prio = helper::GetAioPriority( m_savingMode.GetValue() );
		GAsyncIO.ProfileIOWorkerEnd( prio );
	}

	m_statsBytesReadTotal += procesResultStats.statsBytesReadTotal;

	UpdateNumAsyncOpsInFlight(ioState);

#ifdef USE_RAZOR_PROFILER
	//sceRazorCpuPlotValue(helper::GetPlotNameNumSubmitRequestsFinished(m_setup.priority), procesResultStats.numFinished);
#endif
}

void IOWorkerOrbis::ProcessAio()
{
	for( ;; )
	{
		if (m_HACK_audioThreadStreamingForCutsceneAudio.GetValue())
		{
			FillAudioAsyncOpsBuffer();
		}
		FillAsyncOpsBuffer();

		Bool hasWork = false;
		Bool hasAudioWork = false;
		
		hasWork |= m_ioState.m_fillBuffers.HasWork();
		hasWork |= m_ioState.m_pendingState.asyncOps.Size() > 0;
		hasAudioWork |= m_audioIOState.m_fillBuffers.HasWork();
		hasAudioWork |= m_audioIOState.m_pendingState.asyncOps.Size() > 0;

		if( !hasWork && !hasAudioWork )
		{
			RED_FATAL_ASSERT(m_ioState.m_mergedAsyncOps.m_asyncOps.Empty(), "Sanity check - parent merged operation missing?!");
			RED_FATAL_ASSERT(m_audioIOState.m_mergedAsyncOps.m_asyncOps.Empty(), "Sanity check - parent merged operation missing?!");
			break;
		}

		if (hasAudioWork)
		{
			SubmitNewAioRequests( m_audioIOState );
			FinishReadyAioRequests( m_audioIOState );
		}

		if (hasWork)
		{
			SubmitNewAioRequests( m_ioState );
			FinishReadyAioRequests( m_ioState );
		}
	}
}

void IOWorkerOrbis::FillAsyncOpsBuffer()
{
	if (m_ioState.m_pendingState.requests.Size() > c_queueDrainLimit)
	{
		return;
	}

	auto fillFunc = m_setup.fillAsyncOpsFunc;
	auto context = m_setup.context;

	const Uint32 resolvedMaxRequests = m_HACK_throttleStreamingForCutsceneAudio.GetValue() ? c_HACK_maxAsyncOpsThrottleStreamingForCutsceneAudio : c_maxRequests;

	// For now don't even check if loading mode. 128 doesn't even seem to make any huge dent in loading screen times.
	// Possibly more useful if struggling to stream everything in, but more chance of something going wrong with the collision.
	const Uint32 numOpsToFill = Max<Int32>(0, (Int32)resolvedMaxRequests - (Int32)m_ioState.m_pendingState.asyncOps.Size());
	RED_FATAL_ASSERT(m_ioState.m_resultsPool.Size() >= numOpsToFill, "Internal error: insufficient result buffers for requests");

	RED_FATAL_ASSERT(!m_ioState.m_fillBuffers.HasWork(), "Async ops buffer should have been previously consumed");

	// Call the fill function
	(*fillFunc)(context, numOpsToFill, m_ioState.m_fillBuffers);
}

StatsNumAsyncOpsInFlight IOWorkerOrbis::GetStatsNumAsyncOpsInFlight() const
{
	StatsNumAsyncOpsInFlight stats;
	for (Uint32 i = 0; i < eAsyncPriority_COUNT; i++)
	{
		stats.value[i] = m_ioState.m_statsNumAsyncOpsInFlight.value[i] + m_audioIOState.m_statsNumAsyncOpsInFlight.value[i];
	}
	return stats;
}

void IOWorkerOrbis::EnableLoadingMode(Bool value)
{
	m_loadingMode.SetValue(value);
}

void IOWorkerOrbis::EnableSavingMode(Bool value)
{
	m_savingMode.SetValue(value);
}

void IOWorkerOrbis::FillAudioAsyncOpsBuffer()
{
	auto fillFunc = m_setup.fillAudioAsyncOpsFunc;
	auto context = m_setup.context;
	const Uint32 numOpsToFill = Max<Int32>(0, (Int32)c_maxAudioRequests- (Int32)m_audioIOState.m_pendingState.asyncOps.Size());
	(*fillFunc)(context, m_audioIOState.m_fillBuffers, numOpsToFill);
}

void IOWorkerOrbis::HACK_ThrottleStreamingForCutsceneAudio(Bool throttle)
{
	m_HACK_throttleStreamingForCutsceneAudio.SetValue(throttle);
}

Bool IOWorkerOrbis::HACK_IsThrottleStreamingForCutsceneAudio() const
{
	return m_HACK_throttleStreamingForCutsceneAudio.GetValue();
}

void IOWorkerOrbis::HACK_AudioThreadStreamingForCutsceneAudio(Bool throttle)
{
	m_HACK_audioThreadStreamingForCutsceneAudio.SetValue(throttle);
}

Bool IOWorkerOrbis::HACK_IsAudioThreadStreamingForCutsceneAudio() const
{
	return m_HACK_audioThreadStreamingForCutsceneAudio.GetValue();
}

Uint64 IOWorkerOrbis::GetStatsBytesReadTotal() const
{
	return const_cast<volatile Uint64&>(m_statsBytesReadTotal);
}

} // prv
} // io

#else
RED_NO_EMPTY_FILE();
#endif // RED_PLATFORM_ORBIS
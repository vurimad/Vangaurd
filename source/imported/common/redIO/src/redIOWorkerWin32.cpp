/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "redIOWorkerWin32.h"

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )

//#include "../../redCore/include/profiler.h"
#include "redIOFile.h"
#include "redIOAsyncFileHandleCache.h"

#ifdef RED_FATAL_ASSERT
#undef RED_FATAL_ASSERT
#endif

#ifdef RED_FATAL
#undef RED_FATAL
#endif

#define RED_FATAL_ASSERT ALWAYSENABLED_RED_FATAL_ASSERT
#define RED_FATAL ALWAYSENABLED_RED_FATAL

namespace io
{

extern thread_local bool GIsCallbackThread;
extern const Uint32 c_HACK_maxAsyncOpsThrottleStreamingForCutsceneAudio;
extern const Uint32 c_uncacheableMemoryTag;

namespace prv
{

const red::TAffinityMask c_threadAffinityMask = ( 1ULL << 4 | 1ULL << 5 );

#ifdef RED_PLATFORM_WINPC
// Assuming an SSD
constexpr Int64 c_maxFowardSeekDistance = INT64_MAX;
#else
constexpr Int64 c_maxFowardSeekDistance = RED_KILO_BYTE(64);
#endif

namespace helper
{
	static void InitOverlapped( OVERLAPPED* overlapped )
	{
		HANDLE hEvent = overlapped->hEvent;
		red::Memzero( overlapped, sizeof( *overlapped ) );
		overlapped->hEvent = hEvent;
		// Lazily create event
		if ( overlapped->hEvent == nullptr )
		{
			overlapped->hEvent = ::CreateEvent( nullptr, TRUE, FALSE, nullptr );
			RED_FATAL_ASSERT( overlapped->hEvent );
		}
	}

	static void InitOverlappedRequest( OVERLAPPED* outRequest, const AsyncOp& asyncOp )
	{
		InitOverlapped( outRequest );

		LARGE_INTEGER li;
		li.QuadPart = asyncOp.m_asyncReadToken.m_offset;
		outRequest->OffsetHigh = li.HighPart;
		outRequest->Offset = li.LowPart;
	}

	static void ExtractMergedAsyncOps(Uint64 mergedAsyncOpID, IOWorkerWin32::MergedAsyncOps& src, IOWorkerWin32::TempProcessingAsyncOps& dest)
	{
		auto& mergedOps = src.m_asyncOps;
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

	static void ProcessFinishedResult(red::EAsyncResult asyncResult, Uint32 numBytesTransferred, ReadyAsyncOp&& finishedAsyncOp, AsyncFileHandleCache* asyncFileHandleCache, IOWorkerWin32::MergedAsyncOps& mergedAsyncOpsBuffer, IOWorkerWin32::TempProcessingAsyncOps& tempProcessingAsyncOps)
	{
		RED_FATAL_ASSERT(asyncResult != red::eAsyncResult_Canceled || finishedAsyncOp.m_mergedAsyncOpID == ReadyAsyncOp::c_invalidMergedAsyncOpId, "No support for mergedAsyncOp cancellation");

		const Uint64 baseOffset = finishedAsyncOp.m_internalAsyncOp.m_asyncReadToken.m_offset;

		RED_FATAL_ASSERT(tempProcessingAsyncOps.m_asyncOps.Empty());
		if (finishedAsyncOp.m_mergedAsyncOpID != ReadyAsyncOp::c_invalidMergedAsyncOpId)
		{
			ExtractMergedAsyncOps(finishedAsyncOp.m_mergedAsyncOpID, mergedAsyncOpsBuffer, tempProcessingAsyncOps);
		}
		else
		{
			tempProcessingAsyncOps.m_asyncOps.PushBack(std::move(finishedAsyncOp));
		}

		helper::ScopedCleanupAsyncOps scopedCleanup(asyncFileHandleCache, tempProcessingAsyncOps.m_asyncOps);

		for (auto& readyAsyncOp : tempProcessingAsyncOps.m_asyncOps)
		{

#ifdef RED_PROFILE_FILE_SYSTEM
			if (readyAsyncOp.m_internalAsyncOp.m_operationId)
			{
				if (asyncResult == red::eAsyncResult_Error)
				{
					IIOProfiler::Get()->ProfileAsyncIOReadFailed(readyAsyncOp.m_internalAsyncOp.m_operationId);
				}
				else if (asyncResult == red::eAsyncResult_Success)
				{
					IIOProfiler::Get()->ProfileAsyncIOReadEnd(readyAsyncOp.m_internalAsyncOp.m_operationId);
				}
				else if (asyncResult == red::eAsyncResult_Canceled)
				{
					// do nothing apparently
				}
			}
#endif

			const auto& asyncReadToken = readyAsyncOp.m_internalAsyncOp.m_asyncReadToken;

			auto callback = asyncReadToken.m_callback;
			if (callback)
			{
				io::ShareableIOMemory memoryForIO;
				red::UniqueBuffer memoryForDecompression;
				Uint32 shareableIOMemoryOffset = 0;
				Uint32 callbackNumberOfBytesRead = 0;
				if (asyncResult == red::EAsyncResult::eAsyncResult_Success)
				{
					RED_FATAL_ASSERT(asyncReadToken.m_offset >= 0 && (Uint64)asyncReadToken.m_offset >= baseOffset);
					RED_FATAL_ASSERT(numBytesTransferred >= asyncReadToken.m_numberOfBytesToRead, "%u vs %u", numBytesTransferred, asyncReadToken.m_numberOfBytesToRead);
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
				callback(asyncReadToken, asyncResult, callbackNumberOfBytesRead, std::move(memoryForIO), shareableIOMemoryOffset, std::move(memoryForDecompression));
			}
		}
	}

	// No inline to generate a unique callstack, because we can't properly rely on getting the assert output for retail crashes.
	// Also, help with JIRA callstack merging.
#pragma optimize("",off)
	__declspec(noinline)
	static void OnFailed_InvalidAddress(const AsyncOp& asyncOp)
	{
		auto readTokenCopy = asyncOp.m_asyncReadToken; // probably get optimized out anyway
		Bool isBufferNull = (asyncOp.m_asyncReadToken.m_buffer == nullptr);
		RED_FATAL("Failed with ERROR_INVALID_ADDRESS: requestSource=%u, priority=%u, bufferNull=%d", (Uint32)readTokenCopy.m_requestSource, (Uint32)asyncOp.m_priority, isBufferNull);
	}
#pragma optimize("",on)

#pragma optimize("",off)
	__declspec(noinline)
		static void OnFailed_ErrorIoDevice(const AsyncOp& asyncOp)
	{
		auto readTokenCopy = asyncOp.m_asyncReadToken; // probably get optimized out anyway
		Bool isBufferNull = (asyncOp.m_asyncReadToken.m_buffer == nullptr);
		RED_FATAL("Failed with ERROR_IO_DEVICE: requestSource=%u, priority=%u, bufferNull=%d", (Uint32)readTokenCopy.m_requestSource, (Uint32)asyncOp.m_priority, isBufferNull);
	}
#pragma optimize("",on)

#pragma optimize("",off)
	__declspec(noinline)
		static void OnFailed_UnknownError(const AsyncOp& asyncOp)
	{	
		auto readTokenCopy = asyncOp.m_asyncReadToken; // probably get optimized out anyway
		Bool isBufferNull = (asyncOp.m_asyncReadToken.m_buffer == nullptr);
		RED_FATAL("Failed with ERROR_SUCCESS: requestSource=%u, priority=%u, bufferNull=%d", (Uint32)readTokenCopy.m_requestSource, (Uint32)asyncOp.m_priority, isBufferNull);
	}
#pragma optimize("",on)

	static void ProcessOverlappedResult( OVERLAPPED* overlapped, ReadyAsyncOp&& finishedAsyncOp, AsyncFileHandleCache* asyncFileHandleCache, IOWorkerWin32::MergedAsyncOps& mergedAsyncOpsBuffer, IOWorkerWin32::TempProcessingAsyncOps& tempProcessingAsyncOps, Uint32& outNumBytesRead )
	{
		outNumBytesRead = 0;

		auto asyncResult = red::eAsyncResult_Success;
		DWORD numBytesTransferred = 0;
		if ( !GetOverlappedResult( nullptr, overlapped, &numBytesTransferred, FALSE ) )
		{
			const Uint32 lastError = GetLastError();
			if (lastError == ERROR_OPERATION_ABORTED)
			{
				numBytesTransferred = 0; // better zero here than P0 bug elsewhere
				asyncResult = red::eAsyncResult_Canceled;
			}
			else
			{
				if (lastError == ERROR_INVALID_ADDRESS)
				{
					OnFailed_InvalidAddress(finishedAsyncOp.m_internalAsyncOp);
				}
				else if (lastError == ERROR_IO_DEVICE)
				{
					OnFailed_ErrorIoDevice(finishedAsyncOp.m_internalAsyncOp);
				}
				else if (lastError == 0)
				{
					OnFailed_UnknownError(finishedAsyncOp.m_internalAsyncOp);
				}
				RED_FATAL( "GetOverlappedResult failed with GetLastError()=0x%08X", lastError );
				asyncResult = red::eAsyncResult_Error;
			}
		}
		
		outNumBytesRead = numBytesTransferred;
		
		ProcessFinishedResult(asyncResult, numBytesTransferred, std::move(finishedAsyncOp), asyncFileHandleCache, mergedAsyncOpsBuffer, tempProcessingAsyncOps);
	}
}

IOWorkerWin32::IOWorkerWin32( const IOWorkerWin32Setup& setup )
	: red::Thread( "redIOWorker" )
	, m_overlappedPool()
	, m_setup( setup )
	, m_statsBytesReadTotal(0)
	, m_statsNumAsyncOpsInFlight()
{
	for ( auto& it : m_overlapped )
	{
		m_overlappedPool.PushBack( &it );
	}
}

IOWorkerWin32::~IOWorkerWin32()
{
	for ( auto* overlapped : m_overlappedPool )
	{
		if ( overlapped->hEvent != nullptr )
		{
			::CloseHandle( overlapped->hEvent );
		}
	}
}

red::ManualResetEvent& IOWorkerWin32::GetWakeEvent() const
{
	return m_wakeEvent;
}

void IOWorkerWin32::ThreadFunc()
{
	red::memory::RegisterCurrentThread( GetThreadName() );

	GIsCallbackThread = true;

#ifdef RED_PLATFORM_CONSOLE
	SetAffinityMask( c_threadAffinityMask );
	SetPriority( red::TP_TimeCritical );
#endif

	RED_FATAL_ASSERT( m_setup.shutdownFlag );

	for ( ;; )
	{
		// imagine all IO has been put on and signaled, but then we're waiting again because?
		// because we should probably try to fill the buffer again after calling process aio?

		m_wakeEvent.Wait();
		m_wakeEvent.ResetEvent();
		if ( m_setup.shutdownFlag->GetValue() )
		{
			break;
		}

		ProcessAio();
	}
}

namespace helper
{
	enum class ReadFileOverlappedResult
	{
		Pending,
		Finished,
		ReadError,
		TooManyAsyncOps,
	};

	static ReadFileOverlappedResult ReadFileOverlapped( HANDLE hFile, OVERLAPPED* overlapped, const AsyncOp& asyncOp, Uint32& outNumBytesReadySynchronously )
	{
		DWORD numBytesRead = 0;
		const auto& asyncReadToken = asyncOp.m_asyncReadToken;
		const BOOL ret = ::ReadFile(
			hFile,
			asyncReadToken.m_buffer,
			asyncReadToken.m_numberOfBytesToRead,
			&numBytesRead,
			overlapped );
		
		outNumBytesReadySynchronously = numBytesRead;

		if ( ret != FALSE )
		{
			return ReadFileOverlappedResult::Finished;
		}
		
		const Uint32 lastError = ::GetLastError();
		
		if ( lastError == ERROR_IO_PENDING )
		{
			return ReadFileOverlappedResult::Pending;
		}
		
		if ( lastError == ERROR_INVALID_USER_BUFFER || lastError == ERROR_NOT_ENOUGH_MEMORY )
		{
			return ReadFileOverlappedResult::TooManyAsyncOps;
		}

		if (lastError == ERROR_INVALID_ADDRESS)
		{
			OnFailed_InvalidAddress(asyncOp);
		}

		RED_FATAL( "ReadFileOverlapped: ReadFile() failed. GetLastError()=0x%08X", lastError );

		return ReadFileOverlappedResult::ReadError;
	}
}

void IOWorkerWin32::SubmitNewAioRequests()
{
	//PC_SCOPE_FUNC();

	while (!m_fillBuffers.mergedAsyncOps.Empty())
	{
		m_mergedAsyncOpsBuffer.m_asyncOps.PushBack(helper::PopFront(m_fillBuffers.mergedAsyncOps));
	}

	while (!m_fillBuffers.readyAsyncOps.Empty())
	{
		// Can't submit anymore overlapped requests
		RED_FATAL_ASSERT(!m_overlappedPool.Empty(), "Internal error: collected more ops than possible to process");

		ReadyAsyncOp readyAsyncOp = helper::PopFront(m_fillBuffers.readyAsyncOps);
		const AsyncOp& asyncOp = readyAsyncOp.m_internalAsyncOp;

		// Note: we'll *pop* the overlapped and async op once after ReadFile()
		// If the result finishes or errors immediately, don't need to keep the overlapped
		// And if we get "TooManyAsyncOps" then we need to leave the asyncop for next time.

		auto* overlapped = m_overlappedPool.Back();
		helper::InitOverlappedRequest( overlapped, asyncOp );
		AsyncFile* asyncFile = m_setup.asyncFileHandleCache->GetAsyncFile( asyncOp.m_asyncFileHandle );
		Uint32 numBytesReadSynchronously = 0;
		auto result = helper::ReadFileOverlappedResult::ReadError;

#ifdef RED_PROFILE_FILE_SYSTEM
		if( asyncOp.m_operationId )
		{
			IIOProfiler::Get()->ProfileAsyncIOReadStart( asyncOp.m_operationId );
		}
#endif
		if ( asyncFile )
		{
			result = helper::ReadFileOverlapped( asyncFile->GetFileHandle(), overlapped, asyncOp, numBytesReadSynchronously );
		}

		if ( result == helper::ReadFileOverlappedResult::Pending )
		{
			m_pendingState.asyncOps.PushBack( std::move(readyAsyncOp) );
			m_pendingState.overlapped.PushBack( overlapped );

			m_overlappedPool.PopBack();
		}
		else if ( result == helper::ReadFileOverlappedResult::Finished )
		{
			helper::ProcessFinishedResult( red::eAsyncResult_Success, numBytesReadSynchronously, std::move(readyAsyncOp), m_setup.asyncFileHandleCache,  m_mergedAsyncOpsBuffer, m_tempProcessingAsyncOps );
		}
		else if ( result == helper::ReadFileOverlappedResult::ReadError )
		{
			helper::ProcessFinishedResult( red::eAsyncResult_Error, 0, std::move(readyAsyncOp), m_setup.asyncFileHandleCache, m_mergedAsyncOpsBuffer, m_tempProcessingAsyncOps );
		}
		else
		{
			RED_FATAL_ASSERT( result == helper::ReadFileOverlappedResult::TooManyAsyncOps, "Unexpected result: %u", (Uint32)result );

			m_fillBuffers.readyAsyncOps.PushFront(std::move(readyAsyncOp)); // Back it goes to retry later

			// Stop submitting new requests for now
			break;
		}
	}

	UpdateNumAsyncOpsInFlight();

	// Process cancels after kicking off new I/O above
	while (!m_fillBuffers.cancelledAsyncOps.Empty())
	{
		helper::ProcessFinishedResult(red::eAsyncResult_Canceled, 0, helper::PopFront(m_fillBuffers.cancelledAsyncOps), m_setup.asyncFileHandleCache, m_mergedAsyncOpsBuffer, m_tempProcessingAsyncOps);
	}

	while (!m_fillBuffers.cachedAsyncOps.Empty())
	{
		auto cachedOp = helper::PopFront(m_fillBuffers.cachedAsyncOps);
		const Uint32 numberOfBytesToRead = cachedOp.m_internalAsyncOp.m_asyncReadToken.m_numberOfBytesToRead;
		helper::ProcessFinishedResult(red::eAsyncResult_Success, numberOfBytesToRead, std::move(cachedOp), m_setup.asyncFileHandleCache, m_mergedAsyncOpsBuffer, m_tempProcessingAsyncOps);
	}
}

void IOWorkerWin32::UpdateNumAsyncOpsInFlight()
{
	StatsNumAsyncOpsInFlight newStats;

	for (const auto& it : m_pendingState.asyncOps)
	{
		newStats.value[it.m_internalAsyncOp.m_priority] += 1;
	}
	m_statsNumAsyncOpsInFlight = newStats;
}


void IOWorkerWin32::FinishAllReadyAioRequests()
{
	if ( m_pendingState.overlapped.Empty() )
	{
		RED_FATAL_ASSERT( m_fillBuffers.readyAsyncOps.Empty(), "Internal error: Should have submitted some available asyncops" );
		return;
	}

	Bool wait = true;
	while ( !m_pendingState.overlapped.Empty() )
	{
		if ( !FinishNextReadyAioRequest( wait ) )
		{
			break;
		}
		
		// Don't wait again so we can submit more async ops if possible
		wait = false;
	}
}

namespace helper
{
	static void VerifyWaitResult( Uint32 numWaitHandles, Uint32 waitResult, Bool wait )
	{
		RED_FATAL_ASSERT( waitResult != WAIT_FAILED, "WaitForMultipleObjects failed: GetLastError()=0x%08X", GetLastError() );
		RED_FATAL_ASSERT( ( waitResult < WAIT_ABANDONED_0 ) || ( waitResult > WAIT_ABANDONED_0 + numWaitHandles - 1 ) );
		RED_FATAL_ASSERT(
						( waitResult != WAIT_TIMEOUT || !wait ) ||
						( waitResult >= WAIT_OBJECT_0 && waitResult <= WAIT_OBJECT_0 + numWaitHandles - 1 ) );
	}
}

Bool IOWorkerWin32::FinishNextReadyAioRequest( Bool wait )
{
	HANDLE waitHandles[ c_maxRequests ] = {};
	
	for ( Uint32 i : m_pendingState.overlapped.Indices() )
	{
		const auto& asyncOp = m_pendingState.asyncOps[ i ].m_internalAsyncOp;
		const auto& ov = m_pendingState.overlapped[ i ];

		RED_FATAL_ASSERT( ov->hEvent );
		waitHandles[ i ] = ov->hEvent;

		if (asyncOp.m_asyncReadToken.IsCancelRequested() && !m_pendingState.cancelledMask.Get(i))
		{
			m_pendingState.cancelledMask.Set(i);

			AsyncFile* asyncFile = m_setup.asyncFileHandleCache->GetAsyncFile(asyncOp.m_asyncFileHandle);
			RED_FATAL_ASSERT(asyncFile && asyncFile->GetFileHandle() != INVALID_HANDLE_VALUE);
			(void)::CancelIoEx(asyncFile->GetFileHandle(), ov);
		}
	}

	const Uint32 waitTime = wait ? INFINITE : 0;
	static_assert( c_maxRequests <= MAXIMUM_WAIT_OBJECTS, "WaitForMultipleObjects can't wait for this many handles" );
	const Uint32 numWaitHandles = m_pendingState.overlapped.Size();
	const DWORD waitResult = WaitForMultipleObjects( numWaitHandles, waitHandles, FALSE, waitTime );
	helper::VerifyWaitResult( numWaitHandles, waitResult, wait );

	if ( waitResult == WAIT_TIMEOUT )
	{
		return false;
	}

	Uint32 numBytesRead = 0;
	const Uint32 finishedIndex = waitResult - WAIT_OBJECT_0;
	helper::ProcessOverlappedResult( m_pendingState.overlapped[ finishedIndex ], std::move(m_pendingState.asyncOps[ finishedIndex ]), m_setup.asyncFileHandleCache, m_mergedAsyncOpsBuffer, m_tempProcessingAsyncOps, numBytesRead );

	// put OVERLAPPED back into pool
	m_overlappedPool.PushBack( m_pendingState.overlapped[ finishedIndex ] );

	// cleanup
	RED_FATAL_ASSERT(m_pendingState.overlapped.Size() > 0);
	if (finishedIndex != m_pendingState.overlapped.Size() - 1)
	{
		const Uint32 lastIndex = m_pendingState.overlapped.Size() - 1;
		const Bool val = m_pendingState.cancelledMask.Get(lastIndex);
		m_pendingState.cancelledMask.Clear(lastIndex);
		m_pendingState.cancelledMask.Set(finishedIndex, val);
	}
	else
	{
		m_pendingState.cancelledMask.Clear(finishedIndex);
	}
	m_pendingState.overlapped.RemoveAtReorder( finishedIndex );
	m_pendingState.asyncOps.RemoveAtReorder( finishedIndex );

	// update stats
	m_statsBytesReadTotal += numBytesRead;
	UpdateNumAsyncOpsInFlight();

	return true;
}

void IOWorkerWin32::ProcessAio()
{
	for (;;)
	{
		FillAsyncOpsBuffer();

		Bool hasWork = false;
		hasWork |= m_fillBuffers.HasWork();
		hasWork |= m_pendingState.asyncOps.Size() > 0;

		if (!hasWork)
		{
			RED_FATAL_ASSERT(m_mergedAsyncOpsBuffer.m_asyncOps.Empty());
			break;
		}

		SubmitNewAioRequests();
		FinishAllReadyAioRequests();
	}
}

void IOWorkerWin32::FillAsyncOpsBuffer()
{
	if (m_pendingState.asyncOps.Size() > c_queueDrainLimit)
	{
		return;
	}

	auto fillFunc = m_setup.fillAsyncOpsFunc;
	auto context  = m_setup.context;

	//RED_FATAL_ASSERT(m_asyncOpBuffer.Empty(), "Async ops buffer should have been previously consumed");
	// NOTE: Theoretically asyncOpBuffer may not be empty if previously encountered TooManyOps error from Win32.
	// But if we'll ever actually have that happen is a mystery do far
	RED_FATAL_ASSERT(c_maxRequests >= m_fillBuffers.readyAsyncOps.Size() + m_pendingState.asyncOps.Size(), "Shouldn't have taken more ops than possible to submit");
	
	const Uint32 resolvedMaxRequests = m_HACK_throttleStreamingForCutsceneAudio.GetValue() ? c_HACK_maxAsyncOpsThrottleStreamingForCutsceneAudio : c_maxRequests;
	const Uint32 numOpsToFill = Max<Int32>(0, (Int32)resolvedMaxRequests - (Int32)m_fillBuffers.readyAsyncOps.Size() - (Int32)m_pendingState.asyncOps.Size());

	RED_FATAL_ASSERT(m_overlappedPool.Size() >= numOpsToFill, "Internal error: insufficient available overlapeds");

	// Call the fill function
	(*fillFunc)(context, numOpsToFill, m_fillBuffers);
}

StatsNumAsyncOpsInFlight IOWorkerWin32::GetStatsNumAsyncOpsInFlight() const
{
	return m_statsNumAsyncOpsInFlight;
}

void IOWorkerWin32::EnableLoadingMode(Bool value)
{
	m_loadingMode.SetValue(value);
}

void IOWorkerWin32::EnableSavingMode(Bool value)
{
	/* nothing */ 
}

void IOWorkerWin32::HACK_ThrottleStreamingForCutsceneAudio(Bool throttle)
{
	m_HACK_throttleStreamingForCutsceneAudio.SetValue(throttle);
}

Bool IOWorkerWin32::HACK_IsThrottleStreamingForCutsceneAudio() const
{
	return m_HACK_throttleStreamingForCutsceneAudio.GetValue();
}

void IOWorkerWin32::HACK_AudioThreadStreamingForCutsceneAudio(Bool throttle)
{
	m_HACK_audioThreadStreamingForCutsceneAudio.SetValue(throttle);
}

Bool IOWorkerWin32::HACK_IsAudioThreadStreamingForCutsceneAudio() const
{
	return m_HACK_audioThreadStreamingForCutsceneAudio.GetValue();
}
}
}

#else
RED_NO_EMPTY_FILE();
#endif // RED_PLATFORM_ORBIS
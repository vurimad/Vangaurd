/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "redIOWorkerGeneric.h"

#include "redIOAsyncFileHandleCache.h"
#include "redIOFile.h"

// #fixme: redCore depends on redIO
#include "../../redCore/include/profilerConfiguration.h"
#ifdef USE_RAZOR_PROFILER
# include <razorcpu.h>
# pragma comment( lib, "libSceRazorCPU_stub_weak.a" )
#endif

#ifdef RED_FATAL_ASSERT
#undef RED_FATAL_ASSERT
#endif

#ifdef RED_FATAL
#undef RED_FATAL
#endif

#define RED_FATAL_ASSERT ALWAYSENABLED_RED_FATAL_ASSERT
#define RED_FATAL ALWAYSENABLED_RED_FATAL

namespace dd
{
	static red::CrashDataThreadLocal< String, 1024 > currentAsyncOp{ "Engine/IO", "CurrentAsyncOp" };
}

namespace io
{

extern thread_local bool GIsCallbackThread;
extern const Uint32 c_uncacheableMemoryTag;

namespace prv
{

constexpr Int64 c_maxFowardSeekDistance = RED_KILO_BYTE(64);
const Uint32 c_maxBufferedOps = 1;
red::TAffinityMask c_threadAffinityMask = ( 1ULL << 4 | 1ULL << 5 );

IOWorkerGeneric::IOWorkerGeneric( const IOWorkerGenericSetup& setup )
	: red::Thread( "redIOWorker0" )
	, m_setup( setup )
	, m_wakeEvent()
	, m_statsBytesReadTotal( 0 )
	, m_statsNumAsyncOpsInFlight()
{
}

IOWorkerGeneric::~IOWorkerGeneric()
{
}

void IOWorkerGeneric::ThreadFunc()
{
	red::memory::RegisterCurrentThread( GetThreadName() );

	GIsCallbackThread = true;

#if defined( RED_PLATFORM_CONSOLE )
	SetAffinityMask( c_threadAffinityMask );
	SetPriority(red::TP_TimeCritical);
#endif

	RED_FATAL_ASSERT( m_setup.shutdownFlag );

	for ( ;; )
	{
		m_wakeEvent.Wait();
		m_wakeEvent.ResetEvent();

		if ( m_setup.shutdownFlag->GetValue() )
		{
			break;
		}

		for ( ;; )
		{
			FillAsyncOpBuffers();

			if (!m_fillBuffers.HasWork())
			{
				break;
			}

			while (!m_fillBuffers.mergedAsyncOps.Empty())
			{
				m_mergedAsyncOpsBuffer.PushBack(helper::PopFront(m_fillBuffers.mergedAsyncOps));
			}

			while (!m_fillBuffers.readyAsyncOps.Empty())
			{
				ReadyAsyncOp readyAsyncOp = helper::PopFront(m_fillBuffers.readyAsyncOps);
				ProcessAsyncOp(readyAsyncOp);
			}

			while (!m_fillBuffers.cachedAsyncOps.Empty())
			{
				ReadyAsyncOp cachedAsyncOp = helper::PopFront(m_fillBuffers.cachedAsyncOps);
				ProcessAsyncOp(cachedAsyncOp);
			}

			while (!m_fillBuffers.cancelledAsyncOps.Empty())
			{
				ReadyAsyncOp cancelledAsyncOp = helper::PopFront(m_fillBuffers.cancelledAsyncOps);
				ProcessAsyncOp(cancelledAsyncOp);
			}

			RED_FATAL_ASSERT(m_mergedAsyncOpsBuffer.Empty(), "Internal error - not all merged ops were processed?!");
		} 
	}
}

void IOWorkerGeneric::FillAsyncOpBuffers()
{
	auto fillFunc = m_setup.fillAsyncOpsFunc;
	auto context = m_setup.context;

	(*fillFunc)(context, c_maxBufferedOps, m_fillBuffers);
}

void IOWorkerGeneric::ProcessAsyncOp( ReadyAsyncOp& readyAsyncOp )
{
	const auto& pendingAsyncOp = readyAsyncOp.m_internalAsyncOp;

#if 1
	const char* currentFileName = m_setup.asyncFileHandleCache->GetFileName(pendingAsyncOp.m_asyncFileHandle);
	if (!currentFileName || !*currentFileName)
	{
		currentFileName = "<invalid>";
	}
	RED_SET_SCOPED_CRASH_DATA(dd::currentAsyncOp, currentFileName);
#endif

	// process the op
	ProcessBeginRead(std::move(readyAsyncOp));
}

namespace helper
{
	static void ExtractMergedAsyncOps(Uint64 mergedAsyncOpID, red::DynArray<ReadyAsyncOp>& src, red::DynArray<ReadyAsyncOp>& dest)
	{
		Uint32 i = 0;
		while (i < src.Size())
		{
			if (src[i].m_mergedAsyncOpID == mergedAsyncOpID)
			{
				dest.PushBack(std::move(src[i]));
				src.RemoveAt(i);
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
}

void IOWorkerGeneric::ProcessBeginRead( ReadyAsyncOp&& readyAsyncOp )
{
	RED_FATAL_ASSERT(m_tempProcessingAsyncOps.Empty());

	if (readyAsyncOp.m_internalAsyncOp.m_asyncReadToken.IsCancelRequested())
	{
		RED_FATAL_ASSERT(readyAsyncOp.m_mergedAsyncOpID == ReadyAsyncOp::c_invalidMergedAsyncOpId, "No support for mergedAsyncOp cancellation");

		//NOTE: would need to tag the main mergedOp if cancel becomes supported there.
		// I/O memory is allocated to ensure it's really at least sizeof(Uint32)
		Uint32* tag = (Uint32*)readyAsyncOp.m_internalMemoryForIO.Data();
		if (tag)
		{
			*tag = c_uncacheableMemoryTag;
		}

		const auto cancelledTokenCopy = readyAsyncOp.m_internalAsyncOp.m_asyncReadToken;
		const FAsyncOpCallback callback = cancelledTokenCopy.m_callback;
		if (callback)
		{
			callback(cancelledTokenCopy, red::eAsyncResult_Canceled, 0, io::ShareableIOMemory(), 0, red::UniqueBuffer());
		}
		return;
	}

	AsyncReadToken activeToken = readyAsyncOp.m_internalAsyncOp.m_asyncReadToken;
	ShareableIOMemory shareableIOMemory = readyAsyncOp.m_internalMemoryForIO;
	const Uint64 baseOffset = activeToken.m_offset;
	AsyncFile* asyncFile = m_setup.asyncFileHandleCache->GetAsyncFile(readyAsyncOp.m_internalAsyncOp.m_asyncFileHandle);

	if (readyAsyncOp.m_mergedAsyncOpID != ReadyAsyncOp::c_invalidMergedAsyncOpId)
	{
		helper::ExtractMergedAsyncOps(readyAsyncOp.m_mergedAsyncOpID, m_mergedAsyncOpsBuffer, m_tempProcessingAsyncOps);
	}
	else
	{
		m_tempProcessingAsyncOps.PushBack(std::move(readyAsyncOp));
	}

	helper::ScopedCleanupAsyncOps scopedCleanup(m_setup.asyncFileHandleCache, m_tempProcessingAsyncOps);

	if ( !asyncFile ) // deferred open failed
	{
#ifdef RED_PROFILE_FILE_SYSTEM
		helper::ForEachAsyncOp(m_tempProcessingAsyncOps, [](const auto& asyncOp) {
			if( asyncOp.m_internalAsyncOp.m_operationId )
			{
				IIOProfiler::Get()->ProfileAsyncIOReadFailed( asyncOp.m_internalAsyncOp.m_operationId );
			}
		});
#endif

		helper::ForEachAsyncOp(m_tempProcessingAsyncOps, [](const auto& asyncOp) {
			const FAsyncOpCallback callback = asyncOp.m_internalAsyncOp.m_asyncReadToken.m_callback;
			if ( callback )
			{
				AsyncReadToken opAsyncReadToken = asyncOp.m_internalAsyncOp.m_asyncReadToken;
				callback( opAsyncReadToken, red::eAsyncResult_Error, 0, io::ShareableIOMemory(), 0, red::UniqueBuffer() );
			}
		});
		return;
	}

	StatsNumAsyncOpsInFlight numAsyncOpsInFlight;
	//numAsyncOpsInFlight.value[asyncOp.m_priority] = 1;
	red::ScopedFlag<StatsNumAsyncOpsInFlight> numAsyncOpsInFlightGuard{ numAsyncOpsInFlight,  StatsNumAsyncOpsInFlight() };

#if 0 // #TBD: operattionID and sense with merged reads now
#ifdef RED_PROFILE_FILE_SYSTEM
	if( asyncOP.m_operationId )
	{
		IIOProfiler::Get()->ProfileAsyncIOSeekStart( asyncOp.m_operationId, readOffset );
	}
#endif
#endif

	const Bool seekSucceeded = asyncFile->Seek( activeToken.m_offset, eSeekOrigin_Set );

#if 0
#ifdef RED_PROFILE_FILE_SYSTEM
	if( asyncOp.m_operationId )
	{
		IIOProfiler::Get()->ProfileAsyncIOSeekEnd( asyncOp.m_operationId, seekSucceeded );
	}
#endif
#endif

	if ( !seekSucceeded )
	{
#ifdef RED_PROFILE_FILE_SYSTEM
		helper::ForEachAsyncOp(m_tempProcessingAsyncOps, [](const auto& asyncOp) {
			if( asyncOp.m_internalAsyncOp.m_operationId )
			{
				IIOProfiler::Get()->ProfileAsyncIOReadFailed( asyncOp.m_internalAsyncOp.m_operationId );
			}
		});
#endif

		RED_FATAL( "RedIO: Failed to seek to offset %lld, fd %d, lastError=0x%08X", activeToken.m_offset, readyAsyncOp.m_internalAsyncOp.m_asyncFileHandle, io::LastError() );
		helper::ForEachAsyncOp(m_tempProcessingAsyncOps, [](const auto& asyncOp) {
			auto asyncReadToken = asyncOp.m_internalAsyncOp.m_asyncReadToken;
			FAsyncOpCallback callback = asyncReadToken.m_callback;
			if ( callback )
			{
				callback( asyncReadToken, red::eAsyncResult_Error, 0, io::ShareableIOMemory(), 0, red::UniqueBuffer() );
			}
		});
		return;
	}

	void* const buffer = activeToken.m_buffer;
	const Int64 offset = activeToken.m_offset;
	const Uint32 numberOfBytesToRead = activeToken.m_numberOfBytesToRead;

#ifdef RED_PROFILE_FILE_SYSTEM
	helper::ForEachAsyncOp(m_tempProcessingAsyncOps, [](const auto& asyncOp) {
		if ( asyncOp.m_internalAsyncOp.m_operationId )
		{
			IIOProfiler::Get()->ProfileAsyncIOReadStart( asyncOp.m_internalAsyncOp.m_operationId );
		}
	});
#endif

#ifdef USE_RAZOR_PROFILER
	//::sceRazorCpuBeginLogicalFileAccess( debugLogicalFileName, 0, asyncReadToken.m_numberOfBytesToRead, SCE_RAZOR_CPU_LOGICAL_FILE_READ );
#endif

	Uint32 nbytes = 0;
	if ( asyncFile->Read( buffer, numberOfBytesToRead, nbytes ) && nbytes == numberOfBytesToRead )
	{
		m_statsBytesReadTotal += nbytes;

		helper::ForEachAsyncOp(m_tempProcessingAsyncOps, [baseOffset, nbytes](ReadyAsyncOp& asyncOp) {
			auto asyncReadToken = asyncOp.m_internalAsyncOp.m_asyncReadToken;
			auto callback = asyncReadToken.m_callback;
			if ( callback )
			{
				RED_FATAL_ASSERT(asyncReadToken.m_offset >= 0 && (Uint64)asyncReadToken.m_offset >= baseOffset);
				RED_FATAL_ASSERT(nbytes >= asyncReadToken.m_numberOfBytesToRead);
				const Uint32 callbackNumberOfBytesRead = asyncReadToken.m_numberOfBytesToRead;
				const Uint32 shareableIOMemoryOffset = (Uint32)(asyncReadToken.m_offset - baseOffset);
				callback(asyncReadToken, red::eAsyncResult_Success, callbackNumberOfBytesRead,
					std::move(asyncOp.m_internalMemoryForIO), shareableIOMemoryOffset, std::move(asyncOp.m_internalMemoryForDecompression));
			}
		});
	}
	else
	{
		RED_FATAL( "RedIO: Failed to read %u bytes (read %u bytes), fd %d, lastError=0x%08X", numberOfBytesToRead, nbytes, readyAsyncOp.m_internalAsyncOp.m_asyncFileHandle, io::LastError() );
		helper::ForEachAsyncOp(m_tempProcessingAsyncOps, [](const ReadyAsyncOp& asyncOp) {
			auto asyncReadToken = asyncOp.m_internalAsyncOp.m_asyncReadToken;
			auto callback = asyncReadToken.m_callback;
			if ( callback )
			{
				callback( asyncReadToken, red::eAsyncResult_Error, 0, io::ShareableIOMemory(), 0, red::UniqueBuffer() );
			}
		});
	}

#ifdef RED_PROFILE_FILE_SYSTEM
	helper::ForEachAsyncOp(m_tempProcessingAsyncOps, [](const ReadyAsyncOp& asyncOp) {
		if ( asyncOp.m_internalAsyncOp.m_operationId )
		{
			IIOProfiler::Get()->ProfileAsyncIOReadEnd( asyncOp.m_internalAsyncOp.m_operationId );
		}
	});
#endif

#ifdef USE_RAZOR_PROFILER
	//::sceRazorCpuEndLogicalFileAccess();
#endif
}

red::ManualResetEvent& IOWorkerGeneric::GetWakeEvent() const
{
	return m_wakeEvent;
}

Uint64 IOWorkerGeneric::GetStatsBytesReadTotal() const
{
	return const_cast<volatile Uint64&>(m_statsBytesReadTotal);
}

StatsNumAsyncOpsInFlight IOWorkerGeneric::GetStatsNumAsyncOpsInFlight() const
{
	return m_statsNumAsyncOpsInFlight;
}

void IOWorkerGeneric::EnableLoadingMode(Bool value)
{
	// No-op, but need to record it for other usage
	m_isLoadingMode.SetValue(value);
}

void IOWorkerGeneric::EnableSavingMode(Bool value)
{
	/* nothing */
}

void IOWorkerGeneric::HACK_ThrottleStreamingForCutsceneAudio(Bool throttle)
{
	// Does nothing, but useful to query for general debugging expected console behavior from PC
	m_HACK_throttleStreamingForCutsceneAudio.SetValue(throttle);
}

Bool IOWorkerGeneric::HACK_IsThrottleStreamingForCutsceneAudio() const
{
	return m_HACK_throttleStreamingForCutsceneAudio.GetValue();
}

void IOWorkerGeneric::HACK_AudioThreadStreamingForCutsceneAudio(Bool throttle)
{
	// Does nothing, but useful to query for general debugging expected console behavior from PC
	m_HACK_audioThreadStreamingForCutsceneAudio.SetValue(throttle);
}

Bool IOWorkerGeneric::HACK_IsAudioThreadStreamingForCutsceneAudio() const
{
	return m_HACK_audioThreadStreamingForCutsceneAudio.GetValue();
}
} // prv
} // io

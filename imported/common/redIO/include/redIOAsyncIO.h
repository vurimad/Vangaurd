/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "redIOCommon.h"
#include "redIOAsyncFileHandleCache.h"
#include "redIOAsyncReadToken.h"

#include "../../redSystem/include/redThreadsThread.h"
#include "../../redSystem/include/redThreadsAtomic.h"
#include "../../redMemory/include/uniquePtr.h"

#include "../../redContainers/include/circularBuffer.h"

namespace red
{
	class Thread;
}

namespace io
{

struct AsyncReadToken;
struct AsyncIOStats;
struct AsyncIOStatsAvailableInFinal;
class MemoryAllocator;

namespace orbis
{
	class Profiler;
	class TraceWriter;
}

namespace prv
{
	struct AsyncOp;
	struct ReadyAsyncOp;
	struct FillBuffers;
}

class REDIO_API AsyncIO : red::NonCopyable
{
public:
	AsyncIO();
	~AsyncIO();
	Bool Init( const InitSetup& initSetup );
	void Shutdown();

	TFileHandle OpenFile( const char* filePath, Uint32 asyncFlags = eAsyncFlag_None );	//!< Returns a file handle, or INVALID_FILE_HANDLE on error.
	void AddRefFile( TFileHandle fh );
	void ReleaseFile( TFileHandle fh );			//!< Releases the file handle. See BeginRead for its lifetime.
	Uint64 GetFileSize( TFileHandle fh );			//!< Returns the file size, or 0 on error.
	const char* GetFileName( TFileHandle fh ) const;	//!< Returns the file name
	Uint8 GetAsyncFlags( TFileHandle fh ) const;

    struct SortFlags
    {
        Bool drivingMode = false;
        Bool loadingMode = false;
    };
	void SortIOQueues( const SortFlags flags );

	void EnabledLoadingMode(Bool value);
	void EnableSavingMode(Bool value);

	//! Starts an asynchronous read operation.
	//!
	//! @param fh				the file handle to read from
	//! @param asyncReadToken	the initialized asyncReadToken that controls the read operation through its callback
	//! @param priority			the priority of the read operation
	void BeginRead( TFileHandle fh, const AsyncReadToken& asyncReadToken, EAsyncPriority priority = eAsyncPriority_DEFAULT );

	void ADVANCED_BeginBulkReadThreadLocal();
	void ADVANCED_FinishBulkReadThreadLocal();

	void GetStats( AsyncIOStats& outStats ) const;
	void GetStatsAvailableInFinal( AsyncIOStatsAvailableInFinal& outStats ) const;

	// #temp:
	void ProfileStartTrace();
	void ProfileStopTrace();
	void ProfileDecompressStart( const char* resourcePath );
	void ProfileDecompressEnd( const char* resourcePath );
	void ProfileIOWorkerBegin( Uint32 priority );
	void ProfileIOWorkerEnd( Uint32 priority );
	void ProfileMarkEvent( const char* eventName );

	void KickWorkerThread();

	void HACK_ThrottleStreamingForCutsceneAudio(Bool throttle);
	Bool HACK_IsThrottleStreamingForCutsceneAudio() const;
	void HACK_AudioThreadStreamingForCutsceneAudio(Bool throttle);
	Bool HACK_IsAudioThreadStreamingForCutsceneAudio() const;

private:
	void SortIOQueuesForLoadingMode();
	void SortIOQueuesForStreamingMode( Bool drivingMode );

	void FillAsyncOps(Uint32 maxReadyAsyncOps, prv::FillBuffers& fillBuffers);
	void FillAudioAsyncOps(prv::FillBuffers& fillBuffers, Uint32 maxReadyAsyncOps);

	Bool TryFillNextScheduledAsyncOp_NoLock(prv::FillBuffers& fillBuffers, Uint32 priority);

	static void FillAsyncOpsCallback(void* context, Uint32 maxReadyAsyncOps, prv::FillBuffers& fillBuffers);
    static void FillAudioAsyncOpsCallback(void* context, prv::FillBuffers& fillBuffers, Uint32 maxReadyAsyncOps );
	
	Bool UpdateActivePriority_NoLock();

	struct AsyncOpBuffer
	{
		red::CircularBuffer< prv::AsyncOp >	asyncOps{ red::PoolEngine() };
        Uint64 lastSortedTicket{ 0 };
		Uint32 queueWeight{ 1 }; // how many async ops we're allowed to pop before giving up our RR turn.
		Uint32 asyncOpsSentThisTurn{ 0 }; // see c_maxFractionalQuantum
	};
	

	typedef red::LightMutex	TQueueLock;
	mutable TQueueLock m_bufferLock;

	red::ManualResetEvent*	m_workerWakeEvent{ nullptr }; // owned by the thread worker

#if defined( RED_PLATFORM_ORBIS ) && defined( USE_PROFILER )
	red::UniquePtr< ::io::orbis::Profiler > m_profilerOrbis;
	red::UniquePtr< ::io::orbis::TraceWriter > m_traceWriterOrbis;
#endif

	AsyncOpBuffer			m_asyncOpBuffers[eAsyncPriority_COUNT];
	Uint32					m_currentActivePriority{ (Uint32)eAsyncPriority_GAME };
	Uint64					m_mergedAsyncOpIDCounter{ 0 };
    Uint32                  m_sortFrameNo = 0;
    Bool                    m_hackForThrottleStreamingForAudioEnabled = false;
    Bool                    m_hackForAudioThreadStreamingForAudioEnabled = false;

	red::Atomic<Uint64> m_asyncOpTicketAllocator;
	red::UniquePtr< MemoryAllocator > m_memoryAllocator;
	red::UniquePtr< AsyncFileHandleCache > m_asyncFileHandleCache;
	red::UniquePtr< red::Thread, red::PoolEngine > m_worker;
	red::Atomic< Bool >	m_shutdownFlag;
	red::Atomic< Bool >	m_pauseFlag;

#if defined( USE_PROFILER )
    using QueueString = red::StaticArray<Uint8, 512>;
    using ReadyOpsString = red::StaticArray<Uint8, 128>;
    QueueString m_dbgGameQueueString;
    ReadyOpsString m_dbgReadyOpsString;
#endif
};

extern REDIO_API AsyncIO GAsyncIO;

}


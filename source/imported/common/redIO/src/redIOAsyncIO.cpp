/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "redIOMemory.h"
#include "redIOAsyncIO.h"
#include "redIOAsyncReadToken.h"

#include "../../redSystem/include/redSystemPublic.h"
#include "../../redSystem/include/assert.h"
#include "../../redSystem/include/utility.h"
#include "../../redSystem/include/threads.h"
#include "../../redCore/include/settings.h"

#include "redIOAsyncOp.h"
#include "redIOWorkerOrbis.h"
#include "redIOWorkerGeneric.h"
#include "redIOWorkerWin32.h"
#include "redIOSettings.h"

#include "redIOProfilerOrbis.h"
#include "redIOTraceWriterOrbis.h"
#include "redIOStats.h"
#include "serializationMemoryAllocator.h"

#ifdef RED_FATAL_ASSERT
#undef RED_FATAL_ASSERT
#endif

#ifdef RED_FATAL
#undef RED_FATAL
#endif

#define RED_FATAL_ASSERT ALWAYSENABLED_RED_FATAL_ASSERT
#define RED_FATAL ALWAYSENABLED_RED_FATAL


// FIXME: project dependency mess
#ifdef RED_PLATFORM_ORBIS
#include "../../redCore/include/profiler.h"
#include <razorcpu.h>
#else
#define PC_SCOPE(x)
#endif

#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
#define QUICK_N_DIRTY_STATS
#endif

// For the moment to limit risk.
#ifdef NO_EDITOR
# define RED_ALLOW_MERGE_IO_READS
#endif

namespace io
{
    static Bool g_useDifferentSorting = true;
    static Bool g_usePartialSorting = false;

#ifdef QUICK_N_DIRTY_STATS
red::HashMap<String, Uint64> g_extCacheBytesRead{ red::PoolDebug() };
red::RWSpinLock g_extCacheBytesReadLock;

REDIO_API void GetCacheBytesReadByExt(red::HashMap<String, Uint64>& outStats)
{
	RED_SCOPE_SHARED_LOCK(g_extCacheBytesReadLock);
	outStats = g_extCacheBytesRead;
}
#endif

// TODO: Can only merge when in sorted order, but could compare in both directions
const Uint32 c_maxMergeIOSize = RED_KILO_BYTE(256); // #tbd: not too big not too small, shouldn't hog too much I/O bandwidth and shouldn't cause I/O stalling from alloc failures
const Uint32 c_maxMergeReadThrough = 0;// 4095;// c_maxMergeIOSize - 1;
extern const Uint32 c_HACK_maxAsyncOpsThrottleStreamingForCutsceneAudio = 16; // how many ops to have in flight

namespace hacks
{
#ifndef NO_EDITOR
	static thread_local Bool g_tls_isPriorityReadInline = false;
	REDIO_API void SetIsPriorityReadInline_ThreadLocal(Bool isReadInline)
	{
		g_tls_isPriorityReadInline = isReadInline;
	}
#else
	const Bool g_tls_isPriorityReadInline = false;
#endif
}

#ifdef QUICK_N_DIRTY_STATS
		static Bool GIsMemoryDeserializationMemoryConstrained;
		static Uint32 GNumTotalTimesMemoryDeserializationMemoryConstrained;
		static Uint32 GNumMergedAsyncOps = 0;
		static Uint32 GMaxMergedAsyncOpRead = 0;
		static Uint32 GNumCacheHits = 0;
		static Uint64 GCachedBytesRead = 0;
		REDIO_API Bool IsMemoryDeserializationMemoryConstrained(Uint32& outNumTimesConstrainedTotal)
		{
			outNumTimesConstrainedTotal = GNumTotalTimesMemoryDeserializationMemoryConstrained;
			return GIsMemoryDeserializationMemoryConstrained;
		}

		REDIO_API Uint32 GetNumMergedAsyncOps()
		{
			return GNumMergedAsyncOps;
		}

		REDIO_API Uint32 GetMaxMergedAsyncOpRead()
		{
			return GMaxMergedAsyncOpRead;
		}

		REDIO_API void ResetMaxMergedAsyncOpRead()
		{
			GNumMergedAsyncOps = 0;
			GMaxMergedAsyncOpRead = 0;
		}

		REDIO_API void GetCacheStats(Uint32& outNumCacheHits, Uint64& outNumCachedBytesRead)
		{
			outNumCacheHits = GNumCacheHits;
			outNumCachedBytesRead = GCachedBytesRead;
		}
#endif

AsyncIO GAsyncIO;

const Uint32 RESERVED_ASYNC_OPS  = 1024;//#tbd: Xbox pull deploy seems rather slow at times...

#if defined( RED_PLATFORM_CONSOLE )
static const red::TAffinityMask THREAD_AFFINITY_MASK = ( 1ULL << 4 | 1ULL << 5 );
#endif

#ifndef RED_CONFIGURATION_FINAL
# define DEBUG_CALLBACKS 1
#else
# define DEBUG_CALLBACKS 0
#endif

thread_local Bool GIsCallbackThread;

#if DEBUG_CALLBACKS
# define CALLBACK_CHECK(x) do { RED_FATAL_ASSERT( ! GIsCallbackThread, "Cannot perform %s during callback!", RED_STRINGIFY(x) ); } while(false)
#else
# define CALLBACK_CHECK(x)
#endif

AsyncIO::AsyncIO()
	: m_asyncFileHandleCache( AsyncFileHandleCache::Create() )
	, m_shutdownFlag( false )
{
}

AsyncIO::~AsyncIO()
{
}

Bool AsyncIO::Init( const io::InitSetup& initSetup )
{
	// Shouldn't call twice anyway
	CALLBACK_CHECK( Init );

	m_memoryAllocator = red::CreateUniquePtr<MemoryAllocator>();
	m_asyncFileHandleCache->SetMemoryAllocator(m_memoryAllocator.Get());

	//#fixme: before using pool, need to make GAsyncIO into a ptr to avoid static init issues
	const Int32 numPris = eAsyncPriority_COUNT;
	for (Int32 priority = numPris - 1; priority >= 0; --priority)
	{
		m_asyncOpBuffers[priority].asyncOps.SetPool( red::PoolEngine() );
		m_asyncOpBuffers[priority].asyncOps.Reserve( RESERVED_ASYNC_OPS );
	}

    m_asyncOpBuffers[io::eAsyncPriority_GAME].queueWeight = 1;
    m_asyncOpBuffers[io::eAsyncPriority_AUDIO].queueWeight = 1;
	m_asyncOpBuffers[io::eAsyncPriority_UI].queueWeight = 9999;
    m_asyncOpBuffers[io::eAsyncPriority_FULLSCREENVIDEO].queueWeight = 999;// #tbd: Bink has realtime fill to shut itself off. Give it what it needs.

#if defined( RED_USE_IOWORKER_ORBIS )
# ifdef USE_PROFILER
	if ( initSetup.enableProfiler )
	{
		::io::orbis::ProfilerSetup profilerSetup;
		profilerSetup.traceBufferSize = 64 * 1024 * 1024;
		m_profilerOrbis.Reset( RED_NEW( ::io::orbis::Profiler )( profilerSetup ) );	
		m_traceWriterOrbis.Reset( RED_NEW( ::io::orbis::TraceWriter ) );
	}
# endif

	prv::IOWorkerOrbisSetup setup;
	setup.context = this;
	setup.fillAsyncOpsFunc = &AsyncIO::FillAsyncOpsCallback;
	setup.fillAudioAsyncOpsFunc = &AsyncIO::FillAudioAsyncOpsCallback;
	setup.asyncFileHandleCache = m_asyncFileHandleCache.Get();
	setup.shutdownFlag = &m_shutdownFlag;
	auto worker = red::CreateUniquePtr< prv::IOWorkerOrbis, red::PoolEngine >( setup );

#elif defined( RED_USE_IOWORKER_WIN32 )
	prv::IOWorkerWin32Setup setup;
	setup.context = this;
	setup.fillAsyncOpsFunc = &AsyncIO::FillAsyncOpsCallback;
	setup.asyncFileHandleCache = m_asyncFileHandleCache.Get();
	setup.shutdownFlag = &m_shutdownFlag;
	auto worker = red::CreateUniquePtr< prv::IOWorkerWin32, red::PoolEngine >( setup );

#elif defined( RED_USE_IOWORKER_GENERIC )
	prv::IOWorkerGenericSetup setup;
	setup.context = this;
	setup.fillAsyncOpsFunc = &AsyncIO::FillAsyncOpsCallback;
	setup.asyncFileHandleCache = m_asyncFileHandleCache.Get();
	setup.shutdownFlag = &m_shutdownFlag;
	auto worker = red::CreateUniquePtr< prv::IOWorkerGeneric, red::PoolEngine >( setup );
#else
#error Undefined IOWorker
#endif

	m_workerWakeEvent = &worker->GetWakeEvent();
	m_worker = std::move(worker);
	m_worker->InitThread();

	return true;
}

void AsyncIO::Shutdown()
{
	CALLBACK_CHECK( Shutdown );

	// #tbd: need a proper flush I/O
	// Make resilient to multiple shutdown requests
	if ( m_shutdownFlag.GetValue() )
		return;

	m_shutdownFlag.SetValue( true );
	
	m_workerWakeEvent->SetEvent();

	m_worker->JoinThread();

	// #fixme: Delete worker later, 
	// since when shutting down we'll kick the wake event owner by the worker.
	// Perhaps the event should now be owned by the worker instead
	//m_worker = nullptr;

	// It's possible there was a race condition: we queued some more I/O before shutdown, but the worker threads were out of work and saw the shutdown flag, so didn't process it
	// Threads shutdown; lock no longer necessary
	const Int32 numPris = eAsyncPriority_COUNT;
	for (Int32 priority = numPris - 1; priority >= 0; --priority)
	{
		while (!m_asyncOpBuffers[priority].asyncOps.Empty())
		{
			AsyncReadToken asyncReadToken = m_asyncOpBuffers[priority].asyncOps.Back().m_asyncReadToken;
			m_asyncOpBuffers[priority].asyncOps.PopBack();

			const FAsyncOpCallback callback = asyncReadToken.m_callback;
			if (callback)
			{
				AsyncReadToken opAsyncReadToken = asyncReadToken;
				callback(opAsyncReadToken, red::eAsyncResult_Canceled, 0, io::ShareableIOMemory(), 0, red::UniqueBuffer());
			}
		}
	}

	const Float shutdownTimeoutSeconds = 10.f;
	red::Timer waitTimer;
	for (;;)
	{
		RuntimeIOMemoryMetrics metrics;
		m_memoryAllocator->GetMemoryRuntimeMetrics(metrics);
		if (metrics.m_numAllocsInUse == 0)
		{
			break;
		}

		if (waitTimer.GetSeconds() > shutdownTimeoutSeconds)
		{
			RED_LOG_ERROR("AsyncIO Shutdown timelimite exceeded %f sec", shutdownTimeoutSeconds);
			break;
		}

		red::SleepOnCurrentThread(1);
	}

	m_asyncFileHandleCache->SetMemoryAllocator(nullptr);
	m_memoryAllocator = nullptr;

	// Delete last now, since its event may still be referenced while shutting down.
	m_worker = nullptr;
}

TFileHandle AsyncIO::OpenFile( const char* filePath, Uint32 asyncFlags /*= eAsyncFlag_None*/ )
{
	return m_asyncFileHandleCache->Open( filePath, asyncFlags );
}

void AsyncIO::ReleaseFile( TFileHandle fh )
{
	m_asyncFileHandleCache->Release( fh );
}

namespace helper
{
	// Really need a virtual...
#if defined( RED_USE_IOWORKER_ORBIS )
	static prv::IOWorkerOrbis* CastWorker(red::Thread* worker) { return static_cast<prv::IOWorkerOrbis*>(worker); }
#elif defined( RED_USE_IOWORKER_WIN32 )
	static prv::IOWorkerWin32* CastWorker(red::Thread* worker) { return static_cast<prv::IOWorkerWin32*>(worker); }
#elif defined( RED_USE_IOWORKER_GENERIC )
	static prv::IOWorkerGeneric* CastWorker(red::Thread* worker) { return static_cast<prv::IOWorkerGeneric*>(worker); }
#endif
}

struct AsyncOpBucket
{
    Uint32 begin = 0;
    Uint16 count = 0;
    Uint16 consumed = 0;
    
    // if number of io requests is higher that the value,
    // the a bucket will have longer chunks and higher prio
    static constexpr Uint32 c_burstTreshold = 80;
};
static RED_INLINE Uint32 CalcChunkSize( const AsyncOpBucket& b )
{
    return b.count;
}

union SecondPassSortKey
{
    Uint16 hash;
    struct
    {
        Uint16 index : 8;
        Uint16 criticalInv : 8;
    };
};

static RED_INLINE void ApplyCriticalValue( SecondPassSortKey* key, Uint32 value )
{
    RED_FATAL_ASSERT( value <= 0xff );
    key->criticalInv = 0xff - value;
}

struct SortFnForLoading
{
    RED_INLINE Bool operator()( const prv::AsyncOp& a, const prv::AsyncOp& b ) const
    {
		const Bool isACancelled = a.m_flags.m_cachedIsCancelled;
		const Bool isBCancelled = b.m_flags.m_cachedIsCancelled;
		if (isACancelled != isBCancelled)
		{
			// Move to the front of the list to get cleared out ASAP (vs the otherwise ever-expanding end)
			return isACancelled;
		}

        if( a.m_asyncFileHandle != b.m_asyncFileHandle )
            return a.m_asyncFileHandle < b.m_asyncFileHandle;

        return a.m_asyncReadToken.m_offset < b.m_asyncReadToken.m_offset;
    }
};

struct SortFnForCritical
{
    RED_INLINE Bool operator()( const prv::AsyncOp& a, const prv::AsyncOp& b ) const
    {
        const Bool isACancelled = a.m_flags.m_cachedIsCancelled;
        const Bool isBCancelled = b.m_flags.m_cachedIsCancelled;
        if( isACancelled != isBCancelled )
        {
            // Move to the front of the list to get cleared out ASAP (vs the otherwise ever-expanding end)
            return isACancelled;
        }

        if( a.m_critical != b.m_critical )
            return a.m_critical > b.m_critical;

        if( a.m_asyncFileHandle != b.m_asyncFileHandle )
            return a.m_asyncFileHandle < b.m_asyncFileHandle;

        return a.m_asyncReadToken.m_offset < b.m_asyncReadToken.m_offset;
    }
};

struct SortFnByFileHandleAsc
{
    RED_INLINE Bool operator()( const prv::AsyncOp& a, const prv::AsyncOp& b ) const
    {
		if( a.m_asyncFileHandle != b.m_asyncFileHandle )
            return a.m_asyncFileHandle < b.m_asyncFileHandle;

        return a.m_asyncReadToken.m_offset < b.m_asyncReadToken.m_offset;
    }
};

struct SortFnScrambler
{
    RED_INLINE Bool operator()( const prv::AsyncOp& a, const prv::AsyncOp& b ) const
    {
        if( a.m_secondPassSortKey != b.m_secondPassSortKey )
            return a.m_secondPassSortKey < b.m_secondPassSortKey;

        if( a.m_asyncFileHandle != b.m_asyncFileHandle )
            return a.m_asyncFileHandle < b.m_asyncFileHandle;

        return a.m_asyncReadToken.m_offset < b.m_asyncReadToken.m_offset;
    }
};

#if defined( USE_PROFILER )
static Uint8 AssignLetterToOp( const prv::AsyncOp& op )
{
    Uint8 value = op.m_critical;
    if( op.m_flags.m_cachedIsCancelled )
    {
        value = 'X';
    }
    else if( value == io::eAsyncPriority_COLLISION_CRITICAL )
    {
        value = 'C';
    }
    else if( value == io::eAsyncPriority_GAMEPLAY_CRITICAL )
    {
        value = 'G';
    }
    else if( value == io::eAsyncPriority_AUDIO || value == io::eAsyncPriority_AUDIO_CRITICAL )
    {
        value = 'A';
    }
	else if( value == io::eAsyncPriority_UI )
	{
		value = 'U';
	}
    else if( value == eAsyncPriority_DEFAULT )
    {
        value = (Uint8)op.m_asyncFileHandle;
        const Uint8 count = 'z' - 'a';
        value = 'a' + ((value & 0xff) % count);
    }
    return value;
}
#endif

static void CacheCancelledFlag_NoLock( red::CircularBuffer<prv::AsyncOp>& asyncOps )
{
    for( Uint32 i = 0, len = asyncOps.Size(); i < len; ++i )
    {
        prv::AsyncOp& asyncOp = asyncOps[i];
        if( asyncOp.m_asyncReadToken.IsCancelRequested() )
        {
            // Cache it; must not change while sorting
            asyncOp.m_flags.m_cachedIsCancelled = true;
        }
    }
}

void AsyncIO::SortIOQueues( const SortFlags flags )
{
	RED_SCOPE_LOCK( m_bufferLock );

    const Int32 numPris = eAsyncPriority_COUNT;
    for( Int32 i = numPris - 1; i >= 0; --i )
    {
        auto& asyncOps = m_asyncOpBuffers[i].asyncOps;
        CacheCancelledFlag_NoLock( asyncOps );
    }

    if( flags.loadingMode )
    {
        SortIOQueuesForLoadingMode();
    }
    else
    {
        if( g_useDifferentSorting )
        {
            SortIOQueuesForStreamingMode( flags.drivingMode );
        }
        else
        {
            SortIOQueuesForLoadingMode();
        }
    }

    m_sortFrameNo += 1;

#if defined( USE_PROFILER )
    {
        m_dbgGameQueueString.Clear();
        auto& asyncOps = m_asyncOpBuffers[eAsyncPriority_GAME].asyncOps;
        for( Uint32 i = 0, len = asyncOps.Size(); i < len; ++i )
        {
            const prv::AsyncOp& asyncOp = asyncOps[i];
            if( m_dbgGameQueueString.Full() )
                break;

            const Uint8 value = AssignLetterToOp( asyncOp );
            m_dbgGameQueueString.PushBack( value );
        }
    }
#endif
}

void AsyncIO::SortIOQueuesForLoadingMode()
{
    const Int32 numPris = eAsyncPriority_COUNT;
    for( Int32 i = numPris - 1; i >= 0; --i )
    {
        auto& asyncOps = m_asyncOpBuffers[i].asyncOps;
        asyncOps.Sort( SortFnForLoading() );
    }
}

void AsyncIO::SortIOQueuesForStreamingMode( Bool drivingMode )
{
#ifdef RED_PLATFORM_CONSOLE
    
    if( g_usePartialSorting )
    {
        const Uint32 freshTreshold = 16; // drivingMode ? 64 : 32;

        const Int32 numPris = eAsyncPriority_COUNT;
        for( Int32 i = numPris - 1; i >= 0; --i )
        {
            auto& asyncOps = m_asyncOpBuffers[i].asyncOps;
            const Uint64 lastSortedTickedNo = m_asyncOpBuffers[i].lastSortedTicket;
            Uint32 numFresh = 0;
            for( auto rit = asyncOps.RBegin(), rEnd = asyncOps.REnd(); rit != rEnd; ++rit )
            {
                if( rit->m_ticket == lastSortedTickedNo )
                    break;


                numFresh += 1;
            }

            if( (asyncOps.Size() < freshTreshold) ||  (numFresh < freshTreshold) )
            {
                asyncOps.Sort( SortFnForCritical() );
            }
            else
            {
                asyncOps.SortLastN( SortFnForCritical(), numFresh );
            }

            m_asyncOpBuffers[i].lastSortedTicket = asyncOps.Back().m_ticket;
        }
    }
    else
    {
		red::StaticArray<AsyncOpBucket, 256> buckets;

        const Int32 numPris = eAsyncPriority_COUNT;
        for( Int32 i = numPris - 1; i >= 0; --i )
        {
            auto& asyncOps = m_asyncOpBuffers[i].asyncOps;

            if( asyncOps.Empty() )
                continue;

            asyncOps.Sort( SortFnByFileHandleAsc() );
            // create buckets based on data sorted by fileHandle.
                // so we will know how many requests we have from given archive
            buckets.Clear();
            Uint32 currentFileHandle = 0xffffffff;
            for( Uint32 i = 0, len = asyncOps.Size(); i < len; ++i )
            {
                const prv::AsyncOp& asyncOp = asyncOps[i];
                if( currentFileHandle != asyncOp.m_asyncFileHandle )
                {
                    AsyncOpBucket& b = buckets.EmplaceBack();
                    b.begin = i;
                    b.count = 1;
                    currentFileHandle = asyncOp.m_asyncFileHandle;
                }
                else
                {
                    buckets.Back().count += 1;
                }
            }

            // there is no sense to burn CPU cycles
            const Uint32 nbBuckets = buckets.Size();
            if( nbBuckets <= 1 )
                return;

            // starting from the smallest one.
            // the idea is to assign sorting index in order with traversing buckets
            // each bucket will be consumed in chunks
            // operations with critical value will be prioritized.
            const Uint8 frame = ((m_sortFrameNo % 0xff) >> 5) % buckets.Size();
            Uint32 currentBucket = frame;
            Uint8 sortIndex = 0;
            for( Uint32 i = 0, len = asyncOps.Size(); i < len; ++i )
            {
                // find first not consumed bucket, 
                while( buckets[currentBucket].count == buckets[currentBucket].consumed )
                {
                    currentBucket = (currentBucket + 1) % buckets.Size();
                    sortIndex += 1;
                }
                AsyncOpBucket& b = buckets[currentBucket];
                prv::AsyncOp& asyncOp = asyncOps[b.begin + b.consumed];

                // calculate chunk size for the bucket,
                Uint32 mod = CalcChunkSize( b );

                Uint16 critical = asyncOp.m_critical;

                // check how much in troubles we are
                if( b.count > AsyncOpBucket::c_burstTreshold && (asyncOp.m_critical == 0) )
                {
                    critical = Max( critical, Min( (Uint16)eAsyncPriority_RESERVED_CRITICAL, b.count >> 1 ) );
                }

                if( asyncOp.m_flags.m_cachedIsCancelled )
                    critical = eAsyncPriority_INVALID;
                // 
                SecondPassSortKey key;
                ApplyCriticalValue( &key, critical );
                key.index = (critical >= eAsyncPriority_RESERVED_CRITICAL) ? 0 : (frame - currentBucket);
                asyncOp.m_secondPassSortKey = key.hash;

                b.consumed += 1;

                // switch to a next bucket if needed
                if( (b.consumed % mod) == 0 )
                {
                    currentBucket = (currentBucket + 1) % buckets.Size();
                    sortIndex += 1;
                }
            }
            asyncOps.Sort( SortFnScrambler() );
        }
    }
#else
    SortIOQueuesForLoadingMode();
#endif
}

void AsyncIO::EnabledLoadingMode(Bool value)
{
	helper::CastWorker(m_worker.Get())->EnableLoadingMode(value);
}

void AsyncIO::EnableSavingMode(Bool value)
{
	helper::CastWorker(m_worker.Get())->EnableSavingMode(value);
}

// #tbd: really shouldn't have separate priorities, but just in case...
static thread_local Bool g_tls_bulkReading = false;
static thread_local red::DynArray< prv::AsyncOp > g_tls_bulkReadAsyncOps[ eAsyncPriority_COUNT ]{red::PoolEngine(), red::PoolEngine(), red::PoolEngine(), red::PoolEngine() };
void AsyncIO::ADVANCED_BeginBulkReadThreadLocal()
{
	RED_FATAL_ASSERT(!g_tls_bulkReading);
#ifdef RED_ASSERTS_ENABLED
	for (Uint32 i = 0; i < eAsyncPriority_COUNT; ++i)
	{
		RED_FATAL_ASSERT(g_tls_bulkReadAsyncOps[i].Empty());
	}
#endif
	g_tls_bulkReading = true;
}

void AsyncIO::ADVANCED_FinishBulkReadThreadLocal()
{
	RED_FATAL_ASSERT(g_tls_bulkReading);

	for (Uint32 i = 0; i < eAsyncPriority_COUNT; ++i)
	{
		// Sort for PC HDD (and possibly for consoles if their async op buffers are full)
		// Doesn't really matter if streaming queue here, worst case the IO worker grabs data out of it before it's been sorted by distance, so might as well
		// do the sort here now.
		std::sort(g_tls_bulkReadAsyncOps[i].Begin(), g_tls_bulkReadAsyncOps[i].End(), [](const auto& a, const auto& b) {
			if (a.m_asyncFileHandle != b.m_asyncFileHandle)
			{
				return a.m_asyncFileHandle < b.m_asyncFileHandle;
			}
			return a.m_asyncReadToken.m_offset < b.m_asyncReadToken.m_offset;
		});
	}

	// Lock the asyncOps priority queue
	Int32 numPris = eAsyncPriority_COUNT;
	for (Int32 i = numPris - 1; i >= 0; --i)
	{
		if (g_tls_bulkReadAsyncOps[i].Empty())
		{
			continue;
		}

		RED_SCOPE_LOCK(m_bufferLock);

		for (const auto& asyncOp : g_tls_bulkReadAsyncOps[i])
		{
			m_asyncOpBuffers[i].asyncOps.PushBack(asyncOp);
		}
	}

	for (Uint32 i = 0; i < eAsyncPriority_COUNT; ++i)
	{
		g_tls_bulkReadAsyncOps[i].Clear();
	}
	g_tls_bulkReading = false;

	// Not under the lock, becaue our locks don't play nicely with kernel events
	m_workerWakeEvent->SetEvent();
}

void AsyncIO::BeginRead( TFileHandle fh, const AsyncReadToken& asyncReadToken, EAsyncPriority priority /*= eAsyncPriority_Normal*/ )
{
#ifndef NO_EDITOR
	priority = io::eAsyncPriority_DEFAULT;
#endif

    const EAsyncPriority oryginalPriority = priority;
    if( priority >= eAsyncPriority_COUNT )
    {
        priority = eAsyncPriority_GAME; // TODO
    }

    const Uint64 ticket = m_asyncOpTicketAllocator.Increment();
	if (asyncReadToken.m_ioContext)
	{
		asyncReadToken.m_ioContext->SetAsyncOpId(ticket);
	}

	RED_FATAL_ASSERT( fh != INVALID_FILE_HANDLE );
	//RED_FATAL_ASSERT( asyncReadToken.m_buffer != nullptr || asyncReadToken.m_numberOfBytesToRead == 0, "Invalid read request: buffer=%p, numberOfBytesToRead=%u", asyncReadToken.m_buffer, asyncReadToken.m_numberOfBytesToRead );

	m_asyncFileHandleCache->AddRef(fh); // worker will release file handle when finished

	const AnsiChar* const fileName = m_asyncFileHandleCache->GetFileName( fh );
	if ( !asyncReadToken.m_debugLogicalFileName || !asyncReadToken.m_debugLogicalFileName[0] )
	{
		const_cast<AsyncReadToken&>( asyncReadToken ).m_debugLogicalFileName = fileName;
	}

#ifdef RED_PROFILE_FILE_SYSTEM
	Uint32 asyncOpId = 0;
	asyncOpId = IIOProfiler::Get()->ProfileAllocRequestId();
	RED_ASSERT( fileName );
	RED_ASSERT( asyncReadToken.m_debugLogicalFileName );
	IIOProfiler::Get()->ProfileAsyncIOReadScheduled( fh, asyncOpId, priority, asyncReadToken.m_offset, asyncReadToken.m_numberOfBytesToRead, fileName, asyncReadToken.m_debugLogicalFileName );
#endif

	if ( m_shutdownFlag.GetValue() )
	{
		m_asyncFileHandleCache->Release( fh ); // release addref now, since no workers
		const FAsyncOpCallback callback = asyncReadToken.m_callback;
		if ( callback )
		{
			AsyncReadToken opAsyncReadToken = asyncReadToken;
			callback( opAsyncReadToken, red::eAsyncResult_Canceled, 0, io::ShareableIOMemory(), 0, red::UniqueBuffer() );
		}
		return;
	}

#ifdef USE_PROFILER
# ifdef RED_USE_IOWORKER_ORBIS
	if ( m_traceWriterOrbis )
	{
		//#tbd: should really be able to map when writing, get from archives
		const auto* asyncFile = m_asyncFileHandleCache->GetAsyncFile( fh );
		m_traceWriterOrbis->RegisterLogicalFile(
			asyncFile->GetFileDescriptor(),
			asyncReadToken.m_offset, 
			asyncReadToken.m_numberOfBytesToRead, 
			asyncReadToken.m_debugLogicalFileName
		);
	}
# endif
#endif

	prv::AsyncOp asyncOp;
	asyncOp.m_ticket = ticket;
	asyncOp.m_asyncFileHandle = fh;
	asyncOp.m_asyncReadToken = asyncReadToken;
	asyncOp.m_priority = priority;
    asyncOp.m_critical = oryginalPriority;
    if( ( asyncOp.m_asyncReadToken.m_requestSource == RequestSource::AudioSystem_WwiseContainerStreaming || asyncOp.m_asyncReadToken.m_requestSource == RequestSource::AudioSystem_WwiseLowLevelIO  )  
		&& m_hackForThrottleStreamingForAudioEnabled )
    {
        asyncOp.m_critical = eAsyncPriority_AUDIO_CRITICAL;
    }
#ifdef RED_PROFILE_FILE_SYSTEM
	asyncOp.m_operationId = asyncOpId;
	if ( asyncOp.m_asyncReadToken.m_ioContext )
	{
		const String debugName( asyncOp.m_asyncReadToken.m_debugLogicalFileName );
		Uint32 numberOfBytesToRead = asyncReadToken.m_numberOfBytesToRead;
		asyncOp.m_asyncReadToken.m_ioContext->SetCancelCallback( [asyncOpId, priority, debugName, numberOfBytesToRead]( void )
		{
			IIOProfiler::Get()->ProfileAsyncIOReadCanceled( asyncOpId, priority, numberOfBytesToRead, debugName.AsChar() );
		} );
	}
#endif

	if (!asyncReadToken.m_buffer)
	{
		MemoryAllocator::RequestToken req;

		// First see if already cached and available
		req.mustAllocCachedIOResult = true;
		req.cacheKey.m_fileHandle = fh;
		req.cacheKey.m_fileOffset = asyncReadToken.m_offset;

		req.m_debugFileName = asyncReadToken.m_debugLogicalFileName;
		req.numBytesForIO = asyncReadToken.m_numberOfBytesToRead;
		req.numBytesForDecompressor = asyncReadToken.m_numberOfBytesToAllocateForDecompressor;

		MemoryAllocator::AllocResult result;

		if (m_memoryAllocator->TryAlloc(req, result))
		{
#ifdef RED_PROFILE_FILE_SYSTEM
			{
				IIOProfiler::Get()->ProfileAsyncIOReadTakenFromQueue(asyncOp.m_operationId, asyncOp.m_priority, asyncOp.m_asyncReadToken.m_numberOfBytesToRead, asyncOp.m_asyncReadToken.m_debugLogicalFileName, false);
			}
#endif

			auto bufferForIO = io::ShareableIOMemory(std::move(result.m_bufferForIO));
			auto bufferForDecompressor = std::move(result.m_bufferForDecompressor);
			
			// FIXME API
			bufferForIO = io::ShareableIOMemory(std::move(bufferForIO), asyncReadToken.m_offset);

#ifdef QUICK_N_DIRTY_STATS
			GNumCacheHits += 1;
			GCachedBytesRead += bufferForIO.GetSize();
			const char* ext = red::StrchrR(asyncOp.m_asyncReadToken.m_debugLogicalFileName, '.');
			if (!ext)
			{
				ext = "<unknown>";
			}

			{
				RED_SCOPE_LOCK(g_extCacheBytesReadLock);
				g_extCacheBytesRead[ext] += bufferForIO.GetSize();
			}
#endif
			
			m_asyncFileHandleCache->Release(fh); // release addref now, since no workers
			const FAsyncOpCallback callback = asyncReadToken.m_callback;
			if (callback)
			{
				AsyncReadToken opAsyncReadToken = asyncReadToken;
				callback(opAsyncReadToken, red::eAsyncResult_Success, opAsyncReadToken.m_numberOfBytesToRead, std::move(bufferForIO), 0, std::move(bufferForDecompressor));
			}
			return;
		}
	}

	if (g_tls_bulkReading && !hacks::g_tls_isPriorityReadInline)
	{
		g_tls_bulkReadAsyncOps[priority].PushBack(asyncOp);
		return;
	}
	
    // Lock the asyncOps priority queue
	{
		auto& buffer = m_asyncOpBuffers[priority];
		RED_SCOPE_LOCK( m_bufferLock );

#ifndef NO_EDITOR
		if (hacks::g_tls_isPriorityReadInline)
		{
			// Probably a ReadInline(), which can block everything (e.g., all other threads locked and waiting for it to finish)
			const Bool hasRequiredMemory = (asyncOp.m_asyncReadToken.m_buffer || asyncOp.m_asyncReadToken.m_numberOfBytesToRead == 0) && asyncOp.m_asyncReadToken.m_numberOfBytesToAllocateForDecompressor == 0;
			RED_FATAL_ASSERT(hasRequiredMemory);
			buffer.asyncOps.PushFront(asyncOp);
		}
		else
		{
			buffer.asyncOps.PushBack(asyncOp);
		}
#else
		buffer.asyncOps.PushBack( asyncOp );
#endif
	}

	// Not under the lock, becaue our locks don't play nicely with kernel events
	m_workerWakeEvent->SetEvent();
}

void AsyncIO::FillAsyncOps(Uint32 maxReadyAsyncOps, prv::FillBuffers& fillBuffers)
{
	// Lock the priority queue while working on it
	RED_SCOPE_LOCK(m_bufferLock);

#ifdef RED_PLATFORM_CONSOLE
    for( Uint32 i = 0, size = eAsyncPriority_COUNT; i < size; ++i )
    {
        auto& buffer = m_asyncOpBuffers[i].asyncOps;
        CacheCancelledFlag_NoLock( buffer );
        std::stable_sort( buffer.Begin(), buffer.End(), []( const prv::AsyncOp& a, const prv::AsyncOp& b )
        {
            return a.m_flags.m_cachedIsCancelled > b.m_flags.m_cachedIsCancelled;
        } );
    }
#endif
	// NOTE: outReadyAsyncOps could potentially still have ops from the previous time in it, so we do our own count here
	Uint32 numReadyAsyncOpsCollected = 0;

	while (numReadyAsyncOpsCollected < maxReadyAsyncOps)
	{
		const Uint32 numReadyBefore = fillBuffers.readyAsyncOps.Size();
		if (!UpdateActivePriority_NoLock())
		{
			break;
		}
		if (!TryFillNextScheduledAsyncOp_NoLock(fillBuffers, m_currentActivePriority))
		{
			break;
		}
		
		RED_FATAL_ASSERT(fillBuffers.readyAsyncOps.Empty() ||
			fillBuffers.readyAsyncOps.Back().m_internalAsyncOp.m_asyncReadToken.m_buffer ||
			fillBuffers.readyAsyncOps.Back().m_internalAsyncOp.m_asyncReadToken.m_numberOfBytesToRead == 0);

        if( numReadyBefore < fillBuffers.readyAsyncOps.Size() )
        {
            numReadyAsyncOpsCollected += 1;
        }
	}

    fillBuffers.readyAsyncOps.Sort( []( const auto& opA, const auto& opB ) {
        const auto& a = opA.m_internalAsyncOp;
        const auto& b = opB.m_internalAsyncOp;

		// this make really significant difference for appearances and gmpl
		// with marginal impact to enviro and audio
		if( a.m_critical != b.m_critical )
			return a.m_critical > b.m_critical;

        if( a.m_asyncFileHandle != b.m_asyncFileHandle )
            return a.m_asyncFileHandle < b.m_asyncFileHandle;

        return a.m_asyncReadToken.m_offset < b.m_asyncReadToken.m_offset;
    } );

#if defined( USE_PROFILER )
    {
        m_dbgReadyOpsString.Clear();
        auto& buffer = fillBuffers.readyAsyncOps;
        for( auto it = buffer.Begin(); it != buffer.End(); ++it )
        {
            if( m_dbgReadyOpsString.Full() )
                break;

            const auto& asyncOp = it->m_internalAsyncOp;
            const Uint8 value = AssignLetterToOp( asyncOp );
            m_dbgReadyOpsString.PushBack( value );
        }
    }
#endif
}

static RED_INLINE Bool IsMergable( RequestSource source )
{
    return source != RequestSource::AudioSystem_SoundBankManager &&
        source != RequestSource::AudioSystem_WwiseContainerStreaming &&
        source != RequestSource::AudioSystem_WwiseLowLevelIO;
}

void AsyncIO::FillAudioAsyncOps(prv::FillBuffers& fillBuffers, Uint32 maxReadyAsyncOps)
{
	// Lock the priority queue while working on it
	RED_SCOPE_LOCK(m_bufferLock);

	red::CircularBuffer< prv::AsyncOp > &asyncOps = m_asyncOpBuffers[eAsyncPriority_AUDIO].asyncOps;
	for (Uint32 i = 0; i < maxReadyAsyncOps && !asyncOps.Empty(); i++)
	{
		if (!TryFillNextScheduledAsyncOp_NoLock(fillBuffers, eAsyncPriority_AUDIO))
		{
			break;
		}
	}
}

Bool AsyncIO::TryFillNextScheduledAsyncOp_NoLock(prv::FillBuffers& fillBuffers, Uint32 priority)
{
#ifdef QUICK_N_DIRTY_STATS
	GIsMemoryDeserializationMemoryConstrained = false;
#endif

	if (!UpdateActivePriority_NoLock())
	{
		return false;
	}

	auto& currentBuffer = m_asyncOpBuffers[priority];
    RED_FATAL_ASSERT(!currentBuffer.asyncOps.Empty(), "UpdateActivePriority_NoLock() shouldn't have passed");

    // Grab all canceled first
    while( !currentBuffer.asyncOps.Empty() && currentBuffer.asyncOps.Front().m_flags.m_cachedIsCancelled )
    {
        const auto& asyncOp = currentBuffer.asyncOps.Front();
#ifdef RED_PROFILE_FILE_SYSTEM
        IIOProfiler::Get()->ProfileAsyncIOReadTakenFromQueue( asyncOp.m_operationId, asyncOp.m_priority, asyncOp.m_asyncReadToken.m_numberOfBytesToRead, asyncOp.m_asyncReadToken.m_debugLogicalFileName, true );
#endif

        prv::ReadyAsyncOp cancelledAsyncOp{};
        cancelledAsyncOp.m_internalAsyncOp = std::move( currentBuffer.asyncOps.Front() );
        currentBuffer.asyncOps.PopFront();
        fillBuffers.cancelledAsyncOps.PushBack( std::move( cancelledAsyncOp ) );
    }

    if( currentBuffer.asyncOps.Empty() )
        return true;

	const AsyncReadToken tokenCopy = currentBuffer.asyncOps.Front().m_asyncReadToken;
	io::ShareableIOMemory bufferForIO;
	red::UniqueBuffer bufferForDecompressor;
	Uint32 numAsyncOpsToPop = 1;
	Bool bufferForIOHasCachedIOResult = false;

	const auto fnCanMergeNextAsyncOp = [](const prv::AsyncOp& currentAsyncOp, const prv::AsyncOp& nextAsyncOp) -> Bool
	{
#ifndef RED_ALLOW_MERGE_IO_READS
		return false;
#endif

        if( currentAsyncOp.m_critical > eAsyncPriority_RESERVED_CRITICAL ||
            nextAsyncOp.m_critical > eAsyncPriority_RESERVED_CRITICAL )
        {
            return false;
        }

		const auto& currentToken = currentAsyncOp.m_asyncReadToken;
		const auto& nextToken = nextAsyncOp.m_asyncReadToken;
		RED_FATAL_ASSERT(currentToken.m_offset >= 0 && nextToken.m_offset >= 0);
        
        if( currentToken.m_requestSource != nextToken.m_requestSource )
        {
            return false;
        }

        if( !IsMergable( currentToken.m_requestSource ) )
            return false;

		// Can't merge different files
		if (currentAsyncOp.m_asyncFileHandle != nextAsyncOp.m_asyncFileHandle)
		{
			return false;
		}

		// Can't merge memory if it's pre-allocated
		if (currentAsyncOp.m_asyncReadToken.m_buffer || nextAsyncOp.m_asyncReadToken.m_buffer)
		{
			return false;
		}

		// Allow for forwards overlap. The archives align the reads, and the DMA hardware is supposedly smart enough to combine them.
		return (currentToken.m_offset <= nextToken.m_offset) && (currentToken.m_offset + currentToken.m_numberOfBytesToRead + c_maxMergeReadThrough >= nextToken.m_offset);
	};

	if (!tokenCopy.m_buffer && tokenCopy.m_numberOfBytesToRead > 0)
	{
		if (tokenCopy.m_HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers)
		{
			constexpr Uint32 memOverheadFudge = 1024; // help avoid surprises if fail to allocate too close to max budget size
			RuntimeIOMemoryMetrics ioMetrics;
			m_memoryAllocator->GetMemoryRuntimeMetrics(ioMetrics);
			RED_FATAL_ASSERT(ioMetrics.m_memoryBudgetTotal >= memOverheadFudge);
			if (tokenCopy.m_numberOfBytesToRead >= (ioMetrics.m_memoryBudgetTotal - memOverheadFudge))
			{
				ALWAYSENABLED_RED_FATAL_ASSERT(tokenCopy.m_numberOfBytesToAllocateForDecompressor == 0, "Oversized DDB should have had own memory to decompress into directly");

				RED_LOG_WARNING("!!! Compressed DDB requires too much I/O memory for deserialization: %u. Allocating directly!", tokenCopy.m_numberOfBytesToRead);
				//RED_FATAL_ASSERT( !::IsGameMode(), "The game is requesting too much I/O memory for deserialization: %u", req.numBytesForIO );
				bufferForIO = io::ShareableIOMemory(red::CreateUniqueBuffer< red::PoolEngine >(tokenCopy.m_numberOfBytesToRead, 16));
			}
		}

		if (!bufferForIO.Data())
		{
			MemoryAllocator::RequestToken req;

			// First see if already cached and available
			req.mustAllocCachedIOResult = true;
			req.cacheKey.m_fileHandle = currentBuffer.asyncOps.Front().m_asyncFileHandle;
			req.cacheKey.m_fileOffset = tokenCopy.m_offset;

			req.m_debugFileName = tokenCopy.m_debugLogicalFileName;
			req.numBytesForIO = tokenCopy.m_numberOfBytesToRead;
			req.numBytesForDecompressor = tokenCopy.m_numberOfBytesToAllocateForDecompressor;

			MemoryAllocator::AllocResult result;

			if (m_memoryAllocator->TryAlloc(req, result))
			{
				bufferForIOHasCachedIOResult = true;
				bufferForIO = io::ShareableIOMemory(std::move(result.m_bufferForIO));
				bufferForDecompressor = std::move(result.m_bufferForDecompressor);

#ifdef QUICK_N_DIRTY_STATS
				GNumCacheHits += 1;
				GCachedBytesRead += bufferForIO.GetSize();

				const char* ext = red::StrchrR(tokenCopy.m_debugLogicalFileName, '.');
				if (!ext)
				{
					ext = "<unknown>";
				}

				{
					RED_SCOPE_LOCK(g_extCacheBytesReadLock);
					g_extCacheBytesRead[ext] += bufferForIO.GetSize();
				}
#endif
			}
		}

		// Try to merge reads. NOTE: only if not requesting additional decompression memory, which vastly simplifies the async op merging process.
		// The resource throttler (at a higher level) now has some dedicated decompression memory to supply, so we can still handle compressed resources  even if we're not requesting any decompression memory here.
		// (It's also a waste to allocate decompression memory for each individual async op when we're throttled on the decompression/deserialization side anyway).
		// Most required decompression allocs are general small, so it makes more sense to have dedicated throttler decompression memory instead)
		if (!bufferForIO.Data())
		{
			Uint64 maxReadEndPosition = tokenCopy.m_offset + tokenCopy.m_numberOfBytesToRead;

			const auto& currentAsyncOps = m_asyncOpBuffers[priority].asyncOps;
			if (tokenCopy.m_numberOfBytesToAllocateForDecompressor == 0)
			{
				for (Uint32 i = 1; i < currentAsyncOps.Size(); ++i)
				{
					if (currentAsyncOps[i].m_flags.m_cachedIsCancelled) // shouldn't happen, but stop merging
					{
						break;
					}

					if (currentAsyncOps[i].m_asyncReadToken.m_numberOfBytesToAllocateForDecompressor > 0)
					{
						break;
					}

					if (!fnCanMergeNextAsyncOp( currentAsyncOps[i - 1], currentAsyncOps[i] ))
					{
						break;
					}

					const Uint64 tempMaxReadEndPosition = Max<Uint64>(maxReadEndPosition, currentAsyncOps[i].m_asyncReadToken.m_offset + currentAsyncOps[i].m_asyncReadToken.m_numberOfBytesToRead);
					const Uint32 tempMergedNumberOfBytesToRead = (Uint32)(tempMaxReadEndPosition - tokenCopy.m_offset);

					if (tempMergedNumberOfBytesToRead > c_maxMergeIOSize)
					{
						break;
					}

					// Reads can overlap (or even be the same). Don't just blindly assume contiguous and overallocate.
					// Stil allocate decompression separately for now - bit of a waste if the same token offset+size though.
					maxReadEndPosition = tempMaxReadEndPosition;
					numAsyncOpsToPop += 1;
				}
			}

			RED_FATAL_ASSERT(maxReadEndPosition >= (Uint64)tokenCopy.m_offset);
			const Uint32 mergedNumberOfBytesToRead = (Uint32)(maxReadEndPosition - tokenCopy.m_offset);

			MemoryAllocator::RequestToken req;
			req.cacheKey.m_fileHandle = currentBuffer.asyncOps.Front().m_asyncFileHandle;
			req.cacheKey.m_fileOffset = tokenCopy.m_offset;
			req.m_debugFileName = tokenCopy.m_debugLogicalFileName; //?? sense if merged
			req.numBytesForIO = mergedNumberOfBytesToRead;
			req.numBytesForDecompressor = tokenCopy.m_numberOfBytesToAllocateForDecompressor;

			MemoryAllocator::AllocResult result;
			
			if (!m_memoryAllocator->TryAlloc(req, result))
			{
#ifdef QUICK_N_DIRTY_STATS
				GNumTotalTimesMemoryDeserializationMemoryConstrained += 1;
				GIsMemoryDeserializationMemoryConstrained = true;
#endif
				return false;
			}
#ifdef QUICK_N_DIRTY_STATS
			if (numAsyncOpsToPop > 1)
			{
				GNumMergedAsyncOps += numAsyncOpsToPop;
				if (mergedNumberOfBytesToRead > GMaxMergedAsyncOpRead)
				{
					GMaxMergedAsyncOpRead = mergedNumberOfBytesToRead;
				}
			}
#endif

			bufferForIO = io::ShareableIOMemory(std::move(result.m_bufferForIO));
			bufferForDecompressor = std::move(result.m_bufferForDecompressor);
		}
	}


	// Not doing it for merged ops, since if failed to find cached data for the beginning then I wouldn't expect it to find it a split microsecond later for the whole merged async op.
	// Maybe once in a blue moon, but that would overcomplicate things even more.
	if (bufferForIOHasCachedIOResult)
	{
		RED_FATAL_ASSERT(numAsyncOpsToPop == 1, "Should not have merged ops if cached I/O for first async op was already available");
	}

	if (numAsyncOpsToPop > 1)
	{
		prv::ReadyAsyncOp mainMergedAsyncOp{};
		{
			mainMergedAsyncOp.m_internalAsyncOp = prv::AsyncOp();
			mainMergedAsyncOp.m_internalAsyncOp.m_priority = (EAsyncPriority)priority;
            mainMergedAsyncOp.m_internalAsyncOp.m_critical = currentBuffer.asyncOps.Front().m_critical;
			mainMergedAsyncOp.m_mergedAsyncOpID = ++m_mergedAsyncOpIDCounter;

			mainMergedAsyncOp.m_internalMemoryForIO = ShareableIOMemory(std::move(bufferForIO), tokenCopy.m_offset);
			RED_FATAL_ASSERT(mainMergedAsyncOp.m_internalMemoryForIO.Data());
			// Patch the token
			auto& mergedToken = mainMergedAsyncOp.m_internalAsyncOp.m_asyncReadToken;
			mergedToken.m_debugLogicalFileName = "<merged asyncops>";
			mergedToken.m_offset = tokenCopy.m_offset;
			mergedToken.m_numberOfBytesToRead = mainMergedAsyncOp.m_internalMemoryForIO.GetSize();
			RED_FATAL_ASSERT(mergedToken.m_numberOfBytesToRead >= tokenCopy.m_numberOfBytesToRead);

#ifdef RED_PROFILE_FILE_SYSTEM
			mainMergedAsyncOp.m_internalAsyncOp.m_operationId = IIOProfiler::Get()->ProfileAllocRequestId();
#endif
			mainMergedAsyncOp.m_internalAsyncOp.m_ticket = UINT64_MAX;
			mainMergedAsyncOp.m_internalAsyncOp.m_asyncFileHandle = currentBuffer.asyncOps.Front().m_asyncFileHandle;
			mainMergedAsyncOp.m_internalAsyncOp.m_asyncReadToken.m_buffer = mainMergedAsyncOp.m_internalMemoryForIO.Data();
		}

		for (Uint32 i = 0; i < numAsyncOpsToPop; ++i)
		{
			prv::ReadyAsyncOp readyAsyncOp{};
			readyAsyncOp.m_mergedAsyncOpID = mainMergedAsyncOp.m_mergedAsyncOpID;
			readyAsyncOp.m_internalAsyncOp = std::move(currentBuffer.asyncOps.Front());

			// Remove the found entry
			currentBuffer.asyncOps.PopFront();

			RED_FATAL_ASSERT(bufferForDecompressor.GetSize() == 0); // will be decompressed by dedicated decompressor memory
			RED_FATAL_ASSERT(mainMergedAsyncOp.m_internalMemoryForIO);
			RED_FATAL_ASSERT(mainMergedAsyncOp.m_internalMemoryForIO.GetSize() >= readyAsyncOp.m_internalAsyncOp.m_asyncReadToken.m_numberOfBytesToRead);
			readyAsyncOp.m_internalMemoryForIO = mainMergedAsyncOp.m_internalMemoryForIO; // shared buffer for single read
			readyAsyncOp.m_internalAsyncOp.m_asyncReadToken.m_buffer = nullptr; // only manMergeAsyncOp will write into the buffer

			auto& ioContext = readyAsyncOp.m_internalAsyncOp.m_asyncReadToken.m_ioContext;
			if (ioContext)
			{
				// The I/O worker has to start it soon enough, and might as well do it here where it's all unified
				ioContext->SetLoadingState(IOContext::LoadingState::Loading);
				ioContext->NotifyIOStart();
			}

#ifdef RED_PROFILE_FILE_SYSTEM
			{
				const auto& nextAsyncOp = readyAsyncOp.m_internalAsyncOp;
				IIOProfiler::Get()->ProfileAsyncIOReadTakenFromQueue(nextAsyncOp.m_operationId, nextAsyncOp.m_priority, nextAsyncOp.m_asyncReadToken.m_numberOfBytesToRead, nextAsyncOp.m_asyncReadToken.m_debugLogicalFileName, false);
			}
#endif

			fillBuffers.mergedAsyncOps.PushBack(std::move(readyAsyncOp));
		}

		fillBuffers.readyAsyncOps.PushBack(std::move(mainMergedAsyncOp));
	}
	else
	{
		RED_FATAL_ASSERT(numAsyncOpsToPop == 1);

		prv::ReadyAsyncOp readyAsyncOp{};
		readyAsyncOp.m_internalAsyncOp = std::move(m_asyncOpBuffers[priority].asyncOps.Front());

		// Remove the found entry
		currentBuffer.asyncOps.PopFront();

		readyAsyncOp.m_internalMemoryForIO = ShareableIOMemory(std::move(bufferForIO), tokenCopy.m_offset);
		readyAsyncOp.m_internalMemoryForDecompression = std::move(bufferForDecompressor);
		if (readyAsyncOp.m_internalMemoryForIO.Data())
		{
			RED_FATAL_ASSERT(!readyAsyncOp.m_internalAsyncOp.m_asyncReadToken.m_buffer);
			readyAsyncOp.m_internalAsyncOp.m_asyncReadToken.m_buffer = readyAsyncOp.m_internalMemoryForIO.Data();
		}
		
		auto& ioContext = readyAsyncOp.m_internalAsyncOp.m_asyncReadToken.m_ioContext;
		if (ioContext)
		{
			// The I/O worker has to start it soon enough, and might as well do it here where it's all unified
			ioContext->SetLoadingState(IOContext::LoadingState::Loading);
			ioContext->NotifyIOStart();
		}

#ifdef RED_PROFILE_FILE_SYSTEM
		{
			const auto& nextAsyncOp = readyAsyncOp.m_internalAsyncOp;
			IIOProfiler::Get()->ProfileAsyncIOReadTakenFromQueue(nextAsyncOp.m_operationId, nextAsyncOp.m_priority, nextAsyncOp.m_asyncReadToken.m_numberOfBytesToRead, nextAsyncOp.m_asyncReadToken.m_debugLogicalFileName, false );
		}
#endif

		if (bufferForIOHasCachedIOResult)
		{
			fillBuffers.cachedAsyncOps.PushBack(std::move(readyAsyncOp));
		}
		else
		{
			fillBuffers.readyAsyncOps.PushBack(std::move(readyAsyncOp));
		}
	}

	if (!bufferForIOHasCachedIOResult)
	{
		currentBuffer.asyncOpsSentThisTurn += 1;
	}

	return true;
}

void AsyncIO::FillAsyncOpsCallback(void* context, Uint32 maxReadyAsyncOps, prv::FillBuffers& fillBuffers)
{
	auto thisPtr = static_cast<AsyncIO*>(context);
	RED_FATAL_ASSERT(thisPtr);
	thisPtr->FillAsyncOps(maxReadyAsyncOps, fillBuffers);
}

void AsyncIO::FillAudioAsyncOpsCallback(void* context, prv::FillBuffers& fillBuffers, Uint32 maxReadyAsyncOps )
{
	auto thisPtr = static_cast<AsyncIO*>(context);
	RED_FATAL_ASSERT(thisPtr);
	thisPtr->FillAudioAsyncOps(fillBuffers, maxReadyAsyncOps);
}

// Find next I/O queue to service using a weighted round robin scheduler.
// It's incremental round robin, so we store and update m_currentActivePriority.
// And it's incremental because the output is throttled by how many asyncOps the I/O worker will pop,
// and we don't want to bias the high priority queue (or some other queue) by resetting the round robin phase prematurely
//
// NOTE: possibly should also use the required deserialization memory, but that'd rather overcomplicate things where the current goal
// is to avoid starvation.
Bool AsyncIO::UpdateActivePriority_NoLock()
{
	// Only consider queues with items to process
	// Created in priority processing order
	red::StaticArray<EAsyncPriority, eAsyncPriority_COUNT> activeList;
	for (Int32 i = (Int32)eAsyncPriority_COUNT - 1; i >= 0; --i)
	{
		auto& buffer = m_asyncOpBuffers[i];
		if (!buffer.asyncOps.Empty())
		{
			activeList.PushBack((EAsyncPriority)i);
		}
	}

	if (activeList.Empty())
	{
		return false;
	}

	// See if we're done processing the current queue.
	Bool serviceNextPriority = false;
	{
		const auto& buffer = m_asyncOpBuffers[m_currentActivePriority];
		if (!activeList.Exist((EAsyncPriority)m_currentActivePriority))
		{
			serviceNextPriority = true;
		}
		else if (buffer.asyncOpsSentThisTurn == buffer.queueWeight)
		{
			serviceNextPriority = true;
		}
	}

	if (serviceNextPriority)
	{
		// Simplified fast path. Even if background priority - better to send something than nothing.
		if (activeList.Size() == 1)
		{
			m_currentActivePriority = activeList[0];
			auto& buffer = m_asyncOpBuffers[m_currentActivePriority];
			buffer.asyncOpsSentThisTurn = 0;
		}
		else
		{	
			// Find the next non-empty queue in descending RR order. It could always even be the same queue again.
			for (Uint32 i = 0, len = eAsyncPriority_COUNT; i < len; ++i)
			{
				const Uint32 testPri = (m_currentActivePriority + eAsyncPriority_COUNT - 1 - i) % eAsyncPriority_COUNT;
				if (activeList.Exist((EAsyncPriority)testPri))
				{
					m_currentActivePriority = testPri;
					auto& buffer = m_asyncOpBuffers[m_currentActivePriority];
					buffer.asyncOpsSentThisTurn = 0;
					break;
				}
			}
		}
	}

	return true;
}

void AsyncIO::GetStats( AsyncIOStats& outStats ) const
{
	io::AsyncIOStats stats;

#if defined( RED_USE_IOWORKER_ORBIS )
	auto worker = static_cast< prv::IOWorkerOrbis* >( m_worker.Get() );
#elif defined( RED_USE_IOWORKER_WIN32 )
	auto worker = static_cast<prv::IOWorkerWin32*>(m_worker.Get());
#elif defined( RED_USE_IOWORKER_GENERIC )
	auto worker = static_cast<prv::IOWorkerGeneric*>(m_worker.Get());
#endif

	// throughput but speed and output vs request input would be useful too...
	stats.bytesReadTotal = worker->GetStatsBytesReadTotal();
	
	const auto statsNumAsyncOpsInFlight = worker->GetStatsNumAsyncOpsInFlight();
	//red::Memcpy(&stats.numAsyncOpsQueued, &m_numAsyncOpsQueuedForStats, sizeof(stats.numAsyncOpsQueued));
	red::Memcpy(&stats.numAsyncOpsInFlight, &statsNumAsyncOpsInFlight.value, sizeof(stats.numAsyncOpsInFlight));

	// FIXME: I don't like this, but I'd rather have a more accurate queue count that doesn't include cancels. Maybe can use the IOQueue profiler somehow.
#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
	{
		RED_SCOPE_LOCK(m_bufferLock);

		for (Uint32 pri = 0; pri < eAsyncPriority_COUNT; ++pri)
		{
			stats.numAsyncOpsQueued[pri] = 0;
			const auto& asyncOps = m_asyncOpBuffers[pri].asyncOps;
			for (Uint32 i = 0, len = asyncOps.Size(); i < len; ++i)
			{
				const prv::AsyncOp& asyncOp = asyncOps[i];
				if (!asyncOp.m_flags.m_cachedIsCancelled)
				{
					stats.numAsyncOpsQueued[pri] += 1;
				}
			}
		}
	}
#endif

	outStats = stats;

#if defined( USE_PROFILER )
	{
		RED_SCOPE_LOCK( m_bufferLock );
		outStats.gameQueueString = m_dbgGameQueueString;
		outStats.readyOpsString = m_dbgReadyOpsString;
	}
    
#endif
}

void AsyncIO::GetStatsAvailableInFinal( AsyncIOStatsAvailableInFinal& outStats ) const
{
	outStats.numTotalAsyncOpsQueued = 0u;

	RED_SCOPE_LOCK(m_bufferLock);
	for (Uint32 pri = 0; pri < eAsyncPriority_COUNT; ++pri)
	{
		const auto& asyncOps = m_asyncOpBuffers[pri].asyncOps;
		outStats.numTotalAsyncOpsQueued += asyncOps.Size(); // here we ignore if an op is canceled or not, but since this runs in final i want it to be FAST
	}
}

namespace helper
{
#ifndef NO_EDITOR
	// Copy of systemWin32.cpp, but at a higher inaccessible level
	static Uint64 GetFileSize( const char* fileName )
	{
		UniChar wideName[ MAX_PATH + 1 ];
		RED_VERIFY( red::EngineStringToFileSystemString( fileName, wideName, MAX_PATH ) );

		WIN32_FILE_ATTRIBUTE_DATA fileAttributeData = { 0 };

		Uint64 size = 0;
		if ( ::GetFileAttributesExW( wideName, ::GetFileExInfoStandard, &fileAttributeData ) != 0 )
		{
			size = static_cast<Uint64>( fileAttributeData.nFileSizeHigh ) << 32 | fileAttributeData.nFileSizeLow;
		}
		return size;
	}
#endif
}

Uint64 AsyncIO::GetFileSize( TFileHandle fh )
{
	// Addref so can't lose AsyncFile part way through function and crash myteriously
	m_asyncFileHandleCache->AddRef( fh );
	
	Uint64 size = 0;

#ifndef NO_EDITOR
	// Assuming deferred open.... could complicate this and check if already open.
	const char* fileName = m_asyncFileHandleCache->GetFileName( fh );
	if ( fileName )
	{
		size = helper::GetFileSize( fileName );
	}
#else
	AsyncFile* asyncFile = m_asyncFileHandleCache->GetAsyncFile( fh );
	if ( asyncFile )
	{
		size = asyncFile->GetFileSize();
	}
#endif

	m_asyncFileHandleCache->Release( fh );

	return size;
}

const char* AsyncIO::GetFileName( TFileHandle fh ) const
{
	return m_asyncFileHandleCache->GetFileName( fh );
}

Uint8 AsyncIO::GetAsyncFlags( TFileHandle fh ) const
{
	return m_asyncFileHandleCache->GetAsyncFlags( fh );
}

void AsyncIO::AddRefFile( TFileHandle fh )
{
	m_asyncFileHandleCache->AddRef( fh );
}

void AsyncIO::ProfileStartTrace()
{
#ifdef USE_PROFILER
# ifdef RED_USE_IOWORKER_ORBIS
	if ( m_profilerOrbis )
	{
		m_profilerOrbis->StartTrace();
	}
# endif
#endif
}

void AsyncIO::ProfileStopTrace()
{
#ifdef USE_PROFILER
# ifdef RED_USE_IOWORKER_ORBIS
	if ( m_profilerOrbis )
	{
		RED_FATAL_ASSERT( m_traceWriterOrbis );
		m_traceWriterOrbis->StopTrace( *m_profilerOrbis );
	}
# endif
#endif
}

void AsyncIO::ProfileDecompressStart( const char* resourcePath )
{
#ifdef USE_PROFILER
# ifdef RED_USE_IOWORKER_ORBIS
	if ( m_profilerOrbis )
	{
		m_profilerOrbis->ProfileDecompressStart( resourcePath );
	}
# endif
#endif
}

void AsyncIO::ProfileDecompressEnd( const char* resourcePath )
{
#ifdef USE_PROFILER
# ifdef RED_USE_IOWORKER_ORBIS
	if ( m_profilerOrbis )
	{
		m_profilerOrbis->ProfileDecompressEnd( resourcePath );
	}
# endif
#endif
}

void AsyncIO::ProfileIOWorkerBegin( Uint32 priority )
{
#ifdef USE_PROFILER
# ifdef RED_USE_IOWORKER_ORBIS
	if ( m_profilerOrbis )
	{
		m_profilerOrbis->ProfileIOWorkerBegin( priority );
	}
# endif
#endif
}

void AsyncIO::ProfileIOWorkerEnd( Uint32 priority )
{
#ifdef USE_PROFILER
# ifdef RED_USE_IOWORKER_ORBIS
	if ( m_profilerOrbis )
	{
		m_profilerOrbis->ProfileIOWorkerEnd( priority );
	}
# endif
#endif
}

void AsyncIO::ProfileMarkEvent( const char* eventName )
{
#ifdef USE_PROFILER
# ifdef RED_USE_IOWORKER_ORBIS
	if ( m_profilerOrbis )
	{
		m_profilerOrbis->ProfileMarkEvent( eventName );
	}
# endif
#endif
}

void AsyncIO::KickWorkerThread()
{
	m_workerWakeEvent->SetEvent();
}

void AsyncIO::HACK_ThrottleStreamingForCutsceneAudio(Bool throttle)
{
	helper::CastWorker(m_worker.Get())->HACK_ThrottleStreamingForCutsceneAudio(throttle);
    m_hackForThrottleStreamingForAudioEnabled = throttle;
}

Bool AsyncIO::HACK_IsThrottleStreamingForCutsceneAudio() const
{
	return helper::CastWorker(m_worker.Get())->HACK_IsThrottleStreamingForCutsceneAudio();
}

void AsyncIO::HACK_AudioThreadStreamingForCutsceneAudio(Bool throttle)
{
	helper::CastWorker(m_worker.Get())->HACK_AudioThreadStreamingForCutsceneAudio(throttle);
    m_hackForAudioThreadStreamingForAudioEnabled = throttle;
}

Bool AsyncIO::HACK_IsAudioThreadStreamingForCutsceneAudio() const
{
	return helper::CastWorker(m_worker.Get())->HACK_IsAudioThreadStreamingForCutsceneAudio();
}

}
#ifdef RED_PLATFORM_ORBIS
//#pragma clang optimize on
#endif

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "ioQueueProfiler.h"

#ifdef RED_PROFILE_FILE_SYSTEM
#include "../../../common/redIO/include/redIOAsyncIO.h"
#include "../../../common/redIO/include/redIOProfilerInterface.h"
#include "../../../common/redIO/src/redIONullProfiler.h"

void IOQueueProfiler::Reset()
{
	RED_SCOPE_LOCK( m_hashMapLock );
	m_numAsyncOpsQueuedByTypeForStats.Clear();
	m_timer.Reset();
}

String FileNameToType( const char* filename )
{
	String fileNameAsString = String( filename );
	return fileNameAsString.Contains( "." ) ? fileNameAsString.StringAfterFromRight( "." ) : fileNameAsString;
}

void IncrementMetrics( io::AsyncOpStats* stats, String& debugType, const red::Uint32 size, const red::Uint32 requestID, Double timeMs )
{
	RED_SCOPE_LOCK( *stats->lock );
	if( stats->typeName.Empty() )
	{
		stats->typeName = debugType;
	}
	stats->opCount += 1;
	stats->opPending.Set( requestID, { timeMs } );
}

void DecrementMetrics( io::AsyncOpStats* stats, String& debugType, const red::Uint32 size, const red::Uint32 requestID, Double timeMs, Bool isCanceled )
{
	RED_SCOPE_LOCK( *stats->lock );
	auto request = stats->opPending.Find( requestID );
	if ( request != stats->opPending.End() )
	{
		stats->opCount -= 1;
		Double waitTime = timeMs - request.Value().createdAt;

		if ( isCanceled )
		{
			stats->opCanceledCountTotal += 1;
			stats->opCanceledTimeTotal += waitTime;
		}
		else
		{
			stats->opFinishedCountTotal += 1;
			stats->bytesReadTotal += size;
			stats->opFinishedTimeTotal += waitTime;
		}

		stats->opPending.Remove( request );
	}
}

Double IOQueueProfiler::GetCurrentTimeMs()
{
	return m_timer.GetSeconds() * 1000;
}

void IOQueueProfiler::ProfileAsyncIOReadScheduled( const red::Uint32 fileHandle, const red::Uint32 requestID, const Uint32 priority, const red::Uint64 offset, const red::Uint32 size, const char* filename, const char *logicalFileName )
{
	RED_ASSERT( filename );
	RED_ASSERT( logicalFileName );

	{
		RED_SCOPE_LOCK( m_lock );
		RequestInfo info;
		info.offset = offset;
		info.size = size;
		info.fileHandle = fileHandle;
		m_pendingRequests.Insert( requestID, std::move( info ) );
	}

	Double timeMs = GetCurrentTimeMs();
	String debugType = FileNameToType( logicalFileName );
	io::AsyncOpStats* stats;
	{
		RED_SCOPE_SHARED_LOCK( m_hashMapLock );
		stats = m_numAsyncOpsQueuedByTypeForStats.FindPtr( debugType );

		if ( stats )
		{
			IncrementMetrics( stats, debugType, size, requestID, timeMs );
			return;
		}
	}

	RED_SCOPE_LOCK( m_hashMapLock );
	stats = &m_numAsyncOpsQueuedByTypeForStats[debugType];
	IncrementMetrics( stats, debugType, size, requestID, timeMs );
}

void IOQueueProfiler::ProfileAsyncIOReadTakenFromQueue( const red::Uint32 requestID, const Uint32 priority, const Uint32 size, const char* filename, const Bool isCanceled )
{
	RED_ASSERT( filename );

	{
		RED_SCOPE_LOCK( m_lock );
		if( isCanceled )
		{
			m_pendingRequests.Remove( requestID );
		}
		else
		{
			// Sadly we cannot just blindly trust that the request will be in queue.
			// We may have scheduled it with Null profiler or any different profiler enabled
			// so it may be missing from that instance.
			if( RequestInfo* request = m_pendingRequests.FindPtr( requestID ) )
			{
				request->inQueue = false;
			}
		}
	}

	Double timeMs = GetCurrentTimeMs();
	String debugType = FileNameToType( filename );
	{
		RED_SCOPE_SHARED_LOCK( m_hashMapLock );
		io::AsyncOpStats* stats = m_numAsyncOpsQueuedByTypeForStats.FindPtr( debugType );

		if ( stats )
		{
			DecrementMetrics( stats, debugType, size, requestID, timeMs, isCanceled );
		}
	}
}

void IOQueueProfiler::ProfileAsyncIOReadCanceled( const red::Uint32 requestID, const Uint32 priority, const Uint32 size, const char* filename )
{
	ProfileAsyncIOReadTakenFromQueue( requestID, priority, size, filename, true );
}

void IOQueueProfiler::ProfileAsyncIOReadStart( const red::Uint32 requestID )
{
#ifdef RED_PLATFORM_WINPC
	// This only works for GenericIOWorker which reads and writes file sequentially.
	// In other workers order of execution of ReadStart/ReadEnd is not promised.
	m_lastRequestID = requestID;
	m_lastRequestAt = GetCurrentTimeMs();
#endif

	{
		RED_SCOPE_LOCK( m_lock );
		if( const RequestInfo* info = m_pendingRequests.FindPtr( requestID ) )
		{
			Uint64& fileOffset = m_fileOffsets[ info->fileHandle ];

			Uint64 seekSize = 0;
			if( info->offset > fileOffset )
			{
				seekSize = info->offset - fileOffset;
			}
			else
			{
				seekSize = fileOffset - info->offset;
			}

			m_totalSeekBytes.ExchangeAdd( seekSize );
			fileOffset = info->offset;
		}
	}
}

void IOQueueProfiler::ProfileAsyncIOReadEnd( const red::Uint32 requestID )
{
#ifdef RED_PLATFORM_WINPC
	// This only works for GenericIOWorker which reads and writes file sequentially.
	// In other workers order of execution of ReadStart/ReadEnd is not promised.
	RED_ASSERT( requestID == m_lastRequestID );
	red::Int32 sleepForMs = m_ioMaxSpeedMs - (Int32)(GetCurrentTimeMs() - m_lastRequestAt);
	if( sleepForMs > 0 )
	{
		red::SleepOnCurrentThread( sleepForMs );
	}
#endif

	{
		RED_SCOPE_LOCK( m_lock );
		if( const RequestInfo* info = m_pendingRequests.FindPtr( requestID ) )
		{
			Uint64& fileOffset = m_fileOffsets[ info->fileHandle ];
			fileOffset += info->size;

			m_pendingRequests.Remove( requestID );
		}
	}
}

void IOQueueProfiler::ProfileAsyncIOReadFailed( const red::Uint32 requestID )
{
	{
		RED_SCOPE_LOCK( m_lock );
		m_pendingRequests.Remove( requestID );
	}
}

void IOQueueProfiler::GetStatsPerType( red::SortedArray< io::AsyncOpStats, io::SortByBytesTotalRead >& outStatsPerType, Uint32& opsCount )
{
	RED_SCOPE_SHARED_LOCK( m_hashMapLock );
	for( red::HashMap< String, io::AsyncOpStats >::const_iterator it = m_numAsyncOpsQueuedByTypeForStats.Begin(); it != m_numAsyncOpsQueuedByTypeForStats.End(); ++it )
	{
		RED_SCOPE_SHARED_LOCK( *it.Value().lock );
		outStatsPerType.Insert( it.Value() );
		opsCount += it.Value().opCount;
	}
}

red::Uint32 IOQueueProfiler::ProfileAllocRequestId()
{
	static red::Atomic<Uint32> requestIDCounter{ 0 };
	return requestIDCounter.Increment();
}

void IOQueueProfiler::ProfileSetThreadName( const red::AnsiChar* name )
{
	
}

void IOQueueProfiler::ProfileFinishLoadingPhase( const red::AnsiChar* name )
{
	
}

void IOQueueProfiler::ProfileEndLoading()
{
	
}

void IOQueueProfiler::ProfileAsyncIOOpenFileStart( const char* filePath )
{
	
}

void IOQueueProfiler::ProfileAsyncIOOpenFileEnd( const red::Uint32 allocatedHandle )
{
	RED_SCOPE_LOCK(m_lock);

	m_fileOffsets[ allocatedHandle ] = 0;
}

void IOQueueProfiler::ProfileAsyncIOCloseFileStart( const red::Uint32 fileHandle )
{
	RED_SCOPE_LOCK(m_lock);

	m_fileOffsets[ fileHandle ] = 0;
}

void IOQueueProfiler::ProfileAsyncIOCloseFileEnd()
{
	
}

void IOQueueProfiler::ProfileSyncIOOpenFileStart( const char* filePath )
{
	
}

void IOQueueProfiler::ProfileSyncIOOpenFileEnd( const red::Uint32 allocatedHandle )
{
	
}

void IOQueueProfiler::ProfileSyncIOCloseFileStart( const red::Uint32 fileHandle )
{
	
}

void IOQueueProfiler::ProfileSyncIOCloseFileEnd()
{
	
}

void IOQueueProfiler::ProfileSyncIOReadStart( const red::Uint32 fileHandle, const red::Uint32 size )
{
	
}

void IOQueueProfiler::ProfileSyncIOReadEnd()
{
	
}

void IOQueueProfiler::ProfileSyncIOSeekStart( const red::Uint32 fileHandle, const red::Uint64 offset )
{
	
}

void IOQueueProfiler::ProfileSyncIOSeekEnd()
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadResourceStart( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadResourceEnd( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileSyncLoadDataStart( const red::Uint32 size )
{
	
}

void IOQueueProfiler::ProfileDiskFileSyncLoadDataEnd()
{
	
}

void IOQueueProfiler::ProfileDiskFileSyncDecompressStart( const red::Uint32 size, const red::Uint8 type )
{
	
}

void IOQueueProfiler::ProfileDiskFileSyncDecompressEnd()
{
	
}

void IOQueueProfiler::ProfileDiskFileAsyncDecompressStart( const red::Uint32 size, const red::Uint8 type )
{
	
}

void IOQueueProfiler::ProfileDiskFileAsyncDecompressEnd()
{
	
}

void IOQueueProfiler::ProfileDiskFileDeserializeStart( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileDeserializeEnd( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadTablesStart( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadTablesEnd( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileMapTablesStart( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileMapTablesEnd( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileCreateExportsStart( const red::UniChar* filePath, const red::Uint32 count )
{
	
}

void IOQueueProfiler::ProfileDiskFileCreateExportsEnd( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadExportsStart( const red::UniChar* filePath, const red::Uint32 count )
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadExportsEnd( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadImportsStart( const red::UniChar* filePath, const red::Uint32 count )
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadImportsEnd( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFilePostLoadStart( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFilePostLoadEnd( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadInplaceStart( const red::UniChar* filePath, const red::Uint32 count )
{
	
}

void IOQueueProfiler::ProfileDiskFileLoadInplaceEnd( const red::UniChar* filePath )
{
	
}

void IOQueueProfiler::ProfileDiskFileDeferredDataLoadSyncStart( const red::Uint32 size )
{
	
}

void IOQueueProfiler::ProfileDiskFileDeferredDataLoadSyncEnd()
{
	
}

void IOQueueProfiler::ProfileVarAllocMemoryBlock( const red::Uint32 size )
{
	
}

void IOQueueProfiler::ProfileVarFreeMemoryBlock( const red::Uint32 size )
{
	
}

void IOQueueProfiler::ProfileVarAllocDecompressionTask()
{
	
}

void IOQueueProfiler::ProfileVarFreeDecompressionTask()
{
	
}

void IOQueueProfiler::ProfileVarAddPendingFiles( const red::Uint32 count )
{
	
}

void IOQueueProfiler::ProfileVarRemovePendingFiles( const red::Uint32 count )
{
	
}

void IOQueueProfiler::ProfileSignalShowLoadingScreen()
{
	
}

void IOQueueProfiler::ProfileSignalHideLoadingScreen()
{
	
}

void IOQueueProfiler::ProfileSignalShowBlackScreen()
{
	
}

void IOQueueProfiler::ProfileSignalHideBlackScreen()
{
	
}

void IOQueueProfiler::ProfileSignalVideoPlay()
{
	
}

void IOQueueProfiler::ProfileSignalVideoStop()
{
	
}

void IOQueueProfiler::ProfileSignalVideoReadRequested()
{
	
}

void IOQueueProfiler::ProfileSignalVideoReadCompleted()
{
	
}

#endif // RED_PROFILE_FILE_SYSTEM
/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// profile the file system
#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
	#define RED_PROFILE_FILE_SYSTEM
#endif

#ifdef RED_PROFILE_FILE_SYSTEM

#include "redCoreApi.h"
#include "../../redIO/include/redIOProfilerInterface.h"
#include "../../../common/redIO/include/redIOStats.h"

class REDCORE_API IOQueueProfiler : public IIOProfiler
{
public:
	void Reset();

	void ProfileAsyncIOReadScheduled( const red::Uint32 fileHandle, const red::Uint32 requestID, const Uint32 priority, const red::Uint64 offset, const red::Uint32 size, const char* filename, const char *logicalFileName );

	void ProfileAsyncIOReadTakenFromQueue( const red::Uint32 requestID, const Uint32 priority, const red::Uint32 size, const char* filename, const Bool isCanceled );

	void ProfileAsyncIOReadCanceled( const red::Uint32 requestID, const Uint32 priority, const red::Uint32 size, const char* filename );

	void ProfileAsyncIOReadStart( const red::Uint32 requestID ) override;

	void ProfileAsyncIOReadEnd( const red::Uint32 requestID ) override;
	void ProfileAsyncIOReadFailed( const red::Uint32 requestID ) override;
	void ProfileAsyncIOSeekStart( const red::Uint32 requestID, const red::Uint64 offset ) override {};
	void ProfileAsyncIOSeekEnd( const red::Uint32 requestID, const Bool result ) override {};

#ifdef RED_PLATFORM_WINPC
	void SetIoMaxSpeedMs( Uint32 ioMaxSpeedMs = 0.0f ) { m_ioMaxSpeedMs = ioMaxSpeedMs; }
	Uint32 GetIoMaxSpeedMs() const { return m_ioMaxSpeedMs; }
#else
	// Unsupported
	void SetIoMaxSpeedMs( Uint32 ioMaxSpeedMs = 0.0f ) { }
	Uint32 GetIoMaxSpeedMs() const { return 0; }
#endif

	void GetStatsPerType( red::SortedArray< io::AsyncOpStats, io::SortByBytesTotalRead >& outStatsPerType, Uint32& opsCount );

	Uint64 GetTotalSeekBytes() const { return m_totalSeekBytes.GetValue(); }

	red::Uint32 ProfileAllocRequestId() override;

	void ProfileSetThreadName( const red::AnsiChar* name ) override;
	void ProfileFinishLoadingPhase( const red::AnsiChar* name ) override;
	void ProfileEndLoading() override;
	void ProfileAsyncIOOpenFileStart( const char* filePath ) override;
	void ProfileAsyncIOOpenFileEnd( const red::Uint32 allocatedHandle ) override;
	void ProfileAsyncIOCloseFileStart( const red::Uint32 fileHandle ) override;
	void ProfileAsyncIOCloseFileEnd() override;
	void ProfileSyncIOOpenFileStart( const char* filePath ) override;
	void ProfileSyncIOOpenFileEnd( const red::Uint32 allocatedHandle ) override;
	void ProfileSyncIOCloseFileStart( const red::Uint32 fileHandle ) override;
	void ProfileSyncIOCloseFileEnd() override;
	void ProfileSyncIOReadStart( const red::Uint32 fileHandle, const red::Uint32 size ) override;
	void ProfileSyncIOReadEnd() override;
	void ProfileSyncIOSeekStart( const red::Uint32 fileHandle, const red::Uint64 offset ) override;
	void ProfileSyncIOSeekEnd() override;
	void ProfileDiskFileLoadResourceStart( const red::UniChar* filePath ) override;
	void ProfileDiskFileLoadResourceEnd( const red::UniChar* filePath ) override;
	void ProfileDiskFileSyncLoadDataStart( const red::Uint32 size ) override;
	void ProfileDiskFileSyncLoadDataEnd() override;
	void ProfileDiskFileSyncDecompressStart( const red::Uint32 size, const red::Uint8 type ) override;
	void ProfileDiskFileSyncDecompressEnd() override;
	void ProfileDiskFileAsyncDecompressStart( const red::Uint32 size, const red::Uint8 type ) override;
	void ProfileDiskFileAsyncDecompressEnd() override;
	void ProfileDiskFileDeserializeStart( const red::UniChar* filePath ) override;
	void ProfileDiskFileDeserializeEnd( const red::UniChar* filePath ) override;
	void ProfileDiskFileLoadTablesStart( const red::UniChar* filePath ) override;
	void ProfileDiskFileLoadTablesEnd( const red::UniChar* filePath ) override;
	void ProfileDiskFileMapTablesStart( const red::UniChar* filePath ) override;
	void ProfileDiskFileMapTablesEnd( const red::UniChar* filePath ) override;
	void ProfileDiskFileCreateExportsStart( const red::UniChar* filePath, const red::Uint32 count ) override;
	void ProfileDiskFileCreateExportsEnd( const red::UniChar* filePath ) override;
	void ProfileDiskFileLoadExportsStart( const red::UniChar* filePath, const red::Uint32 count ) override;
	void ProfileDiskFileLoadExportsEnd( const red::UniChar* filePath ) override;
	void ProfileDiskFileLoadImportsStart( const red::UniChar* filePath, const red::Uint32 count ) override;
	void ProfileDiskFileLoadImportsEnd( const red::UniChar* filePath ) override;
	void ProfileDiskFilePostLoadStart( const red::UniChar* filePath ) override;
	void ProfileDiskFilePostLoadEnd( const red::UniChar* filePath ) override;
	void ProfileDiskFileLoadInplaceStart( const red::UniChar* filePath, const red::Uint32 count ) override;
	void ProfileDiskFileLoadInplaceEnd( const red::UniChar* filePath ) override;
	void ProfileDiskFileDeferredDataLoadSyncStart( const red::Uint32 size ) override;
	void ProfileDiskFileDeferredDataLoadSyncEnd() override;
	void ProfileVarAllocMemoryBlock( const red::Uint32 size ) override;
	void ProfileVarFreeMemoryBlock( const red::Uint32 size ) override;
	void ProfileVarAllocDecompressionTask() override;
	void ProfileVarFreeDecompressionTask() override;
	void ProfileVarAddPendingFiles( const red::Uint32 count ) override;
	void ProfileVarRemovePendingFiles( const red::Uint32 count ) override;
	void ProfileSignalShowLoadingScreen() override;
	void ProfileSignalHideLoadingScreen() override;
	void ProfileSignalShowBlackScreen() override;
	void ProfileSignalHideBlackScreen() override;
	void ProfileSignalVideoPlay() override;
	void ProfileSignalVideoStop() override;
	void ProfileSignalVideoReadRequested() override;
	void ProfileSignalVideoReadCompleted() override;

private:
	Double GetCurrentTimeMs();

private:
	red::RWSpinLock m_hashMapLock;
	red::HashMap< String, io::AsyncOpStats > m_numAsyncOpsQueuedByTypeForStats = {red::PoolDebug()};

#ifdef RED_PLATFORM_WINPC
	Uint32 m_lastRequestID;
	Double m_lastRequestAt;
	Uint32 m_ioMaxSpeedMs;
#endif
	red::Timer m_timer;

	red::RWSpinLock m_lock;
	struct RequestInfo
	{
		Uint32 fileHandle;
		Uint64 offset;
		Uint32 size;
		Bool inQueue = true;
	};
	red::HashMap<Uint32, RequestInfo> m_pendingRequests{ red::PoolDebug() };

	red::HashMap<Uint32, Uint64> m_fileOffsets{ red::PoolDebug() };

	red::Atomic<Uint64> m_totalSeekBytes{ 0 };
};

#endif // RED_PROFILE_FILE_SYSTEM

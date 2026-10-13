/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "redIOApi.h"

// profile the file system
#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
#define RED_PROFILE_FILE_SYSTEM
#endif

#ifdef RED_PROFILE_FILE_SYSTEM

class REDIO_API IIOProfiler
{
private:
	static IIOProfiler* GIOProfiler;

public:
	RED_FORCE_INLINE static IIOProfiler* Get()
	{
		return GIOProfiler;
	}

	static void Set( IIOProfiler* newProfiler );

	virtual ~IIOProfiler() { }

	virtual red::Uint32 ProfileAllocRequestId() = 0;
	virtual void ProfileSetThreadName( const red::AnsiChar* name ) = 0;

	virtual void ProfileFinishLoadingPhase( const red::AnsiChar* name ) = 0;
	virtual void ProfileEndLoading() = 0;

	virtual void ProfileAsyncIOOpenFileStart( const char* filePath ) = 0;
	virtual void ProfileAsyncIOOpenFileEnd( const red::Uint32 allocatedHandle ) = 0;
	virtual void ProfileAsyncIOCloseFileStart( const red::Uint32 fileHandle ) = 0;
	virtual void ProfileAsyncIOCloseFileEnd() = 0;
	virtual void ProfileAsyncIOReadScheduled( const red::Uint32 fileHandle, const red::Uint32 requestID, const Uint32 priority, const red::Uint64 offset, const red::Uint32 size, const char* filename, const char *logicalFileName ) = 0;
	virtual void ProfileAsyncIOReadTakenFromQueue( const red::Uint32 requestID, const Uint32 priority, const red::Uint32 size, const char* filename, const Bool isCanceled ) = 0;
	virtual void ProfileAsyncIOReadCanceled( const red::Uint32 requestID, const red::Uint32 priority, const red::Uint32 size, const char* filename ) = 0;
	virtual void ProfileAsyncIOReadStart( const red::Uint32 requestID ) = 0;
	virtual void ProfileAsyncIOReadEnd( const red::Uint32 requestID ) = 0;
	virtual void ProfileAsyncIOReadFailed( const red::Uint32 requestID ) = 0;
	virtual void ProfileAsyncIOSeekStart( const red::Uint32 requestID, const red::Uint64 offset ) = 0;
	virtual void ProfileAsyncIOSeekEnd( const red::Uint32 requestID, const Bool result ) = 0;

	virtual void ProfileSyncIOOpenFileStart( const char* filePath ) = 0;
	virtual void ProfileSyncIOOpenFileEnd( const red::Uint32 allocatedHandle ) = 0;
	virtual void ProfileSyncIOCloseFileStart( const red::Uint32 fileHandle ) = 0;
	virtual void ProfileSyncIOCloseFileEnd() = 0;
	virtual void ProfileSyncIOReadStart( const red::Uint32 fileHandle, const red::Uint32 size ) = 0;
	virtual void ProfileSyncIOReadEnd() = 0;
	virtual void ProfileSyncIOSeekStart( const red::Uint32 fileHandle, const red::Uint64 offset ) = 0;
	virtual void ProfileSyncIOSeekEnd() = 0;

	virtual void ProfileDiskFileLoadResourceStart( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileLoadResourceEnd( const red::UniChar* filePath ) = 0;

	virtual void ProfileDiskFileSyncLoadDataStart( const red::Uint32 size ) = 0;
	virtual void ProfileDiskFileSyncLoadDataEnd() = 0;
	virtual void ProfileDiskFileSyncDecompressStart( const red::Uint32 size, const red::Uint8 type ) = 0;
	virtual void ProfileDiskFileSyncDecompressEnd() = 0;
	virtual void ProfileDiskFileAsyncDecompressStart( const red::Uint32 size, const red::Uint8 type ) = 0;
	virtual void ProfileDiskFileAsyncDecompressEnd() = 0;

	virtual void ProfileDiskFileDeserializeStart( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileDeserializeEnd( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileLoadTablesStart( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileLoadTablesEnd( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileMapTablesStart( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileMapTablesEnd( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileCreateExportsStart( const red::UniChar* filePath, const red::Uint32 count ) = 0;
	virtual void ProfileDiskFileCreateExportsEnd( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileLoadExportsStart( const red::UniChar* filePath, const red::Uint32 count ) = 0;
	virtual void ProfileDiskFileLoadExportsEnd( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileLoadImportsStart( const red::UniChar* filePath, const red::Uint32 count ) = 0;
	virtual void ProfileDiskFileLoadImportsEnd( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFilePostLoadStart( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFilePostLoadEnd( const red::UniChar* filePath ) = 0;
	virtual void ProfileDiskFileLoadInplaceStart( const red::UniChar* filePath, const red::Uint32 count ) = 0;
	virtual void ProfileDiskFileLoadInplaceEnd( const red::UniChar* filePath ) = 0;

	virtual void ProfileDiskFileDeferredDataLoadSyncStart( const red::Uint32 size ) = 0;
	virtual void ProfileDiskFileDeferredDataLoadSyncEnd() = 0;

	virtual void ProfileVarAllocMemoryBlock( const red::Uint32 size ) = 0;
	virtual void ProfileVarFreeMemoryBlock( const red::Uint32 size ) = 0;
	virtual void ProfileVarAllocDecompressionTask() = 0;
	virtual void ProfileVarFreeDecompressionTask() = 0;
	virtual void ProfileVarAddPendingFiles( const red::Uint32 count ) = 0;
	virtual void ProfileVarRemovePendingFiles( const red::Uint32 count ) = 0;

	virtual void ProfileSignalShowLoadingScreen() = 0;
	virtual void ProfileSignalHideLoadingScreen() = 0;
	virtual void ProfileSignalShowBlackScreen() = 0;
	virtual void ProfileSignalHideBlackScreen() = 0;
	virtual void ProfileSignalVideoPlay() = 0;
	virtual void ProfileSignalVideoStop() = 0;
	virtual void ProfileSignalVideoReadRequested() = 0;
	virtual void ProfileSignalVideoReadCompleted() = 0;
};

#endif // RED_PROFILE_FILE_SYSTEM

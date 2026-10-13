/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/redIO/include/redIOProfilerInterface.h"

#ifdef RED_PROFILE_FILE_SYSTEM

class NullIOProfiler : public IIOProfiler
{
public:
	red::Uint32 ProfileAllocRequestId() { return 0; }
	void ProfileSetThreadName( const red::AnsiChar* name ) {}

	void ProfileFinishLoadingPhase( const red::AnsiChar* name ) {}
	void ProfileEndLoading() {}

	void ProfileAsyncIOOpenFileStart( const char* filePath ) {}
	void ProfileAsyncIOOpenFileEnd( const red::Uint32 allocatedHandle ) {}
	void ProfileAsyncIOCloseFileStart( const red::Uint32 fileHandle ) {}
	void ProfileAsyncIOCloseFileEnd() {}
	void ProfileAsyncIOReadScheduled( const red::Uint32 fileHandle, const red::Uint32 requestID, const Uint32 priority, const red::Uint64 offset, const red::Uint32 size, const char* logicalFilename, const char* filename ) {}
	void ProfileAsyncIOReadTakenFromQueue( const red::Uint32 requestID, const Uint32 priority, const red::Uint32 size, const char* filename, const Bool isCanceled ) {}
	void ProfileAsyncIOReadCanceled( const red::Uint32 requestID, const red::Uint32 priority, const red::Uint32 size, const char* filename ) {}
	void ProfileAsyncIOReadStart( const red::Uint32 requestID ) {}
	void ProfileAsyncIOReadEnd( const red::Uint32 requestID ) {}
	void ProfileAsyncIOReadFailed( const red::Uint32 requestID ) {}
	void ProfileAsyncIOSeekStart( const red::Uint32 requestID, const red::Uint64 offset ) {};
	void ProfileAsyncIOSeekEnd( const red::Uint32 requestID, const Bool result ) {};

	void ProfileSyncIOOpenFileStart( const char* filePath ) {}
	void ProfileSyncIOOpenFileEnd( const red::Uint32 allocatedHandle ) {}
	void ProfileSyncIOCloseFileStart( const red::Uint32 fileHandle ) {}
	void ProfileSyncIOCloseFileEnd() {}
	void ProfileSyncIOReadStart( const red::Uint32 fileHandle, const red::Uint32 size ) {}
	void ProfileSyncIOReadEnd() {}
	void ProfileSyncIOSeekStart( const red::Uint32 fileHandle, const red::Uint64 offset ) {}
	void ProfileSyncIOSeekEnd() {}

	void ProfileDiskFileLoadResourceStart( const red::UniChar* filePath ) {}
	void ProfileDiskFileLoadResourceEnd( const red::UniChar* filePath ) {}

	void ProfileDiskFileSyncLoadDataStart( const red::Uint32 size ) {}
	void ProfileDiskFileSyncLoadDataEnd() {}
	void ProfileDiskFileSyncDecompressStart( const red::Uint32 size, const red::Uint8 type ) {}
	void ProfileDiskFileSyncDecompressEnd() {}
	void ProfileDiskFileAsyncDecompressStart( const red::Uint32 size, const red::Uint8 type ) {}
	void ProfileDiskFileAsyncDecompressEnd() {}

	void ProfileDiskFileDeserializeStart( const red::UniChar* filePath ) {}
	void ProfileDiskFileDeserializeEnd( const red::UniChar* filePath ) {}
	void ProfileDiskFileLoadTablesStart( const red::UniChar* filePath ) {}
	void ProfileDiskFileLoadTablesEnd( const red::UniChar* filePath ) {}
	void ProfileDiskFileMapTablesStart( const red::UniChar* filePath ) {}
	void ProfileDiskFileMapTablesEnd( const red::UniChar* filePath ) {}
	void ProfileDiskFileCreateExportsStart( const red::UniChar* filePath, const red::Uint32 count ) {}
	void ProfileDiskFileCreateExportsEnd( const red::UniChar* filePath ) {}
	void ProfileDiskFileLoadExportsStart( const red::UniChar* filePath, const red::Uint32 count ) {}
	void ProfileDiskFileLoadExportsEnd( const red::UniChar* filePath ) {}
	void ProfileDiskFileLoadImportsStart( const red::UniChar* filePath, const red::Uint32 count ) {}
	void ProfileDiskFileLoadImportsEnd( const red::UniChar* filePath ) {}
	void ProfileDiskFilePostLoadStart( const red::UniChar* filePath ) {}
	void ProfileDiskFilePostLoadEnd( const red::UniChar* filePath ) {}
	void ProfileDiskFileLoadInplaceStart( const red::UniChar* filePath, const red::Uint32 count ) {}
	void ProfileDiskFileLoadInplaceEnd( const red::UniChar* filePath ) {}

	void ProfileDiskFileDeferredDataLoadSyncStart( const red::Uint32 size ) {}
	void ProfileDiskFileDeferredDataLoadSyncEnd() {}

	virtual void ProfileVarAllocMemoryBlock( const red::Uint32 size ) {}
	virtual void ProfileVarFreeMemoryBlock( const red::Uint32 size ) {}
	virtual void ProfileVarAllocDecompressionTask() {}
	virtual void ProfileVarFreeDecompressionTask() {}
	virtual void ProfileVarAddPendingFiles( const red::Uint32 count ) {}
	virtual void ProfileVarRemovePendingFiles( const red::Uint32 count ) {}

	virtual void ProfileSignalShowLoadingScreen() {}
	virtual void ProfileSignalHideLoadingScreen() {}
	virtual void ProfileSignalShowBlackScreen() {}
	virtual void ProfileSignalHideBlackScreen() {}
	virtual void ProfileSignalVideoPlay() {}
	virtual void ProfileSignalVideoStop() {}
	virtual void ProfileSignalVideoReadRequested() {}
	virtual void ProfileSignalVideoReadCompleted() {}
};

#endif // RED_PROFILE_FILE_SYSTEM
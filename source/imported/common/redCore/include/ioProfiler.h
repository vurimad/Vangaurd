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
#include "absolutePath.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redIO/include/redIOProfilerInterface.h"
#include "profilerBlockFileWriter.h"


/// Profiler for file system
class REDCORE_API CIOProfiler /*: public IIOProfiler*/
{
public:
	CIOProfiler();
	~CIOProfiler();

	// counter types
	enum ECounterType
	{
		eCounterType_MemoryBlocks = 1,
		eCounterType_DecompressionTasks = 3,
		eCounterType_PendingFiles = 4,
		eCounterType_CachedDataBlocks = 5,
		eCounterType_CachedDataSize = 6,
	};

	// general signals
	enum ESignalType
	{
		eSignalType_FileAsyncReadScheduled = 1,
		eSignalType_LoadingScreenOn = 2,
		eSignalType_LoadingScreenOff = 3,
		eSignalType_BlackScreenOn = 4,
		eSignalType_BlackScreenOff = 5,
		eSignalType_VideoPlay = 6,
		eSignalType_VideoStop = 7,
		eSignalType_VideoReadRequested = 8,
		eSignalType_VideoReadCompleted = 9,
		eSignalType_LoadingProfilerBlock = 10,
		eSignalType_LoadingProfilerEnd = 11,
	};

	// general block types (single param)
	enum EBlockType
	{
		eBlockType_FileSyncOpen = 1,
		eBlockType_FileSyncClose = 2,
		eBlockType_FileAsyncOpen = 3,
		eBlockType_FileAsyncClose = 4,
		eBlockType_FileAsyncRead = 5,
		eBlockType_FileSyncRead = 6,
		eBlockType_FileSyncSeek = 7,

		eBlockType_LoadResource = 10,
		eBlockType_SyncLoadData = 11,
		eBlockType_SyncDecompression = 12,
		eBlockType_AsyncDecompression = 13,
		eBlockType_Deserialize = 14,
		eBlockType_LoadTables = 15,
		eBlockType_MapTables = 16,
		eBlockType_CreateObjects = 17,
		eBlockType_LoadImports = 18,
		eBlockType_LoadObjects = 19,
		eBlockType_PostLoad = 20,
		eBlockType_DeferredSyncRead = 21,
		eBlockType_CachedRead = 22,
		eBlockType_LoadInplace = 23,
		eBlockType_LoadingJob = 24,
		eBlockType_UserProfileBlock = 25,
	};

	// General initialize
	void Initilize( const red::AbsolutePath& basePath );
	void Shutdown();

	// Start/Stop profiling
	void Start();
	void Stop();

	// Flush current profiling
	void Flush();
	void ConditionalFlushOnce();

	// Are we profiling ?
	const Bool IsProfiling() const;

	// Map string to hash
	const Uint32 MapFilePath( const red::AbsolutePath& path );
	const Uint32 MapFilePath( const red::String& path );
	const Uint32 MapFilePath( const red::AnsiChar* path );
	const Uint32 MapFilePath( const red::UniChar* path );

	// Set thread name
	void SetThreadName( const red::AnsiChar* threadName );

	// IIOProfiler implementation

/*
	RED_FORCE_INLINE red::Uint32 ProfileAllocRequestId() override;
	RED_FORCE_INLINE void ProfileSetThreadName( const red::AnsiChar* name ) override;

	RED_FORCE_INLINE void ProfileFinishLoadingPhase( const red::AnsiChar* name ) override;
	RED_FORCE_INLINE void ProfileEndLoading() override;

	RED_FORCE_INLINE void ProfileAsyncIOOpenFileStart( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileAsyncIOOpenFileEnd( const red::Uint32 allocatedHandle ) override;
	RED_FORCE_INLINE void ProfileAsyncIOCloseFileStart( const red::Uint32 fileHandle ) override;
	RED_FORCE_INLINE void ProfileAsyncIOCloseFileEnd() override;
	RED_FORCE_INLINE void ProfileAsyncIOReadScheduled( const red::Uint32 fileHandle, const red::Uint32 requestID, const red::Uint64 offset, const red::Uint32 size ) override;
	RED_FORCE_INLINE void ProfileAsyncIOReadStart( const red::Uint32 requestID ) override;
	RED_FORCE_INLINE void ProfileAsyncIOReadEnd( const red::Uint32 requestID ) override;

	RED_FORCE_INLINE void ProfileSyncIOOpenFileStart( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileSyncIOOpenFileEnd( const red::Uint32 allocatedHandle ) override;
	RED_FORCE_INLINE void ProfileSyncIOCloseFileStart( const red::Uint32 fileHandle ) override;
	RED_FORCE_INLINE void ProfileSyncIOCloseFileEnd() override;
	RED_FORCE_INLINE void ProfileSyncIOReadStart( const red::Uint32 fileHandle, const red::Uint32 size ) override;
	RED_FORCE_INLINE void ProfileSyncIOReadEnd() override;
	RED_FORCE_INLINE void ProfileSyncIOSeekStart( const red::Uint32 fileHandle, const red::Uint64 offset ) override;
	RED_FORCE_INLINE void ProfileSyncIOSeekEnd() override;

	RED_FORCE_INLINE void ProfileDiskFileLoadResourceStart( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileLoadResourceEnd( const red::UniChar* filePath ) override;

	RED_FORCE_INLINE void ProfileDiskFileSyncLoadDataStart( const red::Uint32 size ) override;
	RED_FORCE_INLINE void ProfileDiskFileSyncLoadDataEnd() override;
	RED_FORCE_INLINE void ProfileDiskFileSyncDecompressStart( const red::Uint32 size, const red::Uint8 type ) override;
	RED_FORCE_INLINE void ProfileDiskFileSyncDecompressEnd() override;
	RED_FORCE_INLINE void ProfileDiskFileAsyncDecompressStart( const red::Uint32 size, const red::Uint8 type ) override;
	RED_FORCE_INLINE void ProfileDiskFileAsyncDecompressEnd() override;

	RED_FORCE_INLINE void ProfileDiskFileDeserializeStart( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileDeserializeEnd( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileLoadTablesStart( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileLoadTablesEnd( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileMapTablesStart( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileMapTablesEnd( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileCreateExportsStart( const red::UniChar* filePath, const red::Uint32 count ) override;
	RED_FORCE_INLINE void ProfileDiskFileCreateExportsEnd( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileLoadExportsStart( const red::UniChar* filePath, const red::Uint32 count ) override;
	RED_FORCE_INLINE void ProfileDiskFileLoadExportsEnd( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileLoadImportsStart( const red::UniChar* filePath, const red::Uint32 count ) override;
	RED_FORCE_INLINE void ProfileDiskFileLoadImportsEnd( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFilePostLoadStart( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFilePostLoadEnd( const red::UniChar* filePath ) override;
	RED_FORCE_INLINE void ProfileDiskFileLoadInplaceStart( const red::UniChar* filePath, const red::Uint32 count ) override;
	RED_FORCE_INLINE void ProfileDiskFileLoadInplaceEnd( const red::UniChar* filePath ) override;

	RED_FORCE_INLINE void ProfileDiskFileDeferredDataLoadSyncStart( const red::Uint32 size ) override;
	RED_FORCE_INLINE void ProfileDiskFileDeferredDataLoadSyncEnd() override;

	RED_FORCE_INLINE void ProfileVarAllocMemoryBlock( const red::Uint32 size ) override;
	RED_FORCE_INLINE void ProfileVarFreeMemoryBlock( const red::Uint32 size ) override;
	RED_FORCE_INLINE void ProfileVarAllocDecompressionTask() override;
	RED_FORCE_INLINE void ProfileVarFreeDecompressionTask() override;
	RED_FORCE_INLINE void ProfileVarAddPendingFiles( const red::Uint32 count ) override;
	RED_FORCE_INLINE void ProfileVarRemovePendingFiles( const red::Uint32 count ) override;

	RED_FORCE_INLINE void ProfileSignalShowLoadingScreen() override;
	RED_FORCE_INLINE void ProfileSignalHideLoadingScreen() override;
	RED_FORCE_INLINE void ProfileSignalShowBlackScreen() override;
	RED_FORCE_INLINE void ProfileSignalHideBlackScreen() override;
	RED_FORCE_INLINE void ProfileSignalVideoPlay() override;
	RED_FORCE_INLINE void ProfileSignalVideoStop() override;
	RED_FORCE_INLINE void ProfileSignalVideoReadRequested() override;
	RED_FORCE_INLINE void ProfileSignalVideoReadCompleted() override;*/

private:
	red::AbsolutePath			m_basePath;
	CProfilerBlockFileWriter	m_writer;

	void AssembleFilePath( red::AbsolutePath& outAbsoluteFilePath ) const;
};

REDCORE_API extern CIOProfiler GIOProfiler;

#endif // RED_PROFILE_FILE_SYSTEM

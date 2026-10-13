//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
//#include "../../core/include/corePublic.h"
//#include "../../core/include/fileSys.h"
//#include "../../core/include/configVar.h"
#include "profilerBlockFileWriter.h"
#include "ioProfiler.h"

RED_NO_EMPTY_FILE()

#ifdef RED_PROFILE_FILE_SYSTEM

REDCORE_API CIOProfiler GIOProfiler;

namespace Config
{
/*
	// helper TConfigVar wrapper that restarts job profiler dump every config value change
	template < typename T >
	class IOProfilerConfigVar : public TConfigVar< T >
	{
	public:
		IOProfilerConfigVar( const AnsiChar* group, const AnsiChar* name, const T& defaultValue, const Uint32 flags = 0 )
			: TConfigVar< T >( group, name, defaultValue, flags )
		{ }

		virtual void OnChanged() override
		{
			GIOProfiler.Start();
		}
	};

	IOProfilerConfigVar<Bool>		cvEnableIOProfiling( "Profiler/IO", "Enable", false );
	IOProfilerConfigVar<String>	cvOutputFilePrefix( "Profiler/IO", "OutputFilePrefix", "io" );
	IOProfilerConfigVar<String>	cvOutputFileDirectory( "Profiler/IO", "OutputDirectory", "" );
	IOProfilerConfigVar<Bool>		cvFlushCurrentData( "Profiler/IO", "FlushCurrentData", false );*/
} // Config

CIOProfiler::CIOProfiler()
{
	// define block types for system operations
	m_writer.RegisterBlockType( eBlockType_FileSyncOpen, "Sync Open", 1, 1 ); // (hash) (handle)
	m_writer.RegisterBlockType( eBlockType_FileSyncClose, "Sync Close", 1, 0 ); // (handle) ()
	m_writer.RegisterBlockType( eBlockType_FileAsyncOpen, "Async Open", 1, 1 ); // (hash) (handle)
	m_writer.RegisterBlockType( eBlockType_FileAsyncClose, "Async Close", 1, 0 ); // (handle) ()
	m_writer.RegisterBlockType( eBlockType_FileAsyncRead, "Async Read", 1, 1 ); // (reqid) (reqid)
	m_writer.RegisterBlockType( eBlockType_FileSyncRead, "Sync Read", 2, 0 ); // (file, size) ()
	m_writer.RegisterBlockType( eBlockType_FileSyncSeek, "Sync Seek", 3, 0 ); // (file, offsetLo, offsetHi) ()

	// define block types for engine operations
	m_writer.RegisterBlockType( eBlockType_LoadResource, "Load Resource", 1, 1 ); // (path) (path)
	m_writer.RegisterBlockType( eBlockType_SyncLoadData, "Sync Load File", 1, 0 ); // (size) ()
	m_writer.RegisterBlockType( eBlockType_SyncDecompression, "Sync Decompression", 2, 0 ); // (size, type) ()
	m_writer.RegisterBlockType( eBlockType_AsyncDecompression, "Async Decompression", 2, 0 ); // (size, type) ()
	m_writer.RegisterBlockType( eBlockType_Deserialize, "Deserialize", 1, 1 ); // (path) (path)
	m_writer.RegisterBlockType( eBlockType_LoadTables, "Load Tables", 1, 1 ); // (path) (path)
	m_writer.RegisterBlockType( eBlockType_MapTables, "Map Tables", 1, 1 ); // (path) (path)
	m_writer.RegisterBlockType( eBlockType_CreateObjects, "Create Objects", 2, 1 ); // (path, count) (path)
	m_writer.RegisterBlockType( eBlockType_LoadImports, "Load Imports", 2, 1 ); // (path, count) (path)
	m_writer.RegisterBlockType( eBlockType_LoadObjects, "Load Objects", 2, 1 ); // (path, count) (path)
	m_writer.RegisterBlockType( eBlockType_PostLoad, "Post Load", 1, 1 ); // (path) (path)
	m_writer.RegisterBlockType( eBlockType_DeferredSyncRead, "Sync Deferred Load", 1, 0 ); // (size) ()
	m_writer.RegisterBlockType( eBlockType_LoadInplace, "Load Inplace", 2, 1 ); // (path, count) (path)
	m_writer.RegisterBlockType( eBlockType_LoadingJob, "Job", 2, 0 ); // (name, priority) ()
	m_writer.RegisterBlockType( eBlockType_UserProfileBlock, "Block", 1, 0 ); // (name) ()

	// counters
	m_writer.RegisterCounterType( eCounterType_MemoryBlocks, "IO Memory" ); // (size)
	m_writer.RegisterCounterType( eCounterType_PendingFiles, "Pending files" ); // (count)
	m_writer.RegisterCounterType( eCounterType_DecompressionTasks, "Decompression tasks" ); // (count)

	// signals
	m_writer.RegisterSignalType( eSignalType_BlackScreenOn, "Blackscreen: ON", 0 );
	m_writer.RegisterSignalType( eSignalType_BlackScreenOff, "Blackscreen: OFF", 0 );
	m_writer.RegisterSignalType( eSignalType_LoadingScreenOn, "Loading screen: ON", 0 );
	m_writer.RegisterSignalType( eSignalType_LoadingScreenOff, "Loading screen: OFF", 0 );
	m_writer.RegisterSignalType( eSignalType_VideoPlay, "Video: PLAY", 0 );
	m_writer.RegisterSignalType( eSignalType_VideoStop, "Video: STOP", 0 );
	m_writer.RegisterSignalType( eSignalType_VideoReadRequested, "Video read requested", 0 );
	m_writer.RegisterSignalType( eSignalType_VideoReadCompleted, "Video read completed", 0 );
	m_writer.RegisterSignalType( eSignalType_FileAsyncReadScheduled, "Async Schedule", 5 ); // (handle, reqid, size, offsetLow, offsetHi)
	m_writer.RegisterSignalType( eSignalType_LoadingProfilerBlock, "Loading Block", 1 ); // (name)
	m_writer.RegisterSignalType( eSignalType_LoadingProfilerEnd, "Loading End", 0 );

	//IIOProfiler::Set( this );
}

CIOProfiler::~CIOProfiler()
{
	//IIOProfiler::Set( nullptr );
}

void CIOProfiler::AssembleFilePath( red::AbsolutePath& outAbsoluteFilePath ) const
{
	// use depot path if possible
	red::AbsolutePath outputPath;
/*
	if ( !Config::cvOutputFileDirectory.Get().Empty() )
	{
		outputPath = red::AbsolutePath::CreateDirPath( Config::cvOutputFileDirectory.Get() );
	}
	else*/
	{
#ifdef RED_PLATFORM_ORBIS
		outputPath.AppendDirPath( "/data/" );
#else
		outputPath = m_basePath;
#endif
	}

	// time stamp
	red::DateTime time;
	red::Clock::GetInstance().GetLocalTime(time);

	// assemble file path
/*
	String dateString = String::Printf( "%s_[%02d_%02d_%04d]_[%02d_%02d_%02d].redio",
		Config::cvOutputFilePrefix.Get().AsChar(),
		time.GetDay() + 1, time.GetMonth() + 1, time.GetYear(),
		time.GetHour(), time.GetMinute(), time.GetSecond() );
	outAbsoluteFilePath = outputPath.AddFilePath( dateString );*/
}

void CIOProfiler::Initilize( const red::AbsolutePath& basePath )
{
	m_basePath = basePath;

	// Override setting from command line :)
/*
	const Bool autoProfileIO = (nullptr !=  red::Strstr( red::SGetCommandLine(), TXT("-profileio") ));
	if ( autoProfileIO )
	{
		RED_LOG( "Core: IO profiling enabled by commandline" );
		Config::cvEnableIOProfiling.Set(true);
	}

	// Auto start profiling
	if ( Config::cvEnableIOProfiling.Get() )
	{
		if ( !IsProfiling() )
			Start();
	}*/
}

void CIOProfiler::Shutdown()
{
	// close on exit
	Stop();
}

void CIOProfiler::Start()
{
	// stop current profiling
	if ( IsProfiling() )
		Stop();

	// do not allow new profiling to be started
/*
	if ( !Config::cvEnableIOProfiling.Get() )
		return;*/

	// assemble output file path
	red::AbsolutePath outputFilePath;
	AssembleFilePath( outputFilePath );

	// start file
	m_writer.Start( outputFilePath );
}

void CIOProfiler::Stop()
{
	m_writer.Stop();
}

const Bool CIOProfiler::IsProfiling() const
{
	return m_writer.IsProfiling();
}

void CIOProfiler::Flush()
{
	m_writer.Flush();
}

const Uint32 CIOProfiler::MapFilePath( const red::AbsolutePath& path )
{
	return m_writer.MapString( path );
}

const Uint32 CIOProfiler::MapFilePath( const String& path )
{
	return m_writer.MapString( path );
}

const Uint32 CIOProfiler::MapFilePath( const UniChar* path )
{
	return m_writer.MapString( path );
}

const Uint32 CIOProfiler::MapFilePath( const AnsiChar* path )
{
	return m_writer.MapString( path );
}

void CIOProfiler::SetThreadName( const AnsiChar* threadName )
{
	return m_writer.SetThreadName( threadName );
}

void CIOProfiler::ConditionalFlushOnce()
{
/*
	if ( Config::cvFlushCurrentData.Get() )
	{
		Config::cvFlushCurrentData.Set(false);
		Flush();
	}*/
}

//////////////////////////////////////////////////////////////////////////

/*
Uint32 CIOProfiler::ProfileAllocRequestId()
{
	static atomic::TAtomic32 RequestIDCounter = 0;
	return atomic::Increment32( &RequestIDCounter );
}

void CIOProfiler::ProfileSetThreadName( const red::AnsiChar* name )
{
	SetThreadName( name );
}

void CIOProfiler::ProfileFinishLoadingPhase( const red::AnsiChar* name )
{
	Uint32 params[1] = { MapFilePath( name ) };
	m_writer.Signal( eSignalType_LoadingProfilerBlock, 1, params );
}

void CIOProfiler::ProfileEndLoading()
{
	m_writer.Signal( eSignalType_LoadingProfilerEnd, 0 );
}

void CIOProfiler::ProfileAsyncIOOpenFileStart( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.Start( eBlockType_FileAsyncOpen, 1, params );
}

void CIOProfiler::ProfileAsyncIOOpenFileEnd( const Uint32 allocatedHandle )
{
	Uint32 params[1] = { allocatedHandle };
	m_writer.End( eBlockType_FileAsyncOpen, 1, params );
}

void CIOProfiler::ProfileAsyncIOCloseFileStart( const Uint32 fileHandle )
{
	Uint32 params[1] = { fileHandle };
	m_writer.Start( eBlockType_FileAsyncClose, 1, params );
}

void CIOProfiler::ProfileAsyncIOCloseFileEnd()
{
	m_writer.End( eBlockType_FileAsyncClose, 0 );
}

void CIOProfiler::ProfileAsyncIOReadScheduled( const Uint32 fileHandle, const Uint32 requestID, const Uint64 offset, const Uint32 size )
{
	Uint32 params[5] = { fileHandle, requestID, static_cast<Uint32>( offset ), static_cast<Uint32>( offset >> 32 ), size };
	m_writer.Signal( eSignalType_FileAsyncReadScheduled, 5, params );
}

void CIOProfiler::ProfileAsyncIOReadStart( const Uint32 requestID )
{
	Uint32 params[1] = { requestID };
	m_writer.Start( eBlockType_FileAsyncRead, 1, params );
}

void CIOProfiler::ProfileAsyncIOReadEnd( const Uint32 requestID )
{
	Uint32 params[1] = { requestID };
	m_writer.End( eBlockType_FileAsyncRead, 1, params );
}

void CIOProfiler::ProfileSyncIOOpenFileStart( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.Start( eBlockType_FileSyncOpen, 1, params );
}

void CIOProfiler::ProfileSyncIOOpenFileEnd( const Uint32 allocatedHandle )
{
	Uint32 params[1] = { allocatedHandle };
	m_writer.End( eBlockType_FileSyncOpen, 1, params );
}

void CIOProfiler::ProfileSyncIOCloseFileStart( const Uint32 fileHandle )
{
	Uint32 params[1] = { fileHandle };
	m_writer.Start( eBlockType_FileSyncClose, 1, params );
}

void CIOProfiler::ProfileSyncIOCloseFileEnd()
{
	m_writer.End( eBlockType_FileSyncClose, 0 );
}

void CIOProfiler::ProfileSyncIOReadStart( const Uint32 fileHandle, const Uint32 size )
{
	Uint32 params[2] = { fileHandle, size };
	m_writer.Start( eBlockType_FileSyncRead, 2, params );
}

void CIOProfiler::ProfileSyncIOReadEnd()
{
	m_writer.End( eBlockType_FileSyncRead, 0 );
}

void CIOProfiler::ProfileSyncIOSeekStart( const Uint32 fileHandle, const Uint64 offset )
{
	Uint32 params[3] = { fileHandle, static_cast<Uint32>( offset ), static_cast<Uint32>( offset >> 32 ) };
	m_writer.Start( eBlockType_FileSyncSeek, 3, params );
}

void CIOProfiler::ProfileSyncIOSeekEnd()
{
	m_writer.End( eBlockType_FileSyncSeek, 0 );
}

void CIOProfiler::ProfileDiskFileLoadResourceStart( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.Start( eBlockType_LoadResource, 1, params );
}

void CIOProfiler::ProfileDiskFileLoadResourceEnd( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.End( eBlockType_LoadResource, 1, params );
}

void CIOProfiler::ProfileDiskFileSyncLoadDataStart( const Uint32 size )
{
	Uint32 params[1] = { size };
	m_writer.Start( eBlockType_SyncLoadData, 1, params );
}

void CIOProfiler::ProfileDiskFileSyncLoadDataEnd()
{
	m_writer.End( eBlockType_SyncLoadData, 0 );
}

void CIOProfiler::ProfileDiskFileSyncDecompressStart( const red::Uint32 size, const red::Uint8 type )
{
	Uint32 params[2] = { size, type };
	m_writer.Start( eBlockType_SyncDecompression, 2, params );
}

void CIOProfiler::ProfileDiskFileSyncDecompressEnd()
{
	m_writer.End( eBlockType_SyncDecompression, 0 );
}

void CIOProfiler::ProfileDiskFileAsyncDecompressStart( const red::Uint32 size, const red::Uint8 type )
{
	Uint32 params[2] = { size, type };
	m_writer.Start( eBlockType_AsyncDecompression, 2, params );
}

void CIOProfiler::ProfileDiskFileAsyncDecompressEnd()
{
	m_writer.End( eBlockType_AsyncDecompression, 0 );
}

void CIOProfiler::ProfileDiskFileDeserializeStart( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.Start( eBlockType_Deserialize, 1, params );
}

void CIOProfiler::ProfileDiskFileDeserializeEnd( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.End( eBlockType_Deserialize, 1, params );
}

void CIOProfiler::ProfileDiskFilePostLoadStart( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.Start( eBlockType_PostLoad, 1, params );
}

void CIOProfiler::ProfileDiskFilePostLoadEnd( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.End( eBlockType_PostLoad, 1, params );
}

void CIOProfiler::ProfileDiskFileCreateExportsStart( const UniChar* filePath, const Uint32 numExports )
{
	Uint32 params[2] = { MapFilePath( filePath ), numExports };
	m_writer.Start( eBlockType_CreateObjects, 2, params );
}

void CIOProfiler::ProfileDiskFileCreateExportsEnd( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.End( eBlockType_CreateObjects, 1, params );
}

void CIOProfiler::ProfileDiskFileLoadExportsStart( const UniChar* filePath, const Uint32 numExports )
{
	Uint32 params[2] = { MapFilePath( filePath ), numExports };
	m_writer.Start( eBlockType_LoadObjects, 2, params );
}

void CIOProfiler::ProfileDiskFileLoadExportsEnd( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.End( eBlockType_LoadObjects, 1, params );
}

void CIOProfiler::ProfileDiskFileLoadImportsStart( const UniChar* filePath, const Uint32 numImports )
{
	Uint32 params[2] = { MapFilePath( filePath ), numImports };
	m_writer.Start( eBlockType_LoadImports, 2, params );
}

void CIOProfiler::ProfileDiskFileLoadImportsEnd( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.End( eBlockType_LoadImports, 1, params );
}

void CIOProfiler::ProfileDiskFileLoadInplaceStart( const UniChar* filePath, const Uint32 numImports )
{
	Uint32 params[2] = { MapFilePath( filePath ), numImports };
	m_writer.Start( eBlockType_LoadInplace, 2, params );
}

void CIOProfiler::ProfileDiskFileLoadInplaceEnd( const UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.End( eBlockType_LoadInplace, 1, params );
}

void CIOProfiler::ProfileDiskFileLoadTablesStart( const red::UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.Start( eBlockType_LoadTables, 1, params );
}

void CIOProfiler::ProfileDiskFileLoadTablesEnd( const red::UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.End( eBlockType_LoadTables, 1, params );
}

void CIOProfiler::ProfileDiskFileMapTablesStart( const red::UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.Start( eBlockType_MapTables, 1, params );
}

void CIOProfiler::ProfileDiskFileMapTablesEnd( const red::UniChar* filePath )
{
	Uint32 params[1] = { MapFilePath( filePath ) };
	m_writer.End( eBlockType_MapTables, 1, params );
}

void CIOProfiler::ProfileDiskFileDeferredDataLoadSyncStart( const red::Uint32 size )
{
	Uint32 params[] = { size };
	m_writer.Start( eBlockType_DeferredSyncRead, 1, params );
}

void CIOProfiler::ProfileDiskFileDeferredDataLoadSyncEnd()
{
	m_writer.End( eBlockType_DeferredSyncRead, 0 );
}

//--------------------

void CIOProfiler::ProfileVarAllocMemoryBlock( const red::Uint32 size )
{
	m_writer.CounterIncrement( eCounterType_MemoryBlocks, size );
}

void CIOProfiler::ProfileVarFreeMemoryBlock( const red::Uint32 size )
{
	m_writer.CounterDecrement( eCounterType_MemoryBlocks, size );
}

void CIOProfiler::ProfileVarAllocDecompressionTask()
{
	m_writer.CounterIncrement( eCounterType_DecompressionTasks, 1 );
}

void CIOProfiler::ProfileVarFreeDecompressionTask()
{
	m_writer.CounterDecrement( eCounterType_DecompressionTasks, 1 );
}

void CIOProfiler::ProfileVarAddPendingFiles( const red::Uint32 count )
{
	m_writer.CounterIncrement( eCounterType_PendingFiles, count );
}

void CIOProfiler::ProfileVarRemovePendingFiles( const red::Uint32 count )
{
	m_writer.CounterDecrement( eCounterType_PendingFiles, 1 );
}

//------------------

void CIOProfiler::ProfileSignalShowLoadingScreen()
{
	m_writer.Signal( eSignalType_LoadingScreenOn, 0 );
}

void CIOProfiler::ProfileSignalHideLoadingScreen()
{
	m_writer.Signal( eSignalType_LoadingScreenOff, 0 );
}

void CIOProfiler::ProfileSignalShowBlackScreen()
{
	m_writer.Signal( eSignalType_BlackScreenOn, 0 );
}

void CIOProfiler::ProfileSignalHideBlackScreen()
{
	m_writer.Signal( eSignalType_BlackScreenOff, 0 );
}

void CIOProfiler::ProfileSignalVideoPlay()
{
	m_writer.Signal( eSignalType_VideoPlay, 0 );
}

void CIOProfiler::ProfileSignalVideoStop()
{
	m_writer.Signal( eSignalType_VideoStop, 0 );
}

void CIOProfiler::ProfileSignalVideoReadRequested()
{
	m_writer.Signal( eSignalType_VideoReadRequested, 0 );
}

void CIOProfiler::ProfileSignalVideoReadCompleted()
{
	m_writer.Signal( eSignalType_VideoReadCompleted, 0 );
}*/

#endif // RED_PROFILE_FILE_SYSTEM
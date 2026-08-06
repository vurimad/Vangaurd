/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include <algorithm>

#include "dbgUtils.h"
#include "errorHandler.h"
#include "errorReporterIPCWin32.h"

#ifdef RED_PLATFORM_WINPC
namespace dbgutils
{
namespace win32
{

const wchar_t ERROR_REPORTER_EXE[] = L"REDEngineErrorReporter.exe";

const wchar_t ERROR_REPORTER_TOOLS_PATH[] = L"engine\\tools\\";
const wchar_t ERROR_REPORT_MANAGER_EXE[] = L"ErrorReportManager.exe";

const wchar_t CRASH_REPOTER_PATH[] = L"CrashReporter\\";
const wchar_t CRASH_REPOTER_EXE[] = L"CrashReporter.exe";

// #tbd: use handle inheritance, duplicate handles, and anon names instead
const wchar_t ERROR_REPORTER_FILEMAPPING_PREFIX[] = L"REDEngineErrorReporter";
const wchar_t ERROR_REPORTER_EVENTWAKEUP_PREFIX[] = L"REDEngineErrorReporter_wake";
const wchar_t ERROR_REPORTER_EVENTREPORTFINISHED_PREFIX[] = L"REDEngineErrorReporter_report";
const wchar_t ERROR_REPORTER_MUTEX_PREFIX[] = L"REDEngineErrorReporter_mutex";

namespace impl
{
	Bool CopyToRemote( HANDLE remoteProcess, void* __restrict pOutRemoteValue, const void* __restrict pLocalValue, size_t bytesToCopy )
	{
		if ( !remoteProcess || !pOutRemoteValue || !pLocalValue )
		{
			return false;
		}
		size_t numActualBytes = 0;
		if ( !::WriteProcessMemory( remoteProcess, pOutRemoteValue, pLocalValue, bytesToCopy, &numActualBytes ) )
		{
			RED_DBG_TRACE( "WriteProcessMemory failed: 0x%08X", ::GetLastError() );
			return false;
		}
		if ( numActualBytes != bytesToCopy )
		{
			RED_DBG_TRACE( "WriteProcessMemory incomplete write" );
			return false;
		}

		return true;
	}

	Bool CopyToLocal( HANDLE remoteProcess, void* __restrict pOutLocalValue, const void* __restrict pRemoteValue, size_t bytesToCopy )
	{
		if ( !remoteProcess || !pRemoteValue || !pOutLocalValue )
		{
			return false;
		}
		size_t numActualBytes = 0;
		if ( !::ReadProcessMemory( remoteProcess, pRemoteValue, pOutLocalValue, bytesToCopy, &numActualBytes ) )
		{
			RED_DBG_TRACE( "ReadProcessMemory failed: 0x%08X", ::GetLastError() );
			return false;
		}
		if ( numActualBytes != bytesToCopy )
		{
			RED_DBG_TRACE( "ReadProcessMemory incomplete read" );
			return false;
		}

		return true;
	}

	Bool CopyArrayToLocal( HANDLE remoteProcess, void* __restrict outLocalArray, Uint32 localArrayCount, const void* __restrict remoteArray, Uint32 remoteArrayCount, size_t elementSize, Uint32& outNumElementsCopied )
	{
		outNumElementsCopied = 0;
		if ( !remoteProcess || !remoteArray || !outLocalArray )
		{
			return false;
		}

		const Uint32 countToRead = std::min<Uint32>( localArrayCount, remoteArrayCount );
		if ( countToRead == 0 )
		{
			return false;
		}

		size_t numBytesActuallyRead = 0;
		const size_t bytesToRead = elementSize * countToRead;
		if ( !::ReadProcessMemory( remoteProcess, remoteArray, outLocalArray, bytesToRead, &numBytesActuallyRead ) )
		{
			RED_DBG_TRACE( "CopyArrayToLocal: ReadProcessMemory failed: 0x%08X", ::GetLastError() );
			return false;
		}
		else if ( numBytesActuallyRead != bytesToRead )
		{
			RED_DBG_TRACE( "CopyArrayToLocal: ReadProcessMemory incomplete size" );
			return false;
		}

		outNumElementsCopied = countToRead;
		return true;
	}

	// Size is buffer count including  null terminator
	Bool CopyStringToLocal( HANDLE remoteProcess, void* __restrict localDst, Uint32 localCount, const void* __restrict remoteSrc, Uint32 remoteCount, size_t elementSize )
	{
		if ( remoteCount > localCount )
		{
			RED_DBG_TRACE( "Warning CopyStringToLocal: string truncation. Remote size larger than local size" );
		}

		Uint32 numCopied = 0;
		if ( !CopyArrayToLocal( remoteProcess, localDst, localCount, remoteSrc, remoteCount, elementSize, numCopied ) )
		{
			return false;
		}

		// Ensure null terminated. E.g., could have been truncated or corrupted.
		if ( localDst )
		{
			const Uint32 n = ( numCopied > 0 ) ? numCopied - 1 : 0;
			const Uint32 offset = n * static_cast< Uint32 >( elementSize );
			red::Memzero( static_cast< Uint8* >( localDst ) + offset, elementSize );
		}

		return true;
	}

	///---

	Bool CopyArrayToRemote( HANDLE remoteProcess, void* __restrict outRemoteArray, Uint32 remoteArrayCount, const void* __restrict localArray, Uint32 localArrayCount, size_t elementSize, Uint32& outNumElementsCopied )
	{
		outNumElementsCopied = 0;
		if ( !remoteProcess || !outRemoteArray || !localArray )
		{
			return false;
		}

		const Uint32 countToWrite = std::min<Uint32>( remoteArrayCount, localArrayCount );
		if ( countToWrite == 0 )
		{
			return false;
		}

		size_t numBytesActuallyWritten = 0;
		const size_t bytesToWrite = elementSize * countToWrite;
		if ( !::WriteProcessMemory( remoteProcess, outRemoteArray, localArray, bytesToWrite, &numBytesActuallyWritten ) )
		{
			RED_DBG_TRACE( "CopyArrayToRemote: WriteProcessMemory failed: 0x%08X", ::GetLastError() );
			return false;
		}
		else if ( numBytesActuallyWritten != bytesToWrite )
		{
			RED_DBG_TRACE( "CopyArrayToRemote: WriteProcessMemory incomplete size" );
			return false;
		}

		outNumElementsCopied = countToWrite;
		return true;
	}

	// Size is buffer count including  null terminator
	Bool CopyStringToRemote( HANDLE remoteProcess, void* __restrict remoteDst, Uint32 remoteCount, const void* __restrict localSrc, Uint32 localCount, size_t elementSize )
	{
		if ( localCount > remoteCount )
		{
			RED_DBG_TRACE( "Warning CopyStringToRemote: string truncation. Local size larger than remote size" );
		}

		Uint32 numCopied = 0;
		if ( !CopyArrayToRemote( remoteProcess, remoteDst, remoteCount, localSrc, localCount, elementSize, numCopied ) )
		{
			return false;
		}

		// Ensure null terminated. E.g., could have been truncated or corrupted.
		if ( remoteDst )
		{
			char zeroes[4];
			red::Memzero( zeroes, sizeof(zeroes) );
			const Uint32 n = ( numCopied > 0 ) ? numCopied - 1 : 0;
			const Uint32 offset = n * static_cast< Uint32 >( elementSize );
			size_t numZeroesWritten = 0;
 			if ( !WriteProcessMemory( remoteProcess, static_cast< Uint8* >( remoteDst ) + offset, &zeroes, elementSize, &numZeroesWritten ) )
			{
				RED_DBG_TRACE( "CopyStringToRemote append null: WriteProcessMemory failed 0x%08X", ::GetLastError() );
				return false;
			}
			if ( numZeroesWritten != elementSize )
			{
				RED_DBG_TRACE( "CopyStringToRemote append null: WriteProcessMemory incomplete write!" );
				return false;
			}
		}

		return true;
	}

	///---

	Bool OpenIPCFileMapping( const wchar_t* mappingName, Uint32 size, Uint8 openFlags, FileMappingInfo& outFileMapping )
	{
		HANDLE hMapFile = nullptr;
		if ( (openFlags & eFileMappingFlag_Existing) != 0 )
		{
			hMapFile = ::OpenFileMappingW( FILE_MAP_ALL_ACCESS, FALSE, mappingName );
		}
		else
		{
			hMapFile = ::CreateFileMappingW( INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, mappingName );
		}

		if ( !hMapFile )
		{
			RED_DBG_TRACE( "Can't open file mapping: 0x%08X", ::GetLastError() );
			return false;
		}

		const Uint32 accessFlags = FILE_MAP_READ | ( openFlags & eFileMappingFlag_Write ? FILE_MAP_WRITE : 0 );
		void* const mappedFileView = ::MapViewOfFile( hMapFile, accessFlags, 0, 0, size );
		if ( !mappedFileView )
		{
			RED_DBG_TRACE( "MapViewOfFile failed: 0x%08X", ::GetLastError() );
			::CloseHandle( hMapFile );
			return false;
		}

		outFileMapping.m_hMapFile = hMapFile;
		outFileMapping.m_pMappedView = mappedFileView;

		return true;
	}

	void CloseIPCFileMapping( FileMappingInfo& fileMapping )
	{
		if ( fileMapping.m_pMappedView )
		{
			::UnmapViewOfFile( fileMapping.m_pMappedView );
			fileMapping.m_pMappedView = nullptr;
		}
		if ( fileMapping.m_hMapFile )
		{
			::CloseHandle( fileMapping.m_hMapFile );
			fileMapping.m_hMapFile = nullptr;
		}
	}
} // impl

static Bool WaitForRemoteEvent( HANDLE hEvent, HANDLE hRemoteProcess )
{
	if ( !hEvent || !hRemoteProcess )
	{
		return false;
	}

	const Uint32 numHandles = 2;

	// hEvent must come first in the array so it's WAIT_OBJECT_0
	const HANDLE toWait[numHandles] = { hEvent, hRemoteProcess };
	const Uint32 ret = ::WaitForMultipleObjects( numHandles, toWait, FALSE, INFINITE );
	if ( ret != WAIT_OBJECT_0 )
	{
		// If result is WAIT_OBJECT_0 + 1, then it means the process exited without setting the event
		RED_DBG_TRACE( "WaitForMultipleObjects ret = %u", ret );
		return false;
	}

	return true;
}

// Define in the .cpp, so can't get the wrong version from including a header
static const Uint32 IPC_ARGS_VERSION = 6;
static const size_t IPC_ARGS_SIZE = sizeof( IPCArgs );

IPCArgs::IPCArgs()
	: m_sizeofIPCArgs( IPC_ARGS_SIZE )
	, m_ipcArgsVersion( IPC_ARGS_VERSION )
	, m_pRemoteExceptionPointers( nullptr )
	, m_pRemoteRegisteredAttachmentTable( nullptr )
	, m_ipcErrMsgArgs()
	, m_remoteAppVersionNumber( nullptr )
	, m_remoteAppVersionNumberCount( 0 )
	, m_processID( 0 )
	, m_threadID( 0 )
	, m_errorReason( red::eErrorReason_Unknown )
{}

ScopedSetEvent::ScopedSetEvent( HANDLE hEvent)
	: m_hEvent( hEvent )
{}

ScopedSetEvent::~ScopedSetEvent()
{
	if ( m_hEvent )
	{
		if ( !::SetEvent ( m_hEvent ) )
		{
			RED_DBG_TRACE( "SetEvent failed: 0x%08X", ::GetLastError() );
		}
	}
}

ScopedHandle::ScopedHandle( HANDLE hHandle )
	: m_hHandle( hHandle )
{}

ScopedHandle::~ScopedHandle()
{
	if ( m_hHandle )
	{
		::CloseHandle( m_hHandle );
	}
}

static void CleanupIPC( IPCContext& ipcContext )
{
	HANDLE* toClose[] = { &ipcContext.m_hEventReportFinished, &ipcContext.m_hEventWakeUpErrorReportProcess, &ipcContext.m_hRemoteProcess, &ipcContext.m_hReporterAccessMutex };
	for ( Uint32 i = 0; i < RED_ARRAY_COUNT_U32(toClose); ++i )
	{
		if ( *toClose[i] )
		{
			::CloseHandle( *toClose[i] );
			*toClose[i] = nullptr;
		}
	}

	ipcContext.m_ipcArgs.Close();
	ipcContext.m_ownerProcessID = 0;
	ipcContext.m_isValid = false;
}

Bool OpenIPC( Uint32 processID, EIPCOpenParam param, IPCContext& outIPCContext )
{
	// Guard against multiple initialization
	if ( outIPCContext.m_isValid )
	{
		RED_DBG_TRACE( "Reinitializing valid IPC context!");
		CleanupIPC( outIPCContext );
		return false;
	}

	const Bool openExisting = param == eIPCOpenParam_ErrorReporter;

	if ( openExisting )
	{
		HANDLE hRemoteProcess = ::OpenProcess( PROCESS_ALL_ACCESS, FALSE, processID );
		if ( !hRemoteProcess )
		{
			RED_DBG_TRACE( "Failed to open processID %u: 0x%08X", processID, ::GetLastError() );
			return false;
		}
		outIPCContext.m_hRemoteProcess = hRemoteProcess;
	}
	else
	{
		// Will be set once connects
		outIPCContext.m_hRemoteProcess = nullptr;
	}

	const Uint32 NAME_LEN = 64;
	wchar_t name[ NAME_LEN ] = {0};
	red::SNPrintFUnsafe( name, NAME_LEN, L"%ls.%u", ERROR_REPORTER_FILEMAPPING_PREFIX, processID );

	const Uint8 openFlags = eFileMappingFlag_ReadWrite | ( openExisting ? eFileMappingFlag_Existing : 0 );
	if ( !outIPCContext.m_ipcArgs.Open( name, openFlags ) )
	{
		CleanupIPC( outIPCContext );
		return false;
	}

	red::SNPrintFUnsafe( name, NAME_LEN, L"%ls.%u", ERROR_REPORTER_EVENTWAKEUP_PREFIX, processID );
	outIPCContext.m_hEventWakeUpErrorReportProcess = openExisting ? ::OpenEventW( EVENT_ALL_ACCESS, FALSE, name ) : ::CreateEventW( nullptr, TRUE, FALSE, name );
	if ( !outIPCContext.m_hEventWakeUpErrorReportProcess )
	{
		RED_DBG_TRACE( "Failed to create wakeup event! 0x%08X", ::GetLastError() );
		CleanupIPC( outIPCContext );
		return false;
	}

	red::SNPrintFUnsafe( name, NAME_LEN, L"%ls.%u", ERROR_REPORTER_EVENTREPORTFINISHED_PREFIX, processID );
	outIPCContext.m_hEventReportFinished = openExisting ? ::OpenEventW( EVENT_ALL_ACCESS, FALSE, name ) : ::CreateEventW( nullptr, TRUE, FALSE, name );
	if ( !outIPCContext.m_hEventReportFinished )
	{
		RED_DBG_TRACE( "Failed to create report event! 0x%08X", ::GetLastError() );
		CleanupIPC( outIPCContext );
		return false;
	}

	red::SNPrintFUnsafe( name, NAME_LEN, L"%ls.%u", ERROR_REPORTER_MUTEX_PREFIX, processID );
	outIPCContext.m_hReporterAccessMutex = openExisting ? ::OpenMutexW( SYNCHRONIZE, FALSE, name ) : ::CreateMutexW( nullptr, FALSE, name );
	if ( !outIPCContext.m_hReporterAccessMutex )
	{
		RED_DBG_TRACE( "Failed to create mutex! 0x%08X", ::GetLastError() );
		CleanupIPC( outIPCContext );
		return false;
	}

	outIPCContext.m_isValid = true;

	return true;
}

static Bool GetModuleName( HMODULE hModule, wchar_t* buf, Uint32 bufSize )
{
	wchar_t tmp[MAX_PATH];
	const Uint32 len = ::GetModuleFileNameW( hModule, tmp, MAX_PATH );
	if ( len > 0 )
	{
		const wchar_t* lastSlash = red::StrchrR( tmp, L'\\' );
		const wchar_t* baseName = lastSlash ? lastSlash + 1 : tmp;
		red::Strcpy( buf, baseName, bufSize );
		return true;
	}

	return false;
}

static HANDLE CreateProcessBreakawayFromJobWithFallback( const wchar_t* exePath, wchar_t* mutableCmdLine )
{
	static STARTUPINFOW startupInfo;
	red::Memzero( &startupInfo, sizeof(startupInfo) );
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = SW_HIDE; // will unhide itself if needed, avoided flashing some console or window

	static PROCESS_INFORMATION processInfo;
	red::Memzero( &processInfo, sizeof(processInfo) );

	// ctremblay: Hack to remove UNC path ( "\\?\" ) given from PS4 crash handler. Seems C# do not like UNC.
	while( *exePath != '\0' )
	{
		const wchar_t character = *exePath;
		if( character != '\\' && character != '?' )
			break;

		++exePath;
	}
	

	Uint32 procFlags = CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_CONSOLE | CREATE_DEFAULT_ERROR_MODE | CREATE_NEW_PROCESS_GROUP;
	if ( ::CreateProcessW( exePath, mutableCmdLine, nullptr, nullptr, FALSE, procFlags, nullptr, nullptr, &startupInfo, &processInfo ) )
	{
		::CloseHandle( processInfo.hThread );
		return processInfo.hProcess;
	}

	// JOB_OBJECT_LIMIT_BREAKAWAY_OK probably not set
	if ( ::GetLastError() == ERROR_ACCESS_DENIED )
	{
		procFlags &= ~CREATE_BREAKAWAY_FROM_JOB;
		if ( ::CreateProcessW( exePath, mutableCmdLine, nullptr, nullptr, FALSE, procFlags, nullptr, nullptr, &startupInfo, &processInfo ) )
		{
			::CloseHandle( processInfo.hThread );
			return processInfo.hProcess;
		}
	}

	return nullptr;
}

static void GoUpParentPath( wchar_t* buffer, Uint32 bufferSize, const wchar_t* appendPath, wchar_t** outPathEnd )
{
	wchar_t* pathEnd = red::StrchrR( buffer, L'\\' );
	if ( pathEnd )
	{
		*outPathEnd = pathEnd;
		++pathEnd;
	}
	else
	{
		*outPathEnd = buffer;
		pathEnd = buffer;
	}
	*pathEnd = L'\0';

	const size_t prefixLen = red::Strlen( buffer );
	red::Strcpy( pathEnd, appendPath, bufferSize - prefixLen );
}

static HANDLE CreateReporterProcess( const wchar_t* reporterExe, const wchar_t* reporterPath, wchar_t* cmdLine, Uint32 goUpParentDepth )
{
	RED_DBG_TRACE( "Reporter cmdline args: %ls", cmdLine );

	// First try looking for reporter EXE in binary (not whatever current working) directory
	static wchar_t exePath[ MAX_PATH ] = { '\0' };
	::GetModuleFileNameW( nullptr, exePath, RED_ARRAY_COUNT_U32( exePath ) );

	wchar_t* parentEnd = nullptr;
	GoUpParentPath( exePath, RED_ARRAY_COUNT_U32( exePath ), reporterExe, &parentEnd );
	HANDLE hProcessRet = nullptr;
	hProcessRet = CreateProcessBreakawayFromJobWithFallback( exePath, cmdLine );
	if ( hProcessRet )
	{
		RED_DBG_TRACE( "Launched repoter from binary directory: %ls", exePath );
	}
	else
	{
		RED_DBG_TRACE( "Couldn't launch repoter from binary directory: %ls", exePath );
		wchar_t exeSubPath[ 64 ];
		red::Strcpy( exeSubPath, reporterPath, RED_ARRAY_COUNT_U32( exeSubPath ) );
		red::Strcat( exeSubPath, reporterExe, RED_ARRAY_COUNT_U32( exeSubPath ) );

		for ( Uint32 i = 0; i <= goUpParentDepth; ++i )
		{
			GoUpParentPath( exePath, RED_ARRAY_COUNT_U32( exePath ), exeSubPath, &parentEnd );
			hProcessRet = CreateProcessBreakawayFromJobWithFallback( exePath, cmdLine );
			if ( hProcessRet )
			{
				RED_DBG_TRACE( "Launched reporter from path '%ls'", exePath );
				break;
			}
			else
			{
				RED_DBG_TRACE( "Couldn't launch reporter from path: %ls", exePath );
			}
			*parentEnd = L'\0';
		}
	}

	return hProcessRet;
}

static Bool LaunchReporter( const wchar_t* reporterExe, const wchar_t* reporterPath, wchar_t* cmdLine, Uint32 goUpParentDepth )
{
	HANDLE hProcessRet = CreateReporterProcess( reporterExe, reporterPath, cmdLine, goUpParentDepth );

	if ( hProcessRet )
	{
		::CloseHandle( hProcessRet );
		return true;
	}
	return false;
}

Bool REDSYSTEM_API LaunchErrorReportManager()
{
	const Uint32 CMDLINE_LEN = 64;
	wchar_t cmdLine[ CMDLINE_LEN ] = L"";

	return LaunchReporter( ERROR_REPORT_MANAGER_EXE, ERROR_REPORTER_TOOLS_PATH, cmdLine, 3 );
}

Bool REDSYSTEM_API LaunchCrashReporter()
{
	const Uint32 CMDLINE_LEN = 64;
	wchar_t cmdLine[ CMDLINE_LEN ] = L"";

	return LaunchReporter( CRASH_REPOTER_EXE, CRASH_REPOTER_PATH, cmdLine, 2 );
}

static HANDLE CreateErrorReporterProcess()
{
	const Uint32 CMDLINE_LEN = 64;
	wchar_t cmdLine[ CMDLINE_LEN ];
	red::SNPrintFUnsafe( cmdLine, CMDLINE_LEN, L"%ls %u", ERROR_REPORTER_EXE, ::GetCurrentProcessId() );

	return CreateReporterProcess( ERROR_REPORTER_EXE, ERROR_REPORTER_TOOLS_PATH, cmdLine, 3 );
}

Bool ConnectErrorReporterIfNeeded_NotReentrant( IPCContext& ipcContext )
{
	// static variables to help mitigate stack overflow situations
	static Bool checkOnce = false;
	static Bool refuseToLaunch = false;
	if ( !checkOnce )
	{
		static wchar_t moduleName[64] = { L'\0' };

		// Extra insurance against recursively launching some error reporter process
		if ( GetModuleName( nullptr, moduleName, RED_ARRAY_COUNT_U32( moduleName ) ) )
		{
			red::StrToLower( moduleName, RED_ARRAY_COUNT_U32( moduleName ) );
			if ( red::Strstr( moduleName, L"errorreport" ) )
			{
				RED_DBG_TRACE( "Refusing to launch error reporter!" );
				refuseToLaunch = true;
			}
		}
		else
		{
			RED_DBG_TRACE( "Failed to get module name!" );
		}
	}

	if ( refuseToLaunch )
	{
		return false;
	}

	if ( !ipcContext.m_isValid )
	{
		return false;
	}

	// #tbd: detect if error reporter crashed after here
	if  ( ipcContext.m_hRemoteProcess )
	{
		if ( ::WaitForSingleObject( ipcContext.m_hRemoteProcess, 0 ) == WAIT_OBJECT_0 )
		{
			// Apparently error reporter exited
			return false;
		}
		else
		{
			// Already should have launched
			return true;
		}
	}

	const HANDLE hProcess = CreateErrorReporterProcess();
	if ( !hProcess )
	{
		return false;
	}

	ipcContext.m_hRemoteProcess = hProcess;
	return true;
}

void CloseIPC( IPCContext& ipcContext )
{
	CleanupIPC( ipcContext );
}

Bool WakeErrorReporter( const IPCContext& ipcContext )
{
	if ( !ipcContext.m_isValid )
	{
		return false;
	}
	if ( !::SetEvent( ipcContext.m_hEventWakeUpErrorReportProcess ) )
	{
		RED_DBG_TRACE( "SetEvent failed: 0x%08X", ::GetLastError() );
		return false;
	}

	return true;
}

Bool WaitForErrorReportRequested( const IPCContext& ipcContext )
{
	if ( !ipcContext.m_isValid )
	{
		return false;
	}

	if ( !WaitForRemoteEvent( ipcContext.m_hEventWakeUpErrorReportProcess, ipcContext.m_hRemoteProcess ) )
	{
		return false;
	}

	if ( !::ResetEvent( ipcContext.m_hEventWakeUpErrorReportProcess ) )
	{
		RED_DBG_TRACE( "ResetEvent failed: 0x%08X", ::GetLastError() );
		return false;
	}

	return true;
}

Bool WaitForErrorReportFinished( const IPCContext& ipcContext )
{
	if ( !ipcContext.m_isValid )
	{
		return false;
	}
	if ( !WaitForRemoteEvent( ipcContext.m_hEventReportFinished, ipcContext.m_hRemoteProcess ) )
	{
		return false;
	}

	if ( !::ResetEvent( ipcContext.m_hEventReportFinished ) )
	{
		RED_DBG_TRACE( "ResetEvent failed: 0x%08X", ::GetLastError() );
		return false;
	}

	return true;
}

} // win32
} // dbgutils
#endif // RED_PLATFORM_WINPC
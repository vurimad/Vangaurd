/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/
#include "build.h"

#include "dbgUtils.h"
#include "threads.h" // for RED_TLS
#include "errorHandler.h"

#if defined( RED_PLATFORM_WINPC )

// #define FORCE_FINAL_MINIDUMP

#if (defined( RED_CONFIGURATION_FINAL ) && !defined( USE_PROFILER )) || defined(FORCE_FINAL_MINIDUMP)
// When defined we will be creating final version of minidump with stripped user information
// and the smallest possible size.
#	define FINAL_MINIDUMP
#endif


#define _NO_CVCONST_H
#include <DbgHelp.h>
#undef _NO_CVCONST_H

#include <TlHelp32.h>
#include <shellapi.h>
#include <wct.h>
#include <excpt.h>
#include <signal.h>
#include <psapi.h>
#include <memory>
#include <lmcons.h>

#include "platformUtilsWin32.h"
#include "utility.h"

#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Version.lib")

// Note: the DbgHelp functions frequently use ::HeapAlloc(), so aren't particularly safe
// in-process during critical situations.
namespace dbgutils
{

REDSYSTEM_API Bool GEnableTrace = false;
REDSYSTEM_API Bool GTraceTimestamp = false;
REDSYSTEM_API FILE* GTraceFile = nullptr;

// #tbd: could be a define, but premake isn't cooperating
REDSYSTEM_API Bool GIsErrorReporter = false;

namespace // anonymous
{
	static CRITICAL_SECTION g_csDbgHelp;

	// Always either the local or remote process. If changed, then need to cleanup/re-initialize dbghelp
	// #todo: delete this and just use conn info (after reinitialize if needed)
	static Uint32 g_dbgHelpProcessID;
	static HANDLE g_hDbgHelpProcess;

	static Bool g_isDbgHelpInitialized;

	static INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;

	static BOOL CALLBACK InitDbgHelpMutex( PINIT_ONCE, PVOID, PVOID* )
	{
		::InitializeCriticalSection( &g_csDbgHelp );
		return TRUE;
	}

	static void LockDbgHelpMutex()
	{
		::InitOnceExecuteOnce( &g_initOnce, InitDbgHelpMutex, nullptr, nullptr );
		::EnterCriticalSection( &g_csDbgHelp );
	}

	static void UnlockDbgHelpMutex()
	{
		::LeaveCriticalSection( &g_csDbgHelp );
	}

	static const AnsiChar* GetLastErrorMessage()
	{
		thread_local static AnsiChar errorMessageBuffer[ 1024 ];

		DWORD length = FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, ::GetLastError(),
									   MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), errorMessageBuffer, RED_ARRAY_COUNT( errorMessageBuffer ), nullptr );

		// Cut "/r/n"
		if ( length > 2 )
		{
			errorMessageBuffer[ length - 2 ] = '\0';
		}

		return ( length > 0 ) ? errorMessageBuffer : "Unknown error";
	}

	struct ConnectionInfo
	{
		HANDLE	m_hProcess;
		Uint32	m_processID;
	};

	namespace prv
	{
		static RED_TLS ConnectionInfo g_localConnectionInfo;
		static RED_TLS ConnectionInfo g_remoteConnectionInfo;
	}

	static const ConnectionInfo& GetThreadConnectionInfo( EConnection conn )
	{
		if ( conn == eConnection_LocalProcess )
		{
			if ( !prv::g_localConnectionInfo.m_hProcess )
			{
				prv::g_localConnectionInfo.m_hProcess = ::GetCurrentProcess();
				prv::g_localConnectionInfo.m_processID = ::GetCurrentProcessId();
			}
			return prv::g_localConnectionInfo;
		}
		else
		{
			return prv::g_remoteConnectionInfo;
		}
	}

	struct ScopedThreadInfo
	{
		enum { MAX_THREADS = 512 };

		ScopedThreadInfo()
			: m_numThreads( 0 )
			, m_excludedThreadID( 0 )
			, m_suspended( false )
		{
		}

		void SuspendThreads( EConnection conn )
		{
			if ( !m_suspended )
			{
				m_suspended = true;	
				m_excludedThreadID = conn == eConnection_LocalProcess ? ::GetCurrentThreadId() : 0;
				for ( Uint32 i = 0; i < m_numThreads; ++i )
				{
					if ( m_infos[i].m_id != m_excludedThreadID )
					{
						(void)SuspendThread( m_infos[i].m_handle );
					}
				}
			}
		}

		~ScopedThreadInfo()
		{
			Clear();
		}

		void Clear()
		{
			if ( m_suspended )
			{
				for ( Uint32 i = 0; i < m_numThreads; ++i )
				{
					if( m_infos[i].m_id != m_excludedThreadID )
					{
						(void)::ResumeThread( m_infos[ i ].m_handle);
					}
				}
			}
			for ( Uint32 i = 0; i < m_numThreads; ++i )
			{
				::CloseHandle( m_infos[ i ].m_handle );
			}
			red::Memzero( m_infos, sizeof(m_infos) );
			m_numThreads = 0;
			m_suspended = false;
			m_excludedThreadID = 0;
		}

		struct Info
		{
			HANDLE	m_handle;
			DWORD	m_id;
		};

		Info	m_infos[ MAX_THREADS ];
		Uint32	m_numThreads;

	private:
		ScopedThreadInfo( const ScopedThreadInfo& );
		void operator=( const ScopedThreadInfo& );

	private:
		Uint32 m_excludedThreadID;
		Bool m_suspended;
	};

	Bool GetThreadsInProcess( EConnection conn, ScopedThreadInfo& outInfo, DWORD desiredAccess = THREAD_ALL_ACCESS )
	{
		outInfo.Clear();

		const ConnectionInfo& connInfo = GetThreadConnectionInfo( conn );
		if ( !connInfo.m_hProcess )
		{
			RED_DBG_TRACE( "GetThreadsInProcess: no process" );
			return false;
		}

		const DWORD thisProcID = connInfo.m_processID;
		HANDLE snapshotHandle = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, thisProcID );
		outInfo.m_numThreads = 0;
		if( snapshotHandle == INVALID_HANDLE_VALUE )
		{
			return false;
		}

		THREADENTRY32 threadEntry;
		threadEntry.dwSize = sizeof( THREADENTRY32 );

		if( ::Thread32First( snapshotHandle, &threadEntry ) ) 
		{
			do
			{
				if( threadEntry.th32OwnerProcessID == thisProcID )
				{
					HANDLE hThread = ::OpenThread( desiredAccess, FALSE, threadEntry.th32ThreadID );
					if ( !hThread )
					{
						continue;
					}
					const Uint32 pos = outInfo.m_numThreads++;
					outInfo.m_infos[ pos ].m_handle = hThread;
					outInfo.m_infos[ pos ].m_id = threadEntry.th32ThreadID;
					if ( outInfo.m_numThreads == ScopedThreadInfo::MAX_THREADS )
					{
						break;
					}
				}
			} while( ::Thread32Next( snapshotHandle, &threadEntry ) );
		}
		::CloseHandle( snapshotHandle );

		return true;
	}

	struct DbgHelpLockGuard
	{
		DbgHelpLockGuard()
		{
			LockDbgHelpMutex();
		}

		~DbgHelpLockGuard()
		{
			UnlockDbgHelpMutex();
		}

	private:
		void operator=( const DbgHelpLockGuard& );
		DbgHelpLockGuard( const DbgHelpLockGuard& );
	};

	BOOL CALLBACK SysEnumSourceW( PSOURCEFILEW pSourceFile, void* context )
	{
		RED_DBG_TRACE( "File=%ls", pSourceFile->FileName );
		return TRUE;
	}

#ifdef PIMAGEHLP_CBA_EVENT
#error PIMAGEHLP_CBA_EVENT should not be a macro, possible to the widechar version
#endif

	BOOL CALLBACK NoisySymbolsCallback(
		HANDLE hProcess,
		ULONG ActionCode,
		ULONG64 CallbackData,
		ULONG64 UserContext
	)
	{
		BOOL ret = FALSE;
		switch (ActionCode)
		{
		case CBA_EVENT:
			{
				auto* evt = reinterpret_cast<PIMAGEHLP_CBA_EVENT>(CallbackData);
				RED_DBG_TRACE("%hs", evt->desc);
				ret = TRUE;
			}
			break;
		default:
			break;
		}

		return ret;
	}

	namespace helper
	{
		static Bool IsHexDigit( AnsiChar charToCheck )
		{
			return ( charToCheck >= '0' && charToCheck <= '9' )
				|| ( charToCheck >= 'A' && charToCheck <= 'F' )
				|| ( charToCheck >= 'a' && charToCheck <= 'f' );
		}

		static void FixSymbolInfo( SymbolInfo& symbolInfo, EConnection conn )
		{
			// Remove '?' at the beginning.
			if( symbolInfo.m_nameLength > 0 && symbolInfo.m_name[ 0 ] == '?' )
			{
				--symbolInfo.m_nameLength;
				if( symbolInfo.m_nameLength > 0 )
				{
					red::Memmove( symbolInfo.m_name, symbolInfo.m_name + 1, symbolInfo.m_nameLength );
				}
				symbolInfo.m_name[ symbolInfo.m_nameLength ] = '\0';
			}

			// Replace A0xbaadfood with `anonymous namespace'.
			const AnsiChar c_anonymousNamespaceSymbol[] = "`anonymous namespace'";
			const Uint32 c_anonymousNamespaceSymbolLength = RED_ARRAY_COUNT_U32( c_anonymousNamespaceSymbol ) - 1;
			const AnsiChar c_undecoratedNamespacePrefix[] = "A0x";
			const Uint32 c_undecoratedNamespacePrefixLength = RED_ARRAY_COUNT_U32( c_undecoratedNamespacePrefix ) - 1;
			const Uint32 c_undecoratedNamespaceDigitCount = 8;
			const Uint32 c_undecoratedNamespaceLength = c_undecoratedNamespacePrefixLength + c_undecoratedNamespaceDigitCount;

			AnsiChar* name = symbolInfo.m_name;
			
			while( AnsiChar* undecoratedNamespace = red::Strstr( name, c_undecoratedNamespacePrefix ) )
			{
				bool isFollowedHexDigits = true;
				for( Uint32 i = 0; i < c_undecoratedNamespaceDigitCount; ++i )
				{
					if( !IsHexDigit( undecoratedNamespace[ c_undecoratedNamespacePrefixLength + i ] ) )
					{
						isFollowedHexDigits = false;
						break;
					}
 				}

				if( !isFollowedHexDigits )
				{
					name = undecoratedNamespace + c_undecoratedNamespacePrefixLength;
					continue;
				}

				const Uint32 undecoratedNamespaceBegin = ( Uint32 )red::Distance< Int32 >( symbolInfo.m_name, undecoratedNamespace );
				const Uint32 undecoratedNamespaceEnd = undecoratedNamespaceBegin + c_undecoratedNamespaceLength;

				Uint32 charactersToMove = symbolInfo.m_nameLength - undecoratedNamespaceEnd;
				Uint32 anonymousCharactersToCopy = c_anonymousNamespaceSymbolLength;

				symbolInfo.m_nameLength += c_anonymousNamespaceSymbolLength - c_undecoratedNamespaceLength;
				if( symbolInfo.m_nameLength >= SymbolInfo::MAX_SYMBOL_LEN )
				{
					const Uint32 toManyChars = symbolInfo.m_nameLength - SymbolInfo::MAX_SYMBOL_LEN + 1;
					symbolInfo.m_nameLength = SymbolInfo::MAX_SYMBOL_LEN - 1;
					if( toManyChars < charactersToMove )
					{
						charactersToMove -= toManyChars;
					}
					else
					{
						anonymousCharactersToCopy -= toManyChars - charactersToMove;
						charactersToMove = 0;
					}
				}
				if( charactersToMove > 0 )
				{
					red::Memmove( undecoratedNamespace + c_anonymousNamespaceSymbolLength, undecoratedNamespace + c_undecoratedNamespaceLength, charactersToMove );
				}
				red::Memcpy( undecoratedNamespace, c_anonymousNamespaceSymbol, anonymousCharactersToCopy );
				symbolInfo.m_name[ symbolInfo.m_nameLength ] = '\0';

				name = undecoratedNamespace + anonymousCharactersToCopy;
			}
		}

		static void TraceSymInitSearchPath(HANDLE process)
		{
			char searchPath[2048] = "";
			if (!!::SymGetSearchPath(process, searchPath, RED_ARRAY_COUNT_U32(searchPath)))
			{
				RED_DBG_TRACE("Symbol Search Path: %hs", searchPath);
			}
			else
			{
				RED_DBG_TRACE( "Get Symblol Search Path failed: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
			}
		}

		static void TraceNTSymbolPath()
		{
			char symbolPath[2048] = "";
			const Uint32 pathLen = ::GetEnvironmentVariableA("_NT_SYMBOL_PATH", symbolPath, RED_ARRAY_COUNT_U32(symbolPath) - 1);
			if (pathLen == 0)
			{
				RED_DBG_TRACE( "Get _NT_SYMBOL_PATH failed: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
			}
			else if (pathLen < RED_ARRAY_COUNT_U32(symbolPath))
			{
				RED_DBG_TRACE("_NT_SYMBOL_PATH: %hs", symbolPath);
			}
			else
			{
				RED_DBG_TRACE("_NT_SYMBOL_PATH: symbol path longer (%U) than the buffer (%u)", pathLen, RED_ARRAY_COUNT_U32(symbolPath));
			}
		}

		static void TraceLibraryVersion( const UniChar* dllName, bool loadLibrary = false )
		{
			HMODULE hDllModule = loadLibrary ? LoadLibrary( dllName ) : GetModuleHandle( dllName );

			if ( hDllModule )
			{
				UniChar moduleName[ MAX_PATH ] = {};
				if ( GetModuleFileName( hDllModule, moduleName, RED_ARRAY_COUNT( moduleName ) ) > 0 )
				{
					RED_DBG_TRACE( "%ls file name: %ls", dllName, moduleName );

					DWORD infoHandle;
					DWORD infoSize = GetFileVersionInfoSize( moduleName, &infoHandle );
					if ( infoSize > 0 )
					{
						Uint8* infoData = new Uint8[ infoSize ];
						if ( GetFileVersionInfo( moduleName, infoHandle, infoSize, infoData ) )
						{
							UINT querySize = 0;
							LPBYTE queryBuffer = NULL;
							if ( VerQueryValue( infoData, L"\\", ( VOID FAR* FAR* )&queryBuffer, &querySize ) && querySize > 0 )
							{
								VS_FIXEDFILEINFO* fileInfo = ( VS_FIXEDFILEINFO* )queryBuffer;
								if ( fileInfo->dwSignature == 0xfeef04bd )
								{
									RED_DBG_TRACE( "%ls product version: %u.%u.%u.%u",
										dllName,
										( Uint32 )( ( fileInfo->dwProductVersionMS >> 16 ) & 0xffff ),
										( Uint32 )( ( fileInfo->dwProductVersionMS >> 0 ) & 0xffff ),
										( Uint32 )( ( fileInfo->dwProductVersionLS >> 16 ) & 0xffff ),
										( Uint32 )( ( fileInfo->dwProductVersionLS >> 0 ) & 0xffff )
									);
								}
								else
								{
									RED_DBG_TRACE( "%ls version not found", dllName );
								}
							}
							else
							{
								RED_DBG_TRACE( "Get %ls version information failed", dllName );
							}
						}
						else
						{
							RED_DBG_TRACE( "Get %ls file version info failed: 0x%08X (%hs)", dllName, ::GetLastError(), GetLastErrorMessage() );
						}

						delete [] infoData;
					}
					else
					{
						RED_DBG_TRACE( "Get %ls file version info size failed: 0x%08X (%hs)", dllName, ::GetLastError(), GetLastErrorMessage() );
					}
				}
				else
				{
					RED_DBG_TRACE( "%hs %ls module file name failed: 0x%08X (%hs)", loadLibrary ? "Load" : "Get", dllName, ::GetLastError(), GetLastErrorMessage() );
				}


				if ( !FreeLibrary( hDllModule ) )
				{
					RED_DBG_TRACE( "Free %ls module failed: 0x%08X (%hs)", dllName, ::GetLastError(), GetLastErrorMessage() );
				}
			}
			else
			{
				RED_DBG_TRACE( "Get %ls module failed: 0x%08X (%hs)", dllName, ::GetLastError(), GetLastErrorMessage() );
			}
		}
	}

	static Bool InitializeIfNeeded_NoLock( EConnection conn, Bool forceRefresh = false, Bool deferredLoads = true )
	{
		RED_DBG_TRACE( "[InitializeIfNeeded_NoLock] Start" );
		const ConnectionInfo& connInfo = GetThreadConnectionInfo( conn );
		if ( !connInfo.m_hProcess )
		{
			return false;
		}

		if ( connInfo.m_processID != g_dbgHelpProcessID )
		{
			if ( g_hDbgHelpProcess )
			{
				if ( !::SymCleanup( g_hDbgHelpProcess ) )
				{
					RED_DBG_TRACE( "SymCleanup failed: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
					return false;
				}
				g_isDbgHelpInitialized = false;
			}
			g_dbgHelpProcessID = connInfo.m_processID;
			g_hDbgHelpProcess = connInfo.m_hProcess;
		}

		if ( !g_isDbgHelpInitialized )
		{
			if ( GIsErrorReporter )
			{
				RED_DBG_TRACE( "[InitializeIfNeeded_NoLock] TraceLibraryVersion" );

				helper::TraceLibraryVersion( L"DbgHelp.dll", false );
				helper::TraceLibraryVersion( L"SymSrv.dll", true );
				helper::TraceNTSymbolPath();
			}

			// SYMOPT_DEBUG is super slow, use if needed to actually debug symbol loading
			// Defer loading symbols until actually needed
			DWORD flags = SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_NO_PROMPTS;
			if( deferredLoads )
			{
				flags |= SYMOPT_DEFERRED_LOADS;
			}
			if (GIsErrorReporter)
			{
				flags |= SYMOPT_DEBUG;
			}

			RED_DBG_TRACE( "[InitializeIfNeeded_NoLock] SymSetOptions" );

			::SymSetOptions( flags );

			RED_DBG_TRACE( "[InitializeIfNeeded_NoLock] SymInitialize" );

			//! PLEASE DO NOT ADD USER SEARCH PATHS, IF YOU NEED ADD PDB FOR NEW PROJECT CONTACT WITH BUILD MASTER
			if ( !::SymInitialize( g_hDbgHelpProcess, nullptr, ( conn == eConnection_LocalProcess ) ? FALSE : TRUE ) )
			{
				RED_DBG_TRACE( "SymInitialize failed: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
				g_hDbgHelpProcess = nullptr;
				g_dbgHelpProcessID = 0;
				return false;
			}

			// Comes after SymInitialize since hProcess is documented as the value originally passed to SymInitialize
			if ( GIsErrorReporter )
			{
				RED_DBG_TRACE( "[InitializeIfNeeded_NoLock] TraceSymInitSearchPath" );

				helper::TraceSymInitSearchPath(g_hDbgHelpProcess);
				(void)::SymRegisterCallback64(g_hDbgHelpProcess, NoisySymbolsCallback, 0);
			}

			g_isDbgHelpInitialized = true;
		}
		else if ( forceRefresh )
		{
			RED_DBG_TRACE( "[InitializeIfNeeded_NoLock] SymRefreshModuleList" );

			if ( !::SymRefreshModuleList( g_hDbgHelpProcess ) )
			{
				RED_DBG_TRACE( "SymRefreshModuleList failed: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
				return false;
			}
		}

		RED_DBG_TRACE( "[InitializeIfNeeded_NoLock] End" );

		return true;
	}

	static LONG WINAPI TraceSEH( const char* msg, _EXCEPTION_POINTERS* pe )
	{
		RED_DBG_TRACE( msg );
		RED_DBG_TRACE( "SEH exception code: %08X", pe->ExceptionRecord->ExceptionCode );
		return EXCEPTION_EXECUTE_HANDLER;
	}
}

Bool SetThreadRemoteConnection( Uint32 processID, Bool force /*=false*/ )
{
	if ( processID == prv::g_remoteConnectionInfo.m_processID && !force )
	{
		return true;
	}

	// Doesn't need a lock guard, since TLS
	if ( !CloseThreadRemoteConnection() )
	{
		RED_DBG_TRACE( "Warning: failed to close previous connection!" );
	}
	
	if ( processID == ::GetCurrentProcessId() )
	{
		RED_DBG_TRACE( "SetThreadRemoteConnection: processID %u is current process ID", processID );
		return false;
	}

	const HANDLE hProcess = ::OpenProcess( PROCESS_ALL_ACCESS, FALSE, processID );
	if ( !hProcess )
	{
		RED_DBG_TRACE( "OpenProcess %d failed: 0x%08X (%hs)", processID, ::GetLastError(), GetLastErrorMessage() );
		return false;
	}

	prv::g_remoteConnectionInfo.m_hProcess = hProcess;
	prv::g_remoteConnectionInfo.m_processID = processID;

	return true;
}

Bool CloseThreadRemoteConnection()
{
	const HANDLE hProcess = prv::g_remoteConnectionInfo.m_hProcess;
	red::Memzero( &prv::g_remoteConnectionInfo, sizeof( prv::g_remoteConnectionInfo ) );
	if ( prv::g_remoteConnectionInfo.m_hProcess )
	{
		if ( !::CloseHandle( hProcess ) )
		{
			RED_DBG_TRACE( "CloseHandle failed to close remote connection: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
			return false;
		}
	}
	return true;
}

Bool IsDebuggerAttached( Bool* pOutIsConnectionValid /*= nullptr*/, EConnection conn /*= eConnection_LocalProcess*/ )
{
	Bool nullIsConnectionValid = true;
	Bool& outIsConnectionValid = pOutIsConnectionValid ? *pOutIsConnectionValid : nullIsConnectionValid;
	outIsConnectionValid = true;

	if( conn == eConnection_LocalProcess )
	{
		return ::IsDebuggerPresent() != 0;
	}

	const ConnectionInfo& connInfo = GetThreadConnectionInfo( conn );

	if ( !connInfo.m_hProcess )
	{
		RED_DBG_TRACE( "No remote process set" );
		outIsConnectionValid = false;
		return false;
	}

	BOOL isDebuggerPresent = FALSE;
	if ( !::CheckRemoteDebuggerPresent( connInfo.m_hProcess, &isDebuggerPresent ) )
	{
		RED_DBG_TRACE( "CheckRemoteDebuggerPresent failed: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
		outIsConnectionValid = false;
		return false;
	}

	return isDebuggerPresent != FALSE;
}

Bool IsTraceEnabled()
{
	return GEnableTrace;
}

STATIC_CHECK_USE_DECL
void Trace( const char* msg, ... )
{
	va_list arglist;
	va_start( arglist, msg );
	VTrace( msg, arglist );
	va_end( arglist );
}

STATIC_CHECK_USE_DECL
void VTrace( const char* msg, va_list arglist )
{
	const Uint32 BUFFER_SIZE = 2048 + 64;
	char buffer[ BUFFER_SIZE ];

	char* buf = buffer;
	Int32 bufSize = BUFFER_SIZE;

	if ( GTraceTimestamp )
	{
		red::DateTime now;
		red::Clock::GetInstance().GetLocalTime( now );

		Int32 writtenChars = red::SNPrintFUnsafe( buf, bufSize, "[%04u.%02u.%02u %02u:%02u:%02u.%03u] ", now.GetYear(), now.GetMonth() + 1, now.GetDay() + 1, now.GetHour(), now.GetMinute(), now.GetSecond(), now.GetMilliSeconds() );
		if ( writtenChars > 0 )
		{
			buf += writtenChars;
			bufSize -= writtenChars;
		}
	}

	const Int32 len = red::VSNPrintF( buf, bufSize, msg, arglist );
	if ( len > 0 && len + 1 < bufSize )
	{
		if ( buf[ len - 1 ] != '\r' && buf[ len - 1 ] != '\n' )
		{
			buf[ len ] = '\n';
			buf[ len + 1 ] = '\0';
		}
	}

	static bool hasConsole = ::GetConsoleWindow() != nullptr;

	if ( ::IsDebuggerPresent() )
	{
		::OutputDebugStringA( buffer );
	}
	else
	{
		if ( ::SIsMainThread() ) // help avoid hangs
		{
			FILE* const outputFile = ( GTraceFile ? GTraceFile : stderr );
			fputs( buffer, outputFile );
		}
	}
}

static Bool InitModuleBaseAddr_LocalVM( Address& inOutAddress )
{
	// stack backtrace that is independent of ASLR: address - moduleBaseAddr
	HMODULE hModule = nullptr;
	if ( ::GetModuleHandleEx( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast< LPCWSTR >( inOutAddress.m_absoluteVirtualAddress ), &hModule ) == 0 )
	{
		RED_DBG_TRACE( "GetModuleHandleEx failed for <0x%016llX>: 0x%08X (%hs)", inOutAddress.m_absoluteVirtualAddress, ::GetLastError(), GetLastErrorMessage() );
		return false;
	}
	inOutAddress.m_moduleBaseAbsoluteVirtualAddress = reinterpret_cast< Uint64 >( hModule );
	return true;
}

static Bool InitModuleBaseAddr_RemoteVM_DbgHelpNoLock( Address& inOutAddress )
{
	const DWORD64 moduleBaseAddress = ::SymGetModuleBase64( g_hDbgHelpProcess, inOutAddress.m_absoluteVirtualAddress );
	if ( moduleBaseAddress == 0 )
	{
		RED_DBG_TRACE( "SymGetModuleBase64 <0x%016llX> failed: 0x%08X (%hs)", inOutAddress.m_absoluteVirtualAddress, ::GetLastError(), GetLastErrorMessage() );
		return false;
	}

	inOutAddress.m_moduleBaseAbsoluteVirtualAddress = moduleBaseAddress;

	return true;
}

RED_NOINLINE
Bool GetStackBackTrace_Profiler( Uint32 numFramesToSkip, StackTrace& outStackTrace )
{
	constexpr Uint32 maxFramesSupported = 62;
	if ( RED_UNLIKELY( numFramesToSkip >= maxFramesSupported ) )
	{
		return false;
	}
	red::Memzero( &outStackTrace, sizeof(outStackTrace ) );
	Uint64 backtrace[maxFramesSupported];
	const Uint32 numFrames = RtlCaptureStackBackTrace( numFramesToSkip + 1, maxFramesSupported - 1 - numFramesToSkip, reinterpret_cast< void** >( backtrace ), nullptr );
	for ( Uint32 i = 0; i < numFrames; ++i  )
	{
		outStackTrace.m_frameAddress[i].m_absoluteVirtualAddress = backtrace[i];
	}
	outStackTrace.m_numFrameAddresses = numFrames;

	return true;
}

Bool ModularizeStackBackTrace( StackTrace& inOutStackTrace )
{
	for ( Uint32 i = 0; i < inOutStackTrace.m_numFrameAddresses; ++i )
	{
		Address& addr = inOutStackTrace.m_frameAddress[ i ];
		if (!InitModuleBaseAddr_LocalVM( addr ))
		{
			RED_DBG_TRACE( "Warning: failed to resolve local module base address for <0x%016llX>", addr.m_absoluteVirtualAddress );
		}
	}
	return true;
}

RED_NOINLINE
Bool GetStackBackTrace_Heavy( void* osThreadHandle, const StackBackTraceControl& control, StackTrace& outStackTrace, EConnection conn /*= eConnection_LocalProcess*/ )
{
	red::Memzero( &outStackTrace, sizeof( outStackTrace ) );

	StackTraceEx stackTraceEx = {};

	if( !GetStackBackTraceEx_HeavyWithInlines( osThreadHandle, control, stackTraceEx, conn ) )
	{
		return false;
	}

	GetStackBackTraceFromEx( outStackTrace, stackTraceEx );
	return true;
}

RED_NOINLINE
Bool GetStackBackTraceEx_HeavyWithInlines( void* osThreadHandle, const StackBackTraceControl& control, StackTraceEx& outStackTrace, EConnection conn /*= eConnection_LocalProcess*/)
{
	red::Memzero( &outStackTrace, sizeof( outStackTrace ) );

	RED_DBG_TRACE( "[GetStackBackTraceEx_HeavyWithInlines] Start" );

	// Mutex is also being used here to guard against thread suspensions; obvious this isn't fool proof if threads are being suspended somewhere else
	// that doesn't use this mutex or if in some Win32 function that locks. So it's always safer to suspend a remote process' threads.
	DbgHelpLockGuard lockGuard;
	InitializeIfNeeded_NoLock( conn );
	
	CONTEXT context;
	red::Memzero( &context, sizeof(context) );
	context.ContextFlags = CONTEXT_FULL;
	const HANDLE hThread = osThreadHandle ? reinterpret_cast< HANDLE >( osThreadHandle ) : GetCurrentThread();
	const DWORD tid = ::GetThreadId( hThread );
	Bool isCurrentThread = false;
	if ( tid == 0 )
	{
		RED_DBG_TRACE( "Failed to get thread ID: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
		return false;
	}
	else if ( tid == ::GetCurrentThreadId() )
	{
		isCurrentThread = true;
	}

	if ( !isCurrentThread )
	{
		RED_DBG_TRACE( "[GetStackBackTraceEx_HeavyWithInlines] SuspendThread" );

		const DWORD suspendCount = ::SuspendThread( hThread );
		if ( suspendCount == static_cast< DWORD >( -1 ) )
		{
			RED_DBG_TRACE("Failed to suspend thread <%u>: 0x%08X (%hs)", tid, ::GetLastError(), GetLastErrorMessage() );
			return false;
		}
	}

	if( control.m_type == eStackBackTraceControlType_Context )
	{
		// Copy context, so if modified won't possibly affect the execution when returning (e.g., SEH uses the context to resume execution)
		context = *reinterpret_cast< PCONTEXT >( control.m_context );
	}
	else
	{
		if ( isCurrentThread )
		{
			::RtlCaptureContext( &context );
		}
		else
		{
			if ( !::GetThreadContext( hThread, &context ) )
			{
				RED_DBG_TRACE( "Failed to get thread context for <%u>: 0x%08X (%hs)", tid, ::GetLastError(), GetLastErrorMessage() );
				::ResumeThread( hThread );
				return false;
			}
		}
	}

	// Current call frame for initialization.
	// Skipped in the returned callstack because StackWalkEx updates it.
	STACKFRAME_EX stackFrame = {};
	stackFrame.AddrPC.Mode			= AddrModeFlat;
	stackFrame.AddrPC.Offset		= context.Rip;
	stackFrame.AddrReturn.Mode		= AddrModeFlat;
	stackFrame.AddrReturn.Offset 	= context.Rip;
	stackFrame.AddrFrame.Mode		= AddrModeFlat;
	stackFrame.AddrFrame.Offset		= context.Rbp;
	stackFrame.AddrStack.Mode		= AddrModeFlat;
	stackFrame.AddrStack.Offset		= context.Rsp;
	stackFrame.StackFrameSize		= sizeof( STACKFRAME_EX );
	stackFrame.InlineFrameContext	= INLINE_FRAME_CONTEXT_INIT;

	Uint64 unwindToAddress = 0;
	Bool foundUnwindAddress = true;
	if ( control.m_type == eStackBackTraceControlType_Unwind )
	{
		unwindToAddress = control.m_unwindToAbsoluteVirtualAddress;
		foundUnwindAddress = false;
	}
	const Uint32 numAdditionalFramesToSkip = control.m_numAdditionalFramesToSkip;
	Uint32 unwindStartIndex = 0;
	Uint32 numRecordedFrames = 0;
	Uint32 frameIndex = 0;
	Uint64 prevAddresFrame = 0;

	RED_DBG_TRACE( "[GetStackBackTraceEx_HeavyWithInlines] StackWalkEx" );

	for( Uint32 i = 0; i < MAX_STACK_TRACE_FRAMES; ++i )
	{
		Bool ok = StackWalkEx( IMAGE_FILE_MACHINE_AMD64, g_hDbgHelpProcess, hThread, &stackFrame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr, SYM_STKWALK_DEFAULT ) != FALSE;
		if ( !ok )
		{
			RED_DBG_TRACE( "StackWalkEx failed for %u step: 0x%08X (%hs)", i, ::GetLastError(), GetLastErrorMessage() );
			break;
		}

		if ( i > 0 && stackFrame.AddrPC.Offset != prevAddresFrame )
		{
			++frameIndex;
		}
		prevAddresFrame = stackFrame.AddrPC.Offset;

		if ( !foundUnwindAddress )
		{
			if ( stackFrame.AddrPC.Offset == unwindToAddress )
			{
				foundUnwindAddress = true;
				unwindStartIndex = frameIndex;
			}
			else
			{
				continue;
			}
		}

		if ( ( frameIndex - unwindStartIndex ) < numAdditionalFramesToSkip )
		{
			continue;
		}

		AddressEx& addr = outStackTrace.m_frameAddress[ numRecordedFrames++ ];
		addr.m_absoluteVirtualAddress = stackFrame.AddrPC.Offset;
		addr.m_inlineFrameContext = stackFrame.InlineFrameContext;
	}

	RED_DBG_TRACE( "[GetStackBackTraceEx_HeavyWithInlines] InitModuleBaseAddr" );

	if ( conn == eConnection_LocalProcess )
	{
		for ( Uint32 i = 0; i < numRecordedFrames; ++i )
		{
			Address& addr = outStackTrace.m_frameAddress[ i ];
			if ( !InitModuleBaseAddr_LocalVM( addr ) )
			{
				RED_DBG_TRACE( "Warning: failed to resolve local module base address for <0x%016llX>", addr.m_absoluteVirtualAddress );
			}
		}
	}
	else
	{
		for ( Uint32 i = 0; i < numRecordedFrames; ++i )
		{
			Address& addr = outStackTrace.m_frameAddress[ i ];
			if ( !InitModuleBaseAddr_RemoteVM_DbgHelpNoLock( addr ) )
			{
				RED_DBG_TRACE( "Warning: failed to resolve remote module base address for <0x%016llX>", addr.m_absoluteVirtualAddress );
			}
		}
	}

	outStackTrace.m_numFrameAddresses = numRecordedFrames;

	if ( !isCurrentThread )
	{
		RED_DBG_TRACE( "[GetStackBackTraceEx_HeavyWithInlines] ResumeThread" );

		::ResumeThread( hThread );
	}

	RED_DBG_TRACE( "[GetStackBackTraceEx_HeavyWithInlines] End" );

	return true;
}

Bool GetStackBackTraceFromEx( StackTrace& outStackTrace, const StackTraceEx& inStackTraceEx )
{
	red::Memzero( &outStackTrace, sizeof( outStackTrace ) );

	Uint32 outFrame = 0;
	for ( Uint32 inFrame = 0; inFrame < inStackTraceEx.m_numFrameAddresses; ++inFrame )
	{
		// The consequent inline frame has the same frame address.
		if ( inFrame > 0 && inStackTraceEx.m_frameAddress[ inFrame ].m_absoluteVirtualAddress == inStackTraceEx.m_frameAddress[ inFrame - 1 ].m_absoluteVirtualAddress )
		{
			continue;
		}

		outStackTrace.m_frameAddress[ outFrame ] = inStackTraceEx.m_frameAddress[ inFrame ];
		++outFrame;
	}

	outStackTrace.m_numFrameAddresses = outFrame;
	return true;
}

Bool GetStackBackTrace_Lite( Uint32 numFramesToCapture, Uint32 numFramesToSkip, StackTrace& outStackTrace, Bool resolveModuleBaseAddresses, EConnection conn )
{
	red::Memzero( &outStackTrace, sizeof( outStackTrace ) );

	// Mutex is also being used here to guard against thread suspensions; obvious this isn't fool proof if threads are being suspended somewhere else
	// that doesn't use this mutex or if in some Win32 function that locks. So it's always safer to suspend a remote process' threads.
	DbgHelpLockGuard lockGuard;
	InitializeIfNeeded_NoLock( conn );

	void** frames = reinterpret_cast< void** >( ::alloca( numFramesToCapture * sizeof( void* ) ) );

	const WORD framesCaptured = ::RtlCaptureStackBackTrace( numFramesToSkip, numFramesToCapture, frames, nullptr );
	if ( !framesCaptured )
	{
		return false;
	}

	outStackTrace.m_numFrameAddresses = framesCaptured;

	for ( WORD i = 0; i < framesCaptured; ++i )
	{
		Address& address = outStackTrace.m_frameAddress[ i ];
		address.m_absoluteVirtualAddress = reinterpret_cast< Uint64 >( frames[ i ] );
		if ( resolveModuleBaseAddresses && !InitModuleBaseAddr_RemoteVM_DbgHelpNoLock( address ) )
		{
			RED_DBG_TRACE( "Warning: failed to resolve remote module base address for <0x%016llX>", address.m_absoluteVirtualAddress );
		}
	}

	return true;
}

Bool GettModuleBaseAddr_RemoteVM( Address& address, EConnection conn )
{
	// Mutex is also being used here to guard against thread suspensions; obvious this isn't fool proof if threads are being suspended somewhere else
	// that doesn't use this mutex or if in some Win32 function that locks. So it's always safer to suspend a remote process' threads.
	DbgHelpLockGuard lockGuard;
	InitializeIfNeeded_NoLock( conn );

	return InitModuleBaseAddr_RemoteVM_DbgHelpNoLock( address );
}

Bool GetSymbolInfo( const Address& frameAddress, SymbolInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	if ( !frameAddress.m_absoluteVirtualAddress )
	{
		return false;
	}

	RED_DBG_TRACE( "[GetSymbolInfo] Start" );

	char buffer[ sizeof( SYMBOL_INFO ) + SymbolInfo::MAX_SYMBOL_LEN - 1 ]; // The first character in the Name is accounted for in the size of the structure.
	PSYMBOL_INFO pSymbol = reinterpret_cast< PSYMBOL_INFO >( buffer );
	pSymbol->SizeOfStruct = sizeof( SYMBOL_INFO );
	pSymbol->MaxNameLen	= SymbolInfo::MAX_SYMBOL_LEN;
	DWORD64 displacement = 0;
	Bool success = false;
	DbgHelpLockGuard lockGuard;
	InitializeIfNeeded_NoLock( conn );

	if( ::SymFromAddr( g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress, &displacement, pSymbol ) != FALSE )
	{
		success = true;
		const Uint32 nameLen = Min( pSymbol->NameLen, SymbolInfo::MAX_SYMBOL_LEN - 1 );
		red::Memcpy( outInfo.m_name, pSymbol->Name, nameLen );
		outInfo.m_name[ nameLen ] = '\0';
		outInfo.m_nameLength = nameLen;
		outInfo.m_displacement = displacement;

		helper::FixSymbolInfo( outInfo, conn );
	}
	else
	{
		RED_DBG_TRACE( "SymFromAddr (dbgHelpProc=%p) failed for address <0x%llX>: 0x%08X (%hs)", g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress, ::GetLastError(), GetLastErrorMessage() );
	}

	RED_DBG_TRACE( "[GetSymbolInfo] End" );

	return success;
}

Bool GetInlineSymbolInfo( const AddressEx& frameAddress, SymbolInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	if ( !frameAddress.m_absoluteVirtualAddress )
	{
		return false;
	}

	RED_DBG_TRACE( "[GetInlineSymbolInfo] Start" );

	if ( !::SymAddrIncludeInlineTrace( g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress ) )
	{
		return false;
	}

	char buffer[ sizeof( SYMBOL_INFO ) + SymbolInfo::MAX_SYMBOL_LEN - 1 ]; // The first character in the Name is accounted for in the size of the structure.
	PSYMBOL_INFO pSymbol = reinterpret_cast< PSYMBOL_INFO >( buffer );
	pSymbol->SizeOfStruct = sizeof( SYMBOL_INFO );
	pSymbol->MaxNameLen	= SymbolInfo::MAX_SYMBOL_LEN;
	DWORD64 displacement = 0;
	Bool success = false;
	DbgHelpLockGuard lockGuard;
	InitializeIfNeeded_NoLock( conn );

	if( ::SymFromInlineContext( g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress, frameAddress.m_inlineFrameContext, &displacement, pSymbol ) != FALSE )
	{
		success = true;
		const Uint32 nameLen = Min( pSymbol->NameLen, SymbolInfo::MAX_SYMBOL_LEN - 1 );
		red::Memcpy( outInfo.m_name, pSymbol->Name, nameLen );
		outInfo.m_name[ nameLen ] = '\0';
		outInfo.m_nameLength = nameLen;
		outInfo.m_displacement = displacement;

		helper::FixSymbolInfo( outInfo, conn );
	}
	else
	{
		RED_DBG_TRACE( "SymFromInlineContext (dbgHelpProc=%p) failed for address <0x%llX>: 0x%08X (%hs)", g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress, ::GetLastError(), GetLastErrorMessage() );
	}

	RED_DBG_TRACE( "[GetInlineSymbolInfo] End" );

	return success;
}

Bool GetLineInfo( const Address& frameAddress, LineInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	if ( !frameAddress.m_absoluteVirtualAddress )
	{
		return false;
	}

	RED_DBG_TRACE( "[GetLineInfo] Start" );

	IMAGEHLP_LINE64 line;
	line.SizeOfStruct = sizeof( IMAGEHLP_LINE64 );
	DWORD displacement = 0;
	Bool success = false;
	DbgHelpLockGuard lockGuard;
	InitializeIfNeeded_NoLock( conn );

	if ( ::SymGetLineFromAddr64( g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress, &displacement, &line ) != FALSE )
	{
		success = true;
		outInfo.m_lineNumber = line.LineNumber;
		outInfo.m_lineDisplacement = displacement;
		red::Strcpy( outInfo.m_fileName, line.FileName, LineInfo::MAX_FILE_LEN );
	}
	else
	{
		RED_DBG_TRACE( "SymGetLineFromAddr64 (dbgHelpProc=%p) failed for address <0x%llX>: 0x%08X (%hs)", g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress, ::GetLastError(), GetLastErrorMessage() );
	}

	RED_DBG_TRACE( "[GetLineInfo] End" );

	return success;
}

Bool GetInlineLineInfo( const AddressEx& frameAddress, LineInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	if ( !frameAddress.m_absoluteVirtualAddress )
	{
		return false;
	}

	RED_DBG_TRACE( "[GetInlineLineInfo] Start" );

	if( !::SymAddrIncludeInlineTrace( g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress ) )
	{
		return false;
	}

	IMAGEHLP_LINE64 line;
	line.SizeOfStruct = sizeof( IMAGEHLP_LINE64 );
	DWORD displacement = 0;
	Bool success = false;
	DbgHelpLockGuard lockGuard;
	InitializeIfNeeded_NoLock( conn );

	if ( ::SymGetLineFromInlineContext( g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress, frameAddress.m_inlineFrameContext, frameAddress.m_moduleBaseAbsoluteVirtualAddress, &displacement, &line ) != FALSE )
	{
		success = true;
		outInfo.m_lineNumber = line.LineNumber;
		outInfo.m_lineDisplacement = displacement;
		red::Strcpy( outInfo.m_fileName, line.FileName, LineInfo::MAX_FILE_LEN );
	}
	else
	{
		RED_DBG_TRACE( "SymGetLineFromAddr64 (dbgHelpProc=%p) failed for address <0x%llX>: 0x%08X (%hs)", g_hDbgHelpProcess, frameAddress.m_absoluteVirtualAddress, ::GetLastError(), GetLastErrorMessage() );
	}

	RED_DBG_TRACE( "[GetInlineLineInfo] End" );

	return success;
}

Bool GetModuleInfo( const Address& frameAddress, ModuleInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	if ( !frameAddress.m_moduleBaseAbsoluteVirtualAddress )
	{
		return false;
	}

	RED_DBG_TRACE( "[GetModuleInfo] Start" );

	wchar_t buf[ MAX_PATH ] = { L'\0' };
	const wchar_t* moduleName = nullptr;
	Uint32 moduleNameLen = 0;

	__try
	{
		// Note: valid even if the hModule base address is loaded in another process for GetModuleBaseName( hRemoteProcess, hModule, ... )
		HMODULE hModule = reinterpret_cast< HMODULE >( frameAddress.m_moduleBaseAbsoluteVirtualAddress );

		const ConnectionInfo& connInfo = GetThreadConnectionInfo( conn );
		// Wide versions since these ANSI ones internally use HeapAlloc()
		if ( conn == eConnection_RemoteProcess )
		{
			if ( !connInfo.m_hProcess )
			{
				RED_DBG_TRACE( "Missing process" );
				return false;
			}
			Uint32 fullLen = GetModuleBaseNameW( connInfo.m_hProcess, hModule, buf, MAX_PATH );
			if ( fullLen == 0 )
			{
				RED_DBG_TRACE( "GetModuleBaseNameW HMODULE <%016llX> failed: 0x%08X (%hs)", frameAddress.m_moduleBaseAbsoluteVirtualAddress, ::GetLastError(), GetLastErrorMessage() );
				return false;
			}
			if ( fullLen == MAX_PATH  )
			{
				RED_DBG_TRACE( "GetModuleBaseNameW HMODULE <%016llX> truncated name", frameAddress.m_moduleBaseAbsoluteVirtualAddress );
				buf[ MAX_PATH -1 ] = L'\0'; // null terminate just in case; docs are ambiguous about truncation
				--fullLen;
			}
			moduleName = buf;
			moduleNameLen = fullLen;
		}
		else
		{
			const Uint32 fullLen = ::GetModuleFileNameW( hModule, buf, MAX_PATH );
			if ( fullLen == 0 )
			{
				RED_DBG_TRACE( "GetModuleFileNameW HMODULE <%016llX> failed: 0x%08X (%hs)", frameAddress.m_moduleBaseAbsoluteVirtualAddress, ::GetLastError(), GetLastErrorMessage() );
				return false;
			}
			if ( fullLen == MAX_PATH )
			{
				RED_DBG_TRACE( "GetModuleFileNameW HMODULE <%016llX> truncated name", frameAddress.m_moduleBaseAbsoluteVirtualAddress );
			}
			const wchar_t* lastSlash = red::StrchrR( buf, L'\\' );
			moduleName = lastSlash ? lastSlash + 1 : buf;
			moduleNameLen = static_cast< Uint32 >( red::Strlen( moduleName ) );
		}
	}
	__except( TraceSEH( "GetModuleInfo triggered SEH", GetExceptionInformation() ) )
	{
		// Never seen this happen, but just in case module unloaded (got with GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT)
		// and we don't get some nice ERROR_INVALID_ADDRESS instead
		return false;
	}

	red::WideCharToStdChar_NoConv( outInfo.m_name, moduleName, ModuleInfo::MAX_SYMBOL_LEN );

	RED_DBG_TRACE( "[GetModuleInfo] End" );

	return true;
}

#ifdef FINAL_MINIDUMP
struct FinalMinidumpCallbackContext
{
};


static const wchar_t* GetFileNameFromFilePath(const wchar_t* const path)
{
	const size_t moduleFullPathLength = wcslen( path );
	if( !moduleFullPathLength )
	{
		return nullptr;
	}

	for( size_t i = moduleFullPathLength - 1; i >= 0; --i )
	{
		const Bool isSeparator = ( path[ i ] == '/' ) || ( path[ i ] == '\\' );
		if( isSeparator )
		{
			return path + i + 1;
		}
	}
	return path;
}

static const wchar_t* GetFileExtensionFromFilePath( const wchar_t* const path )
{
	const size_t moduleFullPathLength = wcslen( path );
	if( !moduleFullPathLength )
	{
		return nullptr;
	}

	for( size_t i = moduleFullPathLength - 1; i >= 0; --i )
	{
		const Bool isSeparator = ( path[ i ] == '.' );
		if( isSeparator )
		{
			return path + i + 1;
		}
	}
	return nullptr;
}

static const wchar_t* const c_modulesToInclude[] = {
	L"Galaxy64.dll",
	L"oo2ext_7_win64.dll",
	L"PhysX3_x64.dll",
	L"PhysX3CharacterKinematic_x64.dll",
	L"PhysX3Common_x64.dll",
	L"PhysX3Cooking_x64.dll",
	L"PxFoundation_x64.dll",
	L"PxPvdSDK_x64.dll",

#ifdef FORCE_FINAL_MINIDUMP
	// For release builds, mainly for testing dump generation.
	L"PhysX3CommonCHECKED_x64.dll",
	L"PhysX3CharacterKinematicCHECKED_x64.dll",
	L"PhysX3CookingCHECKED_x64.dll",
	L"PxFoundationCHECKED_x64.dll",
	L"PhysX3CHECKED_x64.dll",
	L"PxPvdSDKCHECKED_x64.dll",
#endif // FORCE_FINAL_MINIDUMP
};

static BOOL WINAPI FinalMinidumpCallback_Module( const FinalMinidumpCallbackContext& context, PMINIDUMP_CALLBACK_INPUT callbackInput, PMINIDUMP_CALLBACK_OUTPUT callbackOutput )
{
	const MINIDUMP_MODULE_CALLBACK& moduleInput = callbackInput->Module;
	const wchar_t* const fileName = GetFileNameFromFilePath( moduleInput.FullPath );
	const wchar_t* const extension = GetFileExtensionFromFilePath( fileName );
	RED_DBG_TRACE( "Module file name: %S Extension: %S, BaseOfImage: %p", fileName, extension ? extension : L"<NONE>", moduleInput.BaseOfImage );

	const wchar_t exeExtension[] = L"exe";
	const Bool isExecutable = _wcsnicmp( extension, exeExtension, sizeof( exeExtension ) ) == 0;

	Bool shouldIncludeModule = false;
	for( const wchar_t* const moduleToIncludeName : c_modulesToInclude )
	{
		if( _wcsicmp( fileName, moduleToIncludeName ) == 0 )
		{
			shouldIncludeModule = true;
		}
	}

	const Bool canIncludeDataSegment = isExecutable || shouldIncludeModule;
	if( canIncludeDataSegment )
	{
		callbackOutput->ModuleWriteFlags |= ModuleWriteDataSeg;

		RED_DBG_TRACE( "Module '%S' will include data segment in dump", moduleInput.FullPath );
	}
	else
	{
		RED_DBG_TRACE( "Module '%S' will NOT include data segment in dump", moduleInput.FullPath );
		callbackOutput->ModuleWriteFlags &= ~ModuleWriteDataSeg;
	}

	return TRUE;
}

static BOOL WINAPI FinalMinidumpCallback( PVOID callbackParam, PMINIDUMP_CALLBACK_INPUT callbackInput, PMINIDUMP_CALLBACK_OUTPUT callbackOutput )
{
	RED_DBG_TRACE( "FinalMinidumpCallback | type %u", callbackInput->CallbackType );

	const FinalMinidumpCallbackContext& context = *static_cast<FinalMinidumpCallbackContext* >( callbackParam );

	switch( callbackInput->CallbackType )
	{
	case ModuleCallback:
		return FinalMinidumpCallback_Module( context, callbackInput, callbackOutput );

	}
	return TRUE;
}

static const Uint32 c_wipeOutName = 'x';
static const Uint32 c_minNameLength = 4;

static const Uint32 c_wipeOutEnv = '-';
static const Uint32 c_maxEnvVariableBuffer = 2048;
static const Uint32 c_minEnvKeyLength = 2;
static const Uint32 c_minEnvValLength = 6;
static const Uint32 c_minEnvLength = 12;

static void MemsetUniChar( void* buffer, Uint32 value, Uint32 size )
{
	Uint8* byteBuffer = reinterpret_cast< Uint8* >( buffer );

	for( Uint32 i = 0; i < size; ++i )
	{
		byteBuffer[ i ] = ( Uint8 )( value >> ( ( i % 2 ) * 8 ) );
	}
}

static Bool WipeOutBytesFromMemory( AnsiChar* fileBuffer, Uint32 fileSizeBytes, const AnsiChar* bytes, Uint32 bytesLength )
{
	if( bytesLength > fileSizeBytes )
	{
		return false;
	}

	Bool isChanged = false;

	for( Uint32 start = 0; start <= fileSizeBytes - bytesLength; ++start )
	{
		if( red::Memcmp( fileBuffer + start, bytes, bytesLength ) == 0 )
		{
			red::Memset( fileBuffer + start, c_wipeOutName, bytesLength );
			isChanged = true;
		}
	}

	return isChanged;
}

static Bool WipeOutBytesFromMemoryByUniChar( AnsiChar* fileBuffer, Uint32 fileSizeBytes, const AnsiChar* bytes, Uint32 bytesLength )
{
	if( bytesLength > fileSizeBytes )
	{
		return false;
	}

	Bool isChanged = false;

	for( Uint32 start = 0; start <= fileSizeBytes - bytesLength; ++start )
	{
		if( red::Memcmp( fileBuffer + start, bytes, bytesLength ) == 0 )
		{
			MemsetUniChar( fileBuffer + start, c_wipeOutName, bytesLength );
			isChanged = true;
		}
	}

	return isChanged;
}

static Bool WipeOutUserDataFromMemory( AnsiChar* fileBuffer, Uint32 fileSizeBytes )
{
	Bool isChanged = false;

	{
		AnsiChar userNameA[ UNLEN + 1 ] = {};
		DWORD userNameALength = RED_ARRAY_COUNT_U32( userNameA );
		if( GetUserNameA( userNameA, &userNameALength ) && userNameALength > c_minNameLength )
		{
			isChanged |= WipeOutBytesFromMemory( fileBuffer, fileSizeBytes, userNameA, userNameALength - 1 );
		}
	}

	{
		UniChar userNameW[ UNLEN + 1 ] = {};
		DWORD userNameWLength = RED_ARRAY_COUNT_U32( userNameW );
		if( GetUserNameW( userNameW, &userNameWLength ) && userNameWLength > c_minNameLength )
		{
			isChanged |= WipeOutBytesFromMemoryByUniChar( fileBuffer, fileSizeBytes, reinterpret_cast< AnsiChar* >( userNameW ), ( userNameWLength - 1 ) * sizeof( UniChar ) );
		}
	}

	{
		AnsiChar computerNameA[ MAX_COMPUTERNAME_LENGTH + 1 ] = {};
		DWORD computerNameALength = RED_ARRAY_COUNT_U32( computerNameA );
		if( GetComputerNameA( computerNameA, &computerNameALength ) && computerNameALength >= c_minNameLength )
		{
			isChanged |= WipeOutBytesFromMemory( fileBuffer, fileSizeBytes, computerNameA, computerNameALength );
		}
	}

	{
		UniChar computerNameW[ MAX_COMPUTERNAME_LENGTH + 1 ] = {};
		DWORD computerNameWLength = RED_ARRAY_COUNT_U32( computerNameW );
		if( GetComputerNameW( computerNameW, &computerNameWLength ) && computerNameWLength >= c_minNameLength )
		{
			isChanged |= WipeOutBytesFromMemoryByUniChar( fileBuffer, fileSizeBytes, reinterpret_cast< AnsiChar* >( computerNameW ), computerNameWLength * sizeof( UniChar ) );
		}
	}

	return isChanged;
}

static Uint32 FindEnvVariable( const AnsiChar* buffer, Uint32 bufferLength )
{
	if( bufferLength < c_minEnvLength )
	{
		return 0;
	}

	// KEY
	Uint32 idx = 0;
	for( ; idx < bufferLength; ++idx )
	{
		const AnsiChar c = buffer[ idx ];
		const Bool isLetter = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' );
		const Bool isNumber = ( c >= '0' && c <= '9' );
		const Bool isSpecial = ( c == '_' || c == '(' || c == ')' || c == ':' );
		if( !isLetter && !isNumber && !isSpecial )
		{
			break;
		}
	}

	if( idx < c_minEnvKeyLength )
	{
		return 0;
	}
	if( idx == bufferLength )
	{
		return 0;
	}

	// =
	if( buffer[ idx ] != '=' )
	{
		return 0;
	}
	++idx;

	// VALUE
	const Uint32 valIdx = idx;
	for( ; idx < bufferLength; ++idx )
	{
		const AnsiChar c = buffer[ idx ];
		constexpr AnsiChar c_asciiSpace = 32;
		constexpr AnsiChar c_asciiDel = 127;
		const Bool isPrintableAsciiCharacter = ( c >= c_asciiSpace ) && ( c < c_asciiDel );
		if( !isPrintableAsciiCharacter )
		{
			break;
		}
	}

	if( idx < c_minEnvLength )
	{
		return 0;
	}
	if( idx - valIdx < c_minEnvValLength )
	{
		return 0;
	}
	if( idx == bufferLength )
	{
		return 0;
	}
	if( buffer[ idx ] != ( AnsiChar )0 ) //< NULL terminated
	{
		return 0;
	}

	return idx;
}

static Uint32 FindEnvVariableUniChar( const UniChar* buffer, Uint32 bufferLength )
{
	if( bufferLength < c_minEnvLength )
	{
		return 0;
	}

	// KEY
	Uint32 idx = 0;
	for( ; idx < bufferLength; ++idx )
	{
		const UniChar c = buffer[ idx ];
		const Bool isLetter = ( c >= L'a' && c <= L'z' ) || ( c >= L'A' && c <= L'Z' );
		const Bool isNumber = ( c >= L'0' && c <= L'9' );
		const Bool isSpecial = ( c == L'_' || c == L'(' || c == L')' || c == L':' );
		if( !isLetter && !isNumber && !isSpecial )
		{
			break;
		}
	}

	if( idx < c_minEnvKeyLength )
	{
		return 0;
	}
	if( idx == bufferLength )
	{
		return 0;
	}

	// =
	if( buffer[ idx ] != L'=' )
	{
		return 0;
	}
	++idx;

	// VALUE
	const Uint32 valIdx = idx;
	for( ; idx < bufferLength; ++idx )
	{
		const UniChar c = buffer[ idx ];
		constexpr UniChar c_asciiSpace = 32;
		constexpr UniChar c_asciiDel = 127;
		const Bool isPrintableAsciiCharacter = ( c >= c_asciiSpace ) && ( c < c_asciiDel );
		if( !isPrintableAsciiCharacter )
		{
			break;
		}
	}

	if( idx < c_minEnvLength )
	{
		return 0;
	}
	if( idx - valIdx < c_minEnvValLength )
	{
		return 0;
	}
	if( idx == bufferLength )
	{
		return 0;
	}

	return idx;
}

static Bool WipeOutEnvironmentDataFromMemory( AnsiChar* fileBuffer, Uint32 fileSizeBytes )
{
	Bool isChanged = false;

	for( Uint32 start = 0; start < fileSizeBytes; ++start )
	{
		Uint32 envVarLength = FindEnvVariable( fileBuffer + start, Min( fileSizeBytes - start, c_maxEnvVariableBuffer + 1 ) );
		if( envVarLength > 0 )
		{
			red::Memset( fileBuffer + start, c_wipeOutEnv, envVarLength );
			isChanged = true;
		}
	}

	for( Uint32 start = 0; start < fileSizeBytes; ++start )
	{
		Uint32 envVarLength = FindEnvVariableUniChar( reinterpret_cast< UniChar* >( fileBuffer + start ), Min( ( fileSizeBytes - start ) / 2, c_maxEnvVariableBuffer + 1 ) );
		if( envVarLength > 0 )
		{
			MemsetUniChar( fileBuffer + start, c_wipeOutEnv, envVarLength * 2 );
			isChanged = true;
		}
	}

	return isChanged;
}

static void WipeOutUserDataFromDumpFile( HANDLE hMiniDumpFile )
{
	DWORD fileSizeBytes = GetFileSize( hMiniDumpFile, nullptr );
	if( fileSizeBytes == 0 )
	{
		return;
	}

	AnsiChar* fileBuffer = new ( std::nothrow )  AnsiChar[ fileSizeBytes ];
	if( !fileBuffer )
	{
		return;
	}

	SetFilePointer( hMiniDumpFile, 0, nullptr, FILE_BEGIN );

	DWORD readSizeBytes = 0;
	if( !ReadFile( hMiniDumpFile, fileBuffer, fileSizeBytes, &readSizeBytes, nullptr ) || readSizeBytes != fileSizeBytes )
	{
		delete [] fileBuffer;
		return;
	}

	Bool isChanged = false;
	isChanged |= WipeOutUserDataFromMemory( fileBuffer, fileSizeBytes );
	isChanged |= WipeOutEnvironmentDataFromMemory( fileBuffer, fileSizeBytes );

	if( isChanged )
	{
		SetFilePointer( hMiniDumpFile, 0, nullptr, FILE_BEGIN );

		DWORD writtenSizeBytes = 0;
		WriteFile( hMiniDumpFile, fileBuffer, fileSizeBytes, &writtenSizeBytes, nullptr );
	}

	delete [] fileBuffer;
}

#endif


Bool WriteMiniDumpFile( const FileInfo& fileName, const MiniDumpArgs& args, EConnection conn /*= eConnection_LocalProcess*/ )
{
	RED_DBG_TRACE( "[WriteMiniDumpFile] Start" );

	// Also guarding thread suspension; otherwise potential deadlock
	DbgHelpLockGuard lockGuard;
	InitializeIfNeeded_NoLock( conn );

	const DWORD thisTID = args.m_threadID != 0 ? args.m_threadID : ::GetCurrentThreadId();
	const DWORD thisProcID = g_dbgHelpProcessID;
	HANDLE hProcess = g_hDbgHelpProcess;
	if ( !hProcess )
	{
		RED_DBG_TRACE( "No process");
		return false;
	}

	RED_DBG_TRACE( "Process handle: %p", hProcess );

	// Suspend other threads in our process to get a more consistent dump snapshot
	// Unfortunately then loses actual suspension info.
	ScopedThreadInfo threadInfo;
	if ( !GetThreadsInProcess( conn, threadInfo, THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME ) )
	{
		RED_DBG_TRACE( "Failed to get threads in process. Minidump without suspending other threads." );
	}

	RED_DBG_TRACE( "[WriteMiniDumpFile] SuspendThreads" );

	threadInfo.SuspendThreads( conn );

	RED_DBG_TRACE( "[WriteMiniDumpFile] CreateFileW" );

	HANDLE hMiniDumpFile = CreateFileW(fileName.m_fileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	if ( hMiniDumpFile == INVALID_HANDLE_VALUE )
	{
		RED_DBG_TRACE( "Failed to open %ls for writing", fileName.m_fileName );
		return false;
	}

	MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
	if( args.m_context )
	{
		exceptionInfo.ThreadId = thisTID;
		exceptionInfo.ExceptionPointers = static_cast< _EXCEPTION_POINTERS* >( const_cast< void* >( args.m_context ) );
		exceptionInfo.ClientPointers = conn == eConnection_LocalProcess ? FALSE : TRUE;
	}

	const MINIDUMP_TYPE miniDumpType = static_cast< MINIDUMP_TYPE >(
		MiniDumpWithIndirectlyReferencedMemory | // Get used dynamic memory.
		MiniDumpWithDataSegs | // Get globals.
		MiniDumpWithThreadInfo |
#if defined( FINAL_MINIDUMP )
		// In Final build we don't want to send paths and other GDPR sensitive data
		MiniDumpFilterModulePaths | // Get rid of modules paths.
#endif
		MiniDumpWithProcessThreadData | // Get TLS info.
		MiniDumpIgnoreInaccessibleMemory | // To ignore the memory read failures.
		MiniDumpWithAvxXStateContext ); // To adds AVX register information.

	MINIDUMP_CALLBACK_INFORMATION* minidumpCallbackInformationPtr = nullptr;
#ifdef FINAL_MINIDUMP
	FinalMinidumpCallbackContext context = {};

	MINIDUMP_CALLBACK_INFORMATION minidumpCallbackInformation;

	minidumpCallbackInformation.CallbackRoutine = FinalMinidumpCallback;
	minidumpCallbackInformation.CallbackParam = &context;

	minidumpCallbackInformationPtr = &minidumpCallbackInformation;
#endif

	RED_DBG_TRACE( "[WriteMiniDumpFile] MiniDumpWriteDump" );

	if ( !::MiniDumpWriteDump( hProcess, thisProcID, hMiniDumpFile, miniDumpType, (args.m_context ? &exceptionInfo : nullptr), nullptr, minidumpCallbackInformationPtr ) )
	{
		RED_DBG_TRACE( "Failed to write minidump file: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
		return false;
	}

	RED_DBG_TRACE( "[WriteMiniDumpFile] FlushFileBuffers+CloseHandle" );

#ifdef FINAL_MINIDUMP
	WipeOutUserDataFromDumpFile( hMiniDumpFile );
#endif

	::FlushFileBuffers( hMiniDumpFile );
	::CloseHandle( hMiniDumpFile );

	RED_DBG_TRACE( "[WriteMiniDumpFile] End" );

	return true;
}

enum class BasicType : DWORD // From cvconst.h in DIA SDK 
{
	btNoType	= 0,
	btVoid		= 1,
	btChar		= 2,
	btWChar		= 3,
	btInt		= 6,
	btUInt		= 7,
	btFloat		= 8,
	btBCD		= 9,
	btBool		= 10,
	btLong		= 13,
	btULong		= 14,
	btCurrency	= 25,
	btDate		= 26,
	btVariant	= 27,
	btComplex	= 28,
	btBit		= 29,
	btBSTR		= 30,
	btHresult	= 31
};

void PsymDumpBasicType( const ULONG64 modBase, const ULONG index, char outResult[ 512 ] )
{
	DWORD baseType = 0;
	SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_BASETYPE, &baseType );

	ULONG64 length = 0;
	SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_LENGTH, &length );

	char result[ 512 ] = { 0 };

	switch( static_cast< BasicType >( baseType ) )
	{
	case BasicType::btVoid: strcpy_s( result, "void" ); break;
	case BasicType::btChar: strcpy_s( result, "char" ); break;
	case BasicType::btWChar: strcpy_s( result, "wchar_t" ); break;
	case BasicType::btInt: strcpy_s( result, "int" ); break;
	case BasicType::btUInt: strcpy_s( result, "unsigned int" ); break;
	case BasicType::btFloat:
	{
		if( length == 4 ) { strcpy_s( result, "float" ); }
		else if( length == 8 ) { strcpy_s( result, "double" ); }
	} break;
	case BasicType::btBCD: strcpy_s( result, "BCD" ); break;
	case BasicType::btBool: strcpy_s( result, "bool" ); break;
	case BasicType::btLong: strcpy_s( result, "long" ); break;
	case BasicType::btULong: strcpy_s( result, "unsigned long" ); break;
	case BasicType::btCurrency: strcpy_s( result, "Currency" ); break;
	case BasicType::btDate: strcpy_s( result, "Date" ); break;
	case BasicType::btVariant: strcpy_s( result, "Variant" ); break;
	case BasicType::btComplex: strcpy_s( result, "Complex" ); break;
	case BasicType::btBit: strcpy_s( result, "Bit" ); break;
	case BasicType::btBSTR: strcpy_s( result, "BSTR" ); break;
	case BasicType::btHresult: strcpy_s( result, "HRESULT" ); break;
	}

	memcpy( outResult, result, sizeof( result ) );
}

void PsymDumpPointerType( const ULONG64 modBase, const ULONG index )
{
	DWORD typeIndex = 0;
	SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_TYPEID, &typeIndex );
}

bool PsymDumpData( const ULONG64 modBase, const ULONG index, ClassInfo::MemberInfo& memberInfo );
bool PsymDumpBaseClass( const ULONG64 modBase, const ULONG index, ClassInfo::BaseClassInfo& baseClass );

void PsymDumpClass( const ULONG64 modBase, const ULONG index, ClassInfo& classInfo )
{
	DWORD numChildren = 0;
	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_CHILDRENCOUNT, &numChildren ) )
	{
		return;
	}

	const size_t findChildrenSize = sizeof( TI_FINDCHILDREN_PARAMS ) + ( numChildren * sizeof( ULONG ) );
	std::unique_ptr< char[] > fcpBuffer( new char[ findChildrenSize ] );
	TI_FINDCHILDREN_PARAMS* const fcp = reinterpret_cast< TI_FINDCHILDREN_PARAMS* >( fcpBuffer.get() );
	memset( fcp, 0, findChildrenSize );

	fcp->Count = numChildren;

	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_FINDCHILDREN, fcp ) )
	{
		return;
	}

	classInfo.m_membersCount = 0;
	classInfo.m_baseClassesCount = 0;

	for( DWORD i = 0; i < numChildren; ++i )
	{
		DWORD tag = SymTagNull;
		if( !SymGetTypeInfo( GetCurrentProcess(), modBase, fcp->ChildId[ i ], TI_GET_SYMTAG, &tag ) )
		{
			return;
		}

		if( tag == SymTagData )
		{
			if( classInfo.m_membersCount < ClassInfo::MAX_MEMBERS_COUNT && PsymDumpData( modBase, fcp->ChildId[ i ], classInfo.m_members[ classInfo.m_membersCount ] ) )
			{
				++classInfo.m_membersCount;
			}
		}

		if( tag == SymTagBaseClass )
		{
			if( classInfo.m_baseClassesCount < ClassInfo::MAX_BASE_CLASSES_COUNT && PsymDumpBaseClass( modBase, fcp->ChildId[ i ], classInfo.m_baseClasses[ classInfo.m_baseClassesCount ] ) )
			{
				++classInfo.m_baseClassesCount;
			}
		}
	}
}

void PsymDumpUnion( const ULONG64 modBase, const ULONG index )
{
}

enum class UdtKind : DWORD // From cvconst.h in DIA SDK. User-defined type kind
{
	Struct = 0,
	Class,
	Union
};

void PsymDumpUDT( const ULONG64 modBase, const ULONG index, ClassInfo& classInfo )
{
	DWORD udtKind = 0; // user-defined type kind
	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_UDTKIND, &udtKind ) )
	{
		return;
	}

	switch( static_cast< UdtKind >( udtKind ) )
	{
	case UdtKind::Struct:
		PsymDumpClass( modBase, index, classInfo );
		break;

	case UdtKind::Class:
		PsymDumpClass( modBase, index, classInfo );
		break;

	case UdtKind::Union: // TODO
		PsymDumpUnion( modBase, index );
		break;
	}
}

bool PsymGetSymbolName( const ULONG64 modBase, const ULONG index, char result[ 512 ] )
{
	wchar_t name[ 512 ] = { 0 };

	{
		WCHAR *namePointer = nullptr;
		if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_SYMNAME, &namePointer ) )
		{
			return false;
		}
		wcscpy_s( name, namePointer );
		LocalFree( namePointer );
	}

	Uint32 currentIndex = 0;
	for( const wchar_t* ch = &name[ 0 ]; *ch; ++ch )
	{
		result[ currentIndex++ ] = static_cast< char >( *ch );
	}
	result[ currentIndex++ ] = '\0';

	return true;
}

Bool PsymCheckTag( const ULONG64 modBase, const ULONG index, const DWORD tagToCheck )
{
	DWORD tag = SymTagNull;
	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_SYMTAG, &tag ) )
	{
		return false;
	}
	return tag == tagToCheck;
}

bool PsymGetTypeInformation( const ULONG64 modBase, const ULONG index, TypeInfo& typeInfo )
{
	DWORD tag = SymTagNull;
	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_SYMTAG, &tag ) )
	{
		return false;
	}

	ULONG64 length = 0;
	SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_LENGTH, &length );

	typeInfo.m_size = static_cast< Uint32 >( length );

	if( tag == SymTagBaseType )
	{
		typeInfo.m_primitiveType = true;
		PsymDumpBasicType( modBase, index, typeInfo.m_name );
	}
	else if( tag == SymTagUDT )
	{
		PsymGetSymbolName( modBase, index, typeInfo.m_name );
	}
	else if( tag == SymTagPointerType )
	{
		typeInfo.m_pointerCount = 0;
		DWORD pointedType = 0;

		DWORD currIndex = index;
		while( PsymCheckTag( modBase, currIndex, SymTagPointerType ) )
		{
			if( !SymGetTypeInfo( GetCurrentProcess(), modBase, currIndex, TI_GET_TYPEID, &pointedType ) )
			{
				return false;
			}

			++typeInfo.m_pointerCount;
			currIndex = pointedType;
		}

		PsymGetTypeInformation( modBase, pointedType, typeInfo );
	}
	else if( tag == SymTagEnum )
	{
		typeInfo.m_enumType = true;
		PsymGetSymbolName( modBase, index, typeInfo.m_name );
	}
	else if( tag == SymTagArrayType )
	{
		typeInfo.m_arrayType = true;

		ULONG64 length = 0;
		SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_LENGTH, &length );
		typeInfo.m_arrayLength = static_cast< Uint32 >( length );
		 
		DWORD elementTypeIndex = index;
		while( PsymCheckTag( modBase, elementTypeIndex, SymTagArrayType ) )
		{
			if( !SymGetTypeInfo( GetCurrentProcess(), modBase, elementTypeIndex, TI_GET_TYPEID, &elementTypeIndex ) )
			{
				return false;
			}
		}

		PsymGetTypeInformation( modBase, elementTypeIndex, typeInfo );
	}

	return true;
}

enum class DataKind : DWORD // From cvconst.h in DIA SDK 
{
	IsUnknown = 0,
	IsLocal,
	IsStaticLocal,
	IsParam,
	IsObjectPtr,
	IsFileStatic,
	IsGlobal,
	IsMember,
	IsStaticMember,
	IsConstant
};

bool PsymDumpData( const ULONG64 modBase, const ULONG index, ClassInfo::MemberInfo& memberInfo )
{
	DWORD typeIndex = 0;
	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_TYPEID, &typeIndex ) )
	{
		return false;
	}

	DWORD dataKind = 0;
	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_DATAKIND, &dataKind ) )
	{
		return false;
	}

	switch( static_cast< DataKind >( dataKind ) )
	{
	case DataKind::IsGlobal:
	case DataKind::IsStaticLocal:
	case DataKind::IsFileStatic:
	case DataKind::IsStaticMember:
		// use TI_GET_ADDRESS to get address
		break;

	case DataKind::IsLocal:
	case DataKind::IsParam:
	case DataKind::IsObjectPtr:
	case DataKind::IsMember:

		ULONG offset = 0;
		if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_OFFSET, &offset ) )
		{
			return false;
		}

		if( static_cast< DataKind >( dataKind ) == DataKind::IsMember )
		{
			PsymGetSymbolName( modBase, index, memberInfo.m_name );
			PsymGetTypeInformation( modBase, typeIndex, memberInfo.m_typeInfo );
			memberInfo.m_offset = static_cast< Uint32 >( offset );
			return true;
		}
	}

	return false;
}

bool PsymDumpBaseClass( const ULONG64 modBase, const ULONG index, ClassInfo::BaseClassInfo& baseClass )
{
	DWORD typeIndex = 0;
	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_TYPEID, &typeIndex ) )
	{
		return false;
	}

	DWORD virtualBase = 0;
	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_VIRTUALBASECLASS, &virtualBase ) )
	{
		return false;
	}

	PsymGetSymbolName( modBase, typeIndex, baseClass.m_name );
	return true;
}

void PsymDumpClassInfo( const ULONG64 modBase, const ULONG index, ClassInfo& classInfo )
{
	DWORD tag = SymTagNull;
	if( !SymGetTypeInfo( GetCurrentProcess(), modBase, index, TI_GET_SYMTAG, &tag ) )
	{
		return;
	}

	if( tag == SymTagUDT )
	{
		PsymDumpUDT( modBase, index, classInfo );
	}
	else
	{
		// TODO: Handle invalid tag
	}
}

void GetClassInformation( const char* const classTypeName, ClassInfo& classInfo )
{
	DbgHelpLockGuard lockGuard;

	if( !classTypeName || *classTypeName == '\0' )
	{
		return;
	}

	InitializeIfNeeded_NoLock( eConnection_LocalProcess, false, false );

	char mask[ 512 ] = { 0 };
	sprintf_s( mask, "*!%s", classTypeName );

	struct UserContextType
	{
		const char* classTypeName = nullptr;
		ClassInfo* classInfo = nullptr;
		Bool m_executed = false;
	};

	using TSymCallback = BOOL( * )( PSYMBOL_INFO, ULONG, PVOID );
	const TSymCallback PsymEnumerateSymbolsCallback = []( PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext ) -> BOOL
	{
		const auto userContext = static_cast< UserContextType* >( UserContext );

		if( userContext->m_executed )
		{
			return FALSE;
		}

		if( !userContext->classInfo )
		{
			return FALSE; // stop enumeration, we miss context
		}

		if( strcmp( pSymInfo->Name, userContext->classTypeName ) != 0 )
		{
			return TRUE; // type name is not valid (continue enumeration)
		}

		PsymDumpClassInfo( pSymInfo->ModBase, pSymInfo->TypeIndex, *userContext->classInfo );
		userContext->m_executed = true;

		return FALSE; // stop enumeration
	};

	UserContextType userContext;
	userContext.classTypeName = classTypeName;
	userContext.classInfo = &classInfo;
	SymEnumTypesByName( GetCurrentProcess(), 0, mask, PsymEnumerateSymbolsCallback, &userContext );
}

void GetTypeInformation( const char* const typeName, TypeInfo& typeInfo )
{
	DbgHelpLockGuard lockGuard;

	if( !typeName || *typeName == '\0' )
	{
		return;
	}

	char mask[ 512 ] = { 0 };
	sprintf_s( mask, "*!%s", typeName );

	struct UserContextType
	{
		const char* typeName = nullptr;
		TypeInfo* typeInfo = nullptr;
		Bool m_executed = false;
	};

	using TSymCallback = BOOL( * )( PSYMBOL_INFO, ULONG, PVOID );
	const TSymCallback PsymEnumerateSymbolsCallback = []( PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext ) -> BOOL
	{
		const auto userContext = static_cast< UserContextType* >( UserContext );

		if( userContext->m_executed )
		{
			return FALSE;
		}

		if( !userContext->typeInfo )
		{
			return FALSE; // stop enumeration, we miss context
		}

		if( strcmp( pSymInfo->Name, userContext->typeName ) != 0 )
		{
			return TRUE; // type name is not valid (continue enumeration)
		}

		PsymGetTypeInformation( pSymInfo->ModBase, pSymInfo->TypeIndex, *userContext->typeInfo );
		userContext->m_executed = true;

		return FALSE; // stop enumeration
	};

	UserContextType userContext;
	userContext.typeName = typeName;
	userContext.typeInfo = &typeInfo;
	SymEnumTypesByName( GetCurrentProcess(), 0, mask, PsymEnumerateSymbolsCallback, &userContext );
}

namespace dbghelper
{
	static BOOL CALLBACK CloseWndEnum( HWND hWnd, LPARAM lParam )
	{
		const DWORD closeID = static_cast< DWORD >( lParam );
		DWORD id = 0;
		::GetWindowThreadProcessId( hWnd, &id ) ;
		if ( id == closeID )
		{
			RED_DBG_TRACE( "Sending close message to hWnd <%p> for processID <%u>", hWnd, id );
			::PostMessage( hWnd, WM_CLOSE, 0, 0 ) ;
		}

		return TRUE;
	}

	// How To Terminate an Application "Cleanly" in Win32
	// https://support.microsoft.com/en-us/kb/178893
	static Bool TryEndProcess( Uint32 processID, Uint32 timeoutMillisec, Bool forceTerminate /*=true*/ )
	{
		Bool ret = false;
		const HANDLE hProcess = ::OpenProcess( SYNCHRONIZE | PROCESS_TERMINATE, FALSE, processID );
		if ( !hProcess )
		{
			RED_DBG_TRACE( "Failed to open process ID <%u>", processID );
			return false;
		}
		::EnumWindows( CloseWndEnum, static_cast< LPARAM >( processID ) );
		const Uint32 waitRet =::WaitForSingleObject( hProcess, timeoutMillisec );
		if ( waitRet == WAIT_OBJECT_0 )
		{
			RED_DBG_TRACE( "Ended process ID <%u>", processID );
			ret = true;
		}
		else
		{
			RED_DBG_TRACE( "WaitForSingleObject returned %u", waitRet );
			if ( forceTerminate && ::TerminateProcess( hProcess, 1 ) )
			{
				RED_DBG_TRACE( "Forcefully ended process ID <%u>", processID );
				ret = true;
			}
			else
			{
				RED_DBG_TRACE( "Failed to end process ID <%u>", processID );
			}
		}
		return ret;
	}

	static Bool GetProcessCaption( char* buffer, Uint32 bufferSize, EConnection conn )
	{
		const ConnectionInfo& connInfo = GetThreadConnectionInfo( conn );

		if ( conn == eConnection_RemoteProcess )
		{
			char processExeAbsolutePath[ MAX_PATH ] = { '\0' };
			DWORD size = RED_ARRAY_COUNT_U32( processExeAbsolutePath ); // input is buffer size, output is strlen (excluding null terminator)
			if ( !::QueryFullProcessImageNameA( connInfo.m_hProcess, 0, processExeAbsolutePath, &size ) )
			{
				RED_DBG_TRACE( "QueryFullProcessImageNameA failed: 0x%08X (%hs)", ::GetLastError(), GetLastErrorMessage() );
				return false;
			}
			const char* lastSlash = red::StrchrR( processExeAbsolutePath, '\\' );
			const char* src = lastSlash ? lastSlash + 1 : processExeAbsolutePath;
			red::Strcpy( buffer, src, bufferSize );
		}
		else
		{
			char processExeAbsolutePath[MAX_PATH];
			const Uint32 len = ::GetModuleFileNameA( nullptr, processExeAbsolutePath, MAX_PATH );
			if ( len == 0 )
			{
				return false;
			}
			const char* lastSlash = red::StrchrR( processExeAbsolutePath, '\\' );
			const char* baseName = lastSlash ? lastSlash + 1 : processExeAbsolutePath;
			red::Strcpy( buffer, baseName, bufferSize );
		}

		const size_t len = red::Strlen( buffer );
		if ( len + 1 < bufferSize )
		{
			red::SNPrintFUnsafe( buffer + len, bufferSize - len, "(%u)", connInfo.m_processID );
		}

		return true;
	}
}

} // namespace dbgutils

#endif

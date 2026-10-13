/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#if !defined( RED_PLATFORM_WINPC ) && !defined( RED_PLATFORM_DURANGO )

RED_NO_EMPTY_FILE();

#else

#ifdef RED_PLATFORM_WINPC
# include <werapi.h>
# pragma comment( lib, "wer.lib" )
#endif

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
#include <signal.h>
#include <crtdbg.h>

#if defined( RED_PLATFORM_WINPC )
#include <TLHELP32.h>
#endif

#include "utility.h"
#include "dbgUtils.h"
#include "errorHandler.h"
#include "errorReporterIPCWin32.h"
#include "errorReporterWin32.h"
#include "errorHandlerImplAttachments.h"
#include "errorHandlerImplMessages.h"
#include "errorHandlerImplCrashDumpData.h"
#include "errorHandlerImplErrorHooksWin32.h"
#include "logger.h"
#include "loggerFileSink.h"
#include "applicationErrorDumper.h"

#ifndef STATUS_POSSIBLE_DEADLOCK
#define STATUS_POSSIBLE_DEADLOCK ((LONG)0xC0000194L)
#endif

#ifndef STATUS_HEAP_CORRUPTION
#define STATUS_HEAP_CORRUPTION ((LONG)0x0000374L)
#endif

#ifndef EXCEPTION_HEAP_CORRUPTION
#define EXCEPTION_HEAP_CORRUPTION STATUS_HEAP_CORRUPTION
#endif

#endif

namespace red
{
	extern red::ThreadId g_loggerThreadId; // errorHandler.cpp

	const char* AccessViolationTypeToString( ULONG_PTR type )
	{
		switch( type )
		{
		case 0: return "The thread attempted to read inaccessible data";
		case 1: return "The thread attempted to write to an inaccessible address";
		case 8: return "The thread caused a user-mode data execution prevention (DEP) violation";
		default:
			break;
		}

		return "<Unknown access violation reason>";
	}

	// Common exceptions and descriptions from EXCEPTION_RECORD docs
	// https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
	const char* ExceptionCodeToString( Uint32 exceptionCode )
	{
		switch( exceptionCode )
		{
		case EXCEPTION_ACCESS_VIOLATION:			return "EXCEPTION_ACCESS_VIOLATION";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:		return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
		case EXCEPTION_BREAKPOINT:					return "EXCEPTION_BREAKPOINT";
		case EXCEPTION_DATATYPE_MISALIGNMENT:		return "EXCEPTION_DATATYPE_MISALIGNMENT";
		case EXCEPTION_FLT_DENORMAL_OPERAND:		return "EXCEPTION_FLT_DENORMAL_OPERAND";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:			return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
		case EXCEPTION_FLT_INEXACT_RESULT:			return "EXCEPTION_FLT_INEXACT_RESULT";
		case EXCEPTION_FLT_INVALID_OPERATION:		return "EXCEPTION_FLT_INVALID_OPERATION";
		case EXCEPTION_FLT_OVERFLOW:				return "EXCEPTION_FLT_OVERFLOW";
		case EXCEPTION_FLT_STACK_CHECK:				return "EXCEPTION_FLT_STACK_CHECK";
		case EXCEPTION_FLT_UNDERFLOW:				return "EXCEPTION_FLT_UNDERFLOW";
		case EXCEPTION_ILLEGAL_INSTRUCTION:			return "EXCEPTION_ILLEGAL_INSTRUCTION";
		case EXCEPTION_IN_PAGE_ERROR:				return "EXCEPTION_IN_PAGE_ERROR";
		case EXCEPTION_INT_DIVIDE_BY_ZERO:			return "EXCEPTION_INT_DIVIDE_BY_ZERO";
		case EXCEPTION_INT_OVERFLOW:				return "EXCEPTION_INT_OVERFLOW";
		case EXCEPTION_INVALID_DISPOSITION:			return "EXCEPTION_INVALID_DISPOSITION";
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:	return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
		case EXCEPTION_PRIV_INSTRUCTION:			return "EXCEPTION_PRIV_INSTRUCTION";
		case EXCEPTION_SINGLE_STEP:					return "EXCEPTION_SINGLE_STEP";
		case EXCEPTION_STACK_OVERFLOW:				return "EXCEPTION_STACK_OVERFLOW";
		case EXCEPTION_POSSIBLE_DEADLOCK:			return "EXCEPTION_POSSIBLE_DEADLOCK";
		case EXCEPTION_INVALID_HANDLE:				return "EXCEPTION_INVALID_HANDLE";
		case EXCEPTION_GUARD_PAGE:					return "EXCEPTION_GUARD_PAGE";
		case EXCEPTION_HEAP_CORRUPTION:				return "EXCEPTION_HEAP_CORRUPTION";
		default:
			break;
		}

		return "<Unknown exception>";
	}

	// Common exceptions and descriptions from EXCEPTION_RECORD docs
	// https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
	// https://msdn.microsoft.com/en-us/library/windows/desktop/ms679356(v=vs.85).aspx
	const char* ExceptionCodeToDetailsString( Uint32 exceptionCode )
	{
		switch( exceptionCode )
		{
		case EXCEPTION_ACCESS_VIOLATION:			return "The thread tried to read from or write to a virtual address for which it does not have the appropriate access";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:		return "The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking";
		case EXCEPTION_BREAKPOINT:					return "A breakpoint was encountered";
		case EXCEPTION_DATATYPE_MISALIGNMENT:		return "The thread tried to read or write data that is misaligned on hardware that does not provide alignment. For example, 16-bit values must be aligned on 2-byte boundaries; 32-bit values on 4-byte boundaries, and so on";
		case EXCEPTION_FLT_DENORMAL_OPERAND:		return "One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:			return "The thread tried to divide a floating-point value by a floating-point divisor of zero";
		case EXCEPTION_FLT_INEXACT_RESULT:			return "The result of a floating-point operation cannot be represented exactly as a decimal fraction";
		case EXCEPTION_FLT_INVALID_OPERATION:		return "This exception represents any floating-point exception not included in this list";
		case EXCEPTION_FLT_OVERFLOW:				return "The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type";
		case EXCEPTION_FLT_STACK_CHECK:				return "The stack overflowed or underflowed as the result of a floating-point operation";
		case EXCEPTION_FLT_UNDERFLOW:				return "The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type";
		case EXCEPTION_ILLEGAL_INSTRUCTION:			return "The thread tried to execute an invalid instruction";
		case EXCEPTION_IN_PAGE_ERROR:				return "The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network";
		case EXCEPTION_INT_DIVIDE_BY_ZERO:			return "The thread tried to divide an integer value by an integer divisor of zero";
		case EXCEPTION_INT_OVERFLOW:				return "The result of an integer operation caused a carry out of the most significant bit of the result";
		case EXCEPTION_INVALID_DISPOSITION:			return "An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception";
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:	return "The thread tried to continue execution after a noncontinuable exception occurred";
		case EXCEPTION_PRIV_INSTRUCTION:			return "The thread tried to execute an instruction whose operation is not allowed in the current machine mode";
		case EXCEPTION_SINGLE_STEP:					return "A trace trap or other single-instruction mechanism signaled that one instruction has been executed";
		case EXCEPTION_STACK_OVERFLOW:				return "The thread used up its stack.";
		case EXCEPTION_POSSIBLE_DEADLOCK:			return "Possible deadlock condition.";
		case EXCEPTION_INVALID_HANDLE:				return "The thread used a handle to a kernel object that was invalid (probably because it had been closed.)";
		case EXCEPTION_GUARD_PAGE:					return "The thread accessed memory allocated with the PAGE_GUARD modifier.";
		case EXCEPTION_HEAP_CORRUPTION:				return "A heap has been corrupted.";
		default:
			break;
		}
		return "<Unknown additional details>";
	}



// To get the JIT popup if no debugger attached.
// E.g., if the executable is cmdlet.exe:
// HKEY_CURRENT_USER\Software\CD Projekt RED\Red Engine Error Handler\PC\Debug\cmdlet.exe
// DWORD (32) LastChanceDebugBreak = 1
namespace err
{
	static wchar_t gDurangoTempDir[] = L"D:\\REDTempCrashInfo";
	static wchar_t gDebugBreakRegKeyRoot[] = L"Software\\CD Projekt RED\\Red Engine Error Handler\\PC\\Debug\\";
	static wchar_t gLastChanceDebugRegValue[] = L"LastChanceDebugBreak";

	static const Uint32 c_msgBufferSize = 1024;
	
	static char gErrorMessageBuffer[ c_msgBufferSize ];
	static Uint32 gErrorHandlerFlags = eErrorHandlerFlags_Default;
	static const char* gErrorHandlerAppVersion = "<Unknown>";
	static Uint64 gUptimeStartTicks = 0;
	static atomic::TAtomic32 gRegisteredOnce = 0;
	static atomic::TAtomic32 gHandledExceptionOnce = 0;

	static dbgutils::RegisteredAttachmentTable gRegisteredAttachmentTable;
	static_assert( std::is_pod< dbgutils::RegisteredAttachmentTable >::value, "Should be POD to avoid static order init fiasco or move into guarded static init function" );

#ifdef RED_PLATFORM_WINPC
	static wchar_t gWinPCModuleFileName[MAX_PATH] = L"<Unknown.exe>";
#endif

	struct CrashFileInfo
	{
		wchar_t fileName[MAX_PATH];
		HANDLE handle;
	};

	static CrashFileInfo gCrashFile;

	static ErrorHandlerHooks::ScriptCallstackVisitorFunc* gScriptCallstackVisitor = nullptr;

	namespace helper
	{
		// Whether there's some reserved stack space for error handling on this thread
		static thread_local Bool tHasThreadStackGuarantee = false;
		static thread_local Bool tExceptionFilterRecursionCheck = false;

		struct HandleErrorParams
		{
			ErrorMessage customErrMsg;
			_EXCEPTION_POINTERS* exceptionInfo = nullptr;
		};

		static void AbortProcess()
		{
#if defined( RED_PLATFORM_WINPC )
			::TerminateProcess( ::GetCurrentProcess(), 1 );
#elif defined( RED_PLATFORM_DURANGO )
			__fastfail( FAST_FAIL_FATAL_APP_EXIT );
#else
#error Undefined platform!
#endif
		}

		static void InitWinPCModuleFileName()
		{
#ifdef RED_PLATFORM_WINPC
			wchar_t nameBuf[MAX_PATH] = {};
			::GetModuleFileNameW(nullptr, nameBuf, RED_ARRAY_COUNT_U32(nameBuf));
			const wchar_t* moduleFileName = red::StrchrR(nameBuf, L'\\');
			if (moduleFileName == nullptr)
			{
				moduleFileName = nameBuf;
			}
			else
			{
				moduleFileName += 1;
			}

			if ( !moduleFileName || !moduleFileName[0] )
			{
				RED_DBG_TRACE("Failed to get module filename!");
				return;
			}

			red::Strcpy( gWinPCModuleFileName, moduleFileName, RED_ARRAY_COUNT_U32(gWinPCModuleFileName) );
#endif
		}

		static Bool GetLastChanceDebugBreakSetting()
		{
#ifdef RED_PLATFORM_WINPC
			wchar_t regPath[MAX_PATH] = L"";
			red::Strcpy(regPath, gDebugBreakRegKeyRoot, RED_ARRAY_COUNT_U32(regPath));
			red::Strcat(regPath, gWinPCModuleFileName, RED_ARRAY_COUNT_U32(regPath));

			HKEY key = nullptr;
			if (::RegOpenKeyExW(HKEY_CURRENT_USER, regPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
			{
				return false;
			}

			DWORD type = 0;
			DWORD val = 0;
			DWORD size = sizeof(val);
			if ((::RegQueryValueExW(key, gLastChanceDebugRegValue, nullptr, &type, (LPBYTE)&val, &size) != ERROR_SUCCESS) || type != REG_DWORD)
			{
				return false;
			}

			return val != 0;
#else
			return false;
#endif
		}

#ifdef RED_PLATFORM_DURANGO
		static void OutputCrashDebugString()
		{
			// For test utilities as a way to signal an impending crash
			::OutputDebugStringW( L"[TITLE CRASHED]" );
		}

		static void DelayCrashForGameDVR()
		{
			// #tbd: if crashes during title activation and we sleep, then the process could get killed
			// #tbd: check if CL internal possibly.
			// Could make this an option, so not always enabled
			// but try 3 seconds to make a gamedvr capture, without making the user wait too long
			// Experimentally, it takes a little under 2 secconds to capture 10 sec of video
			::Sleep( 3 * 1000 );
		}
#endif

		Bool TrySetThreadStackGuarantee()
		{
#ifdef RED_PLATFORM_WINPC
			// Reserve yet another page during stack overflow. Below this seems to not be enough to call CreateProcess during which time.
			ULONG guarantee = 16 * 1024;
			if ( !::SetThreadStackGuarantee( &guarantee ) )
			{
				RED_DBG_TRACE( "SetThreadStackGuarantee failed: 0x%08X", ::GetLastError() );
				return false;
			}

			const ULONG previousGuarantee = guarantee;
			// Call again to get actual guaranteed size: any previous higher reservation isn't lowered.
			ULONG currentGuarantee = 0;
			( void )::SetThreadStackGuarantee( &currentGuarantee ); // when zero, gets current value and always succeeds
			RED_DBG_TRACE( "stack guarantee (TID=%u) changed from %lu to %lu bytes", ::GetCurrentThreadId(), previousGuarantee, currentGuarantee );
			return true;
#else
			return false;
#endif
		}
		
#ifdef RED_PLATFORM_WINPC
		static dbgutils::win32::IPCContext& GetIPCCrashContextWin32_NoLock()
		{
			// Leak it, so should be usable at any time during atexit
			// Let Win32 close handles when process exits
			static Bool initialized = false;
			RED_ALIGN( 16 ) static char ipcCrashContext[ sizeof( dbgutils::win32::IPCContext ) ];
			static_assert( __alignof( ipcCrashContext ) >= __alignof( dbgutils::win32::IPCContext ), "Bad ipcCrashContext alignment" );
			if ( !initialized )
			{
				// Placement new. !! DO NOT CHANGE TO RED_NEW OR DYNAMIC MEMORY ALLOCATION !!
				::new( ipcCrashContext ) dbgutils::win32::IPCContext;
				initialized = true;
			}

			return *reinterpret_cast<dbgutils::win32::IPCContext*>( ipcCrashContext );
		}
#endif

#ifdef RED_PLATFORM_WINPC
		static void LaunchErrorReporter_NoLock()
		{
			// Checks if already exists, since may have gotten launched by an assert before main
			dbgutils::win32::ConnectErrorReporterIfNeeded_NotReentrant( GetIPCCrashContextWin32_NoLock() );
		}
#endif
	
		class FileDumper
		{
		public:
			explicit FileDumper( HANDLE fileHandle )
				: m_fileHandle( fileHandle )
			{
			}

			~FileDumper()
			{
				if ( m_fileHandle && m_fileHandle != INVALID_HANDLE_VALUE )
				{
					::FlushFileBuffers(m_fileHandle);
				}
			}

			void WriteText( const char* text )
			{
				if ( text && m_fileHandle && m_fileHandle != INVALID_HANDLE_VALUE )
				{
					DWORD numBytesWritten = 0;
					::WriteFile( m_fileHandle, text, (Uint32)red::Strlen( text ), &numBytesWritten, nullptr );
				}
			}

			void WriteTextf(STATIC_CHECK_PRINTF_MSC const char* format, ...)
			{
				if (format && m_fileHandle && m_fileHandle != INVALID_HANDLE_VALUE)
				{
					char buf[1024];
					va_list args;

					va_start(args, format);
					red::VSNPrintF(buf, RED_ARRAY_COUNT_U32(buf), format, args);
					va_end(args);

					DWORD numBytesWritten = 0;
					::WriteFile(m_fileHandle, buf, (Uint32)red::Strlen(buf), &numBytesWritten, nullptr);
				}
			}

			void WriteTextNL( const char* text )
			{
				if ( text && m_fileHandle && m_fileHandle != INVALID_HANDLE_VALUE )
				{
					DWORD numBytesWritten = 0;
					::WriteFile( m_fileHandle, text, (Uint32)red::Strlen( text ), &numBytesWritten, nullptr );
					Newline();
				}
			}

			void Newline()
			{
				if ( m_fileHandle && m_fileHandle != INVALID_HANDLE_VALUE )
				{
					DWORD numBytesWritten = 0;
					const char text[] = "\r\n";
					::WriteFile( m_fileHandle, text, (Uint32)red::Strlen( text ), &numBytesWritten, nullptr );
				}
			}

		private:
			HANDLE m_fileHandle;
		};

		static void WriteErrorReason( FileDumper& dumper, EErrorReason errorReason, const HandleErrorParams& params )
		{
			const char* errorReasonText = red::ErrorReasonText( errorReason );
			dumper.WriteText( "Error Reason: " );
			dumper.WriteTextNL( errorReasonText );

			if( params.customErrMsg.IsValid() )
			{
				char lineBuf[ 16 ] = "";
				red::SNPrintFUnsafe( lineBuf, RED_ARRAY_COUNT_U32( lineBuf ), "%u", params.customErrMsg.m_line );

				dumper.WriteText( "Expression: " );
				dumper.WriteTextNL( params.customErrMsg.m_expression );
				dumper.WriteText( "Message: " );
				dumper.WriteTextNL( params.customErrMsg.m_message );
				dumper.WriteText( "File: " );
				dumper.WriteTextNL( params.customErrMsg.m_file );
				dumper.WriteText( "Line: " );
				dumper.WriteTextNL( lineBuf );
			}
			else if( params.exceptionInfo && params.exceptionInfo->ExceptionRecord )
			{
				const EXCEPTION_RECORD& exceptionRecord = *params.exceptionInfo->ExceptionRecord;
				const Uint32 exceptionCode = exceptionRecord.ExceptionCode;

				dumper.WriteText( "Expression: " );
				dumper.WriteTextf( "%hs (0x%08X)", red::ExceptionCodeToString( exceptionCode ), exceptionCode );
				dumper.Newline();
				dumper.WriteText( "Message: " );
				if( exceptionCode == EXCEPTION_ACCESS_VIOLATION && exceptionRecord.NumberParameters >= 2 )
				{
					const ULONG_PTR type = exceptionRecord.ExceptionInformation[ 0 ];
					const ULONG_PTR exceptionVirtAddr = exceptionRecord.ExceptionInformation[ 1 ];
					dumper.WriteTextf( "%hs at 0x%llX.", red::AccessViolationTypeToString( type ), exceptionVirtAddr );
				}
				else
				{
					dumper.WriteTextf( "%hs.", red::ExceptionCodeToDetailsString( exceptionCode ) );
				}
				dumper.Newline();
				dumper.WriteTextNL( "File: <Unknown>" );
				dumper.WriteTextNL( "Line: 0" );
			}
		}

		static void WriteScriptCallstack( FileDumper& dumper )
		{
			if ( gScriptCallstackVisitor )
			{
				Uint32 currentThreadId = GetCurrentThreadId();
				Bool hasWrittenCallStackHeader = false;

				gScriptCallstackVisitor( [&dumper, &hasWrittenCallStackHeader, currentThreadId]( Uint32 threadId, const red::AnsiChar* buffer, Uint32 bufferSize )
				{
					if ( currentThreadId == threadId )
					{
						if ( !hasWrittenCallStackHeader )
						{
							hasWrittenCallStackHeader = true;
							dumper.WriteText( "----------------------------------------\r\n" );
							char callstackPrefix[ 512 ] = { '\0' };
							red::SNPrintFSafe( callstackPrefix, RED_ARRAY_COUNT( callstackPrefix ), "Script Callstack (threadId=%lu):\r\n", threadId );
							dumper.WriteText( callstackPrefix );
						}

						dumper.WriteTextf( "\tScript Frame: %s\r\n", buffer );
					}
					return true;
				});

				if ( hasWrittenCallStackHeader )
				{
					dumper.WriteText( "----------------------------------------\r\n" );
				}
			}
		}

		static void WriteCrashDumpData(FileDumper& dumper, Uint32 exceptionCode )
		{
 			const Uint64 endTimeTicks = red::ProfileTimer::GetTicks();
 			const Uint64 seconds = red::ProfileTimer::GetSec(endTimeTicks - gUptimeStartTicks);
			const Uint32 tid = ::GetCurrentThreadId();
			const Uint32 pid = ::GetCurrentProcessId( );
			dumper.WriteTextf("\"uptimeSeconds\":%llu\r\n", seconds);
			dumper.WriteTextf("\"stopThreadID\":%u\r\n", tid );
			dumper.WriteTextf("\"exceptionCode\":0x%08X\r\n", exceptionCode );
			dumper.WriteTextf( "\"processID\":%u\r\n", pid );
			

			DumpCrashDataResult dumpResult;
			err::DumpCrashData(&dumper, [](void* thisPtr, const char* formattedName, const char* formattedValue) {
				auto* dumper = static_cast<FileDumper*>(thisPtr);
				dumper->WriteText("\"");
				dumper->WriteText(formattedName);
				dumper->WriteText("\":\"");
				dumper->WriteText(formattedValue);
				dumper->WriteText("\"\r\n");
			}, dumpResult);
			RED_UNUSED(dumpResult);
		}

		static void CreateCrashFile( CrashFileInfo& outInfo )
		{
			outInfo.handle = nullptr;
			outInfo.fileName[0] = '\0';

			SYSTEMTIME localTime;
			Uint64 utcTimestamp = 0;
			{
				SYSTEMTIME systemTime;
				::GetSystemTime(&systemTime);
				FILETIME fileTime;
				::SystemTimeToFileTime(&systemTime, &fileTime);
				ULARGE_INTEGER li;
				red::Memcpy(&li, &fileTime, sizeof(ULARGE_INTEGER));
				utcTimestamp = li.QuadPart;

				// How To Convert from GMT(UTC) Time to Local Time
				// https://support.microsoft.com/en-us/kb/245786
				FILETIME localFileTime;
				::FileTimeToLocalFileTime(&fileTime, &localFileTime);
				::FileTimeToSystemTime(&localFileTime, &localTime);
			}
			wchar_t moduleFullPath[MAX_PATH] = L"";
			::GetModuleFileNameW(NULL, moduleFullPath, MAX_PATH);
			wchar_t* moduleName = red::StrchrR(moduleFullPath, L'\\');
			if (moduleName)
				moduleName += 1;
			else
				moduleName = moduleFullPath;

#ifdef RED_PLATFORM_DURANGO
			const wchar_t* const tempDir = gDurangoTempDir;
			::CreateDirectoryW(gDurangoTempDir, nullptr);
#else
			const wchar_t tempDir[] = L"."; // #tbd: or actual tempdir
#endif

			// #tbd: or short name...
			const Uint32 procID = ::GetCurrentProcessId();
			const Uint32 threadID = ::GetCurrentThreadId();
			const Int32 len = red::SNPrintFUnsafe( outInfo.fileName, RED_ARRAY_COUNT_U32(outInfo.fileName), L"%ls\\%ls-%04d%02d%02d-%02d%02d%02d-%lu-%lu.txt",
				tempDir,
				moduleName,
				localTime.wYear, localTime.wMonth, localTime.wDay,
				localTime.wHour, localTime.wMinute, localTime.wSecond, procID, threadID);
			
			Uint32 flagsAndAttrs = FILE_ATTRIBUTE_NORMAL;
#ifdef RED_PLATFORM_WINPC
			flagsAndAttrs |= FILE_FLAG_DELETE_ON_CLOSE;
#endif

			outInfo.handle = ::CreateFileW(outInfo.fileName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, flagsAndAttrs, nullptr);
			if (outInfo.handle != INVALID_HANDLE_VALUE)
			{
				RegisterAttachment(gRegisteredAttachmentTable, outInfo.fileName);
				DWORD numBytesWritten = 0;
				const char msg[] = "Registered crash info file...\r\n";
				::WriteFile(outInfo.handle, msg, (Uint32)red::Strlen(msg), &numBytesWritten, nullptr);
			}
		}

		static void DumpCrashData( EErrorReason errorReason, const HandleErrorParams& params )
		{
			// #tbd: also dump exception info... but for now just let the debugger grab everything?
			if (gCrashFile.handle != INVALID_HANDLE_VALUE)
			{
				FileDumper dumper(gCrashFile.handle);

				dumper.WriteText( "InternalVersion: " );
				dumper.WriteTextNL( gErrorHandlerAppVersion ? gErrorHandlerAppVersion : "<Unknown>" );
				dumper.WriteTextNL( "!!!CRASHED!!!" );

				WriteErrorReason( dumper, errorReason, params );

				dumper.Newline();

				WriteScriptCallstack( dumper );

				red::err::ApplicationDataDumpContext context;
				red::err::ApplicationErrorDumper::RunDumpers( context );

				const Uint32 exceptionCode = ( params.exceptionInfo && params.exceptionInfo->ExceptionRecord ) ? params.exceptionInfo->ExceptionRecord->ExceptionCode : 0;
				WriteCrashDumpData( dumper, exceptionCode );

				// Don't close on PC because we have the delete-on-close attribute set
#ifdef RED_PLATFORM_DURANGO
				if (gCrashFile.handle != INVALID_HANDLE_VALUE)
				{
					// IMPORTANT MUST CLOSE: Let it get picked up by WER
					::CloseHandle(gCrashFile.handle);
					gCrashFile.handle = INVALID_HANDLE_VALUE;
				}
#endif
			}
		}

		static Bool BeginOutOfProcessErrorReporter(EErrorReason errorReason, const HandleErrorParams& params)
		{
			Bool success = true;

#ifdef RED_PLATFORM_WINPC
			char* msgBuffer = gErrorMessageBuffer;
			Uint32 msgBufferSize = RED_ARRAY_COUNT_U32(gErrorMessageBuffer);
			dbgutils::win32::ErrorReporterArgs reporterArgs;
			{
				reporterArgs.m_errorReason = errorReason;
				reporterArgs.m_pAttachmentTable = &gRegisteredAttachmentTable;
				reporterArgs.m_pCustomErrMsg = params.customErrMsg.IsValid() ? &params.customErrMsg : nullptr;
				reporterArgs.m_outErrorMessageBuffer = msgBuffer;
				reporterArgs.m_errorMessageBufferCount = msgBufferSize;
				reporterArgs.m_appVersionNumber = gErrorHandlerAppVersion ? gErrorHandlerAppVersion : "<Unknown>";
			}
			// Don't skip any more frames if have exceptionInfo, we're using the captured context in the exceptionInfo.

			auto& ipcContext = GetIPCCrashContextWin32_NoLock();
			if (!dbgutils::win32::ConnectErrorReporterIfNeeded_NotReentrant(ipcContext))
			{
				RED_DBG_TRACE("Failed to connect to the out of process error reporter!");
				success = false;
			}

			if (success)
			{
				if (!dbgutils::win32::WakeOutOfProcessErrorReporter(ipcContext, reporterArgs, params.exceptionInfo))
				{
					RED_DBG_TRACE("Failed to wake out of process error reporter!");
					success = false;
				}
			}
#endif
			return success;
		}

		static void EndOutOfProcessErrorReporter()
		{
#ifdef RED_PLATFORM_WINPC
			const auto& ipcContext = GetIPCCrashContextWin32_NoLock();
			if (!dbgutils::win32::WaitForErrorReportFinished(ipcContext))
			{
				RED_DBG_TRACE("Failed to wait for out of process error reporter!");
			}
#endif
		}
		
		void BeginEmergencyFlushLogFile(Uint64& outFlushFence)
		{
			red::LoggerFileSink::SendEmergencyCrashModeSignal(); // just sets a static atomic intron after, and should be quick and safe
			if( auto logger = red::GetSystemLoggerSafe() )
			{
				outFlushFence = logger->GetFlushFence();
			}
			else
			{
				outFlushFence = 0;
			}
		}

		void EndEmergencyFlushLogFile(Uint64 flushFence)
		{
			auto logger = red::GetSystemLoggerSafe();
			if( !logger )
			{
				return;
			}

			if( logger->GetMode() == LoggerMode::LoggerMode_Async ) // don't want to block
			{
				red::Timer flushTimer;
				Bool pushedFlush = false;
				for( ;; )
				{
					if( !pushedFlush )
					{
						// Not super safe in case of some live-lock, but the current implementation should only spin if another thread beat us
						// and return in the queue is full. There aren't any locks that could be held forever during a crash.
						pushedFlush = logger->TryPushFlush( LoggerFlushMode_Async );
					}
					if( logger->GetFlushFence() > flushFence )
					{
						break;
					}
					if( flushTimer.GetSeconds() > 1.f )
					{
						RED_DBG_TRACE( "Timed out flushing log" );
						break;
					}
					// Don't yield, just in case...
				}
				RED_DBG_TRACE( "Done" );
			}
		}

		static void SelectPrintErrorMessage( Uint32 errorHandlerFlags, EErrorReason errorReason, const HandleErrorParams& params )
		{
			if ( params.exceptionInfo )
			{
				PrintErrorMessage( gErrorHandlerFlags, params.exceptionInfo, gErrorMessageBuffer, RED_ARRAY_COUNT_U32( gErrorMessageBuffer ) );
			}
			else if ( params.customErrMsg.IsValid() )
			{
				PrintErrorMessage( gErrorHandlerFlags, errorReason, params.customErrMsg, gErrorMessageBuffer, RED_ARRAY_COUNT_U32( gErrorMessageBuffer ) );
			}
			else
			{
				PrintErrorMessage( gErrorHandlerFlags, errorReason, gErrorMessageBuffer, RED_ARRAY_COUNT_U32( gErrorMessageBuffer ) );
			}
		}

		using EnumerateThread = void( *)( DWORD threadId );

		static void EnumerateProcessThreads( DWORD processId, EnumerateThread enumThreadFn )
		{
#if defined( RED_PLATFORM_WINPC )
			const DWORD currentProcessId = GetCurrentProcessId();
			HANDLE hThreadSnapshot = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, currentProcessId );
			if( hThreadSnapshot == INVALID_HANDLE_VALUE )
			{
				RED_DBG_TRACE( "Failed to create thread snapshot." );
				return;
			}

			THREADENTRY32 entry = {};
			entry.dwSize = sizeof( entry );

			if( Thread32First( hThreadSnapshot, &entry ) )
			{
				do
				{
					if( entry.th32OwnerProcessID != currentProcessId )
					{
						continue;
					}

					enumThreadFn( entry.th32ThreadID );
				} while( Thread32Next( hThreadSnapshot, &entry ) );
			}
			CloseHandle( hThreadSnapshot );
#else
			// TODO: Unfortunately, there is no system way to enumerate threads of a given process on Durango platform.
			// ~eryk.dwornicki 14.11.2019
#endif
		}

		// Suspends all the threads we can so we can safely dump the exception data with bigger chance
		// of seeing the race condition.
		static void SuspendThreadsBeforeCrashDataGeneration()
		{
			EnumerateProcessThreads( GetCurrentProcessId(), []( DWORD threadId )
			{
				if( threadId == GetCurrentThreadId() )
				{
					// Never suspend current thread!
					return;
				}

				// We don't want to suspend logger thread so we have complete log file in case of crash.
				if( g_loggerThreadId.IsValid() )
				{
					if( threadId == g_loggerThreadId.id )
					{
						return;
					}
				}

				HANDLE hThread = OpenThread( THREAD_SUSPEND_RESUME, FALSE, threadId );
				if( hThread != INVALID_HANDLE_VALUE )
				{
					SuspendThread( hThread );
					CloseHandle( hThread );
				}
			} );
		}

		static void GenerateCrashData( EErrorReason errorReason, const HandleErrorParams& params )
		{
			// #tbd: could be lame here and sleep a second after this just to make sure the error reporter opened our process handler in case something goes wrong here
			const Bool isErrorReporterBegun = BeginOutOfProcessErrorReporter(errorReason, params);

			// The Win32 error reporter waits a few seconds after taking a minidump to collect attachments
			// We don't flush all this data so we can try to get a more accurate snapshot.

			Uint64 flushFence = 0;
			BeginEmergencyFlushLogFile(flushFence);

			DumpCrashData(errorReason, params); // dump crash data first in case something goes wrong after, and should be quick

			// Dump error into the log
			SelectPrintErrorMessage( gErrorHandlerFlags, errorReason, params );

			EndEmergencyFlushLogFile(flushFence);

			if (isErrorReporterBegun)
			{
				EndOutOfProcessErrorReporter();
			}
		}

#if defined( RED_PLATFORM_WINPC )
		struct HandleData
		{
			Uint32 processId;
			HWND windowHandle;
		};

		static BOOL IsMainWindow( HWND handle )
		{
			return GetWindow( handle, GW_OWNER ) == ( HWND )0 && IsWindowVisible( handle );
		}

		static BOOL CALLBACK EnumWindowsCallback( HWND handle, LPARAM lParam )
		{
			HandleData& data = *( HandleData* )lParam;
			unsigned long process_id = 0;
			GetWindowThreadProcessId( handle, &process_id );
			if( data.processId != process_id || !IsMainWindow( handle ) )
			{
				return TRUE;
			}
			data.windowHandle = handle;
			return FALSE;
		}

		static HWND FindMainWindow( Uint32 processId )
		{
			// Based on: https://stackoverflow.com/questions/1888863/how-to-get-main-window-handle-from-process-id
			HandleData data;
			data.processId = processId;
			data.windowHandle = 0;
			EnumWindows( EnumWindowsCallback, ( LPARAM )&data );
			return data.windowHandle;
		}
#endif

		static void HandleException( EErrorReason errorReason, const HandleErrorParams& params )
		{
			if ( atomic::Exchange32( &gHandledExceptionOnce, 1 ) != 0 )
			{
				for( ;; )
				{
					// Spin forever. We're about to die anyway, but don't spam from multiple threads
					red::SleepOnCurrentThread(1000);
				}
			}

			SuspendThreadsBeforeCrashDataGeneration();
			GenerateCrashData( errorReason, params );
#if defined( RED_PLATFORM_DURANGO )
			OutputCrashDebugString();
			DelayCrashForGameDVR();
#endif
		}

		static thread_local Bool tErrorParamValid = false;
		static thread_local Bool tUnhandledExceptionHandled = false;
		static thread_local red::EErrorReason tErrorReason;
		static thread_local HandleErrorParams tErrorParams;

		static LONG WINAPI ReportUnhandledExceptionFilter( _EXCEPTION_POINTERS* exceptionInfo )
		{
			// Handle exception only once. Some exceptions we handle in FistChanceException mode.
			if( tUnhandledExceptionHandled )
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}

			tUnhandledExceptionHandled = true;

			__try
			{
				ScopedFlag< Bool > flag( tExceptionFilterRecursionCheck = true, false );
	
				if ( tErrorParamValid
					&& ( exceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT
#if defined( RED_PLATFORM_DURANGO ) //< See #define RED_DEBUG_BREAK() for Xbox
						|| exceptionInfo->ExceptionRecord->ExceptionCode == 1
#endif
						) )
				{
					tErrorParams.exceptionInfo = exceptionInfo;
					HandleException( tErrorReason, tErrorParams );
				}
				// #tbd: we should just launch the error reporter at the start anyway vs JIT, since cmdlet startup time isn't that critical as once believed it would be
				// Then safer to do this during stack overflow
				// Try anyway, if doesn't work should fastfail.
				else//  if ( exceptionInfo->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW || tHasThreadStackGuarantee )
				{
					// Have exceptionInfo for context, do not skip more frames in stackwalk!
					HandleErrorParams params;
					params.exceptionInfo = exceptionInfo;
					HandleException( eErrorReason_UnhandledException, params );
				}
			}
			__except ( EXCEPTION_EXECUTE_HANDLER )
			{
				// Panic, bail.
#if defined( RED_PLATFORM_WINPC )
				::RaiseFailFastException( exceptionInfo->ExceptionRecord, exceptionInfo->ContextRecord, 0 );
#elif defined( RED_PLATFORM_DURANGO )
				__fastfail( FAST_FAIL_FATAL_APP_EXIT );
#else
#error Undefined platform!
#endif
			}
			
			// Let the program crash naturally.
			return EXCEPTION_CONTINUE_SEARCH;
		}

		static LONG WINAPI ReportFirstChanceExceptionFilter( _EXCEPTION_POINTERS* exceptionInfo )
		{
			if( IsDebuggerPresent() )
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}

			if( exceptionInfo->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION &&
				exceptionInfo->ExceptionRecord->ExceptionCode != EXCEPTION_HEAP_CORRUPTION )
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}

			return ReportUnhandledExceptionFilter( exceptionInfo );
		}

		RED_NOINLINE static void RED_CDECL PureCallHandler()
		{	
			tErrorParams = HandleErrorParams{};
			tErrorReason = eErrorReason_PureCall;
			tErrorParamValid = true;
			RED_DEBUG_BREAK();
			AbortProcess();
		}

		RED_NOINLINE static void RED_CDECL InvalidParameterHandler( const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved )
		{
			// _invalid_parameter_noinfo will just pass all nulls to this function!
			char fileAscii[ MAX_PATH ] = "<Unknown>";
			char exprAscii[ 256 ] = "<Unknown>";
			if ( file && file[ 0 ] )
			{
				WideCharToStdChar_NoConv( fileAscii, file, RED_ARRAY_COUNT_U32( fileAscii ) );
			}
			if ( expression && expression[ 0 ] )
			{
				WideCharToStdChar_NoConv( exprAscii, expression, RED_ARRAY_COUNT_U32( exprAscii ) );
			}
			ErrorMessage msg;
			{
				msg.m_file = fileAscii;
				msg.m_line = line;
				msg.m_expression = exprAscii;
				msg.m_message = "";
			}

			tErrorParams = HandleErrorParams{};
			tErrorParams.customErrMsg = msg;
			tErrorReason = eErrorReason_InvalidParameter;
			tErrorParamValid = true;
			RED_DEBUG_BREAK();
			AbortProcess();
		}

		RED_NOINLINE static void RED_CDECL AbortHandler( int )
		{
			tErrorParams = HandleErrorParams{};
			tErrorReason = eErrorReason_Abort;
			tErrorParamValid = true;
			RED_DEBUG_BREAK();
			AbortProcess();
		}

		RED_NOINLINE static int RED_CDECL CrtReportHook( int nReportType, char* szMsg, int* pnRet )
		{
			// Can't reliably get file and line info, even parsing out - since a lot of important CRT messages save that for some other assert dialog
			// But the callstack will have the info
			ErrorMessage msg;
			msg.m_message = ( szMsg && szMsg[ 0 ] ) ? szMsg : "<Unknown>";

			// #tbd: we can get CRT warnings during CRT *shutdown* during atexit, like for memory leaks.
			// If we do then so many bets are off what you can even safely do!
			// E.g., can't have some "thread-safe C++11 static with ctor" in MSVC because it'll explode in _Init_thread_header, like in static IPCArgs ipcArgs.
			// It's not that easy to know if in CRT shutdown either - may consider eliminating as much CRT dependency as possible,
			// but that also means to printf/puts etc, using outputdebugstring/kernel writes instead.

			int ret = FALSE;
			if ( nReportType == _CRT_ERROR || nReportType == _CRT_ASSERT )
			{
				*pnRet = 0; // CRT shouldn't call debugbreak for us
				ret = TRUE; // No other hook should be executed

				tErrorParams = HandleErrorParams{};
				tErrorParams.customErrMsg = msg;
				tErrorReason = eErrorReason_CrtError;
				tErrorParamValid = true;
				RED_DEBUG_BREAK();
				AbortProcess();
			}
			else if ( nReportType == _CRT_WARN )
			{
				*pnRet = 0; // CRT still shouldn't call debugbreak for us
				ret = FALSE; // Let some other hook get called if it wants to
			}

			return ret;
		}

		static void AssertHandler( const ErrorMessage& errMsg )
		{
			tErrorParams = HandleErrorParams{};
			tErrorParams.customErrMsg = errMsg;
			tErrorReason = eErrorReason_Assert;
			tErrorParamValid = true;

			SelectPrintErrorMessage( eErrorHandlerFlags_MessageStreamConsole, tErrorReason, tErrorParams );

			// Note: the assert macro will call RED_DEBUG_BREAK() so it happens inline.
			// Being deep in the callstack here is inconvenient and confusing for debugging.
		}

		static void RegisterAttachment( const wchar_t* pathToRegister )
		{
			err::RegisterAttachment( gRegisteredAttachmentTable, pathToRegister );
		}

		static void RegisterAttachment( const char* pathToRegister )
		{
			wchar_t buffer[ MAX_PATH ];
			StdCharToWideChar_NoConv( buffer, pathToRegister, MAX_PATH );
			RegisterAttachment( buffer );
		}

	} // helper

	ErrorHandlerHooks RegisterErrorHooksOnceWin32( Uint32 errFlags, const char* appVersion, ErrorHandlerHooks::ScriptCallstackVisitorFunc* scriptCallstackVisitor )
	{
		ErrorHandlerHooks hooks;
		hooks.fnAssertHandler = &helper::AssertHandler;
		hooks.fnRegisterAttachment = &helper::RegisterAttachment;
		hooks.fnFailFastAbortProcess = &helper::AbortProcess;

		if ( !gScriptCallstackVisitor )
		{
			gScriptCallstackVisitor = scriptCallstackVisitor;
		}

		hooks.fnScriptCallstackVisitor = gScriptCallstackVisitor;

		if ( atomic::Exchange32( &gRegisteredOnce, 1 ) != 0 )
		{
			return hooks;
		}

		gUptimeStartTicks = red::ProfileTimer::GetTicks();

		gErrorHandlerFlags = errFlags;
		if ( appVersion != nullptr )
		{
			gErrorHandlerAppVersion = appVersion;
		}

		helper::InitWinPCModuleFileName();

		helper::tHasThreadStackGuarantee = helper::TrySetThreadStackGuarantee();

		::SetUnhandledExceptionFilter( &helper::ReportUnhandledExceptionFilter );
		// Useful functions to call as per https://randomascii.wordpress.com/2012/07/22/more-adventures-in-failing-to-crash-properly/
		::_set_purecall_handler( &helper::PureCallHandler );
		::_set_invalid_parameter_handler( &helper::InvalidParameterHandler );
#if defined( RED_PLATFORM_WINPC )
		::AddVectoredExceptionHandler( 0/*call last*/, &helper::ReportFirstChanceExceptionFilter );
#endif

		::_set_abort_behavior( _CALL_REPORTFAULT, _CALL_REPORTFAULT );
		::signal( SIGABRT, &helper::AbortHandler );

		// Disable CRT dialog boxes
		_CrtSetReportMode( _CRT_ASSERT, 0 );
		_CrtSetReportMode( _CRT_ERROR, 0 );
		_CrtSetReportMode( _CRT_WARN, 0 );

		// Route everything through our own handler.
		// #tbd: look into _RTC_SetErrorType
		_CrtSetReportHook2( _CRT_RPTHOOK_INSTALL, &helper::CrtReportHook );

#if defined( RED_PLATFORM_WINPC )
		const Uint32 localProcessID = ::GetCurrentProcessId();
		// Allocate the relatively cheaper IPC memory upfront.
		if ( !dbgutils::win32::OpenIPC( localProcessID, dbgutils::win32::eIPCOpenParam_ErrorReportee, helper::GetIPCCrashContextWin32_NoLock() ) )
		{
			RED_DBG_TRACE( "Failed to establish error handler IPC" );
		}

		if ( (errFlags & eErrorHandlerFlags_ErrorReporterJustInTime ) == 0 )
		{
			helper::LaunchErrorReporter_NoLock();
		}
#endif

#if defined( RED_PLATFORM_DURANGO)
		// #tbd: if a file was deleted, does WER keep a copy alive somewhere?
		hacks::DurangoUnregisterFilesNonRecursive( L"D:\\" );
		hacks::DurangoUnregisterFilesNonRecursive( L"D:\\temp\\" );
		hacks::DurangoUnregisterFilesNonRecursive( gDurangoTempDir );
#endif

		helper::CreateCrashFile(gCrashFile);

#if defined( RED_PLATFORM_WINPC )
		if ( !helper::GetLastChanceDebugBreakSetting() )
		{
			// Suppress any possible WER dialog or debug prompt
			::SetErrorMode( ::GetErrorMode() | SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS );
		}
#endif

		return hooks;
	}
} } //red/err

#endif
/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#ifdef RED_PLATFORM_ORBIS
# include <coredump.h>
# include <coredump_structureddata.h>
# include <system_service.h>
#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
#include <libdeci4h.h>
#endif
#include <libsysmodule.h>
#include <net.h>
#include "dbgUtils.h"
#include "crt.h"
#include "errorHandlerImplAttachments.h"
#include "errorHandlerImplCrashDumpData.h"
#include "errorHandlerImplMessages.h"
#include "errorHandlerImplCrashDumpData.h"
#include "errorHandlerImplErrorHooksOrbis.h"
#include "loggerLocklessQueue.h"
#include "threads.h"
#include "logger.h"
#include "applicationErrorDumper.h"

#pragma comment( lib, "libSceCoredump_stub_weak.a" )
#pragma comment( lib, "libSceCoredumpStructuredData.a" )
#pragma comment( lib, "libSceNet_stub_weak.a" )

static const Bool c_debugCrashHandler = true;

namespace red { namespace err
{
	struct CoreDumpParams
	{
		const char* assertFile;
		const char* assertExpression;
		const char* assertMessage;
		Uint32		assertLine;
		Uint64		uptimeStartTicks;
		dbgutils::RegisteredAttachmentTable* registeredAttachmentTable;
		const char* appVersion;
	};

	static_assert( std::is_pod< CoreDumpParams >::value, "" );

	const char ERROR_REPORTER_EXE[] = "REDEngineErrorReporterPS4.exe";
	
	static const Uint32 c_msgBufferSize = 1024;
	
	static char gErrorMessageBuffer[ c_msgBufferSize ];
	static Uint32 gErrorHandlerFlags = eErrorHandlerFlags_Default;
	static const char* gErrorHandlerAppVersion = "<Unknown>";
	
	static atomic::TAtomic32 gRegisteredOnce = 0;
	static atomic::TAtomic32 gHandledErrorOnce = 0;
	static CoreDumpParams gCoreDumpParams;
	
	static dbgutils::RegisteredAttachmentTable gRegisteredAttachmentTable;
	static_assert( std::is_pod< dbgutils::RegisteredAttachmentTable >::value, "Should be POD to avoid static order init fiasco or move into guarded static init function" );

	static const Uint32 c_crashDumpNewlineSize = 2; // "\r\n"
	static const Uint32 c_crashDumpPrefixSize = 3 + 8;// "<%08X> "
	static char gCrashLogBuffer[::red::c_loggerQueueSize * (::red::c_loggerLineLength + c_crashDumpNewlineSize + c_crashDumpPrefixSize)];

	static ErrorHandlerHooks::ScriptCallstackVisitorFunc* gScriptCallstackVisitor = nullptr;
	static char gCrashScriptCallstackBuffer[ 16 * 1024 ];

	static Uint32 gPid = 0;

	namespace
	{
		const char* s_scriptCallstackSeparator = "----------------------------------------\r\n";

		class ScriptStackFrameVisitor
		{
		public:
			ScriptStackFrameVisitor( char*& buffer, Uint32& spaceLeft, Uint32 currentThreadId )
				: m_buffer( buffer )
				, m_spaceLeft( spaceLeft )
				, m_currentThreadId( currentThreadId )
				, m_writtenHeader ( false )
			{}

			Bool operator()( Uint32 threadId, const red::AnsiChar* buffer, Uint32 bufferSize )
			{
				if ( m_currentThreadId == threadId )
				{
					if ( !m_writtenHeader )
					{
						m_writtenHeader = true;

						char callstackPrefix[ 128 ] = { '\0' };
						std::snprintf( callstackPrefix, RED_ARRAY_COUNT( callstackPrefix ), "Script Callstack (threadId=%" PRIu32 "):\r\n", threadId );
						if ( !AddData( s_scriptCallstackSeparator, red::Strlen( s_scriptCallstackSeparator ) ) || !AddData( callstackPrefix, red::Strlen( callstackPrefix ) ) )
						{
							return false;
						}
					}

					if( !AddData( buffer, red::Strlen( buffer ) ) || !AddData( "\r\n", 2 ) )
					{
						return false;
					}
				}

				return true;
			}

		private:
			Bool AddData( const char* data, Uint32 dataLength )
			{
				if ( m_spaceLeft <= dataLength )
				{
					return false;
				}

				red::Strcat( m_buffer, data, m_spaceLeft );
				m_spaceLeft -= dataLength;
				m_buffer += dataLength;
				return true;
			}

			char*& m_buffer;
			Uint32& m_spaceLeft;
			Uint32 m_currentThreadId;
			Bool m_writtenHeader;
		};
	}

	template<>
	class OrbisLoggerCrashDumper<::red::LoggerLine>
	{
	public:
		explicit OrbisLoggerCrashDumper(const ::red::Logger& logger )
			: m_queue(logger.m_loggerLineQueue)
		{
		}

		void DumpToUserData()
		{
			Uint32 spaceLeft = sizeof(gCrashLogBuffer);
			const Uint32 origReadPosition = m_queue.m_dequeuePosition;
			Uint32 readPosition = m_queue.m_dequeuePosition;
			char* buf = gCrashLogBuffer;
			const char* line = "\r\n";
			const Uint32 lineLen = (Uint32)red::Strlen(line);
			do 
			{
				const auto& entry = m_queue.m_entries[readPosition & ::red::c_loggerQueueMask];
				const auto& msg = entry.message;
				const Uint32 len = (Uint32)red::Strlen(msg.buffer, ::red::c_loggerLineLength);
				if (len + c_crashDumpNewlineSize + c_crashDumpPrefixSize > spaceLeft)
				{
					// Should always have enough space, but be defensive about it
					break;
				}

				const Int32 res = red::SNPrintFUnsafe(buf, spaceLeft, "<%08X> ", (Uint32)entry.position );
				if (res < 0 )
				{
					break;
				}

				buf += res;
				spaceLeft -= res;

				red::Memcpy(buf, msg.buffer, len);
				buf += len;
				spaceLeft -= len;

				red::Memcpy(buf, line, lineLen);
				buf += lineLen;
				spaceLeft -= lineLen;

				readPosition = (readPosition - 1) & ::red::c_loggerQueueMask;
			}
			while (readPosition != origReadPosition);
			const Uint32 bytesWritten = sizeof(gCrashLogBuffer) - spaceLeft;
			sceCoredumpAttachMemoryRegionAsUserFile(999, gCrashLogBuffer, bytesWritten, "LoggerQueueCrashDump.txt");
		}

	private:
		const ::red::LoggerLocklessQueue<::red::LoggerLine>& m_queue;
	};

	namespace helper
	{
		static void PrintCoreDumpMesage( const char* dumpMessage )
		{
			if ( c_debugCrashHandler && dumpMessage )
			{
				::sceCoredumpDebugTextOut(dumpMessage, red::Strlen(dumpMessage, 256)); // cap size as an extra precaution));
			}
		}

		// #tbd: relaunch?
		static Bool FormatEth0MacAddress( char* buffer, size_t size )
		{
			// Documented as returning the MAC address for eth0, even if using wifi or some other interface
			Int32 ret = SCE_OK;
			SceNetEtherAddr eth0;
			ret = ::sceNetGetMacAddress( &eth0, 0 );
			if ( ret != SCE_OK )
			{
				RED_DBG_TRACE( "sceNetGetMacAddress failed: 0x%08X", ret );
				return false;
			}
			ret = ::sceNetEtherNtostr( &eth0, buffer, size );
			if ( ret != SCE_OK )
			{
				RED_DBG_TRACE( "sceNetEtherNtostr failed: 0x%08X", ret );
				return false;
			}

			return true;
		}

		static void Deci4hExitHandler( int32_t exitResult, uint32_t hostProcessId, uint32_t hostProcessExitCode, void *userArg )
		{
			RED_DBG_TRACE( "Deci4hCreateProcess: error handler program exiting! result=%d, procID=%u, hostProcExitCode=%u", exitResult, hostProcessId, hostProcessExitCode );
		}

		static Bool Deci4hCreateProcess( const char* exePath, const char* cmdLine = nullptr )
		{
#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
			SceDeci4hCreateHostProcessParam processParam;
			Memzero( &processParam, sizeof( processParam ) );
			{
				processParam.pathName = exePath;
				processParam.cmdLine = cmdLine;
				processParam.flags = SCE_DECI4H_HOST_PROCESS_WORKDIR_FSROOT | SCE_DECI4H_HOST_PROCESS_WINDOW_HIDE; // for lack of a better general option; should generally be writable if need to log etc
				processParam.workDir = nullptr;
			}
			SceDeci4hCreateHostProcessResult result;
			const Int32 ret = ::sceDeci4hCreateHostProcess( &processParam, &Deci4hExitHandler, nullptr /*userArg*/, &result );
			if ( ret != SCE_OK )
			{
				RED_DBG_TRACE( "sceDeci4hCreateHostProcess failed: 0x%08X", ret );
				if ( ret == SCE_DECI4H_ERROR_INVALID_ARGUMENT )
				{
					// Verified error code when couldn't find the .exe, not SCE_DECI4H_ERROR_HOST_CREATE_PROCESS
					RED_DBG_TRACE( "Note: Possible fix - REDEngineErrorReporterPS4.exe should be in the file serving directory root" );
				}
				else if ( ret == SCE_DECI4H_ERROR_HOST_PROCESS_DISABLED )
				{
					RED_DBG_TRACE( "Note: HostExec requires enabling, e.g., 'orbis-ctrl hostexec enable' or in neighborhood settings, or even TMAPI" );
				}
				return false;
			}
#endif
			return true;
		}

		static Bool TryGetExecutablePath( char* exePathBuffer, Uint32 exePathBufferSize )
		{
			const Int32 numWritten = ::sceDbgGetExecutablePath( exePathBuffer, exePathBufferSize );
			if ( numWritten == SCE_SYSTEM_SERVICE_ERROR_REJECTED )
			{
				RED_DBG_TRACE( "sceDbgGetExecutablePath failed with SCE_SYSTEM_SERVICE_ERROR_REJECTED: launched by system software" );
			}

			if ( numWritten == SCE_SYSTEM_SERVICE_ERROR_PARAMETER )
			{
				RED_DBG_TRACE( "sceDbgGetExecutablePath failed with SCE_SYSTEM_SERVICE_ERROR_PARAMETER" );
				return false;
			}		

			if ( numWritten == exePathBufferSize )
			{
				RED_DBG_TRACE( "sceDbgGetExecutablePath failed: buffer too small" );
				return false;
			}

			return true;
		}

		// #tbd: if on /data/ should explicitly pass in some symbol location
		static Bool TryAppendSymbolPathToCmdline( char* cmdLine, Uint32 cmdLineSize )
		{
			char exePathBuffer[ 1024 ] = "";
			const char* symbolDir = exePathBuffer;
			const char* hostPrefix = "/host/";
			const Uint32 hostPrefixLen = red::Strlen( hostPrefix );
			if ( !helper::TryGetExecutablePath( exePathBuffer, RED_ARRAY_COUNT_U32( exePathBuffer ) ) || red::Strcmp( exePathBuffer, hostPrefix, hostPrefixLen ) != 0 )
			{
				return false;
			}

			symbolDir += hostPrefixLen;

			char* lastBackslash = red::StrchrR( exePathBuffer, '\\' );
			if ( lastBackslash )
			{
				*lastBackslash = '\0';
			}

			red::Strcat( cmdLine, " symbols ", cmdLineSize );
			red::Strcat( cmdLine, symbolDir, cmdLineSize );
			return true;
		}

		static void HostExecErrorReporter_NoLock( char* cmdLine )
		{
			char exePath[ 260 ] = "";

			SNPrintFUnsafe( exePath, RED_ARRAY_COUNT_U32( exePath ), "/hostapp/engine\\tools\\%hs", ERROR_REPORTER_EXE );
			if ( !Deci4hCreateProcess( exePath, cmdLine ) )
			{
				RED_DBG_TRACE( "Failed to launch error reporter from %hs", exePath );
			}
			else
			{
				RED_DBG_TRACE( "Launched error reporter from %hs", exePath );
			}
		}
	
		static void AddStopInfoCPU(sce::CoredumpStructuredData::StructuredUserdata& userData)
		{
			SceCoredumpStopInfoCpu stopInfoCpu = {};
			const Int32 ret = ::sceCoredumpGetStopInfoCpu(&stopInfoCpu, sizeof(stopInfoCpu));
			const char* nameThreadID = "PS4/stopInfoCPU/stopThreadID";
			const char* nameReasonCode = "PS4/stopInfoCPU/stopReasonCode";
			if (ret == SCE_OK)
			{
				if (stopInfoCpu.thread == SCE_COREDUMP_THREAD_NA)
				{
					userData.addValue(nameThreadID, "<Stop due to an asynchronous exception>");
				}
				else if (stopInfoCpu.thread == SCE_COREDUMP_THREAD_TRIGGERED)
				{
					userData.addValue(nameThreadID, "<Stop due to a user request>");
				}
				else
				{
					// https://ps4.siedev.net/forums/thread/53323/
					if (stopInfoCpu.thread)
					{
						const Uint32 threadID = *reinterpret_cast<const Uint32*>(stopInfoCpu.thread);
						userData.addValue(nameThreadID, threadID);
					}
				}
				//#todo: map to strings like in crash reporter
				userData.addValue(nameReasonCode, stopInfoCpu.reason_code);
			}
			else if (ret == SCE_COREDUMP_ERROR_STOP_INFO_UNAVAILABLE)
			{
				userData.addValue(nameThreadID, "<Unavailable: not CPU related cause>");
			}
			else
			{
				userData.addValue(nameThreadID, "<Error>");
			}
		}

		static void AddStopInfoGPU(sce::CoredumpStructuredData::StructuredUserdata& userData)
		{
			SceCoredumpStopInfoGpu stopInfoGpu = {};
			const char* name = "PS4/stopInfoGPU/timestamp";
			const Int32 ret = sceCoredumpGetStopInfoGpu(&stopInfoGpu, sizeof(stopInfoGpu));
			if (ret == SCE_OK)
			{
				userData.addValue(name, stopInfoGpu.timestamp);
			}
			else if (ret == SCE_COREDUMP_ERROR_STOP_INFO_UNAVAILABLE )
			{
				userData.addValue(name, "<Unavailable>");
			}
			else
			{
				userData.addValue(name, "<Error>");
			}
		}

// 		static void AddRegisters(sce::CoredumpStructuredData::StructuredUserdata& userData)
// 		{
// 			//::sceCoredumpGetThreadContextInfo
// 		}

		static void DumpStructuredUserData( const CoreDumpParams& dumpParams )
		{
			helper::PrintCoreDumpMesage("== Dumping structured user data ==\n");

			sce::CoredumpStructuredData::StructuredUserdata writer;

			writer.addValue("version", dumpParams.appVersion ? dumpParams.appVersion : "<UNKNOWN>");

			// Always add, even if wasn't from assert. Make any post-processing more consistent.
			writer.addValue("lastErrorExpression", dumpParams.assertExpression ? dumpParams.assertExpression : "<UNKNOWN>");
			writer.addValue("lastErrorMessage", dumpParams.assertMessage ? dumpParams.assertMessage : "<UNKNOWN>");
			writer.addValue("lastErrorFile", dumpParams.assertFile ? dumpParams.assertFile : "<UNKNOWN>");
			writer.addValue("lastErrorLine", dumpParams.assertLine);
			writer.addValue("processID", gPid );

			const Uint64 endTimeTicks = red::ProfileTimer::GetTicks();
			const Uint64 seconds = red::ProfileTimer::GetSec(endTimeTicks - dumpParams.uptimeStartTicks);
			writer.addValue("uptimeSeconds", seconds);
			AddStopInfoCPU(writer);
			AddStopInfoGPU(writer);

			helper::PrintCoreDumpMesage("= Dumping crashdata =\n");

			DumpCrashDataResult dumpResult;
			err::DumpCrashData(&writer, [](void* thisPtr, const char* formattedName, const char* formattedValue) {
				auto* writer = static_cast<sce::CoredumpStructuredData::StructuredUserdata*>(thisPtr);
				
				helper::PrintCoreDumpMesage( ">  ");
				helper::PrintCoreDumpMesage( formattedName );
				helper::PrintCoreDumpMesage("\n");

				writer->addValue(formattedName, formattedValue);
			}, dumpResult);
			helper::PrintCoreDumpMesage("= FINISHED dumping crashdata =\n");
			RED_UNUSED(dumpResult);

			helper::PrintCoreDumpMesage("= FINISHED dumping structured user data =\n");
		}

		static void DumpScriptCallstack()
		{
			if ( gScriptCallstackVisitor )
			{
				helper::PrintCoreDumpMesage( "== Dumping script callstack ==\n" );

				Uint32 spaceLeft = RED_ARRAY_COUNT( gCrashScriptCallstackBuffer );
				char* buf = gCrashScriptCallstackBuffer;

				gScriptCallstackVisitor( ScriptStackFrameVisitor( buf, spaceLeft, red::ThreadId::CurrentThread().AsNumber() ) );

				const Uint32 bytesWritten = RED_ARRAY_COUNT( gCrashScriptCallstackBuffer ) - spaceLeft;
				sceCoredumpAttachMemoryRegionAsUserFile( 999, gCrashScriptCallstackBuffer, bytesWritten, "ScriptCallstackCrashDump.txt" );

				helper::PrintCoreDumpMesage( "== FINISHED dumping script callstack ==\n" );
			}
		}

		static void DumpRegisteredFiles(const CoreDumpParams& dumpParams)
		{
			helper::PrintCoreDumpMesage("== Attaching files ==\n");
			if (dumpParams.registeredAttachmentTable)
			{
				const Uint32 numAttachments = dumpParams.registeredAttachmentTable->m_numRegisteredAttachments;
				for (Uint32 i = 0; i < numAttachments; ++i)
				{
					const auto& absolutePath = dumpParams.registeredAttachmentTable->m_registeredAttachments[i].m_absolutePath;
					Uint32 priority = numAttachments - i;
					helper::PrintCoreDumpMesage(">  ");
					helper::PrintCoreDumpMesage(absolutePath);
					helper::PrintCoreDumpMesage("\n");
					::sceCoredumpAttachUserFile(priority, absolutePath);
				}
			}
			helper::PrintCoreDumpMesage("== FINISHED attaching files ==\n");
		}

		static void DumpLog()
		{
			helper::PrintCoreDumpMesage("== Dumping logger queue ==\n");

			auto& logger = red::GetSystemLogger();
			OrbisLoggerCrashDumper< ::red::LoggerLine > dumper{ logger };
			dumper.DumpToUserData();

			helper::PrintCoreDumpMesage("== FINISHED dumping logger queue ==\n");
		}


		static Bool gIsCoreDumpCallback = false;

		static Int32 CoreDumpCallback( void* userData )
		{
			gIsCoreDumpCallback = true;

			// #tbd: include useful game debug data, prepopulated.
			// Note: 10 seconds limit to gather it or the handler gets aborted!
			// Can see with extract-userdata command of orbis-ctrl 

			// #tbd: size limitations and workaround (about 1 MB)
			// #tbd: attaching memory regions as files
			// #tbd: host/hostapp files

			helper::PrintCoreDumpMesage("=== REDEngine CoreDumpCallback STARTED ===\n");

			const auto* dumpParams = static_cast<const CoreDumpParams*>( userData );
			if (!dumpParams)
			{
				helper::PrintCoreDumpMesage("=== No dump params. Bailing!\n");
				return -1;
			}

			// The PS4 gives us 10 seconds... so we should be fine
			// Can check the debugtext output timestamps in the console log

			// Dump this first, since theoretically nothing should go wrong here...
			DumpRegisteredFiles(*dumpParams);

			// Set the constraint handler so we can maybe catch something here....
			DumpStructuredUserData(*dumpParams);

			// Dump script callstack from current thread
			DumpScriptCallstack();

			// Dump application data
			red::err::ApplicationDataDumpContext context;
			red::err::ApplicationErrorDumper::RunDumpers( context );

			// Dump last, since somewhat riskier
			DumpLog();

			helper::PrintCoreDumpMesage("=== REDEngine CoreDumpCallback FINISHED ===\n");

			return 0;
		}

		static void AbortProcess()
		{
			::sceSystemServiceReportAbnormalTermination( nullptr );
		}

		static void AssertHandler( const ErrorMessage& errMsg )
		{
			if ( atomic::Exchange32( &gHandledErrorOnce, 1 ) != 0 )
			{
				// will abort soon, but prevent multiple asserts from budding in
				for(;;)
				{
					red::SleepOnCurrentThread(1000);
				}
			}

			gCoreDumpParams.assertFile = errMsg.m_file;
			gCoreDumpParams.assertExpression = errMsg.m_expression;
			gCoreDumpParams.assertMessage = errMsg.m_message;
			gCoreDumpParams.assertLine = errMsg.m_line;

			PrintErrorMessage( gErrorHandlerFlags, red::eErrorReason_Assert, errMsg, gErrorMessageBuffer, RED_ARRAY_COUNT_U32( gErrorMessageBuffer ) );		
			// Don't trap the error here, let the assert do its own debugbreak at the source of the issue. More convenient for debugging.
		}

		static void ConstraintHandler( const char* restrict s, void* restrict p, errno_t errcode )
		{
			if ( gIsCoreDumpCallback )
			{
				//....
				return;
			}

			ErrorMessage msg;
			msg.m_message = "Runtime constraint violation";

			// Need to format p and errcode, should deep cpy
			//msg.m_expression = ( s && *s ) ? s : "<no further details>";

			AssertHandler( msg );
			if ( dbgutils::IsDebuggerAttached() )
			{
				RED_DEBUG_BREAK();
			}
			AbortProcess();
		}

		static void RegisterAttachment( const char* pathToRegister )
		{
			err::RegisterAttachment( gRegisteredAttachmentTable, pathToRegister );
		}

		static void StartErrorReporter()
		{
			const Int32 ret = ::sceSysmoduleLoadModule(SCE_SYSMODULE_DECI4H);
			if (ret != SCE_OK && ::sceSysmoduleIsLoaded(SCE_SYSMODULE_DECI4H) != SCE_SYSMODULE_LOADED)
			{
				// #tbd: consider sceNet fallback for testkits? To know the host PC address, would have set up a listen socket and wait for a connection from it, possibly before main().
				RED_DBG_TRACE("sceSysmoduleLoadModule SCE_SYSMODULE_DECI4H failed: 0x%08X%hs", ret, ret == SCE_SYSMODULE_ERROR_INVALID_VALUE ? "SCE_SYSMODULE_ERROR_INVALID_VALUE (TestKit?)" : "");

				// avoid crashes in unresolved PRX symbols
				return;
			}

			// Requires enabling, e.g., "orbis-ctrl hostexec enable" or neighborhood settings, or even TMAPI
			char macAddr[SCE_NET_ETHER_ADDRSTRLEN] = "";
			if (helper::FormatEth0MacAddress(macAddr, RED_ARRAY_COUNT_U32(macAddr)))
			{
				RED_DBG_TRACE("MAC address eth0: %hs", macAddr);

				// mac=%hs:proto=%x
				char cmdLine[2048] = "";
				Int32 numWritten = red::SNPrintFUnsafe(cmdLine, RED_ARRAY_COUNT_U32(cmdLine), "%hs", macAddr);
				if (numWritten > 0)
				{
					(void)helper::TryAppendSymbolPathToCmdline(cmdLine, RED_ARRAY_COUNT_U32(cmdLine));
				}

				// #todo: delayed/initial launch!
				if (!dbgutils::IsDebuggerAttached())
				{
					helper::HostExecErrorReporter_NoLock(cmdLine);
				}
				else
				{
					RED_DBG_TRACE("Debugger attached. Not launching error reporter since will exit at first breakpoint hit");
				}
			}
		}
	} // helper

	ErrorHandlerHooks RegisterErrorHooksOnceOrbis( Uint32 errFlags, const char* appVersion, ErrorHandlerHooks::ScriptCallstackVisitorFunc* scriptCallstackVisitor )
	{
		gPid = static_cast< Uint32 >( getpid( ) );

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

		gErrorHandlerFlags = errFlags;

		if ( appVersion )
		{
			gErrorHandlerAppVersion = appVersion;
		}

		gCoreDumpParams.uptimeStartTicks = red::ProfileTimer::GetTicks();
		gCoreDumpParams.appVersion = appVersion;
		gCoreDumpParams.registeredAttachmentTable = &gRegisteredAttachmentTable;

		Int32 err = ::sceCoredumpRegisterCoredumpHandler( &helper::CoreDumpCallback, SCE_PTHREAD_STACK_MIN, &gCoreDumpParams );
		if ( err != SCE_OK )
		{
			RED_DBG_TRACE( "sceCoredumpRegisterCoredumpHandler failed: 0x%08X", err );
		}

#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
		SceDbgReleaseCheckMode mode;
		if (sceDbgGetReleaseCheckMode(&mode) == SCE_OK && mode == SCE_DBG_RELEASE_CHECK_MODE_DEVELOPMENT)
		{
			helper::StartErrorReporter();
		}
#endif
		
		// Test this first: by default contraint violations are ignored on PS4, but on Win32 they're not
		//::set_constraint_handler_s( &helper::ConstraintHandler );

		return hooks;
	}

} } // red/prv

#else

RED_NO_EMPTY_FILE();

#endif // ORBIS

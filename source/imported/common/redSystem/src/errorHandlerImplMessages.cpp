/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "errorHandlerImplMessages.h"
#include "dbgUtils.h"
#include "log.h"
#include "errorHandler.h"

#ifdef RED_PLATFORM_ORBIS
# include <libdbg.h>
#endif

namespace red { namespace err
{
	static void PrintErrorString( Uint32 errFlags, const char* str )
	{
		// Can still be useful even if no debugger attached. E.g., on orbis you can see the output in the Neighborhood console or in minidumps
		if ((errFlags & red::eErrorHandlerFlags_MessageStreamConsole) != 0)
		{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
			::OutputDebugStringA( str );
#elif defined( RED_PLATFORM_ORBIS )
# ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
			::sceDbgUserChannelPrint(SCE_DBG_USER_CHANNEL_2, str);
# else
			::fputs(str, stderr);
			::fflush(stderr);
# endif
#endif
		}
		if ( ( errFlags & red::eErrorHandlerFlags_MessageStreamLog ) != 0 )
		{
#ifdef RED_LOGGING_ENABLED
			red::LogMessage( LoggerLevel_Error, str );
			RED_LOG_FLUSH_AND_WAIT(); // #tbd: still need to be able to emergency flush the logger thread, plus close the log file on Durango
#endif
		}
		if ( ( errFlags & red::eErrorHandlerFlags_MessageStreamStderr ) != 0 )
		{
			::fputs( str, stderr );
			::fflush( stderr );
		}
		if ( ( errFlags & red::eErrorHandlerFlags_MessageStreamStdout ) != 0 )
		{
			::fputs( str, stdout );
			::fflush( stdout );
		}
		if ( ( errFlags & red::eErrorHandlerFlags_MessageStreamTrace ) != 0 )
		{
			RED_DBG_TRACE( "%hs", str );
		}
	}

	void PrintErrorMessage( Uint32 errFlags, red::EErrorReason errorReason, const red::ErrorMessage& customErrorMsg, char* scratchBuffer, Uint32 bufferSize )
	{
		red::SNPrintFUnsafe( scratchBuffer, bufferSize, "%hs: %hs: %hs - %hs(%u)\n",
								ErrorReasonText( errorReason ), customErrorMsg.m_expression, customErrorMsg.m_message, customErrorMsg.m_file, customErrorMsg.m_line );

		PrintErrorString( errFlags, scratchBuffer );
	}

	void PrintErrorMessage( Uint32 errFlags, red::EErrorReason errorReason, char* scratchBuffer, Uint32 bufferSize )
	{
		red::SNPrintFUnsafe( scratchBuffer, bufferSize, "%hs: <No message>\n", ErrorReasonText( errorReason ) );
		PrintErrorString( errFlags, scratchBuffer );
	}

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	void PrintErrorMessage( Uint32 errFlags, _EXCEPTION_POINTERS* exceptionInfo, char* scratchBuffer, Uint32 bufferSize )
	{
		if ( exceptionInfo && exceptionInfo->ExceptionRecord )
		{
			red::SNPrintFUnsafe( scratchBuffer, bufferSize, "%hs: ExceptionCode=0x%08X at address 0x%llX\n", ErrorReasonText( red::eErrorReason_UnhandledException ),
								 exceptionInfo->ExceptionRecord->ExceptionCode,
								 exceptionInfo->ExceptionRecord->ExceptionAddress );
			PrintErrorString( errFlags, scratchBuffer );
		}
		else
		{
			red::SNPrintFUnsafe( scratchBuffer, bufferSize, "%hs: <Unknown>", ErrorReasonText( red::eErrorReason_UnhandledException ) );
		}
	}
#endif
} } // red/err

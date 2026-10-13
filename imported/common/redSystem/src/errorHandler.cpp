/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"

#include "dbgUtils.h"
#include "utility.h"
#include "threads.h"
#include "errorHandler.h"
#include "applicationErrorDumper.h"

#include "../include/redVanguardVersion.h"

#if defined ( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
# include "errorHandlerImplErrorHooksWin32.h"
#elif defined( RED_PLATFORM_ORBIS )
# include "errorHandlerImplErrorHooksOrbis.h"
#elif defined( RED_PLATFORM_LINUX )
# include "errorHandlerImplErrorHooksLinux.h"
#else
# error Undefined platform!
#endif

namespace
{
	class ScriptStackFrameVisitor
	{
	public:
		ScriptStackFrameVisitor( Uint32& currentThreadId )
			: m_currentThreadId( currentThreadId )
		{}

		Bool operator()( Uint32 threadId, const red::AnsiChar* buffer, Uint32 bufferSize )
		{
			if ( m_currentThreadId != threadId )
			{
				m_currentThreadId = threadId;

				RED_LOG_INFO( "----------------------------------------" );
				const Bool isCurrentThread = red::ThreadId::CurrentThread().id == m_currentThreadId;
				RED_LOG_INFO( "Script Callstack (threadId=%lu%hs):", m_currentThreadId, ( isCurrentThread ? ", current" : "" ) );
			}

			RED_LOG( "\tScript Frame: %s", buffer );
			return true;
		}

	private:
		Uint32& m_currentThreadId;
	};

	void DumpScriptCallstackToLog( red::err::ErrorHandlerHooks::ScriptCallstackVisitorFunc* scriptCallstackVisitor )
	{
		if ( scriptCallstackVisitor )
		{
			Uint32 currentThreadId = 0;
			scriptCallstackVisitor( ScriptStackFrameVisitor( currentThreadId ) );

			if ( currentThreadId != 0 )
			{
				RED_LOG_INFO( "----------------------------------------" );
			}
		}
	}

	void DumpApplicationDataOnAssertion()
	{
		red::err::ApplicationDataDumpContext context;
		context.isAssertHandler = true;
		red::err::ApplicationErrorDumper::RunDumpers( context );
	}
}

#if !defined( RED_DLL ) && !defined( RED_WITH_DLL )
namespace dd
{
	static red::CrashData< Bool > s_oom{ "Engine", "OOM", false };
}
#endif

// Try to avoid C++11 magic thread safe statics or non POD types. Can rely on the CRT library being initialized before main, and then
// ctor could overwrite some already set value.
// #tbd: other platforms, but can't do much validation on consoles. E.g., attaching too big a file on the PS4
// will just fail silently. On Windows, this is mainly just to avoid trying to attach a ridiculously large file.
// Also, don't check upfront since the file doesn't have to exist yet. And not safe to get file size during PS4 crash handler.
// Xbox can now use WerRegisterFile, up to WER_MAX_REGISTERED_ENTRIES. But the file handle needs to be closed at crash time.
namespace red
{
#if !defined( RED_DLL ) && !defined( RED_WITH_DLL )
	thread_local Bool g_oom;
#endif

	namespace err
	{
		ErrorHandlerHooks RegisterErrorHooksOnce( Uint32 errorHandlerFlags = 0,
												  ErrorHandlerHooks::ScriptCallstackVisitorFunc* scriptCallstackVisitor = nullptr )
		{
			ErrorHandlerHooks result = {};
#ifdef RED_USE_ERRORHANDLER
	#if defined ( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
			result = RegisterErrorHooksOnceWin32( errorHandlerFlags, APP_VERSION_NUMBER, scriptCallstackVisitor );
	#elif defined( RED_PLATFORM_ORBIS )
			result = RegisterErrorHooksOnceOrbis( errorHandlerFlags, APP_VERSION_NUMBER, scriptCallstackVisitor );
	#elif defined( RED_PLATFORM_LINUX )
			result = RegisterErrorHooksOnceLinux( errorHandlerFlags, APP_VERSION_NUMBER, scriptCallstackVisitor );
	#else
	#error Undefined platform!
	#endif
#endif
			return result;
		}
	}

	// #tbd: move this somewhere else, then don't include errorHandler.h in errorHandlerImplMessages.cpp
	// But currently also need in common with the win32 crash reporter
	const char* ErrorReasonText( red::EErrorReason errorReason )
	{
		switch ( errorReason )
		{
		case red::eErrorReason_PureCall:			return "Pure virtual call";
		case red::eErrorReason_Assert:				return "Assert";
		case red::eErrorReason_Abort:				return "Abort";
		case red::eErrorReason_CrtError:			return "CRT error";
		case red::eErrorReason_UnhandledException:	return "Unhandled exception";
		case red::eErrorReason_InvalidParameter:	return "Invalid parameter";
		case red::eErrorReason_OutOfMemory:			return "Out of memory";
		default:
			break;
		}
		return "<Unknown error reason>";
	}

	void InitErrorHandler( Uint32 errorHandlerFlags, ScriptCallstackVisitorFunc* scriptCallstackVisitor/* = nullptr*/ )
	{
#ifdef RED_USE_ERRORHANDLER
		(void)err::RegisterErrorHooksOnce( errorHandlerFlags, scriptCallstackVisitor );
#endif
	}

	// Entry point must be noinline for the return address to make sense. Explicit noinline especially in case refactored to call some other helper functions.
	RED_NOINLINE
	void HandleAssert( const ErrorMessage& customErrorMsg )
	{
#if !defined( RED_DLL ) && !defined( RED_WITH_DLL )
		if( red::g_oom )
		{
			dd::s_oom.Set( true );
		}
#endif

#ifdef RED_USE_ERRORHANDLER
		// The assert macro will handle the debug break; break at point of assert failure
		// It will then abort since they're all fatal now.
		const auto hooks = err::RegisterErrorHooksOnce();

		DumpScriptCallstackToLog( hooks.fnScriptCallstackVisitor );
		DumpApplicationDataOnAssertion();

		hooks.fnAssertHandler( customErrorMsg );
#endif
	}

	Bool RegisterAttachmentForErrorReport( const char* pathToRegister )
	{
#ifdef RED_USE_ERRORHANDLER
		const auto hooks = err::RegisterErrorHooksOnce();
		hooks.fnRegisterAttachment( pathToRegister );
#endif
		return true;
	}
	
	void FastFailAbortProcess()
	{
#ifdef RED_USE_ERRORHANDLER
		const auto hooks = err::RegisterErrorHooksOnce();

		hooks.fnFailFastAbortProcess();
#endif
	}

	red::ThreadId g_loggerThreadId;

	void SetLoggerThreadId( red::ThreadId tid )
	{
		g_loggerThreadId = tid;
	}
}


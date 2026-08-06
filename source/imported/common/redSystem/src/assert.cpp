/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "assert.h"
#include "dbgUtils.h"
#include "errorHandler.h"
#include "unitTestMode.h"

namespace red
{

namespace prv
{
	static Bool g_assertsDisabledForUnitTest = false;
	static thread_local Bool t_assertRecursionCheck = false;
	static atomic::TAtomic32 g_assertOnce = 0;

	// If HandleAssert() is a no-op, then don't need to allocate (relatively small) space for g_fmtMsg
#ifdef RED_USE_ERRORHANDLER
	static char g_fmtMsg[ 1024 ] = { '\0' };
#endif

	struct ScopedRecursionFlag
	{
	public:
		explicit ScopedRecursionFlag( Bool& flag )
		{
			m_flag = flag;
			m_flag = true;
		}

		~ScopedRecursionFlag()
		{
			m_flag = false;
		}

		Bool GetFlag() const { return m_flag; }

	private:
		Bool m_flag;
	};

	void DisableAssertsForUnitTest()
	{
		if ( !UnitTestMode() )
		{
			RED_DEBUG_BREAK();
			for(;;) {}
		}

		g_assertsDisabledForUnitTest = true;
	}

	void EnableAssertsForUnitTest()
	{
		if ( !UnitTestMode() )
		{
			RED_DEBUG_BREAK();
			for(;;) {}
		}

		g_assertsDisabledForUnitTest = false;
	}

	RED_NOINLINE
	REDSYSTEM_API void OnAssertFailed( const char* filename, Uint32 line, const char* expression )
	{
		OnAssertFailed( filename, line, expression, "" );
	}

	RED_NOINLINE
	STATIC_CHECK_USE_DECL
	void OnAssertFailed( const char* filename, Uint32 line, const char* expression, const char* message, ... )
	{
		if ( g_assertsDisabledForUnitTest )
		{
		// #tbd: Death tests not supported on other platforms (yet!)
#if defined( RED_PLATFORM_WINPC )
			// Give GTest what it expects to see.
			::ExitProcess( EXIT_FAILURE );
#elif defined( RED_PLATFORM_LINUX )
			::exit( EXIT_FAILURE );
#endif
		}

		if ( t_assertRecursionCheck )
		{
			RED_DBG_TRACE( "!! Recursive assertion detected !!");
			if ( dbgutils::IsDebuggerAttached() )
			{
				RED_DEBUG_BREAK();
			}
			return;
		}
		ScopedRecursionFlag scopedFlag( t_assertRecursionCheck );

		std::fprintf(
			stderr,
			"RED assertion failed: %s\n  at %s:%u\n",
			expression,
			filename,
			line );
		if ( message && message[0] )
		{
			std::fprintf( stderr, "  message: " );
			va_list diagnosticArgs;
			va_start( diagnosticArgs, message );
			std::vfprintf( stderr, message, diagnosticArgs );
			va_end( diagnosticArgs );
			std::fprintf( stderr, "\n" );
		}
		std::fflush( stderr );

#ifdef RED_USE_ERRORHANDLER
		if ( atomic::Exchange32( &g_assertOnce, 1 ) != 0 )
		{
			for ( ;; )
			{
				// Spin until the other thread aborts the process. Locking g_fmtMsg, since the message isn't passed to the error reporter until we call debugbreak and
				// trigger the unhandled exception filter. So the g_fmtMsg can't be on the stack since it needs to outlive this function. And TLS is wasteful.
				red::SleepOnCurrentThread( 1000 );
			}
		}

		{
			va_list args;
			va_start( args, message );
			red::VSNPrintF( g_fmtMsg, RED_ARRAY_COUNT_U32( g_fmtMsg ), message, args );
			va_end( args );
		}

		ErrorMessage msg;
		{
			msg.m_file = filename;
			msg.m_expression = expression;
			msg.m_message = g_fmtMsg;
			msg.m_line = line;
		}

		red::HandleAssert( msg );
#endif // RED_USE_ERRORHANDLER
	}
}
}

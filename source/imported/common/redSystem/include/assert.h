/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "settings.h"
#include "errorHandler.h"
#include "dbgUtils.h"

//////////////////////////////////////////////////////////////////////////

// DO NOT COPY PASTE CODE FROM HERE. IT MAKES IT HARD TO MAINTAIN CONSISTENCY
// If you want asserts on your own define, you can use ALWAYSENABLED_* macros
// If you need to hook asserts into some third party library, you can use alwaysEnabledFatalAssert()

//////////////////////////////////////////////////////////////////////////

namespace red
{
	namespace prv
	{
		extern REDSYSTEM_API void DisableAssertsForUnitTest();
		extern REDSYSTEM_API void EnableAssertsForUnitTest();
		extern REDSYSTEM_API void OnAssertFailed( const char* filename, Uint32 line, const char* expression, STATIC_CHECK_PRINTF_MSC const char* message, ... );
		extern REDSYSTEM_API void OnAssertFailed( const char* filename, Uint32 line, const char* expression );
	}
}

///---

#define INTERNAL_ALWAYSENABLED_RED_FATAL_ASSERT( expression, ... )\
	{\
		::red::prv::OnAssertFailed( __FILE__, __LINE__, #expression, ##__VA_ARGS__ );\
		RED_DEBUG_BREAK();\
		::red::FastFailAbortProcess();\
	}

#define ALWAYSENABLED_RED_FATAL_ASSERT( expression, ... )\
	do\
	{\
		RED_ANALYSIS_ASSUME( !!(expression) );\
		if( !( expression ) )\
		{\
			INTERNAL_ALWAYSENABLED_RED_FATAL_ASSERT( expression, ##__VA_ARGS__ )\
		}\
	}\
	while ( (void)0,0 )

#define ALWAYSENABLED_RED_FATAL( message, ... )\
	do\
	{\
		::red::prv::OnAssertFailed( __FILE__, __LINE__, "", message, ##__VA_ARGS__ );\
		RED_DEBUG_BREAK();\
		::red::FastFailAbortProcess();\
	}\
	while( (void)0,0 )

#define ALWAYSENABLED_RED_VERIFY( expression, ... )\
	do\
	{\
		RED_ANALYSIS_ASSUME( !!(expression) );\
		if( !( expression ) )\
		{\
			INTERNAL_ALWAYSENABLED_RED_FATAL_ASSERT( expression, ##__VA_ARGS__ )\
		}\
	}\
	while( (void)0,0 )

namespace red
{
	RED_FORCE_INLINE void alwaysEnabledFatalAssert(const char* file, int line, const char* condition, const char* message)
	{
		::red::prv::OnAssertFailed( file, line, condition, message );
		RED_DEBUG_BREAK();
		::red::FastFailAbortProcess();
	}
}

#ifdef RED_ASSERTS_ENABLED
# define RED_FATAL_ASSERT	ALWAYSENABLED_RED_FATAL_ASSERT
# define RED_FATAL			ALWAYSENABLED_RED_FATAL
# define RED_VERIFY			ALWAYSENABLED_RED_VERIFY
# define RED_HALT			RED_FATAL
# define RED_ASSERT			RED_FATAL_ASSERT
#else
# define RED_ASSERT( ... )								do { } while ( (void)0,0 )
# define RED_HALT( ... )									do { } while ( (void)0,0 )
# define RED_FATAL_ASSERT( expression, ... )				do { } while ( (void)0,0 )
# define RED_FATAL( message, ... )                       do { } while ( (void)0,0 )
# define RED_VERIFY( expression, ... )                   do { if( !( expression ) ) {} } while ( (void)0,0 )
#endif

#define RED_STATIC_ASSERT( x ) static_assert( x, #x )

//////////////////////////////////////////////////////////////////////////

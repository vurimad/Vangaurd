/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_LOG_H_
#define _RED_SYSTEM_LOG_H_

#include "dataError.h"

namespace dbgutils
{
	struct StackTrace;
}

namespace red
{
	class LoggerSink;

	// update GetLoggerCategories when adding new category
	enum LoggerCategory
	{
		LoggerCategory_Default,
		LoggerCategory_AI,
		LoggerCategory_Animation,
		LoggerCategory_Audio,
		LoggerCategory_Engine,
		LoggerCategory_Gameplay,
		LoggerCategory_GameplayProfile,
		LoggerCategory_GameStateMachine,
		LoggerCategory_Physics,
		LoggerCategory_Rendering,
		LoggerCategory_Scripts,
		LoggerCategory_ScriptRuntimeErrors,
		LoggerCategory_Tools,
		LoggerCategory_Core,
		LoggerCategory_Interop,
		LoggerCategory_Input,
		LoggerCategory_TweakDB,
		LoggerCategory_Math,
		LoggerCategory_ItemFactory,
		LoggerCategory_Localization,
		LoggerCategory_UserInterface,
		LoggerCategory_ObjectPool,
		LoggerCategory_Scenes,
		LoggerCategory_DataProcessing,
		LoggerCategory_EntityValidation,
		LoggerCategory_Jobs,
		LoggerCategory_Navigation,
		LoggerCategory_Traffic,
		LoggerCategory_Population,
		LoggerCategory_PlayerLocomotion,
		LoggerCategory_Services,
		LoggerCategory_Resources,
		LoggerCategory_Multiplayer,
		LoggerCategory_Effects,
		LoggerCategory_Workspots,
		LoggerCategory_DistributedProcess,
		LoggerCategory_DataBuild,
		LoggerCategory_Entity,
		LoggerCategory_SmartObjects,
		LoggerCategory_Scanning,
		LoggerCategory_Interactions,
		LoggerCategory_Muppet,
		LoggerCategory_PingSystem,
		LoggerCategory_Chatter,
		LoggerCategory_ScreenshotTool,
		LoggerCategory_AIDirector,
		LoggerCategory_ResourceLookupTable,
		LoggerCategory_PlayerBreadcrumbs,
		LoggerCategory_FunctionalTests,
		LoggerCategory_Characters,
		LoggerCategory_GPS,
		LoggerCategory_PlayerManager,
		LoggerCategory_Mounting,
		LoggerCategory_Projectiles,
		LoggerCategory_Telemetry,
		LoggerCategory_Garment,

		LoggerCategory_MAX
	};

	struct LoggerCategoryPair
	{
		LoggerCategory category;
		const char* name;
	};

	REDSYSTEM_API extern LoggerCategoryPair g_LoggerCategories[];

	enum LoggerMode : Uint8
	{
		LoggerMode_Async,
		LoggerMode_Sync
	};

	enum LoggerLevel : Uint8
	{
		LoggerLevel_Fatal,		// asserts and fatal errors are logged here
		LoggerLevel_Error,		// errors and normal asserts are logged here
		LoggerLevel_Warning,	// warnings
		LoggerLevel_Info,		// non warning information (DEFAULT)
		LoggerLevel_Debug,		// non essential debugging information
		LoggerLevel_Trace,		// total spam
	};

	enum LoggerFlushMode : Uint8
	{
		LoggerFlushMode_Async, // request to flush log sinks from the worker thread (or sync if LoggerMode_Sync)
		LoggerFlushMode_Sync, // will wait for the calling thread until the work has flushed the log sinks
	};

	using FrameNumberRetriever = Uint64 (*)();

	REDSYSTEM_API void InitializeLogger( LoggerMode mode = LoggerMode_Async, const char * prefix = nullptr );
	REDSYSTEM_API void UninitializeLogger();

	REDSYSTEM_API void RegisterLoggerSink( LoggerSink * sink );
	REDSYSTEM_API void RegisterFilteredLoggerSink( LoggerSink * sink );
	REDSYSTEM_API void UnregisterLoggerSink( LoggerSink * sink );
	REDSYSTEM_API void UnregisterFilteredLoggerSink( LoggerSink * sink );

	REDSYSTEM_API void EnableLogging();
	REDSYSTEM_API void DisableLogging();
	REDSYSTEM_API bool IsLoggingEnabled();

	REDSYSTEM_API void SetLogLevel( LoggerLevel level );
	REDSYSTEM_API void RestoreDefaultLogLevel();

	REDSYSTEM_API void EnableLogCategory( LoggerCategory category, Bool enable );

	REDSYSTEM_API void SetLogThreadContextId( Uint32 contextid );
	REDSYSTEM_API void SetLogDefaultThreadContextId();

	REDSYSTEM_API void SetFrameNumberRetriever( FrameNumberRetriever frameNumberRetriever );

	REDSYSTEM_API void SetLogNetPeerId( Uint32 netPeerId );
	REDSYSTEM_API void SetLogDefaultNetPeerId();

	REDSYSTEM_API Bool CanPrintLog( LoggerLevel level, LoggerCategory category );
	REDSYSTEM_API void LogLine( LoggerLevel level, LoggerCategory category, STATIC_CHECK_PRINTF_MSC const char * line, ... );
	REDSYSTEM_API void LogMessage( LoggerLevel level, const char * message, LoggerCategory category = LoggerCategory_Default );
	REDSYSTEM_API void LogLine( LoggerLevel level, LoggerCategory category, const dbgutils::StackTrace& stackTrace, STATIC_CHECK_PRINTF_MSC const char * line, ... );

	REDSYSTEM_API void LogDataError( LoggerLevel level, LoggerCategory category, const red::DataError& dataError );

	REDSYSTEM_API void LogFlush( LoggerFlushMode flushMode );
}

#ifdef RED_LOGGING_ENABLED

#define INTERNAL_RED_LOG( level, category, message, ... )				do { if( red::CanPrintLog( level, category ) ) { red::LogLine( level, category, message, ##__VA_ARGS__ ); } } while ( (void)0,0 ) 

#define RED_LOG( message, ... )											INTERNAL_RED_LOG( red::LoggerLevel_Info, red::LoggerCategory_Default, message, ##__VA_ARGS__ )
#define RED_LOG_INFO( message, ... )									INTERNAL_RED_LOG( red::LoggerLevel_Info, red::LoggerCategory_Default, message, ##__VA_ARGS__ )
#define RED_LOG_WARNING( message, ... )									INTERNAL_RED_LOG( red::LoggerLevel_Warning, red::LoggerCategory_Default, message, ##__VA_ARGS__ )
#define RED_LOG_ERROR( message, ... )									INTERNAL_RED_LOG( red::LoggerLevel_Error, red::LoggerCategory_Default, message, ##__VA_ARGS__ )
#define RED_LOG_SPAM( message, ... )									INTERNAL_RED_LOG( red::LoggerLevel_Trace, red::LoggerCategory_Default, message, ##__VA_ARGS__ )
#define RED_LOG_SPAM_RETURN_FALSE( message, ... )						{ INTERNAL_RED_LOG( red::LoggerLevel_Trace, red::LoggerCategory_Default, message, ##__VA_ARGS__ ); return false; }
#define RED_LOG_TRACE( message, ... )									INTERNAL_RED_LOG( red::LoggerLevel_Trace, red::LoggerCategory_Default, message, ##__VA_ARGS__ )
#define RED_LOG_DEBUG( message, ... )									INTERNAL_RED_LOG( red::LoggerLevel_Debug, red::LoggerCategory_Default, message, ##__VA_ARGS__ )
#define RED_LOG_ERROR_WITH_STACKTRACE( stackTrace, message, ... )		INTERNAL_RED_LOG( red::LoggerLevel_Error, red::LoggerCategory_Default, stackTrace, message, ##__VA_ARGS__ )

#define RED_LOG_FLUSH()													red::LogFlush( red::LoggerFlushMode_Async )
#define RED_LOG_FLUSH_AND_WAIT()										red::LogFlush( red::LoggerFlushMode_Sync )

#define RED_LOG_CATEGORY( category, message, ... )						INTERNAL_RED_LOG( red::LoggerLevel_Info, category, message, ##__VA_ARGS__ )
#define RED_LOG_CATEGORY_INFO( category, message, ... )					INTERNAL_RED_LOG( red::LoggerLevel_Info, category, message, ##__VA_ARGS__ )
#define RED_LOG_CATEGORY_WARNING( category, message, ... )				INTERNAL_RED_LOG( red::LoggerLevel_Warning, category, message, ##__VA_ARGS__ )
#define RED_LOG_CATEGORY_ERROR( category, message, ... )				INTERNAL_RED_LOG( red::LoggerLevel_Error, category, message, ##__VA_ARGS__ )
#define RED_LOG_CATEGORY_SPAM( category, message, ... )					INTERNAL_RED_LOG( red::LoggerLevel_Trace, category, message, ##__VA_ARGS__ )
#define RED_LOG_CATEGORY_SPAM_RETURN_FALSE( category, message, ... )	{ INTERNAL_RED_LOG( red::LoggerLevel_Trace, category, message, ##__VA_ARGS__ ); return false; }
#define RED_LOG_CATEGORY_TRACE( category, message, ... )				INTERNAL_RED_LOG( red::LoggerLevel_Trace, category, message, ##__VA_ARGS__ )
#define RED_LOG_CATEGORY_DEBUG( category, message, ... )				INTERNAL_RED_LOG( red::LoggerLevel_Debug, category, message, ##__VA_ARGS__ )

#define RED_LOG_CATEGORY_LEVEL( category, level, message, ... )			INTERNAL_RED_LOG( level, category, message, ##__VA_ARGS__ )

#define RED_WARNING( expression, message, ... )							do { if( !( expression ) ) { RED_LOG_WARNING( #expression ": " message, ##__VA_ARGS__ ); } } while ( (void)0,0 )
#define RED_WARNING_CATEGORY( category, expression, message, ... )		do { if( !( expression ) ) { RED_LOG_CATEGORY_WARNING( category, #expression ": " message, ##__VA_ARGS__ ); } } while ( (void)0,0 )
#define RED_WARNING_ONCE( expression, message, ... )					do { static Bool warnOnce = false; if( !warnOnce && !( expression ) ) { warnOnce = true; RED_LOG_ERROR( #expression ": " message, ##__VA_ARGS__ ); } } while ( (void)0,0 )
#define RED_WARNING_CATEGORY_ONCE( category, expression, message, ... ) do { static Bool warnOnce = false; if( !warnOnce && !( expression ) ) { warnOnce = true; RED_LOG_CATEGORY_WARNING( category, #expression ": " message, ##__VA_ARGS__ ); } } while ( (void)0,0 )

#define RED_ERROR( expression, message, ... )							do { if( !( expression ) ) { RED_LOG_ERROR( #expression ": " message, ##__VA_ARGS__ ); } } while ( (void)0,0 )
#define RED_ERROR_CATEGORY( category, expression, message, ... )		do { if( !( expression ) ) { RED_LOG_CATEGORY_ERROR( category, #expression ": " message, ##__VA_ARGS__ ); } } while ( (void)0,0 )
#define RED_ERROR_ONCE( expression, message, ... )						do { static Bool errorOnce = false; if( !errorOnce && !( expression ) ) { errorOnce = true; RED_LOG_ERROR( #expression ": " message, ##__VA_ARGS__ ); } } while ( (void)0,0 )
#define RED_ERROR_CATEGORY_ONCE( category, expression, message, ... )	do { static Bool errorOnce = false; if( !errorOnce && !( expression ) ) { errorOnce = true; RED_LOG_CATEGORY_ERROR( category, #expression ": " message, ##__VA_ARGS__ ); } } while ( (void)0,0 )

#define RED_ENABLE_LOGGING()											red::EnableLogging();
#define RED_DISABLE_LOGGING()											red::DisableLogging();

#else
	
#define RED_LOG( message, ... ) do {} while( (void)0,0 )
#define RED_LOG_INFO( message, ... ) do {} while( (void)0,0 )
#define RED_LOG_WARNING( message, ...  ) do {} while( (void)0,0 )
#define RED_LOG_ERROR( message, ...  ) do {} while( (void)0,0 )
#define RED_LOG_SPAM( message, ...  ) do {} while( (void)0,0 )
#define RED_LOG_SPAM_RETURN_FALSE( message, ...  ){ return false; }
#define RED_LOG_TRACE( message, ... ) do {} while( (void)0,0 )
#define RED_LOG_DEBUG( message, ...	) do {} while( (void)0,0 )
#define RED_LOG_ERROR_WITH_STACKTRACE( stackTrace, message, ... ) do {} while( (void)0,0 )

#define RED_LOG_FLUSH() do {} while( (void)0,0 )
#define RED_LOG_FLUSH_AND_WAIT() do {} while( (void)0,0 )

#define RED_LOG_CATEGORY( category, message, ... ) do {} while( (void)0,0 )
#define RED_LOG_CATEGORY_INFO( category, message, ... ) do {} while( (void)0,0 )
#define RED_LOG_CATEGORY_WARNING( category, message, ... ) do {} while( (void)0,0 )
#define RED_LOG_CATEGORY_ERROR( category, message, ... ) do {} while( (void)0,0 )
#define RED_LOG_CATEGORY_SPAM( category, message, ... ) do {} while( (void)0,0 )
#define RED_LOG_CATEGORY_SPAM_RETURN_FALSE( category, message, ... ) {return false; }
#define RED_LOG_CATEGORY_TRACE( category, message, ... ) do {} while( (void)0,0 )
#define RED_LOG_CATEGORY_DEBUG( category, message, ... ) do {} while( (void)0,0 )

#define RED_LOG_CATEGORY_LEVEL( category, level, message, ... ) do {} while( (void)0,0 )

#define RED_WARNING( expression, message, ... ) do {} while( (void)0,0 )
#define RED_WARNING_CATEGORY( category, expression, message, ... ) do {} while( (void)0,0 )
#define RED_WARNING_ONCE( expression, message, ... ) do {} while( (void)0,0 )
#define RED_WARNING_CATEGORY_ONCE( category, expression, message, ... ) do {} while( (void)0,0 )

#define RED_ERROR( expression, message, ... ) do {} while( (void)0,0 )
#define RED_ERROR_CATEGORY( category, expression, message, ... ) do {} while( (void)0,0 )
#define RED_ERROR_ONCE( expression, message, ... ) do {} while( (void)0,0 )
#define RED_ERROR_CATEGORY_ONCE( category, expression, message, ... ) do {} while( (void)0,0 )

#define RED_ENABLE_LOGGING() do {} while( (void)0,0 )
#define RED_DISABLE_LOGGING() do {} while( (void)0,0 )

#endif // RED_LOGGING_ENABLED

#endif

/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "logger.h"
#include "loggerSink.h"
#include "log.h"
#include "file.h"
#include "dbgUtils.h"
#include "../include/scopedPtr.h"

#include <utility>
#include <algorithm>

namespace red 
{
	#define LOGGER_CATEGORY(x) { ::red::LoggerCategory_##x, #x }

	REDSYSTEM_API LoggerCategoryPair g_LoggerCategories[]
	{
		LOGGER_CATEGORY(Default),
		LOGGER_CATEGORY(AI),
		LOGGER_CATEGORY(Animation),
		LOGGER_CATEGORY(Audio),
		LOGGER_CATEGORY(Engine),
		LOGGER_CATEGORY(Gameplay),
		LOGGER_CATEGORY(GameplayProfile),
		LOGGER_CATEGORY(GameStateMachine),
		LOGGER_CATEGORY(Physics),
		LOGGER_CATEGORY(Rendering),
		LOGGER_CATEGORY(Scripts),
		LOGGER_CATEGORY(ScriptRuntimeErrors),
		LOGGER_CATEGORY(Tools),
		LOGGER_CATEGORY(Core),
		LOGGER_CATEGORY(Interop),
		LOGGER_CATEGORY(Input),
		LOGGER_CATEGORY(TweakDB),
		LOGGER_CATEGORY(Math),
		LOGGER_CATEGORY(ItemFactory),
		LOGGER_CATEGORY(Localization),
		LOGGER_CATEGORY(UserInterface),
		LOGGER_CATEGORY(ObjectPool),
		LOGGER_CATEGORY(Scenes),
		LOGGER_CATEGORY(DataProcessing),
		LOGGER_CATEGORY(EntityValidation),
		LOGGER_CATEGORY(Jobs),
		LOGGER_CATEGORY(Navigation),
		LOGGER_CATEGORY(Population),
		LOGGER_CATEGORY(Traffic),
		LOGGER_CATEGORY(PlayerLocomotion),
		LOGGER_CATEGORY(Services),
		LOGGER_CATEGORY(Resources),
		LOGGER_CATEGORY(Multiplayer),
		LOGGER_CATEGORY(Effects),
		LOGGER_CATEGORY(Workspots),
		LOGGER_CATEGORY(DistributedProcess),
		LOGGER_CATEGORY(DataBuild),
		LOGGER_CATEGORY(Entity),
		LOGGER_CATEGORY(SmartObjects),
		LOGGER_CATEGORY(Scanning),
		LOGGER_CATEGORY(Interactions),
		LOGGER_CATEGORY(Muppet),
		LOGGER_CATEGORY(PingSystem),
		LOGGER_CATEGORY(Chatter),
		LOGGER_CATEGORY(ScreenshotTool),
		LOGGER_CATEGORY(AIDirector),
		LOGGER_CATEGORY(ResourceLookupTable),
		LOGGER_CATEGORY(PlayerBreadcrumbs),
		LOGGER_CATEGORY(FunctionalTests),
		LOGGER_CATEGORY(Characters),
		LOGGER_CATEGORY(GPS),
		LOGGER_CATEGORY(PlayerManager),
		LOGGER_CATEGORY(Mounting),
		LOGGER_CATEGORY(Projectiles),
		LOGGER_CATEGORY(Telemetry),
		LOGGER_CATEGORY(Garment),
	};

	static_assert( RED_ARRAY_COUNT( g_LoggerCategories ) == LoggerCategory_MAX, "Please update g_LoggerCategories with your added/removed category" );

	class TTYSink : public LoggerSink
	{
	public:

		virtual void SinkLogLine( const char * formattedMessage, const LoggerLine & message ) override final;
		virtual void Flush() override final;
	};

	void TTYSink::SinkLogLine( const char * formattedMessage, const LoggerLine &  )
	{
#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
		red::FilePrint( stdout, formattedMessage );
#else
		::OutputDebugStringA( formattedMessage );
#endif
	}

	void TTYSink::Flush()
	{}

	TTYSink s_ttySink;

	namespace helper
	{
		Logger* CreateSystemLogger()
		{
			static Logger* s_logger = ::new ( std::malloc( sizeof( Logger ) ) ) Logger;
			return s_logger;
		}

		Logger* GetOrCreateSystemLogger( Bool createLogger )
		{
			static Logger* s_loggerPtr = nullptr;
			if( !s_loggerPtr && createLogger )
			{
				s_loggerPtr = CreateSystemLogger();
			}

			return s_loggerPtr;
		}
	}

	Logger* GetSystemLoggerSafe()
	{
		return helper::GetOrCreateSystemLogger( false );
	}

	Logger& GetSystemLogger()
	{
		return *helper::GetOrCreateSystemLogger( true );
	}

	void InitializeLogger( LoggerMode mode, const char * prefix )
	{
		auto& logger = GetSystemLogger();
#ifdef RED_PLATFORM_WINPC
		if( dbgutils::IsDebuggerAttached() ) // ctremblay: OutputDebugString is the devil. It can cause massive slow down.
		{
			logger.RegisterSink( &s_ttySink );
		}
#else
		logger.RegisterSink( &s_ttySink );
#endif

		logger.Initialize( mode, prefix );
	}

	void UninitializeLogger()
	{
		auto& logger = GetSystemLogger();
		logger.Uninitialize();
		logger.UnregisterSink( &s_ttySink );
	}

	void RegisterLoggerSink( LoggerSink * sink )
	{
		GetSystemLogger().RegisterSink( sink );
	}

	void RegisterFilteredLoggerSink( LoggerSink * sink )
	{
		GetSystemLogger().RegisterFilteredSink( sink );
	}

	void UnregisterLoggerSink( LoggerSink * sink )
	{
		GetSystemLogger().UnregisterSink( sink );
	}

	void UnregisterFilteredLoggerSink( LoggerSink * sink )
	{
		GetSystemLogger().UnregisterFilteredSink( sink );
	}
	
	void EnableLogging()
	{
		GetSystemLogger().Enable();
	}
	
	void DisableLogging()
	{
		GetSystemLogger().Disable();
	}

	bool IsLoggingEnabled()
	{
		return GetSystemLogger().IsEnabled();
	}

	void SetLogLevel( LoggerLevel level )
	{
		GetSystemLogger().SetLevel( level );
	}

	void RestoreDefaultLogLevel( )
	{
		GetSystemLogger().RestoreDefaultLevel();
	}

	void EnableLogCategory( LoggerCategory category, Bool enable )
	{
		GetSystemLogger().ToggleLoggerCategory(category, enable);
	}

	void SetLogThreadContextId( Uint32 contextid )
	{
		GetSystemLogger().SetThreadContextId( contextid );
	}

	void SetLogDefaultThreadContextId()
	{
		GetSystemLogger().SetDefaulftThreadContextId();
	}

	void SetFrameNumberRetriever( FrameNumberRetriever frameNumberRetriever )
	{
		GetSystemLogger().SetFrameNumberRetriever( frameNumberRetriever );
	}

	void SetLogNetPeerId( Uint32 netPeerId )
	{
		GetSystemLogger().SetNetPeerId( netPeerId );
	}

	void SetLogDefaultNetPeerId()
	{
		GetSystemLogger().SetDefaulftNetPeerId();
	}

	Bool CanPrintLog( LoggerLevel level, LoggerCategory category )
	{
		return GetSystemLogger().CanPrint( level, category );
	}

	STATIC_CHECK_USE_DECL
	void LogLine( LoggerLevel level, LoggerCategory category, const char * line, ... )
	{
		va_list arglist;
		va_start( arglist, line );
		GetSystemLogger().PushLine( level, category, line, arglist );
		va_end( arglist );
	}

	void LogDataError( LoggerLevel level, LoggerCategory category, const red::DataError& dataError )
	{
		GetSystemLogger().PushDataError( level, category, dataError );
	}

	// Mostly for Assertion logging... Will wrap in multiple line for each \n. 
	// If message is longer than logger line and no return carriage, it will truncate. 
	void LogMessage( LoggerLevel level, const char * message, LoggerCategory category )
	{
		auto& logger = GetSystemLogger();
		if ( !logger.CanPrint( level, category ) )
		{
			return;
		}

		const char * currentLine = message;
		while( const char * nextLine = Strchr( currentLine, '\n' ) )
		{
			const Uint32 currentLineSize = std::min( static_cast< Uint32 >( nextLine - currentLine ), c_loggerLineLength - 1 );

			LogLine( level, category, "%.*s", currentLineSize, currentLine );

			currentLine = nextLine + 1;
		}

		LogLine( level, category, "%s", currentLine );
	}

	void LogFlush( LoggerFlushMode flushMode )
	{
		GetSystemLogger().PushFlush( flushMode );
	}
}

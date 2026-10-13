/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "log.h"
#include "logger.h"
#include "loggerSink.h"
#include "dataError.h"
#include "threads.h"
#include <utility>
#include <algorithm>

static const Uint32 DEFAULT_CONTEXT_ID = 0;
static const Uint32 DEFAULT_NETWORK_PEER_ID = 0xFFFFFFFF;

namespace
{
	Uint64 DefaultFrameNumberRetriever()
	{
		return 0;
	}
}

namespace dd
{
	struct LoggerCrashData
	{
		red::CrashData<const char*> loggerMode{ "Logger", "Mode" };
	};

	static LoggerCrashData& GetCrashData()
	{
		static LoggerCrashData data;
		return data;
	}
}

namespace red
{
	template class REDSYSTEM_API LoggerLocklessQueue< LoggerLine >;
	template class REDSYSTEM_API LoggerLocklessQueue< LoggerLineWithDataError >;

	const Uint32 c_loggerFormattedMessageHeader = 256; //< The value should be large enough to hold the entire header added to LoggerLine.buffer.
	const Uint32 c_loggerFormattedMessage = red::c_loggerLineLength + c_loggerFormattedMessageHeader;
	const Uint32 c_loggerSpinTimeMS = 1;
	const Uint32 c_loggerYieldTimeMS = 10;

	static const LoggerLevel c_defaultLoggerLevel = LoggerLevel_Info;
	thread_local static Uint32 s_threadContextId = DEFAULT_CONTEXT_ID;
	static Uint32 s_netPeerId = DEFAULT_NETWORK_PEER_ID;

	const char * c_loggerLevelString[] =
	{
		"[Fatal]",
		"[Error]",
		"[Warning]",
		"[Info]",
		"[Debug]",
		"[Trace]"
	};

	Logger::Logger()
		:	m_isEnabled( false ),
			m_isInitialized( false ),
			m_level( c_defaultLoggerLevel ),
			m_categoryFilter( std::numeric_limits<Uint64>::max() ),
			m_lastConsumedMessageTime( std::chrono::system_clock::now() ),
			m_frameNumberRetriever( DefaultFrameNumberRetriever ),
			m_worker( nullptr )

	{
		red::Memzero( m_prefix, sizeof( m_prefix ) );

		std::memset( m_sinks, 0, sizeof( m_sinks ) );
		std::memset( m_filteredSinks, 0, sizeof( m_filteredSinks ) );
		m_worker = &m_workerStorage;
	}

	Logger::~Logger()
	{}

	void Logger::RestoreDefaultLevel()
	{
		m_level = c_defaultLoggerLevel;
	}

	void Logger::Initialize( LoggerMode mode, const char * prefix )
	{
		m_isEnabled = true;
		m_mode = mode;
		if ( prefix )
		{
			red::Strcpy( m_prefix, prefix, RED_ARRAY_COUNT( m_prefix ) );
		}

		dd::GetCrashData().loggerMode.Set(mode == LoggerMode_Async ? "Async" : "Sync");

		// Non Async mode is kinda an Hack, for script unittest ... Full sync support could be done if needed.
		if( m_mode == LoggerMode_Async )
		{
			m_worker->Start( this ); // TODO, should not pass "this" pointer.
		}

		m_isInitialized = true;
	}

	void Logger::Uninitialize()
	{
		if (m_isInitialized)
		{
			PushFlush( LoggerFlushMode_Async ); // async since still waiting for the worker thread
			m_isEnabled = false;

			// Non Async mode is kinda an Hack, for script unittest ... Full sync support could be done if needed.
			if( m_mode == LoggerMode_Async )
			{
				m_worker->Stop();
			}

			red::Memzero( m_prefix, sizeof( m_prefix ) );

			m_isInitialized = false;
		}
	}

	STATIC_CHECK_USE_DECL
	void Logger::PushLine( LoggerLevel level, LoggerCategory category, const char * message, ... )
	{
		if ( CanPrint(level, category) )
		{
			va_list arglist;
			va_start( arglist, message );
			PushLine( level, category, message, arglist );
			va_end( arglist );
		}
	}

	void Logger::PushLine( LoggerLevel level, LoggerCategory category, STATIC_CHECK_PRINTF_MSC const char * message, va_list arglist )
	{
		if ( CanPrint(level, category) )
		{
			LoggerLine logMessage
			{
				m_frameNumberRetriever(),
				std::chrono::system_clock::now(),
				m_prefix[0] ? m_prefix : nullptr,
				red::ThreadId::CurrentThread().AsNumber(),
				s_threadContextId,
				s_netPeerId,
				LoggerLineType_Log,
				level,
				category
			};

			red::VSNPrintF( logMessage.buffer, sizeof( logMessage.buffer ), message, arglist );
			QueueMessage( std::move( logMessage ) );

			// Non Async mode is kinda an Hack, for script unittest ... Full sync support could be done if needed.
			if( m_mode == LoggerMode_Sync )
			{
				ConsumeNextMessage();
			}
		}
	}

	void Logger::PushDataError( LoggerLevel level, LoggerCategory category, const DataError& dataError )
	{
		LoggerLineWithDataError logMessage
		{
			{
				m_frameNumberRetriever(),
				std::chrono::system_clock::now(),
				m_prefix[0] ? m_prefix : nullptr,
				red::ThreadId::CurrentThread().AsNumber(),
				s_threadContextId,
				s_netPeerId,
				LoggerLineType_DataError,
				level,
				category
			}
		};

		RED_FATAL_ASSERT( dataError.GetDataError() != nullptr, "Data error does not exist" );
		RED_FATAL_ASSERT( dataError.GetDataErrorActions() != nullptr, "Data error does not exist" );
		red::Strcpy( logMessage.line.buffer, dataError.GetDataError(), sizeof( logMessage.line.buffer ) );
		red::Strcpy( logMessage.dataErrorActions, dataError.GetDataErrorActions(), sizeof( logMessage.dataErrorActions ) );
		logMessage.dataErrorMessageFormatHash = dataError.GetMessageFormatHash();

		QueueDataError( std::move( logMessage ) );

		// Non Async mode is kinda an Hack, for script unittest ... Full sync support could be done if needed.
		if ( m_mode == LoggerMode_Sync )
		{
			ConsumeNextMessage();
		}
	}

	void Logger::PushFlush( LoggerFlushMode flushMode )
	{
		if( m_isEnabled )
		{
			const Uint64 flushFence = m_flushFenceCounter.GetValue();
			LoggerLine logMessage =
			{
				m_frameNumberRetriever(),
				std::chrono::system_clock::now(),
				m_prefix[0] ? m_prefix : nullptr,
				red::ThreadId::CurrentThread().AsNumber(),
				s_threadContextId,
				s_netPeerId,
				LoggerLineType_Flush,
				LoggerLevel_Info,
				LoggerCategory_Default
			};

			QueueMessage( std::move( logMessage ) );

			// Non Async mode is kinda an Hack, for script unittest ... Full sync support could be done if needed.
			if( m_mode == LoggerMode_Sync )
			{
				ConsumeNextMessage();
			}
			else if ( flushMode == LoggerFlushMode_Sync )
			{
				std::chrono::system_clock::time_point lastOperation = std::chrono::system_clock::now();
				while ( flushFence == m_flushFenceCounter.GetValue() )
				{
					Wait( lastOperation );
				}
			}
		}
	}

	Bool Logger::TryPushFlush(LoggerFlushMode flushMode)
	{
		if (m_isEnabled)
		{
			Bool flushFence = false;
			LoggerLine logMessage =
			{
				m_frameNumberRetriever(),
				std::chrono::system_clock::now(),
				m_prefix[0] ? m_prefix : nullptr,
				red::ThreadId::CurrentThread().AsNumber(),
				s_threadContextId,
				s_netPeerId,
				LoggerLineType_Flush,
				LoggerLevel_Info,
				LoggerCategory_Default
			};

			if (!TryQueueMessage(std::move(logMessage)))
			{
				return false;
			}

			// Non Async mode is kinda an Hack, for script unittest ... Full sync support could be done if needed.
			if (m_mode == LoggerMode_Sync)
			{
				ConsumeNextMessage();
			}
			else if (flushMode == LoggerFlushMode_Sync)
			{
				std::chrono::system_clock::time_point lastOperation = std::chrono::system_clock::now();
				while (!const_cast<volatile Bool&>(flushFence))
				{
					Wait(lastOperation);
				}
			}
		}

		return true;
	}

	void Logger::QueueMessage(LoggerLine && message)
	{
		if( !m_loggerLineQueue.QueueMessage( std::move( message ) ) )
		{
			// Queue is full!
			std::chrono::system_clock::time_point lastOperation = std::chrono::system_clock::now();

			do
			{
				Wait( lastOperation );
			}
			while( !m_loggerLineQueue.QueueMessage( std::move( message ) ) );
		}
	}

	Bool Logger::TryQueueMessage( LoggerLine&& message)
	{
		return m_loggerLineQueue.QueueMessage( std::move( message ) ); // <-- pointless to move and wouldn't even be correct if it actually moved anything
	}

	void Logger::QueueDataError(LoggerLineWithDataError && message)
	{
		if( !m_loggerLineWithDataErrorQueue.QueueMessage( std::move( message ) ) )
		{
			// Queue is full!
			std::chrono::system_clock::time_point lastOperation = std::chrono::system_clock::now();

			do
			{
				Wait( lastOperation );
			}
			while( !m_loggerLineWithDataErrorQueue.QueueMessage( std::move( message ) ) );
		}
	}

	void FormatLogMessage( const LoggerLine & message, char * buffer, Uint32 size )
	{
		auto time = std::chrono::system_clock::to_time_t( message.time );
		std::tm localTime;
#ifdef RED_COMPILER_MSC
		localtime_s(&localTime, &time);
#else
#	if defined( RED_PLATFORM_LINUX )
		localtime_r(&time, &localTime);
#	else
		localtime_s(&time, &localTime);
#	endif
#endif

		auto bufferPos = strftime( buffer, size, "[%Y.%m.%d %X]", &localTime );

		if ( message.prefix != nullptr )
		{
			// Print stringId when defined.
			auto ret = red::SNPrintFUnsafe( buffer + bufferPos, size - bufferPos, "[%s]", message.prefix );
			RED_ASSERT( ret > 0 );
			bufferPos += ret;
		}

		if ( message.threadContextId != DEFAULT_CONTEXT_ID )
		{
			// Print TCID only if some has set specific one.
			auto ret = red::SNPrintFUnsafe( buffer + bufferPos, size - bufferPos, "[TCID:%d]", message.threadContextId );
			RED_ASSERT( ret > 0 );
			bufferPos += ret;
		}

		if ( message.netPeerId != DEFAULT_NETWORK_PEER_ID &&
			 message.netPeerId != 0 ) // host always has an id = 0
		{
			// Print network id only in multiplayer game and only for clients.
			auto ret = red::SNPrintFUnsafe( buffer + bufferPos, size - bufferPos, "[PeerID:%u]", message.netPeerId );
			RED_ASSERT( ret > 0 );
			bufferPos += ret;
		}

		Strcat( buffer, c_loggerLevelString[ message.level ], size );
		Strcat( buffer, " ", size );
		Strcat( buffer, message.buffer, size );
		Strcat( buffer, "\n", size );
	}

	bool Logger::ConsumeNextMessage()
	{
		bool result = false;
		{
			LoggerLine message;
			if( m_loggerLineQueue.DequeueMessage( message ) )
			{
				m_lastConsumedMessageTime = std::chrono::system_clock::now();
				message.type == LoggerLineType_Log ? ConsumeLogMessage( message ) : ConsumeFlushMessage();
				result = true;
			}
		}
		{
			LoggerLineWithDataError dataError;
			if ( m_loggerLineWithDataErrorQueue.DequeueMessage( dataError ) )
			{
				m_lastConsumedMessageTime = std::chrono::system_clock::now();
				dataError.line.type == LoggerLineType_DataError ? ConsumeDataError( dataError ) : ConsumeFlushMessage();
				result = true;
			}
		}

		if ( !result )
		{
			Wait( m_lastConsumedMessageTime );
		}

		return result;
	}

	void Logger::ConsumeLogMessage( const LoggerLine & message )
	{
		char formattedMessage[ c_loggerFormattedMessage ];

		FormatLogMessage( message, formattedMessage, sizeof( formattedMessage ) );
		SinkMessage( formattedMessage, message );
	}

	void Logger::ConsumeDataError( const LoggerLineWithDataError & message )
	{
		char formattedMessage[ c_loggerFormattedMessage ];

		FormatLogMessage( message.line, formattedMessage, sizeof( formattedMessage ) );
		SinkDataError( formattedMessage, message );
	}

	void Logger::ConsumeFlushMessage()
	{
		ScopedSharedLock< RWSpinLock > scopedLock( m_sinkLock );

		for( Uint32 index = 0; index != c_loggerSinkMaxCount; ++index )
		{
			if( !m_sinks[ index ] )
				break;

			m_sinks[ index ]->Flush();
		}

		m_flushFenceCounter.Increment();
	}

	void Logger::SinkMessage( const char * formattedMessage, const LoggerLine & message )
	{
		ScopedSharedLock< RWSpinLock > scopedLock( m_sinkLock );

		for( Uint32 index = 0; index != c_loggerSinkMaxCount; ++index )
		{
			if( !m_sinks[ index ] )
				break;

			m_sinks[ index ]->SinkLogLine( formattedMessage, message );
		}

		if ( m_categoryFilter & ( 1ull << message.category ) )
		{
			for ( Uint32 index = 0; index != c_loggerSinkMaxCount; ++index )
			{
				if ( !m_filteredSinks[index] )
					break;

				m_filteredSinks[index]->SinkLogLine( formattedMessage, message );
			}
		}
	}

	void Logger::SinkDataError( const char * formattedMessage, const LoggerLineWithDataError & message )
	{
		ScopedSharedLock< RWSpinLock > scopedLock( m_sinkLock );

		for( Uint32 index = 0; index != c_loggerSinkMaxCount; ++index )
		{
			if( !m_sinks[ index ] )
				break;

			m_sinks[ index ]->SinkLogLine( formattedMessage, message );
		}

		if ( m_categoryFilter & ( 1ull << message.line.category ) )
		{
			for ( Uint32 index = 0; index != c_loggerSinkMaxCount; ++index )
			{
				if ( !m_filteredSinks[index] )
					break;

				m_filteredSinks[index]->SinkLogLine( formattedMessage, message );
			}
		}
	}

	void Logger::Enable()
	{
		if (m_isInitialized)
		{
			m_isEnabled = true;
		}
	}

	void Logger::Disable()
	{
		if (m_isInitialized)
		{
			m_isEnabled = false;
		}
	}

	bool Logger::IsEnabled() const
	{
		return m_isEnabled;
	}

	void Logger::SetLevel( LoggerLevel level )
	{
		m_level = level;
	}

	void Logger::Wait( const std::chrono::system_clock::time_point & lastOperation )
	{
		// #tbd: might want to priority boost the worker thread as well

		auto timeSinceLastOperation = std::chrono::system_clock::now() - lastOperation;

		if( timeSinceLastOperation > std::chrono::milliseconds( c_loggerSpinTimeMS ) )
		{
			if( timeSinceLastOperation <  std::chrono::milliseconds( c_loggerYieldTimeMS ) )
			{
				YieldCurrentThread();
			}
			else
			{
				SleepOnCurrentThread( 1 );
			}
		}
	}

	void Logger::RegisterSink( LoggerSink * sink )
	{
		{
			ScopedLock< RWSpinLock > scopedLock( m_sinkLock );
			for( Uint32 index = 0; index != c_loggerSinkMaxCount; ++index )
			{
				if( !m_sinks[ index ] )
				{
					m_sinks[ index ] = sink;
					return;
				}
			}
		}

		// No more sink place. Silent failure.
		PushLine( LoggerLevel_Error, LoggerCategory_Engine, "Logger cannot register more sink.", nullptr );
	}

	void Logger::RegisterFilteredSink( LoggerSink * sink )
	{
		{
			ScopedLock< RWSpinLock > scopedLock( m_sinkLock );
			for ( Uint32 index = 0; index != c_loggerSinkMaxCount; ++index )
			{
				if ( !m_filteredSinks[index] )
				{
					m_filteredSinks[index] = sink;
					return;
				}
			}
		}

		// No more sink place. Silent failure.
		PushLine( LoggerLevel_Error, LoggerCategory_Engine, "Logger cannot register more filtered sink.", nullptr );
	}

	void Logger::UnregisterSink( LoggerSink * sink )
	{
		ScopedLock< RWSpinLock > scopedLock( m_sinkLock );
		auto iter = std::remove( m_sinks, m_sinks + c_loggerSinkMaxCount, sink );
		if( iter != m_sinks + c_loggerSinkMaxCount )
		{
			*iter = nullptr;
		}
	}

	void Logger::UnregisterFilteredSink( LoggerSink * sink )
	{
		ScopedLock< RWSpinLock > scopedLock( m_sinkLock );
		auto iter = std::remove( m_filteredSinks, m_filteredSinks + c_loggerSinkMaxCount, sink );
		if ( iter != m_filteredSinks + c_loggerSinkMaxCount )
		{
			*iter = nullptr;
		}
	}

	void Logger::InternalSetLoggerWorker( LoggerWorker * worker )
	{
		m_worker = worker;
	}

	void Logger::ToggleLoggerCategory( LoggerCategory category, Bool set )
	{
		if ( set )
		{
			m_categoryFilter |= ( 1ull << category );
		}
		else
		{
			m_categoryFilter &= ~( 1ull << category );
		}
	}

	void Logger::SetThreadContextId( Uint32 contextid )
	{
		s_threadContextId = contextid;
	}

	void Logger::SetDefaulftThreadContextId()
	{
		s_threadContextId = DEFAULT_CONTEXT_ID;
	}

	void Logger::SetFrameNumberRetriever( FrameNumberRetriever frameNumberRetriever )
	{
		m_frameNumberRetriever = frameNumberRetriever;
	}

	void Logger::SetNetPeerId( Uint32 netPeerId)
	{
		s_netPeerId = netPeerId;
	}

	void Logger::SetDefaulftNetPeerId()
	{
		s_netPeerId = DEFAULT_NETWORK_PEER_ID;
	}
}

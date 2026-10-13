/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_LOGGER_H_
#define _RED_SYSTEM_LOGGER_H_

#include "loggerLocklessQueue.h"
#include "loggerWorker.h"
#include "../include/readWriteSpinLock.h"

namespace red
{
	const Uint32 c_loggerSinkMaxCount = 8;
	const Uint32 c_loggerMaxPrefixLen = 16;

	class LoggerSink;

	enum LoggerCategory;
	enum LoggerMode : Uint8;
	enum LoggerLevel : Uint8;
	enum LoggerFlushMode : Uint8;

	using FrameNumberRetriever = Uint64 (*)();

	namespace err { template < typename > class OrbisLoggerCrashDumper; }

	class REDSYSTEM_API Logger
	{
		template< typename TLoggerLine >
		friend class err::OrbisLoggerCrashDumper;

	public:

		Logger();
		~Logger();

		void Initialize( LoggerMode mode, const char * prefix = nullptr );
		void Uninitialize();

		void PushLine( LoggerLevel level, LoggerCategory category, STATIC_CHECK_PRINTF_MSC const char * message, ... );
		void PushLine( LoggerLevel level, LoggerCategory category, STATIC_CHECK_PRINTF_MSC const char * message, va_list arglist );
		
		void PushDataError( LoggerLevel level, LoggerCategory category, const DataError& dataError );

		void PushFlush( LoggerFlushMode flushMode );

		Bool TryPushFlush(LoggerFlushMode flushMode);

		void Enable();
		void Disable();
		bool IsEnabled() const;
		bool CanPrint( LoggerLevel level, LoggerCategory category ) const
		{
			return m_isEnabled && ( level <= m_level ) && ( ( 1ull << category ) & m_categoryFilter );
		}

		// Message must be <= this level to be accepted.
		void SetLevel( LoggerLevel level ); 

		// Restore log level to default value.
		void RestoreDefaultLevel();

		// Set thread context id, that will be added to the log. This context is set for all logs from given thread.
		void SetThreadContextId( Uint32 contextid );

		// Restores thread context id.
		void SetDefaulftThreadContextId();

		void SetFrameNumberRetriever( FrameNumberRetriever frameNumberRetriever );

		// Set network peer id, that will be added to the log in multiplayer games.
		// \remarks The network peer is added to log only if different than default value.
		void SetNetPeerId( Uint32 netPeerId );

		// Restores default network peer id (disable logging peer id).
		void SetDefaulftNetPeerId();

		bool ConsumeNextMessage();

		void RegisterSink( LoggerSink * sink );
		void RegisterFilteredSink( LoggerSink * sink );
		void UnregisterSink( LoggerSink * sink );
		void UnregisterFilteredSink( LoggerSink * sink );

		void InternalSetLoggerWorker( LoggerWorker * worker );
		
		void ToggleLoggerCategory( LoggerCategory category, Bool set );

		// #tbd: should remove that bool on the stack
		Uint64 GetFlushFence() const { return m_flushFenceCounter.GetValue(); }
		LoggerMode GetMode() const { return m_mode; }

#ifdef RED_PLATFORM_ORBIS
		void CrashDumpLoggerLineQueue();
#endif

	private:

		void ConsumeLogMessage( const LoggerLine & message );
		void ConsumeFlushMessage();
		void QueueMessage( LoggerLine && message );
		Bool TryQueueMessage( LoggerLine && message);
		void SinkMessage( const char * formattedMessage, const LoggerLine & message );

		void ConsumeDataError( const LoggerLineWithDataError & message );
		void QueueDataError( LoggerLineWithDataError && message );
		void SinkDataError( const char * formattedMessage, const LoggerLineWithDataError & message );

		void Wait( const std::chrono::system_clock::time_point & lastOperation );

		bool m_isEnabled;
		bool m_isInitialized;
		LoggerLevel m_level; // message must be <= this level to be accepted
		Uint64 m_categoryFilter;
		LoggerMode m_mode;
		char m_prefix[ c_loggerMaxPrefixLen ];
		std::chrono::system_clock::time_point m_lastConsumedMessageTime;
		FrameNumberRetriever m_frameNumberRetriever;
		
		LoggerSink * m_sinks[ c_loggerSinkMaxCount ];
		LoggerSink * m_filteredSinks[ c_loggerSinkMaxCount ];
	
		RWSpinLock m_sinkLock;

		LoggerWorker * m_worker;
		LoggerWorker m_workerStorage; 
	
		LoggerLocklessQueue< LoggerLine > m_loggerLineQueue;
		LoggerLocklessQueue< LoggerLineWithDataError > m_loggerLineWithDataErrorQueue;
		red::Atomic<Uint64 > m_flushFenceCounter;
	};

	REDSYSTEM_API Logger* GetSystemLoggerSafe();
	REDSYSTEM_API Logger& GetSystemLogger();
	REDSYSTEM_API void FormatLogMessage( const LoggerLine & message, char * buffer, Uint32 size );
}

#endif

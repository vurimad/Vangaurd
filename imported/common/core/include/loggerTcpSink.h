/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "tcpMessageHandler.h"
#include "../../redSystem/include/loggerSink.h"
#include "../../redSystem/include/logMessage.h"
#include "../../redNetwork/include/socket.h"
#include "../../redMemory/include/uniquePtr.h"

class CMemoryFileWriterExternalBuffer;
class JSONBuilderContext;

namespace red
{
	struct LoggerTcpSinkParameter
	{
		red::String redListenerIpAddress;
		red::String processName;
		red::String processArgs;
		Uint32 processId;
	};

	using DataErrorHandler = std::function< void( const red::String& ) >;

	class RED_CORE_API LoggerTcpSink : public LoggerSink
	{
	public:
		LoggerTcpSink();

		void Initialize( const LoggerTcpSinkParameter& param );
		void Shutdown();

		void SetDataErrorHandler( const DataErrorHandler& dataErrorHandler );

		Bool IsConnected() const;

		virtual void SinkLogLine( const char * formattedMessage, const LoggerLine & message ) override final;
		virtual void SinkLogLine( const char * formattedMessage, const LoggerLineWithDataError & message ) override final;

		virtual void Flush() override final;

	private:
		enum ActiveLoggerLevel : Uint8
		{
			LoggerLevel_Fatal = 0x01,
			LoggerLevel_Error = 0x02,
			LoggerLevel_Warning = 0x04,
			LoggerLevel_Info = 0x08,
			LoggerLevel_Debug = 0x10,
			LoggerLevel_Trace = 0x20
		};

		enum MessageType : Uint32
		{
			ProcessInfo,
			LogLine,
			DataError,
			FilterLoggerCategories
		};

		enum ActionType : Uint16
		{
			ShowResouceInAssetBrowser
		};

		struct Action
		{
			ActionType type;
			red::String data;
		};

		template< typename T, Uint32 Size = 1024 >
		class LogggerCacheQueue
		{
			RED_USE_MEMORY_POOL( red::PoolDebug );

		public:
			LogggerCacheQueue()
				: m_writeIndex( 0 )
				, m_readIndex( 0 )
			{
				std::memset( &m_logs, 0, sizeof( m_logs ) );
			}

			void Push( const T& log )
			{
				if ( m_writeIndex == Size )
				{
					m_writeIndex = 0;
					m_readIndex = 0;
				}

				m_logs[ m_writeIndex++ ] = log;
			}

			Bool Pop( T& log )
			{
				if ( m_readIndex < m_writeIndex )
				{
					log = m_logs[ m_readIndex++ ];
					return true;
				}

				return false;
			}

			Bool Empty() const
			{
				return m_readIndex == m_writeIndex;
			}

		private:
			T m_logs[ Size ];
			Uint32 m_writeIndex;
			Uint32 m_readIndex;
		};

		void SendProcessInfo();
		Bool SendLogLine( const char * formattedMessage, const LoggerLine & message );
		Bool SendDataError( const char* formattedMessage, const LoggerLineWithDataError & message );
		Bool SendData( char* buffer, CMemoryFileWriterExternalBuffer& writer );

		Bool IsConnectionActive() const;
		Bool IsConnecting() const;

		Bool SendCachedLogLines();

		Bool IsDataError( const char* formattedMessage ) const;

		void HandleMessage( const MessageType& messageType, const red::String& message );
		Bool ParseMessage( MessageType& messageType, const red::String& message );
		Bool ParseMessageType( Uint32 type, MessageType& messageType );

		void SerializeProcessInfoToJSON( CMemoryFileWriterExternalBuffer& writer );
		void SerializeLogToJSON(
			CMemoryFileWriterExternalBuffer& writer,
			const char * formattedMessage,
			const LoggerLine & message,
			const MessageType & type = MessageType::LogLine,
			Uint32 dataErrorMessageFormatHash = 0,
			const char* dateErrorActions = nullptr );

		void SerializeDataErrorActions( JSONBuilderContext& context, const char* dataErrorActions );

		void NotifyDataError( const red::String& message );
		void UpdateActiveLoggerLevels( const red::String& message );

		static ActiveLoggerLevel ConvertLoggerLevelToActiveLoggerLevel( red::LoggerLevel level );

		DataErrorHandler m_dataErrorHandler;

		Network::Socket* m_socket;
		UniquePtr< TcpMessageHandler > m_tcpMessageHandler;
		UniquePtr< LogggerCacheQueue< LoggerLine >, red::PoolDebug > m_loggerLineQueue;
		UniquePtr< LogggerCacheQueue< LoggerLineWithDataError >, red::PoolDebug > m_loggerLineWithDataErrorQueue;
		LoggerTcpSinkParameter m_param;
		Atomic< Uint32 > m_activeLoggerLevels;
		
		Atomic< Bool > m_isRunning;
	};
}

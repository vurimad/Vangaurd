/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "loggerTcpSink.h"
#include "../../redFileSystem/include/memoryFileWriter.h"
#include "../../redSystem/include/logMessage.h"
#include "../../resource/include/jsonFileHelper.h"
#include "../../resource/include/jsonDataBuilder.h"
#include "../../redSystem/include/log.h"
#include "../../redSystem/include/dataErrorAction.h"
#include "../../redReflection/include/version.h"

namespace
{
	static const Uint32 s_headerSize = 2;
	static const Uint32 s_maxLogSize = RED_KILO_BYTE( 4 );
	static const Uint32 s_maxDataSize = s_headerSize + s_maxLogSize;
	static const char* s_defaultSymbolName = "<unknown>";
	static const Uint32 c_loggerFormattedMessage = 1024;
	static const Uint32 s_tcpConnectionTimeoutSeconds = 1;
	static const Uint16 s_redListenerPort = 19199;
	static const Uint32 s_socketSendWouldBlockMaxCount = 5;
}

namespace red
{
	extern void FormatLogMessage( const LoggerLine & message, char * buffer, Uint32 size );

	LoggerTcpSink::LoggerTcpSink()
		: m_socket( nullptr )
		, m_isRunning( true )
	{
		m_activeLoggerLevels.SetValue(
			ActiveLoggerLevel::LoggerLevel_Fatal | ActiveLoggerLevel::LoggerLevel_Error |
			ActiveLoggerLevel::LoggerLevel_Warning | ActiveLoggerLevel::LoggerLevel_Info |
			ActiveLoggerLevel::LoggerLevel_Debug | ActiveLoggerLevel::LoggerLevel_Trace
		);
	}

	Bool LoggerTcpSink::IsConnected() const
	{
		return m_socket && ( m_socket->IsConnected() || m_socket->IsConnecting() );
	}

	void LoggerTcpSink::Initialize(const LoggerTcpSinkParameter& param)
	{
		m_loggerLineQueue = red::CreateUniquePtr< LogggerCacheQueue< LoggerLine >, red::PoolDebug >();
		m_loggerLineWithDataErrorQueue = red::CreateUniquePtr< LogggerCacheQueue< LoggerLineWithDataError >, red::PoolDebug >();

		m_param = param;
		const auto onConnectedCallback = [this]()
		{
			SendProcessInfo();
		};

		const auto onMessageCallback = [this]( const IpcMessageHeader& header, const red::String& message )
		{
			MessageType messageType;
			if ( ParseMessage( messageType, message ) )
			{
				HandleMessage( messageType, message );
			}
		};

		TcpMessageHandlerParameter msgHandlerParam
		{
			{
				s_headerSize,
				s_headerSize,
				RED_KILO_BYTE( 8 ),
				onMessageCallback
			},
			{ param.redListenerIpAddress.AsChar(), s_redListenerPort },
			onConnectedCallback
		};

		m_tcpMessageHandler.Reset( RED_NEW( TcpMessageHandler )( msgHandlerParam, m_isRunning ) );
		m_socket = m_tcpMessageHandler->GetSocket();
		m_tcpMessageHandler->InitThread();
	}

	void LoggerTcpSink::Shutdown()
	{
		if ( m_socket )
		{
			if ( m_socket->IsConnecting() )
			{
				red::Timer time;
				while ( !m_socket->FinishConnecting() && ( time.GetSeconds() < s_tcpConnectionTimeoutSeconds ) )
				{
					SleepOnCurrentThread( 20 );
				}

				if ( m_socket->IsConnected() )
				{
					SendProcessInfo();
					SendCachedLogLines();
				}
			}
			m_socket = nullptr;//owned by m_tcpMessageHandler
		}

		m_isRunning.SetValue( false );
		if ( m_tcpMessageHandler )
		{
			m_tcpMessageHandler->JoinThread();
			m_tcpMessageHandler.Reset();
		}
	}

	void LoggerTcpSink::SetDataErrorHandler( const DataErrorHandler& dataErrorHandler )
	{
		m_dataErrorHandler = dataErrorHandler;
	}

	void LoggerTcpSink::SinkLogLine( const char * formattedMessage, const LoggerLine & message )
	{
		if ( !( m_activeLoggerLevels.GetValue() & ConvertLoggerLevelToActiveLoggerLevel( message.level ) ) )
		{
			return;
		}

		if ( IsConnectionActive() && SendCachedLogLines() )
		{
			SendLogLine( formattedMessage, message );
		}
		else
		{
			m_loggerLineQueue->Push( message );
		}
	}

	void LoggerTcpSink::SinkLogLine( const char * formattedMessage, const LoggerLineWithDataError & message )
	{
		if ( IsConnectionActive() && SendCachedLogLines() )
		{
			if ( message.line.type == LoggerLineType::LoggerLineType_DataError )
			{
				SendDataError( formattedMessage, message );
			}
		}
		else
		{
			m_loggerLineWithDataErrorQueue->Push( message );
		}
	}

	void LoggerTcpSink::Flush()
	{
		if ( IsConnectionActive() )
		{
			SendCachedLogLines();
		}
	}

	void LoggerTcpSink::SendProcessInfo()
	{
		char buffer[ s_maxDataSize ];
		red::Memzero( buffer, s_maxDataSize );

		CMemoryFileWriterExternalBuffer writer( buffer + s_headerSize, s_maxLogSize );
		SerializeProcessInfoToJSON( writer );

		SendData( buffer, writer );
	}

	Bool LoggerTcpSink::SendLogLine( const char * formattedMessage, const LoggerLine & message )
	{
		char buffer[ s_maxDataSize ];
		red::Memzero( buffer, s_maxDataSize );

		CMemoryFileWriterExternalBuffer writer( buffer + s_headerSize, s_maxLogSize );
		SerializeLogToJSON( writer, formattedMessage, message );

		if ( !SendData( buffer, writer ) )
		{
			m_loggerLineQueue->Push( message );
			return false;
		}

		return true;
	}

	Bool LoggerTcpSink::SendDataError( const char* formattedMessage, const LoggerLineWithDataError & message )
	{
		char buffer[ s_maxDataSize ];
		red::Memzero( buffer, s_maxDataSize );

		CMemoryFileWriterExternalBuffer writer( buffer + s_headerSize, s_maxLogSize );
		SerializeLogToJSON( writer, formattedMessage, message.line, MessageType::DataError, message.dataErrorMessageFormatHash, message.dataErrorActions );

		if ( !SendData( buffer, writer ) )
		{
			m_loggerLineWithDataErrorQueue->Push( message );
			return false;
		}

		return true;
	}

	Bool LoggerTcpSink::SendData( char* buffer, CMemoryFileWriterExternalBuffer& writer )
	{
		const Uint16 realSize = static_cast< Uint16 >( writer.GetRealSize() );
		std::memcpy( buffer, &realSize, sizeof( Uint16 ) );

		const Uint32 realBufferSize = s_headerSize + realSize;

		Uint32 bytesSend = 0;
		Uint32 totalBytesSend = 0;
		const char* ptr = buffer;
		Uint32 wouldBlockCount = 0;

		do
		{
			bytesSend = m_socket->Send( ptr, realBufferSize - totalBytesSend );
			totalBytesSend += bytesSend;
			ptr += bytesSend;

			if ( !bytesSend && m_socket->GetLastError() == RED_NET_ERR_RECEIVE_OK )
			{
				++wouldBlockCount;
			}

		} while ( totalBytesSend < realBufferSize && wouldBlockCount < s_socketSendWouldBlockMaxCount && !m_socket->IsDropped() );

		return totalBytesSend == realBufferSize;
	}

	Bool LoggerTcpSink::IsConnectionActive() const
	{
		if (m_isRunning.GetValue())
		{
			RED_ASSERT(m_socket != nullptr, "Socket does not exist");
			return m_socket->IsConnected();
		}
		return false;
	}

	Bool LoggerTcpSink::IsConnecting() const
	{
		if (m_isRunning.GetValue())
		{
			RED_ASSERT(m_socket != nullptr, "Socket does not exist");
			return m_socket->IsConnecting();
		}
		return false;
	}

	Bool LoggerTcpSink::SendCachedLogLines()
	{
		while ( !m_loggerLineQueue->Empty() )
		{
			if ( m_socket->IsDropped() || m_socket->IsClosed() )
			{
				return false;
			}

			LoggerLine message;
			if ( m_loggerLineQueue->Pop( message ) )
			{
				char formattedMessage[ c_loggerFormattedMessage ];
				FormatLogMessage( message, formattedMessage, c_loggerFormattedMessage );
				if ( !SendLogLine( formattedMessage, message ) )
				{
					return false;
				}
			}
		}

		while ( !m_loggerLineWithDataErrorQueue->Empty() )
		{
			if ( m_socket->IsDropped() || m_socket->IsClosed() )
			{
				return false;
			}

			LoggerLineWithDataError message;
			if ( m_loggerLineWithDataErrorQueue->Pop( message ) )
			{
				char formattedMessage[ c_loggerFormattedMessage ];
				FormatLogMessage( message.line, formattedMessage, c_loggerFormattedMessage );
				if ( !SendDataError( formattedMessage, message ) )
				{
					return false;
				}
			}
		}

		return true;
	}

	Bool LoggerTcpSink::IsDataError( const char* formattedMessage ) const
	{
		const char* foundDataError = red::Strstr( formattedMessage, "[DATA ERROR]" );
		return foundDataError && foundDataError == formattedMessage;
	}

	void LoggerTcpSink::SerializeProcessInfoToJSON( CMemoryFileWriterExternalBuffer& writer )
	{
		char writerPool[ s_maxLogSize ] = { '\0' };
		JSONFileHelper::JSONFileStream stream{ &writer };
		rapidjson::MemoryPoolAllocator< JSONFileHelper::FixedAllocator > pool{ writerPool, sizeof( writerPool ), sizeof( writerPool ) };
		JSONFileHelper::JSONCompactWriter json{ stream, &pool };
		JSONBuilderContext context{ json };

		context.AnonymousObject();

		context.Key( "type" );
		context.Uint( static_cast< Uint32 >( MessageType::ProcessInfo ) );

		context.Object( "message" );
		{
			context.Key( "name" );
			context.String( m_param.processName.AsChar() );
			context.Key( "pid" );
			context.Uint( m_param.processId );
			context.Key( "argv" );
			context.String( m_param.processArgs.AsChar() );
			context.Key( "has_data_error_handler" );
			context.Bool( static_cast< Bool >( m_dataErrorHandler ) );
			context.Key( "version" );
			context.String( APP_VERSION_NUMBER );

			const auto& loggerCategories = red::g_LoggerCategories;
			{
				context.Array( "logger_categories" );

				for ( int i = 0; i < red::LoggerCategory_MAX; ++i )
				{
					const auto& pair = loggerCategories[i];
					context.AnonymousObject();
					context.Key( "category" );
					context.String( pair.name );
					context.Key( "value" );
					context.Uint( pair.category );
					context.PopObject();
				}
				context.PopArray();
			}

		}
		context.PopObject();

		context.PopObject();
	}

	void LoggerTcpSink::SerializeLogToJSON(
		CMemoryFileWriterExternalBuffer& writer,
		const char * formattedMessage,
		const LoggerLine & message,
		const MessageType & type,
		Uint32 dataErrorMessageFormatHash,
		const char* dataErrorActions )
	{
		char writerPool[ s_maxLogSize ] = { '\0' };
		JSONFileHelper::JSONFileStream stream{ &writer };
		rapidjson::MemoryPoolAllocator< JSONFileHelper::FixedAllocator > pool{ writerPool, sizeof( writerPool ), sizeof( writerPool ) };
		JSONFileHelper::JSONCompactWriter json{ stream, &pool };
		JSONBuilderContext context{ json };

		context.AnonymousObject();

		context.Key( "type" );
		context.Uint( static_cast< Uint32 >( type ) );

		context.Object( "message" );
		{
			context.Key( "log" );
			context.String( formattedMessage );

			context.Object( "data" );
			{
				context.Key("time");
				context.Uint64( std::chrono::system_clock::to_time_t( message.time ) );
				context.Key( "thread_id" );
				context.Uint( message.threadId );
				context.Key( "thread_context_id" );
				context.Uint( message.threadContextId );
				context.Key( "line_type" );
				context.Uint( message.type );
				context.Key( "level" );
				context.Uint( message.level );
				context.Key( "category" );
				context.Uint( message.category );
				context.Key( "frame" );
				context.Uint64( message.frame );

				if ( type == MessageType::DataError )
				{
					context.Key( "message_format_hash" );
					context.Uint( dataErrorMessageFormatHash );

					SerializeDataErrorActions( context, dataErrorActions );
				}
			}
			context.PopObject();
		}
		context.PopObject();

		context.PopObject();
	}

	void LoggerTcpSink::SerializeDataErrorActions( JSONBuilderContext& context, const char* dataErrorActions )
	{
		const char* tmp = dataErrorActions;
		const char* foundAction = red::Strstr( tmp, red::DataErrorAction::s_actionSeparator );
		if ( !foundAction )
		{
			return;
		}

		context.Array( "actions" );

		const auto actionSeparatorSize = red::DataErrorAction::s_actionSeparatorSize;

		while ( foundAction )
		{
			const char* foundActionHeader = red::Strstr( tmp, red::DataErrorAction::s_actionTypeSeparator );
			if ( !foundActionHeader )
			{
				return;
			}

			const String type = String( tmp, static_cast< Uint32 >( foundActionHeader - tmp ) );
			char* end;
			Uint32 typeInt = 0;
			if ( !red::StringToInt( typeInt, type.AsChar(), &end, red::Base::BaseTen ) )
			{
				return;
			}

			const char* foundActionData = red::Strstr( foundActionHeader, red::DataErrorAction::s_actionDescriptionSeparator );
			if ( !foundActionData )
			{
				return;
			}

			const String description = String( foundActionHeader + actionSeparatorSize, static_cast< Uint32 >( foundActionData - ( foundActionHeader + actionSeparatorSize) ) );
			const String data = String( foundActionData + actionSeparatorSize, static_cast< Uint32 >( foundAction - ( foundActionData + actionSeparatorSize ) ) );

			context.AnonymousObject();

			context.Key( "type" );
			context.Uint( typeInt );

			context.Key( "description" );
			context.String( description.AsChar() );

			context.Key( "data" );
			context.String( data.AsChar() );

			context.PopObject();

			tmp = foundAction + 1;
			foundAction = red::Strstr( foundAction + actionSeparatorSize, red::DataErrorAction::s_actionSeparator );
		}

		context.PopArray();
	}

	void LoggerTcpSink::HandleMessage( const MessageType& messageType, const red::String& message )
	{
		switch ( messageType )
		{
		case MessageType::DataError:
			NotifyDataError( message );
			break;

		case MessageType::FilterLoggerCategories:
			UpdateActiveLoggerLevels( message );
			break;
		}
	}

	Bool LoggerTcpSink::ParseMessage( MessageType& messageType, const red::String& message )
	{
		rapidjson::Document document;
		document.Parse< rapidjson::kParseDefaultFlags >( message.AsChar() );

		if ( !document.IsArray() )
		{
			return false;
		}

		const rapidjson::Value& value = document[0];
		if ( !value.IsObject() )
		{
			return false;
		}

		const rapidjson::Value& type = value["type"];
		if ( !type.IsUint() )
		{
			return false;
		}

		return ParseMessageType( type.GetUint(), messageType );
	}

	Bool LoggerTcpSink::ParseMessageType( Uint32 type, MessageType& messageType )
	{
		switch ( type )
		{
		case MessageType::DataError:
		case MessageType::FilterLoggerCategories:
			break;

		default:
			return false;
		}

		messageType = static_cast< MessageType >( type );
		return true;
	}

	void LoggerTcpSink::NotifyDataError( const red::String& message )
	{
		rapidjson::Document document;
		document.Parse< rapidjson::kParseDefaultFlags >( message.AsChar() );

		RED_ASSERT( document.IsArray() );
		const rapidjson::Value& value = document[0];
		RED_ASSERT( value.IsObject() );
		const rapidjson::Value& msg = value["message"];
		RED_ASSERT( msg.IsObject() );

		RED_ASSERT( msg.HasMember( "type" ) );
		RED_ASSERT( msg.HasMember( "data" ) );

		red::String action = red::String::Printf( "%u", msg["type"].GetUint() );
		action += ":";
		action += msg["data"].GetString();

		if ( m_dataErrorHandler )
		{
			m_dataErrorHandler( action );
		}
	}

	void LoggerTcpSink::UpdateActiveLoggerLevels( const red::String& message )
	{
		rapidjson::Document document;
		document.Parse< rapidjson::kParseDefaultFlags >( message.AsChar() );

		RED_ASSERT( document.IsArray() );
		const rapidjson::Value& value = document[0];
		RED_ASSERT( value.IsObject() );
		const rapidjson::Value& msg = value["message"];
		RED_ASSERT( msg.IsArray() );

		const auto& activeLoggerLevels = msg.GetArray();
		Uint32 bitmask = 0;
		for ( Uint32 i = 0; i < activeLoggerLevels.Size(); ++i )
		{
			bitmask |= ConvertLoggerLevelToActiveLoggerLevel( static_cast< LoggerLevel >( activeLoggerLevels[ i ].GetUint() ) );
		}

		m_activeLoggerLevels.SetValue( bitmask );
	}

	LoggerTcpSink::ActiveLoggerLevel LoggerTcpSink::ConvertLoggerLevelToActiveLoggerLevel( red::LoggerLevel level )
	{
		switch ( level )
		{
		case LoggerLevel::LoggerLevel_Fatal:
			return ActiveLoggerLevel::LoggerLevel_Fatal;
		case LoggerLevel::LoggerLevel_Error:
			return ActiveLoggerLevel::LoggerLevel_Error;
		case LoggerLevel::LoggerLevel_Warning:
			return ActiveLoggerLevel::LoggerLevel_Warning;
		case LoggerLevel::LoggerLevel_Info:
			return ActiveLoggerLevel::LoggerLevel_Info;
		case LoggerLevel::LoggerLevel_Debug:
			return ActiveLoggerLevel::LoggerLevel_Debug;
		case LoggerLevel::LoggerLevel_Trace:
			return ActiveLoggerLevel::LoggerLevel_Trace;
		default:
			RED_ASSERT( 0, "Unknown logger level" );
			return ActiveLoggerLevel::LoggerLevel_Fatal;
		}
	}
}
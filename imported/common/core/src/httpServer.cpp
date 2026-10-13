/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "httpHeader.h"
#include "httpServer.h"
#include "httpRequestParser.h"
#include "httpResponseData.h"
#include "httpResponseSender.h"
#include "webSocketEndpoint.h"

#include "../../redFileSystem/include/fileSys.h"
#include "../../redFileSystem/include/memoryFileWriter.h"
#include "../../redFileSystem/include/memoryFileReader.h"

#include "../../../common/redSystem/include/swapBytes.h"
#include "../../../common/redSystem/include/stopWatch.h"

using red::DynArray;

//-----

namespace Config
{
	TConfigVar< Int32 >	cvHTTPKeepAliveTime( "HTTP", "KeepAliveTime", 120, 0, 3600, eConsoleVarFlag_ReadOnly | eConsoleVarFlag_Developer );
	TConfigVar< Int32 >	cvHTTPMaxFileTransferSizeKB( "HTTP", "MaxFileSizeKB", 4096, 0, INT_MAX, eConsoleVarFlag_ReadOnly | eConsoleVarFlag_Developer );
	TConfigVar< Bool >	cvHTTPAllowAllFiles( "HTTP", "AllowAllFiles", false, eConsoleVarFlag_ReadOnly | eConsoleVarFlag_Developer );
	TConfigVar< Int32 >	cvHTTPWaitQueueBacklog( "HTTP", "WaitQueueBacklog", 32, 0, 32, eConsoleVarFlag_ReadOnly | eConsoleVarFlag_Developer );
	TConfigVar< Int32 >	cvHTTPNumThreads( "HTTP", "NumThreads", 4, 1, CHTTPServer::MAX_THREADS, eConsoleVarFlag_ReadOnly | eConsoleVarFlag_Developer );
	TConfigVar< Int32 >	cvHTTPNumActiveWebSockets( "HTTP", "NumActiveWebSockets", 1, 1, CHTTPServer::MAX_THREADS, eConsoleVarFlag_ReadOnly | eConsoleVarFlag_Developer );

	// #todo: should really be per-socket dynamically configurable (e.g., through a web form)
	TConfigVar< Int32 >	cvHTTPWebSocketOutputUpdateFrequency( "HTTP", "WebSocketOutputUpdateFrequency", 0, 0, 60, eConsoleVarFlag_ReadOnly | eConsoleVarFlag_Developer );

	TConfigVar< Int32 >	cvHTTPKeepAliveIdleTimeoutSec( "HTTP", "KeepAliveIdleTimeoutSec", 2, 0, 60, eConsoleVarFlag_ReadOnly | eConsoleVarFlag_Developer );
	TConfigVar< Int32 >	cvHTTPInitialResponseWaitSec( "HTTP", "InitialResponseWaitSec", 30, 0, 60, eConsoleVarFlag_ReadOnly | eConsoleVarFlag_Developer );

}

namespace
{
	template< typename... Args >
	RED_FORCE_INLINE static red::UniquePtr< CHTTPResponseSender > CreateHTTPResponse( Args&& ...args )
	{
		return red::CreateUniquePtr< CHTTPResponseSender >( std::forward< Args >( args )... );
	}

	const Uint32 INVALID_CONNECTION_ID = 0xFFFFFFFF;
	const char WEBSOCKET_PROTOCOL_VERSION_TXT[] = "13";
}

CHTTPConnection::CHTTPConnection( RawTcpManager& network, TConnectionID connectionID, Uint64 generation )
	: m_network( &network )
	, m_id( connectionID )
	, m_parser( red::CreateUniquePtr< CHTTPRequestParser >() )
	, m_generation( generation )
	, m_connectionActive( true )
	, m_isWebSocket( false )
	, m_serverPrivateData( nullptr )
{
}

CHTTPConnection::~CHTTPConnection()
{
	if ( m_id != INVALID_CONNECTION_ID )
	{
		m_network->CloseConnection( m_id );
	}
}

void CHTTPConnection::OnDisconnected( const red::Network::TConnectionID connectionID )
{
	red::ScopedLock< red::Network::RawTcpManager > lock( *m_network );
	m_connectionActive = false;
}

void CHTTPConnection::OnData( const void* data, const Uint32 dataSize, const TConnectionID connectionID )
{
	// #tbd: coarse lock since can be upgraded to WebSocket and OnData is push vs pulling for data, or buffer
	red::ScopedLock< red::Mutex > lock( m_lock );

	if ( !m_isWebSocket )
	{
		// #todo: limit HTTP request copies
		// parse new requests from network stream
		DynArray< CHTTPRequest > newRequests{ red::PoolDebug() };
		m_parser->Append( data, dataSize, newRequests );

		// add any newly parsed requests to the list
		if (!newRequests.Empty())
		{
			m_pendingHttpRequests.Reserve( m_pendingHttpRequests.Size() + newRequests.Size() );
			for (auto& request : newRequests)
			{
				m_pendingHttpRequests.PushBack( red::CreateUniquePtr< CHTTPRequest >( std::move( request ) ) );
			}
		}
	}
	else
	{
		RED_FATAL_ASSERT( m_webSocketFrameParser );
		const CWebSocketFrameParser::EResult result = m_webSocketFrameParser->Decode( data, dataSize );
		if ( result != CWebSocketFrameParser::EResult::Continue )
		{
			RED_LOG_ERROR( "WebSocket connectionID %u failed to decode frame (result=%u). Closing connection", connectionID, result );
			Close();
		}
	}
}

Uint32 CHTTPConnection::Send( const void* buffer, Uint32 size )
{
	red::ScopedLock< red::Network::RawTcpManager > lock( *m_network );
	return m_network->Send( m_id, buffer, size );
}

void CHTTPConnection::Close()
{
	red::ScopedLock< red::Network::RawTcpManager > lock( *m_network );
	if ( m_id != INVALID_CONNECTION_ID )
	{
		m_network->CloseConnection( m_id );
		m_id = INVALID_CONNECTION_ID;
		m_connectionActive = false;
	}
}

red::UniquePtr< CHTTPRequest > CHTTPConnection::PopHTTPRequest()
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	if (m_pendingHttpRequests.Empty())
		return nullptr;

	// pop the first request from queue
	auto ret = std::move( m_pendingHttpRequests.Front() );
	m_pendingHttpRequests.PopFront();

	return ret;
}

void CHTTPConnection::PushHTTPResponse( red::UniquePtr< class CHTTPResponseSender > response )
{
	red::ScopedLock< red::Mutex > lock( m_lock );
	m_pendingHttpResponses.PushBack( std::move( response ) );
}

void CHTTPConnection::FlushHTTPResponses( red::CircularBuffer< red::UniquePtr< class CHTTPResponseSender > >& outResponses )
{
	red::ScopedLock< red::Mutex > lock( m_lock );
	m_pendingHttpResponses.Swap( outResponses );
	m_pendingHttpResponses.Clear();
}

void CHTTPConnection::RequestShutdownWebSocket()
{
	m_requestShutdownWebSocket.SetValue( true );
}

Bool CHTTPConnection::IsRequestedWebSocketShutdown() const
{
	return m_requestShutdownWebSocket.GetValue();
}

void CHTTPConnection::PushOutgoingWebSocketFrame( red::UniquePtr< HTTPWebSocketOutputFrame > frame )
{
	red::ScopedLock< red::Mutex > lock( m_lock );
	m_outgoingWebSocketFrames.PushBack( std::move( frame ) );
}

red::UniquePtr< HTTPWebSocketOutputFrame > CHTTPConnection::PopOutgoingWebSocketFrame()
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	if (m_outgoingWebSocketFrames.Empty())
		return nullptr;

	// pop the first message from queue
	auto ret = std::move( m_outgoingWebSocketFrames.Front() );
	m_outgoingWebSocketFrames.PopFront();

	return ret;
}

void CHTTPConnection::PushIncomingWebSocketFrame( red::UniquePtr< HTTPWebSocketInputFrame > frame )
{
	red::ScopedLock< red::Mutex > lock( m_lock );
	m_incomingWebSocketFrames.PushBack( std::move( frame ) );
}

red::UniquePtr< HTTPWebSocketInputFrame > CHTTPConnection::PopIncomingWebSocketFrame()
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	if (m_incomingWebSocketFrames.Empty())
		return nullptr;

	// pop the first message from queue
	auto ret = std::move( m_incomingWebSocketFrames.Front() );
	m_incomingWebSocketFrames.PopFront();

	return ret;
}

Bool CHTTPConnection::FlushControlWebSocketFrame( HTTPWebSocketInputFrame& outControlFrame )
{
	red::ScopedLock< red::Mutex > lock( m_lock );
	if ( !m_controlWebSocketFrame || m_controlWebSocketFrame->m_opcode == 0 )
	{
		return false;
	}

	red::Memcpy( outControlFrame.m_payload, m_controlWebSocketFrame->m_payload, m_controlWebSocketFrame->m_payloadLength );
	outControlFrame.m_payloadLength = m_controlWebSocketFrame->m_payloadLength;
	outControlFrame.m_opcode = m_controlWebSocketFrame->m_opcode;
	
	// Memory available for reuse
	m_controlWebSocketFrame->m_opcode = 0;

	return true;
}

void CHTTPConnection::UpgradeConnectionToWebSocket()
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	RED_FATAL_ASSERT( !m_isWebSocket, "Internal error: multiple connection upgrades to WebSocket" );
	m_isWebSocket = true;
	m_pendingHttpRequests.Shrink();
	m_pendingHttpResponses.Shrink();

	m_incomingFrame = red::CreateUniquePtr< HTTPWebSocketInputFrame >();

	CWebSocketFrameParser::CallbackToken token;
	token.m_onControlFrame = &CHTTPConnection::OnWebsocketControlFrame;
	token.m_onDataFrame = &CHTTPConnection::OnWebsocketDataFrame;
	token.m_userArg = this;
	token.m_dataPayloadBuffer = m_incomingFrame->m_payload;
	token.m_dataPayloadBufferSize = HTTPWebSocketInputFrame::MAX_PAYLOAD_SIZE;
	m_webSocketFrameParser = red::CreateUniquePtr< CWebSocketFrameParser >( token );

	m_outputFramePool.PushBack( red::CreateUniquePtr<HTTPWebSocketOutputFrame>() );
	m_outputFramePool.PushBack( red::CreateUniquePtr<HTTPWebSocketOutputFrame>() );
}

red::UniquePtr< HTTPWebSocketOutputFrame > CHTTPConnection::TryAcquireOutputFrame()
{
	red::ScopedLock< red::Mutex > lock( m_lock );
	for ( Uint32 i = 0; i < m_outputFramePool.Size(); ++i )
	{
		if ( m_outputFramePool[i] )
		{
			return std::move( m_outputFramePool[i] );
		}
	}

	return nullptr;
}

void CHTTPConnection::RecycleWebSocketOutputFrame( red::UniquePtr< HTTPWebSocketOutputFrame > frame )
{
	red::ScopedLock< red::Mutex > lock( m_lock );
	for (Uint32 i = 0; i < m_outputFramePool.Size(); ++i)
	{
		if (!m_outputFramePool[ i ])
		{
			m_outputFramePool[i] = std::move( frame );
			return;
		}
	}

	RED_LOG_WARNING( "Failed to find a free slot for frame!");
}

CWebSocketFrameParser::EResult CHTTPConnection::OnWebsocketDataFrame( CWebSocketFrameParser::CallbackToken& token, const WebSocketHeaderInfo& headerInfo )
{
	if (headerInfo.m_rsv1 || headerInfo.m_rsv2 || headerInfo.m_rsv3)
	{
		RED_LOG_ERROR( "OnWebsocketDataFrame: reserved bits set and not accepting any extensions, RSV1=%d, RSV2=%d, RSV3=%d",
					   headerInfo.m_rsv1, headerInfo.m_rsv2, headerInfo.m_rsv3 );
		return CWebSocketFrameParser::EResult::Invalid;
	}

	// Callback occurs under lock from OnData
	RED_FATAL_ASSERT( headerInfo.m_payloadLength <= WebSocketHeaderInfo::SMALL_PAYLOAD_MAX, "Message length %lu should not have passed decode validation!", headerInfo.m_payloadLength );
	const Uint32 len = static_cast< Uint32 >( headerInfo.m_payloadLength );
	const auto pThis = reinterpret_cast<CHTTPConnection*>( token.m_userArg );
	RED_FATAL_ASSERT( pThis );

	pThis->m_incomingFrame->m_opcode = headerInfo.m_opCode;
	pThis->m_incomingFrame->m_payloadLength = len;
	pThis->m_incomingWebSocketFrames.PushBack( std::move( pThis->m_incomingFrame ) );

	pThis->m_incomingFrame = red::CreateUniquePtr< HTTPWebSocketInputFrame >();
	token.m_dataPayloadBuffer = pThis->m_incomingFrame->m_payload;
	token.m_dataPayloadBufferSize = HTTPWebSocketInputFrame::MAX_PAYLOAD_SIZE;

	return CWebSocketFrameParser::EResult::Continue;
}

CWebSocketFrameParser::EResult CHTTPConnection::OnWebsocketControlFrame( CWebSocketFrameParser::CallbackToken& token, const WebSocketHeaderInfo& headerInfo, const void* unmaskedPayload )
{
	// Callback occurs under lock from OnData

	const auto pThis = reinterpret_cast< CHTTPConnection* >( token.m_userArg );
	RED_FATAL_ASSERT( pThis );

	if ( headerInfo.m_rsv1 || headerInfo.m_rsv2 || headerInfo.m_rsv3 )
	{
		RED_LOG_ERROR( "OnWebsocketControlFrame: reserved bits set and not accepting any extensions, RSV1=%d, RSV2=%d, RSV3=%d", 
					   headerInfo.m_rsv1, headerInfo.m_rsv2, headerInfo.m_rsv3 );
		return CWebSocketFrameParser::EResult::Invalid;
	}

	auto opcode = static_cast< WebSocketHeaderInfo::EOpcode >( headerInfo.m_opCode );
	RED_FATAL_ASSERT( headerInfo.m_payloadLength <= WebSocketHeaderInfo::SMALL_PAYLOAD_MAX, "Control message length %lu should not have passed decode validation!", headerInfo.m_payloadLength );
	const Uint32 len = static_cast< Uint32 >( headerInfo.m_payloadLength );
	if (!pThis->m_controlWebSocketFrame)
	{
		pThis->m_controlWebSocketFrame = red::CreateUniquePtr< HTTPWebSocketInputFrame >();
		pThis->m_controlWebSocketFrame->m_opcode = 0;
	}
	HTTPWebSocketInputFrame& frame = *pThis->m_controlWebSocketFrame;
	
	Bool updateControlFrame = false;
	switch ( headerInfo.m_opCode )
	{
	case WebSocketHeaderInfo::eOpcode_Close:
		{
			updateControlFrame = true;
		}
		break;
	case WebSocketHeaderInfo::eOpcode_Ping:
		if ( pThis->m_controlWebSocketFrame->m_opcode == 0 || pThis->m_controlWebSocketFrame->m_opcode == WebSocketHeaderInfo::eOpcode_Ping )
		{
			// Can pong the last received ping
			updateControlFrame = true;
		}
		break;
	case WebSocketHeaderInfo::eOpcode_Pong:
		// Unsolicted client heart-beat pong. Ignore.
		break;
	default:
		RED_LOG_WARNING( "HTTP server received unknown control opcode %u", headerInfo.m_opCode );
		break;
	}
	if ( updateControlFrame )
	{
		red::Memcpy( frame.m_payload, unmaskedPayload, headerInfo.m_payloadLength );
		frame.m_opcode = headerInfo.m_opCode;
		frame.m_payloadLength = len;
	}

	return CWebSocketFrameParser::EResult::Continue;
}

//-----

CHTTPServerThread::CHTTPServerThread( const SharedResources& shared )
	: red::Thread( "HTTPServerThread", red::ThreadMemParams( 128*1024 ) )
	, m_waitQueue( *shared.m_waitQueue )
	, m_discardList( *shared.m_discardList )
	, m_activeSlot( *shared.m_activeSlot )
	, m_numQueued( *shared.m_numQueued )
	, m_network( *shared.m_network )
{
}

CHTTPServerThread::~CHTTPServerThread()
{
}

void CHTTPServerThread::RequestExit()
{
	m_exit.SetValue( true );
}

void CHTTPServerThread::LoopHTTPResponses( CHTTPConnection& connection )
{
	red::CircularBuffer< red::UniquePtr< CHTTPResponseSender > > httpResponses{ red::PoolDebug() };
	red::UniquePtr< CHTTPResponseSender > sender;
	red::StopWatch idleTimer;
	Bool idle = false;
	Bool processedAtLeastOneResponse = false;
	while ( connection.IsActive() && !m_exit.GetValue() )
	{
		if ( !sender )
		{
			if ( !httpResponses.Empty() )
			{
				sender = std::move( httpResponses.Front() );
				httpResponses.PopFront();
			}
			else
			{
				connection.FlushHTTPResponses( httpResponses );
				if ( !httpResponses.Empty() )
				{
					sender = std::move( httpResponses.Front() );
					httpResponses.PopFront();
				}
			}

			if ( sender && sender->IsWebSocketUpgrade() )
			{
				RED_LOG( "Received WebSocket upgrade request on connection ID %u", connection.GetConnectionID() );
				// Upgrade before sending any response, so any client data from now on is routed as WebSocket payload
				connection.UpgradeConnectionToWebSocket();
			}
		}

		if ( !sender )
		{
			if ( !idle )
			{
				idle = true;
				idleTimer.Reset();
			}
			else
			{
				// The persistent HTTP connection can cause wait-queue build up, since browsers can open multiple keep alive connections
				// and not reuse them across tabs. Could throw back into the wait queue up to some limit, but will then create even more latency for new connections.
				// Try to process at least one request, especially useful when trying to debug the HTTP server itself
				const Int32 timeout = Config::cvHTTPKeepAliveIdleTimeoutSec.Get();
				if ( timeout > 0 && idleTimer.GetDelta() > timeout )
				{
					if ( processedAtLeastOneResponse || idleTimer.GetDelta() > Config::cvHTTPInitialResponseWaitSec.Get() )
					{
						// Close if other connections waiting. Of course can be a race with other threads and more than necessary will close,
						// but in general just want to keep the page responsive for normal use.
						Bool waitingConnections = false;
						{
							red::ScopedLock< red::Network::RawTcpManager > lock( m_network );
							waitingConnections = !m_waitQueue.Empty();
						}

						if ( waitingConnections )
						{
							RED_LOG( "Core: Idle HTTP connection ID %u will close", connection.GetConnectionID() );
							connection.Close();
							// exit the loop
							break;
						}
					}
				}
			}

			// nothing to send yet, schedule a long wait
			red::SleepOnCurrentThread( 50 );
			continue;
		}

		idle = false;
		processedAtLeastOneResponse = true;

		// send data
		// get data that is left to send
		// this will only return pointer to the data that is not yet send
		const void* payloadData = nullptr;
		Uint32 payloadSize = 0;
		sender->GetDataToSend( payloadData, payloadSize );

		// #fixme network: infinite loop if network error or payload size 0

		// send data
		if (payloadSize > 0)
		{
			const Uint32 numberOfBytesSent = connection.Send( payloadData, payloadSize );
			if (numberOfBytesSent > 0)
			{
				// advance internal state, this returns TRUE if we've send the whole message
				if (sender->AdvanceState( numberOfBytesSent ))
				{
					
					const Bool isWebSocketUpgrade = sender->IsWebSocketUpgrade();
	
					// was this neither an error response nor an upgrade response?
					const Bool hasContent = sender->HasContent();
					
					sender = nullptr;
					const Bool isError = (!isWebSocketUpgrade && !hasContent) || (isWebSocketUpgrade && !httpResponses.Empty());
					if ( isError )
					{
						RED_LOG( "Core: HTTP connection ID %u will close", connection.GetConnectionID() );
						connection.Close();
						// exit the loop
						break;		
					}
					else if ( isWebSocketUpgrade )
					{
						RED_LOG( "Finished sending WebSocket upgrade response on connection ID %u", connection.GetConnectionID() );
						// exit the loop
						break;
					}
				}
			}
		}
	}

	RED_LOG( "Finished HTTP request processing on connection ID %u", connection.GetConnectionID() );
}

namespace
{
	void SendWebSocketRawFrame( CHTTPConnection& connection, const Uint8* buffer, Uint32 toSend )
	{
		Uint32 totalSent = 0;
		while ( totalSent < toSend )
		{
			const Uint32 nsent = connection.Send( buffer + totalSent, toSend - totalSent );
			if ( nsent == 0 && !connection.IsActive() )
			{
				RED_LOG_ERROR( "WebSocket connection send error. Closing connection." );
				connection.Close();
				break;
			}
			totalSent += nsent;
		}
	}

	void SendWebSocketOutputFrame( CHTTPConnection& connection, const HTTPWebSocketOutputFrame& frame )
	{
		// headers
		{
			SendWebSocketRawFrame( connection, frame.m_headers, frame.m_headersSize );
		}

		// payload
		{
			const Uint8* buf = frame.m_dynamicData.Size() > 0 ? frame.m_dynamicData.TypedData() : frame.m_staticData.TypedData();
			const Uint32 toSend = frame.m_dataSize;
			SendWebSocketRawFrame( connection, buf, toSend );
		}
	}

	void EchoWebSocketPayload( CHTTPConnection& connection, Uint8 opcode, const HTTPWebSocketInputFrame& inputFrame )
	{
		Uint8 outputFrame[ WebSocketHeaderInfo::SMALL_MESSAGE_HEADER_BYTES + WebSocketHeaderInfo::SMALL_PAYLOAD_MAX ];
		WebSocketHeaderInfo headerInfo;
		red::Memzero( &headerInfo, sizeof( headerInfo ) );
		{
			headerInfo.m_final = true;
			headerInfo.m_opCode = opcode;
			headerInfo.m_payloadLength = inputFrame.m_payloadLength;
		}
		const Uint32 packedHeaderSize = PackWebSocketHeader( headerInfo, outputFrame, WebSocketHeaderInfo::SMALL_MESSAGE_HEADER_BYTES );
		RED_FATAL_ASSERT( packedHeaderSize != 0, "Failed to pack WebSocket header!" );
		RED_FATAL_ASSERT( packedHeaderSize == WebSocketHeaderInfo::SMALL_MESSAGE_HEADER_BYTES );
		red::Memcpy( outputFrame + WebSocketHeaderInfo::SMALL_MESSAGE_HEADER_BYTES, inputFrame.m_payload, inputFrame.m_payloadLength );
		SendWebSocketRawFrame( connection, outputFrame, WebSocketHeaderInfo::SMALL_MESSAGE_HEADER_BYTES + inputFrame.m_payloadLength );
	}

#ifdef RED_LOGGING_ENABLED
	void LogControlMessage( red::Network::TConnectionID connectionID, const HTTPWebSocketInputFrame& controlFrame )
	{
		if ( controlFrame.m_opcode == WebSocketHeaderInfo::eOpcode_Close )
		{
			Uint16 closeCode = 0;
			char utf8Description[ 127 ] = "<Unknown>";
			if (controlFrame.m_payloadLength >= sizeof( Uint16 ))
			{
				const Uint16 closeCode = red::SwapBytes16( *reinterpret_cast<const Uint16*>( controlFrame.m_payload ) );
				if (controlFrame.m_payloadLength > sizeof( Uint16 ))
				{
					red::Memcpy( utf8Description, controlFrame.m_payload + sizeof( Uint16 ), controlFrame.m_payloadLength - sizeof( Uint16 ) );
				}
			}
			RED_LOG( "WebSocket connection ID %u close requested by client: code %u, reason: %hs", connectionID, closeCode, utf8Description );
		}
		else if ( controlFrame.m_opcode == WebSocketHeaderInfo::eOpcode_Ping )
		{
			RED_LOG( "WebSocket connection ID %u received a ping, payload of %u byte(s)", connectionID, controlFrame.m_payloadLength );
		}
		else
		{
			RED_LOG( "WebSocket connection ID %u received unknown control frame with opcode %u", connectionID, controlFrame.m_opcode );
		}
	}
#endif
}

void CHTTPServerThread::LoopWebSocketFrames( CHTTPConnection& connection )
{
	RED_FATAL_ASSERT( connection.IsWebSocket() );

	RED_LOG( "Starting WebSocket loop on connection ID %u", connection.GetConnectionID() );

	// #tbd: seems like browsers are rude and just close the connection instead of sending a close control message...
	HTTPWebSocketInputFrame controlFrame;
	while ( connection.IsActive() && !connection.IsRequestedWebSocketShutdown() && !m_exit.GetValue() )
	{
		if ( connection.FlushControlWebSocketFrame( controlFrame ) )
		{
#ifdef RED_LOGGING_ENABLED
			LogControlMessage( connection.GetConnectionID(), controlFrame );
#endif
			if ( controlFrame.m_opcode == WebSocketHeaderInfo::eOpcode_Close )
			{	
				EchoWebSocketPayload( connection, WebSocketHeaderInfo::eOpcode_Close, controlFrame );
				connection.Close();
				break;
			}
			else if ( controlFrame.m_opcode == WebSocketHeaderInfo::eOpcode_Ping )
			{
				EchoWebSocketPayload( connection, WebSocketHeaderInfo::eOpcode_Pong, controlFrame );
			}
		}

		auto frame = connection.PopOutgoingWebSocketFrame();
		if ( !frame )
		{
			// #tbd
			red::SleepOnCurrentThread( 50 );
			continue;
		}

		// Send the frame
		SendWebSocketOutputFrame( connection, *frame );

		// Recycle the frame
		connection.RecycleWebSocketOutputFrame( std::move( frame ) );
	}

	if ( connection.IsRequestedWebSocketShutdown() )
	{
		RED_LOG( "WebSocket connection ID %u (gen %lu) shutdown requested", connection.GetConnectionID(), connection.GetGeneration() );
		connection.Close();
	}
}

void CHTTPServerThread::ThreadFunc()
{
#ifdef RED_PLATFORM_CONSOLE
	SetAffinityMask( RED_FLAG( 6 ) );
#endif

	// by default put into discardList along all paths
	// #tbd: bit convoluted ownership...
	class Releaser
	{
	public:
		Releaser( CHTTPServerThread* owner = nullptr )
			: m_owner( owner )
		{}

		void operator()( CHTTPConnection* ptr )
		{
			red::ScopedLock< red::Network::RawTcpManager > lock( m_owner->m_network );
			m_owner->m_activeSlot = nullptr;
			m_owner->m_discardList.PushBack( red::UniquePtr< CHTTPConnection >( ptr ) );
		}

	private:
		CHTTPServerThread* m_owner;
	};

	using ScopedRelease = red::UniquePtr< CHTTPConnection, Releaser >;
	ScopedRelease connection( nullptr, Releaser( this ) );

	for(;;)
	{
		m_numQueued.Acquire();
		if ( m_exit.GetValue() )
		{
			break;
		}
		
		{
			red::ScopedLock< red::Network::RawTcpManager > lock( m_network );
			RED_FATAL_ASSERT( !m_waitQueue.Empty() );
			connection.Reset( m_waitQueue.Front().ReleaseOwnership() );
			m_waitQueue.PopFront();
			RED_FATAL_ASSERT( connection );
			m_activeSlot = connection.Get();
		}
		RED_FATAL_ASSERT( connection );

		LoopHTTPResponses( *connection );
		if ( connection->IsActive() )
		{
			if ( connection->IsWebSocket() )
			{
				// Sanity check for any more queued HTTP responses
				red::CircularBuffer< red::UniquePtr< CHTTPResponseSender > > httpResponses{ red::PoolDebug() };
				connection->FlushHTTPResponses( httpResponses );
				if (httpResponses.Empty())
				{
					LoopWebSocketFrames( *connection );
				}
				else
				{
					RED_LOG_ERROR( "WebSocket had pending response messages on connection ID %u!", connection->GetConnectionID() );
				}	
			}
		}

#ifdef RED_PLATFORM_CONSOLE
		red::SleepOnCurrentThread( 1 );
#endif
	}
}

//-----

CHTTPServer::CHTTPServer()
	: m_initialized( false )
	, m_numWaitingConnections( 0, INT_MAX )
	, m_generationPool( 0 )
	, m_accumTimeDelta( 0. )
	, m_isShuttingDown( false )
	, m_handlers( red::PoolDebug() )
	, m_discardList( red::PoolDebug() )
{
}

CHTTPServer::~CHTTPServer()
{
}

Bool CHTTPServer::Initialize( Uint16& inOutStartPort, const Uint32 numPorts, const red::AbsolutePath& staticFilesPath, Bool reuseAddres )
{
	// already initialized
	if ( m_initialized )
		return false;

	// initialize network
	m_network.Initialize();

	// create HTTP listener
	const Uint16 startPort = inOutStartPort;
	red::Network::Utils::CommAddress addr;
	for ( Uint32 i = 0; i < numPorts; ++i )
	{
		const Uint16 bindPort = startPort + i;
		addr = red::Network::Utils::CommAddress( "0.0.0.0", bindPort );

		red::ScopedLock< red::Network::RawTcpManager > lock( m_network );
		m_listener = m_network.CreateListener( addr, reuseAddres, this );
		if ( m_listener )
		{
			inOutStartPort = bindPort;
			break;
		}
	}

	if (!m_listener)
	{
		m_network.Shutdown(); // make sure the network interface is closed
		return false;
	}

	{
		const Int32 numThreads = Config::cvHTTPNumThreads.Get(); 
		for (Int32 i = 0, len = numThreads; i < len; ++i)
		{
			m_threadConnections.PushBack( nullptr );
			const CHTTPServerThread::SharedResources shared( m_waitQueue, m_discardList, m_threadConnections[i], m_numWaitingConnections, m_network );
			m_threads.PushBack( red::CreateUniquePtr< CHTTPServerThread >( shared ) );
		}
		for ( Uint32 i = 0; i < m_threads.Size(); ++i )
		{
			m_threads[i]->InitThread();
		}
		
		// #tbd: console affinity mask and priority. E.g., 7th core, lower pri
#ifdef RED_PLATFORM_DURANGO

#elif defined( RED_PLATFORM_ORBIS )

#endif
	}

	// initialized
	m_staticFilesAbsolutePath = staticFilesPath;
	m_initialized = true;
	return true;
}

void CHTTPServer::Shutdown()
{
	// stop network service
	if ( m_initialized )
	{
		// Don't accept any more connections
		// Narrow locking scope, since to avoid deadlock with threads trying to push
		// onto the discard list
		{
			red::ScopedLock< red::Network::RawTcpManager > lock( m_network );
			m_isShuttingDown = true;
		}

		// Request exit for all threads, then release semaphore
		// So can't have race where set exit flag for one thread, but another gets the semaphore
		for ( auto& it : m_threads )
		{
			it->RequestExit();
		}
		m_numWaitingConnections.Release( m_threads.Size() );

		for ( auto& it : m_threads )
		{
			it->JoinThread();
		}
		m_threads.Clear();

		{
			red::ScopedLock< red::Network::RawTcpManager > lock( m_network );

			// close listener
			if ( m_listener )
			{
				m_network.CloseListener( m_listener );
				m_listener = 0;
			}
		}

		// close network
		m_network.Shutdown();

		m_waitQueue.Clear();
		m_discardList.Clear();

		// mark as closed
		m_initialized = false;
	}
}

void CHTTPServer::Update( Float timeDelta )
{
	red::StaticArray< CHTTPConnection*, MAX_THREADS > activeHTTPConnections;
	red::StaticArray< CHTTPConnection*, MAX_THREADS > activeWebSockets;

	m_accumTimeDelta += timeDelta;

	// filter out closed connections, prepare list of connections to process
	{
		red::ScopedLock< red::Network::RawTcpManager > lock( m_network );

		for ( Uint32 i = 0; i < m_discardList.Size(); )
		{
			if ( !m_discardList[ i ]->IsActive() )
				m_discardList.RemoveAtReorder( i );
			else
				++i;
		}

		for ( auto it : m_threadConnections )
		{
			if ( !it )
			{
				// Normal inactive slot
				continue;
			}
			if ( it->IsWebSocket() )
			{
				activeWebSockets.PushBack( it );
			}
			else
			{
				activeHTTPConnections.PushBack( it );
			}
		}

		const Int32 maxWebSockets = Config::cvHTTPNumActiveWebSockets.Get();
		if ( maxWebSockets > 0 && static_cast< Int32 >( activeWebSockets.Size() ) > maxWebSockets )
		{
			// Shutdown oldest WebSocket connection
			std::sort( activeWebSockets.Begin(), activeWebSockets.End(), []( CHTTPConnection* a, CHTTPConnection* b  ){ return a->GetGeneration() > b->GetGeneration(); } );
			for ( Uint32 i = maxWebSockets; i < activeWebSockets.Size(); ++i )
			{
				activeWebSockets[i]->RequestShutdownWebSocket();
			}
			activeWebSockets.Resize( maxWebSockets );
		}

		m_stats.m_numActiveHttpConnections = activeHTTPConnections.Size();
		m_stats.m_numActiveWebSockets = activeWebSockets.Size();
		m_stats.m_waitQueueSize = m_waitQueue.Size();
	}

	// process requests from active connections
	for ( auto it : activeHTTPConnections )
	{	
		for ( auto request = it->PopHTTPRequest(); request; request = it->PopHTTPRequest() )
		{
			if ( request->m_websocketUpgrade )
			{
				ServiceWebSocketRequest( it, *request );
			}
			else
			{
				ServiceHTTPRequest( it, *request );
			}
		}
	}

	// Process WebSocket input data frames
	for ( auto it : activeWebSockets )
	{
		WebSocketEndpoint* endpoint = reinterpret_cast<WebSocketEndpoint*>( it->GetServerPrivateData() );
		RED_FATAL_ASSERT( endpoint );

		for ( auto inputFrame = it->PopIncomingWebSocketFrame(); inputFrame; inputFrame = it->PopIncomingWebSocketFrame() )
		{
			if ( inputFrame->m_opcode != WebSocketHeaderInfo::eOpcode_TextFrame && inputFrame->m_opcode != WebSocketHeaderInfo::eOpcode_BinaryFrame )
			{
				RED_LOG_WARNING( "Unexpected WebSocket input frame opcode %u", inputFrame->m_opcode );
				continue;
			}
			const auto payloadType = (inputFrame->m_opcode == WebSocketHeaderInfo::eOpcode_TextFrame) ? WebSocketEndpoint::EWebSocketPayloadType::TextUtf8 : WebSocketEndpoint::EWebSocketPayloadType::Binary;
			CMemoryFileReaderExternalBuffer reader( inputFrame->m_payload, inputFrame->m_payloadLength );
			endpoint->OnReadPayload( payloadType, reader );
		}
	}

	// Process WebSocket output data frames
	for ( auto it : activeWebSockets )
	{
		WebSocketEndpoint* endpoint = reinterpret_cast< WebSocketEndpoint* >( it->GetServerPrivateData() );
		RED_FATAL_ASSERT( endpoint );

		const Float webSocketUpdateFrequency = endpoint->GetUpdateFrequency();
		const Float updateFreq = webSocketUpdateFrequency >= 0.f ? webSocketUpdateFrequency : Config::cvHTTPWebSocketOutputUpdateFrequency.Get();
		const Double diff = m_accumTimeDelta - endpoint->GetLastUpdateTime();
		if ( diff >= updateFreq )
		{
			auto frame = it->TryAcquireOutputFrame();
			if ( !frame )
			{
				// Need to wait until the connection is finished with the current frames
				continue;
			}

			endpoint->SetLastUpdateTime( m_accumTimeDelta );
			ProcessWebSocket( it, endpoint, std::move( frame ) );
		}
	}
}

void CHTTPServer::ProcessWebSocket( CHTTPConnection* connection, WebSocketEndpoint* endpoint, red::UniquePtr< HTTPWebSocketOutputFrame > frame )
{
	auto& staticData = frame->m_staticData;
	auto& dynamicData = frame->m_dynamicData;

	red::Memzero( staticData.TypedData(), staticData.Size() );
	dynamicData.Clear();
	{
		CMemoryFileBufferedWriter writer{ staticData.TypedData(), staticData.Size(), dynamicData };

		const WebSocketEndpoint::EWebSocketPayloadType payloadType = endpoint->OnWritePayload( writer );
		const Uint32 payloadSize = writer.GetRealSize();
		if ( payloadSize > 0 )
		{
			RED_FATAL_ASSERT( payloadSize <= HTTPWebSocketOutputFrame::MAX_PAYLOAD_SIZE, "Given payload size is bigger then maximum payload size" );

			if ( dynamicData.Size() == 0 && dynamicData.DataCapacity() > 0 )
			{
				dynamicData = red::DynArray< Uint8 >( red::PoolDebug() );
			}

			WebSocketHeaderInfo headerInfo;
			red::Memzero( &headerInfo, sizeof( headerInfo ) );
			{
				headerInfo.m_final = true;
				headerInfo.m_opCode = payloadType == WebSocketEndpoint::EWebSocketPayloadType::TextUtf8 ? WebSocketHeaderInfo::eOpcode_TextFrame : WebSocketHeaderInfo::eOpcode_BinaryFrame;
				headerInfo.m_payloadLength = payloadSize;
			}
			Uint8 packedHeader[ WebSocketHeaderInfo::LARGE_MESSAGE_HEADER_BYTES ];
			Uint32 packedHeaderSize = PackWebSocketHeader( headerInfo, packedHeader, sizeof( packedHeader ) );
			RED_FATAL_ASSERT( packedHeaderSize != 0, "Failed to pack WebSocket header!" );
			RED_FATAL_ASSERT( packedHeaderSize <= HTTPWebSocketOutputFrame::MAX_HEADER_SIZE );
			red::Memcpy( frame->m_headers, packedHeader, packedHeaderSize );
			frame->m_headersSize = packedHeaderSize;
			frame->m_dataSize = payloadSize;
			connection->PushOutgoingWebSocketFrame( std::move( frame ) );
		}
		else
		{
			connection->RecycleWebSocketOutputFrame( std::move( frame ) );
		}
	}
}

void CHTTPServer::ServiceHTTPRequest( CHTTPConnection* connection, const class CHTTPRequest& request ) const
{
	ServiceRequest_NoLock( request, OnHTTPResponse( red::PoolDebug(),
		[this, connection, websocketUpgrade = request.m_websocketUpgrade]
		( HTTPResponseDataPtr responseData, CHTTPResponseData::EResultCode returnCode )
		{
			if ( !connection->IsActive() )
			{
				RED_LOG_ERROR( "Dropped HTTP connection" );
				return;
			}

			if ( responseData )
			{
				if ( websocketUpgrade )
				{
					const WebSocketEndpoint* endpoint = responseData->GetWebSocketEndpoint();
					if ( !endpoint )
					{
						connection->PushHTTPResponse( CreateHTTPResponse( CHTTPResponseData::eResultCode_NotFound ) );
					}
					else if ( responseData->GetData() )
					{
						connection->PushHTTPResponse( CreateHTTPResponse( CHTTPResponseData::eResultCode_BadRequest ) );
					}
				}
				else if ( responseData->GetData() )
				{
					connection->PushHTTPResponse( CreateHTTPResponse( responseData->GetData(), responseData->GetContentType() ) );
				}
				else
				{
					connection->PushHTTPResponse( CreateHTTPResponse( responseData->GetResultCode() ) );
				}
			}
			else
			{
				connection->PushHTTPResponse( CreateHTTPResponse( returnCode ) );
			}
		} )
	);
}

void CHTTPServer::ServiceWebSocketRequest( CHTTPConnection* connection, const class CHTTPRequest& request ) const
{
	ServiceWebSocketRequest_NoLock( request, OnHTTPResponse( red::PoolDebug(),
		[this, connection, secWebSocketKey = request.m_secWebSocketKey, secWebSocketVersion = request.m_secWebSocketVersion]
		( HTTPResponseDataPtr responseData, CHTTPResponseData::EResultCode returnCode )
		{
			if ( !connection->IsActive() )
			{
				RED_LOG_ERROR( "Dropped HTTP connection" );
				return;
			}

			if ( secWebSocketVersion != WEBSOCKET_PROTOCOL_VERSION_TXT )
			{
				connection->PushHTTPResponse( CreateHTTPResponse( CHTTPResponseSender::eWebSocket, CHTTPResponseData::eResultCode_UpgradeRequired, "", "", WEBSOCKET_PROTOCOL_VERSION_TXT ) );
			}
			else if ( responseData )
			{
				WebSocketEndpoint* endpoint = responseData->GetWebSocketEndpoint();
				if ( !endpoint )
				{
					connection->PushHTTPResponse( CreateHTTPResponse( CHTTPResponseData::eResultCode_NotFound ) );
				}
				else
				{
					// #tbd: don't totally like this. Setting this before the response is seen by the server thread and upgrades to a WebSocket.
					connection->SetServerPrivateData( endpoint );

					if ( responseData->GetData() )
					{
						connection->PushHTTPResponse( CreateHTTPResponse( CHTTPResponseData::eResultCode_BadRequest ) );
					}
					else
					{
						connection->PushHTTPResponse( CreateHTTPResponse( CHTTPResponseSender::eWebSocket, CHTTPResponseData::eResultCode_SwitchingProtocols, secWebSocketKey, "" /*responseMetadata.m_secWebSocketProtocols*/, WEBSOCKET_PROTOCOL_VERSION_TXT ) );
					}
				}
			}
			else
			{
				connection->PushHTTPResponse( CreateHTTPResponse( returnCode ) );
			}
		} )
	);
}

void CHTTPServer::ServiceRequest_NoLock( const class CHTTPRequest& request, const OnHTTPResponse& onResponse ) const
{
	RED_FATAL_ASSERT( !request.m_websocketUpgrade );

	// URL to get a static file
	if ( request.m_url.BeginsWith( "/static/") && request.m_method == "GET" )
	{
		if ( request.m_websocketUpgrade )
		{
			onResponse( nullptr, CHTTPResponseData::eResultCode_NotFound );
			return;
		}
		
		ServiceFileRequest_NoLock( request, onResponse );
		return;
	}

	// go through the registered handlers
	for ( auto& it : m_handlers )
	{
		if ( it->OnHTTPRequest( request, onResponse ) )
		{
			return;
		}
	}

	// not handled
	onResponse( nullptr, CHTTPResponseData::eResultCode_NotFound );
}

void CHTTPServer::ServiceFileRequest_NoLock( const class CHTTPRequest& request, const OnHTTPResponse& onResponse ) const
{
	// no file serving dir
	if ( m_staticFilesAbsolutePath.Empty() )
	{
		onResponse(nullptr, CHTTPResponseData::eResultCode_BadRequest );
		return;
	}

	// Get the relative path to the file being requested
	String relativePath = request.m_url.StringAfter( "static/" );
	relativePath.ToLower();

	// conform the path
	red::AbsolutePath absoluteFilePath = m_staticFilesAbsolutePath.AddFilePath( relativePath );

	// LOG ALL REQUESTS - to have an evidence of people snooping
	RED_LOG( "Core: HTTP file request: '%hs'", absoluteFilePath.AsChar() );

	//
	// TODO: Check to see if the path give does not recurse lower than permitted in the file system
	//

	// open the file - always use RELATIVE PATH - to prevent people from snooping on your hard drive !!!
	auto inputFile = GFileManager->CreateFileReader( absoluteFilePath, FOF_AbsolutePath );
	if ( !inputFile )
	{
		onResponse( nullptr, CHTTPResponseData::eResultCode_NotFound );
		return;
	}

	// do not send files that are to big or empty
	const Uint32 fileSize = (Uint32) inputFile->GetSize();
	const Uint32 maxSize = Config::cvHTTPMaxFileTransferSizeKB.Get() * 1024;
	if ( (fileSize > maxSize) || !fileSize )
	{
		onResponse( nullptr, CHTTPResponseData::eResultCode_Forbidden );
		return;
	}

	// Only few types of files are allowed 
	// TODO: maybe we should have this somewhere else
	String contentType;
	const red::StringView fileExt = red::paths::GetExtension( relativePath );
	if ( fileExt == "js" )
	{
		contentType = "application/javascript";
	}
	else if ( fileExt == "css" )
	{
		contentType = "text/css";
	}
	else if ( fileExt == "xml" )
	{
		contentType = "text/xml";
	}
	else if ( fileExt == "txt" || fileExt == "csv ")
	{
		contentType = "text/plain";
	}
	else if ( fileExt == "json" )
	{
		contentType = "application/json";
	}
	else if ( fileExt == "png" )
	{
		contentType = "image/png";
	}
	else if ( fileExt == "jpg" || fileExt == "jpeg")
	{
		contentType = "image/jpeg";
	}
	else if ( fileExt == "gif" )
	{
		contentType = "image/gif";
	}
	else
	{
		// for security we may disallow getting files of any format
		if ( !Config::cvHTTPAllowAllFiles.Get() )
		{
			onResponse( nullptr, CHTTPResponseData::eResultCode_Forbidden );
			return;
		}

		// if it's allowed, use a generic format for it
		contentType = "application/octet-stream";
	}

	// send the content of the file
	DataBlobPtr data = red::CreateUniquePtr< CDataBlob >( fileSize );
	inputFile->Serialize( data->GetData(), data->GetDataSize() );

	onResponse( red::CreateUniquePtr< CHTTPResponseData >( contentType, data ), CHTTPResponseData::eResultCode_OK );
}

void CHTTPServer::ServiceWebSocketRequest_NoLock( const class CHTTPRequest& request, const OnHTTPResponse& onResponse ) const
{
	RED_FATAL_ASSERT( request.m_websocketUpgrade );

	// Do some basic WebSocket validation
	if ( request.m_method != "GET" )
	{
		onResponse( nullptr, CHTTPResponseData::eResultCode_BadRequest );
		return;
	}

	if ( request.m_secWebSocketVersion != WEBSOCKET_PROTOCOL_VERSION_TXT )
	{
		onResponse( nullptr, CHTTPResponseData::eResultCode_UpgradeRequired );
		return;
	}

	if ( request.m_url.BeginsWith( "/static/") )
	{
		onResponse( nullptr, CHTTPResponseData::eResultCode_NotFound );
		return;
	}

	// go through the registered handlers
	for ( auto& it : m_handlers )
	{
		if ( it->OnHTTPRequest( request, onResponse ) )
		{
			return;
		}
	}

	// not handled
	onResponse( nullptr, CHTTPResponseData::eResultCode_NotFound );
}

void CHTTPServer::RegisterHandler( IHTTPRequestHandler* handler )
{
	red::ScopedLock< red::Network::RawTcpManager > lock( m_network );
	m_handlers.PushBack( handler );
}

void CHTTPServer::UnregisterHandler( IHTTPRequestHandler* handler )
{
	red::ScopedLock< red::Network::RawTcpManager > lock( m_network );
	m_handlers.Remove( handler );
	
	// #fixme: remove websocket handlers. When actually updating them no longer have the lock...
}

void CHTTPServer::GetStats( Stats& outStats ) const
{
	red::ScopedLock< red::Network::RawTcpManager > lock( m_network );
	outStats = m_stats;
}

void CHTTPServer::OnClosed( const red::Network::TListenerID listenerID )
{
	// close the listener
	if ( m_listener == listenerID )
	{
		RED_LOG_WARNING( "Core: HTTP server socket closed" );
		m_listener = 0;
	}
}

bool CHTTPServer::OnConnection( const red::Network::TListenerID listenerID, const red::Network::TConnectionID connectionID, red::Network::IRawConnectionInterface*& outConnectionInterface )
{
	RED_LOG( "Core: HTTP server new connection %d", connectionID );
	auto tmp = red::CreateUniquePtr< CHTTPConnection >( m_network, connectionID, m_generationPool.Increment() - 1 );
	Bool ret = false;
	{
		if (m_isShuttingDown)
		{
			RED_LOG( "Core: HTTP server rejecting new connection %d on shutdown", connectionID );
			return false;
		}

		if ( static_cast<Int32>( m_waitQueue.Size() ) < Config::cvHTTPWaitQueueBacklog.Get() )
		{
			outConnectionInterface = tmp.Get();
			m_waitQueue.PushBack( std::move( tmp ) );
			ret = true;
		}
		else
		{
			RED_LOG_WARNING( "Dropping connection %d - max wait queue exceeded", connectionID );
		}
	}

	if ( ret )
	{
		m_numWaitingConnections.Release();
	}

	return ret;
}

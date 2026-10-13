/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//---------------------------------

#include "httpRequestHandler.h"
#include "../../../common/redNetwork/include/rawTcpManager.h"
#include "../../../common/redContainers/include/circularBuffer.h"
#include "basicDataBlob.h"
#include "webSocketFrame.h"
#include "webSocketFrameParser.h"

//---------------------------------

class WebSocketEndpoint;

struct HTTPWebSocketOutputFrame
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

	static const Uint32 DEFAULT_PAYLOAD_SIZE = static_cast< Uint32 >( 8_KB );
	static const Uint32 MAX_PAYLOAD_SIZE = static_cast< Uint32 >( 10_MB );
	static const Uint32 MAX_HEADER_SIZE = WebSocketHeaderInfo::LARGE_MESSAGE_HEADER_BYTES;
	static_assert( MAX_PAYLOAD_SIZE <= WebSocketHeaderInfo::LARGE_PAYLOAD_MAX, "A larger payload requires more header bytes!" );

	Uint8 m_headers[ MAX_HEADER_SIZE ];

	red::StaticArray< Uint8, DEFAULT_PAYLOAD_SIZE > m_staticData;
	red::DynArray< Uint8 > m_dynamicData{ red::PoolDebug() };

	Uint32 m_headersSize;
	Uint32 m_dataSize;
};

struct HTTPWebSocketInputFrame
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

	static const Uint32 MAX_PAYLOAD_SIZE = WebSocketHeaderInfo::SMALL_PAYLOAD_MAX;

	Uint8	m_payload[ MAX_PAYLOAD_SIZE ];
	Uint8	m_opcode;
	Uint32	m_payloadLength;
};

class CHTTPConnection : public red::Network::IRawConnectionInterface
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	typedef red::Network::RawTcpManager RawTcpManager;
	typedef red::Network::IRawConnectionInterface IRawConnectionInterface;
	typedef red::Network::TConnectionID TConnectionID;

	CHTTPConnection( red::Network::RawTcpManager& network, TConnectionID connectionID, Uint64 generation );
	~CHTTPConnection();
	CHTTPConnection( const CHTTPConnection& ) = delete;
	CHTTPConnection& operator=( const CHTTPConnection& ) = delete;

	// IRawConnectionInterface interface
	virtual void OnDisconnected( const TConnectionID connectionID ) override;
	virtual void OnData( const void* data, const Uint32 dataSize, const TConnectionID connectionID ) override;

public:
	red::UniquePtr< CHTTPRequest > PopHTTPRequest();
	void PushHTTPResponse( red::UniquePtr< class CHTTPResponseSender > response );
	void FlushHTTPResponses( red::CircularBuffer< red::UniquePtr< class CHTTPResponseSender > >& outResponses );
	void RequestShutdownWebSocket();
	Bool IsRequestedWebSocketShutdown() const;
	void PushOutgoingWebSocketFrame( red::UniquePtr< HTTPWebSocketOutputFrame > frame );
	red::UniquePtr< HTTPWebSocketOutputFrame > PopOutgoingWebSocketFrame();
	void PushIncomingWebSocketFrame( red::UniquePtr< HTTPWebSocketInputFrame > frame );
	red::UniquePtr< HTTPWebSocketInputFrame > PopIncomingWebSocketFrame();
	Bool FlushControlWebSocketFrame( HTTPWebSocketInputFrame& outControlFrame );

public:
	void UpgradeConnectionToWebSocket();

public:
	Uint64 GetGeneration() const { return m_generation; }
	Bool IsActive() const { return m_connectionActive; }
	Bool IsWebSocket() const { return m_isWebSocket; }
	TConnectionID GetConnectionID() const { return m_id; }
	Uint32 Send( const void* buffer, Uint32 size );
	void Close();

public:
	red::UniquePtr< HTTPWebSocketOutputFrame > TryAcquireOutputFrame();
	void RecycleWebSocketOutputFrame( red::UniquePtr< HTTPWebSocketOutputFrame > frame );

public:
	// Data set and accessed only by the server.
	void* GetServerPrivateData() const { return m_serverPrivateData; }
	void SetServerPrivateData( void* data ) { m_serverPrivateData = data; }

private:
	RawTcpManager* m_network;
	TConnectionID m_id;

private:
	red::Mutex m_lock;

	// HTTP data
	red::UniquePtr< class CHTTPRequestParser > m_parser;
	red::CircularBuffer< red::UniquePtr< class CHTTPRequest > > m_pendingHttpRequests{ red::PoolDebug() };
	red::CircularBuffer< red::UniquePtr< class CHTTPResponseSender > > m_pendingHttpResponses{ red::PoolDebug() };

	// WebSocket data
	red::UniquePtr< class CWebSocketFrameParser > m_webSocketParser;
	red::CircularBuffer< red::UniquePtr< HTTPWebSocketOutputFrame > > m_outgoingWebSocketFrames{ red::PoolDebug() };
	red::CircularBuffer< red::UniquePtr< HTTPWebSocketInputFrame > > m_incomingWebSocketFrames{ red::PoolDebug() };
	red::UniquePtr< HTTPWebSocketInputFrame  > m_controlWebSocketFrame;
	red::Atomic< Bool >	m_requestShutdownWebSocket;
	red::StaticArray< red::UniquePtr< HTTPWebSocketOutputFrame >, 2 > m_outputFramePool; // double-buffered, one for sending another for getting next frame
	
	// Common data
	Uint64 m_generation;
	Bool m_connectionActive;
	Bool m_isWebSocket;
	red::UniquePtr< CWebSocketFrameParser > m_webSocketFrameParser;
	red::UniquePtr< HTTPWebSocketInputFrame > m_incomingFrame;

	static CWebSocketFrameParser::EResult OnWebsocketDataFrame( CWebSocketFrameParser::CallbackToken& token, const WebSocketHeaderInfo& headerInfo );
	static CWebSocketFrameParser::EResult OnWebsocketControlFrame( CWebSocketFrameParser::CallbackToken& token, const WebSocketHeaderInfo& headerInfo, const void* unmaskedPayload );

	// Server private data
	void* m_serverPrivateData;
};

//---------------------------------

class CHTTPServerThread : public red::Thread
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	struct SharedResources
	{
		SharedResources( red::CircularBuffer< red::UniquePtr< CHTTPConnection > >& waitQueue,
						 red::DynArray< red::UniquePtr< CHTTPConnection > >& discardList,
						 CHTTPConnection*& activeSlot,
						 red::Semaphore& numQueued,
						 red::Network::RawTcpManager& network )
			: m_waitQueue( &waitQueue )
			, m_discardList( &discardList )
			, m_activeSlot( &activeSlot )
			, m_numQueued( &numQueued )
			, m_network( &network )
		{}

		red::CircularBuffer< red::UniquePtr< CHTTPConnection > >* m_waitQueue;
		red::DynArray< red::UniquePtr< CHTTPConnection > >* m_discardList;
		CHTTPConnection** m_activeSlot;
		red::Semaphore* m_numQueued;
		red::Network::RawTcpManager* m_network;
	};

public:
	explicit CHTTPServerThread( const SharedResources& shared );
	~CHTTPServerThread();
	CHTTPServerThread( const CHTTPServerThread& ) = delete;
	CHTTPServerThread& operator=( const CHTTPServerThread& ) = delete;
	void RequestExit();

private:
	virtual void ThreadFunc() override;

private:
	void LoopHTTPResponses( CHTTPConnection& connection );
	void LoopWebSocketFrames( CHTTPConnection& connection );

private:
	red::CircularBuffer< red::UniquePtr< CHTTPConnection > >& m_waitQueue;
	red::DynArray< red::UniquePtr< CHTTPConnection > >& m_discardList;
	CHTTPConnection*& m_activeSlot;
	red::Semaphore& m_numQueued;
	red::Network::RawTcpManager& m_network;
	red::Atomic<Bool> m_exit;
};

//---------------------------------

/// In-game simple HTTP server
class RED_CORE_API CHTTPServer : public red::Network::IRawListenerInterface
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	static constexpr Uint32 MAX_THREADS = 8;

	struct Stats
	{
		RED_FORCE_INLINE Stats()
			: m_numActiveHttpConnections( 0 )
			, m_numActiveWebSockets( 0 )
			, m_waitQueueSize( 0 )
		{}

		Uint32 m_numActiveHttpConnections;
		Uint32 m_numActiveWebSockets;
		Uint32 m_waitQueueSize;
	};

public:
	CHTTPServer();
	~CHTTPServer();

	// Start sever at given point
	Bool Initialize( Uint16& inOutStartPort, const Uint32 numPorts, const red::AbsolutePath& staticFilesPath, Bool reuseAddress = false );

	// Shutdown server
	void Shutdown();

	// Process synchronous requests/websockets
	void Update( Float timeDelta );

	// Register HTTP request handler (we may have more than one)
	void RegisterHandler( IHTTPRequestHandler* handler );

	// Unregister HTTP request handler
	void UnregisterHandler( IHTTPRequestHandler* handler );

	void GetStats( Stats& outStats ) const;

private:
	void ProcessWebSocket( CHTTPConnection* connection, WebSocketEndpoint* endpoint, red::UniquePtr< HTTPWebSocketOutputFrame > frame );

	// red::Network::IRawListenerInterface
	virtual void OnClosed( const red::Network::TListenerID listenerID ) override;
	virtual bool OnConnection( const red::Network::TListenerID listenerID, const red::Network::TConnectionID connectionID, red::Network::IRawConnectionInterface*& outConnectionInterface ) override;

	// Service function
	void ServiceHTTPRequest( CHTTPConnection* connection, const class CHTTPRequest& request ) const;
	void ServiceWebSocketRequest( CHTTPConnection* connection, const class CHTTPRequest& request ) const;
	void ServiceRequest_NoLock( const class CHTTPRequest& request, const OnHTTPResponse& onResponse ) const;
	void ServiceFileRequest_NoLock( const class CHTTPRequest& request, const OnHTTPResponse& onResponse ) const;
	void ServiceWebSocketRequest_NoLock( const class CHTTPRequest& request, const OnHTTPResponse& onResponse ) const;

	// network access
	mutable red::Network::RawTcpManager		m_network;
	red::Network::TListenerID				m_listener;

	// static file access directory (server "root")
	red::AbsolutePath							m_staticFilesAbsolutePath;

	// state
	Bool									m_initialized;

	// active listeners
	red::DynArray< IHTTPRequestHandler* >	m_handlers;

	// active connections, upgraded connections are pushed back onto the front
	red::CircularBuffer< red::UniquePtr< CHTTPConnection > >				m_waitQueue{ red::PoolDebug() };
	red::DynArray< red::UniquePtr< CHTTPConnection > >						m_discardList;
	red::Semaphore															m_numWaitingConnections;
	red::StaticArray< red::UniquePtr< CHTTPServerThread >, MAX_THREADS >	m_threads;
	red::StaticArray< CHTTPConnection*, MAX_THREADS >						m_threadConnections;
	red::Atomic< Uint64 >													m_generationPool;
	Double																	m_accumTimeDelta;
	Bool 																	m_isShuttingDown;

	Stats m_stats;
};

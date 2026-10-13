/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "tcpMessageHandler.h"

namespace red
{
namespace
{
	static const Uint32 c_reconnectDelayInSeconds = 3;
}
	TcpMessageHandler::TcpMessageHandler( const TcpMessageHandlerParameter& param, red::Atomic< Bool >& processIsRunning )
		: IpcMessageHandler( "TcpMessageHandler", param.ipcParameter, processIsRunning )
		, m_onConnectedCallback( param.onConnectedCallback )
		, m_serverAddress( param.address )
	{
		m_socket = CreateUniquePtr< Network::Socket >();
		ConnectToServer();
	}

	TcpMessageHandler::~TcpMessageHandler()
	{
		if ( m_socket )
		{
			m_socket->Close();
		}
	}

	Network::Socket* TcpMessageHandler::GetSocket() const
	{
		return m_socket.Get();
	}

	void TcpMessageHandler::ThreadFunc()
	{
		red::Timer timer;

		while ( m_processIsRunning.GetValue() )
		{
			if ( m_socket->IsConnecting() )
			{
				if ( m_socket->FinishConnecting() )
				{
					if ( m_socket->IsConnected() )
					{
						m_onConnectedCallback();
					}
				}
				else
				{
					SleepOnCurrentThread( 1 );
				}

				continue;
			}
			else if ( m_socket->IsDropped() || m_socket->IsClosed() )
			{
				if ( timer.GetSeconds() < c_reconnectDelayInSeconds )
				{
					SleepOnCurrentThread( 1 );
				}
				else
				{
					timer.Reset();
					ConnectToServer();
				}

				continue;
			}
			else if ( !m_socket->IsOpen() )
			{
				RED_LOG_ERROR( "Failed to connect to RedListener" );
				return;
			}

			if ( ReadMessage() )
			{
				OnMessage();
			}
		}
	}

	Bool TcpMessageHandler::ReadMessage()
	{
		char buffer[ RED_KILO_BYTE( 2 ) ] = { '\0' };
		const Uint32 size = m_socket->Receive( buffer, RED_ARRAY_COUNT( buffer ) );

		if ( m_socket->IsDropped() )
		{
			return false;
		}

		if ( size > 0 )
		{
			Uint32 bytesWritten = m_ringBuffer.Write( buffer, size );
			if ( bytesWritten < size )
			{
				while ( ParseMessage() )
				{
					OnMessage();
				}

				const Uint32 bytesToWrite = size - bytesWritten;
				bytesWritten = m_ringBuffer.Write( buffer + bytesWritten, bytesToWrite);
				RED_ASSERT( bytesWritten == bytesToWrite, "Failed to write all bytes." );
			}

			return ParseMessage();
		}
		else
		{
			SleepOnCurrentThread( 1 );
		}

		return false;
	}

	void TcpMessageHandler::ConnectToServer()
	{
		if ( !m_socket->Create() )
		{
			RED_LOG_ERROR( "Failed to create socket: errorCode=%ld", m_socket->GetLastError() );
			return;
		}

		if ( !m_socket->Connect( m_serverAddress ) )
		{
			char ipAddress[ 64 ] = { '\0' };
			m_serverAddress.GetIp( ipAddress, RED_ARRAY_COUNT( ipAddress ) );
			RED_LOG_ERROR( "Connection to %s failed. Error code=%ld", ipAddress, m_socket->GetLastError() );
		}
	}
}
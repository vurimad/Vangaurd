/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "namedPipeMessageHandler.h"

namespace red
{
	NamedPipeMessageHandler::NamedPipeMessageHandler( const NamedPipeMessageHandlerParameter& param, red::Atomic< Bool >& processIsRunning )
		: IpcMessageHandler( "NamedPipeMessageHandler", param.ipcParameter, processIsRunning )
		, m_namedPipeBufferSize( param.namedPipeBufferSize )
	{
		RED_ASSERT( m_namedPipeBufferSize > 0, "Provied named pipe buffer size is invalid" );

		m_namedPipe = red::CreateUniquePtr< red::Network::FullDuplexPipe >( param.namedPipeName, m_namedPipeBufferSize );

		if ( param.isServer )
		{
			m_namedPipe->Open();
			m_verifyConnection = &NamedPipeMessageHandler::VerifyServerConnection;
		}
		else
		{
			m_namedPipe->Connect();
			m_verifyConnection = &NamedPipeMessageHandler::VerifyClientConnection;
		}
	}

	NamedPipeMessageHandler::~NamedPipeMessageHandler()
	{
		if ( m_namedPipe )
		{
			m_namedPipe->Close();
		}
	}

	red::Network::FullDuplexPipe* NamedPipeMessageHandler::GetNamedPipe() const
	{
		return m_namedPipe.Get();
	}

	void NamedPipeMessageHandler::ThreadFunc()
	{
		while ( m_processIsRunning.GetValue() )
		{
			if ( !( this->*m_verifyConnection )() )
			{
				SleepOnCurrentThread( 1 );
				continue;
			}

			{
				ScopedLock< SpinLock > lock{ m_messagesToSendQueueLock };

				while ( !m_messagesToSendQueue.Empty() )
				{
					const auto message = m_messagesToSendQueue.Pop();
					m_namedPipe->Send( message.buffer.TypedData(), message.buffer.Size() );
				}
			}

			ReadMessage();
		}
	}

	void NamedPipeMessageHandler::SendMessage( const char* buffer, Uint32 bufferSize )
	{
		ArraySpan< const char > messageSpan( buffer, bufferSize );
		Message message{ DynArray< char >{ messageSpan, red::PoolCore() } };

		ScopedLock< SpinLock > lock{ m_messagesToSendQueueLock };
		m_messagesToSendQueue.Push( std::move( message ) );
	}

	void NamedPipeMessageHandler::ReadMessage()
	{
		red::DynArray< HANDLE > events{ red::PoolBackend() };
		const Uint32 size = m_namedPipe->Receive(events);

		if ( m_namedPipe->IsError() )
		{
			return;
		}

		if ( size > 0 )
		{
			Uint32 bytesWritten = m_ringBuffer.Write( static_cast< const char* >( m_namedPipe->GetReadBuffer() ), size );
			if ( bytesWritten < size )
			{
				while ( ParseMessage() )
				{
					OnMessage();
				}

				const Uint32 bytesToWrite = size - bytesWritten;
				bytesWritten = m_ringBuffer.Write( static_cast< const char* >( m_namedPipe->GetReadBuffer() ) + bytesWritten, bytesToWrite );
				RED_ASSERT( bytesWritten == bytesToWrite, "Failed to write all bytes." );
				return;
			}

			while ( ParseMessage() )
			{
				OnMessage();
			}
		}
		else
		{
			SleepOnCurrentThread( 1 );
		}
	}

	Bool NamedPipeMessageHandler::VerifyClientConnection()
	{
		RED_ASSERT( m_namedPipe != nullptr, "Named pipe does not exist" );
		return m_namedPipe->IsConnected() ? true : m_namedPipe->Connect();
	}

	Bool NamedPipeMessageHandler::VerifyServerConnection()
	{
		RED_ASSERT( m_namedPipe != nullptr, "Named pipe does not exist" );

		if ( m_namedPipe->IsError() || m_namedPipe->IsClosed() )
		{
			m_namedPipe->DisconnectAndReconnect();
		}

		HANDLE events[] = {0, 0};
		return m_namedPipe->IsConnected() || m_namedPipe->FinishConnecting( events );
	}
}
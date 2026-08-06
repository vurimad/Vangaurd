/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"

#ifdef RED_PLATFORM_WINPC

#include "namedPipe.h"

#include "../../redSystem/include/log.h"
#include "packetNetworkSendingDispositor.h"
#include "packetNetworkPacketHeader.h"

namespace red
{
	namespace Network
	{
		//////////////////////////////////////////////////////////////////////////
		// NamedPipe
		//////////////////////////////////////////////////////////////////////////
		NamedPipe::NamedPipe( PipeType type, const String& pipeName, Uint32 bufferSize )
			: m_type( type )
			, m_pipeName( pipeName )
			, m_hPipe( INVALID_HANDLE_VALUE )
			, m_state ( State::Connecting )
			, m_pipeOwner ( false )
			, m_isPending( false )
			, m_bufferSize( bufferSize )
		{
			red::Memset( &m_overlap, 0, sizeof( m_overlap ) );
			m_overlap.hEvent = INVALID_HANDLE_VALUE;
		}

		NamedPipe::~NamedPipe()
		{
			if ( m_hPipe != INVALID_HANDLE_VALUE )
			{
				if ( m_pipeOwner )
				{
					DisconnectNamedPipe( m_hPipe );
				}

				CloseHandle( m_hPipe );
			}

			if ( m_overlap.hEvent != INVALID_HANDLE_VALUE )
			{
				CloseHandle( m_overlap.hEvent );
			}
		}

		Uint32 NamedPipe::Send( const void* buffer, Uint32 size )
		{
			if ( IsConnected() )
			{
				if ( m_isPending )
				{
					DWORD outWrittenSize;
					BOOL ret = GetOverlappedResult( m_hPipe, &m_overlap, &outWrittenSize, FALSE );

					if ( !ret )
					{
						Uint32 errorCode = GetLastError();
						if ( errorCode != ERROR_IO_INCOMPLETE )
						{
							m_isPending = false;
							OnError();
						}
					}
					else
					{
						m_isPending = false;
						return ( Uint32 ) outWrittenSize;
					}
				}
				else
				{
					DWORD outWrittenSize;
					BOOL ret = WriteFile( m_hPipe, buffer, size, &outWrittenSize, &m_overlap );

					if ( ret && outWrittenSize == size )
					{
						return ( Uint32 ) outWrittenSize;;
					}

					if ( GetLastError() == ERROR_IO_PENDING )
					{
						m_isPending = true;
					}
					else
					{
						OnError();
					}
				}
			}

			return 0;
		}

		Bool NamedPipe::Flush()
		{
			RED_ASSERT( IsConnected() );

			return FlushFileBuffers( m_hPipe ) == 1;
		}

		Bool NamedPipe::Connect()
		{
			RED_ASSERT( IsConnecting() );

			UniChar pipeName[ MAX_PATH ];
			red::SNPrintFUnsafe( pipeName, MAX_PATH, TEXT("%hs%hs"), m_pipeName.AsChar(), ( m_type == PipeType::Read ) ? "w" : "r" );

			m_hPipe = CreateFile(
				pipeName,											// pipe name
				( m_type == PipeType::Read ) ? GENERIC_READ : GENERIC_WRITE,
				0,													// no sharing 
				NULL,												// default security attributes
				OPEN_EXISTING,										// opens existing pipe
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,		// default attributes
				NULL );												// no template file

			if ( m_hPipe == INVALID_HANDLE_VALUE )
			{
				return false;
			}

			m_pipeOwner = false;

			if ( m_type == PipeType::Read )
			{
				m_readBuffer = CreateUniqueBuffer< red::PoolEngine >( m_bufferSize, 8 );
			}

			red::Memset( &m_overlap, 0, sizeof( m_overlap ) );
			m_overlap.hEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
			RED_FATAL_ASSERT( m_overlap.hEvent );

			m_state = State::Connected;
			return true;
		}

		Bool NamedPipe::Open( )
		{
			RED_ASSERT( IsConnecting() );

			UniChar pipeName[ MAX_PATH ];
			red::SNPrintFUnsafe( pipeName, MAX_PATH, TEXT("%hs%hs"), m_pipeName.AsChar(), ( m_type == PipeType::Read ) ? "r" : "w" );

			m_hPipe = CreateNamedPipe(
				pipeName,																									// pipe name
				( ( m_type == PipeType::Read ) ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND ) | FILE_FLAG_OVERLAPPED,		// read/write access
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |																		// message-read mode
				PIPE_WAIT,																									// blocking mode
				1,																											// max. instances
				m_bufferSize,																								// output buffer size
				m_bufferSize,																								// input buffer size
				0,																											// client time-out
				NULL );																										// default security attribute

			if ( m_hPipe == INVALID_HANDLE_VALUE )
			{
				// handle error
				OnError();
				return false;
			}

			m_pipeOwner = true;

			if ( m_type == PipeType::Read )
			{
				m_readBuffer = CreateUniqueBuffer< red::PoolEngine >( m_bufferSize, 8 );
			}

			red::Memset( &m_overlap, 0, sizeof( m_overlap ) );
			m_overlap.hEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
			RED_FATAL_ASSERT( m_overlap.hEvent );

			return ConnectToNewClient();
		}

		Bool NamedPipe::ConnectToNewClient()
		{
			BOOL ret = ConnectNamedPipe( m_hPipe, &m_overlap );
			Uint32 errorCode = GetLastError();
			if ( ret )
			{
				// handle error
				OnError();
				return false;
			}

			switch ( errorCode )
			{
				// The overlapped connection in progress. 
				case ERROR_IO_PENDING:
					m_isPending = true;
					break;

				// Client is already connected, so signal an event.
				case ERROR_PIPE_CONNECTED:
					if ( SetEvent( m_overlap.hEvent ) )
						break;

				// If an error occurs during the connect operation. 
				default:
				{
					OnError();
					return false;
				}
			}

			return true;
		}

		void NamedPipe::DisconnectAndReconnect()
		{
			m_state = State::Connecting;
			DisconnectNamedPipe( m_hPipe );
			ConnectToNewClient();
		}

		void NamedPipe::Close()
		{
			Close( State::Closed );
		}

		void NamedPipe::Close( State state )
		{
			if ( m_isPending )
			{
				CancelIoEx ( m_hPipe, &m_overlap );
				RED_ASSERT( WaitForSingleObject( m_overlap.hEvent, 10000 ) == WAIT_OBJECT_0 );
				m_isPending = false;
			}

			m_state = state;
		}

		void NamedPipe::OnError( )
		{
			RED_LOG_ERROR( "Got an error while processing named pipe %s, error code = %ld", m_pipeName.AsChar(), GetLastError() );
			Close( State::Error );
		}
		
		Bool NamedPipe::Receive( Uint32& bytesRead )
		{
			RED_ASSERT( IsConnected() );

			if ( m_isPending )
			{
				// check if operation is finished

				DWORD outReadSize;
				BOOL ret = GetOverlappedResult( m_hPipe, &m_overlap, &outReadSize, FALSE );
				 
				if ( ret )
				{
					m_isPending = false;
					bytesRead = ( Uint32 ) outReadSize;
				}
				else if ( !ret)
				{
					Uint32 errorCode = GetLastError();
					if ( errorCode != ERROR_IO_INCOMPLETE )
					{
						m_isPending = false;
						OnError();
					}

					bytesRead = 0;
				}
			}
			else
			{
				DWORD outReadSize;
				BOOL ret = ReadFile( m_hPipe, m_readBuffer.Get(), m_readBuffer.GetSize(), &outReadSize, &m_overlap );

				// The read operation completed successfully
				if ( ret && outReadSize > 0 )
				{
					bytesRead = ( Uint32 ) outReadSize;
					m_isPending = false;
				}
				else if ( !ret && ( GetLastError() == ERROR_IO_PENDING ) )
				{
					bytesRead = ( Uint32 ) outReadSize;
					m_isPending = true;
				}
				else
				{
					m_isPending = false;
					bytesRead = 0;
					OnError();
				}
			}
			return m_isPending;
		}

		Bool NamedPipe::FinishConnecting()
		{
			RED_ASSERT ( IsConnecting() );

			DWORD outWrittenSize;
			BOOL ret = GetOverlappedResult( m_hPipe, &m_overlap, &outWrittenSize, FALSE );

			if ( ret )
			{
				m_isPending = false;
				m_state = State::Connected;
			}
			else if ( GetLastError() != ERROR_IO_INCOMPLETE )
			{
				m_isPending = false;
				OnError();
			}

			return IsConnected();
		}
	
		//----------------------------------------------------------------------------------------------

		FullDuplexPipe::FullDuplexPipe( const String& pipeName, Uint32 bufferSize )
			: m_pipeName( pipeName )
			, m_readPipe( NamedPipe::PipeType::Read, pipeName, bufferSize )
			, m_writePipe( NamedPipe::PipeType::Writre, pipeName, bufferSize )
		{
		}

		FullDuplexPipe::~FullDuplexPipe()
		{
		}

		Uint32 FullDuplexPipe::Send( const void* buffer, Uint32 size )
		{
			return m_writePipe.Send( buffer, size );
		}

		Uint32 FullDuplexPipe::Receive( red::DynArray< HANDLE >& events )
		{
			Uint32 bytesRead = 0;
			
			if ( m_readPipe.Receive( bytesRead ) )
				events.PushBack( m_readPipe.GetEvent() );

			return bytesRead;
		}

		Bool FullDuplexPipe::Flush()
		{
			Bool res = true;
			res &= m_readPipe.Flush();
			res &= m_writePipe.Flush();
			return res;
		}

		Bool FullDuplexPipe::Connect()
		{
			Bool res = true;

			if ( m_readPipe.IsConnecting() )
				res &= m_readPipe.Connect();
			else
				res &= m_readPipe.IsConnected();

			if ( m_writePipe.IsConnecting() )
				res &= m_writePipe.Connect();
			else
				res &= m_writePipe.IsConnected();

			return res;
		}

		Bool FullDuplexPipe::Open()
		{
			Bool res = true;
			res &= m_readPipe.Open();
			res &= m_writePipe.Open();
			return res;
		}

		void FullDuplexPipe::Close()
		{
			m_readPipe.Close();
			m_writePipe.Close();
		}

		Bool FullDuplexPipe::IsConnected() const
		{
			return m_readPipe.IsConnected() && m_writePipe.IsConnected();
		}

		Bool FullDuplexPipe::IsConnecting() const
		{
			if ( m_readPipe.IsConnecting() && m_writePipe.IsConnecting() )
				return true;

			if ( m_readPipe.IsConnected() && m_writePipe.IsConnecting() )
				return true;

			if ( m_readPipe.IsConnecting() && m_writePipe.IsConnected() )
				return true;

			return false;
		}

		Bool FullDuplexPipe::IsClosed() const
		{
			return m_readPipe.IsClosed() && m_writePipe.IsClosed();
		}

		Bool FullDuplexPipe::IsError() const
		{
			return m_readPipe.IsError() || m_writePipe.IsError();
		}

		HANDLE FullDuplexPipe::GetReadEvent() const
		{
			return m_readPipe.GetEvent();
		}

		Bool FullDuplexPipe::FinishConnecting( red::DynArray< HANDLE >& events )
		{
			Bool connected = true;

			// Finish connecting
			if ( m_readPipe.IsConnecting() )
				connected &= m_readPipe.FinishConnecting();
			else
				connected &= m_readPipe.IsConnected();

			if ( m_writePipe.IsConnecting() )
				connected &= m_writePipe.FinishConnecting();
			else
				connected &= m_writePipe.IsConnected();

			// Add events
			if ( m_readPipe.IsConnecting() )
				events.PushBack( m_readPipe.GetEvent() );

			if ( m_writePipe.IsConnecting() )
				events.PushBack( m_writePipe.GetEvent() );

			return connected;
		}

		Bool FullDuplexPipe::FinishConnecting( HANDLE (&events)[2] )
		{
			Bool connected = true;

			// Finish connecting
			if ( m_readPipe.IsConnecting() )
				connected &= m_readPipe.FinishConnecting();
			else
				connected &= m_readPipe.IsConnected();

			if ( m_writePipe.IsConnecting() )
				connected &= m_writePipe.FinishConnecting();
			else
				connected &= m_writePipe.IsConnected();

			// Add events
			red::Memzero( events, sizeof( HANDLE ) * 2 );
			int i = 0;
			if ( m_readPipe.IsConnecting() )
				events[i++] = m_readPipe.GetEvent();

			if ( m_writePipe.IsConnecting() )
				events[i] = m_writePipe.GetEvent();

			return connected;
		}

		void FullDuplexPipe::DisconnectAndReconnect()
		{
			// Reconnect can be triggered by both pipes
			m_readPipe.DisconnectAndReconnect();
			m_writePipe.DisconnectAndReconnect();
		}
	}

	//----------------------------------------------------------------------------------------------
}
#else
RED_NO_EMPTY_FILE();
#endif

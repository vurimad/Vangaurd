/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "rawTcpManager.h"

#include "../../../common/redSystem/include/stopWatch.h"

namespace red {
	namespace Network {

		RawTcpManager::RawTcpManager()
			: Thread( "RawTcpNetworkThread", { RED_KILO_BYTE( 128 ) } )
			, m_initializedFlag( false )
			, m_shutdownFlag( false )
			, m_idAllocator( 100 )
		{
		}

		RawTcpManager::~RawTcpManager()
		{
			RED_ASSERT( m_listeners.Empty() );
			RED_ASSERT( m_connections.Empty() );
		}

		void RawTcpManager::Initialize()
		{
			// initialize only once
			if ( m_initializedFlag.Exchange( true ) )
				return;

			// start thread
			InitThread();
#if defined( RED_PLATFORM_DURANGO ) || defined( RED_PLATFORM_ORBIS )
			SetAffinityMask( ( 1 << 6 ) );
#endif
		}

		void RawTcpManager::Shutdown()
		{
			// do not shutdown if not yet initialized
			if ( !m_initializedFlag.GetValue() )
				return;

			// send shutdown notification, only once
			if ( m_shutdownFlag.Exchange( true ) )
				return;

			// wait for the thread to finish
			JoinThread();

			// cleanup
			m_listeners.Clear();
			m_connections.Clear();
		}

		TConnectionID RawTcpManager::CreateConnection( const Utils::CommAddress& address, IRawConnectionInterface* callbackInterface )
		{
			Address tcpAddr( address.GetHost().AsChar(), address.GetPort() );

			// invalid interface
			if ( callbackInterface == nullptr )
			{
				RED_LOG_WARNING( "net: Callback interface is null" );
				return 0;
			}

			if ( !m_connections.Full() )
			{
				// connect to given address
				Socket socket;
				socket.Create();
				if ( !socket.Connect( tcpAddr ) )
				{
					RED_LOG_WARNING( "net: Can't connect - error: %d", socket.GetLastError() );
					return 0; // connection failed
				}

				// Wait for connection
				red::StopWatch timer;//tempshit - protection against infinite loop ?
				while ( !socket.FinishConnecting() )
				{
					if ( timer.GetDeltaMS() > 60000. )
						break;
				}

				// close socket if not connected
				if ( !socket.IsConnected() )
				{
					RED_LOG_WARNING( "net: Not connected - error: %d", socket.GetLastError() );
					return 0;
				}

				TConnectionID id = ( m_idAllocator.Increment() << FLAG_SHIFT ) | FLAG_CONNECTION;

				// create connection object, we should not run out of them
				m_connections.EmplaceBack( std::move( socket ), id, nullptr, callbackInterface );

				return id;
			}
			else
			{
				RED_LOG_WARNING("net: TCP connection list is full");
			}

			// return allocated ID
			return 0;
		}

		TListenerID RawTcpManager::CreateListener( const Utils::CommAddress& address, Bool reuseAddress, IRawListenerInterface* callbackInterface )
		{
			Uint16 localPort = address.GetPort();

			// invalid interface
			if ( callbackInterface == nullptr )
				return 0;

			RED_ASSERT( !m_listeners.Full() );

			if ( !m_listeners.Full() )
			{
				// create socket
				Socket socket;
				if ( !socket.Create( reuseAddress ) )
				{
					RED_LOG_WARNING( "net: Failed to create socket - error: %d", socket.GetLastError() );
					return 0; // create failed
				}

				// bind it to local port
				if ( !socket.Bind( localPort ) )
				{
					RED_LOG_WARNING( "net: Failed to bind socket to port - port: %d, errorCode: %d", localPort, socket.GetLastError() );
					return 0; // bind failed
				}

				// start listening on the socket
				if ( !socket.Listen() )
				{
					RED_LOG_WARNING( "net: Failed to start listening on socket - port: %d, errorCode: %d", localPort, socket.GetLastError() );
					return 0; // listen failed
				}
				
				// allocate listener ID
				const Uint32 id = ( m_idAllocator.Increment() << FLAG_SHIFT ) | FLAG_LISTENER;

				// create connection object, we may run out of them
				m_listeners.EmplaceBack( std::move( socket ), id, callbackInterface );

				return id;
			}
			else
			{
				RED_LOG_WARNING("net: TCP listeners list is full");
			}

			// return allocated ID
			return 0;
		}

		Bool RawTcpManager::CloseConnection( const TConnectionID connectionID )
		{
			// not a connection ID
			if ( !( connectionID & FLAG_CONNECTION ) )
				return false;

			// find and close matching connection

			auto it = std::find_if( m_connections.Begin(), m_connections.End(), [connectionID] ( const RawConnection& connection ) {
				return connectionID == connection.m_assignedID;
			} );

			if ( it != m_connections.End() )
			{
				// Shutdown socket without notification since we are closing on user's request
				it->m_socket.Shutdown( RED_NET_SHUTDOWN_SEND );
				it->m_interface = nullptr;
				return true;
			}

			return false;
		}

		Bool RawTcpManager::CloseListener( const TListenerID listenerID )
		{
			// not a listener ID
			if ( !( listenerID & FLAG_LISTENER ) )
				return false;

			auto it = std::find_if( m_listeners.Begin(), m_listeners.End(), [listenerID] ( const RawListener& listener ) {
				return listenerID == listener.m_assignedID;
			} );

			if ( it != m_listeners.End() )
			{
				// remove connections matching this listener
				for ( auto jt = m_connections.Begin(); jt != m_connections.End(); )
				{
					if ( jt->m_owner && jt->m_owner->m_assignedID == listenerID )
					{
						// Shutdown child connection
						jt->m_socket.Shutdown( RED_NET_SHUTDOWN_SEND );
						jt->m_interface = nullptr;
						jt = m_connections.Remove( jt ).Iterator();
					}
					else
					{
						++jt;
					}
				}

				// Shutdown listen socket without notification since we are closing on user's request
				it->m_socket.Shutdown( RED_NET_SHUTDOWN_BOTH );
				it->m_interface = nullptr;

				// remove from connection list
				return true;
			}

			// return true if removed
			return false;
		}

		Uint32 RawTcpManager::Send( const TConnectionID connectionID, const void* data, const Uint32 dataSize )
		{
			// not a connection ID
			if ( !( connectionID & FLAG_CONNECTION ) )
				return 0;

			auto it = std::find_if( m_connections.Begin(), m_connections.End(), [connectionID] ( const RawConnection& connection ) {
				return connectionID == connection.m_assignedID;
			} );

			if ( it != m_connections.End() )
			{
				Uint32 bytesSent = it->m_socket.Send( data, dataSize );

				// we may have been closed while sending
				if ( !it->m_socket.IsConnected() )
				{
					RED_LOG_SPAM( "net: RawConnection Connection Dropped, ConnectionID=%d", it->m_assignedID );

					// notify that the listener got closed
					if ( it->m_interface )
					{
						it->m_interface->OnDisconnected( it->m_assignedID );
						it->m_interface = nullptr;
					}

					// close socket
					it->m_socket.Close();
				}

				return bytesSent;
			}

			// not found or nothing sent
			return 0; 
		}

		SocketId RawTcpManager::BuildSocketSets( fd_set& readSet, fd_set& errorSet )
		{
			red::ScopedLock< red::Mutex > lock( m_lock );

			SocketId highestSocketId = 0;

			FD_ZERO( &readSet );
			FD_ZERO( &errorSet );

			for ( Uint32 i = 0; i < m_connections.Size(); )
			{
				if ( m_connections[ i ].m_socket.IsConnected() || m_connections[ i ].m_socket.IsShuttingDown() )
				{
					highestSocketId = Max( highestSocketId, m_connections[ i ].m_socket.GetRawDescriptor() );

					FD_SET( m_connections[ i ].m_socket.GetRawDescriptor(), &errorSet );
					FD_SET( m_connections[ i ].m_socket.GetRawDescriptor(), &readSet );

					++i;
				}
				else
				{
					// iteration is not affected
					m_connections.RemoveAtReorder( i );
				}
			}

			for ( Uint32 i = 0; i < m_listeners.Size(); )
			{
				if ( m_listeners[ i ].m_socket.IsListening() )
				{
					highestSocketId = Max( highestSocketId, m_listeners[ i ].m_socket.GetRawDescriptor() );

					FD_SET( m_listeners[ i ].m_socket.GetRawDescriptor(), &readSet );
					FD_SET( m_listeners[ i ].m_socket.GetRawDescriptor(), &errorSet );

					++i;
					continue;
				}

				if ( m_listeners[ i ].m_socket.IsShuttingDown() )
				{
					// Wait until all child connections are closed
					auto listener = std::ref( m_listeners[ i ] );
					auto it = std::find_if( m_connections.Begin(), m_connections.End(),
						[listener] ( const RawConnection& connection ) { return connection.m_owner == &listener.get(); } );

					// if there is no child connection we can close the listening socket
					if ( it != m_connections.End() )
					{
						++i;
						continue;
					}
				}
				// iteration is not affected
				m_listeners.RemoveAtReorder( i );
			}

			return highestSocketId;
		}

		Bool RawTcpManager::WaitForAction( SocketId highestSocketId, fd_set& readSet, fd_set& errorSet )
		{
			timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = MAX_WAIT_TIME_MS * 1000;

			// Wait for sockets
			return select( ( int ) highestSocketId + 1, &readSet, nullptr , &errorSet, &timeout ) > 0;
		}

		void RawTcpManager::ProcessListeners( fd_set& readSet, fd_set& errorSet )
		{
			for ( auto it = m_listeners.Begin(); it != m_listeners.End(); )
			{
				if ( it->m_socket.IsShuttingDown() )
				{
					++it;
					continue;
				}

				if ( FD_ISSET( it->m_socket.GetRawDescriptor(), &readSet ) )
				{
					RED_ASSERT( it->m_socket.IsListening() );

					// accept the connection
					Socket newSocket = it->m_socket.Accept();

					// allocate ID
					const TConnectionID connectionID = ( m_idAllocator.Increment() << FLAG_MASK ) | FLAG_CONNECTION;
					const TListenerID listenerID = it->m_assignedID;

					// ask the interface if it allows such connection
					IRawConnectionInterface* connectionInterface = nullptr;
					if ( !it->m_interface->OnConnection( listenerID, connectionID, connectionInterface ) || !connectionInterface )
					{
						RED_LOG_WARNING( "net: RawConnection Refused: listener refused connection" );
						newSocket.Close();
					}
					else
					{
						RED_FATAL_ASSERT( !m_connections.Full(), "net: RawConnection Refused: not enough connection objects" );
						m_connections.EmplaceBack( std::move( newSocket ), connectionID, &( *it ), connectionInterface );

						// connection accepted
						RED_LOG_SPAM( "net: RawConnection Established, ListenerID=%d, ConnectionID=%d", it->m_assignedID, connectionID );
					}

					// we may have been closed while in reading
					if ( !it->m_socket.IsListening() )
					{
						RED_LOG_SPAM( "net: Connection Dropped, ConnectionID=%d", it->m_assignedID );

						// notify that the listener got closed
						if ( it->m_interface )
						{
							it->m_interface->OnClosed( it->m_assignedID );
							it->m_interface = nullptr;
						}

						// close the socket
						it->m_socket.Close();

						it = m_listeners.Remove( it ).Iterator();
						continue;
					}
				}
				else if ( FD_ISSET( it->m_socket.GetRawDescriptor(), &errorSet ) )
				{
					RED_LOG_SPAM( "net: Connection error, ListenerID=%d, errorCode=%d", it->m_assignedID, it->m_socket.GetLastError() );

					// notify that the listener got closed
					if ( it->m_interface )
					{
						it->m_interface->OnClosed( it->m_assignedID );
						it->m_interface = nullptr;
					}

					// close the socket
					it->m_socket.Close();

					it = m_listeners.Remove( it ).Iterator();
					continue;
				}

				++it;
			}
		}

		void RawTcpManager::ProcessConnections(  fd_set& readSet, fd_set& errorSet )
		{
			for ( auto it = m_connections.Begin(); it != m_connections.End(); )
			{
				// skip broken connections
				if ( !it->m_socket.IsConnected() && !it->m_socket.IsShuttingDown() )
				{
					++it;
					continue;
				}

				// something new on this socket
				if ( FD_ISSET( it->m_socket.GetRawDescriptor(), &readSet ) )
				{
					RED_ASSERT( it->m_socket.IsConnected() || it->m_socket.IsShuttingDown() );

					// try to read data
					Uint8 buffer[ BUFFER_SIZE ];
					Uint32 size = 0;

					do
					{
						size = it->m_socket.Receive( buffer, sizeof(buffer) );

						// process data
						if ( ( size > 0 ) && ( it->m_interface ) )
						{
							it->m_interface->OnData( buffer, size, it->m_assignedID );
						}

					} while ( size > 0 );

					// we may have been closed while in reading
					if ( !it->m_socket.IsConnected() && !it->m_socket.IsShuttingDown() && it->m_interface )
					{
						RED_LOG_SPAM( "net: RawConnection Connection Dropped, ConnectionID=%d,", it->m_assignedID );

						// notify that the connection got closed

						it->m_interface->OnDisconnected( it->m_assignedID );
						it->m_interface = nullptr;

						// close
						it->m_socket.Close();

						it = m_connections.Remove( it ).Iterator();
						continue;
					}
				}
				else if ( FD_ISSET( it->m_socket.GetRawDescriptor(), &errorSet ) )
				{
					RED_LOG_SPAM( "net: Connection error, ConnectionID=%d, errorCode=%d", it->m_assignedID, it->m_socket.GetLastError() );

					// notify that the listener got closed
					if ( it->m_interface )
					{
						it->m_interface->OnDisconnected( it->m_assignedID );
						it->m_interface = nullptr;
					}

					// close
					it->m_socket.Close();

					it = m_connections.Remove( it ).Iterator();
					continue;
				}

				++it;
			}
		}

		void RawTcpManager::ThreadFunc()
		{
#ifndef RED_PLATFORM_CONSOLE
			red::memory::RegisterCurrentThread( GetThreadName() );
#endif

			while ( !m_shutdownFlag.GetValue() )
			{
				fd_set readSet;
				fd_set errorSet;

				// Refresh read set.
				SocketId highestSocketId = BuildSocketSets( readSet, errorSet );

				if ( WaitForAction( highestSocketId, readSet, errorSet ) )
				{
					red::ScopedLock< red::Mutex > lock( m_lock );

					// Process incoming connections
					ProcessListeners( readSet, errorSet );

					// Process incoming data
					ProcessConnections( readSet, errorSet );
				}
				else
				{
					red::SleepOnCurrentThread( 1 );
				}
			}
		}

		Bool RawTcpManager::IsConnectionActive( const TConnectionID connectionID )
		{
			// not a connection ID
			if ( !( connectionID & FLAG_CONNECTION ) )
				return false;

			auto it = std::find_if( m_connections.Begin(), m_connections.End(), [connectionID] ( const RawConnection& connection ) {
				return connectionID == connection.m_assignedID;
			} );

			if ( it != m_connections.End() )
			{
				return it->m_socket.IsConnected();
			}

			return false;
		}
	}
} // red::Network


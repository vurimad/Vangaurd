/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rawPipeManager.h"

#ifdef RED_PLATFORM_WINPC

namespace red {
	namespace Network {

	RawPipeManager::RawPipeManager()
		: Thread( "RawPipeNetworkThread" )
		, m_initializedFlag( false )
		, m_shutdownFlag( false )
		, m_idAllocator( 100 )
	{
	}

	RawPipeManager::~RawPipeManager()
	{
		RED_ASSERT ( m_listeners.Empty() );
		RED_ASSERT ( m_connections.Empty() );
	}

	void RawPipeManager::Initialize()
	{
		// initialize only once
		if ( m_initializedFlag.Exchange( true ) )
			return;

		// start thread
		InitThread();
	}

	void RawPipeManager::Shutdown()
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

	red::Network::TConnectionID RawPipeManager::CreateConnection( const Utils::CommAddress& address, IRawConnectionInterface* callbackInterface )
	{
		// invalid interface
		if ( callbackInterface == nullptr )
			return 0;

		RED_ASSERT( !m_connections.Full() );
		if ( !m_connections.Full() )
		{
			// connect to given pipe
			red::SharedPtr < FullDuplexPipe > pipe = red::CreateSharedPtr< FullDuplexPipe >( address.GetHost(), red::Network::MAX_PACKET );

			if ( !pipe->Connect() )
				return 0;

			// allocate connection ID
			const TConnectionID id = ( m_idAllocator.Increment() << FLAG_SHIFT ) | FLAG_CONNECTION;

			// create connection object, we should not run out of them
			m_connections.EmplaceBack( pipe, id, nullptr, callbackInterface );

			return id;
		}

		return 0;
	}

	red::Network::TListenerID RawPipeManager::CreateListener( const Utils::CommAddress& address, Bool, IRawListenerInterface* callbackInterface )
	{
		// invalid interface
		if ( callbackInterface == nullptr )
			return 0;

		RED_ASSERT( !m_listeners.Full() );
		if ( !m_listeners.Full() )
		{
			red::SharedPtr < FullDuplexPipe > pipe = red::CreateSharedPtr< FullDuplexPipe >( address.GetHost(), red::Network::MAX_PACKET );
			if ( !pipe->Open() )
				return 0;

			// allocate listener ID
			const Uint32 id = ( m_idAllocator.Increment() << FLAG_SHIFT ) | FLAG_LISTENER;

			// create connection object, we may run out of them
			m_listeners.EmplaceBack( pipe, id, callbackInterface );

			return id;
		}

		return 0;
	}

	Bool RawPipeManager::CloseConnection( const TConnectionID connectionID )
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
			it->m_pipe->Close();
			it->m_interface = nullptr;
			return true;
		}

		return false;
	}

	Bool RawPipeManager::CloseListener( const TListenerID listenerID )
	{
		// not a listener ID
		if ( !( listenerID & FLAG_LISTENER ) )
			return false;

		auto it = std::find_if( m_listeners.Begin(), m_listeners.End(), [listenerID] ( const RawListener& listener ) {
			return listenerID == listener.m_assignedID;
		} );

		if ( it != m_listeners.End() )
		{
			it->m_pipe->Close();
			it->m_interface = nullptr;
			return true;
		}

		return false;
	}

	Uint32 RawPipeManager::Send( const TConnectionID connectionID, const void* data, const Uint32 dataSize )
	{
		// not a connection ID
		if ( !( connectionID & FLAG_CONNECTION ) )
			return false;

		auto it = std::find_if( m_connections.Begin(), m_connections.End(), [connectionID] ( const RawConnection& connection ) {
			return connectionID == connection.m_assignedID;
		} );

		if ( it != m_connections.End() )
		{
			return it->m_pipe->Send( data, dataSize );
		}

		// not found or nothing sent
		return 0;
	}

	Bool RawPipeManager::IsConnectionActive( const TConnectionID connectionID )
	{
		// not a connection ID
		if ( !( connectionID & FLAG_CONNECTION ) )
			return false;

		auto it = std::find_if( m_connections.Begin(), m_connections.End(), [connectionID] ( const RawConnection& connection ) {
			return connectionID == connection.m_assignedID;
		} );

		if ( it != m_connections.End() )
		{
			return it->m_pipe->IsConnected();
		}

		return false;
	}

	void RawPipeManager::ThreadFunc()
	{
		red::DynArray< HANDLE > events{ red::PoolEngine() };
		while ( !m_shutdownFlag.GetValue() )
		{
			ProcessPipes( events );
			WaitForAction( events );
		}
	}

	void RawPipeManager::ProcessPipes( red::DynArray< HANDLE >& events )
	{
		red::ScopedLock< red::Mutex > lock( m_lock );

		ProcessListeners( events );

		Bool listenerConnectionRemoved = ProcessConnections( events );

		// Check if there is a need to remove dead listeners
		if ( listenerConnectionRemoved )
		{
			ProcessListeners( events );
		}
	}

	void RawPipeManager::ProcessListeners( red::DynArray< HANDLE >& events )
	{
		for ( Uint32 i = 0; i < m_listeners.Size(); )
		{
			if ( m_listeners[ i ].m_pipe->IsClosed() || m_listeners[ i ].m_pipe->IsError() )
			{
				// If there is no active connection we can try to remove listener
				if ( m_listeners[ i ].m_pipe.GetRefCount() == 1 )
				{
					if ( m_listeners[ i ].m_pipe->IsError() )
					{
						RED_FATAL_ASSERT( m_listeners[ i ].m_interface, "Lost connection interface, ID=%d", m_listeners[ i ].m_assignedID );
						m_listeners[ i ].m_interface->OnClosed( m_listeners[ i ].m_assignedID );
					}

					// iteration is not affected
					m_listeners.RemoveAtReorder( i );
					continue;
				}

				++i;
			}
			else
			{
				if ( m_listeners[ i ].m_pipe->IsConnecting() )
				{
					if ( m_listeners[ i ].m_pipe->FinishConnecting( events ) )
					{
						// Try to add new connection
						const TConnectionID connectionID = ( m_idAllocator.Increment() << FLAG_MASK ) | FLAG_CONNECTION;
						IRawConnectionInterface* connInterface = nullptr;
						if ( !m_listeners[ i ].m_interface->OnConnection( m_listeners[ i ].m_assignedID, connectionID, connInterface ) )
						{
							RED_LOG_WARNING( "net: RawConnection Refused: listener refused connection" );
						}
						else
						{
							RED_FATAL_ASSERT( !m_connections.Full(), "RawConnection Refused: not enough connection objects" );
							m_connections.EmplaceBack( m_listeners[ i ].m_pipe, connectionID, &m_listeners[ i ], connInterface );
						}
					}
				}

				// Need to check once again if there was an error during connecting.
				if ( m_listeners[ i ].m_pipe->IsError() )
					continue;

				++i;
			}
		}
	}

	Bool RawPipeManager::ProcessConnections( red::DynArray< HANDLE >& events )
	{
		Bool listenerConnectionRemoved = false;
		for ( Uint32 i = 0; i < m_connections.Size(); )
		{
			if ( m_connections[ i ].m_pipe->IsClosed() || m_connections[ i ].m_pipe->IsError() )
			{
				// Report error
				if ( m_connections[ i ].m_pipe->IsError() )
				{
					RED_LOG_SPAM( "net: Connection Dropped, ConnectionID=%d,", m_connections[ i ].m_assignedID );

					RED_FATAL_ASSERT( m_connections[ i ].m_interface, "Lost connection interface, ID=%d", m_connections[ i ].m_assignedID );
					m_connections[ i ].m_interface->OnDisconnected( m_connections[ i ].m_assignedID );
				}

				// Reconnect listen pipe if not closed on purpose
				if ( m_connections[ i ].m_owner && !m_connections[ i ].m_owner->m_pipe->IsClosed() )
				{
					m_connections[ i ].m_owner->m_pipe->DisconnectAndReconnect();
				}

				if ( m_connections[ i ].m_owner )
				{
					listenerConnectionRemoved = true;
				}

				// Iteration is not affected
				m_connections.RemoveAtReorder( i );
			}
			else
			{
				// Read as long as size is not 0
				Uint32 size = 0;
				do
				{
					size = m_connections[ i ].m_pipe->Receive( events );

					if ( size > 0 )
					{
						m_connections[ i ].m_interface->OnData( m_connections[ i ].m_pipe->GetReadBuffer(), size, m_connections[ i ].m_assignedID );
					}

				} while ( size > 0 );

				// Need to check once again if there was an error during receiving.
				if ( m_connections[ i ].m_pipe->IsError() )
					continue;

				++i;
			}
		}

		return listenerConnectionRemoved;
	}

	void RawPipeManager::WaitForAction( red::DynArray< HANDLE >& events )
	{
		if ( !events.Empty() )
		{
			// Wait for pending event
			WaitForMultipleObjects( events.Size(), &events[ 0 ], FALSE, MAX_WAIT_TIME );
			events.Clear();
		}
		else
		{
			// Nothing to do for now
			red::SleepOnCurrentThread( MAX_WAIT_TIME );
		}

	}

} } // red::Network
#else
RED_NO_EMPTY_FILE();
#endif
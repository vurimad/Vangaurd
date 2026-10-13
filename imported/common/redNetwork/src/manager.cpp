/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"

#include "manager.h"

#include "../../redSystem/include/assert.h"
#include "../../redSystem/include/formatMacros.h"
#include "../../redSystem/include/redThreadsThread.h"

namespace red { namespace Network {

//////////////////////////////////////////////////////////////////////////
// Connection Event Listener
//////////////////////////////////////////////////////////////////////////

ConnectionEventListener::~ConnectionEventListener()
{

}

//////////////////////////////////////////////////////////////////////////
// Manager
//////////////////////////////////////////////////////////////////////////

Manager* Manager::m_instance = nullptr;

const AnsiChar Manager::BIND_TO_CHANNEL_PACKET_IDENTIFIER[] = "BIND";

#define WAKE_THREAD_PORT_RANGE_START	36050
#define WAKE_THREAD_PORT_RANGE_END		36060

Manager::Manager()
:	red::Thread( "Network", { RED_KILO_BYTE( 128 ) } )
,	m_initialized( false )
,	m_shutdown( false )
,	m_listenerPort( 0 )
{
	RED_ASSERT( m_instance == nullptr, "An instance of the network system manager already exists!" );
	m_instance = this;
}

Manager::~Manager()
{
	m_instance = nullptr;
}

void Manager::ThreadFunc()
{
	RED_LOG( "net: Thread Started" );

#ifdef RED_PLATFORM_CONSOLE
	SetAffinityMask( RED_FLAG( 6 ) );
#endif

	while( !m_shutdown.GetValue() && !m_initialized.GetValue() )
	{
		if ( !InitializeInternal() )
		{
			SleepOnCurrentThread( 1 );
		}
	}

	if ( m_initialized.GetValue() )
	{
		while( !m_shutdown.GetValue() )
		{
			Update();
			red::SleepOnCurrentThread( 1 );
		}
	}

	Shutdown();
}

void Manager::Initialize()
{
	InitThread();
}

Bool Manager::InitializeInternal()
{
	// Threadwaker "dummy" UDP socket, that will send dummy data to wake the
	// network thread when there's something to be sent
	m_threadWaker.Create( false, Socket::UDP );

	Uint16 port = WAKE_THREAD_PORT_RANGE_START;
	m_threadWakerBoundAddress = Address( "127.0.0.1", port );
	
	// Search for available port
	while ( m_threadWaker.IsUnbound() && port <= WAKE_THREAD_PORT_RANGE_END && !m_threadWaker.Bind( m_threadWakerBoundAddress ) )
	{
		m_threadWakerBoundAddress.SetPort( ++port );
	}

	if ( m_threadWaker.IsBound() )
	{
		RED_ASSERT( port >= WAKE_THREAD_PORT_RANGE_START && port <= WAKE_THREAD_PORT_RANGE_END, "Unable to find a free port inside the specified range %i -> %i", WAKE_THREAD_PORT_RANGE_START, WAKE_THREAD_PORT_RANGE_END );
		RED_LOG_INFO( "net: Initialised" );
		m_initialized.SetValue( true );
		return true;
	}

	RED_LOG_ERROR( "Manager::InitializeInternal: Unable to find a free port inside the specified range %i -> %i", WAKE_THREAD_PORT_RANGE_START, WAKE_THREAD_PORT_RANGE_END );

	m_threadWaker = Socket();
	return false;
}

void Manager::Update()
{
	CheckForNewConnections();

	UpdatePendingConnections();

	UpdateIncoming();

	UpdateOutgoing();

	WaitUntilNeeded();
}

void Manager::WaitUntilNeeded()
{
	{
		fd_set writeset;
		fd_set readset;
		fd_set errorset;

		FD_ZERO( &writeset );
		FD_ZERO( &readset );
		FD_ZERO( &errorset );

		SocketId highestSocketId = m_threadWaker.GetRawDescriptor();

		if( m_listener.IsListening() )
		{
			FD_SET( m_listener.GetRawDescriptor(), &readset );

			SocketId listenerId = m_listener.GetRawDescriptor();

			if( highestSocketId < listenerId )
			{
				highestSocketId = listenerId;
			}
		}

		FD_SET( m_threadWaker.GetRawDescriptor(), &readset );

		for( auto connectionIter = m_connections.Begin(); connectionIter != m_connections.End(); ++connectionIter )
		{
			if( connectionIter->m_socket.IsConnecting() )
			{
				FD_SET( connectionIter->m_socket.GetRawDescriptor(), &writeset );
				FD_SET( connectionIter->m_socket.GetRawDescriptor(), &errorset );

				if( highestSocketId < connectionIter->m_socket.GetRawDescriptor() )
				{
					highestSocketId = connectionIter->m_socket.GetRawDescriptor();
				}
			}
			else if( connectionIter->m_socket.IsConnected() )
			{
				FD_SET( connectionIter->m_socket.GetRawDescriptor(), &readset );

				if( highestSocketId < connectionIter->m_socket.GetRawDescriptor() )
				{
					highestSocketId = connectionIter->m_socket.GetRawDescriptor();
				}
			}
		}

		// Has to be the value of the highest socket descriptor + 1
		++highestSocketId;

		// Wait for sockets
		select( (int)highestSocketId, &readset, &writeset, &errorset, nullptr );

		// Purge threadwaker buffer
		Address localhost;
		Uint8 buffer[ 32 ];
		while( m_threadWaker.ReceiveFrom( buffer, sizeof( buffer ), localhost ) )
		{
			// preventing spin-lock on thread
			red::SleepOnCurrentThread( 0 );
		};
	}
}

void Manager::Wake()
{
	Uint32 buffer = 0xCA11;

	m_threadWaker.SendTo( &buffer, sizeof( Uint32 ), m_threadWakerBoundAddress );
}

void Manager::Shutdown()
{
	m_lock.Acquire();

	for( auto connectionIter = m_connections.Begin(); connectionIter != m_connections.End(); ++connectionIter )
	{
		for( auto listener = connectionIter->m_listeners.Begin(); listener != connectionIter->m_listeners.End(); ++listener )
		{
			( *listener )->OnConnectionClosed( connectionIter->m_socket.GetPeer() );
		}

		connectionIter->m_socket.Close();
	}

	m_lock.Release();

	if( m_listener.IsOpen() )
	{
		m_listener.Close();
	}

	if( m_threadWaker.IsOpen() )
	{
		m_threadWaker.Close();
	}

	m_initialized.SetValue( false );
	m_shutdown.SetValue( false );

	RED_LOG_INFO( "net: Shut Down" );
}

Bool Manager::ListenForIncomingConnections( Uint16 startPort, Uint16 numPortsToTry )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	if( !m_initialized.GetValue() )
	{
		RED_LOG_ERROR( "net: Cannot listen for incoming connections - system not yet initialised" );
		return false;
	}

	if( !m_listener.Create() )
	{
		RED_LOG_ERROR( "net: Failed to create socket (Error code: %i)", Base::GetLastError() );
		return false;
	}

	Bool bindSuccessfull = false;
	Uint16 port;
	for ( port = startPort; port < startPort + numPortsToTry; ++port )
	{
		if ( m_listener.Bind( port ) )
		{
			bindSuccessfull = true;
			break;
		}
	}

	if ( !bindSuccessfull )
	{
		RED_LOG_ERROR( "net: Failed to bind on port range %hu-%hu (Error code: %i)", startPort, startPort + numPortsToTry - 1, Base::GetLastError() );

		m_listener.Close();
		return false;
	}
	
	if( !m_listener.Listen() )
	{
		RED_LOG_ERROR( "net: Failed to listen (Error code: %i)", Base::GetLastError() );

		m_listener.Close();
		return false;
	}
	m_listenerPort = port;

	// Create the socket that will listen for new incoming connections
	RED_LOG_SPAM( "net: Listening for incoming connections on port %hu", port );

	Wake();

	return true;
}

void Manager::CheckForNewConnections()
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	if( m_listener.IsListening() )
	{
		// Check for new incoming connections
		Socket socket = m_listener.Accept();
		if( socket.IsConnected() )
		{
			if( !m_connections.Full() )
			{
				m_connections.EmplaceBack( std::move( socket ) );
				RED_LOG_SPAM( "net: Connection Established" );
			}
			else
			{
				// We can't deal with any new connections at this time
				RED_LOG_WARNING( "net: Connection Refused" );
				socket.Close();
			}
		}
	}
}

void Manager::UpdatePendingConnections()
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	for ( Uint32 i = 0; i < m_pendingConnections.Size(); )
	{
		const auto& pendingConnection = m_pendingConnections[ i ];

		Channel* channel = FindChannelLock( pendingConnection.m_channelName );

		RED_ASSERT( channel );

		if ( pendingConnection.m_socket->FinishConnecting() )
		{
			if ( pendingConnection.m_socket->IsConnected() )
			{
				BindRemoteChannelToSocket( channel, pendingConnection.m_socket );

				//Notify the connection listeners

				auto connectionIter = std::find_if( m_connections.Begin(), m_connections.End(), [&pendingConnection] ( const Connection& connection ) {
					return connection.m_socket == *pendingConnection.m_socket;
				} );

				if ( connectionIter != m_connections.End() )
				{
					for ( const auto listener : connectionIter->m_listeners )
					{
						listener->OnConnectionSucceeded( pendingConnection.m_socket->GetPeer() );
					}
				}
			}

			// We don't need to worry about the active connection entry (m_connections) as that will be taken care of automatically

			// iteration is not affected
			m_pendingConnections.RemoveAtReorder( i );
		}
		else
		{
			++i;
		}
	}
}

void Manager::UpdateIncoming()
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	for( auto connectionIter = m_connections.Begin(); connectionIter != m_connections.End(); )
	{
		Socket& socket = connectionIter->m_socket;
		IncomingPacket& incoming = connectionIter->m_incoming;

		if( socket.IsConnected() && incoming.Receive( &socket ) )
		{
			ProcessIncomingPacket( incoming, socket );
		}
		else
		{
			if( !socket.IsConnected() && !socket.IsConnecting() )
			{
				if( socket.IsClosed() )
				{
					for( auto listener = connectionIter->m_listeners.Begin(); listener != connectionIter->m_listeners.End(); ++listener )
					{
						( *listener )->OnConnectionClosed( socket.GetPeer() );
					}
				}
				else
				{
					for( auto listener = connectionIter->m_listeners.Begin(); listener != connectionIter->m_listeners.End(); ++listener )
					{
						( *listener )->OnConnectionDropped( socket.GetPeer() );
					}
				}

				RemoveConnection( &socket );
				connectionIter->m_listeners.Clear();

				// iteration is not affected
				m_connections.RemoveReorder( connectionIter );

				RED_LOG( "net: Remote connection closed" );

				continue;
			}
		}

		++connectionIter;
	}
}

void Manager::ProcessIncomingPacket( IncomingPacket& packet, Socket& socket )
{
	AnsiChar name[ Channel::NAME_MAX_LENGTH ];

	// First read in the packet type
	if( packet.ReadString( name, Channel::NAME_MAX_LENGTH ) )
	{
		// Figure out type and act accordingly
		if( red::Strcmp( name, BIND_TO_CHANNEL_PACKET_IDENTIFIER ) == 0 )
		{
			// It's an instruction to bind this connection to a specific channel
			packet.ReadString( name, Channel::NAME_MAX_LENGTH );

			Channel* channel = FindChannelLock( name );

			if( channel )
			{
				channel->RegisterDestination( &socket );
			}
		}
		else
		{
			// It's must be the name of a channel
			ToChannel( name, packet );
		}
	}

	packet.Reset();
}


void Manager::RemoveConnection( Socket* socket )
{
	// Remove connection from channel
	{
		red::ScopedLock< red::Mutex > lock( m_channelLock );

		for( auto& channel : m_channels )
		{
			channel.UnregisterDestination( socket );
		}
	}

	// Remove connection as destination from outgoing packets
	for ( Uint32 i = 0; i < m_outgoingPackets.Size(); ++i )
	{
		for ( Uint32 j = 0; j < m_outgoingPackets[ i ].m_destinations.Size(); )
		{
			if ( m_outgoingPackets[ i ].m_destinations[ j ].m_socket == socket )
			{
				// iteration is not affected
				m_outgoingPackets[ i ].m_destinations.RemoveAtReorder( j );
			}
			else
			{
				++j;
			}
		}
	}
}

RED_INLINE Channel* Manager::FindChannelNoLock( const AnsiChar* channelName )
{
	for( auto& channel : m_channels )
	{
		if( red::Strcmp( channelName, channel.GetName(), Channel::NAME_MAX_LENGTH ) == 0 )
		{
			// Dereference the iterator, then get the address of the channel variable
			return &channel;
		}
	}

	return nullptr;
}

RED_INLINE Channel* Manager::FindChannelLock( const AnsiChar* channelName )
{
	red::ScopedLock< red::Mutex > lock( m_channelLock );

	return FindChannelNoLock( channelName );
}

Channel* Manager::CreateChannelInternal( const AnsiChar* channelName )
{
	red::ScopedLock< red::Mutex > lock( m_channelLock );

	Channel* channel = FindChannelNoLock( channelName );

	if( !channel )
	{
		m_channels.EmplaceBack();
		m_channels.Back().SetName( channelName );
		channel = &m_channels.Back();
	}

	return channel;
}

RED_INLINE Manager::Connection* Manager::FindConnection( const Address& target )
{
	//Find out if any of our existing connections matches the target address
	for( auto connectionIter = m_connections.Begin(); connectionIter != m_connections.End(); ++connectionIter )
	{
		if( connectionIter->m_socket.GetPeer() == target )
		{
			// Get the contents of the iterator, and then return the address
			return &( *connectionIter );
		}
	}

	return nullptr;
}

Manager::Connection* Manager::CreateConnection( const Address& target )
{
	Connection* connection = FindConnection( target );

	if( !connection )
	{
		if( !m_connections.Full() )
		{
			Socket newSocket;

			if( newSocket.Create() )
			{
				if( newSocket.Connect( target ) )
				{
					connection = &m_connections.EmplaceBack( std::move( newSocket ) );
				}
			}
		}
	}

	return connection;
}

void Manager::BindRemoteChannelToSocket( Channel* channel, Socket* socket )
{
	// Now that the socket is fully connected, associate it with the channel
	channel->RegisterDestination( socket );

	// Instruct the other side to associate this connection with the same channel
	OutgoingPacket packet;

	// Fill the packet
	packet.WriteString( BIND_TO_CHANNEL_PACKET_IDENTIFIER );
	packet.WriteString( channel->GetName() );

	Send( socket, packet );
}

void Manager::ToChannel( const AnsiChar* channelName, IncomingPacket& packet )
{
	Channel* channel = FindChannelLock( channelName );
	
	if( channel )
	{
		channel->ReceivePacket( packet );
	}
}

void Manager::UpdateOutgoing()
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	//Keep sending packet data until we either run out of packets or fail to send the packet to all destinations in one go
	while( !m_outgoingPackets.Empty() )
	{
		auto& outPacket = m_outgoingPackets.Front();

		Bool finished = true;
		
		for( auto& destination : outPacket.m_destinations )
		{
			if( destination.m_state != OutgoingPacketDestination::State_Sent )
			{
				// If the destination hasn't finished connecting, then we're not finished sending the packet
				// If the connection has dropped, then this destination no longer affects the success of this packet
				if( destination.m_socket->IsConnecting() || ( destination.m_socket->IsConnected() && !outPacket.m_packet.Send( destination ) ) )
				{
					finished = false;
				}
			}
		}

		if( finished )
		{
			m_outgoingPackets.Pop();
		}
		else
		{
			// The packet hasn't finished sending, wait for the next update
			Wake();
			break;
		}
	}
}

Bool Manager::Send( const AnsiChar* channelName, const OutgoingPacket& packet )
{
	Channel* channel = FindChannelLock( channelName );

	if( channel )
	{
		red::ScopedLock< red::Mutex > lock( m_lock );

		// Create pending packet
		PendingPacket queuedPacket;
		queuedPacket.m_packet = packet;

		// Copy destinations from channel to pending packet
		queuedPacket.m_destinations.Clear();

		for( auto& channelDest : channel->GetDestinations() )
		{
			queuedPacket.m_destinations.EmplaceBack();
			queuedPacket.m_destinations.Back().Initialise( channelDest );
		}

		m_outgoingPackets.Push( std::move( queuedPacket ) );

		//Ensure that the network thread is alive
		Wake();

		return true;
	}
	else
	{
		RED_LOG_ERROR( "net: Could not send packet on unknown channel %s", channelName );
	}

	return false;
}

Bool Manager::Send( Socket* destination, const OutgoingPacket& packet )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// Create pending packet
	PendingPacket queuedPacket;
	queuedPacket.m_packet = packet;

	// Copy destinations from channel to pending packet
	queuedPacket.m_destinations.Clear();
	queuedPacket.m_destinations.EmplaceBack();
	queuedPacket.m_destinations.Back().Initialise( destination );

	m_outgoingPackets.Push( std::move( queuedPacket ) );

	//Ensure that the network thread is alive
	Wake();

	return true;
}

Bool Manager::RegisterListener( const AnsiChar* channelName, ChannelListener* listener )
{
	Channel* channel = CreateChannelInternal( channelName );

	if( channel )
	{
		channel->RegisterListener( listener );

		return true;
	}

	return false;
}

Bool Manager::UnregisterListener( const AnsiChar* channelName, ChannelListener* listener )
{
	Channel* channel = FindChannelLock( channelName );

	if( channel )
	{
		channel->UnregisterListener( listener );

		return true;
	}

	return false;
}

void Manager::UnregisterListener( ConnectionEventListener* listener )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	for( auto connectionIter = m_connections.Begin(); connectionIter != m_connections.End(); ++connectionIter )
	{
		for( auto iListener = connectionIter->m_listeners.Begin(); iListener != connectionIter->m_listeners.End(); ++iListener )
		{
			if( *iListener == listener )
			{
				connectionIter->m_listeners.Remove( iListener );
				return;
			}
		}
	}
}

Bool Manager::ConnectTo( const Address& target, const AnsiChar* channelName, ConnectionEventListener* listener )
{
	Bool retval = false;

	Channel* channel = CreateChannelInternal( channelName );

	if( channel )
	{
		red::ScopedLock< red::Mutex > lock( m_lock );

		Connection* connection = CreateConnection( target );

		if( connection )
		{
			if( connection->m_socket.IsConnecting() )
			{
				// Register connection as pending, we'll check for success or failure later
				{
					auto& pendingConn = m_pendingConnections.EmplaceBack();
					pendingConn.m_socket = &connection->m_socket;
					red::Strcpy( pendingConn.m_channelName, channelName, Channel::NAME_MAX_LENGTH );
				}

				// If a listener was supplied, store it 
				if( listener)
				{
					RED_ASSERT( !connection->m_listeners.Full() );
					connection->m_listeners.EmplaceBack( listener );
				}

				Wake();

				retval = true;
			}
			else if( connection->m_socket.IsConnected() )
			{
				if( listener )
				{
					// Connection was already established
					listener->OnConnectionAvailable( connection->m_socket.GetPeer() );

					RED_ASSERT( !connection->m_listeners.Full() );
					connection->m_listeners.EmplaceBack( listener );
				}

				BindRemoteChannelToSocket( channel, &connection->m_socket );

				retval = true;
			}
		}
	}

	return retval;
}

Bool Manager::DisconnectFrom( const Address& target )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	Connection* connection = FindConnection( target );

	if( connection )
	{
		// simply close the socket, connection cleanup will occur during update step
		connection->m_socket.Close();

		return true;
	}

	return false;
}

Uint32 Manager::GetNumberOfConnectionsToChannel( const AnsiChar* channelName )
{
	Channel* channel = FindChannelLock( channelName );
	return channel ? channel->GetDestinations().Size() : 0;
}

} } // namespace red { namespace Network {

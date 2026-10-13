/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "channelConnectionImpl.h"

namespace comm
{
	void ChannelConnection::OnListenerClosed( red::Network::PacketListenerID listenerId )
	{
		RED_FATAL_ASSERT( listenerId == m_listenerId );

		m_listenerId = 0;

		RED_LOG( "Channel: Listener closed %u", listenerId );
	}

	void ChannelConnection::OnConnectionClosed( red::Network::PacketConnectionID connectionId )
	{
		if ( connectionId == m_connectionId )
		{
			m_connectionId = 0;

			RED_LOG( "Channel: connection closed %u", connectionId );
		}
	}

	ChannelConnection::ChannelConnection( red::Network::IPacketNetworkManager& network )
		: m_network( network )
		, m_connectionId( 0 )
		, m_listenerId( 0 )
	{
	}

	ChannelConnection::~ChannelConnection()
	{
		if ( IsListening() )
			m_network.CloseListener( m_listenerId );

		if ( HasConnection() )
			m_network.CloseConnection( m_connectionId );
	}

	Bool ChannelConnection::OnPacket( ChannelData packet, red::Network::PacketConnectionID remoteConnectionID )
	{
		ResponseDest response{ m_network, remoteConnectionID };

		if ( m_callback )
			return m_callback( std::move( packet ), response );

		return false;
	}

	void ChannelConnection::OnConnection( red::Network::PacketConnectionID connectionId, red::Network::ConnectionDirection direction )
	{
		if ( !HasConnection() && direction == red::Network::ConnectionDirection::Incoming )
		{
			m_connectionId = connectionId;
			RED_LOG( "Channel: Incoming connection established: %u", connectionId );
		}
		else
		{
			RED_LOG( "Channel: %hs connection ignored: %u (Existing connection: %u)", ( direction == red::Network::ConnectionDirection::Incoming ) ? "incoming" : "outgoing", connectionId, m_connectionId );
		}
	}

	void ChannelConnection::Send( ChannelData packet )
	{
		if ( HasConnection() )
		{
			m_network.Send( m_connectionId, std::move( packet ) );
		}
		else
		{
			RED_LOG_ERROR( "Channel: Failed to send packet through channel, connection is not established." );
		}
	}

	Bool ChannelConnection::StartListening( const red::Network::Utils::CommAddress& address, Bool reuseAddress )
	{
		RED_FATAL_ASSERT( !IsListening(), "Connection already established" );

		m_listenerId = m_network.CreateListener( address, reuseAddress, this );

		return IsListening();
	}

	Bool ChannelConnection::OpenConnection( const red::Network::Utils::CommAddress& address )
	{
		RED_FATAL_ASSERT( !HasConnection(), "Connection already established" );

		m_connectionId = m_network.OpenConnection( address, this );

		return HasConnection();
	}
}

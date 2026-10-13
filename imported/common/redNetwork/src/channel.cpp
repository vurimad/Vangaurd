/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "channel.h"
#include "socket.h"

//////////////////////////////////////////////////////////////////////////
// Packet
//////////////////////////////////////////////////////////////////////////
red::Network::ChannelPacket::ChannelPacket( const AnsiChar* channelName )
:	OutgoingPacket()
{
	WriteString( channelName );
}

red::Network::ChannelPacket::~ChannelPacket()
{

}

void red::Network::ChannelPacket::Clear( const AnsiChar* channelName )
{
	RED_ASSERT( channelName, "NULL channel name!" );

	OutgoingPacket::Clear();
	WriteString( channelName );
}

//////////////////////////////////////////////////////////////////////////
// Channel
//////////////////////////////////////////////////////////////////////////
red::Network::Channel::Channel()
{
}

red::Network::Channel::~Channel()
{
}

void red::Network::Channel::ReceivePacket( IncomingPacket& packet )
{
	// For a freshly received packet, the read position will be just after the header and channel name
	Uint16 dataStartPosition = packet.GetPosition();

	red::ScopedLock< red::Mutex > lock( m_listenerLock );

	for ( auto& listener : m_listeners )
	{
		packet.SetPosition( dataStartPosition );
		listener->OnPacketReceived( m_name, packet );
	}
}

void red::Network::Channel::RegisterListener( ChannelListener* listener )
{
	red::ScopedLock< red::Mutex > lock( m_listenerLock );
	m_listeners.PushBack( listener );
}

void red::Network::Channel::UnregisterListener( ChannelListener* listener )
{
	red::ScopedLock< red::Mutex > lock( m_listenerLock );
	m_listeners.Remove( listener );
}

void red::Network::Channel::RegisterDestination( Socket* socket )
{
	red::alg::PushBackUnique( m_destinations, socket );
}

void red::Network::Channel::UnregisterDestination( Socket* socket )
{
	m_destinations.Remove( socket );
}

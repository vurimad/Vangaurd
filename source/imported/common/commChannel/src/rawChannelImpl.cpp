#include "build.h"

#include "rawChannelImpl.h"

namespace comm
{
	RawResponseImpl::RawResponseImpl( const ResponseDest& responseDest )
		: m_network( responseDest.m_network )
		, m_connectionID( responseDest.m_remoteConnectionID )
	{
	}

	void RawResponseImpl::Send( red::UniqueBuffer buffer )
	{
		m_network.Send( m_connectionID, red::Network::Utils::Packet( std::move( buffer ) ) );
	}

	RawChannelImpl::RawChannelImpl( red::UniquePtr< IChannelConnection > connection, const String& name )
		: m_connection( std::move( connection ) )
		, m_name( name )
	{
		m_connection->SetReceiveCallback( [this] ( ChannelData packet, const ResponseDest& responseDest ) -> Bool {
			return OnPacket( std::move( packet ), responseDest );
		});
	}

	RawChannelImpl::~RawChannelImpl()
	{
		RED_ASSERT( m_listeners.Empty() );
	}

	bool RawChannelImpl::IsOpen() const
	{
		return m_connection->IsOpened();
	}

	bool RawChannelImpl::IsConnected() const
	{
		return m_connection->IsConnected();
	}

	void RawChannelImpl::Send( red::UniqueBuffer buffer )
	{
		m_connection->Send( red::Network::Utils::Packet( std::move( buffer ) ) );
	}

	Bool RawChannelImpl::OnPacket( ChannelData packet, const ResponseDest& responseDest )
	{
		RED_SCOPE_LOCK( m_listenersLock );

		ResponseHandle response = red::CreateUniquePtr<RawResponseImpl>( responseDest );
		for ( auto& listener : m_listeners )
		{
			if ( listener->OnMessage( packet.GetBuffer(), response ) )
				return true;
		}

		return false;
	}

	void RawChannelImpl::RegisterListener( IRawMessageListener* listener )
	{
		RED_SCOPE_LOCK( m_listenersLock );

		RED_ASSERT( listener );
		RED_ASSERT( !m_listeners.Exist( listener ) );

		m_listeners.PushBack( listener );
	}

	void RawChannelImpl::UnregisterListener( IRawMessageListener* listener )
	{
		RED_SCOPE_LOCK( m_listenersLock );

		RED_ASSERT( listener );
		RED_ASSERT( m_listeners.Exist( listener ) );

		m_listeners.Remove( listener );
	}

	const String& RawChannelImpl::GetName() const
	{
		return m_name;
	}
}
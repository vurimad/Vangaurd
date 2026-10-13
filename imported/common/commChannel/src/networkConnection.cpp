/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "networkConnection.h"
#include "../../redNetwork/include/rawTcpManager.h"
#include "../../redNetwork/include/packetNetworkManager.h"
#include "../../commChannel/include/channelFactory.h"

namespace comm
{
	//////////////////////////////////////////////////////////////////////////
	// NetworkConnection
	//////////////////////////////////////////////////////////////////////////
	NetworkConnection::NetworkConnection( const red::String& name, const red::String& displayName ) 
		: m_name( name )
		, m_displayName( displayName )
	{
	}

	NetworkConnection::~NetworkConnection()
	{
		RED_ASSERT(!m_channelFactory);
		RED_ASSERT(!m_network);
	}

	Bool NetworkConnection::Initialize()
	{
		// initialize network for channel communication.
		m_network = red::CreateUniquePtr< red::Network::PacketNetworkManager > ( red::CreateUniquePtr< red::Network::RawTcpManager >() );
		if( !m_network->Initialize() )
		{
			RED_LOG_ERROR( "Failed to initialize network for channel communication." );
			return false;
		}

		// create channel factory
		m_channelFactory = red::CreateUniquePtr< comm::ChannelFactory > ( *m_network );
		return true;
	}

	void NetworkConnection::Shutdown()
	{
		// destroy factory
		m_channelFactory.Reset();

		if ( m_network )
		{
			m_network->Shutdown();
			m_network.Reset();
		}
	}
}
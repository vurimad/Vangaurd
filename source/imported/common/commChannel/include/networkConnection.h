/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redNetwork/include/packetNetworkManager.h"

namespace comm
{
	class ChannelFactory;

	//////////////////////////////////////////////////////////////////////////
	// NetworkConnection
	//////////////////////////////////////////////////////////////////////////
	class COMMCHANNEL_API NetworkConnection
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		NetworkConnection( const red::String& name, const red::String& displayName );
		~NetworkConnection();

		/// Get name
		const red::String& GetName() const { return m_name; }

		/// Get name to display
		const red::String& GetDisplayName() const { return m_displayName; }

		/// Initialize connection
		Bool Initialize();

		/// Shutdown connection
		void Shutdown();

		// Get channel factory. Should be used to add new channels.
		RED_INLINE comm::ChannelFactory& GetChannelFactory() const { RED_FATAL_ASSERT( m_channelFactory );  return *m_channelFactory; }

	private:
		red::String												m_name;						//@ application name
		red::String												m_displayName;				//@ name to display
		red::UniquePtr< red::Network::IPacketNetworkManager >	m_network;					//@ Packet network for channel communication
		red::UniquePtr< comm::ChannelFactory >					m_channelFactory;			//@ channel factory
	};
}

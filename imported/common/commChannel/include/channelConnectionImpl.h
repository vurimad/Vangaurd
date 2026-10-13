/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "channelConnection.h"

namespace comm
{
	class ChannelConnection : public IChannelConnection, public red::Network::IPacketNetworkConnectionListener
	{
	public:
		ChannelConnection( red::Network::IPacketNetworkManager& network );
		virtual ~ChannelConnection();

		Bool StartListening( const red::Network::Utils::CommAddress& address, Bool reuseAddress = false );
		Bool OpenConnection( const red::Network::Utils::CommAddress& address );

		RED_INLINE Bool HasConnection() const { return m_connectionId != 0; }
		RED_INLINE Bool IsListening() const { return m_listenerId != 0; }

		// Packet listener interface implementation
		virtual void OnListenerClosed( red::Network::PacketListenerID listenerId ) override;
		virtual void OnConnectionClosed( red::Network::PacketConnectionID connectionId ) override;
		virtual Bool OnPacket( ChannelData packet, red::Network::PacketConnectionID remoteConnectionID ) override;
		virtual void OnConnection( red::Network::PacketConnectionID connectionId, red::Network::ConnectionDirection direction ) override;

		virtual Bool IsOpened() const override { return HasConnection() || IsListening(); }
		virtual Bool IsConnected() const override { return HasConnection(); }
		virtual void Send( ChannelData data );
		virtual void SetReceiveCallback( OnDataReceivedCallback callback ) override { m_callback = callback; }

	private:
		red::Network::IPacketNetworkManager& m_network;
		red::Network::PacketConnectionID m_connectionId;
		red::Network::PacketListenerID m_listenerId;
		OnDataReceivedCallback m_callback;
	};
}

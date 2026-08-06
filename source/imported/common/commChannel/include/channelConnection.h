/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redNetwork/include/packetNetworkInterfaces.h"

namespace comm
{
	struct ResponseDest
	{
		red::Network::IPacketNetworkManager& m_network;
		red::Network::PacketConnectionID m_remoteConnectionID;
	};

	// Callback on data received from connection
	using ChannelData = red::Network::Utils::Packet;
	using OnDataReceivedCallback = std::function< Bool( ChannelData, const ResponseDest& responseDest )> ;

	// Internal interface for channel connections (TCP, Shared Memory etc.)
	class IChannelConnection
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~IChannelConnection() {}

		virtual Bool IsOpened() const = 0;
		virtual Bool IsConnected() const = 0;
		virtual void Send( ChannelData data ) = 0;

		// There can be only one listener per IChannelConnection object
		virtual void SetReceiveCallback( OnDataReceivedCallback callback ) = 0;
	};
}
